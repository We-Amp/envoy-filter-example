#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "source/exe/nighthawk_options.pb.h"

namespace Nighthawk {

typedef std::unique_ptr<nighthawk::CommandLineOptions> CommandLineOptionsPtr;

class Options {
public:
  virtual ~Options() {}

  virtual uint64_t requests_per_second() const PURE;
  virtual uint64_t connections() const PURE;
  virtual std::chrono::seconds duration() const PURE;
  virtual std::string uri() const PURE;

  virtual CommandLineOptionsPtr toCommandLineOptions() const PURE;
};

} // namespace Nighthawk
