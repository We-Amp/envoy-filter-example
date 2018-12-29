#include "exe/stream_decoder.h"

#include "common/http/http1/codec_impl.h"
#include "common/http/utility.h"

using namespace Envoy;

namespace Nighthawk {
namespace Http {

void StreamDecoder::decodeHeaders(Envoy::Http::HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  headers_ = std::move(headers);
  if (complete_) {
    onComplete();
  }
}

void StreamDecoder::decodeData(Buffer::Instance&, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  if (complete_) {
    onComplete();
  }
}

void StreamDecoder::decodeTrailers(Envoy::Http::HeaderMapPtr&&) { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

void StreamDecoder::onComplete() {
  ASSERT(complete_);
  on_complete_cb_();
  delete this;
}

void StreamDecoder::onResetStream(Envoy::Http::StreamResetReason) {
  // TODO(oschaaf): handle stream resets.
  // ADD_FAILURE();
  delete this;
}

} // namespace Http
} // namespace Nighthawk
