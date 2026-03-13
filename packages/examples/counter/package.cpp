#include "NativePackageAPI.hpp"

#include <cstdint>
#include <cstdio>
#include <new>
#include <string_view>

namespace {

struct CounterHandle {
    int64_t value = 0;
};

bool isCounterHandle(const ExprPackageValue& value,
                     CounterHandle*& outHandle,
                     ExprPackageStringView* outError) {
    outHandle = nullptr;
    if (value.kind != EXPR_PACKAGE_VALUE_HANDLE) {
        if (outError != nullptr) {
            static const char kMessage[] = "expected CounterHandle";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    const ExprPackageHandleValue& handle = value.as.handle_value;
    if (handle.package_namespace == nullptr || handle.package_name == nullptr ||
        handle.type_name == nullptr || handle.handle_data == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "invalid CounterHandle metadata";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    if (std::string_view(handle.package_namespace) != "examples" ||
        std::string_view(handle.package_name) != "counter" ||
        std::string_view(handle.type_name) != "CounterHandle") {
        if (outError != nullptr) {
            static const char kMessage[] = "expected examples:counter handle";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    outHandle = static_cast<CounterHandle*>(handle.handle_data);
    return true;
}

void releaseCounterHandle(void* handleData) {
    auto* handle = static_cast<CounterHandle*>(handleData);
    delete handle;
    std::puts("counter_handle_released");
}

bool createCounter(const ExprHostApi* hostApi, const ExprPackageValue* args,
                   size_t argc, ExprPackageValue* outResult,
                   ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "expected exactly 1 argument";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    if (args[0].kind != EXPR_PACKAGE_VALUE_I64) {
        if (outError != nullptr) {
            static const char kMessage[] = "create expects an i64 start value";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    auto* handle = new (std::nothrow) CounterHandle();
    if (handle == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "allocation failed";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    handle->value = args[0].as.i64_value;
    outResult->kind = EXPR_PACKAGE_VALUE_HANDLE;
    outResult->as.handle_value = {"examples", "counter", "CounterHandle",
                                  handle, releaseCounterHandle};
    return true;
}

bool readCounter(const ExprHostApi* hostApi, const ExprPackageValue* args,
                 size_t argc, ExprPackageValue* outResult,
                 ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "expected exactly 1 argument";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    CounterHandle* handle = nullptr;
    if (!isCounterHandle(args[0], handle, outError)) {
        return false;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_I64;
    outResult->as.i64_value = handle->value;
    return true;
}

bool addCounter(const ExprHostApi* hostApi, const ExprPackageValue* args,
                size_t argc, ExprPackageValue* outResult,
                ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 2 || args == nullptr || outResult == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "expected exactly 2 arguments";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    CounterHandle* handle = nullptr;
    if (!isCounterHandle(args[0], handle, outError)) {
        return false;
    }

    if (args[1].kind != EXPR_PACKAGE_VALUE_I64) {
        if (outError != nullptr) {
            static const char kMessage[] = "add expects an i64 delta";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    handle->value += args[1].as.i64_value;
    outResult->kind = EXPR_PACKAGE_VALUE_I64;
    outResult->as.i64_value = handle->value;
    return true;
}

constexpr ExprPackageFunctionExport kFunctions[] = {
    {"create",
     "fn(i64) -> handle<examples:counter:CounterHandle>",
     1,
     createCounter},
    {"read",
     "fn(handle<examples:counter:CounterHandle>) -> i64",
     1,
     readCounter},
    {"add",
     "fn(handle<examples:counter:CounterHandle>, i64) -> i64",
     2,
     addCounter},
};

constexpr ExprPackageConstantExport kConstants[] = {
    {"PACKAGE_ID",
     "str",
     {EXPR_PACKAGE_VALUE_STR, {.string_value = {"examples:counter", 16}}}},
};

constexpr ExprPackageRegistration kRegistration = {
    EXPR_NATIVE_PACKAGE_ABI_VERSION,
    "examples",
    "counter",
    kFunctions,
    sizeof(kFunctions) / sizeof(kFunctions[0]),
    kConstants,
    sizeof(kConstants) / sizeof(kConstants[0]),
};

}  // namespace

extern "C" const ExprPackageRegistration* exprRegisterPackage(void) {
    return &kRegistration;
}
