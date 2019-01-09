#include "exe/rate_limiter.h"

namespace Nighthawk {

bool LinearRateLimiter::tryAcquireOne() {
  if (acquireable_count_ > 0) {
    acquireable_count_--;
    acquired_count_++;
    return true;
  }

  auto elapsed_since_start = time_source_.monotonicTime() - started_at_;
  acquireable_count_ = (elapsed_since_start / pace_) - acquired_count_;
  return acquireable_count_ > 0 ? tryAcquireOne() : false;
}

} // namespace Nighthawk