#! /usr/bin/env python3

# FIB -- A classic benchmark, computes fib(30) inefficiently.

def fib (n):
    if n < 2:
        return n
    else:
        return fib (n - 1) + fib (n - 2)

print ("fib ({0} = {1}".format (30, fib (30)))
