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

using namespace Envoy;

namespace Nighthawk {

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

namespace Http {

HttpCodecClientPool::HttpCodecClientPool(Envoy::Event::Dispatcher& dispatcher,
                                         Envoy::Http::Protocol protocol,
                                         Network::Address::InstanceConstSharedPtr target_address,
                                         unsigned int pool_size)
    : dispatcher_(&dispatcher), protocol_(protocol), target_address_(target_address),
      pool_size_(pool_size) {}

HttpCodecClientPool::~HttpCodecClientPool() {}

CodecClient* HttpCodecClientPool::getCodecClient() {
  int amount = pool_size_ - connected_clients_;
  CodecClient* client = nullptr;

  while (amount-- > 0) {
    Network::ClientConnectionPtr connection;
    ASSERT(protocol_ == Envoy::Http::Protocol::Http11);
    // TODO(oschaaf): https, h/2
    connection = dispatcher_->createClientConnection(
        target_address_, Network::Address::InstanceConstSharedPtr(),
        std::make_unique<Network::RawBufferSocket>(), nullptr);
    client = new CodecClientProd(CodecClient::Type::HTTP1, std::move(connection), *dispatcher_);
    connected_clients_++;
    client->setOnConnect([this, client]() {
      codec_clients_.push_back(client);
      // TODO(oschaaf): wake up benchmark loop here.
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
    ASSERT(!client->remoteClosed());
    codec_clients_.pop_front();
  }

  return client;
}

bool HttpCodecClientPool::tryStartRequest(const std::string host, const std::string path) {
  auto client = getCodecClient();
  if (client != nullptr) {

    // TODO(oschaaf): measurement. wire up completion.
    auto stream_decoder = new Nighthawk::BufferingStreamDecoder(
        [this, client]() -> void { codec_clients_.push_back(client); });

    // TODO(oschaaf): its possible we can increase accuracy by
    // writing a precomputed request string directly to the socket
    // in one go.
    StreamEncoder& encoder = client->newStream(*stream_decoder);
    HeaderMapImpl headers;
    headers.insertMethod().value(Headers::get().MethodValues.Get);
    headers.insertPath().value(std::string(path));
    headers.insertHost().value(std::string(host));
    headers.insertScheme().value(Headers::get().SchemeValues.Http);
    encoder.encodeHeaders(headers, true);
  }
  return client != nullptr;
}

CodecClient::CodecClient(Type type, Network::ClientConnectionPtr&& connection,
                         Event::Dispatcher& dispatcher)
    : type_(type), connection_(std::move(connection)),
      idle_timeout_(std::chrono::milliseconds(30000) /*host_->cluster().idleTimeout()*/),
      cb_onConnect_(nullptr), cb_onClose_(nullptr) {
  // Make sure upstream connections process data and then the FIN, rather than processing
  // TCP disconnects immediately. (see https://github.com/envoyproxy/envoy/issues/1679 for details)
  connection_->detectEarlyCloseWhenReadDisabled(false);
  connection_->addConnectionCallbacks(*this);
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new CodecReadFilter(*this)});

  ENVOY_CONN_LOG(debug, "connecting", *connection_);
  connection_->connect();

  if (idle_timeout_) {
    idle_timer_ = dispatcher.createTimer([this]() -> void { onIdleTimeout(); });
    enableIdleTimer();
  }

  connection_->noDelay(true);
}

CodecClient::~CodecClient() {}

void CodecClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void CodecClient::deleteRequest(ActiveRequest& request) {
  connection_->dispatcher().deferredDelete(request.removeFromList(active_requests_));
  if (codec_client_callbacks_) {
    codec_client_callbacks_->onStreamDestroy();
  }
  if (numActiveRequests() == 0) {
    enableIdleTimer();
  }
}

StreamEncoder& CodecClient::newStream(StreamDecoder& response_decoder) {
  ActiveRequestPtr request(new ActiveRequest(*this, response_decoder));
  ENVOY_CONN_LOG(debug, "request start", *connection_);
  request->encoder_ = &codec_->newStream(*request);
  request->encoder_->getStream().addCallbacks(*request);
  request->moveIntoList(std::move(request), active_requests_);
  disableIdleTimer();
  return *active_requests_.front()->encoder_;
}

void CodecClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    ENVOY_CONN_LOG(debug, "connected", *connection_);
    connected_ = true;
    onConnect();
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    remote_closed_ = true;
  }

  // HTTP/1 can signal end of response by disconnecting. We need to handle that case.
  if (type_ == Type::HTTP1 && event == Network::ConnectionEvent::RemoteClose &&
      !active_requests_.empty()) {
    Buffer::OwnedImpl empty;
    onData(empty);
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_CONN_LOG(debug, "disconnect. resetting {} pending requests", *connection_,
                   active_requests_.size());
    disableIdleTimer();
    idle_timer_.reset();
    while (!active_requests_.empty()) {
      // Fake resetting all active streams so that reset() callbacks get invoked.
      active_requests_.front()->encoder_->getStream().resetStream(
          connected_ ? StreamResetReason::ConnectionTermination
                     : StreamResetReason::ConnectionFailure);
    }
    onClose();
    // wipe out the callbacks
    // connection_->removeConnectionCallbacks(*this);
    cb_onClose_ = nullptr;
  }
}

void CodecClient::responseDecodeComplete(ActiveRequest& request) {
  ENVOY_CONN_LOG(debug, "response complete", *connection_);
  deleteRequest(request);

  // HTTP/2 can send us a reset after a complete response if the request was not complete. Users
  // of CodecClient will deal with the premature response case and we should not handle any
  // further reset notification.
  request.encoder_->getStream().removeCallbacks(request);
}

void CodecClient::onReset(ActiveRequest& request, StreamResetReason reason) {
  ENVOY_CONN_LOG(debug, "request reset", *connection_);
  if (codec_client_callbacks_) {
    codec_client_callbacks_->onStreamReset(reason);
  }

  deleteRequest(request);
}

void CodecClient::onData(Buffer::Instance& data) {
  bool protocol_error = false;
  try {
    codec_->dispatch(data);
  } catch (CodecProtocolException& e) {
    ENVOY_CONN_LOG(info, "protocol error: {}", *connection_, e.what());
    close();
    protocol_error = true;
  } catch (PrematureResponseException& e) {
    ENVOY_CONN_LOG(info, "premature response", *connection_);
    close();

    // Don't count 408 responses where we have no active requests as protocol errors
    if (!active_requests_.empty() ||
        Utility::getResponseStatus(e.headers()) != enumToInt(Code::RequestTimeout)) {
      protocol_error = true;
    }
  }

  if (protocol_error) {
    // host_->cluster().stats().upstream_cx_protocol_error_.inc();
  }
}

CodecClientProd::CodecClientProd(Type type, Network::ClientConnectionPtr&& connection,
                                 Event::Dispatcher& dispatcher)
    : CodecClient(type, std::move(connection), dispatcher) {
  switch (type) {
  case Type::HTTP1: {
    codec_ = std::make_unique<Http1::ClientConnectionImpl>(*connection_, *this);
    break;
  }
  case Type::HTTP2: {
    ASSERT(false);
    // codec_ = std::make_unique<Http2::ClientConnectionImpl>(
    //    *connection_, *this, host->cluster().statsScope(), host->cluster().http2Settings());
    break;
  }
  }
}

} // namespace Http
} // namespace Nighthawk
