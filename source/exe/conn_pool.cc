#include "exe/conn_pool.h"
#include "common/upstream/upstream_impl.h"

using namespace Envoy;
using namespace Envoy::Http;

namespace Nighthawk {

CodecClientPtr
BenchmarkHttp1ConnPoolImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP1, std::move(data.connection_),
                                           data.host_description_, dispatcher_)};
  return codec;
}

} // namespace Nighthawk
