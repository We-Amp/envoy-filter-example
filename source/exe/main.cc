#include "exe/main_common.h"

#include "absl/debugging/symbolize.h"

// NOLINT(namespace-nighthawk)

/**
 * Basic Site-Specific main()
 *
 * This should be used to do setup tasks specific to a particular site's
 * deployment such as initializing signal handling. It calls main_common
 * after setting up command line options.
 */
int main(int argc, char** argv) {
#ifndef __APPLE__
  // absl::Symbolize mostly works without this, but this improves corner case
  // handling, such as running in a chroot jail.
  absl::InitializeSymbolizer(argv[0]);
#endif
  std::unique_ptr<Nighthawk::MainCommon> main_common;

  // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  try {
    main_common = std::make_unique<Nighthawk::MainCommon>(argc, argv);
  } catch (const Nighthawk::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Nighthawk::MalformedArgvException& e) {
    return EXIT_FAILURE;
  } catch (const Nighthawk::NighthawkException& e) {
    return EXIT_FAILURE;
  }
  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostis.
  return main_common->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
