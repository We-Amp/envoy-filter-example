#pragma once

#include "common/common/logger.h"
#include "common/event/real_time_system.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

namespace Nighthawk {

class RateLimiter : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  RateLimiter(uint64_t max_slots) : max_slots_(max_slots), slots_(max_slots) {}
  virtual ~RateLimiter() {}

  virtual bool tryAcquireOne() {
    recoverSlots();
    if (slots_ > 0) {
      slots_--;
      return true;
    }
    return false;
  }

  virtual void recoverSlots() PURE;

protected:
  uint64_t max_slots_;
  uint64_t slots_;
};

class LinearRateLimiter : public RateLimiter {
public:
  LinearRateLimiter(uint64_t max_slots, std::chrono::microseconds slot_recovery_time)
      : RateLimiter(max_slots), slot_recovery_time_(slot_recovery_time),
        last_checked_at_(std::chrono::high_resolution_clock::now()), overflow_count_(0) {}

  virtual void recoverSlots() override {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = now - last_checked_at_;
    auto to_add = elapsed / slot_recovery_time_;

    while (to_add-- > 0 && slots_ < max_slots_) {
      slots_++;
    }

    if (to_add) {
      ENVOY_LOG(warn, "Overflow detected in Linear Rate Limiter. Looks like the client is not able "
                      "to keep up.");
    }
    overflow_count_ += to_add;
    last_checked_at_ = now;
  }

private:
  std::chrono::microseconds slot_recovery_time_;
  std::chrono::time_point<std::chrono::high_resolution_clock> last_checked_at_;
  uint64_t overflow_count_;
};

} // namespace Nighthawk