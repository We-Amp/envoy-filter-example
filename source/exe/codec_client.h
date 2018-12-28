#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
//#include "envoy/upstream/upstream.h"
#include "common/common/assert.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/http/codec_wrappers.h"
#include "common/network/filter_impl.h"

using namespace Envoy;
using namespace Envoy::Http;

namespace Nighthawk {
namespace Http {

/**
 * A self destructing response decoder that discards the response body.
 */
class BufferingStreamDecoder : public StreamDecoder, public StreamCallbacks {
public:
  BufferingStreamDecoder(std::function<void()> on_complete_cb) : on_complete_cb_(on_complete_cb) {}

  bool complete() { return complete_; }
  const HeaderMap& headers() { return *headers_; }

  // Http::StreamDecoder
  void decode100ContinueHeaders(HeaderMapPtr&&) override {}
  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance&, bool end_stream) override;
  void decodeTrailers(HeaderMapPtr&& trailers) override;
  void decodeMetadata(MetadataMapPtr&&) override {}

  // Http::StreamCallbacks
  void onResetStream(StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void onComplete();

  HeaderMapPtr headers_;
  bool complete_{};
  std::function<void()> on_complete_cb_;
};

typedef std::unique_ptr<BufferingStreamDecoder> BufferingStreamDecoderPtr;

} // namespace Http
} // namespace Nighthawk
