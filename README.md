# Envoy-based benchmarking tool

## Building and running the benchmark

```bash
# build it
bazel build opt //:benchmark_main
# run the benchmark
bazel-bin/benchmark_main --rps 50 --duration 3 --connections 1 http://localhost:10000
# show latency percentiles
 ./stats.py
```
