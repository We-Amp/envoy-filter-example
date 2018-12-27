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
#envoy_package()

api_proto_library_internal(
    name = "benchmark_options",
    srcs = ["client_options.proto"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "nighthawk_client_entry_lib",
    srcs = ["client_main_entry.cc"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    external_deps = [
        "abseil_symbolize",
    ],
    deps = [
        ":nighthawk_client_common_lib",
        "@envoy//source/common/common:thread_lib",
    ] 
    #+ envoy_cc_platform_dep("platform_impl_lib"),
)

envoy_cc_library(
    name = "nighthawk_client_common_lib",
    srcs = [
        "client.cc",
        "codec_client.cc",
        "benchmarker.cc",
	    "client_options_impl.cc",
        "conn_pool.cc"
    ],
    hdrs = [
        "client.h",
        "codec_client.h",
        "benchmarker.h",
    	"client_options_impl.h",
        "conn_pool.h"
    ],
    repository = "@envoy",
    deps = [
        ":benchmark_options_cc",
        "//include/nighthawk/common:base_includes",
        "//include/nighthawk/client:client_includes",
        "@envoy//source/exe:envoy_main_common_lib",
        "@envoy//source/exe:envoy_common_lib",
        "@envoy//source/common/common:compiler_requirements_lib",
        "@envoy//source/common/stats:stats_lib",
        "@envoy//source/server/config_validation:server_lib",
        "@envoy//source/common/common:thread_impl_lib_posix",
    ]
)

#envoy_cc_library(
#    name = "platform_impl_lib_posix",
#    hdrs = ["posix/platform_impl.h"],
#    strip_include_prefix = "posix",
#    deps = ["@envoy//source/common/common:thread_lib"],
#    repository = "@envoy",
#)

