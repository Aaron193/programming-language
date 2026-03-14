#!/usr/bin/env python3

import time


n = 30
size = n * n

a = []
b = []
c = []

for i in range(n):
    for j in range(n):
        a.append(i + j)
        b.append(i * j)
        c.append(0)

start = time.perf_counter()

for row in range(n):
    row_offset = row * n
    for col in range(n):
        total = 0
        for k in range(n):
            total += a[row_offset + k] * b[k * n + col]
        c[row_offset + col] = total

checksum = 0
for idx in range(size):
    checksum += c[idx]

elapsed = time.perf_counter() - start

print(checksum)
print(elapsed)
