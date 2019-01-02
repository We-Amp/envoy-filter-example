#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "nighthawk/client/client_options.h"
#include "nighthawk/common/exception.h"

#include "source/exe/client_options.pb.h"

namespace Nighthawk {

typedef std::unique_ptr<nighthawk::ClientCommandLineOptions> NighthawkClientCommandLineOptionsPtr;

class OptionsImpl : public Nighthawk::Options {
public:
  OptionsImpl(int argc, const char* const* argv);

  virtual ClientCommandLineOptionsPtr toClientCommandLineOptions() const override;

  uint64_t requests_per_second() const override { return requests_per_second_; }
  uint64_t connections() const override { return connections_; }
  std::chrono::seconds duration() const override { return std::chrono::seconds(duration_); }
  std::chrono::seconds timeout() const override { return std::chrono::seconds(timeout_); }
  std::string uri() const override { return uri_; }

private:
  uint64_t requests_per_second_;
  uint64_t connections_;
  uint64_t duration_;
  uint64_t timeout_;
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
