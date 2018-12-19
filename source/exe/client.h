#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "common/common/thread.h"
#include "common/event/real_time_system.h"
#include "exe/client_options_impl.h"

using namespace Envoy;

namespace Nighthawk {

class ClientMain {
public:
  ClientMain(int argc, const char* const* argv);
  ClientMain(OptionsImpl options);
  ~ClientMain();

  bool run();

protected:
  Nighthawk::OptionsImpl options_;
  Event::RealTimeSystem real_time_system_;
  std::unique_ptr<Logger::Context> logging_context_;

private:
  void configureComponentLogLevels();
};

} // namespace Nighthawk
