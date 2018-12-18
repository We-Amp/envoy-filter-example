#include "exe/benchmarker.h"

#include <numeric>
#include <string>

#include <fstream>
#include <iostream>

#include "common/http/header_map_impl.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "envoy/network/dns.h"

#include "server/transport_socket_config_impl.h"
#include "common/ssl/ssl_socket.h"
#include "envoy/network/transport_socket.h"
#include "common/stats/isolated_store_impl.h"

using namespace Envoy;

namespace Nighthawk {

const auto timer_resolution = std::chrono::milliseconds(1);

Benchmarker::Benchmarker(Envoy::Event::Dispatcher& dispatcher, unsigned int connections,
                         unsigned int rps, std::chrono::seconds duration, std::string method,
                         std::string uri)
    : dispatcher_(&dispatcher), connections_(connections), rps_(rps), duration_(duration),
      method_(method), is_https_(false), host_(""), path_(""), current_rps_(0), requests_(0),
      callback_count_(0), connected_clients_(0), warming_up_(true),
      max_requests_(rps * duration.count()), dns_failure_(true) {
  // preallocate anticipaged space needed for results.
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

  ENVOY_LOG(debug, "uri {} -> is_https [{}] | host [{}] | path [{}] | port [{}]", uri, is_https_,
            host_, path_, port_);

  // TODO(oschaaf): prep up ssl processing.
  if (is_https_) {
    /*
      envoy::api::v2::auth::UpstreamTlsContext tls_context;
      Envoy::Ssl::ContextManagerImpl manager(dispatcher.timeSystem());
      //Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(manager, );
      auto client_cfg = std::make_unique<Envoy::Ssl::ClientContextConfigImpl>(tls_context,
      factory_context); Stats::IsolatedStoreImpl client_stats_store; Ssl::ClientSslSocketFactory
      client_ssl_socket_factory(std::move(client_cfg), manager, client_stats_store);
*/

    // Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
    //    ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
    // Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format("cluster.{}.", cluster.name()));

    // auto cfg = std::make_unique<Ssl::ClientContextConfigImpl>(tls_context, factory_context_);
    /*

      mock_cluster_info_->transport_socket_factory_ = std::make_unique<Ssl::ClientSslSocketFactory>(
          std::move(cfg), context_manager_, *stats_store_);
      ON_CALL(*mock_cluster_info_, transportSocketFactory())
          .WillByDefault(ReturnRef(*mock_cluster_info_->transport_socket_factory_));
      async_client_transport_socket_ =
          mock_cluster_info_->transport_socket_factory_->createTransportSocket(nullptr);
      client_ssl_ctx_ = createClientSslTransportSocketFactory({}, context_manager_);
    */
  }
}

Nighthawk::Http::CodecClientProd*
Benchmarker::setupCodecClients(unsigned int number_of_clients) {
  int amount = number_of_clients - connected_clients_;
  Nighthawk::Http::CodecClientProd* client = nullptr;

  while (amount-- > 0) {
    Network::ClientConnectionPtr connection;

    if (!is_https_) {
      connection = dispatcher_->createClientConnection(
          target_address_, Network::Address::InstanceConstSharedPtr(),
          std::make_unique<Network::RawBufferSocket>(), nullptr);
    } else {
      /*
      envoy::api::v2::auth::UpstreamTlsContext tls_context;
      Ssl::ContextManagerImpl manager(dispatcher_->timeSystem());
      Stats::IsolatedStoreImpl stats;
      Stats::ScopePtr scope = stats.createScope("sslclient.");
      Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        manager, *scope, cm, local_info, dispatcher, random, stats);

      Ssl::ClientContextConfigImpl cfg(tls_context, factory_context);
      Ssl::ClientContextSharedPtr ctx(new Ssl::ClientContextImpl(*scope, cfg,
      dispatcher_->timeSystem())); Network::TransportSocketOptionsSharedPtr
      transport_socket_options; connection = dispatcher_->createClientConnection( target_address_,
      Network::Address::InstanceConstSharedPtr(), std::make_unique<Ssl::SslSocket>(ctx,
      Ssl::InitialState::Client, transport_socket_options), nullptr);*/
    };

    // TODO(oschaaf): implement h/2.
    auto client = new Nighthawk::Http::CodecClientProd(
        Nighthawk::Http::CodecClient::Type::HTTP1, std::move(connection), *dispatcher_);
    connected_clients_++;
    client->setOnConnect([this, client]() {
      codec_clients_.push_back(client);
      pulse(false);
    });
    client->setOnClose([this, client]() {
      codec_clients_.erase(std::remove(codec_clients_.begin(), codec_clients_.end(), client),
                           codec_clients_.end());
      connected_clients_--;
    });
    return nullptr;
  }
  if (codec_clients_.size() > 0) {
    client = codec_clients_.front();
    codec_clients_.pop_front();
  }
  return client;
}

void Benchmarker::pulse(bool from_timer) {
  auto now = std::chrono::steady_clock::now();
  auto dur = now - start_;
  double ms_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count() / 1000000.0;
  current_rps_ = requests_ / (ms_dur / 1000.0);
  int due_requests = ((rps_ - current_rps_)) * (ms_dur / 1000.0);

  // 1.001 seconds to serve the lowest 1 rps threshold
  if (warming_up_ && dur > std::chrono::microseconds(1000001)) {
    ENVOY_LOG(info, "warmup completed. requested: {} rps: {}", requests_, current_rps_);
    warming_up_ = false;
    requests_ = 0;
    callback_count_ = 0;
    current_rps_ = 0;
    results_.clear();
    start_ = std::chrono::steady_clock::now();
    nanosleep((const struct timespec[]){{0, 1000000L}}, NULL);
    pulse(from_timer);
    return;
  }

  if ((dur - duration_) >= std::chrono::milliseconds(0)) {

    ENVOY_LOG(info, "requested: {} completed:{} rps: {}", requests_, callback_count_, current_rps_);
    ENVOY_LOG(info, "{} ms benmark run completed.", ms_dur);
    dispatcher_->exit();
    return;
  }

  // To increase accuracy we sleep/spin when no requests are due, and
  // no inbound events are expected.(we don't want to delay those!)
  //
  // TODO(oschaaf): this could block timeout & close events from open connections.
  //   (but those shouldn't influence measurements.)
  // TODO(oschaaf): this will not alway work well:
  //    E.g: when there are long delays between us writing a request
  //    and receiving the corresponding notifications of the response stream.
  // TODO(oschaaf): discuss: we may want to experiment with driving
  // the loop from another thread, e.g. by listening to a file descriptor here
  // and writing single bytes to it from the other thread at a high frequency.
  if (due_requests == 0 && connected_clients_ == codec_clients_.size()) {
    nanosleep((const struct timespec[]){{0, 500L}}, NULL);
    timer_->enableTimer(std::chrono::milliseconds(0));
    return;
  }

  while (requests_ < max_requests_ && due_requests-- > 0) {
    auto client = setupCodecClients(connections_);
    if (client == nullptr) {
      timer_->enableTimer(std::chrono::milliseconds(timer_resolution));
      return;
    }

    ++requests_;

    performRequest(client, [this](std::chrono::nanoseconds nanoseconds) {
      ASSERT(nanoseconds.count() > 0);
      results_.push_back(nanoseconds.count());
      if (++callback_count_ == this->max_requests_) {
        dispatcher_->exit();
        return;
      }
      timer_->enableTimer(std::chrono::milliseconds(0));
    });
  }

  if (from_timer) {
    timer_->enableTimer(timer_resolution);
  }
}

void Benchmarker::run(Network::DnsResolverSharedPtr dns_resolver) {
  ENVOY_LOG(info, "Benchmarking [{}]", this->host_);

  // TODO(oschaaf): ipv6
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
        dispatcher_->exit();
      });
  // Wait for DNS resolution to complete before proceeding.
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
  if (dns_failure_) {
    exit(1);
  }

  ENVOY_LOG(info, "target rps: {}, #connections: {}, duration: {} seconds.", rps_, connections_,
            duration_.count());

  // Kick off the benchmark run.
  timer_ = dispatcher_->createTimer([this]() { pulse(true); });
  timer_->enableTimer(timer_resolution);
  start_ = std::chrono::steady_clock::now();
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

  // TODO(oschaaf): we should only do this on request:
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
}

void Benchmarker::performRequest(Nighthawk::Http::CodecClientProd* client,
                                 std::function<void(std::chrono::nanoseconds)> cb) {
  ASSERT(client);
  ASSERT(!client->remoteClosed());
  auto start = std::chrono::steady_clock::now();
  // response self-destructs.
  Nighthawk::BufferingStreamDecoder* response =
      new Nighthawk::BufferingStreamDecoder([this, cb, start, client]() -> void {
        auto dur = std::chrono::steady_clock::now() - start;
        codec_clients_.push_back(client);
        cb(dur);
      });

  // TODO(oschaaf): its possible we can increase accuracy by
  // writing a precomputed request string directly to the socket
  // in one go.
  StreamEncoder& encoder = client->newStream(*response);
  HeaderMapImpl headers;
  headers.insertMethod().value(Headers::get().MethodValues.Get);
  headers.insertPath().value(std::string(path_));
  headers.insertHost().value(std::string(host_));
  headers.insertScheme().value(Headers::get().SchemeValues.Http);
  encoder.encodeHeaders(headers, true);
}

} // namespace Nighthawk
