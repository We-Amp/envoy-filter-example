#include "exe/benchmark_client.h"

#include "common/http/utility.h"
#include "common/network/utility.h"

namespace Nighthawk {

BenchmarkHttpClient::BenchmarkHttpClient(std::string uri,
                                         Envoy::Http::HeaderMapImplPtr&& request_headers,
                                         bool use_h2)
    : request_headers_(std::move(request_headers)), use_h2_(use_h2) {

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
    port_ = Envoy::Network::Utility::portFromTcpUrl(tcp_url);
    host_ = host_.substr(0, colon_index);
  }
}

void BenchmarkHttpClient::onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                                        Envoy::Upstream::HostDescriptionConstSharedPtr) {
  // TODO(oschaaf): we can probably pull these counters from the stats,
  // and therefore do not have to track them ourselves here.
  // TODO(oschaaf): unify termination of the flow here and from the stream decoder.
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

void BenchmarkHttpClient::onPoolReady(Envoy::Http::StreamEncoder& encoder,
                                      Envoy::Upstream::HostDescriptionConstSharedPtr) {
  encoder.encodeHeaders(*request_headers_, true);
}

} // namespace Nighthawk