#include "exe/rate_limiter.h"

namespace Nighthawk {

bool RateLimiter::tryAcquireOne() {
  recoverSlots();
  if (available_slots_ > 0) {
    available_slots_--;
    return true;
  }
  return false;
} // namespace NighthawkboolRateLimiter::tryAcquireOne()

void LinearRateLimiter::recoverSlots() {
  auto elapsed_since_start = std::chrono::high_resolution_clock::now() - started_at_;
  int64_t slots_to_add = (elapsed_since_start / slot_recovery_time_) - total_slots_acquired_;
  if (slots_to_add > 0) {
    int64_t slots_left = slots_to_add - (max_slots_ - available_slots_);
    available_slots_ += slots_to_add - slots_left;
    total_slots_acquired_ += slots_to_add - slots_left;
  }
}

} // namespace Nighthawk