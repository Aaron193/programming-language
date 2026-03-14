#!/usr/bin/env python3

import time


d = {}

start = time.perf_counter()

for i in range(30000):
    key = f"key{i}"
    d[key] = i

total = 0
for i in range(30000):
    key = f"key{i}"
    total += d[key]

elapsed = time.perf_counter() - start

print(total)
print(len(d))
print(elapsed)
