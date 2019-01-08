#pragma once

#include "common/common/logger.h"
#include "common/event/real_time_system.h"

namespace Nighthawk {

class RateLimiter : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  RateLimiter() {}
  virtual ~RateLimiter() {}
  virtual bool tryAcquireOne() PURE;
};

// Simple rate limiter that will allow acquiring at a linear pace.
// The average rate is computed over a timeframe that starts at
// instantiation.
class LinearRateLimiter : public RateLimiter {
public:
  LinearRateLimiter(std::chrono::microseconds pace)
      : RateLimiter(), acquireable_count_(0), acquired_count_(0), pace_(pace),
        started_at_(std::chrono::high_resolution_clock::now()) {}

  virtual bool tryAcquireOne() override;

private:
  int64_t acquireable_count_;
  uint64_t acquired_count_;
  std::chrono::microseconds pace_;
  std::chrono::time_point<std::chrono::high_resolution_clock> started_at_;
};

} // namespace Nighthawk