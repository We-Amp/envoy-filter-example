#include "exe/sequencer.h"

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
  if ((now - start_) > duration_) {
    dispatcher_.exit();
  }

  while (rate_limiter_.tryAcquireOne()) {
    ENVOY_LOG(error, "call target");
    target_([this, now]() {
      auto dur = std::chrono::high_resolution_clock::now() - now;
      ENVOY_LOG(error, "cb latency: {}", dur.count());
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
