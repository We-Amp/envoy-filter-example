licenses(["notice"])  # Apache 2

load(
    "//bazel:nighthawk.bzl",
    "nighthawk_cc_platform_dep"
)

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_platform_dep",
    "envoy_cc_posix_library",
    "envoy_cc_win32_library",
    "envoy_package",
)

load("@envoy//api/bazel:api_build_system.bzl", "api_proto_library_internal")
load("@envoy//bazel:repositories.bzl", "PPC_SKIP_TARGETS")

api_proto_library_internal(
    name = "benchmark_options",
    srcs = ["client_options.proto"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "nighthawk_client_common_lib",
    srcs = [
        "client.cc",
        "stream_decoder.cc",
	      "client_options_impl.cc",
        "benchmark_client.cc",
        "rate_limiter.cc",
        "sequencer.cc",
        "streaming_stats.cc",
    ],
    hdrs = [
        "client.h",
        "stream_decoder.h",
    	"client_options_impl.h",
        "ssl.h",
        "benchmark_client.h",
        "rate_limiter.h",
        "sequencer.h",
        "streaming_stats.h",
    ],
    visibility = ["//visibility:public"],
    repository = "@envoy",
    deps = [
        ":benchmark_options_cc",
        "//include/nighthawk/common:base_includes",
        "//include/nighthawk/client:client_includes",
        "@envoy//source/exe:envoy_common_lib",
        "@envoy//source/common/common:compiler_requirements_lib",
        "@envoy//source/common/stats:stats_lib",
        "@envoy//source/server/config_validation:server_lib",
        "@envoy//source/common/common:thread_impl_lib_posix",
    ]
)
