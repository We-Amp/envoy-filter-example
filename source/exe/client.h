#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "common/common/thread.h"
#include "common/event/real_time_system.h"
#include "envoy/event/dispatcher.h"
#include "exe/client_options_impl.h"
#include "exe/codec_client.h"

using namespace Envoy;

namespace Nighthawk {

class ClientMain {
public:
  ClientMain(int argc, const char* const* argv);
  ClientMain(OptionsImpl options);
  ~ClientMain();

  bool run();

protected:
  Nighthawk::OptionsImpl options_;
  Event::RealTimeSystem real_time_system_;
  std::unique_ptr<Logger::Context> logging_context_;

private:
  void configureComponentLogLevels();
};

class BenchmarkLoop {
public:
  BenchmarkLoop(Envoy::Event::Dispatcher& dispatcher)
      : dispatcher_(&dispatcher), rps_(0), current_rps_(0), duration_(std::chrono::seconds(5)),
        requests_(0), max_requests_(0), callback_count_(0) {
    timer_ = dispatcher_->createTimer([this]() { run(true); });
  }
  virtual ~BenchmarkLoop() {}
  void start();

protected:
  // Our benchmark measures latency between initiating what we want to measure,
  // and when the completion callback is called. Because it's not always possible
  // to keep up with the pace because of running out of resources like available
  // connections it is possible that an implementation will not be able to start.
  // TODO(oschaaf): we will need one more level of indirection, this needs to
  // implement a final method and call a virtual one.
  virtual bool tryStartOne(std::function<void()> completion_callback) PURE;
  // Subclasses can implement this. The benchmark can use a spin loop
  // to improve accuracy in certain cases, when no inbound events are
  // expected.
  // TODO(oschaaf): consider renaming this to something in the benchmark loop context,
  // e.g. to BenchmarkLoop::allowSpinning().
  virtual bool expectInboundEvents() { return true; }

private:
  void scheduleRun();
  void run(bool from_timer);

  Envoy::Event::Dispatcher* dispatcher_;
  Event::TimerPtr timer_;
  // TODO(oschaaf): use TimeSource abstraction.
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;
  unsigned int rps_;
  unsigned int current_rps_;
  std::chrono::seconds duration_;
  unsigned int requests_;
  unsigned int max_requests_;
  unsigned int callback_count_;
};

class HttpBenchmarkTimingLoop : public BenchmarkLoop {
public:
  HttpBenchmarkTimingLoop(Envoy::Event::Dispatcher& dispatcher) : BenchmarkLoop(dispatcher) {}
  virtual bool tryStartOne(std::function<void()> completion_callback) override;

private:
  //  Http::HttpCodecClientPool pool_;
};

} // namespace Nighthawk
