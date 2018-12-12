#!/usr/bin/env python3


import statistics

from hdrh.histogram import HdrHistogram

histogram = HdrHistogram(1, 60000000, 4)

def pp(histogram, p):
    v = histogram.get_value_at_percentile(p)
    print("p{}: {} us".format(p, v))


def main():
    with open("res.txt") as f:
        content = f.readlines()

    content = [int(x.strip()) for x in content]
    for n in content:
        histogram.record_value(n)

    print("Uncorrected hdr histogram percentiles")
    pp(histogram, 50)
    pp(histogram, 90)
    pp(histogram, 99)
    pp(histogram, 99.9)

    print("mean:",statistics.mean(content))
    print("median:",statistics.median(content))
    print("var:",statistics.pvariance(content))
    print("pstdev:",statistics.pstdev(content))


main()
