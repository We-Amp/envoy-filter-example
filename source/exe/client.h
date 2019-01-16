#pragma once

#include "common/common/logger.h"

#include "envoy/event/timer.h"
#include "envoy/network/address.h"
#include "envoy/stats/store.h"

#include "nighthawk/client/worker.h"

// TODO(oschaaf): change to use interfaces here.
#include "exe/client_options_impl.h"
#include "exe/stream_decoder.h"

namespace Nighthawk {

class ClientMain : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  ClientMain(int argc, const char* const* argv);
  ClientMain(OptionsImpl options);
  ~ClientMain();

  bool run();

private:
  Nighthawk::OptionsImpl options_;
  std::unique_ptr<Envoy::Event::TimeSystem> time_system_;
  std::unique_ptr<Envoy::Logger::Context> logging_context_;
  Envoy::Network::Address::InstanceConstSharedPtr target_address_;
  Client::WorkerPtr worker_;
  void configureComponentLogLevels(spdlog::level::level_enum level);
};

} // namespace Nighthawk
