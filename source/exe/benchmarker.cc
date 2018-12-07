#include "exe/benchmarker.h"

#include <numeric>
#include <string>

#include "envoy/network/dns.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

#define NR_CLIENTS 1

namespace Benchmark {

Benchmarker::Benchmarker(Envoy::Event::Dispatcher& dispatcher,
  unsigned int connections, unsigned int rps,
  std::string method, std::string host, std::string path) :
  dispatcher_(&dispatcher),
  connections_(connections),
  rps_(rps),
  method_(method),
  host_(host),
  path_(path),
  current_rps_(rps) {
}

void Benchmarker::setupCodecClients(unsigned int number_of_clients){
  while (codec_clients_.size() < number_of_clients) {
    auto source_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
    auto connection = dispatcher_->createClientConnection(
            Network::Utility::resolveUrl("tcp://127.0.0.1:10000"),
            source_address,
            std::make_unique<Network::RawBufferSocket>(), nullptr);
    // TODO: if this actually connects, we need to start measurement here.
    auto client = new Benchmarking::Http::CodecClientProd(
      Benchmarking::Http::CodecClient::Type::HTTP1,
      std::move(connection),
      *dispatcher_
    );
    codec_clients_.push(client);
  }
}

void Benchmarker::run() {
  std::chrono::milliseconds loop_resolution(5);
  start_ = std::chrono::steady_clock::now();
  current_rps_ = rps_;

  int count = 0;
  int r = 0;
  const int max_requests = 5000;
  auto results = std::list<int>();

  int x = system("clear");
  (void)x;
  setupCodecClients(NR_CLIENTS);
  bool done = false;
  int callback_count = 0;
  
  Event::TimerPtr timer = dispatcher_->createTimer([&]() -> void {
      auto now = std::chrono::steady_clock::now();
      auto dur = now - start_;
      int ms_dur = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
      current_rps_ = ms_dur > 0 ? (r * 1000) / ms_dur : rps_;
      int nr = ((rps_ - current_rps_)) * (ms_dur/1000);

      if (((count) % (500/loop_resolution.count())) == 0){
        std::cout << "\rrps:" << (ms_dur > 0 ? (r * 1000.00) / ms_dur : rps_*1.00) << ", desired: " << rps_ << std::flush;
      }
      while (!done && nr-- > 0 && codec_clients_.size() > 0) {
        performRequest([count, &callback_count, &results](std::chrono::nanoseconds nanoseconds) {
	    ASSERT(nanoseconds.count() > 0);
          results.push_back(nanoseconds.count());
	  callback_count++;
        });
        if (++r == max_requests) {
	  done = true;
        }
      }
      
      if (done && codec_clients_.size() == NR_CLIENTS) {
	ENVOY_LOG(info, "\nBenchmark done. {} queries in {} ms", callback_count, ms_dur);
          dispatcher_->exit();
          return;
      }

      timer->enableTimer(loop_resolution);
      count++;
    });
  timer->enableTimer(loop_resolution);
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

  for (int r: results) {
    ENVOY_LOG(info, "{} us", (r/1000));
  }

  double average = std::accumulate(results.begin(), results.end(), 0.0) / results.size();
  ENVOY_LOG(info, "avg lat. {} us over {} callbacks", (average/1000), results.size());
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
    setupCodecClients(NR_CLIENTS);
    client = codec_clients_.front();
    codec_clients_.pop();
  }
  auto start = std::chrono::steady_clock::now();
			    
  ASSERT(!client->remoteClosed());
  Benchmarking::BufferingStreamDecoder* response = new Benchmarking::BufferingStreamDecoder([this, cb,start,client]() -> void {
    auto dur = std::chrono::steady_clock::now() - start;
    // TODO(oschaaf): Our bufferingStreamDecoder self destructs.
    // Check if we need to cleanup more stuff upon completion/
    // e.g. client->close()? What about stream resets?
    if (client->remoteClosed()) {
      delete client;
      setupCodecClients(NR_CLIENTS);
    } else {
      codec_clients_.push(client);
    }
    cb(dur);
  });

  Http::StreamEncoder& encoder = client->newStream(*response);
  // TODO(oschaaf): check the line below.
  //encoder.getStream().addCallbacks(*response);

  Http::HeaderMapImpl headers;
  headers.insertMethod().value(Http::Headers::get().MethodValues.Get);
  headers.insertPath().value(std::string("/"));
  headers.insertHost().value(std::string("127.0.0.1"));
  headers.insertScheme().value(Http::Headers::get().SchemeValues.Http);
  encoder.encodeHeaders(headers, true);
}

} // namespace Benchmark
