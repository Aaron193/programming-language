#!/usr/bin/env python3

import time


def fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


start = time.perf_counter()
result = fib(35)
elapsed = time.perf_counter() - start

print(result)
print(elapsed)
