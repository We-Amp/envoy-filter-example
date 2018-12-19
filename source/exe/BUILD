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

load("@envoy//api/bazel:api_build_system.bzl", "api_proto_library_internal")
load("@envoy//bazel:repositories.bzl", "PPC_SKIP_TARGETS")
envoy_package()

api_proto_library_internal(
    name = "benchmark_options",
    srcs = ["client_options.proto"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "benchmark_main_entry_lib",
    srcs = ["client_main_entry.cc"],
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
        "client.cc",
        "codec_client.cc",
        "benchmarker.cc",
	    "client_options_impl.cc"
    ],
    hdrs = [
        "client.h",
        "codec_client.h",
        "benchmarker.h",
    	"client_options_impl.h"
    ],
    repository = "@envoy",
    deps = [
        ":benchmark_options_cc",
        "//include/nighthawk/common:base_includes",
        "@envoy//source/exe:envoy_common_lib",
        "@envoy//source/common/api:os_sys_calls_lib",
        "@envoy//source/common/common:compiler_requirements_lib",
        "@envoy//source/common/common:perf_annotation_lib",
    ]
    + select({
        "@envoy//bazel:disable_signal_trace": [],
        "//conditions:default": [
            "@envoy//source/exe:sigaction_lib",
            "@envoy//source/exe:terminate_handler_lib",
        ],
    }),
)
