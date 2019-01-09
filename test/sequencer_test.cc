#include <chrono>

#include "gtest/gtest.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "common/api/api_impl.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/real_time_system.h"
#include "common/stats/isolated_store_impl.h"

#include "nighthawk/common/exception.h"

#include "exe/rate_limiter.h"
#include "exe/sequencer.h"

using namespace std::chrono_literals;

namespace Nighthawk {

class SequencerTest : public testing::Test {
public:
  SequencerTest() : callback_test_count_(0) {}
  bool callback_test(std::function<void()> f) {
    callback_test_count_++;
    f();
    return true;
  }
  int callback_test_count_;
};

// TODO(oschaaf): more construction tests. move to seperate test, etc.

TEST_F(SequencerTest, BasicSequencerTest) {
  Envoy::Thread::ThreadFactoryImplPosix thread_factory;
  Envoy::Stats::IsolatedStoreImpl store;
  Envoy::Api::Impl api(1000ms /*flush interval*/, thread_factory, store);
  // TODO(oschaaf): figure out what it takes to do this with SimulatedTimeSystem instead.
  // Envoy::Event::SimulatedTimeSystem time_system;
  Envoy::Event::RealTimeSystem time_system;
  auto dispatcher = api.allocateDispatcher(time_system);

  LinearRateLimiter rate_limiter(time_system, 100ms);
  SequencerTarget callback_empty;

  ASSERT_THROW(Sequencer sequencer(*dispatcher, time_system, rate_limiter, callback_empty, 1s),
               NighthawkException);

  std::function<bool(std::function<void()>)> f =
      std::bind(&SequencerTest::callback_test, this, std::placeholders::_1);
  Sequencer sequencer(*dispatcher, time_system, rate_limiter, f, 1050ms);
  sequencer.start();
  sequencer.waitForCompletion();
  // With 50ms slack, we ought to have observed 10 callbacks at the 10/second pacing.
  EXPECT_EQ(10, callback_test_count_);
}

} // namespace Nighthawk
