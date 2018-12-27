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

#include "exe/benchmarker.h"
#include "exe/conn_pool.h"

#include "server/transport_socket_config_impl.h"

using namespace Envoy;

namespace Nighthawk {

void BenchmarkLoop::start() {
  start_ = std::chrono::high_resolution_clock::now();
  run(false);
  scheduleRun();
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
                                                 Envoy::TimeSource& time_source)
    : BenchmarkLoop(dispatcher, store, time_source) {
  Network::ConnectionSocket::OptionsSharedPtr options =
      std::make_shared<Network::ConnectionSocket::Options>();
  Envoy::Upstream::ClusterInfoConstSharedPtr cluster;

  envoy::api::v2::Cluster cluster_config;
  envoy::api::v2::core::BindConfig bind_config;
  envoy::config::bootstrap::v2::Runtime runtime_config;

  cluster_config.mutable_connect_timeout()->set_seconds(30);
  Envoy::Stats::ScopePtr scope = store_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Runtime::RandomGeneratorImpl generator;
  ThreadLocal::InstanceImpl tls;
  Envoy::Runtime::LoaderImpl runtime(generator, store_, tls);
  // RandomGenerator& generator, Stats::Store& stats, ThreadLocal::SlotAllocator& tls
  Envoy::Network::TransportSocketFactoryPtr socket_factory =
      std::make_unique<Network::RawBufferSocketFactory>();
  auto cluster_info = std::make_unique<Upstream::ClusterInfoImpl>(
      cluster_config, bind_config, runtime, std::move(socket_factory), std::move(scope),
      false /*added_via_api*/);

  // cluster->http2_settings_.allow_connect_ = true;
  // cluster->http2_settings_.allow_metadata_ = true;
  /*
    {
      const std::string json = R"EOF(
    {
      "name": "addressportconfig",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "fakelbtype",
      "hosts": [{"url": "tcp://192.168.1.1:22"},
                {"url": "tcp://192.168.1.2:44"}]
    }
    )EOF";

      // Upstream::ClusterManagerImpl cm();

      auto ssl_context_manager = std::make_unique<Ssl::ContextManagerImpl>(time_source_);
      envoy::api::v2::Cluster cluster_config; // = parseClusterFromJson(json);
      Envoy::Stats::ScopePtr scope = store_.createScope(fmt::format(
          "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                                : cluster_config.alt_stat_name()));
      Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
          ssl_context_manager, *scope, cm, local_info, dispatcher, random, store_);
      // StaticClusterImpl(cluster_config, runtime, factory_context, std::move(scope), false);
      auto cluster = std::make_unique<StaticClusterImpl>(cluster_config, runtime, factory_context,
                                                         std::move(scope), false);
    }
  */
  uint32_t weight = 1;
  auto host = std::shared_ptr<Upstream::Host>{new Upstream::HostImpl(
      cluster, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"),
      envoy::api::v2::core::Metadata::default_instance(), weight, envoy::api::v2::core::Locality(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0)};

  pool_ = std::make_unique<BenchmarkHttp1ConnPoolImpl>(
      dispatcher, host, Upstream::ResourcePriority::Default, options);
}

bool HttpBenchmarkTimingLoop::tryStartOne(std::function<void()> completion_callback) {
  auto stream_decoder = new Nighthawk::BufferingStreamDecoder(
      [completion_callback]() -> void { completion_callback(); });
  auto cancellable = pool_->newStream(*stream_decoder, *this);
  (void)cancellable;
  return true;
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
  Logger::Registry::setLogLevel(spdlog::level::warn);
  Logger::Logger* logger_to_change = Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::info);
}

bool ClientMain::run() {
  Stats::IsolatedStoreImpl store;
  // TODO(oschaaf): platform specificity need addressing.
  Thread::ThreadFactoryImplPosix thread_factory;
  auto api = new Envoy::Api::Impl(std::chrono::milliseconds(100) /*flush interval*/, thread_factory,
                                  store);
  auto dispatcher = api->allocateDispatcher(real_time_system_);
  HttpBenchmarkTimingLoop bml(*dispatcher, store, real_time_system_);
  bml.start();
  dispatcher->run(Envoy::Event::Dispatcher::RunType::Block);

  // Benchmarker benchmarker(*dispatcher, options_.connections(), options_.requests_per_second(),
  //                        options_.duration(), Headers::get().MethodValues.Get, options_.uri());
  // auto dns_resolver = dispatcher->createDnsResolver({});
  // benchmarker.run(dns_resolver);
  return true;
}

} // namespace Nighthawk
