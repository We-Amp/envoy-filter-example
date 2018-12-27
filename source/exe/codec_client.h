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

namespace Http {

/**
 * Callbacks specific to a codec client.
 */
class CodecClientCallbacks {
public:
  virtual ~CodecClientCallbacks() {}

  /**
   * Called every time an owned stream is destroyed, whether complete or not.
   */
  virtual void onStreamDestroy() PURE;

  /**
   * Called when a stream is reset by the client.
   * @param reason supplies the reset reason.
   */
  virtual void onStreamReset(StreamResetReason reason) PURE;
};

/**
 * This is an HTTP client that multiple stream management and underlying connection management
 * across multiple HTTP codec types.
 */
class CodecClient : Logger::Loggable<Logger::Id::client>,
                    public Envoy::Http::ConnectionCallbacks,
                    public Network::ConnectionCallbacks,
                    public Event::DeferredDeletable {
public:
  /**
   * Type of HTTP codec to use.
   */
  enum class Type { HTTP1, HTTP2 };

  ~CodecClient();

  /**
   * Add a connection callback to the underlying network connection.
   */
  void addConnectionCallbacks(Network::ConnectionCallbacks& cb) {
    connection_->addConnectionCallbacks(cb);
  }

  /**
   * Close the underlying network connection. This is immediate and will not attempt to flush any
   * pending write data.
   */
  void close();

  /**
   * Send a codec level go away indication to the peer.
   */
  void goAway() { codec_->goAway(); }

  /**
   * @return the underlying connection ID.
   */
  uint64_t id() { return connection_->id(); }

  /**
   * @return size_t the number of outstanding requests that have not completed or been reset.
   */
  size_t numActiveRequests() { return active_requests_.size(); }

  /**
   * Create a new stream. Note: The CodecClient will NOT buffer multiple requests for HTTP1
   * connections. Thus, calling newStream() before the previous request has been fully encoded
   * is an error. Pipelining is supported however.
   * @param response_decoder supplies the decoder to use for response callbacks.
   * @return StreamEncoder& the encoder to use for encoding the request.
   */
  StreamEncoder& newStream(StreamDecoder& response_decoder);

  void setConnectionStats(const Network::Connection::ConnectionStats& stats) {
    connection_->setConnectionStats(stats);
  }

  void setCodecClientCallbacks(CodecClientCallbacks& callbacks) {
    codec_client_callbacks_ = &callbacks;
  }

  void setCodecConnectionCallbacks(Envoy::Http::ConnectionCallbacks& callbacks) {
    codec_callbacks_ = &callbacks;
  }

  bool remoteClosed() const { return remote_closed_; }

  Type type() const { return type_; }
  void setOnConnect(std::function<void()> cb) { cb_onConnect_ = cb; }
  void setOnClose(std::function<void()> cb) { cb_onClose_ = cb; }

protected:
  /**
   * Create a codec client and connect to a remote host/port.
   * @param type supplies the codec type.
   * @param connection supplies the connection to communicate on.
   * @param host supplies the owning host.
   */
  CodecClient(Type type, Network::ClientConnectionPtr&& connection, Event::Dispatcher& dispatcher);

  // Http::ConnectionCallbacks
  void onGoAway() override {
    if (codec_callbacks_) {
      codec_callbacks_->onGoAway();
    }
  }

  void onIdleTimeout() {
    // host_->cluster().stats().upstream_cx_idle_timeout_.inc();
    close();
  }

  void onConnect() {
    // TODO(oschaaf): figure out why this one isn't set.
    if (cb_onConnect_ != nullptr) {
      cb_onConnect_();
    }
  }
  void onClose() {
    if (cb_onClose_ != nullptr) {
      cb_onClose_();
    }
  }

  void disableIdleTimer() {
    if (idle_timer_ != nullptr) {
      idle_timer_->disableTimer();
    }
  }

  void enableIdleTimer() {
    if (idle_timer_ != nullptr) {
      idle_timer_->enableTimer(idle_timeout_.value());
    }
  }

  const Type type_;
  ClientConnectionPtr codec_;
  Network::ClientConnectionPtr connection_;
  // Upstream::HostDescriptionConstSharedPtr host_;
  Event::TimerPtr idle_timer_;
  const absl::optional<std::chrono::milliseconds> idle_timeout_;
  std::function<void()> cb_onConnect_;
  std::function<void()> cb_onClose_;

private:
  /**
   * Wrapper read filter to drive incoming connection data into the codec. We could potentially
   * support other filters in the future.
   */
  struct CodecReadFilter : public Network::ReadFilterBaseImpl {
    CodecReadFilter(CodecClient& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

    CodecClient& parent_;
  };

  struct ActiveRequest;

  /**
   * Wrapper for an outstanding request. Designed for handling stream multiplexing.
   */
  struct ActiveRequest : LinkedObject<ActiveRequest>,
                         public Event::DeferredDeletable,
                         public StreamCallbacks,
                         public StreamDecoderWrapper {
    ActiveRequest(CodecClient& parent, StreamDecoder& inner)
        : StreamDecoderWrapper(inner), parent_(parent) {}

    // StreamCallbacks
    void onResetStream(StreamResetReason reason) override { parent_.onReset(*this, reason); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // StreamDecoderWrapper
    void onPreDecodeComplete() override { parent_.responseDecodeComplete(*this); }
    void onDecodeComplete() override {}

    StreamEncoder* encoder_{};
    CodecClient& parent_;
  };

  typedef std::unique_ptr<ActiveRequest> ActiveRequestPtr;

  /**
   * Called when a response finishes decoding. This is called *before* forwarding on to the
   * wrapped decoder.
   */
  void responseDecodeComplete(ActiveRequest& request);

  void deleteRequest(ActiveRequest& request);
  void onReset(ActiveRequest& request, StreamResetReason reason);
  void onData(Buffer::Instance& data);

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  // Pass watermark events from the connection on to the codec which will pass it to the underlying
  // streams.
  void onAboveWriteBufferHighWatermark() override {
    codec_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }
  void onBelowWriteBufferLowWatermark() override {
    codec_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }

  std::list<ActiveRequestPtr> active_requests_;
  Envoy::Http::ConnectionCallbacks* codec_callbacks_{};
  CodecClientCallbacks* codec_client_callbacks_{};
  bool connected_{};
  bool remote_closed_{};
};

typedef std::unique_ptr<CodecClient> CodecClientPtr;

/**
 * Production implementation that installs a real codec.
 */
class CodecClientProd : public CodecClient {
public:
  CodecClientProd(Type type, Network::ClientConnectionPtr&& connection,
                  Event::Dispatcher& dispatcher);
};

// TODO(oschaaf): untie from connection pool. check envoy codebase for
// similar functionality. Ideally we can layer the pooling of codecs on top
// of the connections.
class HttpCodecClientPool {
public:
  HttpCodecClientPool(Envoy::Event::Dispatcher& dispatcher, Envoy::Http::Protocol protocol,
                      Network::Address::InstanceConstSharedPtr target_address_,
                      unsigned int pool_size);
  ~HttpCodecClientPool();
  bool tryStartRequest(const std::string host, const std::string path);

private:
  CodecClient* getCodecClient();
  std::deque<Nighthawk::Http::CodecClient*> codec_clients_;
  unsigned int connected_clients_;
  Envoy::Event::Dispatcher* dispatcher_;
  Envoy::Http::Protocol protocol_;
  Network::Address::InstanceConstSharedPtr target_address_;
  unsigned int pool_size_;
};

} // namespace Http
} // namespace Nighthawk
