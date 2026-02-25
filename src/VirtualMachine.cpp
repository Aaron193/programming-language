#include "VirtualMachine.hpp"

#include <iostream>
#include <string>

static bool isFalsey(const Value& value) {
    if (value.isNil()) return true;
    if (value.isBool()) return !value.asBool();
    return false;
}

static bool isNumberPair(const Value& lhs, const Value& rhs) {
    return lhs.isNumber() && rhs.isNumber();
}

Status VirtualMachine::callFunction(std::shared_ptr<FunctionObject> function,
                                    uint8_t argumentCount,
                                    std::shared_ptr<InstanceObject> receiver) {
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
                                 calleeIndex, receiver});
    return Status::OK;
}

Status VirtualMachine::run(bool printReturnValue, Value& returnValue) {
    while (true) {
        CallFrame& frame = currentFrame();

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
                    std::cerr << "Runtime error: Operand must be a number for "
                                 "unary '-'."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
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
                return Status::RUNTIME_ERROR;
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
                    std::cerr << "Runtime error: Undefined variable '" << name
                              << "'." << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                m_stack.push(it->second);
                break;
            }
            case OpCode::SET_GLOBAL: {
                std::string name = readNameConstant();
                auto it = m_globals.find(name);
                if (it == m_globals.end()) {
                    std::cerr << "Runtime error: Undefined variable '" << name
                              << "'." << std::endl;
                    return Status::RUNTIME_ERROR;
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
            case OpCode::CLASS_OP: {
                std::string name = readNameConstant();
                auto klass = std::make_shared<ClassObject>();
                klass->name = name;
                m_stack.push(Value(klass));
                break;
            }
            case OpCode::METHOD: {
                std::string name = readNameConstant();
                Value method = m_stack.peek(0);
                Value klass = m_stack.peek(1);

                if (!klass.isClass() || !method.isFunction()) {
                    std::cerr << "Runtime error: Invalid method declaration."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                klass.asClass()->methods[name] = method.asFunction();
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

                auto methodIt = instance->klass->methods.find(name);
                if (methodIt == instance->klass->methods.end()) {
                    std::cerr << "Runtime error: Undefined property '" << name
                              << "'." << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                auto bound = std::make_shared<BoundMethodObject>();
                bound->receiver = instance;
                bound->method = methodIt->second;

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

                if (callee.isClass()) {
                    if (argumentCount != 0) {
                        std::cerr << "Runtime error: Class '"
                                  << callee.asClass()->name
                                  << "' expected 0 arguments but got "
                                  << static_cast<int>(argumentCount) << "."
                                  << std::endl;
                        return Status::RUNTIME_ERROR;
                    }

                    size_t calleeIndex =
                        m_stack.size() - static_cast<size_t>(argumentCount) - 1;
                    auto instance = std::make_shared<InstanceObject>();
                    instance->klass = callee.asClass();
                    m_stack.setAt(calleeIndex, Value(instance));
                    break;
                }

                if (callee.isBoundMethod()) {
                    auto bound = callee.asBoundMethod();
                    Status status = callFunction(bound->method, argumentCount,
                                                 bound->receiver);
                    if (status != Status::OK) {
                        return status;
                    }
                    break;
                }

                if (!callee.isFunction()) {
                    std::cerr << "Runtime error: Can only call functions, "
                                 "classes, and methods."
                              << std::endl;
                    return Status::RUNTIME_ERROR;
                }

                Status status =
                    callFunction(callee.asFunction(), argumentCount);
                if (status != Status::OK) {
                    return status;
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

    if (!m_compiler.compile(source, chunk)) {
        return Status::COMPILATION_ERROR;
    }

    m_frames.push_back(CallFrame{&chunk, chunk.getBytes(), 0, 0, nullptr});

    Value returnValue;
    return run(printReturnValue, returnValue);
}
