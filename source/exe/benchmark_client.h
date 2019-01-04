#pragma once

#include "common/common/logger.h"
#include "common/http/header_map_impl.h"
//#include "common/http/headers.h"

#include "common/runtime/runtime_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "envoy/http/conn_pool.h"
#include "envoy/network/address.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/store.h"
#include "envoy/upstream/upstream.h"

namespace Nighthawk {

class BenchmarkHttpClient : public Envoy::Logger::Loggable<Envoy::Logger::Id::main>,
                            public Envoy::Http::ConnectionPool::Callbacks {
public:
  BenchmarkHttpClient(Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
                      Envoy::TimeSource& time_source, std::string uri,
                      Envoy::Http::HeaderMapImplPtr&& request_headers, bool use_h2);

  void initialize();

  // ConnectionPool::Callbacks
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::StreamEncoder& encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::Stats::Store& store_;
  Envoy::TimeSource& time_source_;

  Envoy::Http::HeaderMapImplPtr request_headers_;

  uint64_t pool_connect_failures_;
  uint64_t pool_overflow_failures_;

  bool use_h2_;
  bool is_https_;
  std::string host_;
  uint32_t port_;
  std::string path_;

  bool dns_failure_;
  Envoy::Network::Address::InstanceConstSharedPtr target_address_;
  std::chrono::seconds timeout_;
  uint64_t max_connections_;
  bool h2_;
  Envoy::Http::ConnectionPool::InstancePtr pool_;

  Envoy::Event::TimerPtr timer_;
  std::unique_ptr<Envoy::ThreadLocal::InstanceImpl> tls_;
  std::unique_ptr<Envoy::Runtime::LoaderImpl> runtime_;
  Envoy::Runtime::RandomGeneratorImpl generator_;
  /*
    std::chrono::seconds timeout_;
    uint64_t max_connections_;
    Envoy::Http::ConnectionPool::InstancePtr pool_;

    bool dns_failure_;
    Envoy::Network::Address::InstanceConstSharedPtr target_address_;
  */
};

} // namespace Nighthawk