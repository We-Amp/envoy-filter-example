#pragma once

#include "common/common/logger.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "exe/rate_limiter.h"

namespace Nighthawk {

// TODO(oschaaf): consider renaming this to BenchmarkTarget or some such.
typedef std::function<void(std::function<void()>)> SequencerTarget;

// TODO(oschaaf): consider renaming this to benchmarker some such.
class Sequencer : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  Sequencer(Envoy::Event::Dispatcher& dispatcher, Envoy::TimeSource& time_source,
            RateLimiter& rate_limiter, SequencerTarget& target, std::chrono::microseconds duration,
            std::chrono::microseconds grace_timeout);
  void start();
  void waitForCompletion();
  void set_latency_callback(std::function<void(std::chrono::nanoseconds)> latency_callback) {
    latency_callback_ = latency_callback;
  }

protected:
  void run(bool from_timer);
  void scheduleRun();

private:
  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::TimeSource& time_source_;
  Envoy::Event::TimerPtr timer_;
  RateLimiter& rate_limiter_;
  SequencerTarget& target_;
  std::chrono::microseconds duration_;
  std::chrono::microseconds grace_timeout_;

  Envoy::MonotonicTime start_;
  uint64_t targets_initiated_;
  uint64_t targets_completed_;
  std::function<void(const std::chrono::nanoseconds)> latency_callback_;
};

} // namespace Nighthawk