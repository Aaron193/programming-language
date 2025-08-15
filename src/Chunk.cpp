#include "Chunk.hpp"

#include <iostream>

void Chunk::disassemble(std::string label) {
    int offset = 0;
    while (offset < this->bytes->size()) {
        offset += this->disassembleInstruction(offset);
    }
}

int Chunk::simpleInstruction(const std::string& label, int offset) {
    std::cout << label << std::endl;
    return offset + 1;
}

int Chunk::constantInstruction(const std::string& label, int offset) {
    std::cout << label << " ";

    uint8_t index = this->bytes->at(offset + 1);
    Value val = this->constants->at(index);

    std::cout << val << std::endl;

    return offset + 2;
}

void Chunk::write(uint8_t byte, int line) {
    this->bytes->push_back(byte);
    this->lines->push_back(line);
}

int Chunk::addConstant(Value value) {
    this->constants->push_back(value);
    return this->constants->size() - 1;
}

int Chunk::disassembleInstruction(int offset) {
    std::cout << "LINE: " << this->lines->at(offset) << std::endl;

    uint8_t instruction = this->bytes->at(offset);
    switch (instruction) {
        case OpCode::RETURN:
            return this->simpleInstruction("RETURN", offset);
        case OpCode::CONSTANT:
            return this->constantInstruction("CONSTANT", offset);
        case OpCode::NEGATE:
            return this->simpleInstruction("NEGATE", offset);
        case OpCode::ADD:
            return this->simpleInstruction("ADD", offset);
        case OpCode::SUB:
            return this->simpleInstruction("SUB", offset);
        case OpCode::MULT:
            return this->simpleInstruction("MULT", offset);
        case OpCode::DIV:
            return this->simpleInstruction("DIV", offset);
        case OpCode::GREATER_THAN:
            return this->simpleInstruction("GREATER_THAN", offset);
        case OpCode::LESS_THAN:
            return this->simpleInstruction("LESS_THAN", offset);
        case OpCode::GREATER_EQUAL_THAN:
            return this->simpleInstruction("GREATER_EQUAL_THAN", offset);
        case OpCode::LESS_EQUAL_THAN:
            return this->simpleInstruction("LESS_EQUAL_THAN", offset);
        case OpCode::SHIFT_LEFT:
            return this->simpleInstruction("SHIFT_LEFT", offset);
        case OpCode::SHIFT_RIGHT:
            return this->simpleInstruction("SHIFT_RIGHT", offset);

        default:
            std::cout << "Invalid instruction opcode: " << instruction
                      << std::endl;
            return offset + 1;
    }
}

uint8_t* Chunk::getBytes() { return this->bytes->data(); }

Value* Chunk::getConstants() { return this->constants->data(); }
