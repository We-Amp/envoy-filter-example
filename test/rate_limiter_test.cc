#include <chrono>

#include "ares.h"

#include "gtest/gtest.h"

#include "test/test_common/simulated_time_system.h"

#include "exe/rate_limiter.h"

using namespace Envoy;
using namespace std::chrono_literals;

namespace Nighthawk {

class RateLimiterTest : public testing::Test {
public:
  RateLimiterTest() {}
  void SetUp() {}
  void TearDown() {}
};

TEST_F(RateLimiterTest, LinearRateLimiterTest) {
  Envoy::Event::SimulatedTimeSystem time_system;
  // Construct a 10/second paced rate limiter.
  LinearRateLimiter rl(time_system, 100ms);

  EXPECT_FALSE(rl.tryAcquireOne());

  time_system.sleep(100ms);
  EXPECT_TRUE(rl.tryAcquireOne());
  EXPECT_FALSE(rl.tryAcquireOne());

  time_system.sleep(1s);
  for (int i = 0; i < 10; i++) {
    EXPECT_TRUE(rl.tryAcquireOne());
  }
  EXPECT_FALSE(rl.tryAcquireOne());
}

} // namespace Nighthawk
