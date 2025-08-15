#include "VirtualMachine.hpp"

#include <sys/types.h>

#include <iostream>

#define DEBUG

Status VirtualMachine::run() {
    while (true) {
#ifdef DEBUG
        this->stack.print();
        this->chunk->disassembleInstruction(
            (int)(this->ip - this->chunk->getBytes()));
#endif

        uint8_t instruction;
        switch (instruction = this->readByte()) {
            case OpCode::RETURN: {
                Value val = this->stack.pop();
                std::cout << "Return constant: " << val << std::endl;
                return Status::OK;
            }
            case OpCode::CONSTANT: {
                Value val = this->readConstant();
                this->stack.push(val);
                break;
            }
            case OpCode::NEGATE: {
                this->stack.push(-this->stack.pop());
                break;
            }
            case OpCode::ADD: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(a + b);
                break;
            }
            case OpCode::SUB: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(a - b);
                break;
            }
            case OpCode::MULT: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(a * b);
                break;
            }
            case OpCode::DIV: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(a / b);
                break;
            }
            case OpCode::GREATER_THAN: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(a > b);
                break;
            }
            case OpCode::LESS_THAN: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(a < b);
                break;
            }
            case OpCode::SHIFT_LEFT: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(static_cast<Value>(static_cast<int>(a)
                                                    << static_cast<int>(b)));
                break;
            }
            case OpCode::SHIFT_RIGHT: {
                Value b = this->stack.pop();
                Value a = this->stack.pop();
                this->stack.push(static_cast<Value>(static_cast<int>(a) >>
                                                    static_cast<int>(b)));
                break;
            }
        }
    }
}

Status VirtualMachine::interpret(std::string_view source) {
    this->compiler.compile(source);
    return this->run();
}
