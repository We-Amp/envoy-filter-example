# Envoy filter example

I needed this patch in the envoy submodule to get the build to work:

```diff
oschaaf@ubuntu-srv:~/code/envoy-filter-example/envoy$ git diff
diff --git a/.bazelrc b/.bazelrc
index 416f12293..254a17201 100644
--- a/.bazelrc
+++ b/.bazelrc
@@ -1 +1,2 @@
-import %workspace%/tools/bazel.rc
+#import %workspace%/tools/bazel.rc
+import /home/oschaaf/code/envoy-filter-example/envoy/tools/bazel.rc
\ No newline at end of file
```

# Building and running the benchmark

```
bazel build -c dbg //:benchmark_main
bazel-bin/benchmark_main
```


This project demonstrates the linking of additional filters with the Envoy binary.
A new filter `echo2` is introduced, identical modulo renaming to the existing
[`echo`](https://github.com/envoyproxy/envoy/blob/master/source/extensions/filters/network/echo/echo.h)
filter. Integration tests demonstrating the filter's end-to-end behavior are
also provided.

For an example of additional HTTP filters, see [here](http-filter-example).

## Building

To build the Envoy static binary:

1. `git submodule update --init`
2. `bazel build //:envoy`

## Testing

To run the `echo2` integration test:

`bazel test //:echo2_integration_test`

To run the regular Envoy tests from this project:

`bazel test @envoy//test/...`

## How it works

The [Envoy repository](https://github.com/envoyproxy/envoy/) is provided as a submodule.
The [`WORKSPACE`](WORKSPACE) file maps the `@envoy` repository to this local path.

The [`BUILD`](BUILD) file introduces a new Envoy static binary target, `envoy`,
that links together the new filter and `@envoy//source/exe:envoy_main_lib`. The
`echo2` filter registers itself during the static initialization phase of the
Envoy binary as a new filter.
