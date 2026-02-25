#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    static constexpr size_t MAX_FRAMES = 256;

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
    std::array<CallFrame, MAX_FRAMES> m_frames{};
    size_t m_frameCount = 0;
    CallFrame* m_activeFrame = nullptr;
    // compiler
    Compiler m_compiler;
    // gc
    GC m_gc;
    size_t m_gcThreshold = 1024 * 1024;
    // globals
    std::vector<std::string> m_globalNames;
    std::vector<Value> m_globalValues;
    std::vector<bool> m_globalDefined;
    std::unordered_map<std::string, Value> m_nativeGlobals;
    std::unordered_map<std::string, ModuleObject*> m_moduleCache;
    std::unordered_set<std::string> m_importStack;
    ModuleObject* m_currentModule = nullptr;
    // open upvalues
    std::vector<UpvalueObject*> m_openUpvalues;
    bool m_traceEnabled = false;
    bool m_disassembleEnabled = false;

    CallFrame& currentFrame() { return *m_activeFrame; }

    // inlined methods
    uint8_t readByte() { return *currentFrame().ip++; }
    uint16_t readShort() {
        CallFrame& frame = currentFrame();
        frame.ip += 2;
        return static_cast<uint16_t>((frame.ip[-2] << 8) | frame.ip[-1]);
    }
    const Value& readConstant() {
        CallFrame& frame = currentFrame();
        return frame.chunk->getConstants()[readByte()];
    }
    const std::string& readNameConstant() { return readConstant().asString(); }

    Status run(bool printReturnValue, Value& returnValue,
               size_t stopFrameCount = 0);
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
                     bool traceEnabled = false, bool disassembleEnabled = false,
                     const std::string& sourcePath = "");
};
