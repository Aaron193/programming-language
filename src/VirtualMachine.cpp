#include "VirtualMachine.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "StdLib.hpp"

static bool isFalsey(const Value& value) {
    if (value.isNil()) return true;
    if (value.isBool()) return !value.asBool();
    return false;
}

static bool isNumberPair(const Value& lhs, const Value& rhs) {
    return lhs.isAnyNumeric() && rhs.isAnyNumeric();
}

static bool valueToSignedInt(const Value& value, int64_t& out) {
    if (value.isSignedInt()) {
        out = value.asSignedInt();
        return true;
    }
    if (value.isUnsignedInt()) {
        out = static_cast<int64_t>(value.asUnsignedInt());
        return true;
    }
    if (value.isNumber()) {
        out = static_cast<int64_t>(value.asNumber());
        return true;
    }
    return false;
}

static bool valueToUnsignedInt(const Value& value, uint64_t& out) {
    if (value.isUnsignedInt()) {
        out = value.asUnsignedInt();
        return true;
    }
    if (value.isSignedInt()) {
        out = static_cast<uint64_t>(value.asSignedInt());
        return true;
    }
    if (value.isNumber()) {
        out = static_cast<uint64_t>(value.asNumber());
        return true;
    }
    return false;
}

static bool valueToDouble(const Value& value, double& out) {
    if (value.isNumber()) {
        out = value.asNumber();
        return true;
    }
    if (value.isSignedInt()) {
        out = static_cast<double>(value.asSignedInt());
        return true;
    }
    if (value.isUnsignedInt()) {
        out = static_cast<double>(value.asUnsignedInt());
        return true;
    }
    return false;
}

static int64_t wrapSignedAdd(int64_t lhs, int64_t rhs) {
    return static_cast<int64_t>(static_cast<uint64_t>(lhs) +
                                static_cast<uint64_t>(rhs));
}

static int64_t wrapSignedSub(int64_t lhs, int64_t rhs) {
    return static_cast<int64_t>(static_cast<uint64_t>(lhs) -
                                static_cast<uint64_t>(rhs));
}

static int64_t wrapSignedMul(int64_t lhs, int64_t rhs) {
    return static_cast<int64_t>(static_cast<uint64_t>(lhs) *
                                static_cast<uint64_t>(rhs));
}

static int64_t requireSignedInt(const Value& value) {
    assert(value.isSignedInt());
    return value.asSignedInt();
}

static uint64_t requireUnsignedInt(const Value& value) {
    assert(value.isUnsignedInt());
    return value.asUnsignedInt();
}

static bool hasStrictDirective(std::string_view source) {
    return source.rfind("#!strict", 0) == 0;
}

static std::string_view stripStrictDirectiveLine(std::string_view source) {
    if (!hasStrictDirective(source)) {
        return source;
    }

    size_t newlinePos = source.find('\n');
    if (newlinePos == std::string_view::npos) {
        return std::string_view();
    }

    return source.substr(newlinePos + 1);
}

static std::string valueToString(const Value& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

static std::string valueTypeName(const Value& value) {
    if (value.isAnyNumeric()) return "number";
    if (value.isBool()) return "bool";
    if (value.isNil()) return "null";
    if (value.isString()) return "string";
    if (value.isArray()) return "array";
    if (value.isDict()) return "dict";
    if (value.isSet()) return "set";
    if (value.isIterator()) return "iterator";
    if (value.isModule()) return "module";
    if (value.isClass()) return "class";
    if (value.isInstance()) return "instance";
    if (value.isNative()) return "native";
    if (value.isNativeBound()) return "native_bound_method";
    if (value.isBoundMethod()) return "bound_method";
    if (value.isClosure()) return "closure";
    if (value.isFunction()) return "function";
    return "unknown";
}

static bool toDictKey(const Value& value, std::string& key) {
    if (value.isString()) {
        key = value.asString();
        return true;
    }

    if (value.isAnyNumeric()) {
        std::ostringstream stream;
        if (value.isNumber()) {
            stream << value.asNumber();
        } else if (value.isSignedInt()) {
            stream << value.asSignedInt();
        } else {
            stream << value.asUnsignedInt();
        }
        key = stream.str();
        return true;
    }

    return false;
}

static bool toArrayIndex(const Value& value, size_t& index) {
    if (!value.isAnyNumeric()) {
        return false;
    }

    if (value.isSignedInt()) {
        int64_t signedIndex = value.asSignedInt();
        if (signedIndex < 0) {
            return false;
        }

        index = static_cast<size_t>(signedIndex);
        return true;
    }

    if (value.isUnsignedInt()) {
        index = static_cast<size_t>(value.asUnsignedInt());
        return true;
    }

    double number = value.asNumber();
    if (number < 0.0 || std::floor(number) != number) {
        return false;
    }

    index = static_cast<size_t>(number);
    return true;
}

static bool containsValue(const std::vector<Value>& elements,
                          const Value& needle) {
    for (const auto& element : elements) {
        if (element == needle) {
            return true;
        }
    }

    return false;
}

static ClosureObject* findMethodClosure(ClassObject* klass,
                                        const std::string& name) {
    ClassObject* current = klass;
    while (current) {
        auto it = current->methods.find(name);
        if (it != current->methods.end()) {
            return it->second;
        }
        current = current->superclass;
    }

    return nullptr;
}

static bool isInstanceOfClass(const InstanceObject* instance,
                              const std::string& expectedClassName) {
    if (!instance || !instance->klass) {
        return false;
    }

    ClassObject* current = instance->klass;
    while (current != nullptr) {
        if (current->name == expectedClassName) {
            return true;
        }
        current = current->superclass;
    }

    return false;
}

static TypeRef inferRuntimeType(const Value& value) {
    if (value.isSignedInt()) {
        return TypeInfo::makeI64();
    }
    if (value.isUnsignedInt()) {
        return TypeInfo::makeU64();
    }
    if (value.isNumber()) {
        return TypeInfo::makeF64();
    }
    if (value.isBool()) {
        return TypeInfo::makeBool();
    }
    if (value.isString()) {
        return TypeInfo::makeStr();
    }
    if (value.isNil()) {
        return TypeInfo::makeNull();
    }
    if (value.isInstance() && value.asInstance() && value.asInstance()->klass) {
        return TypeInfo::makeClass(value.asInstance()->klass->name);
    }
    if (value.isArray()) {
        auto array = value.asArray();
        return TypeInfo::makeArray(array && array->elementType
                                       ? array->elementType
                                       : TypeInfo::makeAny());
    }
    if (value.isDict()) {
        auto dict = value.asDict();
        TypeRef key =
            dict && dict->keyType ? dict->keyType : TypeInfo::makeAny();
        TypeRef val =
            dict && dict->valueType ? dict->valueType : TypeInfo::makeAny();
        return TypeInfo::makeDict(key, val);
    }
    if (value.isSet()) {
        auto set = value.asSet();
        return TypeInfo::makeSet(set && set->elementType ? set->elementType
                                                         : TypeInfo::makeAny());
    }

    return TypeInfo::makeAny();
}

static bool valueMatchesType(const Value& value, const TypeRef& expected) {
    if (!expected || expected->isAny()) {
        return true;
    }

    if (expected->kind == TypeKind::OPTIONAL) {
        if (value.isNil()) {
            return true;
        }
        return valueMatchesType(value, expected->innerType);
    }

    if (expected->kind == TypeKind::NULL_TYPE) {
        return value.isNil();
    }

    if (expected->isInteger()) {
        return value.isAnyNumeric();
    }

    if (expected->isFloat()) {
        return value.isAnyNumeric();
    }

    if (expected->kind == TypeKind::BOOL) {
        return value.isBool();
    }

    if (expected->kind == TypeKind::STR) {
        return value.isString();
    }

    if (expected->kind == TypeKind::FUNCTION) {
        return value.isClosure() || value.isFunction() || value.isNative() ||
               value.isBoundMethod() || value.isNativeBound();
    }

    if (expected->kind == TypeKind::CLASS) {
        if (!value.isInstance()) {
            return false;
        }
        return isInstanceOfClass(value.asInstance(), expected->className);
    }

    if (expected->kind == TypeKind::ARRAY) {
        if (!value.isArray()) {
            return false;
        }

        auto array = value.asArray();
        TypeRef elementType =
            expected->elementType ? expected->elementType : TypeInfo::makeAny();
        for (const auto& element : array->elements) {
            if (!valueMatchesType(element, elementType)) {
                return false;
            }
        }
        return true;
    }

    if (expected->kind == TypeKind::DICT) {
        if (!value.isDict()) {
            return false;
        }

        auto dict = value.asDict();
        TypeRef valueType =
            expected->valueType ? expected->valueType : TypeInfo::makeAny();
        for (const auto& entry : dict->map) {
            if (!valueMatchesType(entry.second, valueType)) {
                return false;
            }
        }
        return true;
    }

    if (expected->kind == TypeKind::SET) {
        if (!value.isSet()) {
            return false;
        }

        auto set = value.asSet();
        TypeRef elementType =
            expected->elementType ? expected->elementType : TypeInfo::makeAny();
        for (const auto& element : set->elements) {
            if (!valueMatchesType(element, elementType)) {
                return false;
            }
        }
        return true;
    }

    TypeRef actual = inferRuntimeType(value);
    return isAssignable(actual, expected);
}

UpvalueObject* VirtualMachine::captureUpvalue(size_t stackIndex) {
    for (const auto& upvalue : m_openUpvalues) {
        if (!upvalue->isClosed && upvalue->stackIndex == stackIndex) {
            return upvalue;
        }
    }

    auto upvalue = gcAlloc<UpvalueObject>();
    upvalue->stackIndex = stackIndex;
    upvalue->isClosed = false;
    m_openUpvalues.push_back(upvalue);
    return upvalue;
}

void VirtualMachine::closeUpvalues(size_t fromStackIndex) {
    for (const auto& upvalue : m_openUpvalues) {
        if (!upvalue->isClosed && upvalue->stackIndex >= fromStackIndex) {
            upvalue->closed = m_stack.getAt(upvalue->stackIndex);
            upvalue->isClosed = true;
        }
    }
}

void VirtualMachine::markRoots() {
    for (size_t i = 0; i < m_stack.size(); ++i) {
        m_gc.markValue(m_stack.getAt(i));
    }

    for (size_t i = 0; i < m_globalValues.size(); ++i) {
        if (m_globalDefined[i]) {
            m_gc.markValue(m_globalValues[i]);
        }
    }

    for (auto* upvalue : m_openUpvalues) {
        m_gc.markObject(upvalue);
    }

    for (const auto& [path, module] : m_moduleCache) {
        (void)path;
        m_gc.markObject(module);
    }

    m_gc.markObject(m_currentModule);

    for (size_t i = 0; i < m_frameCount; ++i) {
        const auto& frame = m_frames[i];
        m_gc.markObject(frame.receiver);
        m_gc.markObject(frame.closure);

        if (frame.chunk != nullptr) {
            for (const auto& constant : frame.chunk->getConstantsRange()) {
                m_gc.markValue(constant);
            }
        }
    }
}

void VirtualMachine::collectGarbage() {
    markRoots();
    m_gc.drainGrayStack();
    m_gc.sweep();
}

void VirtualMachine::printStackTrace() {
    std::cerr << "[trace][runtime] stack:" << std::endl;

    for (int index = static_cast<int>(m_frameCount) - 1; index >= 0; --index) {
        const CallFrame& frame = m_frames[index];
        int offset = static_cast<int>(frame.ip - frame.chunk->getBytes()) - 1;
        if (offset < 0) {
            offset = 0;
        }

        std::string functionName = "<script>";
        if (frame.closure && frame.closure->function) {
            if (!frame.closure->function->name.empty()) {
                functionName = frame.closure->function->name;
            } else {
                functionName = "<anonymous>";
            }
        }

        std::cerr << "  at " << functionName << "() [line "
                  << frame.chunk->lineAt(offset) << "]" << std::endl;
    }
}

Status VirtualMachine::runtimeError(const std::string& message) {
    CallFrame& frame = currentFrame();
    int offset = static_cast<int>(frame.ip - frame.chunk->getBytes()) - 1;
    if (offset < 0) {
        offset = 0;
    }

    std::cerr << "[error][runtime][line " << frame.chunk->lineAt(offset) << "] "
              << message << std::endl;
    printStackTrace();
    return Status::RUNTIME_ERROR;
}

Status VirtualMachine::callClosure(ClosureObject* closure,
                                   uint8_t argumentCount,
                                   InstanceObject* receiver) {
    auto function = closure->function;
    if (function->parameters.size() != argumentCount) {
        return runtimeError("Function '" + function->name + "' expected " +
                            std::to_string(function->parameters.size()) +
                            " arguments but got " +
                            std::to_string(static_cast<int>(argumentCount)) +
                            ".");
    }

    size_t calleeIndex =
        m_stack.size() - static_cast<size_t>(argumentCount) - 1;

    if (m_frameCount >= MAX_FRAMES) {
        return runtimeError("Stack overflow: too many nested calls.");
    }

    m_frames[m_frameCount++] = CallFrame{function->chunk.get(),
                                         function->chunk->getBytes(),
                                         calleeIndex + 1,
                                         calleeIndex,
                                         receiver,
                                         closure};
    m_activeFrame = &m_frames[m_frameCount - 1];
    return Status::OK;
}

Status VirtualMachine::run(bool printReturnValue, Value& returnValue,
                           size_t stopFrameCount) {
    while (true) {
        CallFrame& frame = currentFrame();

        if (m_traceEnabled) {
            m_stack.print();
            frame.chunk->disassembleInstruction(
                static_cast<int>(frame.ip - frame.chunk->getBytes()));
        }

        uint8_t instruction = readByte();

        if (!m_traceEnabled) {
            if (instruction == OpCode::GET_LOCAL) {
                uint8_t slot = readByte();
                m_stack.push(m_stack.getAt(currentFrame().slotBase + slot));
                continue;
            }
            if (instruction == OpCode::SET_LOCAL) {
                uint8_t slot = readByte();
                m_stack.setAt(currentFrame().slotBase + slot, m_stack.peek(0));
                continue;
            }
            if (instruction == OpCode::GET_GLOBAL_SLOT) {
                uint8_t slot = readByte();
                if (slot >= m_globalValues.size() || !m_globalDefined[slot]) {
                    std::string name = slot < m_globalNames.size()
                                           ? m_globalNames[slot]
                                           : "<unknown>";
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                m_stack.push(m_globalValues[slot]);
                continue;
            }
            if (instruction == OpCode::SET_GLOBAL_SLOT) {
                uint8_t slot = readByte();
                if (slot >= m_globalValues.size() || !m_globalDefined[slot]) {
                    std::string name = slot < m_globalNames.size()
                                           ? m_globalNames[slot]
                                           : "<unknown>";
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                m_globalValues[slot] = m_stack.peek(0);
                continue;
            }
            if (instruction == OpCode::CONSTANT) {
                const Value& val = readConstant();
                m_stack.push(val);
                continue;
            }
            if (instruction == OpCode::ADD) {
                Value b = m_stack.pop();
                Value a = m_stack.pop();

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(
                        Value(wrapSignedAdd(a.asSignedInt(), b.asSignedInt())));
                    continue;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() + b.asUnsignedInt()));
                    continue;
                }

                if (a.isAnyNumeric() && b.isAnyNumeric()) {
                    double lhs = 0.0;
                    double rhs = 0.0;
                    valueToDouble(a, lhs);
                    valueToDouble(b, rhs);
                    m_stack.push(Value(lhs + rhs));
                    continue;
                }

                if (a.isString() && b.isString()) {
                    m_stack.push(Value(a.asString() + b.asString()));
                    continue;
                }

                return runtimeError(
                    "Operands must be two numbers or two strings for '+'.");
            }
            if (instruction == OpCode::LESS_THAN) {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '<'.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(Value(a.asSignedInt() < b.asSignedInt()));
                    continue;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() < b.asUnsignedInt()));
                    continue;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs < rhs));
                continue;
            }
            if (instruction == OpCode::JUMP_IF_FALSE) {
                uint16_t offset = readShort();
                const Value& condition = m_stack.peek(0);
                if (isFalsey(condition)) {
                    currentFrame().ip += offset;
                }
                continue;
            }
            if (instruction == OpCode::LOOP) {
                uint16_t offset = readShort();
                currentFrame().ip -= offset;
                continue;
            }
            if (instruction == OpCode::POP) {
                m_stack.pop();
                continue;
            }
        }

        switch (instruction) {
            case OpCode::RETURN: {
                Value result = m_stack.pop();
                CallFrame finishedFrame = currentFrame();
                closeUpvalues(finishedFrame.slotBase);
                m_frameCount--;

                if (m_frameCount == stopFrameCount) {
                    m_activeFrame = (m_frameCount == 0)
                                        ? nullptr
                                        : &m_frames[m_frameCount - 1];
                    returnValue = result;
                    if (printReturnValue) {
                        std::cout << "Return constant: " << result << std::endl;
                    }
                    return Status::OK;
                }

                m_activeFrame = &m_frames[m_frameCount - 1];

                m_stack.popN(m_stack.size() - finishedFrame.calleeIndex);
                m_stack.push(result);
                break;
            }
            case OpCode::CONSTANT: {
                const Value& val = readConstant();
                m_stack.push(val);
                break;
            }
            case OpCode::NIL: {
                m_stack.push(Value());
                break;
            }
            case OpCode::TRUE_LITERAL: {
                m_stack.push(Value(true));
                break;
            }
            case OpCode::FALSE_LITERAL: {
                m_stack.push(Value(false));
                break;
            }
            case OpCode::NEGATE: {
                Value value = m_stack.pop();
                if (!value.isAnyNumeric()) {
                    return runtimeError(
                        "Operand must be a number for unary '-'.");
                }

                if (value.isSignedInt()) {
                    m_stack.push(Value(wrapSignedSub(0, value.asSignedInt())));
                } else if (value.isUnsignedInt()) {
                    uint64_t asUnsigned = value.asUnsignedInt();
                    m_stack.push(Value(static_cast<int64_t>(0u - asUnsigned)));
                } else {
                    m_stack.push(Value(-value.asNumber()));
                }
                break;
            }
            case OpCode::NOT: {
                Value value = m_stack.pop();
                m_stack.push(Value(isFalsey(value)));
                break;
            }
            case OpCode::EQUAL_OP: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(Value(a == b));
                break;
            }
            case OpCode::NOT_EQUAL_OP: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(Value(a != b));
                break;
            }
            case OpCode::ADD: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(
                        Value(wrapSignedAdd(a.asSignedInt(), b.asSignedInt())));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() + b.asUnsignedInt()));
                    break;
                }

                if (a.isAnyNumeric() && b.isAnyNumeric()) {
                    double lhs = 0.0;
                    double rhs = 0.0;
                    valueToDouble(a, lhs);
                    valueToDouble(b, rhs);
                    m_stack.push(Value(lhs + rhs));
                    break;
                }

                if (a.isString() && b.isString()) {
                    m_stack.push(Value(a.asString() + b.asString()));
                    break;
                }

                return runtimeError(
                    "Operands must be two numbers or two strings for '+'.");
            }
            case OpCode::SUB: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '-'.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(
                        Value(wrapSignedSub(a.asSignedInt(), b.asSignedInt())));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() - b.asUnsignedInt()));
                    break;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs - rhs));
                break;
            }
            case OpCode::MULT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '*'.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(
                        Value(wrapSignedMul(a.asSignedInt(), b.asSignedInt())));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() * b.asUnsignedInt()));
                    break;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs * rhs));
                break;
            }
            case OpCode::DIV: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '/'.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    if (b.asSignedInt() == 0) {
                        return runtimeError("Division by zero.");
                    }
                    m_stack.push(Value(a.asSignedInt() / b.asSignedInt()));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    if (b.asUnsignedInt() == 0) {
                        return runtimeError("Division by zero.");
                    }
                    m_stack.push(Value(a.asUnsignedInt() / b.asUnsignedInt()));
                    break;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs / rhs));
                break;
            }
            case OpCode::IADD: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                m_stack.push(Value(wrapSignedAdd(lhs, rhs)));
                break;
            }
            case OpCode::ISUB: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                m_stack.push(Value(wrapSignedSub(lhs, rhs)));
                break;
            }
            case OpCode::IMULT: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                m_stack.push(Value(wrapSignedMul(lhs, rhs)));
                break;
            }
            case OpCode::IDIV: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                if (rhs == 0) {
                    return runtimeError("Division by zero.");
                }
                m_stack.push(Value(lhs / rhs));
                break;
            }
            case OpCode::IMOD: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                if (rhs == 0) {
                    return runtimeError("Division by zero.");
                }
                m_stack.push(Value(lhs % rhs));
                break;
            }
            case OpCode::UADD: {
                uint64_t rhs = requireUnsignedInt(m_stack.pop());
                uint64_t lhs = requireUnsignedInt(m_stack.pop());
                m_stack.push(Value(lhs + rhs));
                break;
            }
            case OpCode::USUB: {
                uint64_t rhs = requireUnsignedInt(m_stack.pop());
                uint64_t lhs = requireUnsignedInt(m_stack.pop());
                m_stack.push(Value(lhs - rhs));
                break;
            }
            case OpCode::UMULT: {
                uint64_t rhs = requireUnsignedInt(m_stack.pop());
                uint64_t lhs = requireUnsignedInt(m_stack.pop());
                m_stack.push(Value(lhs * rhs));
                break;
            }
            case OpCode::UDIV: {
                uint64_t rhs = requireUnsignedInt(m_stack.pop());
                uint64_t lhs = requireUnsignedInt(m_stack.pop());
                if (rhs == 0) {
                    return runtimeError("Division by zero.");
                }
                m_stack.push(Value(lhs / rhs));
                break;
            }
            case OpCode::UMOD: {
                uint64_t rhs = requireUnsignedInt(m_stack.pop());
                uint64_t lhs = requireUnsignedInt(m_stack.pop());
                if (rhs == 0) {
                    return runtimeError("Division by zero.");
                }
                m_stack.push(Value(lhs % rhs));
                break;
            }
            case OpCode::GREATER_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '>'.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(Value(a.asSignedInt() > b.asSignedInt()));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() > b.asUnsignedInt()));
                    break;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs > rhs));
                break;
            }
            case OpCode::LESS_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '<'.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(Value(a.asSignedInt() < b.asSignedInt()));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() < b.asUnsignedInt()));
                    break;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs < rhs));
                break;
            }
            case OpCode::GREATER_EQUAL_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '>='.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(Value(a.asSignedInt() >= b.asSignedInt()));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() >= b.asUnsignedInt()));
                    break;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs >= rhs));
                break;
            }
            case OpCode::LESS_EQUAL_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '<='.");
                }

                if (a.isSignedInt() && b.isSignedInt()) {
                    m_stack.push(Value(a.asSignedInt() <= b.asSignedInt()));
                    break;
                }

                if (a.isUnsignedInt() && b.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() <= b.asUnsignedInt()));
                    break;
                }

                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                m_stack.push(Value(lhs <= rhs));
                break;
            }
            case OpCode::IGREATER: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                m_stack.push(Value(lhs > rhs));
                break;
            }
            case OpCode::ILESS: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                m_stack.push(Value(lhs < rhs));
                break;
            }
            case OpCode::IGREATER_EQ: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                m_stack.push(Value(lhs >= rhs));
                break;
            }
            case OpCode::ILESS_EQ: {
                int64_t rhs = requireSignedInt(m_stack.pop());
                int64_t lhs = requireSignedInt(m_stack.pop());
                m_stack.push(Value(lhs <= rhs));
                break;
            }
            case OpCode::POP: {
                m_stack.pop();
                break;
            }
            case OpCode::PRINT_OP: {
                Value value = m_stack.pop();
                std::cout << value << std::endl;
                break;
            }
            case OpCode::DEFINE_GLOBAL: {
                const std::string& name = readNameConstant();
                Value value = m_stack.pop();
                m_nativeGlobals[name] = value;
                break;
            }
            case OpCode::GET_GLOBAL: {
                const std::string& name = readNameConstant();
                auto it = m_nativeGlobals.find(name);
                if (it == m_nativeGlobals.end()) {
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                m_stack.push(it->second);
                break;
            }
            case OpCode::SET_GLOBAL: {
                const std::string& name = readNameConstant();
                auto it = m_nativeGlobals.find(name);
                if (it == m_nativeGlobals.end()) {
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                it->second = m_stack.peek(0);
                break;
            }
            case OpCode::DEFINE_GLOBAL_SLOT: {
                uint8_t slot = readByte();
                if (slot >= m_globalValues.size()) {
                    return runtimeError("Invalid global slot.");
                }
                m_globalValues[slot] = m_stack.pop();
                m_globalDefined[slot] = true;
                break;
            }
            case OpCode::GET_GLOBAL_SLOT: {
                uint8_t slot = readByte();
                if (slot >= m_globalValues.size() || !m_globalDefined[slot]) {
                    std::string name = slot < m_globalNames.size()
                                           ? m_globalNames[slot]
                                           : "<unknown>";
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                m_stack.push(m_globalValues[slot]);
                break;
            }
            case OpCode::SET_GLOBAL_SLOT: {
                uint8_t slot = readByte();
                if (slot >= m_globalValues.size() || !m_globalDefined[slot]) {
                    std::string name = slot < m_globalNames.size()
                                           ? m_globalNames[slot]
                                           : "<unknown>";
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                m_globalValues[slot] = m_stack.peek(0);
                break;
            }
            case OpCode::GET_LOCAL: {
                uint8_t slot = readByte();
                m_stack.push(m_stack.getAt(currentFrame().slotBase + slot));
                break;
            }
            case OpCode::SET_LOCAL: {
                uint8_t slot = readByte();
                m_stack.setAt(currentFrame().slotBase + slot, m_stack.peek(0));
                break;
            }
            case OpCode::GET_UPVALUE: {
                uint8_t slot = readByte();
                auto upvalue = currentFrame().closure->upvalues[slot];
                if (upvalue->isClosed) {
                    m_stack.push(upvalue->closed);
                } else {
                    m_stack.push(m_stack.getAt(upvalue->stackIndex));
                }
                break;
            }
            case OpCode::SET_UPVALUE: {
                uint8_t slot = readByte();
                auto upvalue = currentFrame().closure->upvalues[slot];
                if (upvalue->isClosed) {
                    upvalue->closed = m_stack.peek(0);
                } else {
                    m_stack.setAt(upvalue->stackIndex, m_stack.peek(0));
                }
                break;
            }
            case OpCode::CLASS_OP: {
                const std::string& name = readNameConstant();
                auto klass = gcAlloc<ClassObject>();
                klass->name = name;

                const auto& compilerFieldTypes = m_compiler.classFieldTypes();
                auto fieldIt = compilerFieldTypes.find(name);
                if (fieldIt != compilerFieldTypes.end()) {
                    klass->fieldTypes = fieldIt->second;
                }

                const auto& compilerMethodTypes =
                    m_compiler.classMethodSignatures();
                auto methodIt = compilerMethodTypes.find(name);
                if (methodIt != compilerMethodTypes.end()) {
                    klass->methodTypes = methodIt->second;
                }

                m_stack.push(Value(klass));
                break;
            }
            case OpCode::INHERIT: {
                Value superclassValue = m_stack.pop();
                Value subclassValue = m_stack.peek(0);

                if (!superclassValue.isClass() || !subclassValue.isClass()) {
                    return runtimeError("Inheritance requires classes.");
                }

                auto superclass = superclassValue.asClass();
                auto subclass = subclassValue.asClass();
                subclass->superclass = superclass;

                for (const auto& fieldType : superclass->fieldTypes) {
                    if (subclass->fieldTypes.find(fieldType.first) ==
                        subclass->fieldTypes.end()) {
                        subclass->fieldTypes[fieldType.first] =
                            fieldType.second;
                    }
                }

                for (const auto& methodType : superclass->methodTypes) {
                    if (subclass->methodTypes.find(methodType.first) ==
                        subclass->methodTypes.end()) {
                        subclass->methodTypes[methodType.first] =
                            methodType.second;
                    }
                }

                for (const auto& method : superclass->methods) {
                    if (subclass->methods.find(method.first) ==
                        subclass->methods.end()) {
                        subclass->methods[method.first] = method.second;
                    }
                }
                break;
            }
            case OpCode::METHOD: {
                const std::string& name = readNameConstant();
                Value method = m_stack.peek(0);
                Value klass = m_stack.peek(1);

                if (!klass.isClass() || !method.isClosure()) {
                    return runtimeError("Invalid method declaration.");
                }

                klass.asClass()->methods[name] = method.asClosure();
                m_stack.pop();
                break;
            }
            case OpCode::GET_THIS: {
                auto receiver = currentFrame().receiver;
                if (!receiver) {
                    return runtimeError(
                        "Cannot use 'this' outside of a method.");
                }

                m_stack.push(Value(receiver));
                break;
            }
            case OpCode::GET_SUPER: {
                const std::string& name = readNameConstant();
                auto receiver = currentFrame().receiver;
                if (!receiver || !receiver->klass ||
                    !receiver->klass->superclass) {
                    return runtimeError("Invalid super lookup.");
                }

                auto method =
                    findMethodClosure(receiver->klass->superclass, name);
                if (!method) {
                    return runtimeError("Undefined superclass method '" + name +
                                        "'.");
                }

                auto bound = gcAlloc<BoundMethodObject>();
                bound->receiver = receiver;
                bound->method = method;
                m_stack.push(Value(bound));
                break;
            }
            case OpCode::GET_PROPERTY: {
                const std::string& name = readNameConstant();
                Value receiver = m_stack.peek(0);

                if (receiver.isModule()) {
                    auto module = receiver.asModule();
                    auto it = module->exports.find(name);
                    if (it == module->exports.end()) {
                        return runtimeError("Module '" + module->path +
                                            "' has no export '" + name + "'.");
                    }

                    auto typeIt = module->exportTypes.find(name);
                    if (typeIt != module->exportTypes.end() &&
                        !valueMatchesType(it->second, typeIt->second)) {
                        return runtimeError(
                            "Type error: module export '" + name + "' from '" +
                            module->path + "' expected '" +
                            typeIt->second->toString() + "', got '" +
                            valueTypeName(it->second) + "'.");
                    }

                    m_stack.pop();
                    m_stack.push(it->second);
                    break;
                }

                if (receiver.isArray() || receiver.isDict() ||
                    receiver.isSet()) {
                    auto bound = gcAlloc<NativeBoundMethodObject>();
                    bound->name = name;
                    bound->receiver = receiver;
                    m_stack.pop();
                    m_stack.push(Value(bound));
                    break;
                }

                if (!receiver.isInstance()) {
                    return runtimeError("Only instances have properties.");
                }

                auto instance = receiver.asInstance();
                auto it = instance->fields.find(name);
                if (it != instance->fields.end()) {
                    m_stack.pop();
                    m_stack.push(it->second);
                    break;
                }

                auto method = findMethodClosure(instance->klass, name);
                if (!method) {
                    return runtimeError("Undefined property '" + name + "'.");
                }

                auto bound = gcAlloc<BoundMethodObject>();
                bound->receiver = instance;
                bound->method = method;

                m_stack.pop();
                m_stack.push(Value(bound));
                break;
            }
            case OpCode::SET_PROPERTY: {
                const std::string& name = readNameConstant();
                Value value = m_stack.peek(0);
                Value receiver = m_stack.peek(1);
                if (!receiver.isInstance()) {
                    return runtimeError("Only instances have fields.");
                }

                auto instance = receiver.asInstance();
                auto fieldTypeIt = instance->klass->fieldTypes.find(name);
                if (fieldTypeIt == instance->klass->fieldTypes.end()) {
                    return runtimeError("Undefined field '" + name +
                                        "' on class '" + instance->klass->name +
                                        "'.");
                }

                if (!valueMatchesType(value, fieldTypeIt->second)) {
                    return runtimeError(
                        "Type error: field '" + name + "' on class '" +
                        instance->klass->name + "' expects '" +
                        fieldTypeIt->second->toString() + "', got '" +
                        valueTypeName(value) + "'.");
                }

                instance->fields[name] = value;

                m_stack.pop();
                m_stack.pop();
                m_stack.push(value);
                break;
            }
            case OpCode::CALL: {
                uint8_t argumentCount = readByte();
                Value callee = m_stack.peek(argumentCount);
                size_t calleeIndex =
                    m_stack.size() - static_cast<size_t>(argumentCount) - 1;

                if (callee.isClass()) {
                    if (argumentCount != 0) {
                        return runtimeError(
                            "Class '" + callee.asClass()->name +
                            "' expected 0 arguments but got " +
                            std::to_string(static_cast<int>(argumentCount)) +
                            ".");
                    }

                    auto instance = gcAlloc<InstanceObject>();
                    instance->klass = callee.asClass();
                    m_stack.setAt(calleeIndex, Value(instance));
                    break;
                }

                if (callee.isNative()) {
                    auto native = callee.asNative();
                    if (native->arity >= 0 && native->arity != argumentCount) {
                        return runtimeError(
                            "Native function '" + native->name + "' expected " +
                            std::to_string(native->arity) +
                            " arguments but got " +
                            std::to_string(static_cast<int>(argumentCount)) +
                            ".");
                    }

                    std::vector<Value> args;
                    args.reserve(argumentCount);
                    for (uint8_t i = 0; i < argumentCount; ++i) {
                        args.push_back(m_stack.peek(
                            static_cast<size_t>(argumentCount - 1 - i)));
                    }

                    Value result;
                    if (native->name == "clock") {
                        auto now = std::chrono::duration<double>(
                                       std::chrono::system_clock::now()
                                           .time_since_epoch())
                                       .count();
                        result = Value(now);
                    } else if (native->name == "sqrt") {
                        const Value& arg = args[0];
                        if (!arg.isAnyNumeric()) {
                            return runtimeError(
                                "Native function 'sqrt' expects a number.");
                        }

                        double number = 0.0;
                        valueToDouble(arg, number);
                        if (number < 0.0) {
                            return runtimeError(
                                "Native function 'sqrt' cannot take negative "
                                "numbers.");
                        }

                        result = Value(std::sqrt(number));
                    } else if (native->name == "len") {
                        const Value& arg = args[0];
                        if (arg.isString()) {
                            result = Value(
                                static_cast<int64_t>(arg.asString().length()));
                        } else if (arg.isArray()) {
                            result = Value(static_cast<int64_t>(
                                arg.asArray()->elements.size()));
                        } else if (arg.isDict()) {
                            result = Value(
                                static_cast<int64_t>(arg.asDict()->map.size()));
                        } else if (arg.isSet()) {
                            result = Value(static_cast<int64_t>(
                                arg.asSet()->elements.size()));
                        } else {
                            return runtimeError(
                                "Native function 'len' expects a string, "
                                "array, "
                                "dict, or set.");
                        }
                    } else if (native->name == "type") {
                        result = Value(valueTypeName(args[0]));
                    } else if (native->name == "str") {
                        result = Value(valueToString(args[0]));
                    } else if (native->name == "toString") {
                        result = Value(valueToString(args[0]));
                    } else if (native->name == "num") {
                        const Value& arg = args[0];
                        if (arg.isNumber()) {
                            result = arg;
                        } else if (arg.isSignedInt() || arg.isUnsignedInt()) {
                            double number = 0.0;
                            valueToDouble(arg, number);
                            result = Value(number);
                        } else if (arg.isString()) {
                            const std::string& text = arg.asString();
                            size_t parseIndex = 0;
                            try {
                                double number = std::stod(text, &parseIndex);
                                if (parseIndex != text.size()) {
                                    return runtimeError(
                                        "Native function 'num' expects a "
                                        "numeric string.");
                                }
                                result = Value(number);
                            } catch (const std::exception&) {
                                return runtimeError(
                                    "Native function 'num' expects a numeric "
                                    "string.");
                            }
                        } else {
                            return runtimeError(
                                "Native function 'num' expects a number or "
                                "string.");
                        }
                    } else if (native->name == "parseInt") {
                        const Value& arg = args[0];
                        if (!arg.isString()) {
                            return runtimeError(
                                "Native function 'parseInt' expects a string.");
                        }

                        size_t parseIndex = 0;
                        try {
                            int64_t parsed =
                                std::stoll(arg.asString(), &parseIndex, 10);
                            if (parseIndex != arg.asString().size()) {
                                return runtimeError(
                                    "Native function 'parseInt' expects an "
                                    "integer string.");
                            }
                            result = Value(parsed);
                        } catch (const std::exception&) {
                            return runtimeError(
                                "Native function 'parseInt' expects an integer "
                                "string.");
                        }
                    } else if (native->name == "parseUInt") {
                        const Value& arg = args[0];
                        if (!arg.isString()) {
                            return runtimeError(
                                "Native function 'parseUInt' expects a "
                                "string.");
                        }

                        size_t parseIndex = 0;
                        try {
                            uint64_t parsed =
                                std::stoull(arg.asString(), &parseIndex, 10);
                            if (parseIndex != arg.asString().size()) {
                                return runtimeError(
                                    "Native function 'parseUInt' expects an "
                                    "unsigned integer string.");
                            }
                            result = Value(parsed);
                        } catch (const std::exception&) {
                            return runtimeError(
                                "Native function 'parseUInt' expects an "
                                "unsigned integer string.");
                        }
                    } else if (native->name == "parseFloat") {
                        const Value& arg = args[0];
                        if (!arg.isString()) {
                            return runtimeError(
                                "Native function 'parseFloat' expects a "
                                "string.");
                        }

                        size_t parseIndex = 0;
                        try {
                            double parsed =
                                std::stod(arg.asString(), &parseIndex);
                            if (parseIndex != arg.asString().size()) {
                                return runtimeError(
                                    "Native function 'parseFloat' expects a "
                                    "numeric string.");
                            }
                            result = Value(parsed);
                        } catch (const std::exception&) {
                            return runtimeError(
                                "Native function 'parseFloat' expects a "
                                "numeric string.");
                        }
                    } else if (native->name == "abs") {
                        const Value& arg = args[0];
                        if (!arg.isAnyNumeric()) {
                            return runtimeError(
                                "Native function 'abs' expects a number.");
                        }

                        double number = 0.0;
                        valueToDouble(arg, number);
                        result = Value(std::fabs(number));
                    } else if (native->name == "floor") {
                        const Value& arg = args[0];
                        if (!arg.isAnyNumeric()) {
                            return runtimeError(
                                "Native function 'floor' expects a number.");
                        }

                        double number = 0.0;
                        valueToDouble(arg, number);
                        result = Value(std::floor(number));
                    } else if (native->name == "ceil") {
                        const Value& arg = args[0];
                        if (!arg.isAnyNumeric()) {
                            return runtimeError(
                                "Native function 'ceil' expects a number.");
                        }

                        double number = 0.0;
                        valueToDouble(arg, number);
                        result = Value(std::ceil(number));
                    } else if (native->name == "pow") {
                        double base = 0.0;
                        double exponent = 0.0;
                        if (!valueToDouble(args[0], base) ||
                            !valueToDouble(args[1], exponent)) {
                            return runtimeError(
                                "Native function 'pow' expects numeric "
                                "arguments.");
                        }

                        result = Value(std::pow(base, exponent));
                    } else if (native->name == "error") {
                        const Value& arg = args[0];
                        if (!arg.isString()) {
                            return runtimeError(
                                "Native function 'error' expects a string.");
                        }

                        return runtimeError(arg.asString());
                    } else if (native->name == "Set") {
                        auto set = gcAlloc<SetObject>();
                        for (const auto& arg : args) {
                            if (set->elementType->isAny()) {
                                set->elementType = inferRuntimeType(arg);
                            } else if (!valueMatchesType(arg,
                                                         set->elementType)) {
                                return runtimeError(
                                    "Native function 'Set' expects all "
                                    "elements to have a consistent type.");
                            }

                            if (!containsValue(set->elements, arg)) {
                                set->elements.push_back(arg);
                            }
                        }

                        result = Value(set);
                    } else {
                        return runtimeError("Unknown native function '" +
                                            native->name + "'.");
                    }

                    m_stack.popN(m_stack.size() - calleeIndex);
                    m_stack.push(result);
                    break;
                }

                if (callee.isNativeBound()) {
                    auto bound = callee.asNativeBound();

                    std::vector<Value> args;
                    args.reserve(argumentCount);
                    for (uint8_t i = 0; i < argumentCount; ++i) {
                        args.push_back(m_stack.peek(
                            static_cast<size_t>(argumentCount - 1 - i)));
                    }

                    Value result;
                    const std::string& method = bound->name;
                    Value receiver = bound->receiver;

                    if (receiver.isArray()) {
                        auto array = receiver.asArray();

                        if (method == "push") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Array method 'push' expects 1 argument.");
                            }

                            if (array->elementType->isAny()) {
                                array->elementType = inferRuntimeType(args[0]);
                            } else if (!valueMatchesType(args[0],
                                                         array->elementType)) {
                                return runtimeError(
                                    "Array method 'push' expects value of "
                                    "type '" +
                                    array->elementType->toString() + "'.");
                            }

                            array->elements.push_back(args[0]);
                            result = Value(
                                static_cast<double>(array->elements.size()));
                        } else if (method == "pop") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'pop' expects 0 arguments.");
                            }

                            if (array->elements.empty()) {
                                return runtimeError(
                                    "Array method 'pop' called on empty "
                                    "array.");
                            }

                            result = array->elements.back();
                            array->elements.pop_back();
                        } else if (method == "size") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'size' expects 0 arguments.");
                            }

                            result = Value(
                                static_cast<double>(array->elements.size()));
                        } else if (method == "has") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Array method 'has' expects 1 argument.");
                            }

                            result =
                                Value(containsValue(array->elements, args[0]));
                        } else if (method == "insert") {
                            if (argumentCount != 2) {
                                return runtimeError(
                                    "Array method 'insert' expects 2 "
                                    "arguments.");
                            }

                            size_t index = 0;
                            if (!toArrayIndex(args[0], index)) {
                                return runtimeError(
                                    "Array method 'insert' expects a "
                                    "non-negative integer index.");
                            }

                            if (index > array->elements.size()) {
                                return runtimeError(
                                    "Array method 'insert' index out of "
                                    "bounds.");
                            }

                            if (array->elementType->isAny()) {
                                array->elementType = inferRuntimeType(args[1]);
                            } else if (!valueMatchesType(args[1],
                                                         array->elementType)) {
                                return runtimeError(
                                    "Array method 'insert' expects value of "
                                    "type '" +
                                    array->elementType->toString() + "'.");
                            }

                            array->elements.insert(array->elements.begin() +
                                                       static_cast<long>(index),
                                                   args[1]);
                            result = Value(args[1]);
                        } else if (method == "remove") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Array method 'remove' expects 1 "
                                    "argument.");
                            }

                            size_t index = 0;
                            if (!toArrayIndex(args[0], index)) {
                                return runtimeError(
                                    "Array method 'remove' expects a "
                                    "non-negative integer index.");
                            }

                            if (index >= array->elements.size()) {
                                return runtimeError(
                                    "Array method 'remove' index out of "
                                    "bounds.");
                            }

                            result = array->elements[index];
                            array->elements.erase(array->elements.begin() +
                                                  static_cast<long>(index));
                        } else if (method == "clear") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'clear' expects 0 "
                                    "arguments.");
                            }

                            double removed =
                                static_cast<double>(array->elements.size());
                            array->elements.clear();
                            result = Value(removed);
                        } else if (method == "isEmpty") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'isEmpty' expects 0 "
                                    "arguments.");
                            }

                            result = Value(array->elements.empty());
                        } else if (method == "first") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'first' expects 0 "
                                    "arguments.");
                            }

                            if (array->elements.empty()) {
                                return runtimeError(
                                    "Array method 'first' called on empty "
                                    "array.");
                            }

                            result = array->elements.front();
                        } else if (method == "last") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'last' expects 0 arguments.");
                            }

                            if (array->elements.empty()) {
                                return runtimeError(
                                    "Array method 'last' called on empty "
                                    "array.");
                            }

                            result = array->elements.back();
                        } else {
                            return runtimeError("Undefined array method '" +
                                                method + "'.");
                        }
                    } else if (receiver.isDict()) {
                        auto dict = receiver.asDict();

                        if (method == "get") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Dict method 'get' expects 1 argument.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            auto it = dict->map.find(key);
                            if (it == dict->map.end()) {
                                return runtimeError(
                                    "Dict method 'get' key not found.");
                            }

                            result = it->second;
                        } else if (method == "set") {
                            if (argumentCount != 2) {
                                return runtimeError(
                                    "Dict method 'set' expects 2 arguments.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            if (dict->keyType->isAny()) {
                                dict->keyType = inferRuntimeType(args[0]);
                            } else if (!valueMatchesType(args[0],
                                                         dict->keyType)) {
                                return runtimeError(
                                    "Dict method 'set' key expects type '" +
                                    dict->keyType->toString() + "'.");
                            }

                            if (dict->valueType->isAny()) {
                                dict->valueType = inferRuntimeType(args[1]);
                            } else if (!valueMatchesType(args[1],
                                                         dict->valueType)) {
                                return runtimeError(
                                    "Dict method 'set' value expects type '" +
                                    dict->valueType->toString() + "'.");
                            }

                            dict->map[key] = args[1];
                            result = args[1];
                        } else if (method == "has") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Dict method 'has' expects 1 argument.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            result =
                                Value(dict->map.find(key) != dict->map.end());
                        } else if (method == "keys") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'keys' expects 0 arguments.");
                            }

                            auto keys = gcAlloc<ArrayObject>();
                            std::vector<std::string> orderedKeys;
                            orderedKeys.reserve(dict->map.size());
                            for (const auto& entry : dict->map) {
                                orderedKeys.push_back(entry.first);
                            }
                            std::sort(orderedKeys.begin(), orderedKeys.end());

                            for (const auto& key : orderedKeys) {
                                keys->elements.push_back(Value(key));
                            }
                            keys->elementType = TypeInfo::makeStr();
                            result = Value(keys);
                        } else if (method == "values") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'values' expects 0 "
                                    "arguments.");
                            }

                            auto values = gcAlloc<ArrayObject>();
                            std::vector<std::string> orderedKeys;
                            orderedKeys.reserve(dict->map.size());
                            for (const auto& entry : dict->map) {
                                orderedKeys.push_back(entry.first);
                            }
                            std::sort(orderedKeys.begin(), orderedKeys.end());

                            for (const auto& key : orderedKeys) {
                                values->elements.push_back(dict->map.at(key));
                            }
                            values->elementType = dict->valueType;
                            result = Value(values);
                        } else if (method == "size") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'size' expects 0 arguments.");
                            }

                            result =
                                Value(static_cast<double>(dict->map.size()));
                        } else if (method == "remove") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Dict method 'remove' expects 1 argument.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            auto it = dict->map.find(key);
                            if (it == dict->map.end()) {
                                return runtimeError(
                                    "Dict method 'remove' key not found.");
                            }

                            result = it->second;
                            dict->map.erase(it);
                        } else if (method == "clear") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'clear' expects 0 arguments.");
                            }

                            double removed =
                                static_cast<double>(dict->map.size());
                            dict->map.clear();
                            result = Value(removed);
                        } else if (method == "isEmpty") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'isEmpty' expects 0 "
                                    "arguments.");
                            }

                            result = Value(dict->map.empty());
                        } else if (method == "getOr") {
                            if (argumentCount != 2) {
                                return runtimeError(
                                    "Dict method 'getOr' expects 2 arguments.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            auto it = dict->map.find(key);
                            result =
                                (it != dict->map.end()) ? it->second : args[1];
                        } else {
                            return runtimeError("Undefined dict method '" +
                                                method + "'.");
                        }
                    } else if (receiver.isSet()) {
                        auto set = receiver.asSet();

                        if (method == "add") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'add' expects 1 argument.");
                            }

                            if (set->elementType->isAny()) {
                                set->elementType = inferRuntimeType(args[0]);
                            } else if (!valueMatchesType(args[0],
                                                         set->elementType)) {
                                return runtimeError(
                                    "Set method 'add' expects value of type "
                                    "'" +
                                    set->elementType->toString() + "'.");
                            }

                            bool inserted = false;
                            if (!containsValue(set->elements, args[0])) {
                                set->elements.push_back(args[0]);
                                inserted = true;
                            }
                            result = Value(inserted);
                        } else if (method == "has") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'has' expects 1 argument.");
                            }

                            result =
                                Value(containsValue(set->elements, args[0]));
                        } else if (method == "remove") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'remove' expects 1 argument.");
                            }

                            bool removed = false;
                            for (size_t i = 0; i < set->elements.size(); ++i) {
                                if (set->elements[i] == args[0]) {
                                    set->elements.erase(set->elements.begin() +
                                                        static_cast<long>(i));
                                    removed = true;
                                    break;
                                }
                            }
                            result = Value(removed);
                        } else if (method == "size") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Set method 'size' expects 0 arguments.");
                            }

                            result = Value(
                                static_cast<double>(set->elements.size()));
                        } else if (method == "toArray") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Set method 'toArray' expects 0 "
                                    "arguments.");
                            }

                            auto array = gcAlloc<ArrayObject>();
                            array->elements = set->elements;
                            array->elementType = set->elementType;
                            result = Value(array);
                        } else if (method == "clear") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Set method 'clear' expects 0 arguments.");
                            }

                            double removed =
                                static_cast<double>(set->elements.size());
                            set->elements.clear();
                            result = Value(removed);
                        } else if (method == "isEmpty") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Set method 'isEmpty' expects 0 "
                                    "arguments.");
                            }

                            result = Value(set->elements.empty());
                        } else if (method == "union") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'union' expects 1 argument.");
                            }
                            if (!args[0].isSet()) {
                                return runtimeError(
                                    "Set method 'union' expects a set "
                                    "argument.");
                            }

                            auto out = gcAlloc<SetObject>();
                            out->elements = set->elements;
                            out->elementType = set->elementType;
                            auto rhs = args[0].asSet();

                            if (!set->elementType->isAny() &&
                                !rhs->elementType->isAny() &&
                                !isAssignable(rhs->elementType,
                                              set->elementType) &&
                                !isAssignable(set->elementType,
                                              rhs->elementType)) {
                                return runtimeError(
                                    "Set method 'union' requires compatible "
                                    "element types.");
                            }

                            for (const auto& element : rhs->elements) {
                                if (!containsValue(out->elements, element)) {
                                    out->elements.push_back(element);
                                }
                            }
                            result = Value(out);
                        } else if (method == "intersect") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'intersect' expects 1 "
                                    "argument.");
                            }
                            if (!args[0].isSet()) {
                                return runtimeError(
                                    "Set method 'intersect' expects a set "
                                    "argument.");
                            }

                            auto out = gcAlloc<SetObject>();
                            out->elementType = set->elementType;
                            auto rhs = args[0].asSet();

                            if (!set->elementType->isAny() &&
                                !rhs->elementType->isAny() &&
                                !isAssignable(rhs->elementType,
                                              set->elementType) &&
                                !isAssignable(set->elementType,
                                              rhs->elementType)) {
                                return runtimeError(
                                    "Set method 'intersect' requires "
                                    "compatible element types.");
                            }

                            for (const auto& element : set->elements) {
                                if (containsValue(rhs->elements, element) &&
                                    !containsValue(out->elements, element)) {
                                    out->elements.push_back(element);
                                }
                            }
                            result = Value(out);
                        } else if (method == "difference") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'difference' expects 1 "
                                    "argument.");
                            }
                            if (!args[0].isSet()) {
                                return runtimeError(
                                    "Set method 'difference' expects a set "
                                    "argument.");
                            }

                            auto out = gcAlloc<SetObject>();
                            out->elementType = set->elementType;
                            auto rhs = args[0].asSet();

                            if (!set->elementType->isAny() &&
                                !rhs->elementType->isAny() &&
                                !isAssignable(rhs->elementType,
                                              set->elementType) &&
                                !isAssignable(set->elementType,
                                              rhs->elementType)) {
                                return runtimeError(
                                    "Set method 'difference' requires "
                                    "compatible element types.");
                            }

                            for (const auto& element : set->elements) {
                                if (!containsValue(rhs->elements, element) &&
                                    !containsValue(out->elements, element)) {
                                    out->elements.push_back(element);
                                }
                            }
                            result = Value(out);
                        } else {
                            return runtimeError("Undefined set method '" +
                                                method + "'.");
                        }
                    } else {
                        return runtimeError(
                            "Native bound method receiver is invalid.");
                    }

                    m_stack.popN(m_stack.size() - calleeIndex);
                    m_stack.push(result);
                    break;
                }

                if (callee.isBoundMethod()) {
                    auto bound = callee.asBoundMethod();
                    Status status = callClosure(bound->method, argumentCount,
                                                bound->receiver);
                    if (status != Status::OK) {
                        return status;
                    }
                    break;
                }

                if (callee.isClosure()) {
                    Status status =
                        callClosure(callee.asClosure(), argumentCount);
                    if (status != Status::OK) {
                        return status;
                    }
                    break;
                }

                if (callee.isFunction()) {
                    auto closure = gcAlloc<ClosureObject>();
                    closure->function = callee.asFunction();
                    closure->upvalues = {};
                    Status status = callClosure(closure, argumentCount);
                    if (status != Status::OK) {
                        return status;
                    }
                    break;
                }

                if (!callee.isFunction()) {
                    return runtimeError(
                        "Can only call functions, classes, methods, and "
                        "natives.");
                }
                break;
            }
            case OpCode::CLOSURE: {
                Value constant = readConstant();
                if (!constant.isFunction()) {
                    return runtimeError("CLOSURE expects a function constant.");
                }

                auto function = constant.asFunction();
                auto closure = gcAlloc<ClosureObject>();
                closure->function = function;
                closure->upvalues.reserve(function->upvalueCount);
                m_stack.push(Value(closure));

                for (uint8_t i = 0; i < function->upvalueCount; ++i) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();

                    if (isLocal) {
                        size_t stackIndex = currentFrame().slotBase + index;
                        closure->upvalues.push_back(captureUpvalue(stackIndex));
                    } else {
                        closure->upvalues.push_back(
                            currentFrame().closure->upvalues[index]);
                    }
                }
                break;
            }
            case OpCode::CLOSE_UPVALUE: {
                closeUpvalues(m_stack.size() - 1);
                m_stack.pop();
                break;
            }
            case OpCode::BUILD_ARRAY: {
                uint8_t count = readByte();
                auto array = gcAlloc<ArrayObject>();
                array->elements.resize(count);

                auto mergeType = [&](const TypeRef& current,
                                     const TypeRef& next) -> TypeRef {
                    if (!current) {
                        return next;
                    }
                    if (!next) {
                        return current;
                    }
                    if (current->isAny() || next->isAny()) {
                        return TypeInfo::makeAny();
                    }
                    if (isAssignable(next, current)) {
                        return current;
                    }
                    if (isAssignable(current, next)) {
                        return next;
                    }
                    if (current->isNumeric() && next->isNumeric()) {
                        TypeRef promoted = numericPromotion(current, next);
                        return promoted ? promoted : TypeInfo::makeAny();
                    }
                    return nullptr;
                };

                TypeRef inferredElementType = nullptr;
                for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
                    Value element = m_stack.pop();
                    array->elements[static_cast<size_t>(i)] = element;

                    TypeRef elementType = inferRuntimeType(element);
                    TypeRef merged =
                        mergeType(inferredElementType, elementType);
                    if (!merged) {
                        return runtimeError(
                            "Array literal elements must have consistent "
                            "types.");
                    }
                    inferredElementType = merged;
                }

                array->elementType = inferredElementType ? inferredElementType
                                                         : TypeInfo::makeAny();

                m_stack.push(Value(array));
                break;
            }
            case OpCode::BUILD_DICT: {
                uint8_t pairCount = readByte();
                auto dict = gcAlloc<DictObject>();

                auto mergeType = [&](const TypeRef& current,
                                     const TypeRef& next) -> TypeRef {
                    if (!current) {
                        return next;
                    }
                    if (!next) {
                        return current;
                    }
                    if (current->isAny() || next->isAny()) {
                        return TypeInfo::makeAny();
                    }
                    if (isAssignable(next, current)) {
                        return current;
                    }
                    if (isAssignable(current, next)) {
                        return next;
                    }
                    if (current->isNumeric() && next->isNumeric()) {
                        TypeRef promoted = numericPromotion(current, next);
                        return promoted ? promoted : TypeInfo::makeAny();
                    }
                    return nullptr;
                };

                TypeRef keyType = nullptr;
                TypeRef valueType = nullptr;

                for (int i = 0; i < pairCount; ++i) {
                    Value value = m_stack.pop();
                    Value keyValue = m_stack.pop();

                    TypeRef mergedKeyType =
                        mergeType(keyType, inferRuntimeType(keyValue));
                    if (!mergedKeyType) {
                        return runtimeError(
                            "Dictionary literal keys must have consistent "
                            "types.");
                    }
                    keyType = mergedKeyType;

                    TypeRef mergedValueType =
                        mergeType(valueType, inferRuntimeType(value));
                    if (!mergedValueType) {
                        return runtimeError(
                            "Dictionary literal values must have consistent "
                            "types.");
                    }
                    valueType = mergedValueType;

                    std::string key;
                    if (!toDictKey(keyValue, key)) {
                        return runtimeError(
                            "Dictionary keys must be strings or numbers.");
                    }

                    dict->map[key] = value;
                }

                dict->keyType = keyType ? keyType : TypeInfo::makeAny();
                dict->valueType = valueType ? valueType : TypeInfo::makeAny();

                m_stack.push(Value(dict));
                break;
            }
            case OpCode::GET_INDEX: {
                Value indexValue = m_stack.pop();
                Value container = m_stack.pop();

                if (container.isArray()) {
                    size_t index = 0;
                    if (!toArrayIndex(indexValue, index)) {
                        return runtimeError(
                            "Array index must be a non-negative integer.");
                    }

                    auto array = container.asArray();
                    if (index >= array->elements.size()) {
                        return runtimeError("Array index out of bounds.");
                    }

                    m_stack.push(array->elements[index]);
                    break;
                }

                if (container.isDict()) {
                    std::string key;
                    if (!toDictKey(indexValue, key)) {
                        return runtimeError(
                            "Dictionary keys must be strings or numbers.");
                    }

                    auto dict = container.asDict();
                    auto it = dict->map.find(key);
                    if (it == dict->map.end()) {
                        return runtimeError("Dictionary key not found.");
                    }

                    m_stack.push(it->second);
                    break;
                }

                if (container.isSet()) {
                    auto set = container.asSet();
                    if (!valueMatchesType(indexValue, set->elementType)) {
                        return runtimeError(
                            "Type error: set lookup expects element type '" +
                            set->elementType->toString() + "', got '" +
                            valueTypeName(indexValue) + "'.");
                    }
                    m_stack.push(
                        Value(containsValue(set->elements, indexValue)));
                    break;
                }

                return runtimeError(
                    "Indexing is only supported on array, dict, and set.");
            }
            case OpCode::SET_INDEX: {
                Value value = m_stack.pop();
                Value indexValue = m_stack.pop();
                Value container = m_stack.pop();

                if (container.isArray()) {
                    size_t index = 0;
                    if (!toArrayIndex(indexValue, index)) {
                        return runtimeError(
                            "Array index must be a non-negative integer.");
                    }

                    auto array = container.asArray();
                    if (index >= array->elements.size()) {
                        return runtimeError("Array index out of bounds.");
                    }

                    if (!valueMatchesType(value, array->elementType)) {
                        return runtimeError(
                            "Type error: array assignment expects element "
                            "type '" +
                            array->elementType->toString() + "', got '" +
                            valueTypeName(value) + "'.");
                    }

                    array->elements[index] = value;
                    m_stack.push(value);
                    break;
                }

                if (container.isDict()) {
                    std::string key;
                    if (!toDictKey(indexValue, key)) {
                        return runtimeError(
                            "Dictionary keys must be strings or numbers.");
                    }

                    auto dict = container.asDict();

                    if (!valueMatchesType(indexValue, dict->keyType)) {
                        return runtimeError(
                            "Type error: dictionary key expects '" +
                            dict->keyType->toString() + "', got '" +
                            valueTypeName(indexValue) + "'.");
                    }

                    if (!valueMatchesType(value, dict->valueType)) {
                        return runtimeError(
                            "Type error: dictionary value expects '" +
                            dict->valueType->toString() + "', got '" +
                            valueTypeName(value) + "'.");
                    }

                    dict->map[key] = value;
                    m_stack.push(value);
                    break;
                }

                return runtimeError(
                    "Indexed assignment is only supported on array and dict.");
            }
            case OpCode::DUP: {
                m_stack.push(m_stack.peek(0));
                break;
            }
            case OpCode::DUP2: {
                Value second = m_stack.peek(1);
                Value top = m_stack.peek(0);
                m_stack.push(second);
                m_stack.push(top);
                break;
            }
            case OpCode::ITER_INIT: {
                Value iterable = m_stack.pop();
                auto iterator = gcAlloc<IteratorObject>();

                if (iterable.isArray()) {
                    iterator->kind = IteratorObject::ARRAY_ITER;
                    iterator->array = iterable.asArray();
                } else if (iterable.isDict()) {
                    iterator->kind = IteratorObject::DICT_ITER;
                    iterator->dict = iterable.asDict();

                    iterator->dictKeys.reserve(iterator->dict->map.size());
                    for (const auto& entry : iterator->dict->map) {
                        iterator->dictKeys.push_back(entry.first);
                    }
                    std::sort(iterator->dictKeys.begin(),
                              iterator->dictKeys.end());
                } else if (iterable.isSet()) {
                    iterator->kind = IteratorObject::SET_ITER;
                    iterator->set = iterable.asSet();
                } else {
                    return runtimeError(
                        "Foreach expects an iterable (array, dict, or set).");
                }

                m_stack.push(Value(iterator));
                break;
            }
            case OpCode::ITER_HAS_NEXT: {
                Value iteratorValue = m_stack.pop();
                if (!iteratorValue.isIterator()) {
                    return runtimeError("Internal error: iterator expected.");
                }

                auto iterator = iteratorValue.asIterator();
                bool hasNext = false;

                switch (iterator->kind) {
                    case IteratorObject::ARRAY_ITER:
                        hasNext =
                            iterator->array &&
                            iterator->index < iterator->array->elements.size();
                        break;
                    case IteratorObject::DICT_ITER:
                        hasNext = iterator->dict &&
                                  iterator->index < iterator->dictKeys.size();
                        break;
                    case IteratorObject::SET_ITER:
                        hasNext =
                            iterator->set &&
                            iterator->index < iterator->set->elements.size();
                        break;
                }

                m_stack.push(Value(hasNext));
                break;
            }
            case OpCode::ITER_NEXT: {
                Value iteratorValue = m_stack.pop();
                if (!iteratorValue.isIterator()) {
                    return runtimeError("Internal error: iterator expected.");
                }

                auto iterator = iteratorValue.asIterator();
                Value nextValue;

                switch (iterator->kind) {
                    case IteratorObject::ARRAY_ITER:
                        if (!iterator->array ||
                            iterator->index >=
                                iterator->array->elements.size()) {
                            return runtimeError(
                                "Foreach iterator exhausted unexpectedly.");
                        }
                        nextValue =
                            iterator->array->elements[iterator->index++];
                        break;
                    case IteratorObject::DICT_ITER:
                        if (!iterator->dict ||
                            iterator->index >= iterator->dictKeys.size()) {
                            return runtimeError(
                                "Foreach iterator exhausted unexpectedly.");
                        }
                        nextValue =
                            Value(iterator->dictKeys[iterator->index++]);
                        break;
                    case IteratorObject::SET_ITER:
                        if (!iterator->set ||
                            iterator->index >= iterator->set->elements.size()) {
                            return runtimeError(
                                "Foreach iterator exhausted unexpectedly.");
                        }
                        nextValue = iterator->set->elements[iterator->index++];
                        break;
                }

                m_stack.push(nextValue);
                break;
            }
            case OpCode::IMPORT_MODULE: {
                const std::string& path = readConstant().asString();

                auto cached = m_moduleCache.find(path);
                if (cached != m_moduleCache.end()) {
                    m_stack.push(Value(cached->second));
                    break;
                }

                if (m_importStack.find(path) != m_importStack.end()) {
                    return runtimeError("Circular import detected: '" + path +
                                        "'.");
                }

                std::ifstream file(path);
                if (!file) {
                    return runtimeError("Failed to open module '" + path +
                                        "'.");
                }

                std::string source((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

                m_importStack.insert(path);

                Chunk importedChunk;
                if (!m_compiler.compile(source, importedChunk, path)) {
                    m_importStack.erase(path);
                    return Status::COMPILATION_ERROR;
                }

                auto* module = gcAlloc<ModuleObject>();
                module->path = path;

                auto savedGlobalNames = m_globalNames;
                auto savedGlobalTypes = m_globalTypes;
                auto savedGlobalValues = m_globalValues;
                auto savedGlobalDefined = m_globalDefined;
                ModuleObject* outerModule = m_currentModule;

                m_globalNames = m_compiler.globalNames();
                m_globalTypes = m_compiler.globalTypes();
                if (m_globalTypes.size() < m_globalNames.size()) {
                    m_globalTypes.resize(m_globalNames.size(),
                                         TypeInfo::makeAny());
                }
                m_globalValues.assign(m_globalNames.size(), Value());
                m_globalDefined.assign(m_globalNames.size(), false);
                for (size_t i = 0; i < m_globalNames.size(); ++i) {
                    auto nativeIt = m_nativeGlobals.find(m_globalNames[i]);
                    if (nativeIt != m_nativeGlobals.end()) {
                        m_globalValues[i] = nativeIt->second;
                        m_globalDefined[i] = true;
                    }
                }

                m_currentModule = module;

                auto function = gcAlloc<FunctionObject>();
                function->name = path;
                function->parameters = {};
                function->chunk =
                    std::make_unique<Chunk>(std::move(importedChunk));
                function->upvalueCount = 0;

                auto closure = gcAlloc<ClosureObject>();
                closure->function = function;
                closure->upvalues = {};

                m_stack.push(Value(closure));
                size_t callerFrameCount = m_frameCount;
                Status callStatus = callClosure(closure, 0);
                if (callStatus != Status::OK) {
                    m_stack.pop();
                    m_currentModule = outerModule;
                    m_globalNames = std::move(savedGlobalNames);
                    m_globalTypes = std::move(savedGlobalTypes);
                    m_globalValues = std::move(savedGlobalValues);
                    m_globalDefined = std::move(savedGlobalDefined);
                    m_importStack.erase(path);
                    return callStatus;
                }

                Value ignored;
                Status moduleStatus = run(false, ignored, callerFrameCount);
                if (moduleStatus != Status::OK) {
                    m_currentModule = outerModule;
                    m_globalNames = std::move(savedGlobalNames);
                    m_globalTypes = std::move(savedGlobalTypes);
                    m_globalValues = std::move(savedGlobalValues);
                    m_globalDefined = std::move(savedGlobalDefined);
                    m_importStack.erase(path);
                    return moduleStatus;
                }

                m_stack.pop();

                m_currentModule = outerModule;
                m_globalNames = std::move(savedGlobalNames);
                m_globalTypes = std::move(savedGlobalTypes);
                m_globalValues = std::move(savedGlobalValues);
                m_globalDefined = std::move(savedGlobalDefined);

                m_moduleCache[path] = module;
                m_importStack.erase(path);
                m_stack.push(Value(module));
                break;
            }
            case OpCode::EXPORT_NAME: {
                const std::string& name = readConstant().asString();
                if (m_currentModule != nullptr) {
                    Value value = m_stack.peek(0);
                    TypeRef declaredType = TypeInfo::makeAny();
                    for (size_t i = 0; i < m_globalNames.size(); ++i) {
                        if (m_globalNames[i] == name) {
                            if (i < m_globalTypes.size() && m_globalTypes[i]) {
                                declaredType = m_globalTypes[i];
                            }
                            break;
                        }
                    }

                    if (declaredType && !declaredType->isAny() &&
                        !valueMatchesType(value, declaredType)) {
                        return runtimeError(
                            "Type error: cannot export '" + name + "' as '" +
                            declaredType->toString() + "' from module '" +
                            m_currentModule->path + "'; got '" +
                            valueTypeName(value) + "'.");
                    }

                    m_currentModule->exports[name] = value;
                    m_currentModule->exportTypes[name] = declaredType;
                }
                break;
            }
            case OpCode::JUMP: {
                uint16_t offset = readShort();
                currentFrame().ip += offset;
                break;
            }
            case OpCode::JUMP_IF_FALSE: {
                uint16_t offset = readShort();
                Value condition = m_stack.peek(0);
                if (isFalsey(condition)) {
                    currentFrame().ip += offset;
                }
                break;
            }
            case OpCode::LOOP: {
                uint16_t offset = readShort();
                currentFrame().ip -= offset;
                break;
            }
            case OpCode::SHIFT_LEFT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                int64_t shift = 0;
                if (!valueToSignedInt(b, shift)) {
                    return runtimeError("Operands must be numbers for '<<'.");
                }

                uint32_t amount = static_cast<uint32_t>(shift) & 63u;
                if (a.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() << amount));
                } else {
                    int64_t lhs = 0;
                    if (!valueToSignedInt(a, lhs)) {
                        return runtimeError(
                            "Operands must be numbers for '<<'.");
                    }
                    m_stack.push(Value(static_cast<int64_t>(
                        static_cast<uint64_t>(lhs) << amount)));
                }
                break;
            }
            case OpCode::SHIFT_RIGHT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                int64_t shift = 0;
                if (!valueToSignedInt(b, shift)) {
                    return runtimeError("Operands must be numbers for '>>'.");
                }

                uint32_t amount = static_cast<uint32_t>(shift) & 63u;
                if (a.isUnsignedInt()) {
                    m_stack.push(Value(a.asUnsignedInt() >> amount));
                } else {
                    int64_t lhs = 0;
                    if (!valueToSignedInt(a, lhs)) {
                        return runtimeError(
                            "Operands must be numbers for '>>'.");
                    }
                    m_stack.push(Value(lhs >> amount));
                }
                break;
            }
            case OpCode::BITWISE_AND: {
                uint64_t rhs = 0;
                uint64_t lhs = 0;
                if (!valueToUnsignedInt(m_stack.pop(), rhs) ||
                    !valueToUnsignedInt(m_stack.pop(), lhs)) {
                    return runtimeError("Operands must be integers for '&'.");
                }
                m_stack.push(Value(lhs & rhs));
                break;
            }
            case OpCode::BITWISE_OR: {
                uint64_t rhs = 0;
                uint64_t lhs = 0;
                if (!valueToUnsignedInt(m_stack.pop(), rhs) ||
                    !valueToUnsignedInt(m_stack.pop(), lhs)) {
                    return runtimeError("Operands must be integers for '|'.");
                }
                m_stack.push(Value(lhs | rhs));
                break;
            }
            case OpCode::BITWISE_XOR: {
                uint64_t rhs = 0;
                uint64_t lhs = 0;
                if (!valueToUnsignedInt(m_stack.pop(), rhs) ||
                    !valueToUnsignedInt(m_stack.pop(), lhs)) {
                    return runtimeError("Operands must be integers for '^'.");
                }
                m_stack.push(Value(lhs ^ rhs));
                break;
            }
            case OpCode::BITWISE_NOT: {
                uint64_t value = 0;
                if (!valueToUnsignedInt(m_stack.pop(), value)) {
                    return runtimeError("Operand must be an integer for '~'.");
                }
                m_stack.push(Value(~value));
                break;
            }
            case OpCode::WIDEN_INT: {
                readByte();
                break;
            }
            case OpCode::NARROW_INT: {
                uint8_t kind = readByte();
                Value value = m_stack.pop();

                switch (static_cast<TypeKind>(kind)) {
                    case TypeKind::I8: {
                        int64_t converted = 0;
                        if (!valueToSignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to i8.");
                        }
                        m_stack.push(Value(static_cast<int64_t>(
                            static_cast<int8_t>(converted))));
                        break;
                    }
                    case TypeKind::I16: {
                        int64_t converted = 0;
                        if (!valueToSignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to i16.");
                        }
                        m_stack.push(Value(static_cast<int64_t>(
                            static_cast<int16_t>(converted))));
                        break;
                    }
                    case TypeKind::I32: {
                        int64_t converted = 0;
                        if (!valueToSignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to i32.");
                        }
                        m_stack.push(Value(static_cast<int64_t>(
                            static_cast<int32_t>(converted))));
                        break;
                    }
                    case TypeKind::I64: {
                        int64_t converted = 0;
                        if (!valueToSignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to i64.");
                        }
                        m_stack.push(Value(converted));
                        break;
                    }
                    case TypeKind::U8: {
                        uint64_t converted = 0;
                        if (!valueToUnsignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to u8.");
                        }
                        m_stack.push(Value(static_cast<uint64_t>(
                            static_cast<uint8_t>(converted))));
                        break;
                    }
                    case TypeKind::U16: {
                        uint64_t converted = 0;
                        if (!valueToUnsignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to u16.");
                        }
                        m_stack.push(Value(static_cast<uint64_t>(
                            static_cast<uint16_t>(converted))));
                        break;
                    }
                    case TypeKind::U32: {
                        uint64_t converted = 0;
                        if (!valueToUnsignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to u32.");
                        }
                        m_stack.push(Value(static_cast<uint64_t>(
                            static_cast<uint32_t>(converted))));
                        break;
                    }
                    case TypeKind::U64:
                    case TypeKind::USIZE: {
                        uint64_t converted = 0;
                        if (!valueToUnsignedInt(value, converted)) {
                            return runtimeError("Cannot cast value to u64.");
                        }
                        m_stack.push(Value(converted));
                        break;
                    }
                    default:
                        m_stack.push(value);
                        break;
                }
                break;
            }
            case OpCode::INT_TO_FLOAT: {
                Value value = m_stack.pop();
                double converted = 0.0;
                if (!valueToDouble(value, converted)) {
                    return runtimeError("Cannot cast value to floating-point.");
                }
                m_stack.push(Value(converted));
                break;
            }
            case OpCode::FLOAT_TO_INT: {
                Value value = m_stack.pop();
                int64_t converted = 0;
                if (!valueToSignedInt(value, converted)) {
                    return runtimeError("Cannot cast value to integer.");
                }
                m_stack.push(Value(converted));
                break;
            }
            case OpCode::INT_TO_STR: {
                Value value = m_stack.pop();
                if (value.isSignedInt()) {
                    m_stack.push(Value(std::to_string(value.asSignedInt())));
                    break;
                }
                if (value.isUnsignedInt()) {
                    m_stack.push(Value(std::to_string(value.asUnsignedInt())));
                    break;
                }
                if (value.isNumber()) {
                    m_stack.push(Value(std::to_string(value.asNumber())));
                    break;
                }
                return runtimeError("Cannot cast value to str.");
            }
            case OpCode::CHECK_INSTANCE_TYPE: {
                const std::string& expectedClass = readNameConstant();
                Value value = m_stack.peek(0);

                if (!value.isInstance()) {
                    return runtimeError("Type error: expected instance of '" +
                                        expectedClass + "', got '" +
                                        valueTypeName(value) + "'.");
                }

                auto instance = value.asInstance();
                if (!isInstanceOfClass(instance, expectedClass)) {
                    std::string actualClass = (instance && instance->klass)
                                                  ? instance->klass->name
                                                  : std::string("<unknown>");
                    return runtimeError("Type error: expected instance of '" +
                                        expectedClass + "', got '" +
                                        actualClass + "'.");
                }
                break;
            }
            case OpCode::INT_NEGATE: {
                int64_t value = requireSignedInt(m_stack.pop());
                m_stack.push(Value(wrapSignedSub(0, value)));
                break;
            }
        }
    }
}

Status VirtualMachine::interpret(std::string_view source, bool printReturnValue,
                                 bool traceEnabled, bool disassembleEnabled,
                                 const std::string& sourcePath,
                                 bool strictMode) {
    std::string_view compileSource = stripStrictDirectiveLine(source);
    Chunk chunk;
    m_stack.reset();
    m_frameCount = 0;
    m_activeFrame = nullptr;
    m_openUpvalues.clear();
    m_nativeGlobals.clear();
    m_moduleCache.clear();
    m_importStack.clear();
    m_currentModule = nullptr;
    m_compiler.setGC(&m_gc);
    m_compiler.setStrictMode(strictMode || hasStrictDirective(source));
    m_traceEnabled = traceEnabled;
    m_disassembleEnabled = disassembleEnabled;

    if (!m_compiler.compile(compileSource, chunk, sourcePath)) {
        return Status::COMPILATION_ERROR;
    }

    if (m_disassembleEnabled) {
        std::cout << "== disassembly ==" << std::endl;
        int offset = 0;
        while (offset < chunk.count()) {
            offset = chunk.disassembleInstruction(offset);
        }
        std::cout << "== end disassembly ==" << std::endl;
    }

    m_globalNames = m_compiler.globalNames();
    m_globalTypes = m_compiler.globalTypes();
    if (m_globalTypes.size() < m_globalNames.size()) {
        m_globalTypes.resize(m_globalNames.size(), TypeInfo::makeAny());
    }
    m_globalValues.assign(m_globalNames.size(), Value());
    m_globalDefined.assign(m_globalNames.size(), false);

    auto defineNative = [&](const std::string& name, int arity) {
        auto nativeFn = gcAlloc<NativeFunctionObject>();
        nativeFn->name = name;
        nativeFn->arity = arity;
        m_nativeGlobals[name] = Value(nativeFn);

        for (size_t i = 0; i < m_globalNames.size(); ++i) {
            if (m_globalNames[i] == name) {
                m_globalValues[i] = Value(nativeFn);
                m_globalDefined[i] = true;
                break;
            }
        }
    };

    for (const auto& descriptor : standardLibraryNatives()) {
        defineNative(descriptor.name, descriptor.arity);
    }

    m_frames[m_frameCount++] =
        CallFrame{&chunk, chunk.getBytes(), 0, 0, nullptr, nullptr};
    m_activeFrame = &m_frames[m_frameCount - 1];

    Value returnValue;
    return run(printReturnValue, returnValue, 0);
}
