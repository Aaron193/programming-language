#include "VirtualMachine.hpp"

#include <sys/types.h>

#include <iostream>

#define DEBUG

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
            case OpCode::NEGATE: {
                m_stack.push(-m_stack.pop());
                break;
            }
            case OpCode::ADD: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(a + b);
                break;
            }
            case OpCode::SUB: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(a - b);
                break;
            }
            case OpCode::MULT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(a * b);
                break;
            }
            case OpCode::DIV: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(a / b);
                break;
            }
            case OpCode::GREATER_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(a > b);
                break;
            }
            case OpCode::LESS_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(a < b);
                break;
            }
            case OpCode::SHIFT_LEFT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(static_cast<Value>(static_cast<int>(a)
                                                << static_cast<int>(b)));
                break;
            }
            case OpCode::SHIFT_RIGHT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(static_cast<Value>(static_cast<int>(a) >>
                                                static_cast<int>(b)));
                break;
            }
        }
    }
}

Status VirtualMachine::interpret(std::string_view source) {
    m_compiler.compile(source);
    return run();
}
