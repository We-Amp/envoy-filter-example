#pragma once

#include "common/common/logger.h"

#include "envoy/event/timer.h"
#include "envoy/network/address.h"
#include "envoy/stats/store.h"

#include "exe/client_options_impl.h"
#include "exe/stream_decoder.h"

namespace Nighthawk {

class StreamingStats {
public:
  StreamingStats();
  void clear();
  void push(int64_t x);
  int64_t num_values() const;
  double mean() const;
  double variance() const;
  double stdev() const;
  double skewness() const;
  double kurtosis() const;

  friend StreamingStats operator+(const StreamingStats a, const StreamingStats b);
  StreamingStats& operator+=(const StreamingStats& rhs);

private:
  int64_t n;
  double M1, M2, M3, M4;
};

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

private:
  void configureComponentLogLevels();
};

} // namespace Nighthawk
