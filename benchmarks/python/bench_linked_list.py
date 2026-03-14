#!/usr/bin/env python3

import time


class Node:
    __slots__ = ("value", "next")

    def __init__(self) -> None:
        self.value = 0
        self.next = self


start = time.perf_counter()

head = Node()
head.value = 1
head.next = head

tail = head
for i in range(2, 100001):
    node = Node()
    node.value = i
    node.next = head
    tail.next = node
    tail = node

total = 0
current = head
for _ in range(100000):
    total += current.value
    current = current.next

elapsed = time.perf_counter() - start

print(total)
print(elapsed)
