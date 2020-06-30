#! /usr/bin/env python

# ACK -- One of the Kernighan and Van Wyk benchmarks.

# XXX

# RecursionError: maximum recursion depth exceeded in comparison

c = 0

def ack (m, n):
    global c
    c = c + 1
    if m == 0:
        return n + 1
    elif n == 0:
        return ack (m - 1, 1)
    else:
        return ack (m - 1, ack (m, n - 1))

ack (3, 8)

print (c)
