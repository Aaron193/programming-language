import time

sum = 0

timeA = time.perf_counter()

for i in range(100000):
    sum += i

timeB = time.perf_counter()

timeTaken = timeB - timeA

print(timeTaken)
print(sum)