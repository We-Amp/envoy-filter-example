#include "ares.h"

#include "gtest/gtest.h"

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

using namespace Envoy;

namespace Nighthawk {

class BenchmarkClientTest : public testing::Test {
public:
  BenchmarkClientTest() {}
  void SetUp() {
    ares_library_init(ARES_LIB_INIT_ALL);
    Event::Libevent::Global::initialize();
  }
  void TearDown() { ares_library_cleanup(); }
};

// TODO(oschaaf): this is a very,very crude end-to-end test.
// Needs to be refactored and needs a synthetic origin to test
// against. also, we need more tests.
TEST_F(BenchmarkClientTest, SillyEndToEndTest) {
  Envoy::Event::RealTimeSystem time_system;
  auto thread_factory = Thread::ThreadFactoryImplPosix();

  Stats::IsolatedStoreImpl store;
  Envoy::Api::Impl api(std::chrono::milliseconds(1000) /*flush interval*/, thread_factory, store);
  auto dispatcher = api.allocateDispatcher(time_system);

  Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
  request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);
  request_headers->insertPath().value(std::string("/"));
  request_headers->insertHost().value(std::string("127.0.0.1"));
  request_headers->insertScheme().value(Envoy::Http::Headers::get().SchemeValues.Http);

  auto client = std::make_unique<BenchmarkHttpClient>(
      *dispatcher, store, time_system, "http://127.0.0.1/", std::move(request_headers), false);
  Envoy::ThreadLocal::InstanceImpl tls;
  Envoy::Runtime::RandomGeneratorImpl generator;
  Envoy::Runtime::LoaderImpl runtime(generator, store, tls);
  client->initialize(runtime);

  int response_count = 0;
  client->tryStartOne([&]() { response_count++; });
  client->tryStartOne([&]() {
    response_count++;
    dispatcher->exit();
  });

  dispatcher->run(Envoy::Event::Dispatcher::RunType::Block);
  EXPECT_EQ(2, response_count);
  // SequencerTarget foo = std::bind(&BenchmarkHttpClient::tryStartOne, client,
  // std::placeholders::_1);
  std::function<bool(std::function<void()>)> f =
      std::bind(&BenchmarkHttpClient::tryStartOne, client.get(), std::placeholders::_1);
  client.reset();
  tls.shutdownGlobalThreading();
}

TEST_F(BenchmarkClientTest, SillySequencerTest) {
  Envoy::Event::RealTimeSystem time_system;
  auto thread_factory = Thread::ThreadFactoryImplPosix();

  Stats::IsolatedStoreImpl store;
  Envoy::Api::Impl api(std::chrono::milliseconds(1000) /*flush interval*/, thread_factory, store);
  auto dispatcher = api.allocateDispatcher(time_system);

  Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
  request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);

  auto client = std::make_unique<BenchmarkHttpClient>(
      *dispatcher, store, time_system, "https://localhost/", std::move(request_headers), true);
  Envoy::ThreadLocal::InstanceImpl tls;
  Envoy::Runtime::RandomGeneratorImpl generator;
  Envoy::Runtime::LoaderImpl runtime(generator, store, tls);
  client->initialize(runtime);

  std::function<bool(std::function<void()>)> f =
      std::bind(&BenchmarkHttpClient::tryStartOne, client.get(), std::placeholders::_1);

  std::unique_ptr<RateLimiter> rate_limiter =
      std::make_unique<LinearRateLimiter>(1, std::chrono::microseconds(1000 * 1000));
  Sequencer sequencer(*dispatcher, time_system, *rate_limiter, f, std::chrono::seconds(3));
  sequencer.start();
  sequencer.waitForCompletion();
  client.reset();
  tls.shutdownGlobalThreading();
}

// TODO(oschaaf): need to mock time to test this properly, which requires
// changes to the rate limiter.
TEST_F(BenchmarkClientTest, LinearRateLimiterTest) {
  LinearRateLimiter rl(1, std::chrono::microseconds(1000 * 1000));
  EXPECT_TRUE(rl.tryAcquireOne());
  EXPECT_FALSE(rl.tryAcquireOne());
  usleep((1000 * 1000) + 1);
  EXPECT_TRUE(rl.tryAcquireOne());
  EXPECT_FALSE(rl.tryAcquireOne());
}

} // namespace Nighthawk
