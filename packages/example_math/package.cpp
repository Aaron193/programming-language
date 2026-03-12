#include "NativePackageAPI.hpp"

#include <string>

namespace {

bool addI64(const ExprHostApi* hostApi, const ExprPackageValue* args, size_t argc,
            ExprPackageValue* outResult, ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 2 || args == nullptr || outResult == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "expected exactly 2 arguments";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    if (args[0].kind != EXPR_PACKAGE_VALUE_I64 ||
        args[1].kind != EXPR_PACKAGE_VALUE_I64) {
        if (outError != nullptr) {
            static const char kMessage[] = "add expects i64 arguments";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_I64;
    outResult->as.i64_value = args[0].as.i64_value + args[1].as.i64_value;
    return true;
}

bool greet(const ExprHostApi* hostApi, const ExprPackageValue* args, size_t argc,
           ExprPackageValue* outResult, ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "expected exactly 1 argument";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    if (args[0].kind != EXPR_PACKAGE_VALUE_STR) {
        if (outError != nullptr) {
            static const char kMessage[] = "greet expects a str argument";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    static thread_local std::string message;
    message.assign("Hello, ");
    message.append(args[0].as.string_value.data,
                   args[0].as.string_value.length);

    outResult->kind = EXPR_PACKAGE_VALUE_STR;
    outResult->as.string_value = {message.c_str(), message.size()};
    return true;
}

constexpr ExprPackageFunctionExport kFunctions[] = {
    {"addI64", "fn(i64, i64) -> i64", 2, addI64},
    {"greet", "fn(str) -> str", 1, greet},
};

constexpr ExprPackageConstantExport kConstants[] = {
    {"PACKAGE_NAME",
     "str",
     {EXPR_PACKAGE_VALUE_STR, {.string_value = {"example_math", 12}}}},
    {"MEANING_OF_LIFE",
     "i64",
     {EXPR_PACKAGE_VALUE_I64, {.i64_value = 42}}},
};

constexpr ExprPackageRegistration kRegistration = {
    EXPR_NATIVE_PACKAGE_ABI_VERSION,
    "example_math",
    kFunctions,
    sizeof(kFunctions) / sizeof(kFunctions[0]),
    kConstants,
    sizeof(kConstants) / sizeof(kConstants[0]),
};

}  // namespace

extern "C" const ExprPackageRegistration* exprRegisterPackage(void) {
    return &kRegistration;
}
