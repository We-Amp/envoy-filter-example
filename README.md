# Nighthawk PoC

*An benchmarking tool based on Envoy*

## Current state

This project is in proof-of-concept mode. Currently it offers a closed
loop http benchmark. The benchmark runs asynchronously on an event loop,
utilizing a single thread.

## Building and running the benchmark

TODO(oschaaf): collect and list prerequisites.

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

## Development

At the moment the PoC incorporates a .vscode. This has preconfigured tasks
and launch settings to build the benchmarking client and tests, as well us
run tests. It also provides the right settings for intellisense and wiring up
the IDE for debugging. 
