#include <chrono>

#include "ares.h"

#include "gtest/gtest.h"

#include "test/test_common/simulated_time_system.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/real_time_system.h"
#include "common/http/header_map_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "exe/benchmark_client.h"
#include "exe/rate_limiter.h"
#include "exe/sequencer.h"

using namespace std::chrono_literals;

namespace Nighthawk {

class BenchmarkClientTest : public testing::Test {
public:
  BenchmarkClientTest()
      : api_(1000ms /*flush interval*/, thread_factory_, store_),
        dispatcher_(api_.allocateDispatcher(time_system_)), runtime_(generator_, store_, tls_) {}
  void SetUp() {
    ares_library_init(ARES_LIB_INIT_ALL);
    Envoy::Event::Libevent::Global::initialize();
  }
  void TearDown() {
    tls_.shutdownGlobalThreading();
    ares_library_cleanup();
  }

  Envoy::Thread::ThreadFactoryImplPosix thread_factory_;
  Envoy::Stats::IsolatedStoreImpl store_;
  Envoy::Event::RealTimeSystem time_system_;
  Envoy::Api::Impl api_;
  Envoy::Event::DispatcherPtr dispatcher_;
  Envoy::Runtime::RandomGeneratorImpl generator_;
  Envoy::ThreadLocal::InstanceImpl tls_;
  Envoy::Runtime::LoaderImpl runtime_;
};

// TODO(oschaaf): this is a very,very crude end-to-end test.
// Needs to be refactored and needs a synthetic origin to test
// against. also, we need more tests.
TEST_F(BenchmarkClientTest, BasicTestH1) {
  Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
  request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);
  BenchmarkHttpClient client(*dispatcher_, store_, time_system_, "http://127.0.0.1/",
                             std::move(request_headers), false /*use h2*/);

  // TODO(oschaaf): either get rid of the intialize call, or test that we except
  // when we didn't call it before calling tryStartOne().
  client.initialize(runtime_);

  int amount = 10;
  int inflight_response_count = amount;

  std::function<void()> f = [this, &inflight_response_count]() {
    if (--inflight_response_count == 0) {
      dispatcher_->exit();
    }
  };

  for (int i = 0; i < amount; i++) {
    client.tryStartOne(f);
  }

  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

  EXPECT_EQ(0, inflight_response_count);
  EXPECT_EQ(0, client.pool_connect_failures());
  EXPECT_EQ(0, client.http_bad_response_count());
  EXPECT_EQ(0, client.stream_reset_count());
  EXPECT_EQ(0, client.pool_overflow_failures());
  EXPECT_EQ(amount, client.http_good_response_count());
}

// TODO(oschaaf): see figure out if we can and should simulated time in this test
// to eliminate flake chances, and speed up execution.
TEST_F(BenchmarkClientTest, SequencedH2Test) {
  Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
  request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);

  BenchmarkHttpClient client(*dispatcher_, store_, time_system_, "https://localhost/",
                             std::move(request_headers), true /*use h2*/);
  client.initialize(runtime_);

  // TODO(oschaaf): create an interface that pulls this from implementations upon implementation.
  SequencerTarget f = std::bind(&BenchmarkHttpClient::tryStartOne, &client, std::placeholders::_1);

  LinearRateLimiter rate_limiter(time_system_, 10ms);
  std::chrono::milliseconds duration(59ms);
  Sequencer sequencer(*dispatcher_, time_system_, rate_limiter, f, duration);

  sequencer.start();
  sequencer.waitForCompletion();

  EXPECT_EQ(0, client.pool_connect_failures());
  EXPECT_EQ(0, client.http_bad_response_count());
  EXPECT_EQ(0, client.stream_reset_count());
  EXPECT_EQ(0, client.pool_overflow_failures());
  // We expect all responses to get in within the 9 ms slack we gave it.
  EXPECT_EQ(5, client.http_good_response_count());
}

} // namespace Nighthawk
