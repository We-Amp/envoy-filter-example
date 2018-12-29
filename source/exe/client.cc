#include "exe/client.h"

#include <chrono>
#include <iostream>
#include <memory>

#include "ares.h"

#include "absl/strings/str_split.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

#include "common/runtime/runtime_impl.h"
#include "common/thread_local/thread_local_impl.h"
#include "common/upstream/cluster_manager_impl.h"
#include "common/upstream/upstream_impl.h"
#include "envoy/upstream/upstream.h"

#include "server/transport_socket_config_impl.h"

#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "common/ssl/ssl_socket.h"

#include "envoy/network/transport_socket.h"

#include "openssl/ssl.h" // TLS1_2_VERSION etc

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

const std::string DEFAULT_CIPHER_SUITES =
#ifndef BORINGSSL_FIPS
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
#else // BoringSSL FIPS
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
#endif
    "ECDHE-ECDSA-AES128-SHA:"
    "ECDHE-RSA-AES128-SHA:"
    "AES128-GCM-SHA256:"
    "AES128-SHA:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES256-SHA:"
    "ECDHE-RSA-AES256-SHA:"
    "AES256-GCM-SHA384:"
    "AES256-SHA";

const std::string DEFAULT_ECDH_CURVES =
#ifndef BORINGSSL_FIPS
    "X25519:"
#endif
    "P-256";

namespace {
// This SslSocket will be used when SSL secret is not fetched from SDS server.
class MNotReadySslSocket : public Network::TransportSocket {
public:
  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks&) override {}
  std::string protocol() const override { return EMPTY_STRING; }
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent) override {}
  Network::IoResult doRead(Buffer::Instance&) override {
    return {Envoy::Network::PostIoAction::Close, 0, false};
  }
  Network::IoResult doWrite(Buffer::Instance&, bool) override {
    return {Envoy::Network::PostIoAction::Close, 0, false};
  }
  void onConnected() override {}
  const Ssl::Connection* ssl() const override { return nullptr; }
};
} // namespace

// TODO(oschaaf): make a concrete implementation out of this one.
class MClientContextConfigImpl : public Ssl::ClientContextConfig {
public:
  MClientContextConfigImpl() {}
  virtual ~MClientContextConfigImpl() {}

  virtual const std::string& alpnProtocols() const { return foo_; };

  virtual const std::string& cipherSuites() const { return DEFAULT_CIPHER_SUITES; };

  virtual const std::string& ecdhCurves() const { return DEFAULT_ECDH_CURVES; };

  virtual std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>
  tlsCertificates() const {
    std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>> configs;
    for (const auto& config : tls_certificate_configs_) {
      configs.push_back(config);
    }
    return configs;
  };

  virtual const Ssl::CertificateValidationContextConfig* certificateValidationContext() const {
    return validation_context_config_.get();
  };

  virtual unsigned minProtocolVersion() const { return TLS1_VERSION; };

  virtual unsigned maxProtocolVersion() const { return TLS1_2_VERSION; };

  virtual bool isReady() const { return true; };

  virtual void setSecretUpdateCallback(std::function<void()> callback) { callback_ = callback; };

  // Ssl::ClientContextConfig interface
  virtual const std::string& serverNameIndication() const { return foo_; };

  virtual bool allowRenegotiation() const { return true; };

  virtual size_t maxSessionKeys() const { return 0; };

  virtual const std::string& signingAlgorithmsForTest() const { return foo_; };

private:
  std::string foo_;
  std::function<void()> callback_;
  std::vector<Ssl::TlsCertificateConfigImpl> tls_certificate_configs_;
  Ssl::CertificateValidationContextConfigPtr validation_context_config_;
};

class MClientSslSocketFactory : public Network::TransportSocketFactory,
                                public Secret::SecretCallbacks,
                                Logger::Loggable<Logger::Id::config> {
public:
  MClientSslSocketFactory(Envoy::Stats::Store& store, Envoy::TimeSource& time_source) {
    // TODO(oschaaf): check for leaks
    Ssl::ClientContextConfig* config = new MClientContextConfigImpl();
    Envoy::Stats::ScopePtr scope = store.createScope(fmt::format("cluster.{}.", "ssl-client"));
    Ssl::ClientContextSharedPtr context =
        std::make_shared<Ssl::ClientContextImpl>(*(scope.release()), *config, time_source);
    ssl_ctx_ = context;
  }
  Network::TransportSocketPtr createTransportSocket(
      Network::TransportSocketOptionsSharedPtr transport_socket_options) const override {
    // onAddOrUpdateSecret() could be invoked in the middle of checking the existence of ssl_ctx and
    // creating SslSocket using ssl_ctx. Capture ssl_ctx_ into a local variable so that we check and
    // use the same ssl_ctx to create SslSocket.
    Ssl::ClientContextSharedPtr ssl_ctx;
    {
      absl::ReaderMutexLock l(&ssl_ctx_mu_);
      //  ClientContextImpl(Stats::Scope& scope, const ClientContextConfig& config,
      //              TimeSource& time_source);

      // Ssl::ClientContextSharedPtr context =
      //    std::make_shared<Ssl::ClientContextImpl>(scope, config, time_source_);
      // removeEmptyContexts();
      // contexts_.emplace_back(context);

      ssl_ctx = ssl_ctx_;
    }
    if (ssl_ctx) {
      return std::make_unique<Ssl::SslSocket>(std::move(ssl_ctx), Ssl::InitialState::Client,
                                              transport_socket_options);
    } else {
      ENVOY_LOG(debug, "Create NotReadySslSocket");
      return std::make_unique<MNotReadySslSocket>();
    }
  }

  bool implementsSecureTransport() const override { return true; };

  // Secret::SecretCallbacks
  void onAddOrUpdateSecret() override {
    ENVOY_LOG(debug, "Secret is updated.");
    {
      absl::WriterMutexLock l(&ssl_ctx_mu_);
      // ssl_ctx_ = manager_.createSslClientContext(stats_scope_, *config_);
    }
  }

private:
  // Ssl::ContextManager& manager_;
  // Stats::Scope& stats_scope_;
  // SslSocketFactoryStats stats_;
  // ClientContextConfigPtr config_;
  mutable absl::Mutex ssl_ctx_mu_;
  Ssl::ClientContextSharedPtr ssl_ctx_ GUARDED_BY(ssl_ctx_mu_);
};

HttpBenchmarkTimingLoop::HttpBenchmarkTimingLoop(Envoy::Event::Dispatcher& dispatcher,
                                                 Envoy::Stats::Store& store,
                                                 Envoy::TimeSource& time_source,
                                                 Thread::ThreadFactory& thread_factory)
    : BenchmarkLoop(dispatcher, store, time_source, thread_factory) {

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
      Network::TransportSocketFactoryPtr{new MClientSslSocketFactory(store, time_source)};

  Envoy::Upstream::ClusterInfoConstSharedPtr cluster = std::make_unique<Upstream::ClusterInfoImpl>(
      cluster_config, bind_config, *runtime_, std::move(socket_factory), std::move(scope),
      false /*added_via_api*/);

  Network::ConnectionSocket::OptionsSharedPtr options =
      std::make_shared<Network::ConnectionSocket::Options>();

  auto host = std::shared_ptr<Upstream::Host>{new Upstream::HostImpl(
      cluster, "", Network::Utility::resolveUrl("tcp://192.168.2.232:443"),
      envoy::api::v2::core::Metadata::default_instance(), 1 /* weight */,
      envoy::api::v2::core::Locality(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0)};

  pool_ = std::make_unique<Envoy::Http::Http1::ConnPoolImplProd>(
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
  headers.insertScheme().value(Headers::get().SchemeValues.Http);
  encoder.encodeHeaders(headers, true);
}

ClientMain::ClientMain(int argc, const char* const* argv) : ClientMain(OptionsImpl(argc, argv)) {}

ClientMain::ClientMain(OptionsImpl options) : options_(options) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  configureComponentLogLevels();
}

ClientMain::~ClientMain() { ares_library_cleanup(); }

void ClientMain::configureComponentLogLevels() {
  // We rely on Envoy's logging infra.
  // TODO(oschaaf): Add options to tweak the log level of the various log tags
  // that are available.
  Logger::Registry::setLogLevel(spdlog::level::trace);
  Logger::Logger* logger_to_change = Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::trace);
}

bool ClientMain::run() {
  auto store = std::make_unique<Stats::IsolatedStoreImpl>();
  // TODO(oschaaf): platform specificity need addressing.
  auto thread_factory = Thread::ThreadFactoryImplPosix();
  auto api = std::make_unique<Envoy::Api::Impl>(std::chrono::milliseconds(1000) /*flush interval*/,
                                                thread_factory, *store);
  auto dispatcher = api->allocateDispatcher(real_time_system_);
  HttpBenchmarkTimingLoop bml(*dispatcher, *store, real_time_system_, thread_factory);
  bml.start();
  bml.waitForCompletion();
  // Benchmarker benchmarker(*dispatcher, options_.connections(), options_.requests_per_second(),
  //                        options_.duration(), Headers::get().MethodValues.Get, options_.uri());
  // auto dns_resolver = dispatcher->createDnsResolver({});
  // benchmarker.run(dns_resolver);
  return true;
}

} // namespace Nighthawk
