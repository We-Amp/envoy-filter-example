#include "exe/benchmark_loop.h"

#include <chrono>
#include <iostream>
#include <memory>

#include "ares.h"

#include "absl/strings/str_split.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/event/dispatcher_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

#include "common/runtime/runtime_impl.h"
#include "common/thread_local/thread_local_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"

#include "exe/ssl.h"

using namespace Envoy;

namespace Nighthawk {

void BenchmarkLoop::start() {
  start_ = std::chrono::high_resolution_clock::now();
  run(false);
  scheduleRun();
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::NonBlock);
}
void BenchmarkLoop::waitForCompletion() {
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
}
void BenchmarkLoop::scheduleRun() { timer_->enableTimer(std::chrono::milliseconds(1)); }

void BenchmarkLoop::run(bool from_timer) {
  auto now = std::chrono::high_resolution_clock::now();
  auto dur = now - start_;
  double ms_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count() / 1000000.0;
  current_rps_ = requests_ / (ms_dur / 1000.0);
  int due_requests = ((rps_ - current_rps_)) * (ms_dur / 1000.0);

  if (dur >= duration_) {
    dispatcher_->exit();
    return;
  } else if (pool_connect_failures_ >= 1) { // TODO(oschaaf): config
    ENVOY_LOG(error, "Too many connection failures");
    dispatcher_->exit();
    return;
  }

  if (due_requests == 0 && !expectInboundEvents()) {
    nanosleep((const struct timespec[]){{0, 500L}}, NULL);
    timer_->enableTimer(std::chrono::milliseconds(0));
    return;
  }

  while (requests_ < max_requests_ && due_requests-- > 0) {
    bool started = tryStartOne([this, now]() {
      auto nanoseconds = std::chrono::high_resolution_clock::now() - now;
      ASSERT(nanoseconds.count() > 0);
      // results_.push_back(nanoseconds.count());
      if (++callback_count_ == this->max_requests_) {
        dispatcher_->exit();
        return;
      }
      timer_->enableTimer(std::chrono::milliseconds(0));
    });

    if (!started) {
      scheduleRun();
      return;
    }

    ++requests_;
  }

  if (from_timer) {
    scheduleRun();
  }
}

HttpBenchmarkTimingLoop::HttpBenchmarkTimingLoop(Envoy::Event::Dispatcher& dispatcher,
                                                 Envoy::Stats::Store& store,
                                                 Envoy::TimeSource& time_source,
                                                 Thread::ThreadFactory& thread_factory,
                                                 uint64_t rps, std::chrono::seconds duration)
    : BenchmarkLoop(dispatcher, store, time_source, thread_factory, rps, duration) {

  envoy::api::v2::Cluster cluster_config;
  envoy::api::v2::core::BindConfig bind_config;
  envoy::config::bootstrap::v2::Runtime runtime_config;

  cluster_config.mutable_connect_timeout()->set_seconds(3);
  Envoy::Stats::ScopePtr scope = store_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
  runtime_ = std::make_unique<Envoy::Runtime::LoaderImpl>(generator_, store_, *tls_);

  // Envoy::Network::TransportSocketFactoryPtr socket_factory =
  //    std::make_unique<Network::RawBufferSocketFactory>();

  auto socket_factory =
      Network::TransportSocketFactoryPtr{new Ssl::MClientSslSocketFactory(store, time_source)};

  Envoy::Upstream::ClusterInfoConstSharedPtr cluster = std::make_unique<Upstream::ClusterInfoImpl>(
      cluster_config, bind_config, *runtime_, std::move(socket_factory), std::move(scope),
      false /*added_via_api*/);

  Network::ConnectionSocket::OptionsSharedPtr options =
      std::make_shared<Network::ConnectionSocket::Options>();

  auto host = std::shared_ptr<Upstream::Host>{new Upstream::HostImpl(
      cluster, "", Network::Utility::resolveUrl("tcp://127.0.0.1:443"),
      envoy::api::v2::core::Metadata::default_instance(), 1 /* weight */,
      envoy::api::v2::core::Locality(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0)};

  // pool_ = std::make_unique<Envoy::Http::Http1::ConnPoolImplProd>(
  //    dispatcher, host, Upstream::ResourcePriority::Default, options);

  pool_ = std::make_unique<Envoy::Http::Http2::ProdConnPoolImpl>(
      dispatcher, host, Upstream::ResourcePriority::Default, options);
}

bool HttpBenchmarkTimingLoop::tryStartOne(std::function<void()> completion_callback) {
  auto stream_decoder = new Nighthawk::Http::StreamDecoder(
      [completion_callback]() -> void { completion_callback(); });
  auto cancellable = pool_->newStream(*stream_decoder, *this);
  (void)cancellable;
  return true;
}
void HttpBenchmarkTimingLoop::onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                                            Envoy::Upstream::HostDescriptionConstSharedPtr host) {
  // TODO(oschaaf): we can probably pull these counters from the stats,
  // and therefore do not have to track them ourselves here.
  // TODO(oschaaf): unify termination of the flow here and from the stream decoder.
  (void)host;
  switch (reason) {
  case Envoy::Http::ConnectionPool::PoolFailureReason::ConnectionFailure:
    pool_connect_failures_++;
    break;
  case Envoy::Http::ConnectionPool::PoolFailureReason::Overflow:
    pool_overflow_failures_++;
    break;
  default:
    ASSERT(false);
  }
}
void HttpBenchmarkTimingLoop::onPoolReady(Envoy::Http::StreamEncoder& encoder,
                                          Envoy::Upstream::HostDescriptionConstSharedPtr host) {
  (void)host;
  HeaderMapImpl headers;
  headers.insertMethod().value(Headers::get().MethodValues.Get);
  // TODO(oschaaf): hard coded path and host
  headers.insertPath().value(std::string("/"));
  headers.insertHost().value(std::string("127.0.0.1"));
  headers.insertScheme().value(Headers::get().SchemeValues.Https);
  encoder.encodeHeaders(headers, true);
}

} // namespace Nighthawk
