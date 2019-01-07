#pragma once

#include "common/common/logger.h"
#include "common/event/real_time_system.h"

namespace Nighthawk {

class RateLimiter : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  RateLimiter(uint64_t max_slots) : max_slots_(max_slots), available_slots_(max_slots) {}
  virtual ~RateLimiter() {}

  virtual bool tryAcquireOne() {
    recoverSlots();
    if (available_slots_ > 0) {
      available_slots_--;
      return true;
    }
    return false;
  }

protected:
  virtual void recoverSlots() PURE;

  uint64_t max_slots_;
  uint64_t available_slots_;
};

// Simple rate limiter that will allow acquiring at a linear pace.
// The average rate is computed over a timeframe that starts at
// its instantiation.
class LinearRateLimiter : public RateLimiter {
public:
  LinearRateLimiter(uint64_t max_slots, std::chrono::microseconds slot_recovery_time)
      : RateLimiter(max_slots), total_slots_acquired_(0), slot_recovery_time_(slot_recovery_time),
        started_at_(std::chrono::high_resolution_clock::now()) {}

  virtual void recoverSlots() override {
    auto elapsed_since_start = std::chrono::high_resolution_clock::now() - started_at_;
    int64_t slots_to_add = (elapsed_since_start / slot_recovery_time_) - total_slots_acquired_;
    if (slots_to_add > 0) {
      int64_t slots_left = slots_to_add - (max_slots_ - available_slots_);
      available_slots_ += slots_to_add - slots_left;
      total_slots_acquired_ += slots_to_add - slots_left;
    }
  }

private:
  uint64_t total_slots_acquired_;
  std::chrono::microseconds slot_recovery_time_;
  std::chrono::time_point<std::chrono::high_resolution_clock> started_at_;
};

} // namespace Nighthawk