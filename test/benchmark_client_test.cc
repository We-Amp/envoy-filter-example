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

using namespace Envoy;
using namespace std::chrono_literals;

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
  Envoy::Api::Impl api(1000ms /*flush interval*/, thread_factory, store);
  auto dispatcher = api.allocateDispatcher(time_system);

  Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
  request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);

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
  Envoy::Api::Impl api(1000ms /*flush interval*/, thread_factory, store);
  auto dispatcher = api.allocateDispatcher(time_system);

  Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
  request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);

  auto client = std::make_unique<BenchmarkHttpClient>(
      *dispatcher, store, time_system, "https://localhost/", std::move(request_headers), true);
  Envoy::Runtime::RandomGeneratorImpl generator;
  Envoy::ThreadLocal::InstanceImpl tls;
  Envoy::Runtime::LoaderImpl runtime(generator, store, tls);
  client->initialize(runtime);

  std::function<bool(std::function<void()>)> f =
      std::bind(&BenchmarkHttpClient::tryStartOne, client.get(), std::placeholders::_1);

  std::unique_ptr<RateLimiter> rate_limiter = std::make_unique<LinearRateLimiter>(time_system, 1s);
  Sequencer sequencer(*dispatcher, time_system, *rate_limiter, f, 3s);
  sequencer.start();
  sequencer.waitForCompletion();
  client.reset();
  tls.shutdownGlobalThreading();
}

} // namespace Nighthawk
