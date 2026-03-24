#include "AstParser.hpp"

#include <utility>

#include "NativePackage.hpp"
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

bool isAssignmentOperator(TokenType type) {
    switch (type) {
        case TokenType::EQUAL:
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::STAR_EQUAL:
        case TokenType::SLASH_EQUAL:
        case TokenType::AMPERSAND_EQUAL:
        case TokenType::CARET_EQUAL:
        case TokenType::PIPE_EQUAL:
        case TokenType::SHIFT_LEFT_EQUAL:
        case TokenType::SHIFT_RIGHT_EQUAL:
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
            return true;
        default:
            return false;
    }
}

bool isComparisonOperator(TokenType type) {
    switch (type) {
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
            return true;
        default:
            return false;
    }
}

bool isEqualityOperator(TokenType type) {
    return type == TokenType::EQUAL_EQUAL || type == TokenType::BANG_EQUAL;
}

}  // namespace

AstParser::AstParser(std::string_view source) : m_scanner(source) { advance(); }

AstNodeInfo AstParser::makeNodeInfo(const Token& token) {
    AstNodeInfo node;
    node.id = m_nextNodeId++;
    node.line = token.line();
    node.span = token.span();
    return node;
}

AstNodeInfo AstParser::makeNodeInfo(const SourceSpan& span) {
    AstNodeInfo node;
    node.id = m_nextNodeId++;
    node.line = span.line();
    node.span = span;
    return node;
}

bool AstParser::parseModule(AstModule& outModule) {
    outModule.items.clear();

    while (!check(TokenType::END_OF_FILE)) {
        AstItemPtr item = parseItem();
        if (!item) {
            return false;
        }
        outModule.items.push_back(std::move(item));
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
        reportScannerError(m_current);
    }
}

const Token& AstParser::peekToken(size_t offset) { return tokenAt(offset); }

const Token& AstParser::tokenAt(size_t offset) {
    if (offset == 0) {
        return m_current;
    }

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

bool AstParser::matchSameLine(TokenType type) {
    if (!check(type) || hasLineBreakBeforeCurrent()) {
        return false;
    }
    advance();
    return true;
}

bool AstParser::consume(TokenType type) {
    if (!check(type)) {
        if (check(TokenType::SEMI_COLON)) {
            rejectStraySemicolon();
            return false;
        }
        reportExpectedToken(type);
        if (!check(TokenType::END_OF_FILE)) {
            advance();
        }
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

std::string AstParser::tokenDescription(TokenType type) const {
    switch (type) {
        case TokenType::AT:
            return "'@'";
        case TokenType::BANG:
            return "'!'";
        case TokenType::BANG_EQUAL:
            return "'!='";
        case TokenType::TILDE:
            return "'~'";
        case TokenType::AMPERSAND:
            return "'&'";
        case TokenType::AMPERSAND_EQUAL:
            return "'&='";
        case TokenType::CARET:
            return "'^'";
        case TokenType::CARET_EQUAL:
            return "'^='";
        case TokenType::PIPE:
            return "'|'";
        case TokenType::PIPE_EQUAL:
            return "'|='";
        case TokenType::PLUS:
            return "'+'";
        case TokenType::PLUS_PLUS:
            return "'++'";
        case TokenType::PLUS_EQUAL:
            return "'+='";
        case TokenType::MINUS:
            return "'-'";
        case TokenType::MINUS_MINUS:
            return "'--'";
        case TokenType::MINUS_EQUAL:
            return "'-='";
        case TokenType::STAR:
            return "'*'";
        case TokenType::STAR_EQUAL:
            return "'*='";
        case TokenType::GREATER:
            return "'>'";
        case TokenType::GREATER_EQUAL:
            return "'>='";
        case TokenType::LESS:
            return "'<'";
        case TokenType::LESS_EQUAL:
            return "'<='";
        case TokenType::SHIFT_LEFT_TOKEN:
            return "'<<'";
        case TokenType::SHIFT_LEFT_EQUAL:
            return "'<<='";
        case TokenType::SHIFT_RIGHT_TOKEN:
            return "'>>'";
        case TokenType::SHIFT_RIGHT_EQUAL:
            return "'>>='";
        case TokenType::SLASH:
            return "'/'";
        case TokenType::SLASH_EQUAL:
            return "'/='";
        case TokenType::OPEN_PAREN:
            return "'('";
        case TokenType::CLOSE_PAREN:
            return "')'";
        case TokenType::OPEN_BRACKET:
            return "'['";
        case TokenType::CLOSE_BRACKET:
            return "']'";
        case TokenType::OPEN_CURLY:
            return "'{'";
        case TokenType::CLOSE_CURLY:
            return "'}'";
        case TokenType::SEMI_COLON:
            return "';'";
        case TokenType::COMMA:
            return "','";
        case TokenType::COLON:
            return "':'";
        case TokenType::DOT:
            return "'.'";
        case TokenType::QUESTION:
            return "'?'";
        case TokenType::EQUAL:
            return "'='";
        case TokenType::FAT_ARROW:
            return "'=>'";
        case TokenType::EQUAL_EQUAL:
            return "'=='";
        case TokenType::LOGICAL_AND:
            return "'&&'";
        case TokenType::LOGICAL_OR:
            return "'||'";
        case TokenType::IDENTIFIER:
            return "identifier";
        case TokenType::STRING:
            return "string literal";
        case TokenType::NUMBER:
            return "number";
        case TokenType::PRINT:
            return "'print'";
        case TokenType::VAR:
            return "'var'";
        case TokenType::CONST:
            return "'const'";
        case TokenType::TYPE:
            return "'type'";
        case TokenType::STRUCT:
            return "'struct'";
        case TokenType::SUPER:
            return "'super'";
        case TokenType::FOR:
            return "'for'";
        case TokenType::WHILE:
            return "'while'";
        case TokenType::IF:
            return "'if'";
        case TokenType::ELSE:
            return "'else'";
        case TokenType::TRUE:
            return "'true'";
        case TokenType::FALSE:
            return "'false'";
        case TokenType::_NULL:
            return "'null'";
        case TokenType::THIS:
            return "'this'";
        case TokenType::_RETURN:
            return "'return'";
        case TokenType::IMPORT:
            return "'import'";
        case TokenType::TYPE_I8:
            return "'i8'";
        case TokenType::TYPE_I16:
            return "'i16'";
        case TokenType::TYPE_I32:
            return "'i32'";
        case TokenType::TYPE_I64:
            return "'i64'";
        case TokenType::TYPE_U8:
            return "'u8'";
        case TokenType::TYPE_U16:
            return "'u16'";
        case TokenType::TYPE_U32:
            return "'u32'";
        case TokenType::TYPE_U64:
            return "'u64'";
        case TokenType::TYPE_USIZE:
            return "'usize'";
        case TokenType::TYPE_F32:
            return "'f32'";
        case TokenType::TYPE_F64:
            return "'f64'";
        case TokenType::TYPE_BOOL:
            return "'bool'";
        case TokenType::TYPE_STR:
            return "'str'";
        case TokenType::TYPE_FN:
            return "'fn'";
        case TokenType::TYPE_VOID:
            return "'void'";
        case TokenType::TYPE_NULL_KW:
            return "'null'";
        case TokenType::AS_KW:
            return "'as'";
        case TokenType::END_OF_FILE:
            return "end of file";
        case TokenType::ERROR:
            return "invalid token";
        default:
            return "token";
    }
}

std::string AstParser::tokenDisplayText(const Token& token) const {
    if (token.type() == TokenType::END_OF_FILE) {
        return "end of file";
    }
    if (token.type() == TokenType::ERROR) {
        return "invalid token";
    }

    const std::string text = tokenText(token);
    if (!text.empty()) {
        return "'" + text + "'";
    }

    return tokenDescription(token.type());
}

void AstParser::reportDiagnostic(const SourceSpan& span,
                                 const std::string& message,
                                 const std::string& code) {
    m_hadError = true;
    m_errors.push_back(ParseError{span, message, code});
}

void AstParser::reportScannerError(const Token& token) {
    const std::string message = tokenText(token);
    std::string code = "lex.invalid_token";
    if (message == "Unterminated string.") {
        code = "lex.unterminated_string";
    } else if (message == "Invalid numeric literal suffix.") {
        code = "lex.invalid_numeric_suffix";
    }

    reportDiagnostic(token.span(), message, code);
}

void AstParser::reportExpectedToken(TokenType expected) {
    reportDiagnostic(m_current.span(),
                     "Expected " + tokenDescription(expected) + " but found " +
                         tokenDisplayText(m_current) + ".",
                     "parse.expected_token");
}

void AstParser::reportUnexpectedToken(const Token& token) {
    if (token.type() == TokenType::SEMI_COLON) {
        rejectStraySemicolon();
        return;
    }

    reportDiagnostic(token.span(),
                     "Unexpected token " + tokenDisplayText(token) + ".",
                     "parse.unexpected_token");
}

void AstParser::error() { reportUnexpectedToken(m_current); }

void AstParser::errorAtLine(size_t line, const std::string& message) {
    errorAtSpan(makePointSpan(line == 0 ? 1 : line, 1), message);
}

void AstParser::errorAtSpan(const SourceSpan& span, const std::string& message) {
    reportDiagnostic(span, message, "parse.error");
}

void AstParser::rejectStraySemicolon(std::string code) {
    if (!check(TokenType::SEMI_COLON)) {
        return;
    }

    m_hadError = true;
    m_errors.push_back(ParseError{
        m_current.span(),
        "Semicolons are only allowed inside 'for (...)' clauses.",
        std::move(code),
    });
    advance();
}

bool AstParser::isRecoveryBoundaryToken(TokenType type) const {
    switch (type) {
        case TokenType::TYPE:
        case TokenType::VAR:
        case TokenType::CONST:
        case TokenType::TYPE_FN:
        case TokenType::FOR:
        case TokenType::IF:
        case TokenType::WHILE:
        case TokenType::PRINT:
        case TokenType::_RETURN:
        case TokenType::CLOSE_CURLY:
        case TokenType::END_OF_FILE:
            return true;
        default:
            return false;
    }
}

bool AstParser::recoverLineLeadingContinuation(
    std::initializer_list<TokenType> terminators) {
    if (!hasLineBreakBeforeCurrent() ||
        !isLineContinuationToken(m_current.type())) {
        return false;
    }

    errorAtSpan(m_current.span(),
                "Continuation token '" +
                    std::string(continuationTokenText(m_current.type())) +
                    "' must stay on the previous line.");

    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;

    while (!check(TokenType::END_OF_FILE)) {
        if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
            for (TokenType terminator : terminators) {
                if (check(terminator)) {
                    return true;
                }
            }

            if (check(TokenType::SEMI_COLON) || check(TokenType::CLOSE_CURLY) ||
                isRecoveryBoundaryToken(m_current.type())) {
                return true;
            }
        }

        switch (m_current.type()) {
            case TokenType::OPEN_PAREN:
                ++parenDepth;
                break;
            case TokenType::CLOSE_PAREN:
                if (parenDepth > 0) {
                    --parenDepth;
                }
                break;
            case TokenType::OPEN_BRACKET:
                ++bracketDepth;
                break;
            case TokenType::CLOSE_BRACKET:
                if (bracketDepth > 0) {
                    --bracketDepth;
                }
                break;
            case TokenType::OPEN_CURLY:
                ++braceDepth;
                break;
            case TokenType::CLOSE_CURLY:
                if (braceDepth > 0) {
                    --braceDepth;
                }
                break;
            default:
                break;
        }

        advance();
    }

    return true;
}

bool AstParser::rejectUnexpectedTrailingToken(
    std::initializer_list<TokenType> allowedTerminators) {
    if (check(TokenType::END_OF_FILE) || hasLineBreakBeforeCurrent() ||
        check(TokenType::SEMI_COLON) || check(TokenType::CLOSE_CURLY)) {
        return false;
    }

    for (TokenType terminator : allowedTerminators) {
        if (check(terminator)) {
            return false;
        }
    }

    error();

    while (!check(TokenType::END_OF_FILE) && !hasLineBreakBeforeCurrent()) {
        bool reachedTerminator = false;
        for (TokenType terminator : allowedTerminators) {
            if (check(terminator)) {
                reachedTerminator = true;
                break;
            }
        }

        if (reachedTerminator || check(TokenType::SEMI_COLON) ||
            check(TokenType::CLOSE_CURLY) ||
            isRecoveryBoundaryToken(m_current.type())) {
            break;
        }

        advance();
    }

    return true;
}

bool AstParser::parseTypeLookahead(size_t& offset) {
    auto consumeOptionalSuffix = [&]() {
        while (tokenAt(offset).type() == TokenType::QUESTION) {
            ++offset;
        }
    };

    const Token& current = tokenAt(offset);
    if (current.type() == TokenType::TYPE_FN) {
        ++offset;
        if (tokenAt(offset).type() != TokenType::OPEN_PAREN) {
            return false;
        }

        ++offset;
        if (tokenAt(offset).type() != TokenType::CLOSE_PAREN) {
            while (true) {
                if (!parseTypeLookahead(offset)) {
                    return false;
                }
                if (tokenAt(offset).type() == TokenType::COMMA) {
                    ++offset;
                    continue;
                }
                if (tokenAt(offset).type() != TokenType::CLOSE_PAREN) {
                    return false;
                }
                break;
            }
        }

        ++offset;
        if (!parseTypeLookahead(offset)) {
            return false;
        }

        consumeOptionalSuffix();
        return true;
    }

    if (isTypeToken(current.type())) {
        ++offset;
        consumeOptionalSuffix();
        return true;
    }

    if (current.type() != TokenType::IDENTIFIER) {
        return false;
    }

    std::string_view name(current.start(), current.length());
    ++offset;

    if (isHandleTypeNameText(name)) {
        if (tokenAt(offset).type() != TokenType::LESS) {
            return false;
        }
        ++offset;

        if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
            return false;
        }
        std::string_view packageNamespace(tokenAt(offset).start(),
                                          tokenAt(offset).length());
        ++offset;
        if (tokenAt(offset).type() != TokenType::COLON ||
            !isValidPackageIdPart(packageNamespace)) {
            return false;
        }
        ++offset;

        if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
            return false;
        }
        std::string_view packageName(tokenAt(offset).start(),
                                     tokenAt(offset).length());
        ++offset;
        if (tokenAt(offset).type() != TokenType::COLON ||
            !isValidPackageIdPart(packageName)) {
            return false;
        }
        ++offset;

        if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
            return false;
        }
        std::string_view typeName(tokenAt(offset).start(),
                                  tokenAt(offset).length());
        ++offset;
        if (tokenAt(offset).type() != TokenType::GREATER ||
            !isValidHandleTypeName(typeName)) {
            return false;
        }
        ++offset;
        consumeOptionalSuffix();
        return true;
    }

    if (isCollectionTypeNameText(name) && tokenAt(offset).type() == TokenType::LESS) {
        ++offset;
        if (name == "Array" || name == "Set") {
            if (!parseTypeLookahead(offset) ||
                tokenAt(offset).type() != TokenType::GREATER) {
                return false;
            }
            ++offset;
            consumeOptionalSuffix();
            return true;
        }

        if (!parseTypeLookahead(offset) ||
            tokenAt(offset).type() != TokenType::COMMA) {
            return false;
        }
        ++offset;
        if (!parseTypeLookahead(offset) ||
            tokenAt(offset).type() != TokenType::GREATER) {
            return false;
        }
        ++offset;
        consumeOptionalSuffix();
        return true;
    }

    consumeOptionalSuffix();
    return true;
}

bool AstParser::isTypedTypeAnnotationStart() {
    if (check(TokenType::TYPE_FN)) {
        return peekToken().type() == TokenType::OPEN_PAREN;
    }

    if (isTypeToken(m_current.type())) {
        return true;
    }

    if (!check(TokenType::IDENTIFIER)) {
        return false;
    }

    const Token& lookahead = peekToken();
    if (isHandleTypeNameText(tokenText(m_current))) {
        return lookahead.type() == TokenType::LESS;
    }

    if (isCollectionTypeNameText(tokenText(m_current))) {
        return lookahead.type() == TokenType::LESS;
    }

    size_t offset = 0;
    if (!parseTypeLookahead(offset)) {
        return false;
    }

    return true;
}

std::unique_ptr<AstTypeExpr> AstParser::parseTypeExpr() {
    std::unique_ptr<AstTypeExpr> typeExpr;
    Token startToken = m_current;

    if (check(TokenType::TYPE_FN)) {
        Token typeToken = m_current;
        advance();
        if (!consume(TokenType::OPEN_PAREN)) {
            return nullptr;
        }

        auto functionType = std::make_unique<AstTypeExpr>();
        functionType->node = makeNodeInfo(typeToken);
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

        functionType->node = makeNodeInfo(
            combineSourceSpans(typeToken.span(), functionType->returnType->node.span));

        typeExpr = std::move(functionType);
    } else if (isTypeToken(m_current.type()) &&
               m_current.type() != TokenType::TYPE_FN) {
        auto namedType = std::make_unique<AstTypeExpr>();
        namedType->node = makeNodeInfo(m_current);
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
            handleType->node = makeNodeInfo(
                combineSourceSpans(nameToken.span(), m_previous.span()));
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
                collectionType->node = makeNodeInfo(
                    combineSourceSpans(nameToken.span(), m_previous.span()));
                collectionType->kind =
                    name == "Array" ? AstTypeKind::ARRAY : AstTypeKind::SET;
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
                dictType->node = makeNodeInfo(
                    combineSourceSpans(nameToken.span(), m_previous.span()));
                dictType->kind = AstTypeKind::DICT;
                dictType->token = nameToken;
                dictType->keyType = std::move(keyType);
                dictType->valueType = std::move(valueType);
                typeExpr = std::move(dictType);
            }
        } else {
            auto namedType = std::make_unique<AstTypeExpr>();
            namedType->node = makeNodeInfo(nameToken);
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
        optionalType->node =
            makeNodeInfo(combineSourceSpans(typeExpr->node.span, m_previous.span()));
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
        do {
            if (!check(TokenType::IDENTIFIER)) {
                error();
                return {};
            }

            AstParameter param;
            param.node = makeNodeInfo(m_current);
            param.name = m_current;
            advance();
            if (isTypedTypeAnnotationStart()) {
                param.type = parseTypeExpr();
                if (!param.type) {
                    return {};
                }
                param.node = makeNodeInfo(
                    combineSourceSpans(param.name.span(), param.type->node.span));
            }
            params.push_back(std::move(param));
        } while (match(TokenType::COMMA));
    }

    if (!consume(TokenType::CLOSE_PAREN)) {
        return {};
    }

    return params;
}

AstItemPtr AstParser::parseItem() {
    if (check(TokenType::SEMI_COLON)) {
        rejectStraySemicolon();
        if (m_hadError) {
            return nullptr;
        }
    }

    auto item = std::make_unique<AstItem>();

    if (match(TokenType::TYPE)) {
        item->node = makeNodeInfo(m_previous);
        if (!parseTypeDeclaration(*item)) {
            return nullptr;
        }
        return item;
    }

    if (check(TokenType::TYPE_FN) && peekToken().type() == TokenType::IDENTIFIER) {
        advance();
        item->node = makeNodeInfo(m_previous);
        if (!parseFunctionDeclaration(*item)) {
            return nullptr;
        }
        return item;
    }

    AstStmtPtr stmt;
    if (check(TokenType::CONST) || check(TokenType::VAR)) {
        stmt = parseVariableDeclarationStatement();
    } else {
        stmt = parseStatement();
    }

    if (!stmt) {
        return nullptr;
    }

    item->node = stmt->node;
    item->value = std::move(stmt);
    return item;
}

bool AstParser::parseTypeDeclaration(AstItem& outItem) {
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

        AstTypeAliasDecl aliasDecl;
        aliasDecl.node =
            makeNodeInfo(combineSourceSpans(nameToken.span(), aliasedType->node.span));
        aliasDecl.name = nameToken;
        aliasDecl.aliasedType = std::move(aliasedType);
        rejectStraySemicolon();
        outItem.value = std::move(aliasDecl);
        return !m_hadError;
    }

    match(TokenType::STRUCT);

    AstClassDecl classDecl;
    classDecl.node = makeNodeInfo(nameToken);
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
        if (check(TokenType::SEMI_COLON)) {
            rejectStraySemicolon();
            continue;
        }
        if (!parseClassMember(classDecl)) {
            return false;
        }
    }

    if (!consume(TokenType::CLOSE_CURLY)) {
        return false;
    }

    classDecl.node =
        makeNodeInfo(combineSourceSpans(nameToken.span(), m_previous.span()));
    outItem.value = std::move(classDecl);
    return !m_hadError;
}

bool AstParser::parseFunctionDeclaration(AstItem& outItem) {
    if (!check(TokenType::IDENTIFIER)) {
        error();
        return false;
    }

    AstFunctionDecl functionDecl;
    functionDecl.node = makeNodeInfo(m_current);
    functionDecl.name = m_current;
    advance();
    functionDecl.params = parseParameters();
    if (m_hadError) {
        return false;
    }

    if (isTypedTypeAnnotationStart()) {
        functionDecl.returnType = parseTypeExpr();
        if (!functionDecl.returnType) {
            return false;
        }
    }

    functionDecl.body = parseBlockStatement();
    if (!functionDecl.body) {
        return false;
    }

    functionDecl.node = makeNodeInfo(
        combineSourceSpans(functionDecl.name.span(), functionDecl.body->node.span));
    outItem.value = std::move(functionDecl);
    return !m_hadError;
}

bool AstParser::parseClassMember(AstClassDecl& outClassDecl) {
    std::vector<int> annotatedOperators;

    auto finishMethod = [&](AstMethodDecl methodDecl) {
        methodDecl.body = parseBlockStatement();
        if (!methodDecl.body) {
            return false;
        }

        methodDecl.node = makeNodeInfo(
            combineSourceSpans(methodDecl.name.span(), methodDecl.body->node.span));
        outClassDecl.methods.push_back(std::move(methodDecl));
        return !m_hadError;
    };

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
        methodDecl.node = makeNodeInfo(m_current);
        methodDecl.name = m_current;
        methodDecl.annotatedOperators = std::move(annotatedOperators);
        advance();
        methodDecl.params = parseParameters();
        if (m_hadError) {
            return false;
        }

        if (isTypedTypeAnnotationStart()) {
            methodDecl.returnType = parseTypeExpr();
            if (!methodDecl.returnType) {
                return false;
            }
        }

        return finishMethod(std::move(methodDecl));
    }

    if (check(TokenType::IDENTIFIER) && peekToken().type() == TokenType::OPEN_PAREN) {
        AstMethodDecl methodDecl;
        methodDecl.node = makeNodeInfo(m_current);
        methodDecl.name = m_current;
        methodDecl.annotatedOperators = std::move(annotatedOperators);
        advance();
        methodDecl.params = parseParameters();
        if (m_hadError) {
            return false;
        }

        return finishMethod(std::move(methodDecl));
    }

    size_t returnTypeOffset = 0;
    if (isTypedTypeAnnotationStart() &&
        (returnTypeOffset = 0, parseTypeLookahead(returnTypeOffset)) &&
        tokenAt(returnTypeOffset).type() == TokenType::IDENTIFIER &&
        tokenAt(returnTypeOffset + 1).type() == TokenType::OPEN_PAREN) {
        std::unique_ptr<AstTypeExpr> returnType = parseTypeExpr();
        if (!returnType) {
            return false;
        }

        if (check(TokenType::IDENTIFIER) && peekToken().type() == TokenType::OPEN_PAREN) {
            AstMethodDecl methodDecl;
            methodDecl.node = makeNodeInfo(m_current);
            methodDecl.name = m_current;
            methodDecl.returnType = std::move(returnType);
            methodDecl.annotatedOperators = std::move(annotatedOperators);
            advance();
            methodDecl.params = parseParameters();
            if (m_hadError) {
                return false;
            }

            return finishMethod(std::move(methodDecl));
        }

        return false;
    }

    if (!annotatedOperators.empty()) {
        error();
    }

    if (!check(TokenType::IDENTIFIER)) {
        error();
        return false;
    }

    AstFieldDecl fieldDecl;
    fieldDecl.node = makeNodeInfo(m_current);
    fieldDecl.name = m_current;
    advance();
    fieldDecl.type = parseTypeExpr();
    if (!fieldDecl.type) {
        return false;
    }
    fieldDecl.node =
        makeNodeInfo(combineSourceSpans(fieldDecl.name.span(), fieldDecl.type->node.span));
    rejectStraySemicolon();
    outClassDecl.fields.push_back(std::move(fieldDecl));
    return !m_hadError;
}

AstStmtPtr AstParser::parseStatement() {
    if (check(TokenType::SEMI_COLON)) {
        rejectStraySemicolon();
        return nullptr;
    }

    if (match(TokenType::PRINT)) {
        return parsePrintStatement(m_previous);
    }

    if (match(TokenType::IF)) {
        return parseIfStatement(m_previous);
    }

    if (match(TokenType::WHILE)) {
        return parseWhileStatement(m_previous);
    }

    if (match(TokenType::FOR)) {
        return parseForStatement(m_previous);
    }

    if (match(TokenType::_RETURN)) {
        return parseReturnStatement(m_previous);
    }

    if (check(TokenType::OPEN_CURLY)) {
        return parseBlockStatement();
    }

    return parseExpressionStatement();
}

AstStmtPtr AstParser::parseBlockStatement() {
    Token openToken = m_current;
    if (!consume(TokenType::OPEN_CURLY)) {
        return nullptr;
    }

    AstBlockStmt blockStmt;
    while (!check(TokenType::CLOSE_CURLY) && !check(TokenType::END_OF_FILE)) {
        AstItemPtr item = parseItem();
        if (!item) {
            return nullptr;
        }
        blockStmt.items.push_back(std::move(item));
    }

    if (!consume(TokenType::CLOSE_CURLY)) {
        return nullptr;
    }

    auto stmt = std::make_unique<AstStmt>();
    stmt->node = makeNodeInfo(combineSourceSpans(openToken.span(), m_previous.span()));
    stmt->value = std::move(blockStmt);
    return stmt;
}

AstStmtPtr AstParser::parsePrintStatement(const Token& printToken) {
    if (!consume(TokenType::OPEN_PAREN)) {
        return nullptr;
    }

    AstPrintStmt printStmt;
    printStmt.expression = parseExpression();
    if (!printStmt.expression) {
        return nullptr;
    }
    recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
    if (!consume(TokenType::CLOSE_PAREN)) {
        return nullptr;
    }
    rejectStraySemicolon();

    auto stmt = std::make_unique<AstStmt>();
    stmt->node =
        makeNodeInfo(combineSourceSpans(printToken.span(), printStmt.expression->node.span));
    stmt->value = std::move(printStmt);
    return stmt;
}

AstStmtPtr AstParser::parseIfStatement(const Token& ifToken) {
    if (!consume(TokenType::OPEN_PAREN)) {
        return nullptr;
    }

    AstIfStmt ifStmt;
    ifStmt.condition = parseExpression();
    if (!ifStmt.condition) {
        return nullptr;
    }
    recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
    if (!consume(TokenType::CLOSE_PAREN)) {
        return nullptr;
    }
    ifStmt.thenBranch = parseStatement();
    if (!ifStmt.thenBranch) {
        return nullptr;
    }
    if (match(TokenType::ELSE)) {
        ifStmt.elseBranch = parseStatement();
        if (!ifStmt.elseBranch) {
            return nullptr;
        }
    }

    auto stmt = std::make_unique<AstStmt>();
    const SourceSpan endSpan = ifStmt.elseBranch
                                   ? ifStmt.elseBranch->node.span
                                   : ifStmt.thenBranch->node.span;
    stmt->node = makeNodeInfo(combineSourceSpans(ifToken.span(), endSpan));
    stmt->value = std::move(ifStmt);
    return stmt;
}

AstStmtPtr AstParser::parseWhileStatement(const Token& whileToken) {
    if (!consume(TokenType::OPEN_PAREN)) {
        return nullptr;
    }

    AstWhileStmt whileStmt;
    whileStmt.condition = parseExpression();
    if (!whileStmt.condition) {
        return nullptr;
    }
    recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
    if (!consume(TokenType::CLOSE_PAREN)) {
        return nullptr;
    }
    whileStmt.body = parseStatement();
    if (!whileStmt.body) {
        return nullptr;
    }

    auto stmt = std::make_unique<AstStmt>();
    stmt->node =
        makeNodeInfo(combineSourceSpans(whileToken.span(), whileStmt.body->node.span));
    stmt->value = std::move(whileStmt);
    return stmt;
}

AstStmtPtr AstParser::parseForStatement(const Token& forToken) {
    if (!consume(TokenType::OPEN_PAREN)) {
        return nullptr;
    }

    AstForStmt forStmt;
    std::optional<AstForEachStmt> forEachStmt;

    if (match(TokenType::SEMI_COLON)) {
    } else if (check(TokenType::CONST) || check(TokenType::VAR)) {
        Token mutabilityToken = m_current;
        bool isConst = match(TokenType::CONST);
        if (!isConst) {
            consume(TokenType::VAR);
        }
        if (!check(TokenType::IDENTIFIER)) {
            error();
            return nullptr;
        }

        Token nameToken = m_current;
        advance();
        std::unique_ptr<AstTypeExpr> declaredType = parseTypeExpr();
        if (!declaredType) {
            return nullptr;
        }

        if (match(TokenType::COLON)) {
            AstForEachStmt foreach;
            foreach.isConst = isConst;
            foreach.name = nameToken;
            foreach.declaredType = std::move(declaredType);
            foreach.iterable = parseExpression();
            if (!foreach.iterable) {
                return nullptr;
            }
            recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
            if (!consume(TokenType::CLOSE_PAREN)) {
                return nullptr;
            }
            foreach.body = parseStatement();
            if (!foreach.body) {
                return nullptr;
            }
            forEachStmt = std::move(foreach);
        } else {
            if (!consume(TokenType::EQUAL)) {
                return nullptr;
            }
            auto varDecl = std::make_unique<AstVarDeclStmt>();
            varDecl->node = makeNodeInfo(mutabilityToken);
            varDecl->isConst = isConst;
            varDecl->name = nameToken;
            varDecl->declaredType = std::move(declaredType);
            varDecl->initializer = parseExpression();
            if (!varDecl->initializer) {
                return nullptr;
            }
            recoverLineLeadingContinuation({TokenType::SEMI_COLON});
            if (!consume(TokenType::SEMI_COLON)) {
                return nullptr;
            }
            varDecl->node = makeNodeInfo(
                combineSourceSpans(mutabilityToken.span(), varDecl->initializer->node.span));
            forStmt.initializer = std::move(varDecl);
        }
        (void)mutabilityToken;
    } else {
        AstExprPtr initializer = parseExpression();
        if (!initializer) {
            return nullptr;
        }
        recoverLineLeadingContinuation({TokenType::SEMI_COLON});
        if (!consume(TokenType::SEMI_COLON)) {
            return nullptr;
        }
        forStmt.initializer = std::move(initializer);
    }

    if (forEachStmt.has_value()) {
        auto stmt = std::make_unique<AstStmt>();
        stmt->node =
            makeNodeInfo(combineSourceSpans(forToken.span(), forEachStmt->body->node.span));
        stmt->value = std::move(*forEachStmt);
        return stmt;
    }

    if (!check(TokenType::SEMI_COLON)) {
        forStmt.condition = parseExpression();
        if (!forStmt.condition) {
            return nullptr;
        }
        recoverLineLeadingContinuation({TokenType::SEMI_COLON});
    }
    if (!consume(TokenType::SEMI_COLON)) {
        return nullptr;
    }

    if (!check(TokenType::CLOSE_PAREN)) {
        forStmt.increment = parseExpression();
        if (!forStmt.increment) {
            return nullptr;
        }
        recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
    }
    if (!consume(TokenType::CLOSE_PAREN)) {
        return nullptr;
    }

    forStmt.body = parseStatement();
    if (!forStmt.body) {
        return nullptr;
    }

    auto stmt = std::make_unique<AstStmt>();
    stmt->node = makeNodeInfo(combineSourceSpans(forToken.span(), forStmt.body->node.span));
    stmt->value = std::move(forStmt);
    return stmt;
}

AstStmtPtr AstParser::parseReturnStatement(const Token& returnToken) {
    AstReturnStmt returnStmt;

    if (check(TokenType::SEMI_COLON) || check(TokenType::CLOSE_CURLY) ||
        check(TokenType::END_OF_FILE)) {
        rejectStraySemicolon();
    } else {
        returnStmt.value = parseExpression();
        if (!returnStmt.value) {
            return nullptr;
        }
        recoverLineLeadingContinuation();
        rejectUnexpectedTrailingToken();
        rejectStraySemicolon();
    }

    auto stmt = std::make_unique<AstStmt>();
    stmt->node = makeNodeInfo(returnStmt.value
                                  ? combineSourceSpans(returnToken.span(),
                                                       returnStmt.value->node.span)
                                  : returnToken.span());
    stmt->value = std::move(returnStmt);
    return stmt;
}

AstStmtPtr AstParser::parseExpressionStatement() {
    AstExprStmt exprStmt;
    exprStmt.expression = parseExpression();
    if (!exprStmt.expression) {
        return nullptr;
    }
    recoverLineLeadingContinuation();
    rejectUnexpectedTrailingToken();
    rejectStraySemicolon();

    auto stmt = std::make_unique<AstStmt>();
    stmt->node = exprStmt.expression->node;
    stmt->value = std::move(exprStmt);
    return stmt;
}

AstStmtPtr AstParser::parseVariableDeclarationStatement(bool allowForClause) {
    if (!check(TokenType::CONST) && !check(TokenType::VAR)) {
        error();
        return nullptr;
    }

    Token mutabilityToken = m_current;
    bool isConst = match(TokenType::CONST);
    if (!isConst) {
        consume(TokenType::VAR);
    }

    if (match(TokenType::OPEN_CURLY)) {
        AstDestructuredImportStmt destructuredDecl;
        destructuredDecl.isConst = isConst;

        if (!check(TokenType::CLOSE_CURLY)) {
            do {
                if (!check(TokenType::IDENTIFIER)) {
                    error();
                    return nullptr;
                }

                AstImportBinding binding;
                binding.node = makeNodeInfo(m_current);
                binding.exportedName = m_current;
                SourceSpan bindingSpan = m_current.span();
                advance();
                if (match(TokenType::AS_KW)) {
                    if (!check(TokenType::IDENTIFIER)) {
                        error();
                        return nullptr;
                    }
                    binding.localName = m_current;
                    bindingSpan =
                        combineSourceSpans(bindingSpan, m_current.span());
                    advance();
                }
                if (match(TokenType::COLON)) {
                    if (!isTypedTypeAnnotationStart()) {
                        errorAtSpan(m_previous.span(),
                                    "Expected type after ':' in import binding.");
                        return nullptr;
                    }
                    binding.expectedType = parseTypeExpr();
                    if (!binding.expectedType) {
                        return nullptr;
                    }
                    bindingSpan = combineSourceSpans(
                        bindingSpan, binding.expectedType->node.span);
                }
                binding.node = makeNodeInfo(bindingSpan);
                destructuredDecl.bindings.push_back(std::move(binding));
            } while (match(TokenType::COMMA));
        }

        if (!consume(TokenType::CLOSE_CURLY) || !consume(TokenType::EQUAL)) {
            return nullptr;
        }

        if (!check(TokenType::AT)) {
            error();
            return nullptr;
        }
        Token atToken = m_current;
        advance();
        destructuredDecl.initializer = parseImportExpression(atToken);
        if (!destructuredDecl.initializer) {
            return nullptr;
        }

        if (!allowForClause) {
            rejectStraySemicolon();
        }

        auto stmt = std::make_unique<AstStmt>();
        stmt->node = makeNodeInfo(combineSourceSpans(
            mutabilityToken.span(), destructuredDecl.initializer->node.span));
        stmt->value = std::move(destructuredDecl);
        return stmt;
    }

    if (!check(TokenType::IDENTIFIER)) {
        error();
        return nullptr;
    }

    auto varDecl = std::make_unique<AstVarDeclStmt>();
    varDecl->node = makeNodeInfo(mutabilityToken);
    varDecl->isConst = isConst;
    varDecl->name = m_current;
    advance();

    if (check(TokenType::EQUAL)) {
        varDecl->omittedType = true;
    } else {
        varDecl->declaredType = parseTypeExpr();
        if (!varDecl->declaredType) {
            return nullptr;
        }
    }

    if (!consume(TokenType::EQUAL)) {
        return nullptr;
    }

    if (check(TokenType::AT)) {
        Token atToken = m_current;
        advance();
        varDecl->initializer = parseImportExpression(atToken);
    } else {
        varDecl->initializer = parseExpression();
    }
    if (!varDecl->initializer) {
        return nullptr;
    }

    if (!allowForClause) {
        recoverLineLeadingContinuation();
        rejectUnexpectedTrailingToken();
        rejectStraySemicolon();
    }

    auto stmt = std::make_unique<AstStmt>();
    stmt->node =
        makeNodeInfo(combineSourceSpans(mutabilityToken.span(), varDecl->initializer->node.span));
    stmt->value = std::move(*varDecl);
    return stmt;
}

AstExprPtr AstParser::parseExpression() { return parseAssignment(); }

AstExprPtr AstParser::parseAssignment() {
    AstExprPtr lhs = parseOr();
    if (!lhs) {
        return nullptr;
    }

    if (!isAssignmentOperator(m_current.type())) {
        return lhs;
    }

    const bool lhsAssignable =
        std::holds_alternative<AstIdentifierExpr>(lhs->value) ||
        std::holds_alternative<AstMemberExpr>(lhs->value) ||
        std::holds_alternative<AstIndexExpr>(lhs->value);
    if (hasLineBreakBeforeCurrent() &&
        (m_current.type() != TokenType::PLUS_PLUS &&
         m_current.type() != TokenType::MINUS_MINUS)) {
        return lhs;
    }

    if (hasLineBreakBeforeCurrent() && !lhsAssignable) {
        return lhs;
    }

    Token op = m_current;
    advance();

    if (op.type() == TokenType::PLUS_PLUS || op.type() == TokenType::MINUS_MINUS) {
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(combineSourceSpans(lhs->node.span, op.span()));
        expr->value =
            AstUpdateExpr{op, std::move(lhs), false};
        return expr;
    }

    AstExprPtr rhs = parseAssignment();
    if (!rhs) {
        return nullptr;
    }

    auto expr = std::make_unique<AstExpr>();
    expr->node = makeNodeInfo(combineSourceSpans(lhs->node.span, rhs->node.span));
    expr->value = AstAssignmentExpr{std::move(lhs), op, std::move(rhs)};
    return expr;
}

AstExprPtr AstParser::parseOr() {
    AstExprPtr expr = parseAnd();
    while (expr && matchSameLine(TokenType::LOGICAL_OR)) {
        Token op = m_previous;
        AstExprPtr rhs = parseAnd();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseAnd() {
    AstExprPtr expr = parseEquality();
    while (expr && matchSameLine(TokenType::LOGICAL_AND)) {
        Token op = m_previous;
        AstExprPtr rhs = parseEquality();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseEquality() {
    AstExprPtr expr = parseComparison();
    while (expr && !hasLineBreakBeforeCurrent() &&
           isEqualityOperator(m_current.type())) {
        Token op = m_current;
        advance();
        AstExprPtr rhs = parseComparison();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseComparison() {
    AstExprPtr expr = parseBitwiseOr();
    while (expr && !hasLineBreakBeforeCurrent() &&
           isComparisonOperator(m_current.type())) {
        Token op = m_current;
        advance();
        AstExprPtr rhs = parseBitwiseOr();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseBitwiseOr() {
    AstExprPtr expr = parseBitwiseXor();
    while (expr && matchSameLine(TokenType::PIPE)) {
        Token op = m_previous;
        AstExprPtr rhs = parseBitwiseXor();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseBitwiseXor() {
    AstExprPtr expr = parseBitwiseAnd();
    while (expr && matchSameLine(TokenType::CARET)) {
        Token op = m_previous;
        AstExprPtr rhs = parseBitwiseAnd();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseBitwiseAnd() {
    AstExprPtr expr = parseShift();
    while (expr && matchSameLine(TokenType::AMPERSAND)) {
        Token op = m_previous;
        AstExprPtr rhs = parseShift();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseShift() {
    AstExprPtr expr = parseTerm();
    while (expr && !hasLineBreakBeforeCurrent() &&
           (check(TokenType::SHIFT_LEFT_TOKEN) ||
            check(TokenType::SHIFT_RIGHT_TOKEN))) {
        Token op = m_current;
        advance();
        AstExprPtr rhs = parseTerm();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseTerm() {
    AstExprPtr expr = parseFactor();
    while (expr && !hasLineBreakBeforeCurrent() &&
           (check(TokenType::PLUS) || check(TokenType::MINUS))) {
        Token op = m_current;
        advance();
        AstExprPtr rhs = parseFactor();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseFactor() {
    AstExprPtr expr = parseUnary();
    while (expr && !hasLineBreakBeforeCurrent() &&
           (check(TokenType::STAR) || check(TokenType::SLASH))) {
        Token op = m_current;
        advance();
        AstExprPtr rhs = parseUnary();
        if (!rhs) {
            return nullptr;
        }
        auto binary = std::make_unique<AstExpr>();
        binary->node = makeNodeInfo(combineSourceSpans(expr->node.span, rhs->node.span));
        binary->value = AstBinaryExpr{std::move(expr), op, std::move(rhs)};
        expr = std::move(binary);
    }
    return expr;
}

AstExprPtr AstParser::parseUnary() {
    if (match(TokenType::BANG) || match(TokenType::MINUS) ||
        match(TokenType::TILDE)) {
        Token op = m_previous;
        AstExprPtr operand = parseUnary();
        if (!operand) {
            return nullptr;
        }
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(combineSourceSpans(op.span(), operand->node.span));
        expr->value = AstUnaryExpr{op, std::move(operand)};
        return expr;
    }

    if (match(TokenType::PLUS_PLUS) || match(TokenType::MINUS_MINUS)) {
        Token op = m_previous;
        AstExprPtr operand = parseUnary();
        if (!operand) {
            return nullptr;
        }
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(combineSourceSpans(op.span(), operand->node.span));
        expr->value = AstUpdateExpr{op, std::move(operand), true};
        return expr;
    }

    return parseCast();
}

AstExprPtr AstParser::parseCast() {
    AstExprPtr expr = parseCall();
    while (expr && matchSameLine(TokenType::AS_KW)) {
        Token asToken = m_previous;
        std::unique_ptr<AstTypeExpr> targetType = parseTypeExpr();
        if (!targetType) {
            return nullptr;
        }

        auto castExpr = std::make_unique<AstExpr>();
        castExpr->node =
            makeNodeInfo(combineSourceSpans(expr->node.span, targetType->node.span));
        castExpr->value = AstCastExpr{std::move(expr), std::move(targetType)};
        expr = std::move(castExpr);
    }
    return expr;
}

AstExprPtr AstParser::parseCall() {
    AstExprPtr expr = parsePrimary();
    if (!expr) {
        return nullptr;
    }

    while (true) {
        if (matchSameLine(TokenType::OPEN_PAREN)) {
            Token openParen = m_previous;
            AstCallExpr callExpr;
            callExpr.callee = std::move(expr);
            if (!check(TokenType::CLOSE_PAREN)) {
                do {
                    AstExprPtr arg = parseExpression();
                    if (!arg) {
                        return nullptr;
                    }
                    callExpr.arguments.push_back(std::move(arg));
                } while (match(TokenType::COMMA));
            }
            if (!consume(TokenType::CLOSE_PAREN)) {
                return nullptr;
            }

            auto call = std::make_unique<AstExpr>();
            call->node = makeNodeInfo(
                combineSourceSpans(callExpr.callee->node.span, m_previous.span()));
            call->value = std::move(callExpr);
            expr = std::move(call);
            continue;
        }

        if (matchSameLine(TokenType::DOT)) {
            Token dotToken = m_previous;
            if (!check(TokenType::IDENTIFIER)) {
                error();
                return nullptr;
            }
            Token memberToken = m_current;
            advance();

            auto member = std::make_unique<AstExpr>();
            member->node =
                makeNodeInfo(combineSourceSpans(expr->node.span, memberToken.span()));
            member->value = AstMemberExpr{std::move(expr), memberToken};
            expr = std::move(member);
            continue;
        }

        if (matchSameLine(TokenType::OPEN_BRACKET)) {
            Token openBracket = m_previous;
            AstExprPtr index = parseExpression();
            if (!index) {
                return nullptr;
            }
            if (!consume(TokenType::CLOSE_BRACKET)) {
                return nullptr;
            }

            auto indexed = std::make_unique<AstExpr>();
            indexed->node = makeNodeInfo(
                combineSourceSpans(expr->node.span, m_previous.span()));
            indexed->value = AstIndexExpr{std::move(expr), std::move(index)};
            expr = std::move(indexed);
            continue;
        }

        break;
    }

    return expr;
}

AstExprPtr AstParser::parseFunctionLiteralExpr() {
    Token fnToken = m_previous;

    auto functionExpr = std::make_unique<AstExpr>();
    functionExpr->node = makeNodeInfo(fnToken);

    AstFunctionExpr value;
    value.params = parseParameters();
    if (m_hadError) {
        return nullptr;
    }

    if (check(TokenType::FAT_ARROW)) {
        value.usesFatArrow = true;
        if (hasLineBreakBeforeCurrent()) {
            errorAtSpan(m_current.span(),
                        "Continuation token '" +
                            std::string(continuationTokenText(m_current.type())) +
                            "' must stay on the previous line.");
        }
        advance();
        if (check(TokenType::OPEN_CURLY)) {
            value.blockBody = parseBlockStatement();
            if (!value.blockBody) {
                return nullptr;
            }
            functionExpr->node =
                makeNodeInfo(combineSourceSpans(fnToken.span(), value.blockBody->node.span));
            functionExpr->value = std::move(value);
            return functionExpr;
        }
        value.expressionBody = parseExpression();
        if (!value.expressionBody) {
            return nullptr;
        }
        recoverLineLeadingContinuation({TokenType::COMMA, TokenType::CLOSE_PAREN,
                                        TokenType::CLOSE_BRACKET,
                                        TokenType::CLOSE_CURLY});
        rejectUnexpectedTrailingToken({TokenType::COMMA, TokenType::CLOSE_PAREN,
                                       TokenType::CLOSE_BRACKET,
                                       TokenType::CLOSE_CURLY});
        functionExpr->node = makeNodeInfo(
            combineSourceSpans(fnToken.span(), value.expressionBody->node.span));
        functionExpr->value = std::move(value);
        return functionExpr;
    }

    if (isTypedTypeAnnotationStart()) {
        value.returnType = parseTypeExpr();
        if (!value.returnType) {
            return nullptr;
        }
    }

    value.blockBody = parseBlockStatement();
    if (!value.blockBody) {
        return nullptr;
    }

    functionExpr->node =
        makeNodeInfo(combineSourceSpans(fnToken.span(), value.blockBody->node.span));
    functionExpr->value = std::move(value);
    return functionExpr;
}

AstExprPtr AstParser::parseImportExpression(const Token& atToken) {
    if (!check(TokenType::IDENTIFIER)) {
        error();
        return nullptr;
    }

    std::string directiveName = tokenText(m_current);
    advance();
    if (directiveName != "import") {
        error();
        return nullptr;
    }

    if (!consume(TokenType::OPEN_PAREN) || !check(TokenType::STRING)) {
        return nullptr;
    }

    Token pathToken = m_current;
    advance();
    if (!consume(TokenType::CLOSE_PAREN)) {
        return nullptr;
    }

    auto expr = std::make_unique<AstExpr>();
    expr->node = makeNodeInfo(combineSourceSpans(atToken.span(), m_previous.span()));
    expr->value = AstImportExpr{pathToken};
    return expr;
}

AstExprPtr AstParser::parsePrimary() {
    if (match(TokenType::NUMBER) || match(TokenType::STRING) ||
        match(TokenType::TRUE) || match(TokenType::FALSE) ||
        match(TokenType::_NULL) || match(TokenType::TYPE_NULL_KW)) {
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(m_previous);
        expr->value = AstLiteralExpr{m_previous};
        return expr;
    }

    if (match(TokenType::OPEN_PAREN)) {
        Token openToken = m_previous;
        AstExprPtr inner = parseExpression();
        if (!inner) {
            return nullptr;
        }
        if (!consume(TokenType::CLOSE_PAREN)) {
            return nullptr;
        }
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(combineSourceSpans(openToken.span(), m_previous.span()));
        expr->value = AstGroupingExpr{std::move(inner)};
        return expr;
    }

    if (match(TokenType::TYPE_FN)) {
        return parseFunctionLiteralExpr();
    }

    if (match(TokenType::AT)) {
        return parseImportExpression(m_previous);
    }

    if (match(TokenType::THIS)) {
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(m_previous);
        expr->value = AstThisExpr{m_previous};
        return expr;
    }

    if (match(TokenType::SUPER)) {
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(m_previous);
        expr->value = AstSuperExpr{m_previous};
        return expr;
    }

    if (match(TokenType::OPEN_BRACKET)) {
        Token openToken = m_previous;
        AstArrayLiteralExpr arrayLiteral;
        if (!check(TokenType::CLOSE_BRACKET)) {
            do {
                AstExprPtr element = parseExpression();
                if (!element) {
                    return nullptr;
                }
                arrayLiteral.elements.push_back(std::move(element));
            } while (match(TokenType::COMMA));
        }
        if (!consume(TokenType::CLOSE_BRACKET)) {
            return nullptr;
        }
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(combineSourceSpans(openToken.span(), m_previous.span()));
        expr->value = std::move(arrayLiteral);
        return expr;
    }

    if (match(TokenType::OPEN_CURLY)) {
        Token openToken = m_previous;
        AstDictLiteralExpr dictLiteral;
        if (!check(TokenType::CLOSE_CURLY)) {
            do {
                AstDictEntry entry;
                entry.key = parseExpression();
                if (!entry.key || !consume(TokenType::COLON)) {
                    return nullptr;
                }
                entry.value = parseExpression();
                if (!entry.value) {
                    return nullptr;
                }
                dictLiteral.entries.push_back(std::move(entry));
            } while (match(TokenType::COMMA));
        }
        if (!consume(TokenType::CLOSE_CURLY)) {
            return nullptr;
        }
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(combineSourceSpans(openToken.span(), m_previous.span()));
        expr->value = std::move(dictLiteral);
        return expr;
    }

    if (check(TokenType::IDENTIFIER) || check(TokenType::TYPE) ||
        isTypeToken(m_current.type())) {
        Token nameToken = m_current;
        advance();
        auto expr = std::make_unique<AstExpr>();
        expr->node = makeNodeInfo(nameToken);
        expr->value = AstIdentifierExpr{nameToken};
        return expr;
    }

    errorAtSpan(m_current.span(), "Expected expression.");
    if (!check(TokenType::END_OF_FILE)) {
        advance();
    }
    return nullptr;
}
