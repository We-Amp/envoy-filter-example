#include "exe/client.h"

#include <chrono>
#include <iostream>
#include <memory>

#include "ares.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "exe/benchmark_client.h"
#include "exe/rate_limiter.h"
#include "exe/sequencer.h"

using namespace Envoy;

namespace Nighthawk {

namespace {

// returns 0 on failure. returns the number of HW CPU's
// that the current thread has affinity with.
// TODO(oschaaf): mull over what to do w/regard to hyperthreading.
uint32_t determine_cpus_with_affinity() {
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

  uint32_t concurrency = determine_cpus_with_affinity();
  if (concurrency == 0) {
    ENVOY_LOG(warn, "Failed to determine the number of cpus with affinity to our thread.");
    concurrency = std::thread::hardware_concurrency();
  }
  ENVOY_LOG(info, "CPUs with affinity: {}. Running {} event loops", concurrency, concurrency);

  // We're going to fire up #concurrency benchmark loops and wait for them to complete.
  std::vector<Thread::ThreadPtr> threads;

  for (uint32_t i = 0; i < concurrency; i++) {
    auto thread = thread_factory.createThread([&]() {
      auto store = std::make_unique<Stats::IsolatedStoreImpl>();
      auto api = std::make_unique<Envoy::Api::Impl>(
          std::chrono::milliseconds(1000) /*flush interval*/, thread_factory, *store);
      auto dispatcher = api->allocateDispatcher(*time_system_);

      // TODO(oschaaf): not here.
      Envoy::ThreadLocal::InstanceImpl tls;
      Envoy::Runtime::RandomGeneratorImpl generator;
      Envoy::Runtime::LoaderImpl runtime(generator, *store, tls);
      Envoy::Event::RealTimeSystem time_system;

      // TODO(oschaaf): refactor header setup. euse uri parsing.
      Envoy::Http::HeaderMapImplPtr request_headers =
          std::make_unique<Envoy::Http::HeaderMapImpl>();
      request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);
      request_headers->insertPath().value(std::string("/"));
      request_headers->insertHost().value(std::string("127.0.0.1"));
      request_headers->insertScheme().value(Envoy::Http::Headers::get().SchemeValues.Http);

      // TODO(oschaaf): Fix options_.timeout()
      auto client =
          std::make_unique<BenchmarkHttpClient>(*dispatcher, *store, time_system, options_.uri(),
                                                std::move(request_headers), options_.h2());

      client->initialize(runtime);

      std::function<bool(std::function<void()>)> f =
          std::bind(&BenchmarkHttpClient::tryStartOne, client.get(), std::placeholders::_1);

      std::unique_ptr<RateLimiter> rate_limiter = std::make_unique<LinearRateLimiter>(
          options_.connections(),
          std::chrono::microseconds((1000 * 1000) / options_.requests_per_second()));
      Sequencer sequencer(*dispatcher, time_system, *rate_limiter, f,
                          std::chrono::seconds(options_.duration()));
      sequencer.start();
      sequencer.waitForCompletion();
      client.reset();
      // TODO(oschaaf): shouldn't be doing this here.
      tls.shutdownGlobalThreading();
    });
    threads.push_back(std::move(thread));
  }

  for (auto& t : threads) {
    t->join();
  }
  // TODO(oschaaf): collect and merge latency statistics from the threads

  return true;
}

} // namespace Nighthawk
