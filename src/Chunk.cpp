#include "Chunk.hpp"

#include <iostream>

#include "GC.hpp"

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

static int closureInstruction(const std::string& label, int offset,
                              const std::vector<uint8_t>& bytes,
                              const std::vector<Value>& constants) {
    std::cout << label << " ";
    uint8_t index = bytes.at(offset + 1);
    Value val = constants.at(index);
    std::cout << val << std::endl;

    if (!val.isFunction()) {
        return offset + 2;
    }

    auto function = val.asFunction();
    int current = offset + 2;
    for (uint8_t i = 0; i < function->upvalueCount; ++i) {
        uint8_t isLocal = bytes.at(current++);
        uint8_t slot = bytes.at(current++);
        std::cout << "  | upvalue " << static_cast<int>(i) << " "
                  << (isLocal ? "local " : "upvalue ") << static_cast<int>(slot)
                  << std::endl;
    }

    return current;
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
        case OpCode::IADD:
            return simpleInstruction("IADD", offset);
        case OpCode::ISUB:
            return simpleInstruction("ISUB", offset);
        case OpCode::IMULT:
            return simpleInstruction("IMULT", offset);
        case OpCode::IDIV:
            return simpleInstruction("IDIV", offset);
        case OpCode::IMOD:
            return simpleInstruction("IMOD", offset);
        case OpCode::UADD:
            return simpleInstruction("UADD", offset);
        case OpCode::USUB:
            return simpleInstruction("USUB", offset);
        case OpCode::UMULT:
            return simpleInstruction("UMULT", offset);
        case OpCode::UDIV:
            return simpleInstruction("UDIV", offset);
        case OpCode::UMOD:
            return simpleInstruction("UMOD", offset);
        case OpCode::GREATER_THAN:
            return simpleInstruction("GREATER_THAN", offset);
        case OpCode::LESS_THAN:
            return simpleInstruction("LESS_THAN", offset);
        case OpCode::GREATER_EQUAL_THAN:
            return simpleInstruction("GREATER_EQUAL_THAN", offset);
        case OpCode::LESS_EQUAL_THAN:
            return simpleInstruction("LESS_EQUAL_THAN", offset);
        case OpCode::IGREATER:
            return simpleInstruction("IGREATER", offset);
        case OpCode::ILESS:
            return simpleInstruction("ILESS", offset);
        case OpCode::IGREATER_EQ:
            return simpleInstruction("IGREATER_EQ", offset);
        case OpCode::ILESS_EQ:
            return simpleInstruction("ILESS_EQ", offset);
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
        case OpCode::DEFINE_GLOBAL_SLOT:
            return byteInstruction("DEFINE_GLOBAL_SLOT", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::GET_GLOBAL_SLOT:
            return byteInstruction("GET_GLOBAL_SLOT", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::SET_GLOBAL_SLOT:
            return byteInstruction("SET_GLOBAL_SLOT", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::GET_LOCAL:
            return byteInstruction("GET_LOCAL", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::SET_LOCAL:
            return byteInstruction("SET_LOCAL", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::GET_UPVALUE:
            return byteInstruction("GET_UPVALUE", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::SET_UPVALUE:
            return byteInstruction("SET_UPVALUE", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::CLASS_OP:
            return constantInstruction("CLASS", offset);
        case OpCode::INHERIT:
            return simpleInstruction("INHERIT", offset);
        case OpCode::METHOD:
            return constantInstruction("METHOD", offset);
        case OpCode::GET_THIS:
            return simpleInstruction("GET_THIS", offset);
        case OpCode::GET_SUPER:
            return constantInstruction("GET_SUPER", offset);
        case OpCode::GET_PROPERTY:
            return constantInstruction("GET_PROPERTY", offset);
        case OpCode::SET_PROPERTY:
            return constantInstruction("SET_PROPERTY", offset);
        case OpCode::CALL:
            return byteInstruction("CALL", offset, m_bytes->at(offset + 1));
        case OpCode::CLOSURE:
            return closureInstruction("CLOSURE", offset, *m_bytes,
                                      *m_constants);
        case OpCode::CLOSE_UPVALUE:
            return simpleInstruction("CLOSE_UPVALUE", offset);
        case OpCode::BUILD_ARRAY:
            return byteInstruction("BUILD_ARRAY", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::BUILD_DICT:
            return byteInstruction("BUILD_DICT", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::GET_INDEX:
            return simpleInstruction("GET_INDEX", offset);
        case OpCode::SET_INDEX:
            return simpleInstruction("SET_INDEX", offset);
        case OpCode::DUP:
            return simpleInstruction("DUP", offset);
        case OpCode::DUP2:
            return simpleInstruction("DUP2", offset);
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
        case OpCode::BITWISE_AND:
            return simpleInstruction("BITWISE_AND", offset);
        case OpCode::BITWISE_OR:
            return simpleInstruction("BITWISE_OR", offset);
        case OpCode::BITWISE_XOR:
            return simpleInstruction("BITWISE_XOR", offset);
        case OpCode::BITWISE_NOT:
            return simpleInstruction("BITWISE_NOT", offset);
        case OpCode::WIDEN_INT:
            return byteInstruction("WIDEN_INT", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::NARROW_INT:
            return byteInstruction("NARROW_INT", offset,
                                   m_bytes->at(offset + 1));
        case OpCode::INT_TO_FLOAT:
            return simpleInstruction("INT_TO_FLOAT", offset);
        case OpCode::FLOAT_TO_INT:
            return simpleInstruction("FLOAT_TO_INT", offset);
        case OpCode::CHECK_INSTANCE_TYPE:
            return constantInstruction("CHECK_INSTANCE_TYPE", offset);
        case OpCode::INT_NEGATE:
            return simpleInstruction("INT_NEGATE", offset);
        case OpCode::ITER_INIT:
            return simpleInstruction("ITER_INIT", offset);
        case OpCode::ITER_HAS_NEXT:
            return simpleInstruction("ITER_HAS_NEXT", offset);
        case OpCode::ITER_NEXT:
            return simpleInstruction("ITER_NEXT", offset);
        case OpCode::IMPORT_MODULE:
            return constantInstruction("IMPORT_MODULE", offset);
        case OpCode::EXPORT_NAME:
            return constantInstruction("EXPORT_NAME", offset);

        default:
            std::cout << "Invalid instruction opcode: " << instruction
                      << std::endl;
            return offset + 1;
    }
}

void FunctionObject::trace(GC& gc) {
    if (!chunk) {
        return;
    }

    for (const auto& value : chunk->getConstantsRange()) {
        gc.markValue(value);
    }
}

void ClassObject::trace(GC& gc) {
    gc.markObject(superclass);
    for (const auto& [name, method] : methods) {
        (void)name;
        gc.markObject(method);
    }
}

void BoundMethodObject::trace(GC& gc) {
    gc.markObject(receiver);
    gc.markObject(method);
}

void NativeFunctionObject::trace(GC& gc) { (void)gc; }

void UpvalueObject::trace(GC& gc) {
    if (isClosed) {
        gc.markValue(closed);
    }
}

void ClosureObject::trace(GC& gc) {
    gc.markObject(function);
    for (auto* upvalue : upvalues) {
        gc.markObject(upvalue);
    }
}

void InstanceObject::trace(GC& gc) {
    gc.markObject(klass);
    for (const auto& [name, value] : fields) {
        (void)name;
        gc.markValue(value);
    }
}

void ArrayObject::trace(GC& gc) {
    for (const auto& value : elements) {
        gc.markValue(value);
    }
}

void DictObject::trace(GC& gc) {
    for (const auto& [key, value] : map) {
        (void)key;
        gc.markValue(value);
    }
}

void SetObject::trace(GC& gc) {
    for (const auto& value : elements) {
        gc.markValue(value);
    }
}

void IteratorObject::trace(GC& gc) {
    gc.markObject(array);
    gc.markObject(dict);
    gc.markObject(set);
}

void ModuleObject::trace(GC& gc) {
    for (const auto& [name, value] : exports) {
        (void)name;
        gc.markValue(value);
    }
}

void NativeBoundMethodObject::trace(GC& gc) { gc.markValue(receiver); }
