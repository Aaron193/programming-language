#!/usr/bin/env python3

import time


n = 5000
arr = []

for value in range(n, 0, -1):
    arr.append(value)

start = time.perf_counter()

i = 1
while i < len(arr):
    key = arr[i]
    j = i - 1
    shifting = True

    while j >= 0 and shifting:
        if arr[j] > key:
            arr[j + 1] = arr[j]
            j -= 1
        else:
            shifting = False

    arr[j + 1] = key
    i += 1

elapsed = time.perf_counter() - start

print(arr[0])
print(arr[n - 1])
print(elapsed)
