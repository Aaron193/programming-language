#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Chunk.hpp"
#include "GC.hpp"
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
    PREC_SHIFT,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY,
};

class Compiler {
   private:
    using ParseFn = std::function<void(bool)>;

    struct Local {
        Token name;
        int depth;
        bool isCaptured;
    };

    struct Upvalue {
        uint8_t index;
        bool isLocal;
    };

    struct FunctionContext {
        std::vector<Local> locals;
        std::vector<Upvalue> upvalues;
        int scopeDepth;
        bool inFunction;
        bool inMethod;
    };

    struct CompiledFunction {
        FunctionObject* function;
        std::vector<Upvalue> upvalues;
    };

    struct ParseRule {
        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };

    struct ClassContext {
        bool hasSuperclass;
        ClassContext* enclosing;
    };

    Chunk* m_chunk = nullptr;
    std::unique_ptr<Scanner> m_scanner;
    std::unique_ptr<Parser> m_parser;
    ClassContext* m_currentClass = nullptr;
    std::vector<FunctionContext> m_contexts;
    GC* m_gc = nullptr;

    void advance();
    void synchronize();
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
    void namedVariable(const Token& name, bool canAssign);
    uint8_t parseVariable(const std::string& message);
    void defineVariable(uint8_t global);
    void beginScope();
    void endScope();
    void addLocal(const Token& name);
    int resolveLocal(const Token& name);
    int resolveLocalInContext(const Token& name, int contextIndex);
    int addUpvalue(int contextIndex, uint8_t index, bool isLocal);
    int resolveUpvalue(const Token& name, int contextIndex);
    void markInitialized();
    FunctionContext& currentContext() { return m_contexts.back(); }
    const FunctionContext& currentContext() const { return m_contexts.back(); }
    bool identifiersEqual(const Token& lhs, const Token& rhs) const;
    int emitJump(uint8_t instruction);
    void patchJump(int offset);
    void emitLoop(int loopStart);
    bool isAssignmentOperator(TokenType type) const;
    bool emitCompoundBinary(TokenType assignmentType);

    void expression();
    void declaration();
    void classDeclaration();
    void methodDeclaration();
    void functionDeclaration();
    void statement();
    void block();
    void ifStatement();
    void whileStatement();
    void forStatement();
    void printStatement();
    void returnStatement();
    void expressionStatement();
    void varDeclaration();
    void parsePrecedence(Precedence precedence);
    ParseRule getRule(TokenType type);

    void number(bool canAssign);
    void variable(bool canAssign);
    void thisExpression(bool canAssign);
    void superExpression(bool canAssign);
    void literal(bool canAssign);
    void stringLiteral(bool canAssign);
    void grouping(bool canAssign);
    void arrayLiteral(bool canAssign);
    void dictLiteral(bool canAssign);
    void unary(bool canAssign);
    void prefixUpdate(bool canAssign);
    void binary(bool canAssign);
    void call(bool canAssign);
    void dot(bool canAssign);
    void subscript(bool canAssign);
    void andOperator(bool canAssign);
    void orOperator(bool canAssign);

    CompiledFunction compileFunction(const std::string& name,
                                     bool isMethod = false);

   public:
    Compiler() = default;
    ~Compiler() = default;

    void setGC(GC* gc) { m_gc = gc; }

    bool compile(std::string_view source, Chunk& chunk);
};