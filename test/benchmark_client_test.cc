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

TEST_F(BenchmarkClientTest, TestTest) {
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

  auto client = std::make_unique<BenchmarkHttpClient>(*dispatcher, store, time_system,
                                                      "http://127.0.0.1:10001/",
                                                      std::move(request_headers), false);
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
  client.reset();
  tls.shutdownGlobalThreading();
}

} // namespace Nighthawk
