#include "exe/benchmark_loop.h"

#include <chrono>
#include <fstream>
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

BenchmarkLoop::BenchmarkLoop(Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
                             Envoy::TimeSource& time_source, Thread::ThreadFactory& thread_factory,
                             uint64_t rps, std::chrono::seconds duration, std::string uri)
    : dispatcher_(dispatcher), store_(store),
      timer_(dispatcher_.createTimer([this]() { run(true); })), time_source_(time_source),
      thread_factory_(thread_factory), pool_connect_failures_(0), pool_overflow_failures_(0),
      is_https_(false), host_(""), port_(0), path_("/"), rps_(rps), current_rps_(0),
      duration_(duration), requests_(0), max_requests_(rps_ * duration_.count()),
      callback_count_(0), uri_(uri) {

  results_.reserve(duration.count() * rps);

  // parse incoming uri into fields that we need.
  // TODO(oschaaf): refactor. also input validation, etc.
  absl::string_view host, path;
  Envoy::Http::Utility::extractHostPathFromUri(uri, host, path);
  host_ = std::string(host);
  path_ = std::string(path);

  size_t colon_index = host_.find(':');
  is_https_ = uri.find("https://") == 0;

  if (colon_index == std::string::npos) {
    port_ = is_https_ ? 443 : 80;
  } else {
    std::string tcp_url = fmt::format("tcp://{}", this->host_);
    port_ = Network::Utility::portFromTcpUrl(tcp_url);
    host_ = host_.substr(0, colon_index);
  }
}

BenchmarkLoop::~BenchmarkLoop() {
  // TODO(oschaaf): we should only do this on request.
  // TODO(oschaaf): we should not do this in this destructor.
  std::ofstream myfile;
  myfile.open("res.txt");
  for (int r : results_) {
    myfile << (r / 1000) << "\n";
  }
  myfile.close();

  double average = std::accumulate(results_.begin(), results_.end(), 0.0) / results_.size();
  auto minmax = std::minmax_element(results_.begin(), results_.end());
  ENVOY_LOG(info, "avg latency {} us over {} callbacks", (average / 1000), results_.size());
  ENVOY_LOG(info, "min / max latency: {} / {}", (*(minmax.first) / 1000),
            (*(minmax.second) / 1000));

  // TODO(oschaaf): check
  tls_->shutdownGlobalThreading();
}

void BenchmarkLoop::initialize() {
  // TODO(oschaaf): ipv6, refactor dns stuff into separate call
  auto dns_resolver = dispatcher_.createDnsResolver({});
  Network::ActiveDnsQuery* active_dns_query_ = dns_resolver->resolve(
      host_, Network::DnsLookupFamily::V4Only,
      [this, &active_dns_query_](
          const std::list<Network::Address::InstanceConstSharedPtr>&& address_list) -> void {
        active_dns_query_ = nullptr;
        ENVOY_LOG(debug, "DNS resolution complete for {} ({} entries).", this->host_,
                  address_list.size());
        if (!address_list.empty()) {
          dns_failure_ = false;
          target_address_ = Network::Utility::getAddressWithPort(*address_list.front(), port_);
        } else {
          ENVOY_LOG(critical, "Could not resolve host [{}]", host_);
        }
        dispatcher_.exit();
      });
  // Wait for DNS resolution to complete before proceeding.
  dispatcher_.run(Envoy::Event::Dispatcher::RunType::Block);
}

bool BenchmarkLoop::start() {
  initialize();
  if (dns_failure_) {
    return false;
  }

  start_ = std::chrono::high_resolution_clock::now();
  run(false);
  scheduleRun();
  dispatcher_.run(Envoy::Event::Dispatcher::RunType::NonBlock);
  return true;
}
void BenchmarkLoop::waitForCompletion() {
  dispatcher_.run(Envoy::Event::Dispatcher::RunType::Block);
}
void BenchmarkLoop::scheduleRun() { timer_->enableTimer(std::chrono::milliseconds(1)); }

void BenchmarkLoop::run(bool from_timer) {
  auto now = std::chrono::high_resolution_clock::now();
  auto dur = now - start_;
  double ms_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count() / 1000000.0;
  current_rps_ = requests_ / (ms_dur / 1000.0);
  int due_requests = ((rps_ - current_rps_)) * (ms_dur / 1000.0);

  if (dur >= duration_ && callback_count_ >= max_requests_) {
    ENVOY_LOG(info, "requested: {} completed:{} rps: {}", requests_, callback_count_, current_rps_);
    ENVOY_LOG(info, "{} ms benmark run completed.", ms_dur);
    dispatcher_.exit();
    return;
  } else if (pool_connect_failures_ >= 1) { // TODO(oschaaf): config
    ENVOY_LOG(error, "Too many connection failures");
    dispatcher_.exit();
    // TODO(oschaaf): program should return exit code here.
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
      callback_count_++;
      results_.push_back(nanoseconds.count());
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

HttpBenchmarkTimingLoop::HttpBenchmarkTimingLoop(
    Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
    Envoy::TimeSource& time_source, Thread::ThreadFactory& thread_factory, uint64_t rps,
    std::chrono::seconds duration, uint64_t max_connections, std::chrono::seconds timeout,
    std::string uri, bool h2)
    : BenchmarkLoop(dispatcher, store, time_source, thread_factory, rps, duration, uri),
      timeout_(timeout), max_connections_(max_connections), h2_(h2) {
  ENVOY_LOG(info, "uri {} -> h2[{}] | is_https [{}] | host [{}] | path [{}] | port [{}]", uri, h2_,
            is_https_, host_, path_, port_);
}

bool HttpBenchmarkTimingLoop::tryStartOne(std::function<void()> completion_callback) {
  auto stream_decoder = new Nighthawk::Http::StreamDecoder(
      [completion_callback]() -> void { completion_callback(); });
  auto cancellable = pool_->newStream(*stream_decoder, *this);
  (void)cancellable;
  return true;
}

void HttpBenchmarkTimingLoop::initialize() {

  BenchmarkLoop::initialize();
  envoy::api::v2::Cluster cluster_config;
  envoy::api::v2::core::BindConfig bind_config;
  envoy::config::bootstrap::v2::Runtime runtime_config;

  auto thresholds = cluster_config.mutable_circuit_breakers()->add_thresholds();

  cluster_config.mutable_connect_timeout()->set_seconds(timeout_.count());
  thresholds->mutable_max_connections()->set_value(max_connections_);

  Envoy::Stats::ScopePtr scope = store_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
  runtime_ = std::make_unique<Envoy::Runtime::LoaderImpl>(generator_, store_, *tls_);

  Network::TransportSocketFactoryPtr socket_factory;
  if (is_https_) {
    socket_factory =
        Network::TransportSocketFactoryPtr{new Ssl::MClientSslSocketFactory(store_, time_source_)};
  } else {
    socket_factory = std::make_unique<Network::RawBufferSocketFactory>();
  };

  Envoy::Upstream::ClusterInfoConstSharedPtr cluster = std::make_unique<Upstream::ClusterInfoImpl>(
      cluster_config, bind_config, *runtime_, std::move(socket_factory), std::move(scope),
      false /*added_via_api*/);

  Network::ConnectionSocket::OptionsSharedPtr options =
      std::make_shared<Network::ConnectionSocket::Options>();

  auto host = std::shared_ptr<Upstream::Host>{new Upstream::HostImpl(
      cluster, host_, target_address_, envoy::api::v2::core::Metadata::default_instance(),
      1 /* weight */, envoy::api::v2::core::Locality(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0)};

  if (h2_) {
    pool_ = std::make_unique<Envoy::Http::Http2::ProdConnPoolImpl>(
        dispatcher_, host, Upstream::ResourcePriority::Default, options);
  } else {
    pool_ = std::make_unique<Envoy::Http::Http1::ConnPoolImplProd>(
        dispatcher_, host, Upstream::ResourcePriority::Default, options);
  }
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
  headers.insertPath().value(std::string(path_));
  headers.insertHost().value(std::string(host_));
  headers.insertScheme().value(is_https_ ? Headers::get().SchemeValues.Https
                                         : Headers::get().SchemeValues.Http);
  encoder.encodeHeaders(headers, true);
}

} // namespace Nighthawk
