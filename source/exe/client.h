#pragma once

#include "common/common/logger.h"
#include "common/event/real_time_system.h"
#include "envoy/stats/store.h"

#include "exe/client_options_impl.h"
#include "exe/stream_decoder.h"

namespace Nighthawk {

class ClientMain {
public:
  ClientMain(int argc, const char* const* argv);
  ClientMain(OptionsImpl options);
  ~ClientMain();

  bool run();

protected:
  Nighthawk::OptionsImpl options_;

  // TODO(oschaaf): timesystem
  Envoy::Event::RealTimeSystem real_time_system_;
  std::unique_ptr<Envoy::Logger::Context> logging_context_;

private:
  void configureComponentLogLevels();
};

} // namespace Nighthawk
