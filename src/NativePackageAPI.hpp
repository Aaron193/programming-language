#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXPR_NATIVE_PACKAGE_ABI_VERSION 1u

typedef enum ExprPackageValueKind {
    EXPR_PACKAGE_VALUE_NULL = 0,
    EXPR_PACKAGE_VALUE_BOOL = 1,
    EXPR_PACKAGE_VALUE_I64 = 2,
    EXPR_PACKAGE_VALUE_U64 = 3,
    EXPR_PACKAGE_VALUE_F64 = 4,
    EXPR_PACKAGE_VALUE_STR = 5,
} ExprPackageValueKind;

typedef struct ExprPackageStringView {
    const char* data;
    size_t length;
} ExprPackageStringView;

typedef struct ExprPackageValue {
    ExprPackageValueKind kind;
    union {
        bool boolean_value;
        int64_t i64_value;
        uint64_t u64_value;
        double f64_value;
        ExprPackageStringView string_value;
    } as;
} ExprPackageValue;

typedef struct ExprHostApi {
    uint32_t abi_version;
} ExprHostApi;

typedef bool (*ExprNativePackageFn)(const ExprHostApi* host_api,
                                    const ExprPackageValue* args, size_t argc,
                                    ExprPackageValue* out_result,
                                    ExprPackageStringView* out_error);

typedef struct ExprPackageFunctionExport {
    const char* name;
    const char* signature;
    int arity;
    ExprNativePackageFn callback;
} ExprPackageFunctionExport;

typedef struct ExprPackageConstantExport {
    const char* name;
    const char* type_name;
    ExprPackageValue value;
} ExprPackageConstantExport;

typedef struct ExprPackageRegistration {
    uint32_t abi_version;
    const char* package_name;
    const ExprPackageFunctionExport* functions;
    size_t function_count;
    const ExprPackageConstantExport* constants;
    size_t constant_count;
} ExprPackageRegistration;

typedef const ExprPackageRegistration* (*ExprRegisterPackageFn)(void);
