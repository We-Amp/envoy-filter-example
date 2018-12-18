#include "exe/benchmarking_options_impl.h"

#include "tclap/CmdLine.h"

namespace Nighthawk {

// TODO(oschaaf): We hide the real argc from the base class.
OptionsImpl::OptionsImpl(int argc, const char* const* argv,
                         const Envoy::OptionsImpl::HotRestartVersionCb& hot_restart_version_cb,
                         spdlog::level::level_enum default_log_level)
    : Envoy::OptionsImpl(1, argv, hot_restart_version_cb, default_log_level) {
  (void)argc;

  TCLAP::CmdLine cmd("benchmarking", ' ', "PoC");
  // we need to rebuild the command line parsing ourselves here.
  TCLAP::ValueArg<uint64_t> requests_per_second("", "rps", "target requests per second", false,
                                                500 /*default qps*/, "uint64_t", cmd);
  TCLAP::ValueArg<uint64_t> connections("", "connections", "number of connections to use", false, 1,
                                        "uint64_t", cmd);
  TCLAP::ValueArg<uint64_t> duration("", "duration", "duration (seconds)", false, 5, "uint64_t",
                                     cmd);
  TCLAP::UnlabeledValueArg<std::string> uri("uri", "uri to benchmark", true, "", "uri format", cmd);

  cmd.setExceptionHandling(false);
  try {
    cmd.parse(argc, argv);
    // TODO(oschaaf): can't access count_
    cmd.getArgList().size();
  } catch (TCLAP::ArgException& e) {
    try {
      cmd.getOutput()->failure(cmd, e);
    } catch (const TCLAP::ExitException&) {
      // failure() has already written an informative message to stderr, so all that's left to do
      // is throw our own exception with the original message.
      throw Envoy::MalformedArgvException(e.what());
    }
  } catch (const TCLAP::ExitException& e) {
    // parse() throws an ExitException with status 0 after printing the output for --help and
    // --version.
    throw Envoy::NoServingException();
  }

  requests_per_second_ = requests_per_second.getValue();
  connections_ = connections.getValue();
  duration_ = duration.getValue();
  uri_ = uri.getValue();
}
OptionsImpl::OptionsImpl(const std::string& service_cluster, const std::string& service_node,
                         const std::string& service_zone, spdlog::level::level_enum log_level)
    : Envoy::OptionsImpl(service_cluster, service_node, service_zone, log_level) {}

NighthawkCommandLineOptionsPtr OptionsImpl::toBenchmarkingCommandLineOptions() const {
  auto options = std::make_unique<nighthawk::CommandLineOptions>();
  options->set_requests_per_second(requests_per_second_);
  options->set_connections(connections_);
  options->set_duration(duration_);
  options->set_uri(uri_);
  return options;
}

} // namespace Nighthawk
