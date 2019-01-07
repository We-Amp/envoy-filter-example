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

#include "exe/benchmark_loop.h"

using namespace Envoy;

namespace Nighthawk {

namespace {

// returns 0 on failure. returns the number of HW CPU's
// that the current thread has affinity with.
// TODO(oschaaf): hyperthreading: maybe not take that into
// account.
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
      HttpBenchmarkTimingLoop bml(*dispatcher, *store, *time_system_, thread_factory,
                                  options_.requests_per_second(), options_.duration(),
                                  options_.connections(), options_.timeout(), options_.uri(),
                                  options_.h2());
      // TODO(oschaaf): return values should be posted back.
      if (bml.start()) {
        bml.waitForCompletion();
        return true;
      }
      return false;
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
