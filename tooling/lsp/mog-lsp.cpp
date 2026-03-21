#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "tooling/FrontendTooling.hpp"

namespace {

struct JsonValue;
using JsonObject = std::unordered_map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    using Variant =
        std::variant<std::nullptr_t, bool, double, std::string, JsonObject,
                     JsonArray>;

    Variant value = nullptr;

    JsonValue() = default;
    explicit JsonValue(std::nullptr_t) : value(nullptr) {}
    explicit JsonValue(bool boolean) : value(boolean) {}
    explicit JsonValue(double number) : value(number) {}
    explicit JsonValue(std::string string) : value(std::move(string)) {}
    explicit JsonValue(JsonObject object) : value(std::move(object)) {}
    explicit JsonValue(JsonArray array) : value(std::move(array)) {}
};

class JsonParser {
   public:
    explicit JsonParser(std::string_view text) : m_text(text) {}

    bool parse(JsonValue& outValue, std::string& outError) {
        skipWhitespace();
        if (!parseValue(outValue, outError)) {
            return false;
        }

        skipWhitespace();
        if (m_pos != m_text.size()) {
            outError = "Unexpected trailing JSON content.";
            return false;
        }

        return true;
    }

   private:
    std::string_view m_text;
    size_t m_pos = 0;

    void skipWhitespace() {
        while (m_pos < m_text.size() &&
               std::isspace(static_cast<unsigned char>(m_text[m_pos])) != 0) {
            ++m_pos;
        }
    }

    bool parseValue(JsonValue& outValue, std::string& outError) {
        if (m_pos >= m_text.size()) {
            outError = "Unexpected end of JSON.";
            return false;
        }

        const char current = m_text[m_pos];
        if (current == '{') {
            JsonObject object;
            if (!parseObject(object, outError)) {
                return false;
            }
            outValue = JsonValue(std::move(object));
            return true;
        }

        if (current == '[') {
            JsonArray array;
            if (!parseArray(array, outError)) {
                return false;
            }
            outValue = JsonValue(std::move(array));
            return true;
        }

        if (current == '"') {
            std::string string;
            if (!parseString(string, outError)) {
                return false;
            }
            outValue = JsonValue(std::move(string));
            return true;
        }

        if (current == 't' && consumeLiteral("true")) {
            outValue = JsonValue(true);
            return true;
        }

        if (current == 'f' && consumeLiteral("false")) {
            outValue = JsonValue(false);
            return true;
        }

        if (current == 'n' && consumeLiteral("null")) {
            outValue = JsonValue(nullptr);
            return true;
        }

        if (current == '-' || std::isdigit(static_cast<unsigned char>(current))) {
            double number = 0.0;
            if (!parseNumber(number, outError)) {
                return false;
            }
            outValue = JsonValue(number);
            return true;
        }

        outError = "Unsupported JSON token.";
        return false;
    }

    bool parseObject(JsonObject& outObject, std::string& outError) {
        ++m_pos;
        skipWhitespace();

        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }

        while (m_pos < m_text.size()) {
            std::string key;
            if (!parseString(key, outError)) {
                return false;
            }

            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') {
                outError = "Expected ':' in JSON object.";
                return false;
            }

            ++m_pos;
            skipWhitespace();

            JsonValue value;
            if (!parseValue(value, outError)) {
                return false;
            }

            outObject.emplace(std::move(key), std::move(value));

            skipWhitespace();
            if (m_pos >= m_text.size()) {
                outError = "Unterminated JSON object.";
                return false;
            }

            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }

            if (m_text[m_pos] != ',') {
                outError = "Expected ',' in JSON object.";
                return false;
            }

            ++m_pos;
            skipWhitespace();
        }

        outError = "Unterminated JSON object.";
        return false;
    }

    bool parseArray(JsonArray& outArray, std::string& outError) {
        ++m_pos;
        skipWhitespace();

        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }

        while (m_pos < m_text.size()) {
            JsonValue value;
            if (!parseValue(value, outError)) {
                return false;
            }

            outArray.push_back(std::move(value));

            skipWhitespace();
            if (m_pos >= m_text.size()) {
                outError = "Unterminated JSON array.";
                return false;
            }

            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }

            if (m_text[m_pos] != ',') {
                outError = "Expected ',' in JSON array.";
                return false;
            }

            ++m_pos;
            skipWhitespace();
        }

        outError = "Unterminated JSON array.";
        return false;
    }

    bool parseString(std::string& outString, std::string& outError) {
        if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
            outError = "Expected JSON string.";
            return false;
        }

        ++m_pos;
        outString.clear();
        while (m_pos < m_text.size()) {
            const char current = m_text[m_pos++];
            if (current == '"') {
                return true;
            }

            if (current != '\\') {
                outString.push_back(current);
                continue;
            }

            if (m_pos >= m_text.size()) {
                outError = "Invalid JSON escape sequence.";
                return false;
            }

            const char escaped = m_text[m_pos++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    outString.push_back(escaped);
                    break;
                case 'b':
                    outString.push_back('\b');
                    break;
                case 'f':
                    outString.push_back('\f');
                    break;
                case 'n':
                    outString.push_back('\n');
                    break;
                case 'r':
                    outString.push_back('\r');
                    break;
                case 't':
                    outString.push_back('\t');
                    break;
                case 'u':
                    outError = "Unicode escapes are not supported.";
                    return false;
                default:
                    outError = "Invalid JSON escape sequence.";
                    return false;
            }
        }

        outError = "Unterminated JSON string.";
        return false;
    }

    bool parseNumber(double& outNumber, std::string& outError) {
        const size_t start = m_pos;
        if (m_text[m_pos] == '-') {
            ++m_pos;
        }

        while (m_pos < m_text.size() &&
               std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
            ++m_pos;
        }

        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_text.size() &&
                   std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
                ++m_pos;
            }
        }

        if (m_pos < m_text.size() &&
            (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() &&
                (m_text[m_pos] == '+' || m_text[m_pos] == '-')) {
                ++m_pos;
            }
            while (m_pos < m_text.size() &&
                   std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
                ++m_pos;
            }
        }

        std::string numberText(m_text.substr(start, m_pos - start));
        char* endPtr = nullptr;
        errno = 0;
        outNumber = std::strtod(numberText.c_str(), &endPtr);
        if (errno != 0 || endPtr == nullptr || *endPtr != '\0') {
            outError = "Invalid JSON number.";
            return false;
        }

        return true;
    }

    bool consumeLiteral(const char* literal) {
        const size_t length = std::char_traits<char>::length(literal);
        if (m_text.substr(m_pos, length) != literal) {
            return false;
        }
        m_pos += length;
        return true;
    }
};

std::optional<std::reference_wrapper<const JsonObject>> asObject(
    const JsonValue& value) {
    if (const auto* object = std::get_if<JsonObject>(&value.value)) {
        return *object;
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const JsonArray>> asArray(
    const JsonValue& value) {
    if (const auto* array = std::get_if<JsonArray>(&value.value)) {
        return *array;
    }
    return std::nullopt;
}

const JsonValue* getObjectValue(const JsonObject& object, const char* key) {
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::optional<std::string> getStringValue(const JsonObject& object,
                                          const char* key) {
    const JsonValue* value = getObjectValue(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (const auto* string = std::get_if<std::string>(&value->value)) {
        return *string;
    }
    return std::nullopt;
}

std::optional<int> getIntegerValue(const JsonObject& object, const char* key) {
    const JsonValue* value = getObjectValue(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (const auto* number = std::get_if<double>(&value->value)) {
        return static_cast<int>(*number);
    }
    return std::nullopt;
}

std::optional<bool> getBooleanValue(const JsonObject& object, const char* key) {
    const JsonValue* value = getObjectValue(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (const auto* boolean = std::get_if<bool>(&value->value)) {
        return *boolean;
    }
    return std::nullopt;
}

std::string jsonEscape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned int>(
                                      static_cast<unsigned char>(ch)));
                    escaped += buffer;
                } else {
                    escaped.push_back(ch);
                }
                break;
        }
    }
    return escaped;
}

std::string serializeJson(const JsonValue& value);

std::string serializeObject(const JsonObject& object) {
    std::string result = "{";
    bool first = true;
    for (const auto& [key, value] : object) {
        if (!first) {
            result += ",";
        }
        first = false;
        result += "\"";
        result += jsonEscape(key);
        result += "\":";
        result += serializeJson(value);
    }
    result += "}";
    return result;
}

std::string serializeArray(const JsonArray& array) {
    std::string result = "[";
    bool first = true;
    for (const auto& value : array) {
        if (!first) {
            result += ",";
        }
        first = false;
        result += serializeJson(value);
    }
    result += "]";
    return result;
}

std::string serializeJson(const JsonValue& value) {
    if (std::holds_alternative<std::nullptr_t>(value.value)) {
        return "null";
    }
    if (const auto* boolean = std::get_if<bool>(&value.value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* number = std::get_if<double>(&value.value)) {
        std::ostringstream out;
        out << *number;
        return out.str();
    }
    if (const auto* string = std::get_if<std::string>(&value.value)) {
        return "\"" + jsonEscape(*string) + "\"";
    }
    if (const auto* object = std::get_if<JsonObject>(&value.value)) {
        return serializeObject(*object);
    }
    return serializeArray(std::get<JsonArray>(value.value));
}

bool readMessage(std::istream& input, std::string& outPayload) {
    outPayload.clear();
    std::string line;
    size_t contentLength = 0;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            break;
        }

        const std::string prefix = "Content-Length:";
        if (line.rfind(prefix, 0) == 0) {
            contentLength = static_cast<size_t>(
                std::strtoull(line.substr(prefix.size()).c_str(), nullptr, 10));
        }
    }

    if (contentLength == 0) {
        return false;
    }

    outPayload.resize(contentLength);
    input.read(outPayload.data(), static_cast<std::streamsize>(contentLength));
    return input.good() || input.gcount() == static_cast<std::streamsize>(contentLength);
}

void writeMessage(std::ostream& output, const std::string& payload) {
    output << "Content-Length: " << payload.size() << "\r\n\r\n";
    output << payload;
    output.flush();
}

std::string pathToFileUri(const std::string& path) {
    std::string uri = "file://";
    for (unsigned char ch : path) {
        const bool unreserved =
            (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
            ch == '/' || ch == '~';
        if (unreserved) {
            uri.push_back(static_cast<char>(ch));
            continue;
        }

        char buffer[4];
        std::snprintf(buffer, sizeof(buffer), "%%%02X",
                      static_cast<unsigned int>(ch));
        uri += buffer;
    }
    return uri;
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

std::string decodeUriPath(std::string_view uri) {
    constexpr std::string_view prefix = "file://";
    if (uri.substr(0, prefix.size()) != prefix) {
        return std::string(uri);
    }

    std::string_view encodedPath = uri.substr(prefix.size());
    std::string path;
    path.reserve(encodedPath.size());
    for (size_t index = 0; index < encodedPath.size(); ++index) {
        const char ch = encodedPath[index];
        if (ch == '%' && index + 2 < encodedPath.size()) {
            const int hi = hexValue(encodedPath[index + 1]);
            const int lo = hexValue(encodedPath[index + 2]);
            if (hi >= 0 && lo >= 0) {
                path.push_back(static_cast<char>((hi << 4) | lo));
                index += 2;
                continue;
            }
        }
        path.push_back(ch);
    }
    return path;
}

std::string canonicalizePath(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::error_code ec;
    const std::filesystem::path fsPath(path);
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(fsPath, ec);
    if (!ec) {
        return canonical.string();
    }

    const std::filesystem::path absolute = std::filesystem::absolute(fsPath, ec);
    if (!ec) {
        return absolute.string();
    }

    return path;
}

std::optional<std::string> readFileText(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return std::nullopt;
    }

    return buffer.str();
}

std::string lowerCase(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const unsigned char ch : text) {
        lowered.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

bool pathWithinRoot(const std::string& path, const std::string& root) {
    if (root.empty()) {
        return false;
    }

    if (path == root) {
        return true;
    }

    if (!startsWith(path, root)) {
        return false;
    }

    const char boundary = path[root.size()];
    return boundary == '/' || boundary == '\\';
}

bool isHiddenOrBuildDirectory(const std::filesystem::path& path) {
    for (const auto& component : path) {
        const std::string name = component.string();
        if (name.empty() || name == ".") {
            continue;
        }
        if (name == "build") {
            return true;
        }
        if (!name.empty() && name.front() == '.') {
            return true;
        }
    }
    return false;
}

JsonValue makePosition(const ToolingPosition& position) {
    return JsonValue(JsonObject{
        {"line", JsonValue(static_cast<double>(position.line))},
        {"character", JsonValue(static_cast<double>(position.character))},
    });
}

JsonValue makeRange(const ToolingRange& range) {
    return JsonValue(JsonObject{
        {"start", makePosition(range.start)},
        {"end", makePosition(range.end)},
    });
}

JsonValue makeLocation(const std::string& uri, const ToolingRange& range) {
    return JsonValue(JsonObject{
        {"uri", JsonValue(uri)},
        {"range", makeRange(range)},
    });
}

JsonValue makeRelatedInformation(const std::string& uri,
                                 const ToolingDiagnostic& diagnostic) {
    JsonArray items;
    items.reserve(diagnostic.notes.size() + diagnostic.importTrace.size());

    for (const auto& note : diagnostic.notes) {
        items.push_back(JsonValue(JsonObject{
            {"location", makeLocation(uri, note.range)},
            {"message", JsonValue(note.message)},
        }));
    }

    for (const auto& frame : diagnostic.importTrace) {
        std::string message = "while importing '" + frame.rawSpecifier + "'";
        if (!frame.importerPath.empty()) {
            message += " from '" + frame.importerPath + "'";
        }
        if (!frame.resolvedPath.empty() && frame.resolvedPath != frame.rawSpecifier) {
            message += " -> '" + frame.resolvedPath + "'";
        }

        const std::string frameUri = frame.importerPath.empty()
                                         ? uri
                                         : pathToFileUri(frame.importerPath);
        items.push_back(JsonValue(JsonObject{
            {"location", makeLocation(frameUri, frame.range)},
            {"message", JsonValue(std::move(message))},
        }));
    }

    return JsonValue(std::move(items));
}

bool isCompletionIdentifierChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' ||
           ch == '@';
}

size_t offsetForPosition(std::string_view text, const ToolingPosition& position) {
    size_t offset = 0;
    size_t line = 0;
    while (offset < text.size() && line < position.line) {
        if (text[offset++] == '\n') {
            ++line;
        }
    }

    size_t character = 0;
    while (offset < text.size() && character < position.character &&
           text[offset] != '\n') {
        ++offset;
        ++character;
    }

    return offset;
}

size_t completionPrefixStart(std::string_view text,
                             const ToolingPosition& position) {
    size_t offset = offsetForPosition(text, position);
    while (offset > 0 && isCompletionIdentifierChar(text[offset - 1])) {
        --offset;
    }
    return offset;
}

bool isMemberCompletionContext(std::string_view text,
                               const ToolingPosition& position) {
    const size_t prefixStart = completionPrefixStart(text, position);
    return prefixStart > 0 && text[prefixStart - 1] == '.';
}

struct DocumentState {
    std::string uri;
    std::string path;
    std::string text;
    int version = 0;
    ToolingDocumentAnalysis analysis;
};

class MogLspServer {
   public:
    explicit MogLspServer(std::vector<std::string> packageSearchPaths)
        : m_packageSearchPaths(std::move(packageSearchPaths)) {}

    int run() {
        std::string payload;
        while (readMessage(std::cin, payload)) {
            JsonValue message;
            std::string parseError;
            JsonParser parser(payload);
            if (!parser.parse(message, parseError)) {
                continue;
            }

            const auto objectRef = asObject(message);
            if (!objectRef.has_value()) {
                continue;
            }

            handleMessage(objectRef->get());
            if (m_exitRequested) {
                break;
            }
        }

        return m_shutdownRequested ? 0 : 1;
    }

   private:
    std::unordered_map<std::string, DocumentState> m_documents;
    AstFrontendModuleGraphCache m_cache;
    std::vector<std::string> m_packageSearchPaths;
    std::vector<std::string> m_workspaceRoots;
    bool m_shutdownRequested = false;
    bool m_exitRequested = false;

    void handleMessage(const JsonObject& message) {
        const JsonValue* methodValue = getObjectValue(message, "method");
        const std::string* method = methodValue == nullptr
                                        ? nullptr
                                        : std::get_if<std::string>(&methodValue->value);
        if (method == nullptr) {
            return;
        }

        const JsonValue* id = getObjectValue(message, "id");
        const JsonObject* params = nullptr;
        if (const JsonValue* paramsValue = getObjectValue(message, "params")) {
            if (const auto paramsRef = asObject(*paramsValue); paramsRef.has_value()) {
                params = &paramsRef->get();
            }
        }

        if (*method == "initialize") {
            handleInitialize(id, params);
            return;
        }

        if (*method == "initialized") {
            return;
        }

        if (*method == "shutdown") {
            m_shutdownRequested = true;
            if (id != nullptr) {
                sendResponse(*id, JsonValue(nullptr));
            }
            return;
        }

        if (*method == "exit") {
            m_exitRequested = true;
            return;
        }

        if (*method == "textDocument/didOpen" && params != nullptr) {
            handleDidOpen(*params);
            return;
        }

        if (*method == "textDocument/didChange" && params != nullptr) {
            handleDidChange(*params);
            return;
        }

        if (*method == "textDocument/didSave" && params != nullptr) {
            handleDidSave(*params);
            return;
        }

        if (*method == "textDocument/didClose" && params != nullptr) {
            handleDidClose(*params);
            return;
        }

        if (*method == "textDocument/documentSymbol" && params != nullptr &&
            id != nullptr) {
            handleDocumentSymbol(*id, *params);
            return;
        }

        if (*method == "workspace/symbol" && params != nullptr && id != nullptr) {
            handleWorkspaceSymbol(*id, *params);
            return;
        }

        if (*method == "textDocument/definition" && params != nullptr &&
            id != nullptr) {
            handleDefinition(*id, *params);
            return;
        }

        if (*method == "textDocument/references" && params != nullptr &&
            id != nullptr) {
            handleReferences(*id, *params);
            return;
        }

        if (*method == "textDocument/hover" && params != nullptr &&
            id != nullptr) {
            handleHover(*id, *params);
            return;
        }

        if (*method == "textDocument/completion" && params != nullptr &&
            id != nullptr) {
            handleCompletion(*id, *params);
            return;
        }

        if (*method == "textDocument/signatureHelp" && params != nullptr &&
            id != nullptr) {
            handleSignatureHelp(*id, *params);
            return;
        }

        if (*method == "textDocument/prepareRename" && params != nullptr &&
            id != nullptr) {
            handlePrepareRename(*id, *params);
            return;
        }

        if (*method == "textDocument/rename" && params != nullptr && id != nullptr) {
            handleRename(*id, *params);
            return;
        }

        if (id != nullptr) {
            sendResponse(*id, JsonValue(nullptr));
        }
    }

    void handleDidOpen(const JsonObject& params) {
        const JsonValue* documentValue = getObjectValue(params, "textDocument");
        if (documentValue == nullptr) {
            return;
        }
        const auto documentObject = asObject(*documentValue);
        if (!documentObject.has_value()) {
            return;
        }

        auto uri = getStringValue(documentObject->get(), "uri");
        auto text = getStringValue(documentObject->get(), "text");
        if (!uri.has_value() || !text.has_value()) {
            return;
        }

        DocumentState document;
        document.uri = *uri;
        document.path = canonicalizePath(decodeUriPath(*uri));
        document.text = *text;
        document.version = getIntegerValue(documentObject->get(), "version").value_or(0);
        const std::string key = document.uri;
        m_documents[key] = std::move(document);
        analyzeAndPublish(m_documents[key]);
    }

    void handleDidChange(const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            return;
        }

        const JsonValue* changesValue = getObjectValue(params, "contentChanges");
        if (changesValue == nullptr) {
            return;
        }

        const auto changes = asArray(*changesValue);
        if (!changes.has_value() || changes->get().empty()) {
            return;
        }

        const auto firstChangeObject = asObject(changes->get().front());
        if (!firstChangeObject.has_value()) {
            return;
        }

        auto text = getStringValue(firstChangeObject->get(), "text");
        if (!text.has_value()) {
            return;
        }

        documentIt->second.text = *text;
        documentIt->second.version = getTextDocumentVersion(params).value_or(
            documentIt->second.version);
        analyzeAndPublish(documentIt->second);
    }

    void handleDidSave(const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            return;
        }

        analyzeAndPublish(documentIt->second);
    }

    void handleDidClose(const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            return;
        }

        m_documents.erase(*uri);
        sendPublishDiagnostics(*uri, {});
    }

    void handleDocumentSymbol(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        sendResponse(id,
                     makeDocumentSymbolsResponse(documentIt->second.analysis));
    }

    void handleInitialize(const JsonValue* id, const JsonObject* params) {
        m_workspaceRoots.clear();
        if (params != nullptr) {
            configureWorkspaceRoots(*params);
        }
        sendInitializeResponse(id);
    }

    void handleWorkspaceSymbol(const JsonValue& id, const JsonObject& params) {
        const std::string query =
            getStringValue(params, "query").value_or(std::string());

        struct RankedSymbol {
            int rank = 0;
            ToolingWorkspaceSymbol symbol;
            std::string displayPath;
        };

        std::vector<RankedSymbol> ranked;
        const std::string loweredQuery = lowerCase(query);
        for (const auto& path : collectWorkspaceFiles()) {
            const auto analysis = analyzeWorkspaceDocument(path);
            if (!analysis.has_value()) {
                continue;
            }

            for (const auto& symbol : collectWorkspaceSymbolsForTooling(*analysis)) {
                const std::string loweredName = lowerCase(symbol.name);
                const std::string loweredPath = lowerCase(symbol.path);
                int rank = 5;
                if (!loweredQuery.empty()) {
                    if (loweredName == loweredQuery) {
                        rank = 0;
                    } else if (startsWith(loweredName, loweredQuery)) {
                        rank = 1;
                    } else if (loweredName.find(loweredQuery) != std::string::npos) {
                        rank = 2;
                    } else if (startsWith(loweredPath, loweredQuery)) {
                        rank = 3;
                    } else if (loweredPath.find(loweredQuery) != std::string::npos) {
                        rank = 4;
                    } else {
                        continue;
                    }
                }

                ranked.push_back(RankedSymbol{
                    rank,
                    symbol,
                    relativeWorkspacePath(symbol.path),
                });
            }
        }

        std::sort(ranked.begin(), ranked.end(),
                  [](const RankedSymbol& lhs, const RankedSymbol& rhs) {
                      if (lhs.rank != rhs.rank) {
                          return lhs.rank < rhs.rank;
                      }
                      if (lhs.symbol.name != rhs.symbol.name) {
                          return lhs.symbol.name < rhs.symbol.name;
                      }
                      if (lhs.displayPath != rhs.displayPath) {
                          return lhs.displayPath < rhs.displayPath;
                      }
                      if (lhs.symbol.selectionRange.start.line !=
                          rhs.symbol.selectionRange.start.line) {
                          return lhs.symbol.selectionRange.start.line <
                                 rhs.symbol.selectionRange.start.line;
                      }
                      return lhs.symbol.selectionRange.start.character <
                             rhs.symbol.selectionRange.start.character;
                  });

        JsonArray items;
        const size_t limit = std::min<size_t>(ranked.size(), 200);
        items.reserve(limit);
        for (size_t index = 0; index < limit; ++index) {
            JsonObject item;
            item["name"] = JsonValue(ranked[index].symbol.name);
            item["kind"] =
                JsonValue(symbolKindForToolingKind(ranked[index].symbol.kind));
            item["location"] = makeLocation(
                pathToFileUri(ranked[index].symbol.path),
                ranked[index].symbol.selectionRange);
            item["containerName"] = JsonValue(ranked[index].displayPath);
            items.push_back(JsonValue(std::move(item)));
        }

        sendResponse(id, JsonValue(std::move(items)));
    }

    void handleDefinition(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto position = getPosition(params);
        if (!position.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto definition =
            findDefinitionForTooling(documentIt->second.analysis, *position);
        if (!definition.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        sendResponse(id, makeLocation(pathToFileUri(definition->path),
                                      definition->selectionRange));
    }

    void handleReferences(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        const auto position = getPosition(params);
        if (!position.has_value()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        bool includeDeclaration = true;
        if (const JsonValue* contextValue = getObjectValue(params, "context")) {
            if (const auto context = asObject(*contextValue); context.has_value()) {
                includeDeclaration = getBooleanValue(
                                         context->get(), "includeDeclaration")
                                         .value_or(true);
            }
        }

        auto references =
            findReferencesForTooling(documentIt->second.analysis, *position);
        if (!includeDeclaration && !references.empty()) {
            references.erase(references.begin());
        }

        JsonArray items;
        items.reserve(references.size());
        for (const auto& reference : references) {
            items.push_back(
                makeLocation(pathToFileUri(reference.path), reference.selectionRange));
        }
        sendResponse(id, JsonValue(std::move(items)));
    }

    void handleHover(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto position = getPosition(params);
        if (!position.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto hover = findHoverForTooling(documentIt->second.analysis, *position);
        if (!hover.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        JsonObject contents;
        contents["kind"] = JsonValue(std::string("plaintext"));
        contents["value"] = JsonValue(hover->detail);

        JsonObject result;
        result["contents"] = JsonValue(std::move(contents));
        result["range"] = makeRange(hover->range);
        sendResponse(id, JsonValue(std::move(result)));
    }

    void handleCompletion(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        const auto position = getPosition(params);
        if (!position.has_value()) {
            sendResponse(id, JsonValue(JsonArray{}));
            return;
        }

        const bool memberContext =
            isMemberCompletionContext(documentIt->second.text, *position);

        const size_t prefixStart =
            completionPrefixStart(documentIt->second.text, *position);
        const size_t prefixEnd =
            offsetForPosition(documentIt->second.text, *position);
        const std::string prefix = documentIt->second.text.substr(
            prefixStart, prefixEnd - prefixStart);

        const auto completions =
            findCompletionsForTooling(documentIt->second.analysis,
                                      documentIt->second.text, *position);
        JsonArray items;
        for (const auto& completion : completions) {
            if (memberContext && completion.kind != "field" &&
                completion.kind != "method" &&
                completion.kind != "function" &&
                completion.kind != "class" &&
                completion.kind != "constant") {
                continue;
            }
            if (!prefix.empty() && !startsWith(completion.label, prefix)) {
                continue;
            }

            JsonObject item;
            item["label"] = JsonValue(completion.label);
            item["kind"] =
                JsonValue(completionKindForToolingKind(completion.kind));
            if (!completion.detail.empty()) {
                item["detail"] = JsonValue(completion.detail);
            }
            if (!completion.sortText.empty()) {
                item["sortText"] = JsonValue(completion.sortText);
            }
            items.push_back(JsonValue(std::move(item)));
        }

        sendResponse(id, JsonValue(std::move(items)));
    }

    void handleSignatureHelp(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto position = getPosition(params);
        if (!position.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto signatureHelp = findSignatureHelpForTooling(
            documentIt->second.analysis, documentIt->second.text, *position);
        if (!signatureHelp.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        JsonArray signatures;
        for (const auto& signature : signatureHelp->signatures) {
            JsonObject signatureObject;
            signatureObject["label"] = JsonValue(signature.label);

            JsonArray parameters;
            for (const auto& parameter : signature.parameters) {
                JsonObject parameterObject;
                parameterObject["label"] = JsonValue(parameter.label);
                parameters.push_back(JsonValue(std::move(parameterObject)));
            }
            signatureObject["parameters"] = JsonValue(std::move(parameters));
            signatures.push_back(JsonValue(std::move(signatureObject)));
        }

        JsonObject result;
        result["signatures"] = JsonValue(std::move(signatures));
        result["activeSignature"] =
            JsonValue(static_cast<double>(signatureHelp->activeSignature));
        result["activeParameter"] =
            JsonValue(static_cast<double>(signatureHelp->activeParameter));
        sendResponse(id, JsonValue(std::move(result)));
    }

    void handlePrepareRename(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto position = getPosition(params);
        if (!position.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto target =
            prepareRenameForTooling(documentIt->second.analysis, *position);
        if (!target.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        JsonObject result;
        result["range"] = makeRange(target->range);
        result["placeholder"] = JsonValue(target->placeholder);
        sendResponse(id, JsonValue(std::move(result)));
    }

    void handleRename(const JsonValue& id, const JsonObject& params) {
        auto uri = getTextDocumentUri(params);
        if (!uri.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        auto documentIt = m_documents.find(*uri);
        if (documentIt == m_documents.end()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const auto position = getPosition(params);
        if (!position.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        const std::string newName =
            getStringValue(params, "newName").value_or(std::string());
        const auto target =
            prepareRenameForTooling(documentIt->second.analysis, *position);
        if (!target.has_value()) {
            sendResponse(id, JsonValue(nullptr));
            return;
        }

        if (const auto validation = validateRenameForTooling(*target, newName);
            validation.has_value()) {
            sendErrorResponse(id, -32602, *validation);
            return;
        }

        std::vector<ToolingTextEdit> edits =
            findRenameEditsForTooling(documentIt->second.analysis, *target, newName);
        if (target->strategy == "exported") {
            const std::string sourcePath = target->sourcePath;
            for (const auto& path : collectWorkspaceFiles()) {
                if (path == sourcePath) {
                    continue;
                }

                const auto analysis = analyzeWorkspaceDocument(path);
                if (!analysis.has_value()) {
                    continue;
                }

                auto importerEdits =
                    findImportRenameEditsForTooling(*analysis, *target, newName);
                edits.insert(edits.end(), importerEdits.begin(), importerEdits.end());
            }
        }

        sendResponse(id, makeWorkspaceEditResponse(edits));
    }

    std::optional<std::string> getTextDocumentUri(const JsonObject& params) const {
        const JsonValue* documentValue = getObjectValue(params, "textDocument");
        if (documentValue == nullptr) {
            return std::nullopt;
        }

        const auto document = asObject(*documentValue);
        if (!document.has_value()) {
            return std::nullopt;
        }

        return getStringValue(document->get(), "uri");
    }

    std::optional<int> getTextDocumentVersion(const JsonObject& params) const {
        const JsonValue* documentValue = getObjectValue(params, "textDocument");
        if (documentValue == nullptr) {
            return std::nullopt;
        }

        const auto document = asObject(*documentValue);
        if (!document.has_value()) {
            return std::nullopt;
        }

        return getIntegerValue(document->get(), "version");
    }

    std::optional<ToolingPosition> getPosition(const JsonObject& params) const {
        const JsonValue* positionValue = getObjectValue(params, "position");
        if (positionValue == nullptr) {
            return std::nullopt;
        }

        const auto positionObject = asObject(*positionValue);
        if (!positionObject.has_value()) {
            return std::nullopt;
        }

        return ToolingPosition{
            static_cast<size_t>(
                getIntegerValue(positionObject->get(), "line").value_or(0)),
            static_cast<size_t>(getIntegerValue(positionObject->get(),
                                                "character")
                                    .value_or(0)),
        };
    }

    void configureWorkspaceRoots(const JsonObject& params) {
        std::unordered_set<std::string> seen;

        if (const JsonValue* foldersValue = getObjectValue(params, "workspaceFolders")) {
            if (const auto folders = asArray(*foldersValue); folders.has_value()) {
                for (const auto& folderValue : folders->get()) {
                    const auto folder = asObject(folderValue);
                    if (!folder.has_value()) {
                        continue;
                    }

                    const auto uri = getStringValue(folder->get(), "uri");
                    if (!uri.has_value()) {
                        continue;
                    }

                    const std::string path =
                        canonicalizePath(decodeUriPath(*uri));
                    if (seen.insert(path).second) {
                        m_workspaceRoots.push_back(path);
                    }
                }
            }
        }

        if (m_workspaceRoots.empty()) {
            if (const auto rootUri = getStringValue(params, "rootUri");
                rootUri.has_value()) {
                const std::string path =
                    canonicalizePath(decodeUriPath(*rootUri));
                if (seen.insert(path).second) {
                    m_workspaceRoots.push_back(path);
                }
            } else if (const auto rootPath = getStringValue(params, "rootPath");
                       rootPath.has_value()) {
                const std::string path = canonicalizePath(*rootPath);
                if (seen.insert(path).second) {
                    m_workspaceRoots.push_back(path);
                }
            }
        }
    }

    const DocumentState* findOpenDocumentByPath(const std::string& path) const {
        for (const auto& [uri, document] : m_documents) {
            (void)uri;
            if (document.path == path) {
                return &document;
            }
        }
        return nullptr;
    }

    std::optional<ToolingDocumentAnalysis> analyzeWorkspaceDocument(
        const std::string& path) {
        if (const DocumentState* openDocument = findOpenDocumentByPath(path)) {
            ToolingAnalyzeOptions options;
            options.sourcePath = openDocument->path;
            options.packageSearchPaths = m_packageSearchPaths;
            options.moduleGraphCache = &m_cache;
            options.strictMode =
                toolingSourceStartsWithStrictDirective(openDocument->text);
            return analyzeDocumentForTooling(openDocument->text, options);
        }

        const auto text = readFileText(path);
        if (!text.has_value()) {
            return std::nullopt;
        }

        ToolingAnalyzeOptions options;
        options.sourcePath = path;
        options.packageSearchPaths = m_packageSearchPaths;
        options.moduleGraphCache = &m_cache;
        options.strictMode = toolingSourceStartsWithStrictDirective(*text);
        return analyzeDocumentForTooling(*text, options);
    }

    std::vector<std::string> collectWorkspaceFiles() const {
        std::vector<std::string> files;
        std::unordered_set<std::string> seen;

        auto addFile = [&](const std::string& rawPath) {
            const std::string path = canonicalizePath(rawPath);
            if (!path.empty() && seen.insert(path).second) {
                files.push_back(path);
            }
        };

        for (const auto& root : m_workspaceRoots) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(root, ec)) {
                if (std::filesystem::path(root).extension() == ".mog") {
                    addFile(root);
                }
                continue;
            }

            std::filesystem::recursive_directory_iterator iterator(
                root, std::filesystem::directory_options::skip_permission_denied,
                ec);
            std::filesystem::recursive_directory_iterator end;
            while (!ec && iterator != end) {
                const std::filesystem::path path = iterator->path();
                if (iterator->is_directory(ec)) {
                    if (!ec &&
                        isHiddenOrBuildDirectory(path.lexically_relative(root))) {
                        iterator.disable_recursion_pending();
                    }
                    iterator.increment(ec);
                    continue;
                }

                if (!ec && iterator->is_regular_file(ec) &&
                    path.extension() == ".mog") {
                    addFile(path.string());
                }
                iterator.increment(ec);
            }
        }

        for (const auto& [uri, document] : m_documents) {
            (void)uri;
            if (std::filesystem::path(document.path).extension() != ".mog") {
                continue;
            }

            for (const auto& root : m_workspaceRoots) {
                if (pathWithinRoot(document.path, root)) {
                    addFile(document.path);
                    break;
                }
            }
        }

        return files;
    }

    std::string relativeWorkspacePath(const std::string& path) const {
        for (const auto& root : m_workspaceRoots) {
            if (!pathWithinRoot(path, root)) {
                continue;
            }

            std::error_code ec;
            const std::filesystem::path relative =
                std::filesystem::path(path).lexically_relative(root);
            if (!ec && !relative.empty()) {
                return relative.string();
            }
        }
        return path;
    }

    void analyzeAndPublish(DocumentState& document) {
        ToolingAnalyzeOptions options;
        options.sourcePath = document.path;
        options.packageSearchPaths = m_packageSearchPaths;
        options.moduleGraphCache = &m_cache;
        options.strictMode = toolingSourceStartsWithStrictDirective(document.text);

        document.analysis = analyzeDocumentForTooling(document.text, options);
        sendPublishDiagnostics(document.uri, document.analysis.diagnostics);
    }

    void sendInitializeResponse(const JsonValue* id) {
        if (id == nullptr) {
            return;
        }

        JsonObject result;
        result["capabilities"] = JsonValue(JsonObject{
            {"textDocumentSync", JsonValue(1.0)},
            {"documentSymbolProvider", JsonValue(true)},
            {"workspaceSymbolProvider", JsonValue(true)},
            {"definitionProvider", JsonValue(true)},
            {"referencesProvider", JsonValue(true)},
            {"hoverProvider", JsonValue(true)},
            {"renameProvider",
             JsonValue(JsonObject{{"prepareProvider", JsonValue(true)}})},
            {"completionProvider",
             JsonValue(JsonObject{{"resolveProvider", JsonValue(false)},
                                  {"triggerCharacters",
                                   JsonValue(JsonArray{
                                       JsonValue(std::string("."))})}})},
            {"signatureHelpProvider",
             JsonValue(JsonObject{{"triggerCharacters",
                                   JsonValue(JsonArray{
                                       JsonValue(std::string("(")),
                                       JsonValue(std::string(","))})}})},
        });
        sendResponse(*id, JsonValue(std::move(result)));
    }

    double symbolKindForToolingKind(std::string_view kind) const {
        if (kind == "class") {
            return 5.0;
        }
        if (kind == "function") {
            return 12.0;
        }
        if (kind == "constant") {
            return 14.0;
        }
        if (kind == "import") {
            return 2.0;
        }
        if (kind == "type") {
            return 26.0;
        }
        return 13.0;
    }

    double completionKindForToolingKind(std::string_view kind) const {
        if (kind == "method") {
            return 2.0;
        }
        if (kind == "class") {
            return 7.0;
        }
        if (kind == "field") {
            return 5.0;
        }
        if (kind == "function") {
            return 3.0;
        }
        if (kind == "constant") {
            return 21.0;
        }
        if (kind == "import") {
            return 9.0;
        }
        if (kind == "keyword") {
            return 14.0;
        }
        if (kind == "type") {
            return 22.0;
        }
        return 6.0;
    }

    JsonValue makeDocumentSymbolsResponse(
        const ToolingDocumentAnalysis& analysis) const {
        JsonArray items;
        items.reserve(analysis.documentSymbols.size());
        for (const auto& symbol : analysis.documentSymbols) {
            JsonObject item;
            item["name"] = JsonValue(symbol.name);
            item["kind"] = JsonValue(symbolKindForToolingKind(symbol.kind));
            item["range"] = makeRange(symbol.range);
            item["selectionRange"] = makeRange(symbol.selectionRange);
            if (!symbol.detail.empty()) {
                item["detail"] = JsonValue(symbol.detail);
            }
            items.push_back(JsonValue(std::move(item)));
        }
        return JsonValue(std::move(items));
    }

    JsonValue makeWorkspaceEditResponse(
        const std::vector<ToolingTextEdit>& edits) const {
        JsonObject changes;
        std::unordered_map<std::string, size_t> uriIndexes;

        for (const auto& edit : edits) {
            const std::string uri = pathToFileUri(edit.path);
            auto uriIt = uriIndexes.find(uri);
            if (uriIt == uriIndexes.end()) {
                uriIt = uriIndexes.emplace(uri, uriIndexes.size()).first;
                changes[uri] = JsonValue(JsonArray{});
            }

            auto* editArray = std::get_if<JsonArray>(&changes[uri].value);
            if (editArray == nullptr) {
                continue;
            }

            JsonObject item;
            item["range"] = makeRange(edit.range);
            item["newText"] = JsonValue(edit.newText);
            editArray->push_back(JsonValue(std::move(item)));
        }

        JsonObject result;
        result["changes"] = JsonValue(std::move(changes));
        return JsonValue(std::move(result));
    }

    void sendResponse(const JsonValue& id, const JsonValue& result) {
        JsonObject payload;
        payload["jsonrpc"] = JsonValue(std::string("2.0"));
        payload["id"] = id;
        payload["result"] = result;
        writeMessage(std::cout, serializeJson(JsonValue(std::move(payload))));
    }

    void sendErrorResponse(const JsonValue& id, int code,
                           const std::string& message) {
        JsonObject error;
        error["code"] = JsonValue(static_cast<double>(code));
        error["message"] = JsonValue(message);

        JsonObject payload;
        payload["jsonrpc"] = JsonValue(std::string("2.0"));
        payload["id"] = id;
        payload["error"] = JsonValue(std::move(error));
        writeMessage(std::cout, serializeJson(JsonValue(std::move(payload))));
    }

    void sendPublishDiagnostics(const std::string& uri,
                                const std::vector<ToolingDiagnostic>& diagnostics) {
        JsonArray lspDiagnostics;
        lspDiagnostics.reserve(diagnostics.size());
        for (const auto& diagnostic : diagnostics) {
            JsonObject item;
            item["range"] = makeRange(diagnostic.range);
            item["severity"] = JsonValue(1.0);
            item["source"] = JsonValue(std::string("mog"));
            item["message"] = JsonValue(diagnostic.message);
            if (!diagnostic.code.empty()) {
                item["code"] = JsonValue(diagnostic.code);
            }

            JsonValue related = makeRelatedInformation(uri, diagnostic);
            if (const auto* relatedArray = std::get_if<JsonArray>(&related.value);
                relatedArray != nullptr && !relatedArray->empty()) {
                item["relatedInformation"] = related;
            }

            lspDiagnostics.push_back(JsonValue(std::move(item)));
        }

        JsonObject params;
        params["uri"] = JsonValue(uri);
        params["diagnostics"] = JsonValue(std::move(lspDiagnostics));

        JsonObject payload;
        payload["jsonrpc"] = JsonValue(std::string("2.0"));
        payload["method"] =
            JsonValue(std::string("textDocument/publishDiagnostics"));
        payload["params"] = JsonValue(std::move(params));
        writeMessage(std::cout, serializeJson(JsonValue(std::move(payload))));
    }
};

std::vector<std::string> defaultPackageSearchPaths(const char* executablePath) {
    std::vector<std::string> paths;

    try {
        const std::filesystem::path executable =
            std::filesystem::weakly_canonical(executablePath);
        paths.push_back((executable.parent_path() / "packages").string());
    } catch (const std::exception&) {
    }

    return paths;
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    MogLspServer server(defaultPackageSearchPaths(argv[0]));
    return server.run();
}
