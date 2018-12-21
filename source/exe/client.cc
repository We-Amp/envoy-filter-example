#include "exe/client.h"

#include <chrono>
#include <iostream>
#include <memory>

#include "ares.h"

#include "absl/strings/str_split.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/http/headers.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "exe/benchmarker.h"

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
  Logger::Registry::setLogLevel(spdlog::level::warn);
  Logger::Logger* logger_to_change = Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::info);
}

bool ClientMain::run() {
  Stats::IsolatedStoreImpl stats;
  Thread::ThreadFactoryImplPosix thread_factory;
  auto api = new Envoy::Api::Impl(std::chrono::milliseconds(100) /*flush interval*/, thread_factory,
                                  stats);
  auto dispatcher = api->allocateDispatcher(real_time_system_);
  // dispatcher = Event::DispatcherPtr{new Event::DispatcherImpl(real_time_system_)};
  Benchmarker benchmarker(*dispatcher, options_.connections(), options_.requests_per_second(),
                          options_.duration(), Headers::get().MethodValues.Get, options_.uri());
  auto dns_resolver = dispatcher->createDnsResolver({});
  benchmarker.run(dns_resolver);
  return true;
}

} // namespace Nighthawk
