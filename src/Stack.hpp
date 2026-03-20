#pragma once
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>

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
#ifndef NDEBUG
        if (m_top >= static_cast<int>(m_capacity) - 1) {
            throw std::overflow_error("Stack overflow.");
        }
#endif
        m_data[++m_top] = value;
    }

    void push(T&& value) {
#ifndef NDEBUG
        if (m_top >= static_cast<int>(m_capacity) - 1) {
            throw std::overflow_error("Stack overflow.");
        }
#endif
        m_data[++m_top] = std::move(value);
    }

    T pop() {
#ifndef NDEBUG
        if (m_top < 0) {
            throw std::underflow_error("Stack underflow.");
        }
#endif
        return m_data[m_top--];
    }

    T popMove() {
#ifndef NDEBUG
        if (m_top < 0) {
            throw std::underflow_error("Stack underflow.");
        }
#endif
        return std::move(m_data[m_top--]);
    }

    const T& peek(size_t distance) const {
        static const T empty{};
        if (m_top < 0 || distance > static_cast<size_t>(m_top)) {
            return empty;
        }

        return m_data[m_top - static_cast<int>(distance)];
    }

    const T& peekUnchecked(size_t distance) const {
        return m_data[m_top - static_cast<int>(distance)];
    }

    T& peekRef(size_t distance) {
        static T empty{};
        if (m_top < 0 || distance > static_cast<size_t>(m_top)) {
            return empty;
        }

        return m_data[m_top - static_cast<int>(distance)];
    }

    T& peekRefUnchecked(size_t distance) {
        return m_data[m_top - static_cast<int>(distance)];
    }

    const T& getAt(size_t index) const {
        static const T empty{};
        if (index > static_cast<size_t>(m_top)) {
            return empty;
        }

        return m_data[index];
    }

    const T& getAtUnchecked(size_t index) const { return m_data[index]; }

    T& getAtRef(size_t index) {
        static T empty{};
        if (index > static_cast<size_t>(m_top)) {
            return empty;
        }

        return m_data[index];
    }

    T& getAtRefUnchecked(size_t index) { return m_data[index]; }

    const T& top() const {
        if (m_top < 0) {
            throw std::underflow_error("Stack underflow.");
        }

        return m_data[m_top];
    }

    const T& topUnchecked() const { return m_data[m_top]; }

    const T& second() const {
        if (m_top < 1) {
            throw std::underflow_error("Stack underflow.");
        }

        return m_data[m_top - 1];
    }

    const T& secondUnchecked() const { return m_data[m_top - 1]; }

    void setAt(size_t index, const T& value) {
        if (index <= static_cast<size_t>(m_top)) {
            m_data[index] = value;
        }
    }

    void setAt(size_t index, T&& value) {
        if (index <= static_cast<size_t>(m_top)) {
            m_data[index] = std::move(value);
        }
    }

    void setAtUnchecked(size_t index, const T& value) { m_data[index] = value; }

    void setAtUnchecked(size_t index, T&& value) {
        m_data[index] = std::move(value);
    }

    void popN(size_t count) {
#ifndef NDEBUG
        if (count > size()) {
            throw std::underflow_error("Stack underflow.");
        }
#endif
        m_top -= static_cast<int>(count);
    }

    void replaceTop(const T& value) {
#ifndef NDEBUG
        if (m_top < 0) {
            throw std::underflow_error("Stack underflow.");
        }
#endif
        m_data[m_top] = value;
    }

    void replaceTop(T&& value) {
#ifndef NDEBUG
        if (m_top < 0) {
            throw std::underflow_error("Stack underflow.");
        }
#endif
        m_data[m_top] = std::move(value);
    }

    void replaceTopUnchecked(const T& value) { m_data[m_top] = value; }

    void replaceTopUnchecked(T&& value) { m_data[m_top] = std::move(value); }

    void replaceTopPair(const T& value) {
#ifndef NDEBUG
        if (m_top < 1) {
            throw std::underflow_error("Stack underflow.");
        }
#endif
        m_data[m_top - 1] = value;
        --m_top;
    }

    void replaceTopPair(T&& value) {
#ifndef NDEBUG
        if (m_top < 1) {
            throw std::underflow_error("Stack underflow.");
        }
#endif
        m_data[m_top - 1] = std::move(value);
        --m_top;
    }

    void replaceTopPairUnchecked(const T& value) {
        m_data[m_top - 1] = value;
        --m_top;
    }

    void replaceTopPairUnchecked(T&& value) {
        m_data[m_top - 1] = std::move(value);
        --m_top;
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
