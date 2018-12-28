#include "exe/client.h"

#include "absl/debugging/symbolize.h"

// NOLINT(namespace-nighthawk)

int main(int argc, char** argv) {
#ifndef __APPLE__
  // absl::Symbolize mostly works without this, but this improves corner case
  // handling, such as running in a chroot jail.
  absl::InitializeSymbolizer(argv[0]);
#endif
  std::unique_ptr<Nighthawk::ClientMain> client;

  try {
    client = std::make_unique<Nighthawk::ClientMain>(argc, argv);
  } catch (const Nighthawk::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Nighthawk::MalformedArgvException& e) {
    return EXIT_FAILURE;
  } catch (const Nighthawk::NighthawkException& e) {
    return EXIT_FAILURE;
  }

  return client->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}