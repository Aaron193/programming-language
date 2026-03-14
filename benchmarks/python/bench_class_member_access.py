#!/usr/bin/env python3

import time


class Particle:
    __slots__ = ("x", "y")

    def __init__(self) -> None:
        self.x = 0
        self.y = 0

    def move(self, dx: int, dy: int) -> int:
        self.x += dx
        self.y += dy
        return self.x + self.y


particles = []
for i in range(50000):
    p = Particle()
    p.x = i
    p.y = i * 2
    particles.append(p)

start = time.perf_counter()

checksum = 0
for _ in range(10):
    for p in particles:
        checksum += p.move(1, 2)
        checksum += p.x
        checksum += p.y

elapsed = time.perf_counter() - start

print(checksum)
print(len(particles))
print(elapsed)
