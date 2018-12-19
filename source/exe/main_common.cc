#include "exe/main_common.h"

#include <iostream>
#include <memory>

#include "common/common/compiler_requirements.h"
#include "common/common/perf_annotation.h"
#include "common/event/libevent.h"
#include "common/network/utility.h"
//#include "common/stats/thread_local_store.h"

#include "common/network/raw_buffer_socket.h"
#include "common/upstream/upstream_impl.h"
//#include "server/config_validation/server.h"
//#include "server/drain_manager_impl.h"
//#include "server/hot_restart_nop_impl.h"
//#include "server/options_impl.h"
#include "server/proto_descriptors.h"
//#include "server/server.h"
//#include "server/test_hooks.h"

//#include "envoy/upstream/host_description.h"

#include "absl/strings/str_split.h"

#include "ares.h"

using namespace Envoy;

namespace Nighthawk {

MainCommonBase::MainCommonBase(OptionsImpl& options, Event::TimeSystem& time_system,
                               Thread::ThreadFactory& thread_factory)
    : options_(options), thread_factory_(thread_factory), time_system_(time_system) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  RELEASE_ASSERT(Envoy::Server::validateProtoDescriptors(), "");

  tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
  // Thread::BasicLockable& log_lock = restarter_->logLock();
  // Thread::BasicLockable& access_log_lock = restarter_->accessLogLock();
  // auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
  // logging_context_ =
  //    std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(), log_lock);

  configureComponentLogLevels();

  // stats_store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(options_.statsOptions(),
  //                                                            restarter_->statsAllocator());
}

MainCommonBase::~MainCommonBase() { ares_library_cleanup(); }

void MainCommonBase::configureComponentLogLevels() {
  Logger::Logger* logger_to_change = Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::info);
}

bool MainCommonBase::run() {
  // service_->run();
  return true;
}

MainCommon::MainCommon(int argc, const char* const* argv)
    : options_(argc, argv), base_(options_, real_time_system_, thread_factory_) {}

} // namespace Nighthawk
