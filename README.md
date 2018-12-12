# Envoy-based benchmarking tool

# Building and running the benchmark

```
# build it
bazel build -c opt //:benchmark_main
# run the benchmark
taskset -c 0 bazel-bin/benchmark_main --rps 50 --duration 3 --connections 1 http://localhost:10000
# show latency percentiles
 ./stats.py
```

# Updateing the envoy submodule workspace
I needed this patch to get the Envoy libs which will be consumed
to build for the Envoy submodule.

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
