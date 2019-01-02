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

ClientMain::ClientMain(int argc, const char* const* argv) : ClientMain(OptionsImpl(argc, argv)) {}

ClientMain::ClientMain(OptionsImpl options) : options_(options) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  configureComponentLogLevels();
}

ClientMain::~ClientMain() { ares_library_cleanup(); }

void ClientMain::configureComponentLogLevels() {
  // We rely on Envoy's logging infra.
  // TODO(oschaaf): Add options to tweak the log level of the various log tags
  // that are available.
  Logger::Registry::setLogLevel(spdlog::level::trace);
  Logger::Logger* logger_to_change = Envoy::Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::trace);
}

bool ClientMain::run() {
  auto store = std::make_unique<Stats::IsolatedStoreImpl>();
  // TODO(oschaaf): platform specificity need addressing.
  auto thread_factory = Thread::ThreadFactoryImplPosix();
  auto api = std::make_unique<Envoy::Api::Impl>(std::chrono::milliseconds(1000) /*flush interval*/,
                                                thread_factory, *store);
  auto dispatcher = api->allocateDispatcher(real_time_system_);
  HttpBenchmarkTimingLoop bml(*dispatcher, *store, real_time_system_, thread_factory);
  bml.start();
  bml.waitForCompletion();
  // Benchmarker benchmarker(*dispatcher, options_.connections(), options_.requests_per_second(),
  //                        options_.duration(), Headers::get().MethodValues.Get, options_.uri());
  // auto dns_resolver = dispatcher->createDnsResolver({});
  // benchmarker.run(dns_resolver);
  return true;
}

} // namespace Nighthawk
