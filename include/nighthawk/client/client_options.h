#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "source/exe/client_options.pb.h"

namespace Nighthawk {

typedef std::unique_ptr<nighthawk::ClientCommandLineOptions> ClientCommandLineOptionsPtr;

class Options {
public:
  virtual ~Options() {}

  virtual uint64_t requests_per_second() const PURE;
  virtual uint64_t connections() const PURE;
  virtual std::chrono::seconds duration() const PURE;
  virtual std::chrono::seconds timeout() const PURE;
  virtual std::string uri() const PURE;

  virtual ClientCommandLineOptionsPtr toClientCommandLineOptions() const PURE;
};

} // namespace Nighthawk
