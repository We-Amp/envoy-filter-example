#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "common/common/thread.h"
#include "common/event/real_time_system.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

//#include "server/options_impl.h
#include "exe/benchmarking_options_impl.h"
//#include "exe/service.h"
//#include "server/server.h"
//#include "server/test_hooks.h"

/*
#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#include "exe/terminate_handler.h"
#endif
*/

using namespace Envoy;

namespace Nighthawk {

class MainCommonBase {
public:
  // Consumer must guarantee that all passed references are alive until this object is
  // destructed.
  MainCommonBase(OptionsImpl& options, Event::TimeSystem& time_system,
                 Thread::ThreadFactory& thread_factory);
  ~MainCommonBase();

  bool run();

protected:
  Nighthawk::OptionsImpl& options_;

  Thread::ThreadFactory& thread_factory_;

  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> stats_store_;
  std::unique_ptr<Logger::Context> logging_context_;
  // std::unique_ptr<Service::InstanceImpl> service_;
  Event::TimeSystem& time_system_;

private:
  void configureComponentLogLevels();
};

// TODO(jmarantz): consider removing this class; I think it'd be more useful to
// go through MainCommonBase directly.
class MainCommon {
public:
  MainCommon(int argc, const char* const* argv);
  bool run() { return base_.run(); }

private:
  /*
  #ifdef ENVOY_HANDLE_SIGNALS
    Envoy::SignalAction handle_sigs;
    Envoy::TerminateHandler log_on_terminate;
  #endif
  */
  OptionsImpl options_;
  Event::RealTimeSystem real_time_system_;
  // DefaultTestHooks default_test_hooks_;
  Thread::ThreadFactoryImpl thread_factory_;
  MainCommonBase base_;
};

} // namespace Nighthawk
