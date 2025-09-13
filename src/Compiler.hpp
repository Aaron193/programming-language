#pragma once
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

class Compiler {
   private:
    std::unique_ptr<Scanner> m_scanner;
    std::unique_ptr<Parser> m_parser;

    void advance();
    void errorAtCurrent(const std::string& message);
    void errorAt(const Token& token, const std::string& message);
    void consume(TokenType type, const std::string& message);

   public:
    Compiler() = default;
    ~Compiler() = default;

    bool compile(std::string_view source, Chunk& chunk);
};