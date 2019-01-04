#pragma once

#include "common/common/logger.h"
#include "common/http/header_map_impl.h"
//#include "common/http/headers.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/address.h"

namespace Nighthawk {

class BenchmarkHttpClient : public Envoy::Logger::Loggable<Envoy::Logger::Id::main>,
                            public Envoy::Http::ConnectionPool::Callbacks {
public:
  BenchmarkHttpClient(std::string uri, Envoy::Http::HeaderMapImplPtr&& request_headers,
                      bool use_h2);
  // ConnectionPool::Callbacks
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::StreamEncoder& encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Envoy::Http::HeaderMapImplPtr request_headers_;

  uint64_t pool_connect_failures_;
  uint64_t pool_overflow_failures_;

  bool use_h2_;
  bool is_https_;
  std::string host_;
  uint32_t port_;
  std::string path_;

  /*
    std::chrono::seconds timeout_;
    uint64_t max_connections_;
    Envoy::Http::ConnectionPool::InstancePtr pool_;

    bool dns_failure_;
    Envoy::Network::Address::InstanceConstSharedPtr target_address_;
  */
};

} // namespace Nighthawk