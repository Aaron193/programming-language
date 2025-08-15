#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/*
 OpCode is a bytecode *instruction*
 eg: [CONST 2, CONST 3, ADD] -> 2 + 3 -> 5
*/
enum OpCode {
    RETURN,
    CONSTANT,
    NEGATE,
    ADD,
    SUB,
    MULT,
    DIV,
    GREATER_THAN,
    LESS_THAN,
    GREATER_EQUAL_THAN,
    LESS_EQUAL_THAN,
    SHIFT_LEFT,
    SHIFT_RIGHT,
};

typedef double Value;

class Chunk {
   private:
    // Dynamic array to hold our bytecode
    std::unique_ptr<std::vector<uint8_t>> bytes =
        std::make_unique<std::vector<uint8_t>>();

    // dynamic array to hold a lists of per-chunk constants
    std::unique_ptr<std::vector<Value>> constants =
        std::make_unique<std::vector<Value>>();

    // dynamic array to hold line numbers
    std::unique_ptr<std::vector<int>> lines =
        std::make_unique<std::vector<int>>();

    void disassemble(std::string label);
    int simpleInstruction(const std::string& label, int offset);
    int constantInstruction(const std::string& label, int offset);

   public:
    Chunk() = default;
    ~Chunk() = default;

    void write(uint8_t byte, int line);
    int addConstant(Value value);
    int disassembleInstruction(int offset);

    uint8_t* getBytes();
    Value* getConstants();
};
