#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/common/pure.h"

namespace Nighthawk {

class Driver {
public:
  virtual ~Driver() {}

private:
  // TODO(oschaaf): design
  /*
  We need:
  - One or more functions configured that we want to time.
    These functions could provide callbacks through an interface,
    and promise to call us back when it is time to compute the latency.
  - input to provide the desired timing profile. We call that function with
    context that has  the number of outstanding calls, elapsed time etc, and
    maybe a context provided by the caller for tracking state like connection-
    pooling.
    The object provides a method which returns true/false,where true means
    that it is time to initate a new connections.


  auto h = HttpClient();
  auto l  = WrapLatencyMeasurements();

  Sequencer.setInterval(h.beginRequest);



  */
  // std::function<void()> method_;
  // TimingProfile timing_profile_;
};

} // namespace Nighthawk