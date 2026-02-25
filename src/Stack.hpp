#pragma once
#include <cstddef>
#include <iostream>

#define STACK_SIZE 256

// fixed stack

template <typename T>
class Stack {
   private:
    T m_data[STACK_SIZE];
    size_t m_capacity = STACK_SIZE;
    int m_top = -1;

   public:
    Stack() = default;
    ~Stack() = default;

    void push(const T& value) {
        if (m_top < (int)m_capacity - 1) {
            m_data[++m_top] = value;
        }
    }

    T pop() {
        if (m_top >= 0) {
            return m_data[m_top--];
        }
        // underflow
        return T();
    }

    T peek(size_t distance) const {
        if (m_top < 0 || distance > static_cast<size_t>(m_top)) {
            return T();
        }

        return m_data[m_top - static_cast<int>(distance)];
    }

    void popN(size_t count) {
        while (count > 0 && m_top >= 0) {
            m_top--;
            count--;
        }
    }

    void reset() { m_top = -1; }

    bool isEmpty() { return m_top == -1; }

    size_t size() { return m_top + 1; }

    void print() {
        for (int i = 0; i <= m_top; ++i) {
            std::cout << m_data[i] << " ";
        }
        std::cout << std::endl;
    }
};
