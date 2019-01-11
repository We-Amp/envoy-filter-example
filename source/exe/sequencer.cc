#include "nighthawk/common/exception.h"

#include "exe/sequencer.h"

using namespace std::chrono_literals;

namespace Nighthawk {

Sequencer::Sequencer(Envoy::Event::Dispatcher& dispatcher, Envoy::TimeSource& time_source,
                     RateLimiter& rate_limiter, SequencerTarget& target,
                     std::chrono::microseconds duration, std::chrono::microseconds grace_timeout)
    : dispatcher_(dispatcher), time_source_(time_source),
      timer_(dispatcher_.createTimer([this]() { run(true); })), rate_limiter_(rate_limiter),
      target_(target), duration_(duration), grace_timeout_(grace_timeout),
      start_(time_source.monotonicTime().min()), targets_initiated_(0), targets_completed_(0) {
  if (target_ == nullptr) {
    throw NighthawkException("Sequencer must be constructed with a SequencerTarget.");
  }
}

void Sequencer::start() {
  start_ = time_source_.monotonicTime();
  run(false);
  scheduleRun();
}

void Sequencer::scheduleRun() { timer_->enableTimer(1ms); }

void Sequencer::run(bool from_timer) {
  auto now = time_source_.monotonicTime();
  // We put a cap on duration here. Which means we do not care care if we initiate/complete more
  // or less requests then anticipated based on rps * duration (seconds).
  if ((now - start_) > duration_) {
    if (targets_completed_ == targets_initiated_) {
      ENVOY_LOG(info, "Sequencer done processing {} operations in {} ms.", targets_completed_,
                std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count());
      dispatcher_.exit();
    } else {
      // We wait untill all due responses are in.
      if (((now - start_) - duration_) > grace_timeout_) {
        ENVOY_LOG(warn,
                  "Sequencer timeout waiting for due responses. Initiated: {} / Completed: {}",
                  targets_initiated_, targets_completed_);
        dispatcher_.exit();
        return;
      }
      scheduleRun();
    }
    return;
  }

  while (rate_limiter_.tryAcquireOne()) {
    target_([this, now]() {
      if (latency_callback_ != nullptr) {
        auto dur = time_source_.monotonicTime() - now;
        latency_callback_(dur);
      }
      targets_completed_++;
    });
    targets_initiated_++;
  }

  if (from_timer) {
    scheduleRun();
  }
}

void Sequencer::waitForCompletion() { dispatcher_.run(Envoy::Event::Dispatcher::RunType::Block); }

} // namespace Nighthawk
