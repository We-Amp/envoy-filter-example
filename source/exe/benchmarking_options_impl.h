#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
//#include "envoy/server/options.h"
#include "server/options_impl.h"
#include "envoy/stats/stats_options.h"
#include "common/stats/stats_options_impl.h"

#include "source/exe/benchmark_options.pb.h"

#include "spdlog/spdlog.h"

namespace Benchmarking {

typedef std::unique_ptr<benchmarking::CommandLineOptions> BenchmarkingCommandLineOptionsPtr;

class OptionsImpl : public Envoy::OptionsImpl {
public:
  OptionsImpl(int argc, const char* const* argv, const Envoy::OptionsImpl::HotRestartVersionCb& hot_restart_version_cb,
              spdlog::level::level_enum default_log_level);

  // Test constructor; creates "reasonable" defaults, but desired values should be set explicitly.
  OptionsImpl(const std::string& service_cluster, const std::string& service_node,
              const std::string& service_zone, spdlog::level::level_enum log_level);

  virtual BenchmarkingCommandLineOptionsPtr toBenchmarkingCommandLineOptions() const;

  uint64_t requests_per_second() { return requests_per_second_; }

private:
  uint64_t requests_per_second_;
};

} // namespace Benchmarking
