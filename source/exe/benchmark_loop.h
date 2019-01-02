#pragma once

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/real_time_system.h"
// TODO(oschaaf):
#include "common/runtime/runtime_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/store.h"
#include "envoy/upstream/upstream.h"

#include "exe/client_options_impl.h"
#include "exe/stream_decoder.h"

using namespace Envoy;

namespace Nighthawk {

class BenchmarkLoop : Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  BenchmarkLoop(Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
                Envoy::TimeSource& time_source, Thread::ThreadFactory& thread_factory, uint64_t rps,
                std::chrono::seconds duration)
      : store_(store), time_source_(time_source), thread_factory_(thread_factory),
        pool_connect_failures_(0), pool_overflow_failures_(0), dispatcher_(&dispatcher), rps_(rps),
        current_rps_(0), duration_(duration), requests_(0), max_requests_(rps_ * duration_.count()),
        callback_count_(0) {
    timer_ = dispatcher_->createTimer([this]() { run(true); });
  }
  virtual ~BenchmarkLoop() {
    // TODO(oschaaf): check
    tls_->shutdownGlobalThreading();
  }
  void start();
  void waitForCompletion();

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

protected:
  Envoy::Stats::Store& store_;
  Envoy::TimeSource& time_source_;
  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  Thread::ThreadFactory& thread_factory_;
  std::unique_ptr<Envoy::Runtime::LoaderImpl> runtime_;
  Runtime::RandomGeneratorImpl generator_;
  uint64_t pool_connect_failures_;
  uint64_t pool_overflow_failures_;

private:
  void scheduleRun();
  void run(bool from_timer);

  Envoy::Event::Dispatcher* dispatcher_;
  Event::TimerPtr timer_;
  // TODO(oschaaf): use TimeSource abstraction.
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;
  uint64_t rps_;
  uint64_t current_rps_;
  std::chrono::seconds duration_;
  uint64_t requests_;
  uint64_t max_requests_;
  uint64_t callback_count_;
};

class HttpBenchmarkTimingLoop : public BenchmarkLoop,
                                public Envoy::Http::ConnectionPool::Callbacks {
public:
  HttpBenchmarkTimingLoop(Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
                          Envoy::TimeSource& time_source, Thread::ThreadFactory& thread_factory,
                          uint64_t rps, std::chrono::seconds duration);
  virtual bool tryStartOne(std::function<void()> completion_callback) override;

  // ConnectionPool::Callbacks
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::StreamEncoder& encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Envoy::Http::ConnectionPool::InstancePtr pool_;
};

} // namespace Nighthawk
