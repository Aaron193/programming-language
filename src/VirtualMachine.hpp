#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Chunk.hpp"
#include "Compiler.hpp"
#include "GC.hpp"
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
        size_t calleeIndex;
        InstanceObject* receiver;
        ClosureObject* closure;
    };

    // expression evaluation stack
    Stack<Value> m_stack;
    // call frame stack
    std::vector<CallFrame> m_frames;
    // compiler
    Compiler m_compiler;
    // gc
    GC m_gc;
    size_t m_gcThreshold = 1024 * 1024;
    // globals
    std::unordered_map<std::string, Value> m_globals;
    // open upvalues
    std::vector<UpvalueObject*> m_openUpvalues;
    bool m_traceEnabled = false;
    bool m_disassembleEnabled = false;

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
    Status runtimeError(const std::string& message);
    void printStackTrace();
    Status callClosure(ClosureObject* closure, uint8_t argumentCount,
                       InstanceObject* receiver = nullptr);
    UpvalueObject* captureUpvalue(size_t stackIndex);
    void closeUpvalues(size_t fromStackIndex);
    void markRoots();
    void collectGarbage();

    template <typename T, typename... Args>
    T* gcAlloc(Args&&... args) {
        if (m_gc.bytesAllocated() >= m_gcThreshold) {
            collectGarbage();
            size_t next = m_gc.bytesAllocated() * 2;
            m_gcThreshold = next < (1024 * 1024) ? (1024 * 1024) : next;
        }

        return m_gc.allocate<T>(std::forward<Args>(args)...);
    }

   public:
    VirtualMachine() = default;
    ~VirtualMachine() = default;

    Status interpret(std::string_view source, bool printReturnValue = false,
                     bool traceEnabled = false,
                     bool disassembleEnabled = false);
};
