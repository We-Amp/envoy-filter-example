#pragma once

#include "common/common/logger.h"
#include "envoy/event/timer.h"

namespace Nighthawk {

class RateLimiter : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  RateLimiter(Envoy::TimeSource& time_source) : time_source_(time_source) {}
  virtual ~RateLimiter() {}
  virtual bool tryAcquireOne() PURE;

protected:
  Envoy::TimeSource& time_source_;
};

// Simple rate limiter that will allow acquiring at a linear pace.
// The average rate is computed over a timeframe that starts at
// instantiation.
class LinearRateLimiter : public RateLimiter {
public:
  LinearRateLimiter(Envoy::TimeSource& time_source, std::chrono::microseconds pace)
      : RateLimiter(time_source), acquireable_count_(0), acquired_count_(0), pace_(pace),
        started_at_(time_source_.monotonicTime()) {}

  virtual bool tryAcquireOne() override;

private:
  int64_t acquireable_count_;
  uint64_t acquired_count_;
  const std::chrono::microseconds pace_;
  const Envoy::MonotonicTime started_at_;
};

} // namespace Nighthawk