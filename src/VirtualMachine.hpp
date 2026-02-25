#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

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
    struct CallFrame {
        Chunk* chunk;
        uint8_t* ip;
        size_t slotBase;
    };

    // expression evaluation stack
    Stack<Value> m_stack;
    // call frame stack
    std::vector<CallFrame> m_frames;
    // compiler
    Compiler m_compiler;
    // globals
    std::unordered_map<std::string, Value> m_globals;

    CallFrame& currentFrame() { return m_frames.back(); }

    // inlined methods
    uint8_t readByte() { return *currentFrame().ip++; }
    uint16_t readShort() {
        CallFrame& frame = currentFrame();
        frame.ip += 2;
        return static_cast<uint16_t>((frame.ip[-2] << 8) | frame.ip[-1]);
    }
    Value readConstant() {
        CallFrame& frame = currentFrame();
        return frame.chunk->getConstants()[readByte()];
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
