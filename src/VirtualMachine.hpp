#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

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
    Chunk* m_chunk;
    // instruction byte pointer
    uint8_t* m_ip;
    // expression evaluation stack
    Stack<Value> m_stack;
    // stack base for current call frame
    size_t m_frameBase = 0;
    // compiler
    Compiler m_compiler;
    // globals
    std::unordered_map<std::string, Value> m_globals;

    // inlined methods
    uint8_t readByte() { return *this->m_ip++; }
    uint16_t readShort() {
        m_ip += 2;
        return static_cast<uint16_t>((m_ip[-2] << 8) | m_ip[-1]);
    }
    Value readConstant() {
        return this->m_chunk->getConstants()[this->readByte()];
    }
    std::string readNameConstant() {
        Value constant = readConstant();
        return constant.asString();
    }

    Status run(bool printReturnValue, Value& returnValue);
    Status callFunction(std::shared_ptr<FunctionObject> function,
                        uint8_t argumentCount);

   public:
    VirtualMachine() = default;
    ~VirtualMachine() = default;

    Status interpret(std::string_view source);
};
