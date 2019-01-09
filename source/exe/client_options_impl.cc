#include "exe/client_options_impl.h"

#include "tclap/CmdLine.h"

namespace Nighthawk {

OptionsImpl::OptionsImpl(int argc, const char* const* argv) {
  TCLAP::CmdLine cmd("benchmarking", ' ', "PoC");
  TCLAP::ValueArg<uint64_t> requests_per_second("", "rps", "target requests per second", false,
                                                5 /*default qps*/, "uint64_t", cmd);
  TCLAP::ValueArg<uint64_t> connections("", "connections", "number of connections to use", false, 1,
                                        "uint64_t", cmd);
  TCLAP::ValueArg<uint64_t> duration("", "duration", "duration (seconds)", false, 5, "uint64_t",
                                     cmd);
  TCLAP::ValueArg<uint64_t> timeout("", "timeout", "timeout (seconds)", false, 5, "uint64_t", cmd);

  TCLAP::SwitchArg h2("", "h2", "Use h2", cmd);

  TCLAP::ValueArg<std::string> concurrency("", "concurrency", "concurrency (seconds)", false, "1",
                                           "int64_t", cmd);

  TCLAP::UnlabeledValueArg<std::string> uri("uri", "uri to benchmark", true, "", "uri format", cmd);

  cmd.setExceptionHandling(false);
  try {
    cmd.parse(argc, argv);
  } catch (TCLAP::ArgException& e) {
    try {
      cmd.getOutput()->failure(cmd, e);
    } catch (const TCLAP::ExitException&) {
      // failure() has already written an informative message to stderr, so all that's left to do
      // is throw our own exception with the original message.
      throw Nighthawk::MalformedArgvException(e.what());
    }
  } catch (const TCLAP::ExitException& e) {
    // parse() throws an ExitException with status 0 after printing the output for --help and
    // --version.
    throw Nighthawk::NoServingException();
  }

  requests_per_second_ = requests_per_second.getValue();
  connections_ = connections.getValue();
  duration_ = duration.getValue();
  timeout_ = timeout.getValue();
  uri_ = uri.getValue();
  h2_ = h2.getValue();
  concurrency_ = concurrency.getValue();
}

Nighthawk::ClientCommandLineOptionsPtr OptionsImpl::toClientCommandLineOptions() const {
  Nighthawk::ClientCommandLineOptionsPtr command_line_options =
      std::make_unique<nighthawk::ClientCommandLineOptions>();

  command_line_options->set_connections(connections());
  command_line_options->mutable_duration()->set_seconds(duration().count());
  command_line_options->set_requests_per_second(requests_per_second());
  command_line_options->mutable_duration()->set_seconds(timeout().count());
  command_line_options->set_h2(h2());
  command_line_options->set_uri(uri());
  command_line_options->set_concurrency(concurrency());

  return command_line_options;
}

} // namespace Nighthawk
