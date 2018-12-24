#include "exe/client.h"

#include <chrono>
#include <iostream>
#include <memory>

#include "ares.h"

#include "absl/strings/str_split.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/http/headers.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "exe/benchmarker.h"

using namespace Envoy;

namespace Nighthawk {

void BenchmarkLoop::start() {
  start_ = std::chrono::high_resolution_clock::now();
  run(false);
  scheduleRun();
}

void BenchmarkLoop::scheduleRun() { timer_->enableTimer(std::chrono::milliseconds(1)); }

void BenchmarkLoop::run(bool from_timer) {
  auto now = std::chrono::high_resolution_clock::now();
  auto dur = now - start_;
  double ms_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count() / 1000000.0;
  current_rps_ = requests_ / (ms_dur / 1000.0);
  int due_requests = ((rps_ - current_rps_)) * (ms_dur / 1000.0);

  if (dur >= duration_) {
    dispatcher_->exit();
    return;
  }
  if (due_requests == 0 && !expectInboundEvents()) {
    nanosleep((const struct timespec[]){{0, 500L}}, NULL);
    timer_->enableTimer(std::chrono::milliseconds(0));
    return;
  }

  while (requests_ < max_requests_ && due_requests-- > 0) {
    if (!tryStartOne()) {
      scheduleRun();
      return;
    }

    ++requests_;
    /*
        performRequest(client, [this](std::chrono::nanoseconds nanoseconds) {
          ASSERT(nanoseconds.count() > 0);
          results_.push_back(nanoseconds.count());
          if (++callback_count_ == this->max_requests_) {
            dispatcher_->exit();
            return;
          }
          timer_->enableTimer(std::chrono::milliseconds(0));
        });*/
  }

  if (from_timer) {
    scheduleRun();
  }
}

ClientMain::ClientMain(int argc, const char* const* argv) : ClientMain(OptionsImpl(argc, argv)) {}

ClientMain::ClientMain(OptionsImpl options) : options_(options) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  configureComponentLogLevels();
}

ClientMain::~ClientMain() { ares_library_cleanup(); }

void ClientMain::configureComponentLogLevels() {
  // We rely on Envoy's logging infra.
  // TODO(oschaaf): Add options to tweak the log level of the various log tags
  // that are available.
  Logger::Registry::setLogLevel(spdlog::level::warn);
  Logger::Logger* logger_to_change = Logger::Registry::logger("main");
  logger_to_change->setLevel(spdlog::level::info);
}

bool ClientMain::run() {
  Stats::IsolatedStoreImpl stats;
  // TODO(oschaaf): platform specificity need addressing.
  Thread::ThreadFactoryImplPosix thread_factory;
  auto api = new Envoy::Api::Impl(std::chrono::milliseconds(100) /*flush interval*/, thread_factory,
                                  stats);
  auto dispatcher = api->allocateDispatcher(real_time_system_);
  HttpBenchmarkTimingLoop bml(*dispatcher);
  bml.start();

  Benchmarker benchmarker(*dispatcher, options_.connections(), options_.requests_per_second(),
                          options_.duration(), Headers::get().MethodValues.Get, options_.uri());
  auto dns_resolver = dispatcher->createDnsResolver({});
  benchmarker.run(dns_resolver);
  return true;
}

} // namespace Nighthawk
