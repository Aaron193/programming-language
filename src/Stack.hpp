#pragma once
#include <cstddef>
#include <iostream>

#define STACK_SIZE 256

// fixed stack

template <typename T>
class Stack {
   private:
    T data[STACK_SIZE];
    size_t capacity = STACK_SIZE;
    int top = -1;

   public:
    Stack() = default;
    ~Stack() = default;

    void push(const T& value) {
        if (top < (int)capacity - 1) {
            data[++top] = value;
        }
    }

    T pop() {
        if (top >= 0) {
            return data[top--];
        }
        // underflow
        return T();
    }

    void reset() { top = -1; }

    bool isEmpty() { return top == -1; }

    size_t size() { return top + 1; }

    void print() {
        for (int i = 0; i <= top; ++i) {
            std::cout << data[i] << " ";
        }
        std::cout << std::endl;
    }
};
