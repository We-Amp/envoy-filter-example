#include "exe/benchmarker.h"

#include <numeric>
#include <string>

#include <fstream>
#include <iostream>

#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "envoy/network/dns.h"

namespace Benchmark {

const auto timer_resolution = std::chrono::milliseconds(2);

Benchmarker::Benchmarker(Envoy::Event::Dispatcher& dispatcher, unsigned int connections,
                         unsigned int rps, std::chrono::seconds duration, std::string method,
                         std::string host, std::string path)
    : dispatcher_(&dispatcher), connections_(connections), rps_(rps), duration_(duration),
      method_(method), host_(host), path_(path), current_rps_(0), requests_(0), callback_count_(0) {
  results_.reserve(duration.count() * rps);
}

void Benchmarker::setupCodecClients(unsigned int number_of_clients) {
  while (codec_clients_.size() < number_of_clients) {
    auto source_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
    // TODO(oschaaf): RawBufferSocket is leaked on exit.
    auto connection = dispatcher_->createClientConnection(
        Network::Utility::resolveUrl("tcp://127.0.0.1:10000"), source_address,
        std::make_unique<Network::RawBufferSocket>(), nullptr);
    auto client = new Benchmarking::Http::CodecClientProd(
        Benchmarking::Http::CodecClient::Type::HTTP1, std::move(connection), *dispatcher_);
    codec_clients_.push(client);
  }
}

void Benchmarker::pulse(bool from_timer) {
  int max_requests = rps_ * duration_.count(); // ~ seconds to run if we hit the right rps
  auto now = std::chrono::steady_clock::now();
  auto dur = now - start_;
  int ms_dur = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
  current_rps_ = requests_ / (ms_dur / 1000.000);
  int due_requests = ((rps_ - current_rps_)) * (ms_dur / 1000.000);

  if ((requests_ % (max_requests / 10)) == 0) {
    ENVOY_LOG(trace, "done {}/{} | rps {} | due {} @ {}", requests_, callback_count_, current_rps_,
              due_requests, ms_dur);
  }

  if ((dur - duration_) > std::chrono::seconds(5)) {
    ENVOY_LOG(info, "requested: {} completed:{} rps: {}", requests_, callback_count_, current_rps_);
    ENVOY_LOG(error, "Benchmarking timed out. {} queries in {} ms", callback_count_, ms_dur);
    dispatcher_->exit();
    return;
  }

  while (requests_ < max_requests && due_requests-- > 0 && codec_clients_.size() > 0) {
    ++requests_;
    performRequest([this, ms_dur, max_requests](std::chrono::nanoseconds nanoseconds) {
      ASSERT(nanoseconds.count() > 0);
      results_.push_back(nanoseconds.count());
      if (++callback_count_ == max_requests) {
        ENVOY_LOG(info, "requested: {} completed:{} rps: {}", requests_, callback_count_, current_rps_);
        ENVOY_LOG(info, "Benchmark done. {} queries in {} ms", callback_count_, ms_dur);
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

void Benchmarker::run() {
  start_ = std::chrono::steady_clock::now();
  setupCodecClients(connections_);

  ENVOY_LOG(info, "target rps: {}, #connections: {}, duration: {} seconds.", rps_, connections_, duration_.count());

  timer_ = dispatcher_->createTimer([this]() { pulse(true); });
  timer_->enableTimer(timer_resolution);
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

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

void Benchmarker::performRequest(std::function<void(std::chrono::nanoseconds)> cb) {
  auto client = codec_clients_.front();
  codec_clients_.pop();
  // XXX(oschaaf): remoteClosed -- probably not a safe approach
  // here, but for now let's go with it.
  // maybe let the client handle this transiently.
  // Also, note that we explicitly do not measure connection setup
  // latency here.
  while (client->remoteClosed()) {
    delete client;
    setupCodecClients(connections_);
    client = codec_clients_.front();
    codec_clients_.pop();
  }

  ASSERT(!client->remoteClosed());
  auto start = std::chrono::steady_clock::now();
  // response self-destructs.
  Benchmarking::BufferingStreamDecoder* response =
      new Benchmarking::BufferingStreamDecoder([this, cb, start, client]() -> void {
        auto dur = std::chrono::steady_clock::now() - start;
        codec_clients_.push(client);
        cb(dur);
      });

  Http::StreamEncoder& encoder = client->newStream(*response);
  Http::HeaderMapImpl headers;
  headers.insertMethod().value(Http::Headers::get().MethodValues.Get);
  headers.insertPath().value(std::string("/"));
  headers.insertHost().value(std::string("127.0.0.1"));
  headers.insertScheme().value(Http::Headers::get().SchemeValues.Http);
  encoder.encodeHeaders(headers, true);
}

} // namespace Benchmark
