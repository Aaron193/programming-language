#!/usr/bin/env python3

import time


class Counter:
    __slots__ = ("value",)

    def __init__(self) -> None:
        self.value = 0

    def inc(self) -> int:
        self.value += 1
        return self.value


def make_multiplier(k: int):
    def multiply(x: int) -> int:
        return x * k

    return multiply


mul2 = make_multiplier(2)
mul3 = make_multiplier(3)

counter = Counter()
history = []
table = {}
keys = set()

start = time.perf_counter()

for i in range(25000):
    key = f"k{i}"
    value = mul2(i) + mul3(i) + counter.inc()
    table[key] = value
    keys.add(key)
    history.append(value)

sum_a = 0
for value in history:
    sum_a += value

sum_b = 0
key_list = list(keys)
for key in key_list:
    sum_b += table[key]

elapsed = time.perf_counter() - start

print(sum_a)
print(sum_b)
print(counter.value)
print(len(keys))
print(elapsed)
