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

StreamingStats::StreamingStats() { clear(); }

void StreamingStats::clear() {
  n = 0;
  M1 = M2 = M3 = M4 = 0.0;
}

void StreamingStats::push(int64_t x) {
  double delta, delta_n, delta_n2, term1;

  int64_t n1 = n;
  n++;
  delta = x - M1;
  delta_n = delta / n;
  delta_n2 = delta_n * delta_n;
  term1 = delta * delta_n * n1;
  M1 += delta_n;
  M4 += term1 * delta_n2 * (n * n - 3 * n + 3) + 6 * delta_n2 * M2 - 4 * delta_n * M3;
  M3 += term1 * delta_n * (n - 2) - 3 * delta_n * M2;
  M2 += term1;
}

int64_t StreamingStats::num_values() const { return n; }

double StreamingStats::mean() const { return M1; }

double StreamingStats::variance() const { return M2 / (n - 1.0); }

double StreamingStats::stdev() const { return sqrt(variance()); }

double StreamingStats::skewness() const { return sqrt(double(n)) * M3 / pow(M2, 1.5); }

double StreamingStats::kurtosis() const { return double(n) * M4 / (M2 * M2) - 3.0; }

StreamingStats operator+(const StreamingStats a, const StreamingStats b) {
  StreamingStats combined;

  combined.n = a.n + b.n;

  double delta = b.M1 - a.M1;
  double delta2 = delta * delta;
  double delta3 = delta * delta2;
  double delta4 = delta2 * delta2;

  combined.M1 = (a.n * a.M1 + b.n * b.M1) / combined.n;

  combined.M2 = a.M2 + b.M2 + delta2 * a.n * b.n / combined.n;

  combined.M3 = a.M3 + b.M3 + delta3 * a.n * b.n * (a.n - b.n) / (combined.n * combined.n);
  combined.M3 += 3.0 * delta * (a.n * b.M2 - b.n * a.M2) / combined.n;

  combined.M4 = a.M4 + b.M4 +
                delta4 * a.n * b.n * (a.n * a.n - a.n * b.n + b.n * b.n) /
                    (combined.n * combined.n * combined.n);
  combined.M4 += 6.0 * delta2 * (a.n * a.n * b.M2 + b.n * b.n * a.M2) / (combined.n * combined.n) +
                 4.0 * delta * (a.n * b.M3 - b.n * a.M3) / combined.n;

  return combined;
}

StreamingStats& StreamingStats::operator+=(const StreamingStats& rhs) {
  StreamingStats combined = *this + rhs;
  *this = combined;
  return *this;
}

ClientMain::ClientMain(int argc, const char* const* argv) : ClientMain(OptionsImpl(argc, argv)) {}

ClientMain::ClientMain(OptionsImpl options)
    : options_(options), time_system_(std::make_unique<Envoy::Event::RealTimeSystem>()) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Envoy::Event::Libevent::Global::initialize();
  configureComponentLogLevels();
}

ClientMain::~ClientMain() { ares_library_cleanup(); }

void ClientMain::configureComponentLogLevels() {
  // We rely on Envoy's logging infra.
  // TODO(oschaaf): Add options to tweak the log level of the various log tags
  // that are available.
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  Envoy::Logger::Logger* logger_to_change = Envoy::Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::info);
}

bool ClientMain::run() {
  // TODO(oschaaf): platform specificity need addressing.
  auto thread_factory = Envoy::Thread::ThreadFactoryImplPosix();

  Envoy::Thread::MutexBasicLockable log_lock;
  auto logging_context = std::make_unique<Envoy::Logger::Context>(
      spdlog::level::info, Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, log_lock);

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
  std::vector<Envoy::Thread::ThreadPtr> threads;
  std::vector<std::vector<uint64_t>> global_results;
  // TODO(oschaaf): rework this. We pre-allocate the global results vector
  // to avoid reallocations which would crash us.
  // Wire up a proper stats sink.
  global_results.reserve(concurrency);

  if (autoscale) {
    ENVOY_LOG(info, "Detected {} (v)CPUs with affinity..", cpu_cores_with_affinity);
  }

  ENVOY_LOG(info, "Starting {} threads / event loops.", concurrency);
  ENVOY_LOG(info, "Global targets: {} connections and {} calls per second.",
            options_.connections() * concurrency, options_.requests_per_second() * concurrency);

  if (concurrency > 1) {
    ENVOY_LOG(info, "   (Per-worker targets: {} connections and {} calls per second)",
              options_.connections(), options_.requests_per_second());
  }

  for (uint32_t i = 0; i < concurrency; i++) {
    global_results.push_back(std::vector<uint64_t>());
    std::vector<uint64_t>& results = global_results.at(i);
    // TODO(oschaaf): get us a stats sink.

    results.reserve(options_.duration().count() * options_.requests_per_second());

    auto thread = thread_factory.createThread([&, i]() {
      auto store = std::make_unique<Envoy::Stats::IsolatedStoreImpl>();
      auto api =
          std::make_unique<Envoy::Api::Impl>(1000ms /*flush interval*/, thread_factory, *store);
      auto dispatcher = api->allocateDispatcher(*time_system_);
      StreamingStats streaming_stats;

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

      // one to warm up.
      client->tryStartOne([&dispatcher] { dispatcher->exit(); });
      dispatcher->run(Envoy::Event::Dispatcher::RunType::Block);

      // With the linear rate limiter, we run an open-loop test, where we initiate new
      // calls regardless of the number of comletions we observe keeping up.
      LinearRateLimiter rate_limiter(time_system, 1000000000ns / options_.requests_per_second());
      SequencerTarget f =
          std::bind(&BenchmarkHttpClient::tryStartOne, client.get(), std::placeholders::_1);
      Sequencer sequencer(*dispatcher, time_system, rate_limiter, f, options_.duration(),
                          options_.timeout());

      sequencer.set_latency_callback([&results, i, this, &streaming_stats, &client,
                                      &sequencer](std::chrono::nanoseconds latency) {
        ASSERT(latency.count() > 0);
        results.push_back(latency.count());
        streaming_stats.push(latency.count());

        // Report results from the first worker about every one second.
        // TODO(oschaaf): we should only do this in explicit verbose mode because
        // of introducing locks, probably.
        // TODO(oschaaf): failures aren't ending up in this callback, so they will
        // influence timing of this happening.
        if (((results.size() % options_.requests_per_second()) == 0) && i == 0) {
          ENVOY_LOG(info,
                    "#{} completions/sec. mean: {}+/-{}us. skewness: {}, kurtosis: {}."
                    "pool connect failures: {}, overflow failures: {}. Replies: Good {}, Bad: "
                    "{}. Stream resets: {}.",
                    sequencer.completions_per_second(),
                    (static_cast<int64_t>(streaming_stats.mean())) / 1000,
                    (static_cast<int64_t>(streaming_stats.stdev())) / 1000,
                    streaming_stats.skewness(), streaming_stats.kurtosis(),
                    client->pool_connect_failures(), client->pool_overflow_failures(),
                    client->http_good_response_count(), client->http_bad_response_count(),
                    client->stream_reset_count());
        }
      });

      sequencer.start();
      sequencer.waitForCompletion();
      ENVOY_LOG(info,
                "pool connect failures: {}, overflow failures: {}. Replies: Good {}, Bad: "
                "{}. Stream resets: {}",
                client->pool_connect_failures(), client->pool_overflow_failures(),
                client->http_good_response_count(), client->http_bad_response_count(),
                client->stream_reset_count());
      // As we prevent pool overflow failures, we don't expect any.
      ASSERT(!client->pool_overflow_failures());
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
