# Envoy-based benchmarking tool

## Building and running the benchmark

```bash
# build it
bazel build //:nighthawk_client
# test it
bazel test //test:nighthawk_test
# run a benchmark
bazel-bin/nighthawk_client --rps 50 --duration 3 --connections 1 http://localhost:10000
# show latency percentiles
 ./stats.py
```
