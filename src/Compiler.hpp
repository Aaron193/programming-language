#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Chunk.hpp"
#include "GC.hpp"
#include "Scanner.hpp"
#include "TypeInfo.hpp"

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
        TypeRef type;
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
        TypeRef returnType;
    };

    struct CompiledFunction {
        FunctionObject* function;
        std::vector<Upvalue> upvalues;
        std::vector<TypeRef> parameterTypes;
        TypeRef returnType;
    };

    struct ParseRule {
        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };

    struct ClassContext {
        bool hasSuperclass;
        ClassContext* enclosing;
        std::string className;
    };

    Chunk* m_chunk = nullptr;
    std::unique_ptr<Scanner> m_scanner;
    std::unique_ptr<Parser> m_parser;
    ClassContext* m_currentClass = nullptr;
    std::vector<FunctionContext> m_contexts;
    GC* m_gc = nullptr;
    std::unordered_map<std::string, uint8_t> m_globalSlots;
    std::unordered_set<std::string> m_classNames;
    std::unordered_map<std::string, TypeRef> m_functionSignatures;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classFieldTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classMethodSignatures;
    std::vector<TypeRef> m_globalTypes;
    std::vector<TypeRef> m_exprTypeStack;
    std::vector<std::string> m_globalNames;
    std::vector<std::string> m_exportedNames;
    std::string m_sourcePath;
    bool m_strictMode = false;
    bool m_hasBufferedToken = false;
    Token m_bufferedToken;

    void advance();
    const Token& peekNextToken();
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
    uint8_t globalSlot(const Token& name);
    void namedVariable(const Token& name, bool canAssign);
    uint8_t parseVariable(const std::string& message,
                          const TypeRef& declaredType = TypeInfo::makeAny());
    void defineVariable(uint8_t global);
    void beginScope();
    void endScope();
    void addLocal(const Token& name,
                  const TypeRef& declaredType = TypeInfo::makeAny());
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
    bool emitCompoundBinary(TokenType assignmentType,
                            const TypeRef& leftType = TypeInfo::makeAny(),
                            const TypeRef& rightType = TypeInfo::makeAny());
    TypeRef inferVariableType(const Token& name) const;
    void emitCoerceToType(const TypeRef& sourceType, const TypeRef& targetType);
    void emitCheckInstanceType(const TypeRef& targetType);
    uint8_t arithmeticOpcode(TokenType operatorType,
                             const TypeRef& numericType) const;
    void pushExprType(const TypeRef& type);
    TypeRef popExprType();
    TypeRef peekExprType() const;

    void expression();
    void declaration();
    void collectClassNames(std::string_view source);
    void collectFunctionSignatures(std::string_view source);
    bool resolveModuleExportTypes(
        const std::string& resolvedPath,
        std::unordered_map<std::string, TypeRef>& outExportTypes,
        std::string& outError);
    TypeRef tokenToType(const Token& token) const;
    void classDeclaration();
    void classMemberDeclaration();
    void typedClassMemberDeclaration();
    void methodDeclaration();
    void functionDeclaration();
    void importDeclaration();
    void exportDeclaration();
    void emitExportName(const Token& nameToken);
    void statement();
    void block();
    void ifStatement();
    void whileStatement();
    void forStatement();
    void printStatement();
    void returnStatement();
    void expressionStatement();
    void autoVarDeclaration();
    void typedVarDeclaration();
    bool isTypeToken(TokenType type) const;
    bool isCollectionTypeName(const Token& token) const;
    bool isTypedTypeAnnotationStart();
    bool isTypedVarDeclarationStart();
    bool parseTypeExpr();
    TypeRef parseTypeExprType();
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
    void castOperator(bool canAssign);
    void call(bool canAssign);
    void dot(bool canAssign);
    void subscript(bool canAssign);
    void andOperator(bool canAssign);
    void orOperator(bool canAssign);

    CompiledFunction compileFunction(
        const std::string& name, bool isMethod = false,
        const TypeRef& declaredReturnType = TypeInfo::makeAny());

   public:
    Compiler() = default;
    ~Compiler() = default;

    void setGC(GC* gc) { m_gc = gc; }
    void setSourcePath(const std::string& path) { m_sourcePath = path; }
    void setStrictMode(bool strictMode) { m_strictMode = strictMode; }
    const std::vector<std::string>& globalNames() const {
        return m_globalNames;
    }
    const std::vector<TypeRef>& globalTypes() const { return m_globalTypes; }
    const std::vector<std::string>& exportedNames() const {
        return m_exportedNames;
    }
    const std::unordered_map<std::string, TypeRef>& functionSignatures() const {
        return m_functionSignatures;
    }
    const std::unordered_map<std::string,
                             std::unordered_map<std::string, TypeRef>>&
    classFieldTypes() const {
        return m_classFieldTypes;
    }
    const std::unordered_map<std::string,
                             std::unordered_map<std::string, TypeRef>>&
    classMethodSignatures() const {
        return m_classMethodSignatures;
    }

    bool compile(std::string_view source, Chunk& chunk,
                 const std::string& sourcePath = "");
};