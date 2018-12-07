#pragma once

#include <functional>
#include <string>
#include <queue>

#include "exe/codec_client.h"

#include "common/common/logger.h"
#include "envoy/event/dispatcher.h"

namespace Benchmark {

class Benchmarker : Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  Benchmarker(Envoy::Event::Dispatcher& dispatcher, unsigned int connections,
    unsigned int rps, std::string method,
    std::string host, std::string path);
    void run();
private:
  void setupCodecClients(unsigned int number_of_clients);
  void performRequest(std::function<void(std::chrono::nanoseconds)> cb);

  Envoy::Event::Dispatcher* dispatcher_;
  unsigned int connections_;
  unsigned int rps_;
  std::string method_;
  std::string host_;
  std::string path_;
  std::chrono::steady_clock::time_point start_;
  unsigned int current_rps_;
  std::queue<Benchmarking::Http::CodecClientProd*> codec_clients_;
};

} // namespace Benchmark