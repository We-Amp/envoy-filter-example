#include "exe/client.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>

#include "ares.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/real_time_system.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "exe/benchmark_client.h"
#include "exe/rate_limiter.h"
#include "exe/sequencer.h"

using namespace std::chrono_literals;
using namespace Envoy;

namespace Nighthawk {

namespace {

// returns 0 on failure. returns the number of HW CPU's
// that the current thread has affinity with.
// TODO(oschaaf): mull over what to do w/regard to hyperthreading.
uint32_t determine_cpu_cores_with_affinity() {
  uint32_t concurrency = 0;
  int i;
  pthread_t thread = pthread_self();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  i = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (i != 0) {
    return 0;
  } else {
    for (i = 0; i < CPU_SETSIZE; i++) {
      if (CPU_ISSET(i, &cpuset)) {
        concurrency++;
      }
    }
  }
  return concurrency;
}

} // namespace

ClientMain::ClientMain(int argc, const char* const* argv) : ClientMain(OptionsImpl(argc, argv)) {}

ClientMain::ClientMain(OptionsImpl options)
    : options_(options), time_system_(std::make_unique<Envoy::Event::RealTimeSystem>()) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  configureComponentLogLevels();
}

ClientMain::~ClientMain() { ares_library_cleanup(); }

void ClientMain::configureComponentLogLevels() {
  // We rely on Envoy's logging infra.
  // TODO(oschaaf): Add options to tweak the log level of the various log tags
  // that are available.
  Logger::Registry::setLogLevel(spdlog::level::info);
  Logger::Logger* logger_to_change = Envoy::Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::info);
}

bool ClientMain::run() {
  // TODO(oschaaf): platform specificity need addressing.
  auto thread_factory = Thread::ThreadFactoryImplPosix();

  Thread::MutexBasicLockable log_lock;
  auto logging_context = std::make_unique<Logger::Context>(
      spdlog::level::info, Logger::Logger::DEFAULT_LOG_FORMAT, log_lock);

  uint32_t cpu_cores_with_affinity = determine_cpu_cores_with_affinity();
  if (cpu_cores_with_affinity == 0) {
    ENVOY_LOG(warn, "Failed to determine the number of cpus with affinity to our thread.");
    cpu_cores_with_affinity = std::thread::hardware_concurrency();
  }

  bool autoscale = options_.concurrency() == "auto";
  // TODO(oschaaf): concurrency is a string option, this needs more sanity checking.
  // The default for concurrency is one, in which case these warnings cannot show up.
  // TODO(oschaaf): Maybe, in the case where the concurrency flag is left out, but
  // affinity is set / we don't have affinity with all cores, we should default to autoscale.
  // (e.g. we are called via taskset).
  uint32_t concurrency = autoscale ? cpu_cores_with_affinity : std::stoi(options_.concurrency());

  // We're going to fire up #concurrency benchmark loops and wait for them to complete.
  std::vector<Thread::ThreadPtr> threads;
  std::vector<std::vector<uint64_t>> global_results;
  // TODO(oschaaf): rework this. We pre-allocate the global results vector
  // to avoid reallocations which would crash us.
  // Wire up a proper stats sink.
  global_results.reserve(concurrency);

  // TODO(oschaaf): as we spread the tasks accross workers, numbers may not always align
  // well. We may want to warn about that, or fix it so that we do reach the ancipated amount
  // of request/responses in the happy flow (~rps * duration in seconds).
  // TODO(oschaaf): we actually may not want to do this, consider removing this
  // and delegate adding up the numbers to the user as concurrency increases.
  // Perhaps default to a single event loop when we do.
  uint64_t per_thread_connections = std::max(options_.connections() / concurrency, 1UL);
  uint64_t per_thread_rps = std::max(options_.requests_per_second() / concurrency, 1UL);

  if (autoscale) {
    ENVOY_LOG(info, "Detected {} (v)CPUs with affinity..", cpu_cores_with_affinity);
  }

  ENVOY_LOG(info, "Starting {} threads / event loops.", concurrency);

  if ((options_.connections() % concurrency) != 0) {
    ENVOY_LOG(warn, "The specified number of connections did not align to the concurrency level.");
  }
  if ((options_.requests_per_second() % concurrency) != 0) {
    ENVOY_LOG(warn, "The specified queries per seconds did not align to the concurrency level.");
  }

  ENVOY_LOG(info,
            "Using {} connections ({} total) and targetting {} requests per second ({} total).",
            per_thread_connections, (per_thread_connections * concurrency), per_thread_rps,
            (per_thread_rps * concurrency));

  for (uint32_t i = 0; i < concurrency; i++) {
    global_results.push_back(std::vector<uint64_t>());
    std::vector<uint64_t>& results = global_results.at(i);
    // TODO(oschaaf): refactor stats sink.
    results.reserve(options_.duration().count() * per_thread_rps);

    auto thread = thread_factory.createThread([&]() {
      auto store = std::make_unique<Stats::IsolatedStoreImpl>();
      auto api =
          std::make_unique<Envoy::Api::Impl>(1000ms /*flush interval*/, thread_factory, *store);
      auto dispatcher = api->allocateDispatcher(*time_system_);

      // TODO(oschaaf): not here.
      Envoy::ThreadLocal::InstanceImpl tls;
      Envoy::Runtime::RandomGeneratorImpl generator;
      Envoy::Runtime::LoaderImpl runtime(generator, *store, tls);
      Envoy::Event::RealTimeSystem time_system;

      Envoy::Http::HeaderMapImplPtr request_headers =
          std::make_unique<Envoy::Http::HeaderMapImpl>();
      request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);

      // TODO(oschaaf): Fix options_.timeout()
      auto client =
          std::make_unique<BenchmarkHttpClient>(*dispatcher, *store, time_system, options_.uri(),
                                                std::move(request_headers), options_.h2());
      client->set_connection_timeout(options_.timeout());
      client->set_connection_limit(options_.connections());

      client->initialize(runtime);

      // With the linear rate limiter, we run an open-loop test, where we initiate new
      // calls regardless of the number of comletions we observe keeping up.
      LinearRateLimiter rate_limiter(time_system, 1000000us / per_thread_rps);
      std::function<bool(std::function<void()>)> f =
          std::bind(&BenchmarkHttpClient::tryStartOne, client.get(), std::placeholders::_1);
      Sequencer sequencer(*dispatcher, time_system, rate_limiter, f,
                          std::chrono::seconds(options_.duration()));

      sequencer.set_latency_callback([&results](std::chrono::nanoseconds latency) {
        ASSERT(latency.count() > 0);
        results.push_back(latency.count());
      });

      sequencer.start();
      sequencer.waitForCompletion();
      ENVOY_LOG(info,
                "Connection: connect failures: {}, overflow failures: {} . Protocol: good {} / bad "
                "{} / reset {}",
                client->pool_connect_failures(), client->pool_overflow_failures(),
                client->http_good_response_count(), client->http_bad_response_count(),
                client->stream_reset_count());
      client.reset();
      // TODO(oschaaf): shouldn't be doing this here.
      tls.shutdownGlobalThreading();
    });
    threads.push_back(std::move(thread));
  }

  for (auto& t : threads) {
    t->join();
  }

  // TODO(oschaaf): proper stats tracking/configuration
  std::ofstream myfile;
  myfile.open("res.txt");

  for (uint32_t i = 0; i < concurrency; i++) {
    std::vector<uint64_t>& results = global_results.at(i);
    // Remove first element, consider it a warmup call.
    // TODO(oschaaf):
    if (!results.empty()) {
      results.erase(results.begin());
    }
    for (int r : results) {
      myfile << (r / 1000) << "\n";
    }
    double average = std::accumulate(results.begin(), results.end(), 0.0) / results.size();
    auto minmax = std::minmax_element(results.begin(), results.end());
    ENVOY_LOG(info, "worker {}: avg latency {}us over {} callbacks. Min/Max: {}/{}.", i,
              (average / 1000), results.size(), (*(minmax.first) / 1000),
              (*(minmax.second) / 1000));
  }

  myfile.close();

  return true;
}

} // namespace Nighthawk
