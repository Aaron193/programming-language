#!/usr/bin/env python3

import time


limit = 100000
composite = [False] * (limit + 1)

start = time.perf_counter()

p = 2
while p * p <= limit:
    if not composite[p]:
        multiple = p * p
        while multiple <= limit:
            composite[multiple] = True
            multiple += p
    p += 1

count = 0
n = 2
while n <= limit:
    if not composite[n]:
        count += 1
    n += 1

elapsed = time.perf_counter() - start

print(count)
print(elapsed)
