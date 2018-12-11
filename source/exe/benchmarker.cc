#include "exe/benchmarker.h"

#include <numeric>
#include <string>

#include <fstream>
#include <iostream>

#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/http/utility.h"
#include "envoy/network/dns.h"

namespace Benchmark {

const auto timer_resolution = std::chrono::milliseconds(1);

Benchmarker::Benchmarker(Envoy::Event::Dispatcher& dispatcher, unsigned int connections,
                         unsigned int rps, std::chrono::seconds duration, std::string method,
                         std::string uri)
    : dispatcher_(&dispatcher), connections_(connections), rps_(rps), duration_(duration),
      method_(method), host_(""), path_(""), current_rps_(0), requests_(0), callback_count_(0),
      connected_clients_(0), warming_up_(true), max_requests_(rps * duration.count()) {
  results_.reserve(duration.count() * rps);
  absl::string_view host, path;
  Envoy::Http::Utility::extractHostPathFromUri(uri, host, path);
  host_ = std::string(host);
  path_ = std::string(path);
}

Benchmarking::Http::CodecClientProd*
Benchmarker::setupCodecClients(unsigned int number_of_clients) {
  int amount = number_of_clients - connected_clients_;
  Benchmarking::Http::CodecClientProd* client = nullptr;

  while (amount-- > 0) {
    auto source_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
    auto connection = dispatcher_->createClientConnection(
        Network::Utility::resolveUrl(fmt::format("tcp://{}", host_)), source_address,
        std::make_unique<Network::RawBufferSocket>(), nullptr);
    auto client = new Benchmarking::Http::CodecClientProd(
        Benchmarking::Http::CodecClient::Type::HTTP1, std::move(connection), *dispatcher_);
    connected_clients_++;
    client->setOnConnect([this, client]() {
      codec_clients_.push_back(client);
      pulse(false);
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
    codec_clients_.pop_front();
  }
  return client;
}

void Benchmarker::pulse(bool from_timer) {
  auto now = std::chrono::steady_clock::now();
  auto dur = now - start_;
  double ms_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count() / 1000000.0;
  current_rps_ = requests_ / (ms_dur / 1000.0);
  int due_requests = ((rps_ - current_rps_)) * (ms_dur / 1000.0);

  // 1.001 seconds to serve the lowest 1 rps threshold
  if (warming_up_ && dur > std::chrono::microseconds(1000001)) {
    ENVOY_LOG(info, "warmup completed. requested: {} rps: {}", requests_, current_rps_);
    warming_up_ = false;
    requests_ = 0;
    callback_count_ = 0;
    current_rps_ = 0;
    results_.clear();
    start_ = std::chrono::steady_clock::now();
    nanosleep((const struct timespec[]){{0, 1000000L}}, NULL);
    pulse(from_timer);
    return;
  }

  if ((dur - duration_) >= std::chrono::milliseconds(0)) {
    ENVOY_LOG(info, "requested: {} completed:{} rps: {}", requests_, callback_count_, current_rps_);
    ENVOY_LOG(error, "{} ms benmark run completed.", ms_dur);
    dispatcher_->exit();
    return;
  }

  // To increase accuracy we sleep/spin when no requests are due, and
  // no inbound events are expected.
  // TODO(oschaaf): this could block timeout events from open connections,
  // (but those shouldn't influence measurements.)
  // TODO(oschaaf): more importantly, this may not work well with long
  // running transactions. Thought: we may want to experiment with driving
  // the loop from another thread, e.g. by listening to a file descriptor here
  // and writing single bytes to it from the other thread at a high frequency.
  if (due_requests == 0 && connected_clients_ == codec_clients_.size()) {
    nanosleep((const struct timespec[]){{0, 500L}}, NULL);
    timer_->enableTimer(std::chrono::milliseconds(0));
    return;
  }

  while (requests_ < max_requests_ && due_requests-- > 0) {
    auto client = setupCodecClients(connections_);
    if (client == nullptr) {
      timer_->enableTimer(std::chrono::milliseconds(timer_resolution));
      return;
    }

    // if (!warming_up_ && (requests_ % (max_requests_ / 10)) == 0) {
    // ENVOY_LOG(trace, "done {}/{} | rps {} | due {} @ {}ms. | client {}", requests_,
    // callback_count_, current_rps_,
    //        due_requests, ms_dur, connected_clients_);
    //}

    ++requests_;
    performRequest(client, [this, ms_dur](std::chrono::nanoseconds nanoseconds) {
      ASSERT(nanoseconds.count() > 0);
      // OS: rare latency spikes
      // ASSERT(nanoseconds.count() < 10000000);
      results_.push_back(nanoseconds.count());
      if (++callback_count_ == this->max_requests_) {
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
  std::string logo("\
,,,,,,,,...............................................................,.................................................................................,,,,,,\n\
,,,...................................................................(#/...............................................................................,,,,,,,\n\
......................................................................*%%,..............................................................................,,,,,,,\n\
......................................................................./&/...............................................................................,,,,,,\n\
.......................................................................,&%/............................................................................,,.,,,,,\n\
.....,,,................................................................#&%,..........................................................................,,,,,,,,,\n\
..../(((#((((((*...............................................   ....../&&/..........................,*(*.............................................,,,,,,,,\n\
....,(############(,......,...,..........................................%&%,.......................*//*...........................................,,,,,,,,,,,,\n\
.....,(#(##(########(/,....,,............................................#%.....................///*............................................,,,,,,,,,,,,,\n\
,,,,.,.,(#(#############(*,..............................................*#&&*.................*///*...............................................,..,,,,,,,,,\n\
,,,,,..../((#(#############/*............................................./&*..............*///*................................................,,,,,,,,,,,,.\n\
..........,(###############%%#/...........................................,&&............*///*,..............................................,,,..,,.......,,\n\
............/(###################(,........................................(&&&*........,/////......................................,,................,,.,,,,,,\n\
.............*(####################(/......................................*&&&%......*(/(//,..........................................................,,,,,,,,\n\
...............*#######################(*.........//*,........... ..........%&&&,..*((///*.......................................   ..............,,,,,,,,,,,,,\n\
................,/###(#####################*....**////////(((((((((((((((((#%%%((/(((/,..................................   ..............,,,,,,,,,,,,,,,,,,,\n\
..................,(#########################((//////////,.(#%(((((((#(##(((###(####((,.........................          ........................,,,,,,,,,,,,,\n\
....................(#########################(#%#///((##%%%%#((//((//*,/((((//*//(###%%#,............            .... ........,,.,,,,,,,..,,,,,,,,,,,,,,,,,,,,\n\
....................../#####################(/*,/(((######%%%#((///(*(((//(((((#####***((###(,...         ...........................,,,,,,,,,,,,,,,,,,,,,,,,,,\n\
.......................*(##################(///(((((##%%%%%%%#////((((((((//((//(((####(/*(####/*. ................,.........,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\n\
........................./###############(((((((((((#%%%%%%%#*//(((((/(((((((((#%%#(/((###########/,......................,,,,,.....,,,,,,,,,,,,,,,,,,,,,,,,,,,\n\
..........................,(############(((((((*/(((%%%%%##%#/(//((((((((#((/*#&@&&&@&&@@@@@@&&&%%#(##/*,....................,,,,,......,,,,,,,,,,,,,,,,,,,,,,,\n\
............................*###########((((/**/((((##%%%%%##/(//((((((#%%%/,&%&@@&@@&&&&@@@@@@@@@,#%%#,,.......................,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\n\
..............................(#########(((//((((#&&&%(#####(////(((#%%%#*//&&&%#%@@@@@@@@@@@@@&&/##%%%%*...............,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\n\
...............................*(#######((((((((&&&&&&&((#%#(//(((((#%#(*(/%@&%@&%##&@&@@@@@@@@@@&&&&&%%%%%%#..........,,.,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\n\
.................................*######(((((#%&&&&&%#&&&%#(//(#(/(#(/*(/#&@@&%&&##%@@@@@@@@@@&&&&&&&&&%&%%/*,,,,,,,,,,,,,,**********************,,,,,,,,,,,,\n\
................................. ,/####(((((&&&&&%&&&&&&(///((/##*/%#(&@@@&%&&##%@@@@@@@@@@&&&&&&&&@&&&%##(((#############((((###%%%%%#%%%%#####(*,,,,,,,,\n\
...............................  ...*###(((,.*#&&&&&&&&(((((((/,**,/&%&&&&%&%&%##%@@@@@@@@@@@&&&&&&@@@&%&%%%%%%%###%###%%%%%%%%%%%%%%%%%%%%%%%%%%###(*,,,,,\n\
......................         .......(#(.     ,(%&&&&&&%((((((((,,..,%&&&&&&%&@%&%#%&@@@@&@@@@@@@&&&@@@@@@&&%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%/*,,\n\
.........                 ........... .,/.      .,#&&&%((((((((*,....(&&%&&&&&&@@@&@@@@@@@@@@&&@&&&@@&&@@@&&&%%#%%%%%%%%%%%%%%%%%%%%%%%%%&&&&&&&&&&&%%%%%(*,,\n\
........    ..........,............. ....*((,      ,(((((((((/.....,(%&@%%%&&%%@@@@&@@@&@@@@@@@@@@@@@@@@%@@@@@&%%%%%%%%%%%%%%%%%&&&&&&&&&&&&&&&&&%&%%#/,,,,****\n\
.       ...,,..............................(#(/.    /((((((//*....*(#&&&&&&@&%%@@@@@@@@@@@@@@@@@@@@@@@@@&%@@@@@&%&%%%%%%%%%&&&&&&&&&&&%%&%%&&&&%#(*,,,,,,,*,*,,\n\
..,,,,,,,,,...........................,.....,(###(*(((((((***,...*%%%%#%&&&%&@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@&%%%%%%%%%&&&&&&%%%%%%%%%%%#/*,,*,,*,,********//\n\
,.,,,,,,,.........................,,,........,*####((((/*,***...*%%&%%%#%&&%&%&@@@@@@@@&&@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%%%%&&%%%%%%(/*******,,,,,,***********\n\
,,..........................,............,.,,.,,(##((/*****,,.,#%&&,./@&&&&&&@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@&%%%%%%%%%%&%%#/////(//////********************\n\
...........,,.............,...,,,,,,,,,,,,,,.,,,,*#(*******,..(&&%#*../&@&&&&&@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@&%%&%%%%%%#/*,****/((/(((////*/****/////**/****/\n\
...,,,,,,,,,,,,,,,....,,,,,,,,,,,,,,,,,,,,,,*/*************,,/&%%,....%@@@&&&&@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@&%%%%%(//****,,****///((/////////*//*///*////////\n\
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,****,********************,,(%/...,,,/@@@@@&&&@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%#(/*************/**/**//////***//*////////*//**///\n\
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,**********************,*******##,.,,,,,,%@@@@@&&&@@@@@@@@@@@@@@@@@@@@@@@@&//********************/****//**/***/**////(///////////\n\
*,,,,,,****/***,,,*******,,****,,,,**,,,,,,,*,,***,*,,,,***.....,,,,/@@@@@@@&@@@@@@@@@@@@@@@@@@@/*,,,**************,*********,,*****//*******/////**/////////\n\
*******////////**********,,*******,.,,,,,,,,,,,,,,,,,,,,,*(*.....,,,%@@@@@@@&@@@@@@@@@@@@@@%#*,,,,**,******,,,,,,***,,,***********//////******/(//*////////////\n\
***************,*,,*********,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,*,,.,....*@@@@@@@@&@@@@@@@%#/,,,,,,,*,,,,,,,,,,,,*,,,,**,,,,,,,********************/*****//***///**//\n\
**//**********,***********,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,/(...*%@@@@@@@&&@@%(/,,,,,,,,,,,,,,,,,,,,,,,,,,**,,,,,,,,,,*************************///******/////\n\
**//*******************,,,,,,,,,,,,,*,,,,,,**,,,,,,,,,,,,,,,,/(,,,#@@@@@@&%((*,,,,,,,,*,,,,,,,,,,,,,*,,,,,,,**,,,,,,,,,****,,***************************///////\n\
************,,,***********,****,,,,,,,****,,,,,,,,,,,,,,,,,.,,,.,/&&(*,,,,,*,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,*****,,**,**********************************////\n\
****************,,***,*****,,,,,,***,,,,,,,,,,,,,,,,,,,,,..,,..,,*,,*,,,,*,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,******,***,,,**********************************/****///\n\
**,,***,**********,*,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,*,,,,,,*,**,****,*********************************/**/////////////\n\
*******************,,,,,,,****,,,,***,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,*,,,***,,,**,***********,***********************************///////////\n\
****************,,,,,,*****,,,****,,,,**,,,**,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,*,,,,,,**,,*,,,*,,,,,,,***,,,,*****,****************************/***////////////////\n\
****************,,,*****,,,,,,***,,,,,**,,*,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,*,,,,,,,,,,,,,,************,****************************///*///////////////////\n\
*************,******,,,,,,,,,,,,,,,,,,,,***,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,*,,*,***,***,**********************************///////////////////////");
  ENVOY_LOG(info, "{}", logo);
  ENVOY_LOG(info, "Benchmarking {}", this->host_);
  ENVOY_LOG(info, "target rps: {}, #connections: {}, duration: {} seconds.", rps_, connections_,
            duration_.count());

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

void Benchmarker::performRequest(Benchmarking::Http::CodecClientProd* client,
                                 std::function<void(std::chrono::nanoseconds)> cb) {
  ASSERT(client);
  ASSERT(!client->remoteClosed());
  auto start = std::chrono::steady_clock::now();
  // response self-destructs.
  Benchmarking::BufferingStreamDecoder* response =
      new Benchmarking::BufferingStreamDecoder([this, cb, start, client]() -> void {
        auto dur = std::chrono::steady_clock::now() - start;
        codec_clients_.push_back(client);
        cb(dur);
      });

  // client->cork();
  Http::StreamEncoder& encoder = client->newStream(*response);
  Http::HeaderMapImpl headers;
  headers.insertMethod().value(Http::Headers::get().MethodValues.Get);
  headers.insertPath().value(std::string(path_));
  headers.insertHost().value(std::string(host_));
  headers.insertScheme().value(Http::Headers::get().SchemeValues.Http);
  encoder.encodeHeaders(headers, true);
  // client->unCork();
}

} // namespace Benchmark
