#! /usr/bin/env python

# FIB -- A classic benchmark, computes fib(30) inefficiently.

def fib (n):
    if n < 2:
        return n
    else:
        return fib (n - 1) + fib (n - 2)

fib (30)
