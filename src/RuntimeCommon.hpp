#pragma once

#include <cstddef>
#include <cstdint>

class VirtualMachine;
struct NativeFunctionObject;

enum Status {
    OK,
    COMPILATION_ERROR,
    RUNTIME_ERROR,
};

using NativeInvokeFn = Status (*)(VirtualMachine& vm,
                                  const NativeFunctionObject& native,
                                  uint8_t argumentCount, size_t calleeIndex);
