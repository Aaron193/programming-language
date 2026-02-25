#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include "Chunk.hpp"
#include "Scanner.hpp"

struct Parser {
    Token current;
    Token previous;
    bool hadError = false;
    // TODO: since we have exceptions, we can use them instead of panic mode
    bool panicMode = false;
};

enum Precedence {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY,
};

class Compiler {
   private:
    using ParseFn = std::function<void()>;

    struct ParseRule {
        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };

    Chunk* m_chunk = nullptr;
    std::unique_ptr<Scanner> m_scanner;
    std::unique_ptr<Parser> m_parser;

    void advance();
    void errorAtCurrent(const std::string& message);
    void errorAt(const Token& token, const std::string& message);
    void consume(TokenType type, const std::string& message);

    Chunk* currentChunk() { return m_chunk; }
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitReturn();
    uint8_t makeConstant(Value value);
    void emitConstant(Value value);
    uint8_t identifierConstant(const Token& name);
    uint8_t parseVariable(const std::string& message);
    void defineVariable(uint8_t global);

    void expression();
    void declaration();
    void statement();
    void printStatement();
    void expressionStatement();
    void varDeclaration();
    void parsePrecedence(Precedence precedence);
    ParseRule getRule(TokenType type);

    void number();
    void variable();
    void literal();
    void stringLiteral();
    void grouping();
    void unary();
    void binary();

   public:
    Compiler() = default;
    ~Compiler() = default;

    bool compile(std::string_view source, Chunk& chunk);
};