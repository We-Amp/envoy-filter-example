#pragma once

#include <stdexcept>
#include <string>

namespace Nighthawk {
/**
 * Base class for all nighthawk exceptions.
 */
class NighthawkException : public std::runtime_error {
public:
  NighthawkException(const std::string& message) : std::runtime_error(message) {}
};
} // namespace Nighthawk
