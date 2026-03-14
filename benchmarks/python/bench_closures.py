#!/usr/bin/env python3

import time


def make_adder(n: int):
    def add_one(x: int) -> int:
        return n + x

    return add_one


funcs = []

start = time.perf_counter()

for i in range(10000):
    funcs.append(make_adder(i))

total = 0
for func in funcs:
    total += func(1)

elapsed = time.perf_counter() - start

print(total)
print(elapsed)
