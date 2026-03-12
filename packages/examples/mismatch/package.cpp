#include "NativePackageAPI.hpp"

namespace {

bool identity(const ExprHostApi* hostApi, const ExprPackageValue* args, size_t argc,
              ExprPackageValue* outResult, ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        if (outError != nullptr) {
            static const char kMessage[] = "expected exactly 1 argument";
            *outError = {kMessage, sizeof(kMessage) - 1};
        }
        return false;
    }

    *outResult = args[0];
    return true;
}

constexpr ExprPackageFunctionExport kFunctions[] = {
    {"identity", "fn(i64) -> i64", 1, identity},
};

constexpr ExprPackageRegistration kRegistration = {
    EXPR_NATIVE_PACKAGE_ABI_VERSION,
    "examples",
    "declared_math",
    kFunctions,
    sizeof(kFunctions) / sizeof(kFunctions[0]),
    nullptr,
    0,
};

}  // namespace

extern "C" const ExprPackageRegistration* exprRegisterPackage(void) {
    return &kRegistration;
}
