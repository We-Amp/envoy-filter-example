
#pragma once

#include "common/http/http1/conn_pool.h"
#include "envoy/upstream/upstream.h"

using namespace Envoy;

namespace Nighthawk {

class ConnectionPoolCallbacks : public Envoy::Http::ConnectionPool::Callbacks {
public:
  ConnectionPoolCallbacks() {}
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override {
    (void)reason;
    (void)host;
  };
  void onPoolReady(Envoy::Http::StreamEncoder& encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override {
    (void)encoder;
    (void)host;
  };
};

class BenchmarkHttp1ConnPoolImpl : public Envoy::Http::Http1::ConnPoolImpl {
public:
  BenchmarkHttp1ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                             Upstream::ResourcePriority priority,
                             const Network::ConnectionSocket::OptionsSharedPtr& options)
      : ConnPoolImpl(dispatcher, host, priority, options) {}

  Envoy::Http::CodecClientPtr
  createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

} // namespace Nighthawk