#/bin/bash
#set -x
set +e

killall -s SIGKILL envoy

#~/bin/quiscense.sh

set -e

function runtest() {
  for i in `seq 1 $3`;
  do
    rm /tmp/envoy-http.log || true

    $(taskset -c 10-13 envoy -l error --file-flush-interval-msec 500 --config-path envoy.yaml > /dev/null)&
    sleep .5
    id="$(date +%s)"
    echo "run test $2 test (id:$id)"
    echo $1
    set +x
    res=$(eval "$1" 2>&1)
    filename="/home/oschaaf/code/envoy-filter-example/results/$2-$id"
    echo "write to $filename"
    echo "$1" > $filename
    if [ $2 = "benchmark" ]; then
      ./stats.py res.txt benchmark >> $filename
    else
      echo "$res" >> tmp
      sed -n -e '/Uncorrected/,$p' tmp >> $filename
      rm tmp
    fi
    echo "*** FROM ACCESS LOG" >> $filename
    ./stats.py /tmp/envoy-http.log envoy >> $filename
    rm /tmp/envoy-http.log
    echo "test runs for $2 done and written to $filename"
    sleep 0.6
    killall envoy
    sleep .5
  done
}

url="http://127.0.0.1:10000/"
duration="5"
qps="50"
cores="9"
concurrency="1"
repeat=50

# single cpu core, single connection & concurrency, increasing QPS
qps=50
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=250
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=500
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=2000
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=6000
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=12000
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat

concurrency="100"
# single cpu core, single connection & concurrency, increasing QPS
qps=50
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=250
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=500
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=2000
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=6000
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat
qps=12000
runtest "taskset -c $cores wrk2 -t 1 -c $concurrency -d $duration --rate $qps -U $url" "wrk" $repeat
runtest "taskset -c $cores bazel-bin/nighthawk_client --rps $qps --duration $duration --connections $concurrency $url" "benchmark" $repeat


echo "all tests done"
