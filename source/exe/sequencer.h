#pragma once

#include "common/common/logger.h"
#include "common/event/real_time_system.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "exe/rate_limiter.h"

namespace Nighthawk {

// TODO(oschaaf): consider renaming this to BenchmarkTarget or some such.
typedef std::function<bool(std::function<void()>)> SequencerTarget;

// TODO(oschaaf): consider renaming this to benchmarker some such.
class Sequencer : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  Sequencer(Envoy::Event::Dispatcher& dispatcher, Envoy::TimeSource& time_source,
            RateLimiter& rate_limiter, SequencerTarget&);
  bool start();
  void waitForCompletion();

protected:
  void run();

private:
  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::TimeSource& time_source_;
  RateLimiter& rate_limiter_;
  SequencerTarget& target_;
};

} // namespace Nighthawk