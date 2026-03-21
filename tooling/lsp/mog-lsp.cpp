#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
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

struct DocumentState {
    std::string uri;
    std::string path;
    std::string text;
    int version = 0;
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
            sendInitializeResponse(id);
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
        document.path = decodeUriPath(*uri);
        document.text = *text;
        document.version = getIntegerValue(documentObject->get(), "version").value_or(0);
        m_documents[document.uri] = document;
        analyzeAndPublish(m_documents[document.uri]);
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

    void analyzeAndPublish(const DocumentState& document) {
        ToolingAnalyzeOptions options;
        options.sourcePath = document.path;
        options.packageSearchPaths = m_packageSearchPaths;
        options.moduleGraphCache = &m_cache;
        options.strictMode = toolingSourceStartsWithStrictDirective(document.text);

        ToolingDocumentAnalysis analysis =
            analyzeDocumentForTooling(document.text, options);
        sendPublishDiagnostics(document.uri, analysis.diagnostics);
    }

    void sendInitializeResponse(const JsonValue* id) {
        if (id == nullptr) {
            return;
        }

        JsonObject result;
        result["capabilities"] = JsonValue(JsonObject{
            {"textDocumentSync", JsonValue(1.0)},
        });
        sendResponse(*id, JsonValue(std::move(result)));
    }

    void sendResponse(const JsonValue& id, const JsonValue& result) {
        JsonObject payload;
        payload["jsonrpc"] = JsonValue(std::string("2.0"));
        payload["id"] = id;
        payload["result"] = result;
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
