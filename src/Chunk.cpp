#include "Chunk.hpp"

#include <iostream>

void Chunk::disassemble(std::string label) {
    int offset = 0;
    while (offset < m_bytes->size()) {
        offset += disassembleInstruction(offset);
    }
}

int Chunk::simpleInstruction(const std::string& label, int offset) {
    std::cout << label << std::endl;
    return offset + 1;
}

int Chunk::constantInstruction(const std::string& label, int offset) {
    std::cout << label << " ";

    uint8_t index = m_bytes->at(offset + 1);
    Value val = m_constants->at(index);

    std::cout << val << std::endl;

    return offset + 2;
}

void Chunk::write(uint8_t byte, int line) {
    m_bytes->push_back(byte);
    m_lines->push_back(line);
}

int Chunk::addConstant(Value value) {
    m_constants->push_back(value);
    return m_constants->size() - 1;
}

int Chunk::disassembleInstruction(int offset) {
    std::cout << "LINE: " << m_lines->at(offset) << std::endl;

    uint8_t instruction = m_bytes->at(offset);
    switch (instruction) {
        case OpCode::RETURN:
            return simpleInstruction("RETURN", offset);
        case OpCode::CONSTANT:
            return constantInstruction("CONSTANT", offset);
        case OpCode::NEGATE:
            return simpleInstruction("NEGATE", offset);
        case OpCode::ADD:
            return simpleInstruction("ADD", offset);
        case OpCode::SUB:
            return simpleInstruction("SUB", offset);
        case OpCode::MULT:
            return simpleInstruction("MULT", offset);
        case OpCode::DIV:
            return simpleInstruction("DIV", offset);
        case OpCode::GREATER_THAN:
            return simpleInstruction("GREATER_THAN", offset);
        case OpCode::LESS_THAN:
            return simpleInstruction("LESS_THAN", offset);
        case OpCode::GREATER_EQUAL_THAN:
            return simpleInstruction("GREATER_EQUAL_THAN", offset);
        case OpCode::LESS_EQUAL_THAN:
            return simpleInstruction("LESS_EQUAL_THAN", offset);
        case OpCode::SHIFT_LEFT:
            return simpleInstruction("SHIFT_LEFT", offset);
        case OpCode::SHIFT_RIGHT:
            return simpleInstruction("SHIFT_RIGHT", offset);

        default:
            std::cout << "Invalid instruction opcode: " << instruction
                      << std::endl;
            return offset + 1;
    }
}
