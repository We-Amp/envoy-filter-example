#pragma once

#include "common/common/logger.h"

#include "envoy/event/timer.h"
#include "envoy/network/address.h"
#include "envoy/stats/store.h"

#include "nighthawk/client/options.h"
#include "nighthawk/client/worker.h"

#include "client/stream_decoder.h"

namespace Nighthawk {

class ClientMain : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  ClientMain(int argc, const char* const* argv);
  ClientMain(Client::OptionsPtr options);
  ~ClientMain();

  bool run();

private:
  Client::OptionsPtr options_;
  std::unique_ptr<Envoy::Event::TimeSystem> time_system_;
  std::unique_ptr<Envoy::Logger::Context> logging_context_;
  Envoy::Network::Address::InstanceConstSharedPtr target_address_;
  Client::WorkerPtr worker_;
  void configureComponentLogLevels(spdlog::level::level_enum level);
};

} // namespace Nighthawk
