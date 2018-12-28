#include "exe/codec_client.h"

#include <cstdint>
#include <memory>

#include "common/network/connection_impl.h"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h> /* See NOTES */

#include "common/common/enum_to_int.h"
#include "common/http/exception.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"

// TODO(oschaaf): check.
#include "common/network/address_impl.h"
#include "common/network/raw_buffer_socket.h"

// TODO(oschaaf): rename this file to benchmark_stream_decoder or some such.
using namespace Envoy;

namespace Nighthawk {
namespace Http {

// TODO(oschaaf): rename
void BufferingStreamDecoder::decodeHeaders(Envoy::Http::HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  headers_ = std::move(headers);
  if (complete_) {
    onComplete();
  }
}

void BufferingStreamDecoder::decodeData(Buffer::Instance&, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  if (complete_) {
    onComplete();
  }
}

void BufferingStreamDecoder::decodeTrailers(Envoy::Http::HeaderMapPtr&&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void BufferingStreamDecoder::onComplete() {
  ASSERT(complete_);
  on_complete_cb_();
  delete this;
}

void BufferingStreamDecoder::onResetStream(Envoy::Http::StreamResetReason) {
  // TODO(oschaaf): handle stream resets.
  // ADD_FAILURE();
  delete this;
}

} // namespace Http
} // namespace Nighthawk
