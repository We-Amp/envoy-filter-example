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

class RateLimiter : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  RateLimiter(uint64_t max_slots) : max_slots_(max_slots), slots_(max_slots) {}
  virtual ~RateLimiter() {}

  virtual bool tryAcquireOne() {
    recoverSlots();
    if (slots_ > 0) {
      slots_--;
      return true;
    }
    return false;
  }

  virtual void recoverSlots() PURE;

protected:
  uint64_t max_slots_;
  uint64_t slots_;
};

class LinearRateLimiter : public RateLimiter {
public:
  LinearRateLimiter(uint64_t max_slots, std::chrono::microseconds slot_recovery_time)
      : RateLimiter(max_slots), slot_recovery_time_(slot_recovery_time),
        last_checked_at_(std::chrono::high_resolution_clock::now()), overflow_count_(0) {}

  virtual void recoverSlots() override {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = now - last_checked_at_;
    auto to_add = elapsed / slot_recovery_time_;

    while (to_add-- > 0 && slots_ < max_slots_) {
      slots_++;
    }

    if (to_add) {
      ENVOY_LOG(warn, "Overflow detected in Linear Rate Limiter. Looks like the client is not able "
                      "to keep up.");
    }
    overflow_count_ += to_add;
    last_checked_at_ = now;
  }

private:
  std::chrono::microseconds slot_recovery_time_;
  std::chrono::time_point<std::chrono::high_resolution_clock> last_checked_at_;
  uint64_t overflow_count_;
};

class Sequencer : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  Sequencer(Envoy::Event::Dispatcher& dispatcher, Envoy::TimeSource& time_source);
  virtual ~Sequencer() {}
  bool start();
  void waitForCompletion();

protected:
  virtual void run() PURE;

private:
  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::TimeSource& time_source_;
};

class LinearBenchmarkSequencer : public Sequencer {
public:
  LinearBenchmarkSequencer(Envoy::Event::Dispatcher& dispatcher, Envoy::TimeSource& time_source)
      : Sequencer(dispatcher, time_source) {}
  virtual ~LinearBenchmarkSequencer() {}

protected:
  virtual void run() override;
};

class BenchmarkLoop : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  BenchmarkLoop(Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
                Envoy::TimeSource& time_source, Thread::ThreadFactory& thread_factory, uint64_t rps,
                std::chrono::seconds duration, std::string uri);
  virtual ~BenchmarkLoop();
  bool start();
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
  virtual void initialize();

protected:
  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::Stats::Store& store_;
  Event::TimerPtr timer_;
  Envoy::TimeSource& time_source_;
  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  Thread::ThreadFactory& thread_factory_;
  std::unique_ptr<Envoy::Runtime::LoaderImpl> runtime_;
  Runtime::RandomGeneratorImpl generator_;
  // TODO(oschaaf): generalize usage of these and move out to derived class(es)
  uint64_t pool_connect_failures_;
  uint64_t pool_overflow_failures_;

  // parsed and interpreted uri components
  bool is_https_;
  std::string host_;
  uint32_t port_;
  std::string path_;
  bool dns_failure_;
  Network::Address::InstanceConstSharedPtr target_address_;

private:
  void scheduleRun();
  void run(bool from_timer);

  // TODO(oschaaf): use TimeSource abstraction.
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;
  uint64_t rps_;
  uint64_t current_rps_;
  std::chrono::seconds duration_;
  uint64_t requests_;
  uint64_t max_requests_;
  uint64_t callback_count_;
  std::string uri_;
  std::vector<int> results_;
};

class HttpBenchmarkTimingLoop : public BenchmarkLoop,
                                public Envoy::Http::ConnectionPool::Callbacks {
public:
  HttpBenchmarkTimingLoop(Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
                          Envoy::TimeSource& time_source, Thread::ThreadFactory& thread_factory,
                          uint64_t rps, std::chrono::seconds duration, uint64_t max_connections,
                          std::chrono::seconds timeout, std::string uri, bool h2);
  virtual bool tryStartOne(std::function<void()> completion_callback) override;

  // ConnectionPool::Callbacks
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::StreamEncoder& encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

private:
  virtual void initialize() override;

  std::chrono::seconds timeout_;
  uint64_t max_connections_;
  bool h2_;
  Envoy::Http::ConnectionPool::InstancePtr pool_;
};

} // namespace Nighthawk
