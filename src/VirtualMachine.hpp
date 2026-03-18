#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Chunk.hpp"
#include "Compiler.hpp"
#include "GC.hpp"
#include "RuntimeCommon.hpp"
#include "Stack.hpp"

class VirtualMachine {
   private:
    static constexpr size_t MAX_FRAMES = 256;
    friend Status invokeBuiltinNative(VirtualMachine& vm,
                                      const NativeFunctionObject& native,
                                      uint8_t argumentCount,
                                      size_t calleeIndex);
    friend Status invokePackageNative(VirtualMachine& vm,
                                      const NativeFunctionObject& native,
                                      uint8_t argumentCount,
                                      size_t calleeIndex);
    friend bool packageValueToValue(VirtualMachine& vm,
                                    const ExprPackageValue& value,
                                    Value& outValue,
                                    std::string& outError);

    struct CallFrame {
        Chunk* chunk;
        uint8_t* ip;
        size_t slotBase;
        size_t calleeIndex;
        InstanceObject* receiver;
        ClosureObject* closure;
        ModuleObject* module;
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
    std::vector<TypeRef> m_globalTypes;
    std::vector<Value> m_globalValues;
    std::vector<bool> m_globalDefined;
    std::unordered_map<std::string, Value> m_nativeGlobals;
    std::unordered_map<std::string, ModuleObject*> m_moduleCache;
    std::unordered_set<std::string> m_importStack;
    std::vector<std::string> m_packageSearchPaths;
    std::vector<void*> m_loadedNativeLibraryHandles;
    std::deque<NativePackageBinding> m_nativePackageBindings;
    ModuleObject* m_currentModule = nullptr;
    bool m_defaultStrictMode = false;
    // open upvalues, maintained in descending stack-index order
    UpvalueObject* m_openUpvaluesHead = nullptr;
    bool m_traceEnabled = false;
    bool m_disassembleEnabled = false;

    CallFrame& currentFrame() { return *m_activeFrame; }
    ModuleObject* currentGlobalModule() {
        return m_activeFrame ? m_activeFrame->module : nullptr;
    }
    std::vector<std::string>& currentGlobalNames() {
        ModuleObject* module = currentGlobalModule();
        return module ? module->globalNames : m_globalNames;
    }
    std::vector<TypeRef>& currentGlobalTypes() {
        ModuleObject* module = currentGlobalModule();
        return module ? module->globalTypes : m_globalTypes;
    }
    std::vector<Value>& currentGlobalValues() {
        ModuleObject* module = currentGlobalModule();
        return module ? module->globalValues : m_globalValues;
    }
    std::vector<bool>& currentGlobalDefined() {
        ModuleObject* module = currentGlobalModule();
        return module ? module->globalDefined : m_globalDefined;
    }

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
    size_t currentInstructionOffset() {
        CallFrame& frame = currentFrame();
        return static_cast<size_t>(frame.ip - frame.chunk->getBytes() - 1);
    }

    Value makeStringValue(std::string text);
    bool tryFuseBuiltinStringConcat(const NativeFunctionObject& native,
                                    uint8_t argumentCount, size_t calleeIndex,
                                    Status& outStatus);
    Status run(bool printReturnValue, Value& returnValue,
               size_t stopFrameCount = 0);
    Status runtimeError(const std::string& message);
    void printStackTrace();
    Status callClosure(ClosureObject* closure, uint8_t argumentCount,
                       InstanceObject* receiver = nullptr);
    Status callValue(Value callee, uint8_t argumentCount, size_t calleeIndex);
    Status callNativeMethod(NativeMethodId id, const std::string& name,
                            const Value& receiver, uint8_t argumentCount,
                            size_t calleeIndex);
    Status invokeProperty(size_t instructionOffset, const std::string& name,
                          uint8_t argumentCount);
    Status invokeSuper(const std::string& name, uint8_t argumentCount);
    UpvalueObject* captureUpvalue(size_t stackIndex);
    void closeUpvalues(size_t fromStackIndex);
    void markRoots();
    void collectGarbage();
    void unloadNativeLibraries();
    void resetRuntimeState();

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
    ~VirtualMachine();

    void setPackageSearchPaths(std::vector<std::string> packageSearchPaths) {
        m_packageSearchPaths = std::move(packageSearchPaths);
    }

    Status interpret(std::string_view source, bool printReturnValue = false,
                     bool traceEnabled = false, bool disassembleEnabled = false,
                     const std::string& sourcePath = "",
                     bool strictMode = false);
};
