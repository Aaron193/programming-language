#include "AstParser.hpp"

#include <utility>

#include "SyntaxRules.hpp"

namespace {

bool isTypeToken(TokenType type) {
    switch (type) {
        case TokenType::TYPE_I8:
        case TokenType::TYPE_I16:
        case TokenType::TYPE_I32:
        case TokenType::TYPE_I64:
        case TokenType::TYPE_U8:
        case TokenType::TYPE_U16:
        case TokenType::TYPE_U32:
        case TokenType::TYPE_U64:
        case TokenType::TYPE_USIZE:
        case TokenType::TYPE_F32:
        case TokenType::TYPE_F64:
        case TokenType::TYPE_BOOL:
        case TokenType::TYPE_STR:
        case TokenType::TYPE_FN:
        case TokenType::TYPE_VOID:
        case TokenType::TYPE_NULL_KW:
            return true;
        default:
            return false;
    }
}

}  // namespace

AstParser::AstParser(std::string_view source) : m_scanner(source) { advance(); }

bool AstParser::parseModule(AstModule& outModule) {
    outModule.declarations.clear();

    while (!check(TokenType::END_OF_FILE)) {
        if (match(TokenType::SEMI_COLON)) {
            continue;
        }

        if (match(TokenType::TYPE)) {
            if (!parseTopLevelTypeDeclaration(outModule)) {
                return false;
            }
            continue;
        }

        if (check(TokenType::TYPE_FN) &&
            peekToken().type() == TokenType::IDENTIFIER) {
            advance();
            if (!parseTopLevelFunctionDeclaration(outModule)) {
                return false;
            }
            continue;
        }

        if (check(TokenType::CONST) || check(TokenType::VAR)) {
            skipVariableDeclaration();
            continue;
        }

        skipStatement();
    }

    return !m_hadError;
}

void AstParser::advance() {
    m_previous = m_current;
    while (true) {
        if (!m_bufferedTokens.empty()) {
            m_current = m_bufferedTokens.front();
            m_bufferedTokens.pop_front();
        } else {
            m_current = m_scanner.nextToken();
        }
        if (m_current.type() != TokenType::ERROR) {
            break;
        }
        error();
    }
}

const Token& AstParser::peekToken(size_t offset) { return tokenAt(offset); }

const Token& AstParser::tokenAt(size_t offset) {
    while (m_bufferedTokens.size() < offset) {
        m_bufferedTokens.push_back(m_scanner.nextToken());
    }
    return m_bufferedTokens[offset - 1];
}

bool AstParser::check(TokenType type) const { return m_current.type() == type; }

bool AstParser::match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

bool AstParser::consume(TokenType type) {
    if (!check(type)) {
        error();
        return false;
    }
    advance();
    return true;
}

bool AstParser::hasLineBreakBeforeCurrent() const {
    return m_previous.line() != 0 && m_current.line() > m_previous.line();
}

std::string AstParser::tokenText(const Token& token) const {
    return std::string(token.start(), token.length());
}

void AstParser::error() { m_hadError = true; }

std::unique_ptr<AstTypeExpr> AstParser::parseTypeExpr() {
    std::unique_ptr<AstTypeExpr> typeExpr;

    if (check(TokenType::TYPE_FN)) {
        Token typeToken = m_current;
        advance();
        if (!consume(TokenType::OPEN_PAREN)) {
            return nullptr;
        }

        auto functionType = std::make_unique<AstTypeExpr>();
        functionType->kind = AstTypeKind::FUNCTION;
        functionType->token = typeToken;

        if (!check(TokenType::CLOSE_PAREN)) {
            while (true) {
                auto paramType = parseTypeExpr();
                if (!paramType) {
                    return nullptr;
                }
                functionType->paramTypes.push_back(std::move(paramType));

                if (!match(TokenType::COMMA)) {
                    break;
                }
            }
        }

        if (!consume(TokenType::CLOSE_PAREN)) {
            return nullptr;
        }

        functionType->returnType = parseTypeExpr();
        if (!functionType->returnType) {
            return nullptr;
        }

        typeExpr = std::move(functionType);
    } else if (isTypeToken(m_current.type()) &&
               m_current.type() != TokenType::TYPE_FN) {
        auto namedType = std::make_unique<AstTypeExpr>();
        namedType->kind = AstTypeKind::NAMED;
        namedType->token = m_current;
        advance();
        typeExpr = std::move(namedType);
    } else if (check(TokenType::IDENTIFIER)) {
        Token nameToken = m_current;
        std::string name = tokenText(nameToken);
        advance();

        if (isHandleTypeNameText(name)) {
            if (!consume(TokenType::LESS)) {
                return nullptr;
            }
            if (!check(TokenType::IDENTIFIER)) {
                error();
                return nullptr;
            }
            Token namespaceToken = m_current;
            advance();
            if (!consume(TokenType::COLON) || !check(TokenType::IDENTIFIER)) {
                return nullptr;
            }
            Token packageToken = m_current;
            advance();
            if (!consume(TokenType::COLON) || !check(TokenType::IDENTIFIER)) {
                return nullptr;
            }
            Token typeToken = m_current;
            advance();
            if (!consume(TokenType::GREATER)) {
                return nullptr;
            }

            auto handleType = std::make_unique<AstTypeExpr>();
            handleType->kind = AstTypeKind::NATIVE_HANDLE;
            handleType->token = nameToken;
            handleType->packageNamespace = tokenText(namespaceToken);
            handleType->packageName = tokenText(packageToken);
            handleType->nativeHandleTypeName = tokenText(typeToken);
            typeExpr = std::move(handleType);
        } else if (isCollectionTypeNameText(name)) {
            if (!consume(TokenType::LESS)) {
                return nullptr;
            }

            if (name == "Array" || name == "Set") {
                auto elementType = parseTypeExpr();
                if (!elementType || !consume(TokenType::GREATER)) {
                    return nullptr;
                }

                auto collectionType = std::make_unique<AstTypeExpr>();
                collectionType->kind = name == "Array" ? AstTypeKind::ARRAY
                                                        : AstTypeKind::SET;
                collectionType->token = nameToken;
                collectionType->elementType = std::move(elementType);
                typeExpr = std::move(collectionType);
            } else {
                auto keyType = parseTypeExpr();
                if (!keyType || !consume(TokenType::COMMA)) {
                    return nullptr;
                }
                auto valueType = parseTypeExpr();
                if (!valueType || !consume(TokenType::GREATER)) {
                    return nullptr;
                }

                auto dictType = std::make_unique<AstTypeExpr>();
                dictType->kind = AstTypeKind::DICT;
                dictType->token = nameToken;
                dictType->keyType = std::move(keyType);
                dictType->valueType = std::move(valueType);
                typeExpr = std::move(dictType);
            }
        } else {
            auto namedType = std::make_unique<AstTypeExpr>();
            namedType->kind = AstTypeKind::NAMED;
            namedType->token = nameToken;
            typeExpr = std::move(namedType);
        }
    } else {
        error();
        return nullptr;
    }

    while (match(TokenType::QUESTION)) {
        auto optionalType = std::make_unique<AstTypeExpr>();
        optionalType->kind = AstTypeKind::OPTIONAL;
        optionalType->token = typeExpr->token;
        optionalType->innerType = std::move(typeExpr);
        typeExpr = std::move(optionalType);
    }

    return typeExpr;
}

std::vector<AstParameter> AstParser::parseParameters() {
    std::vector<AstParameter> params;
    if (!consume(TokenType::OPEN_PAREN)) {
        return params;
    }

    if (!check(TokenType::CLOSE_PAREN)) {
        while (true) {
            if (!check(TokenType::IDENTIFIER)) {
                error();
                return {};
            }

            AstParameter param;
            param.name = m_current;
            advance();
            param.type = parseTypeExpr();
            if (!param.type) {
                return {};
            }
            params.push_back(std::move(param));

            if (!match(TokenType::COMMA)) {
                break;
            }
        }
    }

    if (!consume(TokenType::CLOSE_PAREN)) {
        return {};
    }

    return params;
}

bool AstParser::parseTopLevelTypeDeclaration(AstModule& outModule) {
    if (!check(TokenType::IDENTIFIER)) {
        error();
        return false;
    }

    Token nameToken = m_current;
    advance();

    if (!check(TokenType::STRUCT) && !check(TokenType::LESS) &&
        !check(TokenType::OPEN_CURLY)) {
        auto aliasedType = parseTypeExpr();
        if (!aliasedType) {
            return false;
        }
        outModule.declarations.push_back(
            AstTypeAliasDecl{nameToken, std::move(aliasedType)});
        match(TokenType::SEMI_COLON);
        return !m_hadError;
    }

    if (match(TokenType::STRUCT)) {
    }

    AstClassDecl classDecl;
    classDecl.name = nameToken;

    if (match(TokenType::LESS)) {
        if (!check(TokenType::IDENTIFIER)) {
            error();
            return false;
        }
        classDecl.superclass = m_current;
        advance();
    }

    if (!consume(TokenType::OPEN_CURLY)) {
        return false;
    }

    while (!check(TokenType::CLOSE_CURLY) && !check(TokenType::END_OF_FILE)) {
        if (match(TokenType::SEMI_COLON)) {
            continue;
        }
        if (!parseClassMember(classDecl)) {
            return false;
        }
    }

    if (!consume(TokenType::CLOSE_CURLY)) {
        return false;
    }

    outModule.declarations.push_back(std::move(classDecl));
    return !m_hadError;
}

bool AstParser::parseTopLevelFunctionDeclaration(AstModule& outModule) {
    if (!check(TokenType::IDENTIFIER)) {
        error();
        return false;
    }

    AstFunctionDecl functionDecl;
    functionDecl.name = m_current;
    advance();
    functionDecl.params = parseParameters();
    if (m_hadError) {
        return false;
    }

    if (!check(TokenType::OPEN_CURLY) && !check(TokenType::END_OF_FILE)) {
        functionDecl.returnType = parseTypeExpr();
        if (!functionDecl.returnType) {
            return false;
        }
    }

    if (!check(TokenType::OPEN_CURLY)) {
        error();
        return false;
    }
    skipBlock();

    outModule.declarations.push_back(std::move(functionDecl));
    return !m_hadError;
}

bool AstParser::parseClassMember(AstClassDecl& outClassDecl) {
    std::vector<int> annotatedOperators;

    while (match(TokenType::AT)) {
        if (!check(TokenType::IDENTIFIER)) {
            error();
            return false;
        }
        std::string annotationName = tokenText(m_current);
        advance();
        if (!consume(TokenType::OPEN_PAREN) || !check(TokenType::STRING)) {
            return false;
        }
        std::string literal = tokenText(m_current);
        advance();
        if (!consume(TokenType::CLOSE_PAREN)) {
            return false;
        }
        if (annotationName == "operator" && literal.size() >= 2) {
            int op = parseOperatorAnnotationToken(
                std::string_view(literal.data() + 1, literal.size() - 2));
            if (op != -1) {
                annotatedOperators.push_back(op);
            }
        }
    }

    if (match(TokenType::TYPE_FN)) {
        if (!check(TokenType::IDENTIFIER)) {
            error();
            return false;
        }

        AstMethodDecl methodDecl;
        methodDecl.name = m_current;
        methodDecl.annotatedOperators = std::move(annotatedOperators);
        advance();
        methodDecl.params = parseParameters();
        if (m_hadError) {
            return false;
        }

        if (!check(TokenType::OPEN_CURLY) && !check(TokenType::END_OF_FILE)) {
            methodDecl.returnType = parseTypeExpr();
            if (!methodDecl.returnType) {
                return false;
            }
        }

        if (!check(TokenType::OPEN_CURLY)) {
            error();
            return false;
        }
        skipBlock();

        outClassDecl.methods.push_back(std::move(methodDecl));
        return !m_hadError;
    }

    if (!check(TokenType::IDENTIFIER)) {
        error();
        return false;
    }

    AstFieldDecl fieldDecl;
    fieldDecl.name = m_current;
    advance();
    fieldDecl.type = parseTypeExpr();
    if (!fieldDecl.type) {
        return false;
    }
    match(TokenType::SEMI_COLON);
    outClassDecl.fields.push_back(std::move(fieldDecl));
    return !m_hadError;
}

void AstParser::skipBlock() {
    if (!match(TokenType::OPEN_CURLY)) {
        error();
        return;
    }

    int depth = 1;
    while (depth > 0 && !check(TokenType::END_OF_FILE)) {
        TokenType type = m_current.type();
        advance();
        if (type == TokenType::OPEN_CURLY) {
            depth++;
        } else if (type == TokenType::CLOSE_CURLY) {
            depth--;
        }
    }

    if (depth != 0) {
        error();
    }
}

void AstParser::skipParenthesized() {
    if (!match(TokenType::OPEN_PAREN)) {
        error();
        return;
    }

    int depth = 1;
    while (depth > 0 && !check(TokenType::END_OF_FILE)) {
        TokenType type = m_current.type();
        advance();
        if (type == TokenType::OPEN_PAREN) {
            depth++;
        } else if (type == TokenType::CLOSE_PAREN) {
            depth--;
        } else if (type == TokenType::OPEN_CURLY) {
            int blockDepth = 1;
            while (blockDepth > 0 && !check(TokenType::END_OF_FILE)) {
                TokenType nestedType = m_current.type();
                advance();
                if (nestedType == TokenType::OPEN_CURLY) {
                    blockDepth++;
                } else if (nestedType == TokenType::CLOSE_CURLY) {
                    blockDepth--;
                }
            }
        }
    }

    if (depth != 0) {
        error();
    }
}

void AstParser::skipStatement() {
    if (match(TokenType::PRINT)) {
        if (check(TokenType::OPEN_PAREN)) {
            skipParenthesized();
        } else {
            skipExpression();
        }
        match(TokenType::SEMI_COLON);
        return;
    }

    if (match(TokenType::IF)) {
        if (check(TokenType::OPEN_PAREN)) {
            skipParenthesized();
        }
        skipStatement();
        if (match(TokenType::ELSE)) {
            skipStatement();
        }
        return;
    }

    if (match(TokenType::WHILE)) {
        if (check(TokenType::OPEN_PAREN)) {
            skipParenthesized();
        }
        skipStatement();
        return;
    }

    if (match(TokenType::FOR)) {
        if (check(TokenType::OPEN_PAREN)) {
            skipParenthesized();
        }
        skipStatement();
        return;
    }

    if (match(TokenType::_RETURN)) {
        if (check(TokenType::SEMI_COLON) || check(TokenType::CLOSE_CURLY) ||
            check(TokenType::END_OF_FILE)) {
            match(TokenType::SEMI_COLON);
            return;
        }
        skipExpression();
        match(TokenType::SEMI_COLON);
        return;
    }

    if (check(TokenType::OPEN_CURLY)) {
        skipBlock();
        return;
    }

    skipExpression();
    match(TokenType::SEMI_COLON);
}

void AstParser::skipExpression(TokenType primaryTerminator) {
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    bool consumedAny = false;

    while (!check(TokenType::END_OF_FILE)) {
        if (consumedAny && parenDepth == 0 && bracketDepth == 0 &&
            braceDepth == 0) {
            if (check(primaryTerminator) || check(TokenType::SEMI_COLON) ||
                check(TokenType::CLOSE_CURLY)) {
                return;
            }
            if (hasLineBreakBeforeCurrent() &&
                !isLineContinuationToken(m_current.type())) {
                return;
            }
        }

        TokenType type = m_current.type();
        advance();
        consumedAny = true;

        switch (type) {
            case TokenType::OPEN_PAREN:
                parenDepth++;
                break;
            case TokenType::CLOSE_PAREN:
                if (parenDepth == 0) {
                    return;
                }
                parenDepth--;
                break;
            case TokenType::OPEN_BRACKET:
                bracketDepth++;
                break;
            case TokenType::CLOSE_BRACKET:
                if (bracketDepth == 0) {
                    return;
                }
                bracketDepth--;
                break;
            case TokenType::OPEN_CURLY:
                braceDepth++;
                break;
            case TokenType::CLOSE_CURLY:
                if (braceDepth == 0) {
                    return;
                }
                braceDepth--;
                break;
            default:
                break;
        }
    }
}

void AstParser::skipVariableDeclaration() {
    advance();

    if (match(TokenType::OPEN_CURLY)) {
        int depth = 1;
        while (depth > 0 && !check(TokenType::END_OF_FILE)) {
            TokenType type = m_current.type();
            advance();
            if (type == TokenType::OPEN_CURLY) {
                depth++;
            } else if (type == TokenType::CLOSE_CURLY) {
                depth--;
            }
        }
        if (match(TokenType::EQUAL)) {
            skipExpression();
        }
        match(TokenType::SEMI_COLON);
        return;
    }

    if (!check(TokenType::IDENTIFIER)) {
        error();
        return;
    }
    advance();

    if (!check(TokenType::EQUAL)) {
        auto ignoredType = parseTypeExpr();
        if (!ignoredType) {
            return;
        }
    }

    if (!consume(TokenType::EQUAL)) {
        return;
    }
    skipExpression();
    match(TokenType::SEMI_COLON);
}
