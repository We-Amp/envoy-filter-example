#pragma once

#include <deque>
#include <functional>
#include <string>

#include "common/common/logger.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "envoy/event/dispatcher.h"

#include "exe/codec_client.h"

namespace Benchmark {

class Benchmarker : Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  Benchmarker(Envoy::Event::Dispatcher& dispatcher, unsigned int connections, unsigned int rps,
              std::chrono::seconds duration, std::string method, std::string host);
  void run(Network::DnsResolverSharedPtr dns_resolver);

private:
  void pulse(bool from_timer);
  Benchmarking::Http::CodecClientProd* setupCodecClients(unsigned int number_of_clients);
  void performRequest(Benchmarking::Http::CodecClientProd* client,
                      std::function<void(std::chrono::nanoseconds)> cb);

  Envoy::Event::Dispatcher* dispatcher_;
  unsigned int connections_;
  unsigned int rps_;
  std::chrono::seconds duration_;
  std::string method_;
  bool is_https_;
  std::string host_;
  uint32_t port_;
  std::string path_;
  std::chrono::steady_clock::time_point start_;
  unsigned int current_rps_;
  std::deque<Benchmarking::Http::CodecClientProd*> codec_clients_;
  Event::TimerPtr timer_;
  int requests_;
  int callback_count_;
  std::vector<int> results_;
  unsigned int connected_clients_;
  bool warming_up_;
  int max_requests_;
  bool dns_failure_;
  Network::Address::InstanceConstSharedPtr target_address_;
  // Ssl::ContextManagerImpl* context_manager_;
  Network::TransportSocketFactoryPtr client_ssl_ctx_;
};

} // namespace Benchmark
