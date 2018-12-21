licenses(["notice"])  # Apache 2

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_basic_cc_library(
    name = "client_includes",
    hdrs = [
        "driver.h",
        "client_options.h",
    ],
    include_prefix = "nighthawk/client",
)

