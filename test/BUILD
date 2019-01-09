package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_test",
)

envoy_cc_test(
    name = "nighthawk_test",
    srcs = ["client_options_test.cc",
        "benchmark_client_test.cc",
        "rate_limiter_test.cc",
        "sequencer_test.cc",
    ],
    repository = "@envoy",
    deps = [
        "@envoy//test/integration:integration_lib",
        "//source/exe:nighthawk_client_common_lib"
    ],
)

