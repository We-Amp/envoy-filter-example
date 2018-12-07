licenses(["notice"])  # Apache 2

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "@envoy//source/extensions:all_extensions.bzl",
    "envoy_all_extensions",
    "envoy_windows_extensions",
)
load("@envoy//bazel:repositories.bzl", "PPC_SKIP_TARGETS")
envoy_package()

envoy_cc_library(
    name = "benchmark_main_entry_lib",
    srcs = ["main.cc"],
    hdrs = [],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    external_deps = [
        "abseil_symbolize",
    ],
    deps = [
        ":benchmark_main_common_lib",
    ],
)

envoy_cc_library(
    name = "benchmark_main_common_lib",
    srcs = [
        "main_common.cc",
        "codec_client.cc",
        "service.cc",
        "benchmarker.cc"
    ],
    hdrs = [
        "main_common.h",
        "codec_client.h",
        "service.h",
        "benchmarker.h"
    ],
    repository = "@envoy",
    deps = [
        "@envoy//source/exe:envoy_common_lib",
        "@envoy//source/common/api:os_sys_calls_lib",
        "@envoy//source/common/common:compiler_requirements_lib",
        "@envoy//source/common/common:perf_annotation_lib",
        "@envoy//source/server:hot_restart_lib",
        "@envoy//source/server:hot_restart_nop_lib",
        "@envoy//source/server:proto_descriptors_lib",
        "@envoy//source/server/config_validation:server_lib",
    ]
    + select({
        "@envoy//bazel:disable_signal_trace": [],
        "//conditions:default": [
            "@envoy//source/exe:sigaction_lib",
            "@envoy//source/exe:terminate_handler_lib",
        ],
    }),
)
