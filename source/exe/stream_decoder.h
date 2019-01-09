#pragma once

#include "common/http/codec_wrappers.h"

using namespace Envoy;
using namespace Envoy::Http;

namespace Nighthawk {
namespace Http {

class StreamDecoderCompletionCallback {
public:
  virtual ~StreamDecoderCompletionCallback() {}
  virtual void onComplete(bool success, const HeaderMap& headers) PURE;
};

/**
 * A self destructing response decoder that discards the response body.
 */
class StreamDecoder : public Envoy::Http::StreamDecoder, public Envoy::Http::StreamCallbacks {
public:
  StreamDecoder(std::function<void()> caller_completion_callback,
                StreamDecoderCompletionCallback& on_complete_cb)
      : caller_completion_callback_(caller_completion_callback), on_complete_cb_(on_complete_cb) {}

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
  void onComplete(bool success);

  HeaderMapPtr headers_;
  bool complete_{};
  std::function<void()> caller_completion_callback_;
  StreamDecoderCompletionCallback& on_complete_cb_;
};

} // namespace Http
} // namespace Nighthawk
