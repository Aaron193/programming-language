#pragma once
#include <cstdint>
#include <string_view>

#include "Chunk.hpp"
#include "Compiler.hpp"
#include "Stack.hpp"

enum Status {
    OK,
    COMPILATION_ERROR,
    RUNTIME_ERROR,
};

class VirtualMachine {
   private:
    // current chunk being interpreted
    Chunk* chunk;
    // instruction byte pointer
    uint8_t* ip;
    // expression evaluation stack
    Stack<Value> stack;
    // compiler
    Compiler compiler;

    // inlined methods
    uint8_t readByte() { return *this->ip++; }
    uint8_t writeByte(uint8_t byte) { return *this->ip++ = byte; }
    Value readConstant() {
        return this->chunk->getConstants()[this->readByte()];
    }

    Status run();

   public:
    VirtualMachine() = default;
    ~VirtualMachine() = default;

    Status interpret(std::string_view source);
};
