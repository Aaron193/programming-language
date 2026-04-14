#include "PackageRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_set>

#include "NativePackage.hpp"
#include "PackageManifest.hpp"
#include "Scanner.hpp"
#include "SyntaxRules.hpp"

namespace {

#if defined(__APPLE__)
constexpr const char* kPackageLibraryFileName = "package.dylib";
#else
constexpr const char* kPackageLibraryFileName = "package.so";
#endif

constexpr const char* kProjectManifestFileName = "mog.toml";
constexpr const char* kProjectLockFileName = "mog.lock";
constexpr const char* kProjectInstallRegistryFileName = ".mog/install/registry.toml";
constexpr const char* kPackageApiFileName = "package.api.mog";

enum class TomlSectionKind {
    NONE,
    PACKAGE,
    EXPORT,
};

std::string trim(std::string_view text) {
    size_t start = 0;
    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::string stripComment(std::string_view line) {
    bool inString = false;
    for (size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"' && (index == 0 || line[index - 1] != '\\')) {
            inString = !inString;
        }
        if (!inString && line[index] == '#') {
            return trim(line.substr(0, index));
        }
    }

    return trim(line);
}

bool parseQuotedString(const std::string& value, std::string& out,
                       std::string& outError) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        outError = "Expected quoted string value.";
        return false;
    }

    out.clear();
    for (size_t index = 1; index + 1 < value.size(); ++index) {
        char ch = value[index];
        if (ch == '\\' && index + 1 < value.size() - 1) {
            ++index;
            char escaped = value[index];
            switch (escaped) {
                case '\\':
                case '"':
                    out.push_back(escaped);
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    outError = "Unsupported escape sequence in string value.";
                    return false;
            }
            continue;
        }
        out.push_back(ch);
    }

    return true;
}

bool parseQuotedStringArray(const std::string& value,
                            std::vector<std::string>& outValues,
                            std::string& outError) {
    outValues.clear();
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        outError = "Expected array value.";
        return false;
    }

    std::string body = trim(std::string_view(value).substr(1, value.size() - 2));
    while (!body.empty()) {
        size_t valueLength = 0;
        bool inString = false;
        bool escaped = false;
        for (; valueLength < body.size(); ++valueLength) {
            const char ch = body[valueLength];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == '"') {
                    inString = false;
                }
                continue;
            }

            if (ch == '"') {
                inString = true;
                continue;
            }
            if (ch == ',') {
                break;
            }
        }

        std::string parsed;
        if (!parseQuotedString(trim(body.substr(0, valueLength)), parsed, outError)) {
            return false;
        }
        outValues.push_back(parsed);

        if (valueLength >= body.size()) {
            body.clear();
        } else {
            body = trim(std::string_view(body).substr(valueLength + 1));
        }
    }

    return true;
}

std::string normalizeRelativePath(std::string pathText) {
    std::replace(pathText.begin(), pathText.end(), '\\', '/');
    return pathText;
}

bool pathIsWithin(const std::filesystem::path& child,
                  const std::filesystem::path& parent) {
    auto childIt = child.begin();
    auto parentIt = parent.begin();
    for (; parentIt != parent.end(); ++parentIt, ++childIt) {
        if (childIt == child.end() || *childIt != *parentIt) {
            return false;
        }
    }
    return true;
}

bool loadWorkspaceMembersFromManifest(const std::filesystem::path& manifestPath,
                                      std::vector<std::string>& outMembers,
                                      std::string& outError) {
    outMembers.clear();
    std::ifstream file(manifestPath);
    if (!file) {
        outError = "Could not open project manifest '" + manifestPath.string() + "'.";
        return false;
    }

    enum class Section {
        ROOT,
        WORKSPACE,
        OTHER,
    };

    Section section = Section::ROOT;
    std::string line;
    while (std::getline(file, line)) {
        const std::string content = stripComment(line);
        if (content.empty()) {
            continue;
        }

        if (content == "[workspace]") {
            section = Section::WORKSPACE;
            continue;
        }
        if (content.front() == '[' && content.back() == ']') {
            section = Section::OTHER;
            continue;
        }
        if (section != Section::WORKSPACE) {
            continue;
        }

        const size_t equals = content.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(std::string_view(content).substr(0, equals));
        const std::string value =
            trim(std::string_view(content).substr(equals + 1));
        if (key != "members") {
            continue;
        }

        if (!parseQuotedStringArray(value, outMembers, outError)) {
            return false;
        }
        for (auto& member : outMembers) {
            member = normalizeRelativePath(member);
        }
        return true;
    }

    return true;
}

std::string canonicalOrEmpty(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::exists(resolved, ec) || ec) {
        return "";
    }
    return resolved.string();
}

std::string canonicalOrLexical(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return path.lexically_normal().string();
    }
    return resolved.string();
}

bool appendRegistryEntry(std::vector<PackageRegistryEntry>& outEntries,
                         PackageRegistryEntry&& entry, std::string& outError) {
    if (entry.importName.empty()) {
        outError = "Package registry entry is missing 'name'.";
        return false;
    }

    for (const auto& existing : outEntries) {
        if (existing.importName == entry.importName) {
            outError = "Duplicate package registry entry for '" +
                       entry.importName + "'.";
            return false;
        }
    }

    outEntries.push_back(std::move(entry));
    return true;
}

bool resolveEntryPaths(const std::string& projectRoot,
                       PackageRegistryEntry& entry,
                       std::string& outError) {
    if (entry.packageDir.empty() && !entry.sourcePath.empty()) {
        entry.packageDir = entry.sourcePath;
    }

    if (entry.packageDir.empty()) {
        outError = "Package registry entry '" + entry.importName +
                   "' is missing 'package_dir'.";
        return false;
    }

    const std::filesystem::path packageDir =
        std::filesystem::path(projectRoot) / entry.packageDir;
    entry.packageDir = canonicalOrLexical(packageDir);

    if (entry.apiPath.empty()) {
        entry.apiPath = (packageDir / kPackageApiFileName).string();
    } else {
        entry.apiPath =
            canonicalOrLexical(std::filesystem::path(projectRoot) / entry.apiPath);
    }

    if (entry.kind == "source") {
        if (entry.entryPath.empty()) {
            entry.entryPath = (packageDir / "src" / "main.mog").string();
        } else {
            entry.entryPath = canonicalOrLexical(std::filesystem::path(projectRoot) /
                                                 entry.entryPath);
        }
    } else {
        if (entry.libraryPath.empty()) {
            entry.libraryPath =
                (std::filesystem::path(projectRoot) / "build" / "packages" /
                 entry.packageNamespace / entry.packageName /
                 kPackageLibraryFileName)
                    .string();
        } else {
            entry.libraryPath =
                canonicalOrLexical(std::filesystem::path(projectRoot) /
                                   entry.libraryPath);
        }
    }

    if (!entry.sourcePath.empty()) {
        entry.sourcePath =
            canonicalOrLexical(std::filesystem::path(projectRoot) / entry.sourcePath);
    }

    return true;
}

bool loadLockfileEntries(const std::filesystem::path& lockfilePath,
                         std::vector<PackageRegistryEntry>& outEntries,
                         std::string& outError) {
    outEntries.clear();
    outError.clear();

    std::ifstream file(lockfilePath);
    if (!file) {
        outError = "Could not open lockfile '" + lockfilePath.string() + "'.";
        return false;
    }

    TomlSectionKind section = TomlSectionKind::NONE;
    PackageRegistryEntry current;
    bool hasCurrent = false;
    std::string line;
    size_t lineNumber = 0;

    auto flushCurrent = [&]() -> bool {
        if (!hasCurrent) {
            return true;
        }
        if (current.packageId.empty() &&
            !current.packageNamespace.empty() &&
            !current.packageName.empty()) {
            current.packageId =
                makePackageId(current.packageNamespace, current.packageName);
        }
        if (!appendRegistryEntry(outEntries, std::move(current), outError)) {
            return false;
        }
        current = PackageRegistryEntry{};
        hasCurrent = false;
        return true;
    };

    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string content = stripComment(line);
        if (content.empty()) {
            continue;
        }

        if (content == "[[package]]") {
            if (!flushCurrent()) {
                outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                           ": " + outError;
                return false;
            }
            section = TomlSectionKind::PACKAGE;
            hasCurrent = true;
            continue;
        }

        const size_t equals = content.find('=');
        if (equals == std::string::npos) {
            outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                       ": expected key = value.";
            return false;
        }

        const std::string key = trim(std::string_view(content).substr(0, equals));
        const std::string value =
            trim(std::string_view(content).substr(equals + 1));

        if (section == TomlSectionKind::NONE) {
            if (key == "schema_version") {
                continue;
            }
            outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                       ": entries must appear inside [[package]].";
            return false;
        }

        if (section != TomlSectionKind::PACKAGE || !hasCurrent) {
            outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                       ": entries must appear inside [[package]].";
            return false;
        }

        std::string parsed;
        if (key == "dependencies") {
            if (!parseQuotedStringArray(value, current.dependencyIds, outError)) {
                outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                           ": " + outError;
                return false;
            }
            continue;
        }
        if (key == "dependency_groups") {
            if (!parseQuotedStringArray(value, current.dependencyGroups,
                                        outError)) {
                outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                           ": " + outError;
                return false;
            }
            continue;
        }

        if (!parseQuotedString(value, parsed, outError)) {
            outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                       ": " + outError;
            return false;
        }

        if (key == "name") {
            current.importName = parsed;
        } else if (key == "package_id") {
            current.packageId = parsed;
        } else if (key == "namespace") {
            current.packageNamespace = parsed;
        } else if (key == "package_name") {
            current.packageName = parsed;
        } else if (key == "version") {
            current.version = parsed;
        } else if (key == "kind") {
            current.kind = parsed;
        } else if (key == "package_dir") {
            current.packageDir = parsed;
        } else if (key == "entry") {
            current.entryPath = parsed;
        } else if (key == "library") {
            current.libraryPath = parsed;
        } else if (key == "api") {
            current.apiPath = parsed;
        } else if (key == "description") {
            current.description = parsed;
        } else if (key == "source_type") {
            current.sourceType = parsed;
        } else if (key == "source_path") {
            current.sourcePath = parsed;
        } else if (key == "manifest_digest") {
            current.manifestDigest = parsed;
        } else if (key == "api_digest") {
            current.apiDigest = parsed;
        }
    }

    return flushCurrent();
}

bool fileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

bool loadPackageManifestEntry(const std::filesystem::path& packageDir,
                              PackageRegistryEntry& outEntry,
                              std::string& outError) {
    PackageManifest manifest;
    if (!loadPackageManifest(packageDir.string(), manifest, outError)) {
        return false;
    }

    outEntry = PackageRegistryEntry{};
    outEntry.importName = manifest.importName.empty() ? manifest.packageName
                                                      : manifest.importName;
    outEntry.packageNamespace = manifest.packageNamespace;
    outEntry.packageName = manifest.packageName;
    outEntry.packageId =
        makePackageId(outEntry.packageNamespace, outEntry.packageName);
    outEntry.version = manifest.version;
    outEntry.packageDir = canonicalOrLexical(packageDir);
    outEntry.kind = manifest.kind.empty() ? "native" : manifest.kind;
    outEntry.description = manifest.description;
    outEntry.dependencyIds = manifest.dependencies;
    outEntry.sourceType = "path";
    outEntry.sourcePath = outEntry.packageDir;
    if (!manifest.entry.empty()) {
        outEntry.entryPath = canonicalOrLexical(packageDir / manifest.entry);
    }
    if (!manifest.library.empty()) {
        outEntry.libraryPath = canonicalOrLexical(packageDir / manifest.library);
    }

    const std::filesystem::path apiPath = packageDir / kPackageApiFileName;
    if (fileExists(apiPath.string())) {
        outEntry.apiPath = canonicalOrLexical(apiPath);
    }

    return true;
}

bool scanPackageRootForEntry(const std::filesystem::path& root,
                             std::string_view rawSpecifier,
                             PackageRegistryEntry& outEntry) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        return false;
    }

    for (const auto& namespaceEntry : std::filesystem::directory_iterator(root, ec)) {
        if (ec || !namespaceEntry.is_directory()) {
            continue;
        }

        const std::filesystem::path packageDir =
            namespaceEntry.path() / std::string(rawSpecifier);
        std::string loadError;
        if (!loadPackageManifestEntry(packageDir, outEntry, loadError)) {
            continue;
        }
        return true;
    }

    return false;
}

struct ParsedPackageApiType {
    TypeRef type;
    std::string text;
    SourceSpan span = makePointSpan(1, 1);
};

class PackageApiParser {
   public:
    PackageApiParser(std::string_view source, std::string apiPath,
                     std::string packageId, std::string importName)
        : m_scanner(source),
          m_apiPath(std::move(apiPath)),
          m_packageId(std::move(packageId)),
          m_importName(std::move(importName)) {
        advance();
    }

    bool parse(PackageApiMetadata& outMetadata, std::string& outError) {
        outMetadata = PackageApiMetadata{};
        outError.clear();

        if (!checkIdentifier("package")) {
            outError = "Package API must start with 'package <name>'.";
            return false;
        }
        advance();
        if (!check(TokenType::IDENTIFIER)) {
            outError = "Expected package name after 'package'.";
            return false;
        }

        outMetadata.packageName = tokenText(m_current);
        if (outMetadata.packageName != m_importName) {
            outError = "Package API declares package '" + outMetadata.packageName +
                       "' but manifest import name is '" + m_importName + "'.";
            return false;
        }
        advance();

        while (!check(TokenType::END_OF_FILE)) {
            if (!parseDeclaration(outMetadata, outError)) {
                return false;
            }
        }

        return true;
    }

   private:
    Scanner m_scanner;
    Token m_current;
    Token m_previous;
    std::deque<Token> m_bufferedTokens;
    std::string m_apiPath;
    std::string m_packageId;
    std::string m_importName;
    std::unordered_map<std::string, TypeRef> m_opaqueTypes;

    void advance() {
        m_previous = m_current;
        if (!m_bufferedTokens.empty()) {
            m_current = m_bufferedTokens.front();
            m_bufferedTokens.pop_front();
            return;
        }
        m_current = m_scanner.nextToken();
    }

    const Token& tokenAt(size_t offset) {
        if (offset == 0) {
            return m_current;
        }
        while (m_bufferedTokens.size() < offset) {
            m_bufferedTokens.push_back(m_scanner.nextToken());
        }
        return m_bufferedTokens[offset - 1];
    }

    bool check(TokenType type) const { return m_current.type() == type; }

    bool match(TokenType type) {
        if (!check(type)) {
            return false;
        }
        advance();
        return true;
    }

    bool checkIdentifier(std::string_view expected) const {
        return m_current.type() == TokenType::IDENTIFIER &&
               tokenText(m_current) == expected;
    }

    std::string tokenText(const Token& token) const {
        return tokenLexeme(token);
    }

    bool consume(TokenType type, std::string_view description,
                 std::string& outError) {
        if (check(type)) {
            advance();
            return true;
        }
        outError = "Expected " + std::string(description) + ".";
        return false;
    }

    bool parseAnnotation(std::string& outName, std::string& outValue,
                         SourceSpan& outSpan, std::string& outError) {
        Token atToken = m_current;
        if (!consume(TokenType::AT, "'@'", outError)) {
            return false;
        }
        if (!check(TokenType::IDENTIFIER)) {
            outError = "Expected annotation name after '@'.";
            return false;
        }
        outName = tokenText(m_current);
        advance();
        if (!consume(TokenType::OPEN_PAREN, "'('", outError)) {
            return false;
        }
        if (!check(TokenType::STRING)) {
            outError = "Expected string literal annotation argument.";
            return false;
        }
        const std::string literal = tokenText(m_current);
        outValue =
            literal.size() >= 2 ? literal.substr(1, literal.size() - 2) : "";
        advance();
        if (!consume(TokenType::CLOSE_PAREN, "')'", outError)) {
            return false;
        }
        outSpan = combineSourceSpans(atToken.span(), m_previous.span());
        return true;
    }

    ParsedPackageApiType parseTypeExpr(std::string& outError) {
        ParsedPackageApiType parsed;
        Token startToken = m_current;

        auto applyOptionalSuffix = [&]() {
            while (check(TokenType::QUESTION)) {
                Token questionToken = m_current;
                advance();
                parsed.type = TypeInfo::makeOptional(parsed.type);
                parsed.text += "?";
                parsed.span =
                    combineSourceSpans(parsed.span, questionToken.span());
            }
        };

        if (check(TokenType::TYPE_FN)) {
            advance();
            if (!consume(TokenType::OPEN_PAREN, "'('", outError)) {
                return {};
            }

            std::vector<TypeRef> params;
            std::vector<std::string> paramTexts;
            if (!check(TokenType::CLOSE_PAREN)) {
                while (true) {
                    ParsedPackageApiType paramType = parseTypeExpr(outError);
                    if (!paramType.type) {
                        return {};
                    }
                    params.push_back(paramType.type);
                    paramTexts.push_back(paramType.text);
                    if (!match(TokenType::COMMA)) {
                        break;
                    }
                }
            }
            if (!consume(TokenType::CLOSE_PAREN, "')'", outError)) {
                return {};
            }

            ParsedPackageApiType returnType = parseTypeExpr(outError);
            if (!returnType.type) {
                return {};
            }

            parsed.type = TypeInfo::makeFunction(std::move(params), returnType.type);
            parsed.text = "fn(";
            for (size_t index = 0; index < paramTexts.size(); ++index) {
                if (index != 0) {
                    parsed.text += ", ";
                }
                parsed.text += paramTexts[index];
            }
            parsed.text += ") ";
            parsed.text += returnType.text;
            parsed.span = combineSourceSpans(startToken.span(), returnType.span);
            applyOptionalSuffix();
            return parsed;
        }

        switch (m_current.type()) {
            case TokenType::TYPE_I8:
                parsed.type = TypeInfo::makeI8();
                break;
            case TokenType::TYPE_I16:
                parsed.type = TypeInfo::makeI16();
                break;
            case TokenType::TYPE_I32:
                parsed.type = TypeInfo::makeI32();
                break;
            case TokenType::TYPE_I64:
                parsed.type = TypeInfo::makeI64();
                break;
            case TokenType::TYPE_U8:
                parsed.type = TypeInfo::makeU8();
                break;
            case TokenType::TYPE_U16:
                parsed.type = TypeInfo::makeU16();
                break;
            case TokenType::TYPE_U32:
                parsed.type = TypeInfo::makeU32();
                break;
            case TokenType::TYPE_U64:
                parsed.type = TypeInfo::makeU64();
                break;
            case TokenType::TYPE_USIZE:
                parsed.type = TypeInfo::makeUSize();
                break;
            case TokenType::TYPE_F32:
                parsed.type = TypeInfo::makeF32();
                break;
            case TokenType::TYPE_F64:
                parsed.type = TypeInfo::makeF64();
                break;
            case TokenType::TYPE_BOOL:
                parsed.type = TypeInfo::makeBool();
                break;
            case TokenType::TYPE_STR:
                parsed.type = TypeInfo::makeStr();
                break;
            case TokenType::TYPE_VOID:
                parsed.type = TypeInfo::makeVoid();
                break;
            case TokenType::TYPE_NULL_KW:
                parsed.type = TypeInfo::makeNull();
                break;
            case TokenType::IDENTIFIER:
                break;
            default:
                outError = "Expected type expression.";
                return {};
        }

        if (parsed.type) {
            parsed.text = tokenText(m_current);
            parsed.span = m_current.span();
            advance();
            applyOptionalSuffix();
            return parsed;
        }

        Token nameToken = m_current;
        const std::string name = tokenText(nameToken);
        advance();

        if (isCollectionTypeNameText(name)) {
            if (!consume(TokenType::LESS, "'<'", outError)) {
                return {};
            }
            if (name == "Array" || name == "Set") {
                ParsedPackageApiType elementType = parseTypeExpr(outError);
                if (!elementType.type ||
                    !consume(TokenType::GREATER, "'>'", outError)) {
                    return {};
                }
                parsed.type = name == "Array" ? TypeInfo::makeArray(elementType.type)
                                               : TypeInfo::makeSet(elementType.type);
                parsed.text = name + "<" + elementType.text + ">";
                parsed.span =
                    combineSourceSpans(nameToken.span(), m_previous.span());
                applyOptionalSuffix();
                return parsed;
            }

            ParsedPackageApiType keyType = parseTypeExpr(outError);
            if (!keyType.type || !consume(TokenType::COMMA, "','", outError)) {
                return {};
            }
            ParsedPackageApiType valueType = parseTypeExpr(outError);
            if (!valueType.type ||
                !consume(TokenType::GREATER, "'>'", outError)) {
                return {};
            }
            parsed.type = TypeInfo::makeDict(keyType.type, valueType.type);
            parsed.text = name + "<" + keyType.text + ", " + valueType.text + ">";
            parsed.span = combineSourceSpans(nameToken.span(), m_previous.span());
            applyOptionalSuffix();
            return parsed;
        }

        if (match(TokenType::DOT)) {
            outError =
                "Package API declarations must use package-local type names, not "
                "qualified names.";
            return {};
        }

        auto opaqueIt = m_opaqueTypes.find(name);
        if (opaqueIt == m_opaqueTypes.end()) {
            outError = "Unknown package API type '" + name + "'.";
            return {};
        }

        parsed.type = opaqueIt->second;
        parsed.text = name;
        parsed.span = nameToken.span();
        applyOptionalSuffix();
        return parsed;
    }

    bool parseOpaqueType(PackageApiMetadata& outMetadata, const std::string& doc,
                         const std::string& nativeHandleTypeName,
                         std::string& outError) {
        Token opaqueToken = m_current;
        advance();
        if (!check(TokenType::TYPE)) {
            outError = "Expected 'type' after 'opaque'.";
            return false;
        }
        advance();
        if (!check(TokenType::IDENTIFIER)) {
            outError = "Expected opaque type name.";
            return false;
        }

        Token nameToken = m_current;
        const std::string name = tokenText(nameToken);
        advance();

        if (nativeHandleTypeName.empty()) {
            outError = "Opaque type '" + name +
                       "' must declare @native_handle(\"...\").";
            return false;
        }
        if (!isValidHandleTypeName(nativeHandleTypeName)) {
            outError = "Opaque type '" + name +
                       "' has invalid @native_handle name '" +
                       nativeHandleTypeName + "'.";
            return false;
        }

        TypeRef type = TypeInfo::makeNativeHandle(m_packageId,
                                                  nativeHandleTypeName,
                                                  m_importName, name);
        m_opaqueTypes[name] = type;
        outMetadata.typeExports[name] = PackageApiOpaqueType{
            type,
            doc,
            nativeHandleTypeName,
            combineSourceSpans(opaqueToken.span(), nameToken.span()),
            nameToken.span(),
        };
        return true;
    }

    bool parseConstDecl(PackageApiMetadata& outMetadata, const std::string& doc,
                        std::string& outError) {
        Token constToken = m_current;
        advance();
        if (!check(TokenType::IDENTIFIER)) {
            outError = "Expected constant name after 'const'.";
            return false;
        }

        Token nameToken = m_current;
        const std::string name = tokenText(nameToken);
        advance();

        ParsedPackageApiType type = parseTypeExpr(outError);
        if (!type.type) {
            return false;
        }

        outMetadata.valueExports[name] = PackageApiExport{
            type.type,
            doc,
            "constant",
            {},
            "",
            combineSourceSpans(constToken.span(), type.span),
            nameToken.span(),
        };
        return true;
    }

    bool parseFunctionDecl(PackageApiMetadata& outMetadata, const std::string& doc,
                           std::string& outError) {
        Token fnToken = m_current;
        advance();
        if (!check(TokenType::IDENTIFIER)) {
            outError = "Expected function name after 'fn'.";
            return false;
        }

        Token nameToken = m_current;
        const std::string name = tokenText(nameToken);
        advance();

        if (!consume(TokenType::OPEN_PAREN, "'('", outError)) {
            return false;
        }

        std::vector<TypeRef> params;
        std::vector<std::string> parameterLabels;
        if (!check(TokenType::CLOSE_PAREN)) {
            while (true) {
                if (!check(TokenType::IDENTIFIER)) {
                    outError = "Expected parameter name in function declaration.";
                    return false;
                }
                const std::string parameterName = tokenText(m_current);
                advance();

                ParsedPackageApiType paramType = parseTypeExpr(outError);
                if (!paramType.type) {
                    return false;
                }
                params.push_back(paramType.type);
                parameterLabels.push_back(parameterName + " " + paramType.text);
                if (!match(TokenType::COMMA)) {
                    break;
                }
            }
        }

        if (!consume(TokenType::CLOSE_PAREN, "')'", outError)) {
            return false;
        }

        ParsedPackageApiType returnType = parseTypeExpr(outError);
        if (!returnType.type) {
            return false;
        }

        outMetadata.valueExports[name] = PackageApiExport{
            TypeInfo::makeFunction(std::move(params), returnType.type),
            doc,
            "function",
            std::move(parameterLabels),
            returnType.text,
            combineSourceSpans(fnToken.span(), returnType.span),
            nameToken.span(),
        };
        return true;
    }

    bool parseDeclaration(PackageApiMetadata& outMetadata,
                          std::string& outError) {
        std::string doc;
        std::string nativeHandleTypeName;
        while (check(TokenType::AT)) {
            std::string annotationName;
            std::string annotationValue;
            SourceSpan annotationSpan = makePointSpan(1, 1);
            if (!parseAnnotation(annotationName, annotationValue, annotationSpan,
                                 outError)) {
                return false;
            }

            if (annotationName == "doc") {
                doc = annotationValue;
            } else if (annotationName == "native_handle") {
                nativeHandleTypeName = annotationValue;
            } else {
                outError = "Unsupported package API annotation '@" +
                           annotationName + "'.";
                return false;
            }
        }

        if (checkIdentifier("opaque")) {
            return parseOpaqueType(outMetadata, doc, nativeHandleTypeName,
                                   outError);
        }
        if (check(TokenType::CONST)) {
            if (!nativeHandleTypeName.empty()) {
                outError = "@native_handle can only annotate opaque type declarations.";
                return false;
            }
            return parseConstDecl(outMetadata, doc, outError);
        }
        if (check(TokenType::TYPE_FN)) {
            if (!nativeHandleTypeName.empty()) {
                outError = "@native_handle can only annotate opaque type declarations.";
                return false;
            }
            return parseFunctionDecl(outMetadata, doc, outError);
        }

        outError = "Unsupported package API declaration starting at '" +
                   tokenText(m_current) + "'.";
        return false;
    }
};

}  // namespace

std::string packageImportNameFromId(std::string_view packageId) {
    const size_t colon = packageId.rfind(':');
    if (colon == std::string::npos) {
        return std::string(packageId);
    }
    return std::string(packageId.substr(colon + 1));
}

bool findProjectRootForPackages(const std::string& importerPath,
                                std::string& outProjectRoot) {
    outProjectRoot.clear();

    std::vector<std::filesystem::path> starts;
    if (!importerPath.empty()) {
        std::filesystem::path importer(importerPath);
        std::error_code importerEc;
        if (std::filesystem::is_directory(importer, importerEc) && !importerEc) {
            starts.push_back(importer);
        } else {
            starts.push_back(importer.parent_path());
        }
    }
    starts.push_back(std::filesystem::current_path());

    std::unordered_set<std::string> visited;
    for (std::filesystem::path current : starts) {
        std::error_code ec;
        current = std::filesystem::weakly_canonical(current, ec);
        if (ec) {
            current = current.lexically_normal();
        }

        const std::filesystem::path startPath = current;
        std::string nearestProjectRoot;
        while (!current.empty()) {
            const std::string normalized = current.string();
            if (!visited.insert(normalized).second) {
                break;
            }

            const std::filesystem::path manifestPath =
                current / kProjectManifestFileName;
            if (fileExists(manifestPath.string())) {
                if (nearestProjectRoot.empty()) {
                    nearestProjectRoot = normalized;
                }

                std::vector<std::string> workspaceMembers;
                std::string workspaceError;
                if (loadWorkspaceMembersFromManifest(manifestPath, workspaceMembers,
                                                     workspaceError) &&
                    !workspaceMembers.empty()) {
                    for (const std::string& member : workspaceMembers) {
                        const std::filesystem::path memberPath =
                            current / member;
                        const std::string canonicalMember =
                            canonicalOrEmpty(memberPath);
                        if (canonicalMember.empty()) {
                            continue;
                        }
                        if (pathIsWithin(startPath,
                                         std::filesystem::path(canonicalMember))) {
                            outProjectRoot = normalized;
                            return true;
                        }
                    }

                    if (pathIsWithin(startPath, current)) {
                        outProjectRoot = normalized;
                        return true;
                    }
                }
            }

            if (current == current.root_path()) {
                break;
            }
            current = current.parent_path();
        }

        if (!nearestProjectRoot.empty()) {
            outProjectRoot = nearestProjectRoot;
            return true;
        }
    }

    return false;
}

bool loadProjectPackageRegistry(const std::string& projectRoot,
                                std::vector<PackageRegistryEntry>& outEntries,
                                std::string& outError) {
    outEntries.clear();
    outError.clear();

    const std::filesystem::path installRegistryPath =
        std::filesystem::path(projectRoot) / kProjectInstallRegistryFileName;
    if (fileExists(installRegistryPath.string())) {
        if (!loadLockfileEntries(installRegistryPath, outEntries, outError)) {
            return false;
        }
    } else {
        const std::filesystem::path lockfilePath =
            std::filesystem::path(projectRoot) / kProjectLockFileName;
        if (fileExists(lockfilePath.string())) {
            if (!loadLockfileEntries(lockfilePath, outEntries, outError)) {
                return false;
            }
        } else {
            outError = "Project package install metadata is missing at '" +
                       installRegistryPath.string() + "'. Run 'mog install'.";
            return false;
        }
    }

    for (auto& entry : outEntries) {
        if (!resolveEntryPaths(projectRoot, entry, outError)) {
            return false;
        }
    }

    return true;
}

bool loadProjectLockfile(const std::string& projectRoot,
                         std::vector<PackageRegistryEntry>& outEntries,
                         std::string& outError) {
    outEntries.clear();
    outError.clear();

    const std::filesystem::path lockfilePath =
        std::filesystem::path(projectRoot) / kProjectLockFileName;
    if (!fileExists(lockfilePath.string())) {
        outError = "Project lockfile is missing at '" + lockfilePath.string() +
                   "'. Run 'mog install' first.";
        return false;
    }

    if (!loadLockfileEntries(lockfilePath, outEntries, outError)) {
        return false;
    }

    for (auto& entry : outEntries) {
        if (!resolveEntryPaths(projectRoot, entry, outError)) {
            return false;
        }
    }

    return true;
}

bool resolvePackageRegistryEntry(
    const std::string& importerPath, std::string_view rawSpecifier,
    const std::vector<std::string>& packageSearchPaths,
    PackageRegistryEntry& outEntry, std::string& outError) {
    outEntry = PackageRegistryEntry{};
    outError.clear();

    if (rawSpecifier.find(':') != std::string_view::npos) {
        outError = "Package imports must use bare names like 'window', not '" +
                   std::string(rawSpecifier) + "'.";
        return false;
    }

    std::string projectRoot;
    if (findProjectRootForPackages(importerPath, projectRoot)) {
        std::vector<PackageRegistryEntry> entries;
        std::string registryError;
        if (loadProjectPackageRegistry(projectRoot, entries, registryError)) {
            for (const auto& entry : entries) {
                if (entry.importName == rawSpecifier) {
                    outEntry = entry;
                    return true;
                }
            }
        } else if (registryError.find("Project package install metadata is missing") ==
                   std::string::npos) {
            outError = registryError;
            return false;
        }
    }

    for (const auto& root : normalizePackageSearchPaths(packageSearchPaths, importerPath)) {
        if (scanPackageRootForEntry(root, rawSpecifier, outEntry)) {
            return true;
        }
    }

    outError = "Cannot find package '" + std::string(rawSpecifier) + "'.";
    return false;
}

bool resolveHandlePackageId(const std::string& importerPath,
                            std::string_view rawSpecifier,
                            const std::vector<std::string>& packageSearchPaths,
                            std::string& outPackageId,
                            std::string& outPackageNamespace,
                            std::string& outPackageName,
                            std::string& outError) {
    outPackageId.clear();
    outPackageNamespace.clear();
    outPackageName.clear();
    outError.clear();

    if (rawSpecifier.find(':') != std::string_view::npos) {
        outError =
            "Handle types must use bare package names like "
            "handle<window:WindowHandle>.";
        return false;
    }

    if (!isValidPackageIdPart(rawSpecifier)) {
        outError = "Handle type must use a lowercase package import name.";
        return false;
    }

    PackageRegistryEntry entry;
    if (!resolvePackageRegistryEntry(importerPath, rawSpecifier, packageSearchPaths,
                                     entry, outError)) {
        return false;
    }

    outPackageId = entry.packageId;
    outPackageNamespace = entry.packageNamespace;
    outPackageName = entry.packageName;
    return true;
}

bool loadPackageApiMetadata(const std::string& apiPath,
                            const std::string& packageId,
                            const std::string& importName,
                            PackageApiMetadata& outMetadata,
                            std::string& outError) {
    outMetadata = PackageApiMetadata{};
    outError.clear();

    std::ifstream file(apiPath);
    if (!file) {
        outError = "Could not open package API file '" + apiPath + "'.";
        return false;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    return parsePackageApiMetadata(source, apiPath, packageId, importName,
                                   outMetadata, outError);
}

bool parsePackageApiMetadata(std::string_view source,
                             const std::string& apiPath,
                             const std::string& packageId,
                             const std::string& importName,
                             PackageApiMetadata& outMetadata,
                             std::string& outError) {
    PackageApiParser parser(source, apiPath, packageId, importName);
    return parser.parse(outMetadata, outError);
}

namespace {

bool packageTypesEqual(const TypeRef& lhs, const TypeRef& rhs) {
    if (!lhs || !rhs || lhs->kind != rhs->kind) {
        return false;
    }

    switch (lhs->kind) {
        case TypeKind::FUNCTION:
            if (lhs->paramTypes.size() != rhs->paramTypes.size() ||
                !packageTypesEqual(lhs->returnType, rhs->returnType)) {
                return false;
            }
            for (size_t index = 0; index < lhs->paramTypes.size(); ++index) {
                if (!packageTypesEqual(lhs->paramTypes[index], rhs->paramTypes[index])) {
                    return false;
                }
            }
            return true;
        case TypeKind::ARRAY:
        case TypeKind::SET:
            return packageTypesEqual(lhs->elementType, rhs->elementType);
        case TypeKind::DICT:
            return packageTypesEqual(lhs->keyType, rhs->keyType) &&
                   packageTypesEqual(lhs->valueType, rhs->valueType);
        case TypeKind::OPTIONAL:
            return packageTypesEqual(lhs->innerType, rhs->innerType);
        case TypeKind::CLASS:
            return lhs->className == rhs->className;
        case TypeKind::NATIVE_HANDLE:
            return lhs->nativeHandlePackageId == rhs->nativeHandlePackageId &&
                   lhs->nativeHandleTypeName == rhs->nativeHandleTypeName;
        default:
            return true;
    }
}

}  // namespace

bool validateNativePackageApi(const PackageApiMetadata& api,
                              const NativePackageDescriptor& descriptor,
                              std::string& outError) {
    outError.clear();

    for (const auto& [name, valueExport] : api.valueExports) {
        const auto descriptorIt = descriptor.exportTypes.find(name);
        if (descriptorIt == descriptor.exportTypes.end()) {
            outError = "Package API declares export '" + name +
                       "' that native package '" + descriptor.packageId +
                       "' does not provide.";
            return false;
        }
        if (!packageTypesEqual(valueExport.type, descriptorIt->second)) {
            outError = "Package API export '" + name +
                       "' type mismatch: declaration uses '" +
                       valueExport.type->toString() + "' but native package uses '" +
                       descriptorIt->second->toString() + "'.";
            return false;
        }
    }

    for (const auto& function : descriptor.functions) {
        if (api.valueExports.find(function.name) == api.valueExports.end()) {
            outError = "Native package '" + descriptor.packageId +
                       "' exports function '" + function.name +
                       "' but package API does not declare it.";
            return false;
        }
    }

    for (const auto& constant : descriptor.constants) {
        if (api.valueExports.find(constant.name) == api.valueExports.end()) {
            outError = "Native package '" + descriptor.packageId +
                       "' exports constant '" + constant.name +
                       "' but package API does not declare it.";
            return false;
        }
    }

    return true;
}
