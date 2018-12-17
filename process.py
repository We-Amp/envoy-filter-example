#!/usr/bin/python3
from enum import Enum
import pprint
from os import listdir
from os.path import isfile, join
import jsonpickle
import statistics
from hdrh.histogram import HdrHistogram
import json

mypath = "results/"
onlyfiles = [f for f in listdir(mypath) if isfile(join(mypath, f))]


class Source(Enum):
    Benchmark = 1
    Wrk2 = 2


class Stats:
    def __init__(self, command):
        self.command = command
        self.stats = dict()

    def add_stat(self, key, value):
        if not key in self.stats:
            self.stats[key] = []
        self.stats[key].append(value)

    def source(self):
        if "benchmark_main" in self.command:
            return Source.Benchmark
        return Source.Wrk2

    def add_benchmark_targets(self, lines):
        env_ctrl = False
        for s in lines:
            s = s.strip()
            if s.startswith("*** FROM ACCESS LOG"):
                env_ctrl = True
            t = s.split(":")
            if len(t) != 2:
                continue
            if env_ctrl == True:
                self.add_stat("env_%s" % t[0], float(t[1]))
            else:
                self.add_stat("proto_%s" % t[0], float(t[1]))

    def add_wrk2_targets(self, lines):
        for s in lines:
            s = s.strip()
            a = s.split(" ")
            sv = a[len(a)-1]
            v = -1
            if sv.endswith("us"):
                v = float(sv[:-2])
                self.add_stat("wrk2_%s" % a[0].rstrip(
                    "%").rstrip("0").rstrip("."), v)
            elif sv.endswith("ms"):
                v = float(sv[:-2]) * 1000
                self.add_stat("wrk2_%s" % a[0].rstrip(
                    "%").rstrip("0").rstrip("."), v)

        env_ctrl = False
        for s in lines:
            s = s.strip()
            if s.startswith("*** FROM ACCESS LOG"):
                env_ctrl = True
            t = s.split(":")
            if len(t) != 2:
                continue
            if env_ctrl == True:
                self.add_stat("env_%s" % t[0], float(t[1]))


agg = dict()
for name in onlyfiles:
    path = join(mypath, name)
    with open(path, "r") as f:
        # print(path)
        s = f.readlines()
        commandline = s[0].strip()
        if commandline not in agg:
            agg[commandline] = Stats(commandline)
        stats = agg[commandline]

        if stats.source() == Source.Benchmark:
            stats.add_benchmark_targets(s)
        else:
            stats.add_wrk2_targets(s)

        # print(s)
        # break
print("\n")
print("\n")

print(jsonpickle.encode(agg))
