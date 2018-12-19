package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
)

envoy_cc_binary(
    name = "benchmark_main",
    repository = "@envoy",
    stamped = True,
    deps = ["//source/exe:benchmark_main_entry_lib",
    ],
)
