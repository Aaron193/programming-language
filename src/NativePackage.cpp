#include "NativePackage.hpp"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <dlfcn.h>

#include "ModuleResolver.hpp"
#include "TypeInfo.hpp"

namespace {

constexpr std::string_view kNativeImportPrefix = "native:";
constexpr const char* kRegisterPackageSymbol = "exprRegisterPackage";

#if defined(__APPLE__)
constexpr const char* kPackageLibraryFileName = "package.dylib";
#else
constexpr const char* kPackageLibraryFileName = "package.so";
#endif

bool looksLikeSourceModuleSpecifier(const std::string& rawImportPath) {
    if (rawImportPath.empty()) {
        return false;
    }

    std::filesystem::path path(rawImportPath);
    if (path.is_absolute()) {
        return true;
    }

    if (rawImportPath.rfind("./", 0) == 0 || rawImportPath.rfind("../", 0) == 0) {
        return true;
    }

    if (rawImportPath.find('/') != std::string::npos ||
        rawImportPath.find('\\') != std::string::npos) {
        return true;
    }

    return path.extension() == ".expr";
}

}  // namespace

bool isValidPackageIdPart(std::string_view text) {
    if (text.empty()) {
        return false;
    }

    for (char ch : text) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' ||
            ch == '-') {
            continue;
        }
        return false;
    }

    return true;
}

bool isValidHandleTypeName(std::string_view text) {
    if (text.empty()) {
        return false;
    }

    for (char ch : text) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
            continue;
        }
        return false;
    }

    return true;
}

std::string makePackageId(std::string_view packageNamespace,
                          std::string_view packageName) {
    return std::string(packageNamespace) + ":" + std::string(packageName);
}

namespace {

bool parseNamespacedPackageSpecifier(const std::string& rawImportPath,
                                     std::string& outNamespace,
                                     std::string& outName,
                                     std::string& outError) {
    outNamespace.clear();
    outName.clear();
    outError.clear();

    size_t colon = rawImportPath.find(':');
    if (colon == std::string::npos) {
        return false;
    }

    if (rawImportPath.find(':', colon + 1) != std::string::npos) {
        outError = "Invalid package ID '" + rawImportPath +
                   "': expected exactly one ':'.";
        return false;
    }

    std::string_view packageNamespace(rawImportPath.data(), colon);
    std::string_view packageName(rawImportPath.data() + colon + 1,
                                 rawImportPath.size() - colon - 1);

    if (!isValidPackageIdPart(packageNamespace) ||
        !isValidPackageIdPart(packageName)) {
        outError = "Invalid package ID '" + rawImportPath +
                   "': namespace and name must use lowercase letters, digits, "
                   "'_', or '-'.";
        return false;
    }

    outNamespace.assign(packageNamespace);
    outName.assign(packageName);
    return true;
}

std::string weaklyCanonicalOrEmpty(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return "";
    }

    if (!std::filesystem::exists(resolved, ec) || ec) {
        return "";
    }

    return resolved.string();
}

class TypeStringParser {
   public:
    explicit TypeStringParser(std::string_view text) : m_text(text) {}

    TypeRef parse(std::string& outError) {
        TypeRef type = parseType(outError);
        if (!outError.empty()) {
            return nullptr;
        }

        skipWhitespace();
        if (!isAtEnd()) {
            outError = "Unexpected trailing type syntax near '" +
                       std::string(m_text.substr(m_position)) + "'.";
            return nullptr;
        }

        return type;
    }

   private:
    std::string_view m_text;
    size_t m_position = 0;

    bool isAtEnd() const { return m_position >= m_text.size(); }

    void skipWhitespace() {
        while (!isAtEnd() &&
               std::isspace(static_cast<unsigned char>(m_text[m_position]))) {
            ++m_position;
        }
    }

    bool match(std::string_view token) {
        skipWhitespace();
        if (m_text.substr(m_position, token.size()) != token) {
            return false;
        }

        m_position += token.size();
        return true;
    }

    bool consume(char ch, std::string& outError) {
        skipWhitespace();
        if (isAtEnd() || m_text[m_position] != ch) {
            outError = std::string("Expected '") + ch + "' in type signature.";
            return false;
        }

        ++m_position;
        return true;
    }

    std::string parseIdentifier() {
        skipWhitespace();
        size_t start = m_position;
        while (!isAtEnd()) {
            char ch = m_text[m_position];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
                ++m_position;
                continue;
            }
            break;
        }

        return std::string(m_text.substr(start, m_position - start));
    }

    TypeRef parseType(std::string& outError) {
        skipWhitespace();
        if (match("fn")) {
            if (!consume('(', outError)) {
                return nullptr;
            }

            std::vector<TypeRef> params;
            skipWhitespace();
            if (!match(")")) {
                while (true) {
                    TypeRef param = parseType(outError);
                    if (!param) {
                        return nullptr;
                    }
                    params.push_back(param);

                    skipWhitespace();
                    if (match(")")) {
                        break;
                    }

                    if (!consume(',', outError)) {
                        return nullptr;
                    }
                }
            }

            if (!match("->")) {
                outError = "Expected '->' in function type.";
                return nullptr;
            }

            TypeRef returnType = parseType(outError);
            if (!returnType) {
                return nullptr;
            }

            return TypeInfo::makeFunction(std::move(params), returnType);
        }

        std::string identifier = parseIdentifier();
        if (identifier.empty()) {
            outError = "Expected type name in package signature.";
            return nullptr;
        }

        TypeRef type;
        if (identifier == "i8") {
            type = TypeInfo::makeI8();
        } else if (identifier == "i16") {
            type = TypeInfo::makeI16();
        } else if (identifier == "i32") {
            type = TypeInfo::makeI32();
        } else if (identifier == "i64") {
            type = TypeInfo::makeI64();
        } else if (identifier == "u8") {
            type = TypeInfo::makeU8();
        } else if (identifier == "u16") {
            type = TypeInfo::makeU16();
        } else if (identifier == "u32") {
            type = TypeInfo::makeU32();
        } else if (identifier == "u64") {
            type = TypeInfo::makeU64();
        } else if (identifier == "usize") {
            type = TypeInfo::makeUSize();
        } else if (identifier == "f32") {
            type = TypeInfo::makeF32();
        } else if (identifier == "f64") {
            type = TypeInfo::makeF64();
        } else if (identifier == "bool") {
            type = TypeInfo::makeBool();
        } else if (identifier == "str") {
            type = TypeInfo::makeStr();
        } else if (identifier == "void") {
            type = TypeInfo::makeVoid();
        } else if (identifier == "null") {
            type = TypeInfo::makeNull();
        } else if (identifier == "any") {
            type = TypeInfo::makeAny();
        } else if (identifier == "Array") {
            if (!consume('<', outError)) {
                return nullptr;
            }
            TypeRef elementType = parseType(outError);
            if (!elementType || !consume('>', outError)) {
                return nullptr;
            }
            type = TypeInfo::makeArray(elementType);
        } else if (identifier == "Dict") {
            if (!consume('<', outError)) {
                return nullptr;
            }
            TypeRef keyType = parseType(outError);
            if (!keyType || !consume(',', outError)) {
                return nullptr;
            }
            TypeRef valueType = parseType(outError);
            if (!valueType || !consume('>', outError)) {
                return nullptr;
            }
            type = TypeInfo::makeDict(keyType, valueType);
        } else if (identifier == "Set") {
            if (!consume('<', outError)) {
                return nullptr;
            }
            TypeRef elementType = parseType(outError);
            if (!elementType || !consume('>', outError)) {
                return nullptr;
            }
            type = TypeInfo::makeSet(elementType);
        } else if (identifier == "handle") {
            if (!consume('<', outError)) {
                return nullptr;
            }

            skipWhitespace();
            size_t specStart = m_position;
            while (!isAtEnd() && m_text[m_position] != '>') {
                ++m_position;
            }

            if (isAtEnd()) {
                outError = "Expected '>' in handle type.";
                return nullptr;
            }

            std::string spec(m_text.substr(specStart, m_position - specStart));
            ++m_position;

            size_t firstColon = spec.find(':');
            size_t secondColon =
                firstColon == std::string::npos ? std::string::npos
                                                : spec.find(':', firstColon + 1);
            if (firstColon == std::string::npos ||
                secondColon == std::string::npos ||
                spec.find(':', secondColon + 1) != std::string::npos) {
                outError = "Handle type must use handle<namespace:name:Type>.";
                return nullptr;
            }

            std::string_view packageNamespace(spec.data(), firstColon);
            std::string_view packageName(spec.data() + firstColon + 1,
                                         secondColon - firstColon - 1);
            std::string_view typeName(spec.data() + secondColon + 1,
                                      spec.size() - secondColon - 1);

            if (!isValidPackageIdPart(packageNamespace) ||
                !isValidPackageIdPart(packageName) ||
                !isValidHandleTypeName(typeName)) {
                outError =
                    "Handle type must use lowercase package IDs and an "
                    "alphanumeric type name.";
                return nullptr;
            }

            type = TypeInfo::makeNativeHandle(
                makePackageId(packageNamespace, packageName),
                std::string(typeName));
        } else {
            type = TypeInfo::makeClass(identifier);
        }

        skipWhitespace();
        while (match("?")) {
            type = TypeInfo::makeOptional(type);
        }

        return type;
    }
};

TypeRef parsePackageType(std::string_view text, std::string& outError) {
    TypeStringParser parser(text);
    return parser.parse(outError);
}

bool packageValueMatchesType(const ExprPackageValue& value,
                             const TypeRef& type) {
    if (!type) {
        return false;
    }

    if (type->kind == TypeKind::OPTIONAL) {
        if (value.kind == EXPR_PACKAGE_VALUE_NULL) {
            return true;
        }

        return type->innerType != nullptr &&
               packageValueMatchesType(value, type->innerType);
    }

    switch (value.kind) {
        case EXPR_PACKAGE_VALUE_NULL:
            return type->kind == TypeKind::NULL_TYPE;
        case EXPR_PACKAGE_VALUE_BOOL:
            return type->kind == TypeKind::BOOL;
        case EXPR_PACKAGE_VALUE_I64:
            return type->kind == TypeKind::I64;
        case EXPR_PACKAGE_VALUE_U64:
            return type->kind == TypeKind::U64 || type->kind == TypeKind::USIZE;
        case EXPR_PACKAGE_VALUE_F64:
            return type->kind == TypeKind::F64;
        case EXPR_PACKAGE_VALUE_STR:
            return type->kind == TypeKind::STR;
        case EXPR_PACKAGE_VALUE_HANDLE:
            return false;
        default:
            return false;
    }
}

bool shouldKeepHandle(bool keepLibraryLoaded, void** outLibraryHandle) {
    return keepLibraryLoaded || outLibraryHandle != nullptr;
}

std::string packageDescriptorId(const NativePackageDescriptor& descriptor) {
    if (descriptor.packageNamespace.empty()) {
        return descriptor.packageName;
    }

    return descriptor.packageId;
}

}  // namespace

std::vector<std::string> normalizePackageSearchPaths(
    const std::vector<std::string>& packageSearchPaths,
    const std::string& importerPath) {
    std::vector<std::string> roots;
    std::unordered_set<std::string> seen;

    auto addRoot = [&](const std::filesystem::path& candidate) {
        std::string resolved = weaklyCanonicalOrEmpty(candidate);
        if (resolved.empty()) {
            return;
        }
        if (seen.insert(resolved).second) {
            roots.push_back(resolved);
        }
    };

    for (const auto& root : packageSearchPaths) {
        addRoot(root);
    }

    if (!importerPath.empty()) {
        std::filesystem::path importer(importerPath);
        addRoot(importer.parent_path() / "packages");
    }

    addRoot(std::filesystem::current_path() / "packages");

    return roots;
}

bool resolveImportTarget(const std::string& importerPath,
                         const std::string& rawImportPath,
                         const std::vector<std::string>& packageSearchPaths,
                         ImportTarget& outTarget,
                         std::string& outError) {
    outTarget = ImportTarget{};
    outError.clear();
    outTarget.rawSpecifier = rawImportPath;

    if (looksLikeSourceModuleSpecifier(rawImportPath)) {
        std::string resolvedPath = resolveImportPath(importerPath, rawImportPath);
        if (resolvedPath.empty()) {
            outError = "Cannot find module '" + rawImportPath + "'.";
            return false;
        }

        outTarget.kind = ImportTargetKind::SOURCE_MODULE;
        outTarget.canonicalId = resolvedPath;
        outTarget.resolvedPath = resolvedPath;
        outTarget.displayName = resolvedPath;
        return true;
    }

    std::string packageNamespace;
    std::string packageName;
    std::string parseError;
    bool isNamespacedImport = rawImportPath.find(':') != std::string::npos;
    if (isNamespacedImport &&
        !parseNamespacedPackageSpecifier(rawImportPath, packageNamespace,
                                         packageName, parseError)) {
        outError = parseError;
        return false;
    }

    if (isNamespacedImport) {
        for (const auto& root :
             normalizePackageSearchPaths(packageSearchPaths, importerPath)) {
            std::filesystem::path libraryPath = std::filesystem::path(root) /
                                                packageNamespace / packageName /
                                                kPackageLibraryFileName;
            std::string resolvedLibraryPath = weaklyCanonicalOrEmpty(libraryPath);
            if (resolvedLibraryPath.empty()) {
                continue;
            }

            outTarget.kind = ImportTargetKind::NATIVE_PACKAGE;
            outTarget.canonicalId =
                std::string(kNativeImportPrefix) + resolvedLibraryPath;
            outTarget.resolvedPath = resolvedLibraryPath;
            outTarget.displayName = rawImportPath;
            outTarget.packageNamespace = packageNamespace;
            outTarget.packageName = packageName;
            return true;
        }

        outError = "Cannot find native package '" + rawImportPath + "'.";
        return false;
    }

    for (const auto& root :
         normalizePackageSearchPaths(packageSearchPaths, importerPath)) {
        std::filesystem::path libraryPath =
            std::filesystem::path(root) / rawImportPath / kPackageLibraryFileName;
        std::string resolvedLibraryPath = weaklyCanonicalOrEmpty(libraryPath);
        if (resolvedLibraryPath.empty()) {
            continue;
        }

        outTarget.kind = ImportTargetKind::NATIVE_PACKAGE;
        outTarget.canonicalId =
            std::string(kNativeImportPrefix) + resolvedLibraryPath;
        outTarget.resolvedPath = resolvedLibraryPath;
        outTarget.displayName = rawImportPath;
        outTarget.packageName = rawImportPath;
        outTarget.isLegacyBarePackage = true;
        return true;
    }

    outError = "Cannot find module or native package '" + rawImportPath + "'.";
    return false;
}

bool isNativeImportTargetId(const std::string& canonicalId) {
    return canonicalId.rfind(kNativeImportPrefix.data(), 0) == 0;
}

std::string nativeImportLibraryPath(const std::string& canonicalId) {
    if (!isNativeImportTargetId(canonicalId)) {
        return "";
    }

    return canonicalId.substr(kNativeImportPrefix.size());
}

bool loadNativePackageDescriptor(const std::string& libraryPath,
                                 NativePackageDescriptor& outDescriptor,
                                 std::string& outError,
                                 bool keepLibraryLoaded,
                                 void** outLibraryHandle) {
    outDescriptor = NativePackageDescriptor{};
    outError.clear();
    if (outLibraryHandle != nullptr) {
        *outLibraryHandle = nullptr;
    }

    dlerror();
    void* handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* dlError = dlerror();
        outError = "Failed to load native package '" + libraryPath + "'";
        if (dlError != nullptr) {
            outError += ": ";
            outError += dlError;
        }
        outError += ".";
        return false;
    }

    auto closeHandle = [&]() {
        if (handle != nullptr) {
            dlclose(handle);
            handle = nullptr;
        }
    };

    dlerror();
    void* symbol = dlsym(handle, kRegisterPackageSymbol);
    const char* symbolError = dlerror();
    if (symbolError != nullptr || symbol == nullptr) {
        outError = "Native package '" + libraryPath +
                   "' is missing required symbol '" +
                   std::string(kRegisterPackageSymbol) + "'.";
        closeHandle();
        return false;
    }

    auto registerPackage = reinterpret_cast<ExprRegisterPackageFn>(symbol);
    const ExprPackageRegistration* registration = registerPackage();
    if (registration == nullptr) {
        outError = "Native package '" + libraryPath +
                   "' returned null registration metadata.";
        closeHandle();
        return false;
    }

    const auto* registrationHeader =
        reinterpret_cast<const ExprPackageRegistrationHeader*>(registration);

    const ExprPackageFunctionExport* functions = nullptr;
    size_t functionCount = 0;
    const ExprPackageConstantExport* constants = nullptr;
    size_t constantCount = 0;

    if (registrationHeader->abi_version == EXPR_NATIVE_PACKAGE_ABI_VERSION ||
        registrationHeader->abi_version ==
            EXPR_NATIVE_PACKAGE_NAMESPACED_ABI_VERSION) {
        if (registration->package_namespace == nullptr ||
            registration->package_namespace[0] == '\0') {
            outError = "Native package '" + libraryPath +
                       "' did not declare a package namespace.";
            closeHandle();
            return false;
        }

        if (registration->package_name == nullptr ||
            registration->package_name[0] == '\0') {
            outError = "Native package '" + libraryPath +
                       "' did not declare a package name.";
            closeHandle();
            return false;
        }

        if (!isValidPackageIdPart(registration->package_namespace) ||
            !isValidPackageIdPart(registration->package_name)) {
            outError = "Native package '" + libraryPath +
                       "' declared an invalid package ID.";
            closeHandle();
            return false;
        }

        outDescriptor.packageNamespace = registration->package_namespace;
        outDescriptor.packageName = registration->package_name;
        outDescriptor.packageId = makePackageId(outDescriptor.packageNamespace,
                                                outDescriptor.packageName);
        functions = registration->functions;
        functionCount = registration->function_count;
        constants = registration->constants;
        constantCount = registration->constant_count;
    } else if (registrationHeader->abi_version ==
               EXPR_NATIVE_PACKAGE_LEGACY_ABI_VERSION) {
        const auto* legacyRegistration =
            reinterpret_cast<const ExprPackageRegistrationV1*>(registration);
        if (legacyRegistration->package_name == nullptr ||
            legacyRegistration->package_name[0] == '\0') {
            outError = "Native package '" + libraryPath +
                       "' did not declare a package name.";
            closeHandle();
            return false;
        }

        outDescriptor.packageName = legacyRegistration->package_name;
        outDescriptor.packageId = outDescriptor.packageName;
        outDescriptor.isLegacyAbi = true;
        functions = legacyRegistration->functions;
        functionCount = legacyRegistration->function_count;
        constants = legacyRegistration->constants;
        constantCount = legacyRegistration->constant_count;
    } else {
        outError = "Native package '" + libraryPath +
                   "' has ABI version " +
                   std::to_string(registrationHeader->abi_version) +
                   ", expected " +
                   std::to_string(EXPR_NATIVE_PACKAGE_ABI_VERSION) + " or " +
                   std::to_string(EXPR_NATIVE_PACKAGE_NAMESPACED_ABI_VERSION) +
                   " or " +
                   std::to_string(EXPR_NATIVE_PACKAGE_LEGACY_ABI_VERSION) + ".";
        closeHandle();
        return false;
    }

    outDescriptor.libraryPath = libraryPath;

    std::string descriptorId = packageDescriptorId(outDescriptor);

    for (size_t index = 0; index < functionCount; ++index) {
        const ExprPackageFunctionExport& function = functions[index];
        if (function.name == nullptr || function.signature == nullptr ||
            function.callback == nullptr) {
            outError = "Native package '" + descriptorId +
                       "' has an incomplete function export entry.";
            closeHandle();
            return false;
        }

        std::string typeError;
        TypeRef type = parsePackageType(function.signature, typeError);
        if (!type || type->kind != TypeKind::FUNCTION) {
            outError = "Native package '" + descriptorId +
                       "' export '" + function.name +
                       "' has invalid function signature: " + typeError;
            closeHandle();
            return false;
        }

        if (function.arity >= 0 &&
            static_cast<size_t>(function.arity) != type->paramTypes.size()) {
            outError = "Native package '" + descriptorId +
                       "' export '" + function.name +
                       "' has arity/signature mismatch.";
            closeHandle();
            return false;
        }

        NativePackageFunctionDescriptor descriptor;
        descriptor.name = function.name;
        descriptor.type = type;
        descriptor.arity = function.arity;
        descriptor.callback = &function;
        outDescriptor.exportTypes[descriptor.name] = descriptor.type;
        outDescriptor.functions.push_back(std::move(descriptor));
    }

    for (size_t index = 0; index < constantCount; ++index) {
        const ExprPackageConstantExport& constant = constants[index];
        if (constant.name == nullptr || constant.type_name == nullptr) {
            outError = "Native package '" + descriptorId +
                       "' has an incomplete constant export entry.";
            closeHandle();
            return false;
        }

        std::string typeError;
        TypeRef type = parsePackageType(constant.type_name, typeError);
        if (!type || type->kind == TypeKind::FUNCTION) {
            outError = "Native package '" + descriptorId +
                       "' constant '" + constant.name +
                       "' has invalid type: " + typeError;
            closeHandle();
            return false;
        }

        if (!packageValueMatchesType(constant.value, type)) {
            outError = "Native package '" + descriptorId +
                       "' constant '" + constant.name +
                       "' does not match declared type '" +
                        type->toString() + "'.";
            closeHandle();
            return false;
        }

        NativePackageConstantDescriptor descriptor;
        descriptor.name = constant.name;
        descriptor.type = type;
        descriptor.value = constant.value;
        if (descriptor.value.kind == EXPR_PACKAGE_VALUE_STR &&
            descriptor.value.as.string_value.data != nullptr) {
            descriptor.stringValueStorage.assign(
                descriptor.value.as.string_value.data,
                descriptor.value.as.string_value.length);
        }
        outDescriptor.exportTypes[descriptor.name] = descriptor.type;
        outDescriptor.constants.push_back(std::move(descriptor));
    }

    if (shouldKeepHandle(keepLibraryLoaded, outLibraryHandle)) {
        if (outLibraryHandle != nullptr) {
            *outLibraryHandle = handle;
        }
    } else {
        closeHandle();
    }

    if (!keepLibraryLoaded && outLibraryHandle == nullptr) {
        closeHandle();
    }

    return true;
}
