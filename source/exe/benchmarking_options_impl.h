#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "nighthawk/common/exception.h"
#include "nighthawk/common/options.h"
//#include "envoy/server/options.h"
#include "common/stats/stats_options_impl.h"
#include "envoy/stats/stats_options.h"
//#include "server/options_impl.h"

#include "source/exe/nighthawk_options.pb.h"

#include "spdlog/spdlog.h"

namespace Nighthawk {

typedef std::unique_ptr<nighthawk::CommandLineOptions> NighthawkCommandLineOptionsPtr;

// We derive from envoy's option implementation, in an attempt
// to leverage envoy's option handling infra, which failed.
class OptionsImpl : public Nighthawk::Options {
public:
  OptionsImpl(int argc, const char* const* argv);

  virtual CommandLineOptionsPtr toCommandLineOptions() const override;

  uint64_t requests_per_second() const override { return requests_per_second_; }
  uint64_t connections() const override { return connections_; }
  std::chrono::seconds duration() const override { return std::chrono::seconds(duration_); }
  std::string uri() const override { return uri_; }

private:
  uint64_t requests_per_second_;
  uint64_t connections_;
  uint64_t duration_;
  std::string uri_;
};

class NoServingException : public NighthawkException {
public:
  NoServingException() : NighthawkException("NoServingException") {}
};

/**
 * Thrown when an OptionsImpl was not constructed because the argv was invalid.
 */
class MalformedArgvException : public NighthawkException {
public:
  MalformedArgvException(const std::string& what) : NighthawkException(what) {}
};

} // namespace Nighthawk
