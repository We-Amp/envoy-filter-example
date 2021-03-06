#include <chrono>

#include "gtest/gtest.h"

#include "test/test_common/simulated_time_system.h"

#include "nighthawk/common/exception.h"

#include "common/rate_limiter.h"

using namespace std::chrono_literals;

namespace Nighthawk {

class RateLimiterTest : public testing::Test {};

TEST_F(RateLimiterTest, LinearRateLimiterTest) {
  Envoy::Event::SimulatedTimeSystem time_system;
  // Construct a 10/second paced rate limiter.
  LinearRateLimiter rate_limiter(time_system, 100ms);

  EXPECT_FALSE(rate_limiter.tryAcquireOne());

  time_system.sleep(100ms);
  EXPECT_TRUE(rate_limiter.tryAcquireOne());
  EXPECT_FALSE(rate_limiter.tryAcquireOne());

  time_system.sleep(1s);
  for (int i = 0; i < 10; i++) {
    EXPECT_TRUE(rate_limiter.tryAcquireOne());
  }
  EXPECT_FALSE(rate_limiter.tryAcquireOne());
}

TEST_F(RateLimiterTest, LinearRateLimiterInvalidArgumentTest) {
  Envoy::Event::SimulatedTimeSystem time_system;
  ASSERT_THROW(LinearRateLimiter rate_limiter(time_system, -100ms), NighthawkException);
}

} // namespace Nighthawk
