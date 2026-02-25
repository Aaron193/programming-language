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

static int jumpInstruction(const std::string& label, int sign, int offset,
                           uint8_t highByte, uint8_t lowByte) {
    uint16_t jump = static_cast<uint16_t>((highByte << 8) | lowByte);
    std::cout << label << " " << offset << " -> " << offset + 3 + (sign * jump)
              << std::endl;
    return offset + 3;
}

static int byteInstruction(const std::string& label, int offset,
                           uint8_t value) {
    std::cout << label << " " << static_cast<int>(value) << std::endl;
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
        case OpCode::NIL:
            return simpleInstruction("NIL", offset);
        case OpCode::TRUE_LITERAL:
            return simpleInstruction("TRUE_LITERAL", offset);
        case OpCode::FALSE_LITERAL:
            return simpleInstruction("FALSE_LITERAL", offset);
        case OpCode::NEGATE:
            return simpleInstruction("NEGATE", offset);
        case OpCode::NOT:
            return simpleInstruction("NOT", offset);
        case OpCode::EQUAL_OP:
            return simpleInstruction("EQUAL", offset);
        case OpCode::NOT_EQUAL_OP:
            return simpleInstruction("NOT_EQUAL", offset);
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
        case OpCode::POP:
            return simpleInstruction("POP", offset);
        case OpCode::PRINT_OP:
            return simpleInstruction("PRINT", offset);
        case OpCode::DEFINE_GLOBAL:
            return constantInstruction("DEFINE_GLOBAL", offset);
        case OpCode::GET_GLOBAL:
            return constantInstruction("GET_GLOBAL", offset);
        case OpCode::SET_GLOBAL:
            return constantInstruction("SET_GLOBAL", offset);
        case OpCode::CALL:
            return byteInstruction("CALL", offset, m_bytes->at(offset + 1));
        case OpCode::JUMP:
            return jumpInstruction("JUMP", 1, offset, m_bytes->at(offset + 1),
                                   m_bytes->at(offset + 2));
        case OpCode::JUMP_IF_FALSE:
            return jumpInstruction("JUMP_IF_FALSE", 1, offset,
                                   m_bytes->at(offset + 1),
                                   m_bytes->at(offset + 2));
        case OpCode::LOOP:
            return jumpInstruction("LOOP", -1, offset, m_bytes->at(offset + 1),
                                   m_bytes->at(offset + 2));
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
