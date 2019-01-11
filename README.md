# Nighthawk PoC

*A benchmarking tool based on Envoy*

## Current state

This project is in proof-of-concept mode. Supports HTTP/1.1 and HTTP/2 over http
and https (NOTE: no certs are validated), and is able to leverage multiple vCPUs
by running more event loops.

## Building and running the benchmark

TODO(oschaaf): collect and list prerequisites.

```bash
# build it
bazel build //:nighthawk_client
# test it
bazel test //test:nighthawk_test
# run a benchmark

bazel-bin/nighthawk_client --rps 500 http://127.0.0.1/
[2019-01-11 14:59:43.809][016342][info][main] [source/exe/client.cc:106] Starting 1 threads / event loops.
[2019-01-11 14:59:43.809][016342][info][main] [source/exe/client.cc:108] Global targets: 1 connections and 500 calls per second.
[2019-01-11 14:59:48.813][016343][info][main] [source/exe/sequencer.cc:36] Sequencer done processing 2499 operations in 5002 ms.
[2019-01-11 14:59:48.813][016343][info][main] [source/exe/client.cc:168] Connection: connect failures: 0, overflow failures: 0 . Protocol: good 2499 / bad 0 / reset 0
[2019-01-11 14:59:48.814][016342][info][main] [source/exe/client.cc:200] worker 0: avg latency 200.33us over 2498 callbacks. Min/Max: 130/549.
# show latency percentiles
./stats.py res.txt benchmark
Uncorrected hdr histogram percentiles (us)
p50: 183
p75: 260
p90: 263
p99: 320
p99.9: 539
p99.99: 549
p99.999: 549
p100: 549
min: 130
max: 549
mean: 199.83586869495596
median: 219.5
var: 4773.378985654496
pstdev: 69.08964456164539
```

## Development

At the moment the PoC incorporates a .vscode. This has preconfigured tasks
and launch settings to build the benchmarking client and tests, as well us
run tests. It also provides the right settings for intellisense and wiring up
the IDE for debugging. 
