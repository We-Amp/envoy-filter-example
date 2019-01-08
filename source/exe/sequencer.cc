#include "exe/sequencer.h"

#include <chrono>

using namespace std::chrono_literals;

namespace Nighthawk {

Sequencer::Sequencer(Envoy::Event::Dispatcher& dispatcher, Envoy::TimeSource& time_source,
                     RateLimiter& rate_limiter, SequencerTarget& target,
                     std::chrono::seconds duration)
    : dispatcher_(dispatcher), time_source_(time_source),
      timer_(dispatcher_.createTimer([this]() { run(true); })), rate_limiter_(rate_limiter),
      target_(target), duration_(duration), start_(std::chrono::high_resolution_clock::now()),
      targets_initiated_(0), targets_completed_(0) {
  (void)time_source_;
}

void Sequencer::start() {
  start_ = std::chrono::high_resolution_clock::now();
  run(false);
  scheduleRun();
}

void Sequencer::scheduleRun() { timer_->enableTimer(std::chrono::milliseconds(1)); }

void Sequencer::run(bool from_timer) {
  auto now = std::chrono::high_resolution_clock::now();

  // We put a cap on duration here. Which means we do not care care if we initiate/complete more
  // or less requests then anticipated based on rps * duration (seconds).
  if ((now - start_) > duration_) {
    if (targets_completed_ == targets_initiated_) {
      ENVOY_LOG(info, "Sequencer done processing {} operations", targets_completed_);
      dispatcher_.exit();
    } else {
      // We wait untill all due responses are in.
      if (((now - start_) - duration_) > 5s) {
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
        auto dur = std::chrono::high_resolution_clock::now() - now;
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
