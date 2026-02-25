#include "VirtualMachine.hpp"

#include <sys/types.h>

#include <iostream>
#include <string>

#define DEBUG

static bool isFalsey(const Value& value) {
    if (value.isNil()) return true;
    if (value.isBool()) return !value.asBool();
    return false;
}

static bool isNumberPair(const Value& lhs, const Value& rhs) {
    return lhs.isNumber() && rhs.isNumber();
}

Status VirtualMachine::run() {
    while (true) {
#ifdef DEBUG
        m_stack.print();
        m_chunk->disassembleInstruction((int)(m_ip - m_chunk->getBytes()));
#endif

        uint8_t instruction;
        switch (instruction = readByte()) {
            case OpCode::RETURN: {
                Value val = m_stack.pop();
                std::cout << "Return constant: " << val << std::endl;
                return Status::OK;
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
            case OpCode::JUMP: {
                uint16_t offset = readShort();
                m_ip += offset;
                break;
            }
            case OpCode::JUMP_IF_FALSE: {
                uint16_t offset = readShort();
                Value condition = m_stack.peek(0);
                if (isFalsey(condition)) {
                    m_ip += offset;
                }
                break;
            }
            case OpCode::LOOP: {
                uint16_t offset = readShort();
                m_ip -= offset;
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

Status VirtualMachine::interpret(std::string_view source) {
    Chunk chunk;

    if (!m_compiler.compile(source, chunk)) {
        return Status::COMPILATION_ERROR;
    }

    m_chunk = &chunk;
    m_ip = m_chunk->getBytes();

    return run();
}
