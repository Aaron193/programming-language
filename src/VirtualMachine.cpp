#include "VirtualMachine.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

static bool isFalsey(const Value& value) {
    if (value.isNil()) return true;
    if (value.isBool()) return !value.asBool();
    return false;
}

static bool isNumberPair(const Value& lhs, const Value& rhs) {
    return lhs.isNumber() && rhs.isNumber();
}

static std::shared_ptr<ClosureObject> findMethodClosure(
    std::shared_ptr<ClassObject> klass, const std::string& name) {
    std::shared_ptr<ClassObject> current = klass;
    while (current) {
        auto it = current->methods.find(name);
        if (it != current->methods.end()) {
            return it->second;
        }
        current = current->superclass;
    }

    return nullptr;
}

std::shared_ptr<UpvalueObject> VirtualMachine::captureUpvalue(
    size_t stackIndex) {
    for (const auto& upvalue : m_openUpvalues) {
        if (!upvalue->isClosed && upvalue->stackIndex == stackIndex) {
            return upvalue;
        }
    }

    auto upvalue = std::make_shared<UpvalueObject>();
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

Status VirtualMachine::callClosure(std::shared_ptr<ClosureObject> closure,
                                   uint8_t argumentCount,
                                   std::shared_ptr<InstanceObject> receiver) {
    auto function = closure->function;
    if (function->parameters.size() != argumentCount) {
        std::cerr << "Runtime error: Function '" << function->name
                  << "' expected " << function->parameters.size()
                  << " arguments but got " << static_cast<int>(argumentCount)
                  << "." << std::endl;
        return Status::RUNTIME_ERROR;
    }

    size_t calleeIndex =
        m_stack.size() - static_cast<size_t>(argumentCount) - 1;

    m_frames.push_back(CallFrame{function->chunk.get(),
                                 function->chunk->getBytes(), calleeIndex + 1,
                                 calleeIndex, receiver, closure});
    return Status::OK;
}

Status VirtualMachine::run(bool printReturnValue, Value& returnValue) {
    while (true) {
        CallFrame& frame = currentFrame();
        auto runtimeError = [&](const std::string& message) {
            int offset =
                static_cast<int>(frame.ip - frame.chunk->getBytes()) - 1;
            if (offset < 0) {
                offset = 0;
            }

            std::cerr << "[line " << frame.chunk->lineAt(offset)
                      << "] Runtime error: " << message << std::endl;
            return Status::RUNTIME_ERROR;
        };

#ifdef VM_TRACE
        m_stack.print();
        frame.chunk->disassembleInstruction(
            static_cast<int>(frame.ip - frame.chunk->getBytes()));
#endif

        uint8_t instruction;
        switch (instruction = readByte()) {
            case OpCode::RETURN: {
                Value result = m_stack.pop();
                CallFrame finishedFrame = currentFrame();
                closeUpvalues(finishedFrame.slotBase);
                m_frames.pop_back();

                if (m_frames.empty()) {
                    returnValue = result;
                    if (printReturnValue) {
                        std::cout << "Return constant: " << result << std::endl;
                    }
                    return Status::OK;
                }

                m_stack.popN(m_stack.size() - finishedFrame.calleeIndex);
                m_stack.push(result);
                break;
            }
            case OpCode::CONSTANT: {
                Value val = readConstant();
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
                if (!value.isNumber()) {
                    return runtimeError(
                        "Operand must be a number for unary '-'.");
                }

                m_stack.push(Value(-value.asNumber()));
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

                if (a.isNumber() && b.isNumber()) {
                    m_stack.push(Value(a.asNumber() + b.asNumber()));
                    break;
                }

                if (a.isString() && b.isString()) {
                    m_stack.push(Value(a.asString() + b.asString()));
                    break;
                }

                std::cerr << "Runtime error: Operands must be two numbers or "
                             "two strings for '+'."
                          << std::endl;
                return runtimeError(
                    "Operands must be two numbers or two strings for '+'.");
            }
            case OpCode::SUB: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '-'."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(a.asNumber() - b.asNumber()));
                break;
            }
            case OpCode::MULT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '*'."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(a.asNumber() * b.asNumber()));
                break;
            }
            case OpCode::DIV: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '/'."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(a.asNumber() / b.asNumber()));
                break;
            }
            case OpCode::GREATER_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '>'."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(a.asNumber() > b.asNumber()));
                break;
            }
            case OpCode::LESS_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '<'."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(a.asNumber() < b.asNumber()));
                break;
            }
            case OpCode::GREATER_EQUAL_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '>='."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(a.asNumber() >= b.asNumber()));
                break;
            }
            case OpCode::LESS_EQUAL_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '<='."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(a.asNumber() <= b.asNumber()));
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
                std::string name = readNameConstant();
                Value value = m_stack.pop();
                m_globals[name] = value;
                break;
            }
            case OpCode::GET_GLOBAL: {
                std::string name = readNameConstant();
                auto it = m_globals.find(name);
                if (it == m_globals.end()) {
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                m_stack.push(it->second);
                break;
            }
            case OpCode::SET_GLOBAL: {
                std::string name = readNameConstant();
                auto it = m_globals.find(name);
                if (it == m_globals.end()) {
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                it->second = m_stack.peek(0);
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
                std::string name = readNameConstant();
                auto klass = std::make_shared<ClassObject>();
                klass->name = name;
                m_stack.push(Value(klass));
                break;
            }
            case OpCode::INHERIT: {
                Value superclassValue = m_stack.pop();
                Value subclassValue = m_stack.peek(0);

                if (!superclassValue.isClass() || !subclassValue.isClass()) {
                    std::cerr << "Runtime error: Inheritance requires classes."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                auto superclass = superclassValue.asClass();
                auto subclass = subclassValue.asClass();
                subclass->superclass = superclass;
                for (const auto& method : superclass->methods) {
                    if (subclass->methods.find(method.first) ==
                        subclass->methods.end()) {
                        subclass->methods[method.first] = method.second;
                    }
                }
                break;
            }
            case OpCode::METHOD: {
                std::string name = readNameConstant();
                Value method = m_stack.peek(0);
                Value klass = m_stack.peek(1);

                if (!klass.isClass() || !method.isClosure()) {
                    std::cerr << "Runtime error: Invalid method declaration."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                klass.asClass()->methods[name] = method.asClosure();
                m_stack.pop();
                break;
            }
            case OpCode::GET_THIS: {
                auto receiver = currentFrame().receiver;
                if (!receiver) {
                    std::cerr << "Runtime error: Cannot use 'this' outside of "
                                 "a method."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(receiver));
                break;
            }
            case OpCode::GET_SUPER: {
                std::string name = readNameConstant();
                auto receiver = currentFrame().receiver;
                if (!receiver || !receiver->klass ||
                    !receiver->klass->superclass) {
                    std::cerr << "Runtime error: Invalid super lookup."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                auto method =
                    findMethodClosure(receiver->klass->superclass, name);
                if (!method) {
                    std::cerr << "Runtime error: Undefined superclass method '"
                              << name << "'." << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                auto bound = std::make_shared<BoundMethodObject>();
                bound->receiver = receiver;
                bound->method = method;
                m_stack.push(Value(bound));
                break;
            }
            case OpCode::GET_PROPERTY: {
                std::string name = readNameConstant();
                Value receiver = m_stack.peek(0);
                if (!receiver.isInstance()) {
                    std::cerr
                        << "Runtime error: Only instances have properties."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
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
                    std::cerr << "Runtime error: Undefined property '" << name
                              << "'." << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                auto bound = std::make_shared<BoundMethodObject>();
                bound->receiver = instance;
                bound->method = method;

                m_stack.pop();
                m_stack.push(Value(bound));
                break;
            }
            case OpCode::SET_PROPERTY: {
                std::string name = readNameConstant();
                Value value = m_stack.peek(0);
                Value receiver = m_stack.peek(1);
                if (!receiver.isInstance()) {
                    std::cerr << "Runtime error: Only instances have fields."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                auto instance = receiver.asInstance();
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

                    auto instance = std::make_shared<InstanceObject>();
                    instance->klass = callee.asClass();
                    m_stack.setAt(calleeIndex, Value(instance));
                    break;
                }

                if (callee.isNative()) {
                    auto native = callee.asNative();
                    if (native->arity != argumentCount) {
                        return runtimeError(
                            "Native function '" + native->name + "' expected " +
                            std::to_string(static_cast<int>(native->arity)) +
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
                    } else {
                        result = Value();
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
                    auto closure = std::make_shared<ClosureObject>();
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
                auto closure = std::make_shared<ClosureObject>();
                closure->function = function;
                closure->upvalues.reserve(function->upvalueCount);

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

                m_stack.push(Value(closure));
                break;
            }
            case OpCode::CLOSE_UPVALUE: {
                closeUpvalues(m_stack.size() - 1);
                m_stack.pop();
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
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '<<'."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(Value(
                    static_cast<double>(static_cast<int>(a.asNumber())
                                        << static_cast<int>(b.asNumber()))));
                break;
            }
            case OpCode::SHIFT_RIGHT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    std::cerr
                        << "Runtime error: Operands must be numbers for '>>'."
                        << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(
                    Value(static_cast<double>(static_cast<int>(a.asNumber()) >>
                                              static_cast<int>(b.asNumber()))));
                break;
            }
        }
    }
}

Status VirtualMachine::interpret(std::string_view source,
                                 bool printReturnValue) {
    Chunk chunk;
    m_stack.reset();
    m_frames.clear();
    m_openUpvalues.clear();

    if (!m_compiler.compile(source, chunk)) {
        return Status::COMPILATION_ERROR;
    }

    auto clockFn = std::make_shared<NativeFunctionObject>();
    clockFn->name = "clock";
    clockFn->arity = 0;
    m_globals[clockFn->name] = Value(clockFn);

    m_frames.push_back(
        CallFrame{&chunk, chunk.getBytes(), 0, 0, nullptr, nullptr});

    Value returnValue;
    return run(printReturnValue, returnValue);
}
