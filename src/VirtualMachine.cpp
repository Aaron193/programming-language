#include "VirtualMachine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <dlfcn.h>

#include "NativePackage.hpp"
#include "StdLib.hpp"

static bool isFalsey(const Value& value) {
    if (value.isNil()) return true;
    if (value.isBool()) return !value.asBool();
    return false;
}

static bool isNumberPair(const Value& lhs, const Value& rhs) {
    return lhs.isAnyNumeric() && rhs.isAnyNumeric();
}

static bool valueToSignedInt(const Value& value, int64_t& out) {
    if (value.isSignedInt()) {
        out = value.asSignedInt();
        return true;
    }
    if (value.isUnsignedInt()) {
        out = static_cast<int64_t>(value.asUnsignedInt());
        return true;
    }
    if (value.isNumber()) {
        out = static_cast<int64_t>(value.asNumber());
        return true;
    }
    return false;
}

static bool valueToUnsignedInt(const Value& value, uint64_t& out) {
    if (value.isUnsignedInt()) {
        out = value.asUnsignedInt();
        return true;
    }
    if (value.isSignedInt()) {
        out = static_cast<uint64_t>(value.asSignedInt());
        return true;
    }
    if (value.isNumber()) {
        out = static_cast<uint64_t>(value.asNumber());
        return true;
    }
    return false;
}

static bool valueToBitwiseUnsignedInt(const Value& value, uint64_t& out) {
    if (value.isUnsignedInt()) {
        out = value.asUnsignedInt();
        return true;
    }
    if (value.isSignedInt()) {
        out = static_cast<uint64_t>(value.asSignedInt());
        return true;
    }
    return false;
}

static bool valueToBitwiseShiftAmount(const Value& value, uint32_t& out) {
    uint64_t raw = 0;
    if (!valueToBitwiseUnsignedInt(value, raw)) {
        return false;
    }
    out = static_cast<uint32_t>(raw) & 63u;
    return true;
}

static bool valueToDouble(const Value& value, double& out) {
    if (value.isNumber()) {
        out = value.asNumber();
        return true;
    }
    if (value.isSignedInt()) {
        out = static_cast<double>(value.asSignedInt());
        return true;
    }
    if (value.isUnsignedInt()) {
        out = static_cast<double>(value.asUnsignedInt());
        return true;
    }
    return false;
}

static int64_t wrapSignedAdd(int64_t lhs, int64_t rhs) {
    return static_cast<int64_t>(static_cast<uint64_t>(lhs) +
                                static_cast<uint64_t>(rhs));
}

static int64_t wrapSignedSub(int64_t lhs, int64_t rhs) {
    return static_cast<int64_t>(static_cast<uint64_t>(lhs) -
                                static_cast<uint64_t>(rhs));
}

static int64_t wrapSignedMul(int64_t lhs, int64_t rhs) {
    return static_cast<int64_t>(static_cast<uint64_t>(lhs) *
                                static_cast<uint64_t>(rhs));
}

static int64_t requireSignedInt(const Value& value) {
    if (!value.isSignedInt()) {
        throw std::runtime_error(
            "Type error: expected signed integer operand.");
    }
    return value.asSignedInt();
}

static uint64_t requireUnsignedInt(const Value& value) {
    if (!value.isUnsignedInt()) {
        throw std::runtime_error(
            "Type error: expected unsigned integer operand.");
    }
    return value.asUnsignedInt();
}

static bool hasStrictDirective(std::string_view source) {
    return source.rfind("#!strict", 0) == 0;
}

static std::string_view stripStrictDirectiveLine(std::string_view source) {
    if (!hasStrictDirective(source)) {
        return source;
    }

    size_t newlinePos = source.find('\n');
    if (newlinePos == std::string_view::npos) {
        return std::string_view();
    }

    return source.substr(newlinePos + 1);
}

static std::string valueToString(const Value& value) {
    if (value.isString()) return value.asString();
    if (value.isSignedInt()) return std::to_string(value.asSignedInt());
    if (value.isUnsignedInt()) return std::to_string(value.asUnsignedInt());
    if (value.isBool()) return value.asBool() ? "true" : "false";
    if (value.isNil()) return "null";

    std::ostringstream out;
    out << value;
    return out.str();
}

static std::string valueTypeName(const Value& value) {
    if (value.isAnyNumeric()) return "number";
    if (value.isBool()) return "bool";
    if (value.isNil()) return "null";
    if (value.isString()) return "string";
    if (value.isArray()) return "array";
    if (value.isDict()) return "dict";
    if (value.isSet()) return "set";
    if (value.isIterator()) return "iterator";
    if (value.isModule()) return "module";
    if (value.isNativeHandle()) return "native_handle";
    if (value.isClass()) return "class";
    if (value.isInstance()) return "instance";
    if (value.isNative()) return "native";
    if (value.isNativeBound()) return "native_bound_method";
    if (value.isBoundMethod()) return "bound_method";
    if (value.isClosure()) return "closure";
    if (value.isFunction()) return "function";
    return "unknown";
}

static bool toArrayIndex(const Value& value, size_t& index) {
    if (!value.isAnyNumeric()) {
        return false;
    }

    if (value.isSignedInt()) {
        int64_t signedIndex = value.asSignedInt();
        if (signedIndex < 0) {
            return false;
        }

        index = static_cast<size_t>(signedIndex);
        return true;
    }

    if (value.isUnsignedInt()) {
        index = static_cast<size_t>(value.asUnsignedInt());
        return true;
    }

    double number = value.asNumber();
    if (number < 0.0 || std::floor(number) != number) {
        return false;
    }

    index = static_cast<size_t>(number);
    return true;
}

static bool containsValue(const std::vector<Value>& elements,
                          const Value& needle) {
    for (const auto& element : elements) {
        if (element == needle) {
            return true;
        }
    }

    return false;
}

static bool setContainsValue(SetObject* set, const Value& needle) {
    return set->indexByValue.find(needle) != set->indexByValue.end();
}

template <typename ValueRef>
static bool setInsertValueImpl(SetObject* set, ValueRef&& element) {
    auto [indexIt, inserted] =
        set->indexByValue.emplace(element, set->elements.size());
    if (!inserted) {
        return false;
    }

    try {
        set->elements.push_back(std::forward<ValueRef>(element));
    } catch (...) {
        set->indexByValue.erase(indexIt);
        throw;
    }

    return true;
}

static bool setInsertValue(SetObject* set, const Value& element) {
    return setInsertValueImpl(set, element);
}

static bool setInsertValue(SetObject* set, Value&& element) {
    return setInsertValueImpl(set, std::move(element));
}

static bool setRemoveValue(SetObject* set, const Value& element) {
    auto indexIt = set->indexByValue.find(element);
    if (indexIt == set->indexByValue.end()) {
        return false;
    }

    size_t removeIndex = indexIt->second;
    size_t lastIndex = set->elements.size() - 1;

    if (removeIndex != lastIndex) {
        Value moved = set->elements[lastIndex];
        set->elements[removeIndex] = moved;
        auto movedIt = set->indexByValue.find(moved);
        if (movedIt != set->indexByValue.end()) {
            movedIt->second = removeIndex;
        }
    }

    set->elements.pop_back();
    set->indexByValue.erase(indexIt);
    return true;
}

static std::vector<Value> sortedDictKeys(const DictObject* dict) {
    std::vector<Value> orderedKeys;
    orderedKeys.reserve(dict->map.size());
    for (const auto& entry : dict->map) {
        orderedKeys.push_back(entry.first);
    }
    std::sort(orderedKeys.begin(), orderedKeys.end(), valueSortLess);
    return orderedKeys;
}

enum class BuiltinNativeKind : uint8_t {
    CLOCK,
    SQRT,
    LEN,
    TYPE,
    STR,
    TO_STRING,
    NUM,
    PARSE_INT,
    PARSE_UINT,
    PARSE_FLOAT,
    ABS,
    FLOOR,
    CEIL,
    POW,
    ERROR,
    SET,
};

static const ExprHostApi kExprHostApi = {EXPR_HOST_API_ABI_VERSION};

static bool valueToPackageValue(const Value& value, ExprPackageValue& out) {
    out = ExprPackageValue{};
    if (value.isNil()) {
        out.kind = EXPR_PACKAGE_VALUE_NULL;
        return true;
    }
    if (value.isBool()) {
        out.kind = EXPR_PACKAGE_VALUE_BOOL;
        out.as.boolean_value = value.asBool();
        return true;
    }
    if (value.isSignedInt()) {
        out.kind = EXPR_PACKAGE_VALUE_I64;
        out.as.i64_value = value.asSignedInt();
        return true;
    }
    if (value.isUnsignedInt()) {
        out.kind = EXPR_PACKAGE_VALUE_U64;
        out.as.u64_value = value.asUnsignedInt();
        return true;
    }
    if (value.isNumber()) {
        out.kind = EXPR_PACKAGE_VALUE_F64;
        out.as.f64_value = value.asNumber();
        return true;
    }
    if (value.isString()) {
        out.kind = EXPR_PACKAGE_VALUE_STR;
        out.as.string_value.data = value.asString().c_str();
        out.as.string_value.length = value.asString().size();
        return true;
    }
    if (value.isNativeHandle()) {
        NativeHandleObject* handle = value.asNativeHandle();
        if (handle == nullptr || handle->handleData == nullptr ||
            handle->finalizer == nullptr || handle->packageNamespace.empty() ||
            handle->packageName.empty() || handle->typeName.empty()) {
            return false;
        }

        out.kind = EXPR_PACKAGE_VALUE_HANDLE;
        out.as.handle_value.package_namespace = handle->packageNamespace.c_str();
        out.as.handle_value.package_name = handle->packageName.c_str();
        out.as.handle_value.type_name = handle->typeName.c_str();
        out.as.handle_value.handle_data = handle->handleData;
        out.as.handle_value.finalizer = handle->finalizer;
        return true;
    }

    return false;
}

bool packageValueToValue(VirtualMachine& vm, const ExprPackageValue& value,
                         Value& outValue, std::string& outError) {
    switch (value.kind) {
        case EXPR_PACKAGE_VALUE_NULL:
            outValue = Value();
            return true;
        case EXPR_PACKAGE_VALUE_BOOL:
            outValue = Value(value.as.boolean_value);
            return true;
        case EXPR_PACKAGE_VALUE_I64:
            outValue = Value(static_cast<int64_t>(value.as.i64_value));
            return true;
        case EXPR_PACKAGE_VALUE_U64:
            outValue = Value(static_cast<uint64_t>(value.as.u64_value));
            return true;
        case EXPR_PACKAGE_VALUE_F64:
            outValue = Value(value.as.f64_value);
            return true;
        case EXPR_PACKAGE_VALUE_STR: {
            std::string text;
            if (value.as.string_value.data != nullptr &&
                value.as.string_value.length > 0) {
                text.assign(value.as.string_value.data,
                            value.as.string_value.length);
            }
            outValue = vm.makeStringValue(std::move(text));
            return true;
        }
        case EXPR_PACKAGE_VALUE_HANDLE: {
            const ExprPackageHandleValue& handleValue = value.as.handle_value;
            if (handleValue.package_namespace == nullptr ||
                handleValue.package_namespace[0] == '\0' ||
                handleValue.package_name == nullptr ||
                handleValue.package_name[0] == '\0' ||
                handleValue.type_name == nullptr ||
                handleValue.type_name[0] == '\0' ||
                handleValue.handle_data == nullptr ||
                handleValue.finalizer == nullptr) {
                outError = "Native package returned invalid handle metadata.";
                return false;
            }

            if (!isValidHandleTypeName(handleValue.type_name)) {
                outError = "Native package returned handle with invalid type "
                           "name.";
                return false;
            }

            auto* handle = vm.gcAlloc<NativeHandleObject>();
            handle->packageNamespace = handleValue.package_namespace;
            handle->packageName = handleValue.package_name;
            handle->packageId =
                makePackageId(handle->packageNamespace, handle->packageName);
            handle->typeName = handleValue.type_name;
            handle->handleData = handleValue.handle_data;
            handle->finalizer = handleValue.finalizer;
            outValue = Value(handle);
            return true;
        }
        default:
            outError = "Native package returned unsupported value kind.";
            return false;
    }
}

static const char* builtinKindName(BuiltinNativeKind kind) {
    switch (kind) {
        case BuiltinNativeKind::CLOCK:
            return "clock";
        case BuiltinNativeKind::SQRT:
            return "sqrt";
        case BuiltinNativeKind::LEN:
            return "len";
        case BuiltinNativeKind::TYPE:
            return "type";
        case BuiltinNativeKind::STR:
            return "str";
        case BuiltinNativeKind::TO_STRING:
            return "toString";
        case BuiltinNativeKind::NUM:
            return "num";
        case BuiltinNativeKind::PARSE_INT:
            return "parseInt";
        case BuiltinNativeKind::PARSE_UINT:
            return "parseUInt";
        case BuiltinNativeKind::PARSE_FLOAT:
            return "parseFloat";
        case BuiltinNativeKind::ABS:
            return "abs";
        case BuiltinNativeKind::FLOOR:
            return "floor";
        case BuiltinNativeKind::CEIL:
            return "ceil";
        case BuiltinNativeKind::POW:
            return "pow";
        case BuiltinNativeKind::ERROR:
            return "error";
        case BuiltinNativeKind::SET:
            return "Set";
        default:
            return "<native>";
    }
}

static const void* builtinNativeTag(const std::string& name) {
    static constexpr BuiltinNativeKind kClock = BuiltinNativeKind::CLOCK;
    static constexpr BuiltinNativeKind kSqrt = BuiltinNativeKind::SQRT;
    static constexpr BuiltinNativeKind kLen = BuiltinNativeKind::LEN;
    static constexpr BuiltinNativeKind kType = BuiltinNativeKind::TYPE;
    static constexpr BuiltinNativeKind kStr = BuiltinNativeKind::STR;
    static constexpr BuiltinNativeKind kToString = BuiltinNativeKind::TO_STRING;
    static constexpr BuiltinNativeKind kNum = BuiltinNativeKind::NUM;
    static constexpr BuiltinNativeKind kParseInt = BuiltinNativeKind::PARSE_INT;
    static constexpr BuiltinNativeKind kParseUInt =
        BuiltinNativeKind::PARSE_UINT;
    static constexpr BuiltinNativeKind kParseFloat =
        BuiltinNativeKind::PARSE_FLOAT;
    static constexpr BuiltinNativeKind kAbs = BuiltinNativeKind::ABS;
    static constexpr BuiltinNativeKind kFloor = BuiltinNativeKind::FLOOR;
    static constexpr BuiltinNativeKind kCeil = BuiltinNativeKind::CEIL;
    static constexpr BuiltinNativeKind kPow = BuiltinNativeKind::POW;
    static constexpr BuiltinNativeKind kError = BuiltinNativeKind::ERROR;
    static constexpr BuiltinNativeKind kSet = BuiltinNativeKind::SET;

    if (name == "clock") return &kClock;
    if (name == "sqrt") return &kSqrt;
    if (name == "len") return &kLen;
    if (name == "type") return &kType;
    if (name == "str") return &kStr;
    if (name == "toString") return &kToString;
    if (name == "num") return &kNum;
    if (name == "parseInt") return &kParseInt;
    if (name == "parseUInt") return &kParseUInt;
    if (name == "parseFloat") return &kParseFloat;
    if (name == "abs") return &kAbs;
    if (name == "floor") return &kFloor;
    if (name == "ceil") return &kCeil;
    if (name == "pow") return &kPow;
    if (name == "error") return &kError;
    if (name == "Set") return &kSet;
    return nullptr;
}

static NativeMethodId resolveArrayMethodId(const std::string& name) {
    if (name == "push") return NativeMethodId::ARRAY_PUSH;
    if (name == "pop") return NativeMethodId::ARRAY_POP;
    if (name == "size") return NativeMethodId::ARRAY_SIZE;
    if (name == "has") return NativeMethodId::ARRAY_HAS;
    if (name == "insert") return NativeMethodId::ARRAY_INSERT;
    if (name == "remove") return NativeMethodId::ARRAY_REMOVE;
    if (name == "clear") return NativeMethodId::ARRAY_CLEAR;
    if (name == "isEmpty") return NativeMethodId::ARRAY_IS_EMPTY;
    if (name == "first") return NativeMethodId::ARRAY_FIRST;
    if (name == "last") return NativeMethodId::ARRAY_LAST;
    return NativeMethodId::INVALID;
}

static NativeMethodId resolveDictMethodId(const std::string& name) {
    if (name == "get") return NativeMethodId::DICT_GET;
    if (name == "set") return NativeMethodId::DICT_SET;
    if (name == "has") return NativeMethodId::DICT_HAS;
    if (name == "keys") return NativeMethodId::DICT_KEYS;
    if (name == "values") return NativeMethodId::DICT_VALUES;
    if (name == "size") return NativeMethodId::DICT_SIZE;
    if (name == "remove") return NativeMethodId::DICT_REMOVE;
    if (name == "clear") return NativeMethodId::DICT_CLEAR;
    if (name == "isEmpty") return NativeMethodId::DICT_IS_EMPTY;
    if (name == "getOr") return NativeMethodId::DICT_GET_OR;
    return NativeMethodId::INVALID;
}

static NativeMethodId resolveSetMethodId(const std::string& name) {
    if (name == "add") return NativeMethodId::SET_ADD;
    if (name == "has") return NativeMethodId::SET_HAS;
    if (name == "remove") return NativeMethodId::SET_REMOVE;
    if (name == "size") return NativeMethodId::SET_SIZE;
    if (name == "toArray") return NativeMethodId::SET_TO_ARRAY;
    if (name == "clear") return NativeMethodId::SET_CLEAR;
    if (name == "isEmpty") return NativeMethodId::SET_IS_EMPTY;
    if (name == "union") return NativeMethodId::SET_UNION;
    if (name == "intersect") return NativeMethodId::SET_INTERSECT;
    if (name == "difference") return NativeMethodId::SET_DIFFERENCE;
    return NativeMethodId::INVALID;
}

static NativeMethodId resolveNativeMethodId(const Value& receiver,
                                            const std::string& name) {
    if (receiver.isArray()) {
        return resolveArrayMethodId(name);
    }
    if (receiver.isDict()) {
        return resolveDictMethodId(name);
    }
    if (receiver.isSet()) {
        return resolveSetMethodId(name);
    }
    return NativeMethodId::INVALID;
}

static ClosureObject* findMethodClosure(ClassObject* klass,
                                        const std::string& name) {
    ClassObject* current = klass;
    while (current) {
        auto it = current->methods.find(name);
        if (it != current->methods.end()) {
            return it->second;
        }
        current = current->superclass;
    }

    return nullptr;
}

static void rebuildFieldLayout(ClassObject* klass) {
    if (!klass) {
        return;
    }

    klass->fieldIndexByName.clear();

    std::vector<std::string> orderedFieldNames;
    if (klass->superclass) {
        orderedFieldNames = klass->superclass->fieldNames;
        for (size_t index = 0; index < orderedFieldNames.size(); ++index) {
            klass->fieldIndexByName.emplace(orderedFieldNames[index], index);
        }
    }

    std::vector<std::string> ownFieldNames;
    ownFieldNames.reserve(klass->fieldTypes.size());
    for (const auto& [name, type] : klass->fieldTypes) {
        (void)type;
        if (klass->fieldIndexByName.find(name) ==
            klass->fieldIndexByName.end()) {
            ownFieldNames.push_back(name);
        }
    }

    std::sort(ownFieldNames.begin(), ownFieldNames.end());
    for (const auto& name : ownFieldNames) {
        klass->fieldIndexByName.emplace(name, orderedFieldNames.size());
        orderedFieldNames.push_back(name);
    }

    klass->fieldNames = std::move(orderedFieldNames);
}

static bool isInstanceOfClass(const InstanceObject* instance,
                              const std::string& expectedClassName) {
    if (!instance || !instance->klass) {
        return false;
    }

    ClassObject* current = instance->klass;
    while (current != nullptr) {
        if (current->name == expectedClassName) {
            return true;
        }
        current = current->superclass;
    }

    return false;
}

static TypeRef inferRuntimeType(const Value& value) {
    if (value.isSignedInt()) {
        return TypeInfo::makeI64();
    }
    if (value.isUnsignedInt()) {
        return TypeInfo::makeU64();
    }
    if (value.isNumber()) {
        return TypeInfo::makeF64();
    }
    if (value.isBool()) {
        return TypeInfo::makeBool();
    }
    if (value.isString()) {
        return TypeInfo::makeStr();
    }
    if (value.isNil()) {
        return TypeInfo::makeNull();
    }
    if (value.isNativeHandle()) {
        auto* handle = value.asNativeHandle();
        if (handle != nullptr) {
            return TypeInfo::makeNativeHandle(handle->packageId,
                                              handle->typeName);
        }
    }
    if (value.isInstance() && value.asInstance() && value.asInstance()->klass) {
        return TypeInfo::makeClass(value.asInstance()->klass->name);
    }
    if (value.isArray()) {
        auto array = value.asArray();
        return TypeInfo::makeArray(array && array->elementType
                                       ? array->elementType
                                       : TypeInfo::makeAny());
    }
    if (value.isDict()) {
        auto dict = value.asDict();
        TypeRef key =
            dict && dict->keyType ? dict->keyType : TypeInfo::makeAny();
        TypeRef val =
            dict && dict->valueType ? dict->valueType : TypeInfo::makeAny();
        return TypeInfo::makeDict(key, val);
    }
    if (value.isSet()) {
        auto set = value.asSet();
        return TypeInfo::makeSet(set && set->elementType ? set->elementType
                                                         : TypeInfo::makeAny());
    }

    return TypeInfo::makeAny();
}

static bool valueMatchesType(const Value& value, const TypeRef& expected) {
    if (!expected || expected->isAny()) {
        return true;
    }

    if (expected->kind == TypeKind::OPTIONAL) {
        if (value.isNil()) {
            return true;
        }
        return valueMatchesType(value, expected->innerType);
    }

    if (expected->kind == TypeKind::NULL_TYPE) {
        return value.isNil();
    }

    if (expected->isInteger()) {
        return value.isAnyNumeric();
    }

    if (expected->isFloat()) {
        return value.isAnyNumeric();
    }

    if (expected->kind == TypeKind::BOOL) {
        return value.isBool();
    }

    if (expected->kind == TypeKind::STR) {
        return value.isString();
    }

    if (expected->kind == TypeKind::FUNCTION) {
        return value.isClosure() || value.isFunction() || value.isNative() ||
               value.isBoundMethod() || value.isNativeBound();
    }

    if (expected->kind == TypeKind::CLASS) {
        if (value.isNativeHandle()) {
            auto* handle = value.asNativeHandle();
            return handle != nullptr && handle->typeName == expected->className;
        }
        if (!value.isInstance()) {
            return false;
        }
        return isInstanceOfClass(value.asInstance(), expected->className);
    }

    if (expected->kind == TypeKind::NATIVE_HANDLE) {
        if (!value.isNativeHandle()) {
            return false;
        }

        auto* handle = value.asNativeHandle();
        return handle != nullptr &&
               handle->packageId == expected->nativeHandlePackageId &&
               handle->typeName == expected->nativeHandleTypeName;
    }

    if (expected->kind == TypeKind::ARRAY) {
        if (!value.isArray()) {
            return false;
        }

        auto array = value.asArray();
        TypeRef elementType =
            expected->elementType ? expected->elementType : TypeInfo::makeAny();
        if (elementType->isAny()) {
            return true;
        }

        TypeRef runtimeElementType =
            array->elementType ? array->elementType : TypeInfo::makeAny();
        if (!runtimeElementType->isAny()) {
            return isAssignable(runtimeElementType, elementType);
        }

        for (const auto& element : array->elements) {
            if (!valueMatchesType(element, elementType)) {
                return false;
            }
        }
        return true;
    }

    if (expected->kind == TypeKind::DICT) {
        if (!value.isDict()) {
            return false;
        }

        auto dict = value.asDict();
        TypeRef valueType =
            expected->valueType ? expected->valueType : TypeInfo::makeAny();
        if (valueType->isAny()) {
            return true;
        }

        TypeRef runtimeValueType =
            dict->valueType ? dict->valueType : TypeInfo::makeAny();
        if (!runtimeValueType->isAny()) {
            return isAssignable(runtimeValueType, valueType);
        }

        for (const auto& entry : dict->map) {
            if (!valueMatchesType(entry.second, valueType)) {
                return false;
            }
        }
        return true;
    }

    if (expected->kind == TypeKind::SET) {
        if (!value.isSet()) {
            return false;
        }

        auto set = value.asSet();
        TypeRef elementType =
            expected->elementType ? expected->elementType : TypeInfo::makeAny();
        if (elementType->isAny()) {
            return true;
        }

        TypeRef runtimeElementType =
            set->elementType ? set->elementType : TypeInfo::makeAny();
        if (!runtimeElementType->isAny()) {
            return isAssignable(runtimeElementType, elementType);
        }

        for (const auto& element : set->elements) {
            if (!valueMatchesType(element, elementType)) {
                return false;
            }
        }
        return true;
    }

    TypeRef actual = inferRuntimeType(value);
    return isAssignable(actual, expected);
}

UpvalueObject* VirtualMachine::captureUpvalue(size_t stackIndex) {
    UpvalueObject* previous = nullptr;
    UpvalueObject* current = m_openUpvaluesHead;

    while (current != nullptr && current->stackIndex > stackIndex) {
        previous = current;
        current = current->nextOpen;
    }

    if (current != nullptr && current->stackIndex == stackIndex) {
        return current;
    }

    auto* created = gcAlloc<UpvalueObject>();
    created->stackIndex = stackIndex;
    created->isClosed = false;
    created->nextOpen = current;

    if (previous == nullptr) {
        m_openUpvaluesHead = created;
    } else {
        previous->nextOpen = created;
    }

    return created;
}

void VirtualMachine::closeUpvalues(size_t fromStackIndex) {
    while (m_openUpvaluesHead != nullptr &&
           m_openUpvaluesHead->stackIndex >= fromStackIndex) {
        UpvalueObject* upvalue = m_openUpvaluesHead;
        upvalue->closed = m_stack.getAt(upvalue->stackIndex);
        upvalue->isClosed = true;

        m_openUpvaluesHead = upvalue->nextOpen;
        upvalue->nextOpen = nullptr;
    }
}

void VirtualMachine::markRoots() {
    for (size_t i = 0; i < m_stack.size(); ++i) {
        m_gc.markValue(m_stack.getAt(i));
    }

    for (size_t i = 0; i < m_globalValues.size(); ++i) {
        if (m_globalDefined[i]) {
            m_gc.markValue(m_globalValues[i]);
        }
    }

    UpvalueObject* upvalue = m_openUpvaluesHead;
    while (upvalue != nullptr) {
        m_gc.markObject(upvalue);
        upvalue = upvalue->nextOpen;
    }

    for (const auto& [path, module] : m_moduleCache) {
        (void)path;
        m_gc.markObject(module);
    }

    m_gc.markObject(m_currentModule);

    std::array<const Chunk*, MAX_FRAMES> visitedRootChunks{};
    size_t visitedRootChunkCount = 0;
    for (size_t i = 0; i < m_frameCount; ++i) {
        const auto& frame = m_frames[i];
        m_gc.markObject(frame.receiver);
        m_gc.markObject(frame.closure);

        // Constants for closure-backed frames are already reached via
        // frame.closure -> function -> chunk trace.
        if (frame.chunk != nullptr && frame.closure == nullptr) {
            bool seen = false;
            for (size_t chunkIndex = 0; chunkIndex < visitedRootChunkCount;
                 ++chunkIndex) {
                if (visitedRootChunks[chunkIndex] == frame.chunk) {
                    seen = true;
                    break;
                }
            }

            if (seen) {
                continue;
            }

            if (visitedRootChunkCount < visitedRootChunks.size()) {
                visitedRootChunks[visitedRootChunkCount++] = frame.chunk;
            }

            for (const auto& constant : frame.chunk->getConstantsRange()) {
                m_gc.markValue(constant);
            }
        }
    }
}

void VirtualMachine::collectGarbage() {
    markRoots();
    m_gc.drainGrayStack();
    m_gc.sweep();
}

void VirtualMachine::printStackTrace() {
    std::cerr << "[trace][runtime] stack:" << std::endl;

    for (int index = static_cast<int>(m_frameCount) - 1; index >= 0; --index) {
        const CallFrame& frame = m_frames[index];
        int offset = static_cast<int>(frame.ip - frame.chunk->getBytes()) - 1;
        if (offset < 0) {
            offset = 0;
        }

        std::string functionName = "<script>";
        if (frame.closure && frame.closure->function) {
            if (!frame.closure->function->name.empty()) {
                functionName = frame.closure->function->name;
            } else {
                functionName = "<anonymous>";
            }
        }

        std::cerr << "  at " << functionName << "() [line "
                  << frame.chunk->lineAt(offset) << "]" << std::endl;
    }
}

Status VirtualMachine::runtimeError(const std::string& message) {
    CallFrame& frame = currentFrame();
    int offset = static_cast<int>(frame.ip - frame.chunk->getBytes()) - 1;
    if (offset < 0) {
        offset = 0;
    }

    std::cerr << "[error][runtime][line " << frame.chunk->lineAt(offset) << "] "
              << message << std::endl;
    printStackTrace();
    return Status::RUNTIME_ERROR;
}

Value VirtualMachine::makeStringValue(std::string text) {
    auto* stringObject = gcAlloc<StringObject>();
    stringObject->value = std::move(text);
    return Value(stringObject);
}

Status invokeBuiltinNative(VirtualMachine& vm, const NativeFunctionObject& native,
                           uint8_t argumentCount, size_t calleeIndex) {
    if (native.userdata == nullptr) {
        return vm.runtimeError("Unknown native function '" + native.name + "'.");
    }

    const auto kind =
        *static_cast<const BuiltinNativeKind*>(native.userdata);
    const size_t argBase = calleeIndex + 1;
    auto argAt = [&](uint8_t index) -> const Value& {
        return vm.m_stack.getAt(argBase + static_cast<size_t>(index));
    };
    auto argAtRef = [&](uint8_t index) -> Value& {
        return vm.m_stack.getAtRef(argBase + static_cast<size_t>(index));
    };

    Value result;
    switch (kind) {
        case BuiltinNativeKind::CLOCK: {
            auto now = std::chrono::duration<double>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
            result = Value(now);
            break;
        }
        case BuiltinNativeKind::SQRT: {
            const Value& arg = argAt(0);
            if (!arg.isAnyNumeric()) {
                return vm.runtimeError(
                    "Native function 'sqrt' expects a number.");
            }

            double number = 0.0;
            valueToDouble(arg, number);
            if (number < 0.0) {
                return vm.runtimeError(
                    "Native function 'sqrt' cannot take negative numbers.");
            }

            result = Value(std::sqrt(number));
            break;
        }
        case BuiltinNativeKind::LEN: {
            const Value& arg = argAt(0);
            if (arg.isString()) {
                result = Value(static_cast<int64_t>(arg.asString().length()));
            } else if (arg.isArray()) {
                result =
                    Value(static_cast<int64_t>(arg.asArray()->elements.size()));
            } else if (arg.isDict()) {
                result = Value(static_cast<int64_t>(arg.asDict()->map.size()));
            } else if (arg.isSet()) {
                result = Value(static_cast<int64_t>(arg.asSet()->elements.size()));
            } else {
                return vm.runtimeError("Native function 'len' expects a "
                                       "string, array, dict, or set.");
            }
            break;
        }
        case BuiltinNativeKind::TYPE:
            result = vm.makeStringValue(valueTypeName(argAt(0)));
            break;
        case BuiltinNativeKind::STR:
        case BuiltinNativeKind::TO_STRING: {
            const Value& arg = argAt(0);
            result =
                arg.isString() ? arg : vm.makeStringValue(valueToString(arg));
            break;
        }
        case BuiltinNativeKind::NUM: {
            const Value& arg = argAt(0);
            if (arg.isNumber()) {
                result = arg;
            } else if (arg.isSignedInt() || arg.isUnsignedInt()) {
                double number = 0.0;
                valueToDouble(arg, number);
                result = Value(number);
            } else if (arg.isString()) {
                const std::string& text = arg.asString();
                size_t parseIndex = 0;
                try {
                    double number = std::stod(text, &parseIndex);
                    if (parseIndex != text.size()) {
                        return vm.runtimeError("Native function 'num' expects "
                                               "a numeric string.");
                    }
                    result = Value(number);
                } catch (const std::exception&) {
                    return vm.runtimeError(
                        "Native function 'num' expects a numeric string.");
                }
            } else {
                return vm.runtimeError(
                    "Native function 'num' expects a number or string.");
            }
            break;
        }
        case BuiltinNativeKind::PARSE_INT: {
            const Value& arg = argAt(0);
            if (!arg.isString()) {
                return vm.runtimeError(
                    "Native function 'parseInt' expects a string.");
            }

            size_t parseIndex = 0;
            try {
                int64_t parsed = std::stoll(arg.asString(), &parseIndex, 10);
                if (parseIndex != arg.asString().size()) {
                    return vm.runtimeError("Native function 'parseInt' expects "
                                           "an integer string.");
                }
                result = Value(parsed);
            } catch (const std::exception&) {
                return vm.runtimeError("Native function 'parseInt' expects an "
                                       "integer string.");
            }
            break;
        }
        case BuiltinNativeKind::PARSE_UINT: {
            const Value& arg = argAt(0);
            if (!arg.isString()) {
                return vm.runtimeError(
                    "Native function 'parseUInt' expects a string.");
            }

            size_t parseIndex = 0;
            try {
                uint64_t parsed = std::stoull(arg.asString(), &parseIndex, 10);
                if (parseIndex != arg.asString().size()) {
                    return vm.runtimeError("Native function 'parseUInt' expects "
                                           "an unsigned integer string.");
                }
                result = Value(parsed);
            } catch (const std::exception&) {
                return vm.runtimeError("Native function 'parseUInt' expects "
                                       "an unsigned integer string.");
            }
            break;
        }
        case BuiltinNativeKind::PARSE_FLOAT: {
            const Value& arg = argAt(0);
            if (!arg.isString()) {
                return vm.runtimeError(
                    "Native function 'parseFloat' expects a string.");
            }

            size_t parseIndex = 0;
            try {
                double parsed = std::stod(arg.asString(), &parseIndex);
                if (parseIndex != arg.asString().size()) {
                    return vm.runtimeError("Native function 'parseFloat' "
                                           "expects a numeric string.");
                }
                result = Value(parsed);
            } catch (const std::exception&) {
                return vm.runtimeError("Native function 'parseFloat' expects "
                                       "a numeric string.");
            }
            break;
        }
        case BuiltinNativeKind::ABS: {
            const Value& arg = argAt(0);
            if (!arg.isAnyNumeric()) {
                return vm.runtimeError(
                    "Native function 'abs' expects a number.");
            }

            double number = 0.0;
            valueToDouble(arg, number);
            result = Value(std::fabs(number));
            break;
        }
        case BuiltinNativeKind::FLOOR: {
            const Value& arg = argAt(0);
            if (!arg.isAnyNumeric()) {
                return vm.runtimeError(
                    "Native function 'floor' expects a number.");
            }

            double number = 0.0;
            valueToDouble(arg, number);
            result = Value(std::floor(number));
            break;
        }
        case BuiltinNativeKind::CEIL: {
            const Value& arg = argAt(0);
            if (!arg.isAnyNumeric()) {
                return vm.runtimeError(
                    "Native function 'ceil' expects a number.");
            }

            double number = 0.0;
            valueToDouble(arg, number);
            result = Value(std::ceil(number));
            break;
        }
        case BuiltinNativeKind::POW: {
            double base = 0.0;
            double exponent = 0.0;
            if (!valueToDouble(argAt(0), base) ||
                !valueToDouble(argAt(1), exponent)) {
                return vm.runtimeError(
                    "Native function 'pow' expects numeric arguments.");
            }

            result = Value(std::pow(base, exponent));
            break;
        }
        case BuiltinNativeKind::ERROR: {
            const Value& arg = argAt(0);
            if (!arg.isString()) {
                return vm.runtimeError(
                    "Native function 'error' expects a string.");
            }

            return vm.runtimeError(arg.asString());
        }
        case BuiltinNativeKind::SET: {
            auto set = vm.gcAlloc<SetObject>();
            set->elements.reserve(argumentCount);
            set->indexByValue.reserve(argumentCount);
            for (uint8_t i = 0; i < argumentCount; ++i) {
                Value& arg = argAtRef(i);
                if (set->elementType->isAny()) {
                    set->elementType = inferRuntimeType(arg);
                } else if (!valueMatchesType(arg, set->elementType)) {
                    return vm.runtimeError("Native function 'Set' expects all "
                                           "elements to have a consistent "
                                           "type.");
                }

                setInsertValue(set, std::move(arg));
            }

            result = Value(set);
            break;
        }
    }

    vm.m_stack.popN(vm.m_stack.size() - calleeIndex);
    vm.m_stack.push(std::move(result));
    return Status::OK;
}

Status invokePackageNative(VirtualMachine& vm, const NativeFunctionObject& native,
                           uint8_t argumentCount, size_t calleeIndex) {
    auto* binding =
        static_cast<const NativePackageBinding*>(native.userdata);
    if (binding == nullptr || binding->function == nullptr ||
        binding->function->callback == nullptr) {
        return vm.runtimeError("Native package function '" + native.name +
                               "' is not callable.");
    }

    std::vector<ExprPackageValue> packageArgs(argumentCount);
    const size_t argBase = calleeIndex + 1;
    for (uint8_t index = 0; index < argumentCount; ++index) {
        const Value& arg = vm.m_stack.getAt(argBase + static_cast<size_t>(index));
        if (arg.isNativeHandle()) {
            NativeHandleObject* handle = arg.asNativeHandle();
            if (handle == nullptr || handle->packageId != binding->packageId) {
                return vm.runtimeError("Native package function '" +
                                       binding->packageId + "." + native.name +
                                       "' cannot accept a foreign native "
                                       "handle.");
            }
        }

        if (!valueToPackageValue(arg, packageArgs[index])) {
            return vm.runtimeError("Native package function '" + native.name +
                                   "' cannot accept runtime value of type '" +
                                   valueTypeName(arg) + "'.");
        }
    }

    ExprPackageValue resultValue{};
    ExprPackageStringView errorView{nullptr, 0};
    bool ok = binding->function->callback(&kExprHostApi, packageArgs.data(),
                                          packageArgs.size(), &resultValue,
                                          &errorView);
    if (!ok) {
        std::string message = "Native package function '" + binding->packageId +
                              "." + native.name + "' failed.";
        if (errorView.data != nullptr && errorView.length > 0) {
            message = "Native package function '" + binding->packageId + "." +
                      native.name + "' failed: " +
                      std::string(errorView.data, errorView.length);
        }
        return vm.runtimeError(message);
    }

    if (resultValue.kind == EXPR_PACKAGE_VALUE_HANDLE) {
        const ExprPackageHandleValue& handleValue = resultValue.as.handle_value;
        if (handleValue.package_namespace == nullptr ||
            handleValue.package_name == nullptr ||
            binding->packageNamespace != handleValue.package_namespace ||
            binding->packageName != handleValue.package_name) {
            return vm.runtimeError("Native package function '" +
                                   binding->packageId + "." + native.name +
                                   "' returned a handle owned by a different "
                                   "package.");
        }
    }

    Value result;
    std::string conversionError;
    if (!packageValueToValue(vm, resultValue, result, conversionError)) {
        return vm.runtimeError("Native package function '" +
                               binding->packageId + "." + native.name +
                               "' returned invalid value: " + conversionError);
    }

    vm.m_stack.popN(vm.m_stack.size() - calleeIndex);
    vm.m_stack.push(std::move(result));
    return Status::OK;
}

Status VirtualMachine::callClosure(ClosureObject* closure,
                                   uint8_t argumentCount,
                                   InstanceObject* receiver) {
    auto function = closure->function;
    if (function->parameters.size() != argumentCount) {
        return runtimeError("Function '" + function->name + "' expected " +
                            std::to_string(function->parameters.size()) +
                            " arguments but got " +
                            std::to_string(static_cast<int>(argumentCount)) +
                            ".");
    }

    size_t calleeIndex =
        m_stack.size() - static_cast<size_t>(argumentCount) - 1;

    if (m_frameCount >= MAX_FRAMES) {
        return runtimeError("Stack overflow: too many nested calls.");
    }

    m_frames[m_frameCount++] = CallFrame{function->chunk.get(),
                                         function->chunk->getBytes(),
                                         calleeIndex + 1,
                                         calleeIndex,
                                         receiver,
                                         closure};
    m_activeFrame = &m_frames[m_frameCount - 1];
    return Status::OK;
}

Status VirtualMachine::callNativeMethod(NativeMethodId id,
                                        const std::string& name,
                                        const Value& receiver,
                                        uint8_t argumentCount,
                                        size_t calleeIndex) {
    const size_t argBase = calleeIndex + 1;
    auto argAt = [&](uint8_t index) -> const Value& {
        return m_stack.getAt(argBase + static_cast<size_t>(index));
    };
    auto argAtRef = [&](uint8_t index) -> Value& {
        return m_stack.getAtRef(argBase + static_cast<size_t>(index));
    };

    Value result;

    if (receiver.isArray()) {
        auto array = receiver.asArray();

        if (id == NativeMethodId::ARRAY_PUSH) {
            if (argumentCount != 1) {
                return runtimeError("Array method 'push' expects 1 argument.");
            }

            if (array->elementType->isAny()) {
                array->elementType = inferRuntimeType(argAt(0));
            } else if (!valueMatchesType(argAt(0), array->elementType)) {
                return runtimeError("Array method 'push' expects value of "
                                    "type '" +
                                    array->elementType->toString() + "'.");
            }

            array->elements.push_back(std::move(argAtRef(0)));
            result = Value(static_cast<double>(array->elements.size()));
        } else if (id == NativeMethodId::ARRAY_POP) {
            if (argumentCount != 0) {
                return runtimeError("Array method 'pop' expects 0 arguments.");
            }

            if (array->elements.empty()) {
                return runtimeError(
                    "Array method 'pop' called on empty array.");
            }

            result = array->elements.back();
            array->elements.pop_back();
        } else if (id == NativeMethodId::ARRAY_SIZE) {
            if (argumentCount != 0) {
                return runtimeError("Array method 'size' expects 0 arguments.");
            }

            result = Value(static_cast<double>(array->elements.size()));
        } else if (id == NativeMethodId::ARRAY_HAS) {
            if (argumentCount != 1) {
                return runtimeError("Array method 'has' expects 1 argument.");
            }

            result = Value(containsValue(array->elements, argAt(0)));
        } else if (id == NativeMethodId::ARRAY_INSERT) {
            if (argumentCount != 2) {
                return runtimeError(
                    "Array method 'insert' expects 2 arguments.");
            }

            size_t index = 0;
            if (!toArrayIndex(argAt(0), index)) {
                return runtimeError("Array method 'insert' expects a "
                                    "non-negative integer index.");
            }

            if (index > array->elements.size()) {
                return runtimeError(
                    "Array method 'insert' index out of bounds.");
            }

            if (array->elementType->isAny()) {
                array->elementType = inferRuntimeType(argAt(1));
            } else if (!valueMatchesType(argAt(1), array->elementType)) {
                return runtimeError("Array method 'insert' expects value of "
                                    "type '" +
                                    array->elementType->toString() + "'.");
            }

            array->elements.insert(array->elements.begin() +
                                       static_cast<long>(index),
                                   std::move(argAtRef(1)));
            result = array->elements[index];
        } else if (id == NativeMethodId::ARRAY_REMOVE) {
            if (argumentCount != 1) {
                return runtimeError(
                    "Array method 'remove' expects 1 argument.");
            }

            size_t index = 0;
            if (!toArrayIndex(argAt(0), index)) {
                return runtimeError("Array method 'remove' expects a "
                                    "non-negative integer index.");
            }

            if (index >= array->elements.size()) {
                return runtimeError(
                    "Array method 'remove' index out of bounds.");
            }

            result = array->elements[index];
            array->elements.erase(array->elements.begin() +
                                  static_cast<long>(index));
        } else if (id == NativeMethodId::ARRAY_CLEAR) {
            if (argumentCount != 0) {
                return runtimeError("Array method 'clear' expects 0 arguments.");
            }

            double removed = static_cast<double>(array->elements.size());
            array->elements.clear();
            result = Value(removed);
        } else if (id == NativeMethodId::ARRAY_IS_EMPTY) {
            if (argumentCount != 0) {
                return runtimeError(
                    "Array method 'isEmpty' expects 0 arguments.");
            }

            result = Value(array->elements.empty());
        } else if (id == NativeMethodId::ARRAY_FIRST) {
            if (argumentCount != 0) {
                return runtimeError("Array method 'first' expects 0 arguments.");
            }

            if (array->elements.empty()) {
                return runtimeError(
                    "Array method 'first' called on empty array.");
            }

            result = array->elements.front();
        } else if (id == NativeMethodId::ARRAY_LAST) {
            if (argumentCount != 0) {
                return runtimeError("Array method 'last' expects 0 arguments.");
            }

            if (array->elements.empty()) {
                return runtimeError(
                    "Array method 'last' called on empty array.");
            }

            result = array->elements.back();
        } else {
            return runtimeError("Undefined array method '" + name + "'.");
        }
    } else if (receiver.isDict()) {
        auto dict = receiver.asDict();

        if (id == NativeMethodId::DICT_GET) {
            if (argumentCount != 1) {
                return runtimeError("Dict method 'get' expects 1 argument.");
            }

            Value& key = argAtRef(0);
            auto it = dict->map.find(key);
            if (it == dict->map.end()) {
                return runtimeError("Dict method 'get' key not found.");
            }

            result = it->second;
        } else if (id == NativeMethodId::DICT_SET) {
            if (argumentCount != 2) {
                return runtimeError("Dict method 'set' expects 2 arguments.");
            }

            const Value& key = argAt(0);

            if (dict->keyType->isAny()) {
                dict->keyType = inferRuntimeType(key);
            } else if (!valueMatchesType(key, dict->keyType)) {
                return runtimeError("Dict method 'set' key expects type '" +
                                    dict->keyType->toString() + "'.");
            }

            Value& storedValue = argAtRef(1);
            if (dict->valueType->isAny()) {
                dict->valueType = inferRuntimeType(storedValue);
            } else if (!valueMatchesType(storedValue, dict->valueType)) {
                return runtimeError("Dict method 'set' value expects type '" +
                                    dict->valueType->toString() + "'.");
            }

            auto [it, inserted] = dict->map.insert_or_assign(
                std::move(key), std::move(storedValue));
            (void)inserted;
            result = it->second;
        } else if (id == NativeMethodId::DICT_HAS) {
            if (argumentCount != 1) {
                return runtimeError("Dict method 'has' expects 1 argument.");
            }

            const Value& key = argAt(0);
            result = Value(dict->map.find(key) != dict->map.end());
        } else if (id == NativeMethodId::DICT_KEYS) {
            if (argumentCount != 0) {
                return runtimeError("Dict method 'keys' expects 0 arguments.");
            }

            auto keys = gcAlloc<ArrayObject>();
            std::vector<Value> orderedKeys = sortedDictKeys(dict);
            keys->elements.reserve(orderedKeys.size());

            for (const auto& key : orderedKeys) {
                keys->elements.push_back(key);
            }
            keys->elementType =
                dict->keyType ? dict->keyType : TypeInfo::makeAny();
            result = Value(keys);
        } else if (id == NativeMethodId::DICT_VALUES) {
            if (argumentCount != 0) {
                return runtimeError(
                    "Dict method 'values' expects 0 arguments.");
            }

            auto values = gcAlloc<ArrayObject>();
            std::vector<Value> orderedKeys = sortedDictKeys(dict);
            values->elements.reserve(orderedKeys.size());

            for (const auto& key : orderedKeys) {
                auto it = dict->map.find(key);
                if (it != dict->map.end()) {
                    values->elements.push_back(it->second);
                }
            }
            values->elementType = dict->valueType;
            result = Value(values);
        } else if (id == NativeMethodId::DICT_SIZE) {
            if (argumentCount != 0) {
                return runtimeError("Dict method 'size' expects 0 arguments.");
            }

            result = Value(static_cast<double>(dict->map.size()));
        } else if (id == NativeMethodId::DICT_REMOVE) {
            if (argumentCount != 1) {
                return runtimeError("Dict method 'remove' expects 1 argument.");
            }

            const Value& key = argAt(0);
            auto it = dict->map.find(key);
            if (it == dict->map.end()) {
                return runtimeError("Dict method 'remove' key not found.");
            }

            result = it->second;
            dict->map.erase(it);
        } else if (id == NativeMethodId::DICT_CLEAR) {
            if (argumentCount != 0) {
                return runtimeError("Dict method 'clear' expects 0 arguments.");
            }

            double removed = static_cast<double>(dict->map.size());
            dict->map.clear();
            result = Value(removed);
        } else if (id == NativeMethodId::DICT_IS_EMPTY) {
            if (argumentCount != 0) {
                return runtimeError(
                    "Dict method 'isEmpty' expects 0 arguments.");
            }

            result = Value(dict->map.empty());
        } else if (id == NativeMethodId::DICT_GET_OR) {
            if (argumentCount != 2) {
                return runtimeError("Dict method 'getOr' expects 2 arguments.");
            }

            const Value& key = argAt(0);
            auto it = dict->map.find(key);
            result = (it != dict->map.end()) ? it->second : argAt(1);
        } else {
            return runtimeError("Undefined dict method '" + name + "'.");
        }
    } else if (receiver.isSet()) {
        auto set = receiver.asSet();

        if (id == NativeMethodId::SET_ADD) {
            if (argumentCount != 1) {
                return runtimeError("Set method 'add' expects 1 argument.");
            }

            Value& element = argAtRef(0);
            if (set->elementType->isAny()) {
                set->elementType = inferRuntimeType(element);
            } else if (!valueMatchesType(element, set->elementType)) {
                return runtimeError("Set method 'add' expects value of type '" +
                                    set->elementType->toString() + "'.");
            }

            bool inserted = setInsertValue(set, std::move(element));
            result = Value(inserted);
        } else if (id == NativeMethodId::SET_HAS) {
            if (argumentCount != 1) {
                return runtimeError("Set method 'has' expects 1 argument.");
            }

            result = Value(setContainsValue(set, argAt(0)));
        } else if (id == NativeMethodId::SET_REMOVE) {
            if (argumentCount != 1) {
                return runtimeError("Set method 'remove' expects 1 argument.");
            }

            bool removed = setRemoveValue(set, argAt(0));
            result = Value(removed);
        } else if (id == NativeMethodId::SET_SIZE) {
            if (argumentCount != 0) {
                return runtimeError("Set method 'size' expects 0 arguments.");
            }

            result = Value(static_cast<double>(set->elements.size()));
        } else if (id == NativeMethodId::SET_TO_ARRAY) {
            if (argumentCount != 0) {
                return runtimeError(
                    "Set method 'toArray' expects 0 arguments.");
            }

            auto array = gcAlloc<ArrayObject>();
            array->elements.reserve(set->elements.size());
            array->elements.insert(array->elements.end(), set->elements.begin(),
                                   set->elements.end());
            array->elementType = set->elementType;
            result = Value(array);
        } else if (id == NativeMethodId::SET_CLEAR) {
            if (argumentCount != 0) {
                return runtimeError("Set method 'clear' expects 0 arguments.");
            }

            double removed = static_cast<double>(set->elements.size());
            set->elements.clear();
            set->indexByValue.clear();
            result = Value(removed);
        } else if (id == NativeMethodId::SET_IS_EMPTY) {
            if (argumentCount != 0) {
                return runtimeError(
                    "Set method 'isEmpty' expects 0 arguments.");
            }

            result = Value(set->elements.empty());
        } else if (id == NativeMethodId::SET_UNION) {
            if (argumentCount != 1) {
                return runtimeError("Set method 'union' expects 1 argument.");
            }
            if (!argAt(0).isSet()) {
                return runtimeError(
                    "Set method 'union' expects a set argument.");
            }

            auto out = gcAlloc<SetObject>();
            out->elementType = set->elementType;
            auto rhs = argAt(0).asSet();
            out->elements.reserve(set->elements.size() + rhs->elements.size());
            out->indexByValue.reserve(set->elements.size() +
                                      rhs->elements.size());
            for (const auto& element : set->elements) {
                setInsertValue(out, element);
            }

            if (!set->elementType->isAny() && !rhs->elementType->isAny() &&
                !isAssignable(rhs->elementType, set->elementType) &&
                !isAssignable(set->elementType, rhs->elementType)) {
                return runtimeError(
                    "Set method 'union' requires compatible element types.");
            }

            for (const auto& element : rhs->elements) {
                setInsertValue(out, element);
            }
            result = Value(out);
        } else if (id == NativeMethodId::SET_INTERSECT) {
            if (argumentCount != 1) {
                return runtimeError(
                    "Set method 'intersect' expects 1 argument.");
            }
            if (!argAt(0).isSet()) {
                return runtimeError(
                    "Set method 'intersect' expects a set argument.");
            }

            auto out = gcAlloc<SetObject>();
            out->elementType = set->elementType;
            auto rhs = argAt(0).asSet();
            out->elements.reserve(
                std::min(set->elements.size(), rhs->elements.size()));
            out->indexByValue.reserve(
                std::min(set->elements.size(), rhs->elements.size()));

            if (!set->elementType->isAny() && !rhs->elementType->isAny() &&
                !isAssignable(rhs->elementType, set->elementType) &&
                !isAssignable(set->elementType, rhs->elementType)) {
                return runtimeError(
                    "Set method 'intersect' requires compatible element types.");
            }

            for (const auto& element : set->elements) {
                if (setContainsValue(rhs, element)) {
                    setInsertValue(out, element);
                }
            }
            result = Value(out);
        } else if (id == NativeMethodId::SET_DIFFERENCE) {
            if (argumentCount != 1) {
                return runtimeError(
                    "Set method 'difference' expects 1 argument.");
            }
            if (!argAt(0).isSet()) {
                return runtimeError(
                    "Set method 'difference' expects a set argument.");
            }

            auto out = gcAlloc<SetObject>();
            out->elementType = set->elementType;
            auto rhs = argAt(0).asSet();
            out->elements.reserve(set->elements.size());
            out->indexByValue.reserve(set->elements.size());

            if (!set->elementType->isAny() && !rhs->elementType->isAny() &&
                !isAssignable(rhs->elementType, set->elementType) &&
                !isAssignable(set->elementType, rhs->elementType)) {
                return runtimeError("Set method 'difference' requires "
                                    "compatible element types.");
            }

            for (const auto& element : set->elements) {
                if (!setContainsValue(rhs, element)) {
                    setInsertValue(out, element);
                }
            }
            result = Value(out);
        } else {
            return runtimeError("Undefined set method '" + name + "'.");
        }
    } else {
        return runtimeError("Native bound method receiver is invalid.");
    }

    m_stack.popN(m_stack.size() - calleeIndex);
    m_stack.push(std::move(result));
    return Status::OK;
}

Status VirtualMachine::callValue(Value callee, uint8_t argumentCount,
                                 size_t calleeIndex) {
    if (callee.isClass()) {
        if (argumentCount != 0) {
            return runtimeError("Class '" + callee.asClass()->name +
                                "' expected 0 arguments but got " +
                                std::to_string(static_cast<int>(argumentCount)) +
                                ".");
        }

        auto instance = gcAlloc<InstanceObject>();
        instance->klass = callee.asClass();
        instance->fieldSlots.resize(instance->klass->fieldNames.size());
        instance->initializedFieldSlots.assign(
            instance->klass->fieldNames.size(), 0);
        m_stack.setAt(calleeIndex, Value(instance));
        return Status::OK;
    }

    if (callee.isNative()) {
        auto native = callee.asNative();
        if (native->arity >= 0 && native->arity != argumentCount) {
            return runtimeError("Native function '" + native->name +
                                "' expected " +
                                std::to_string(native->arity) +
                                " arguments but got " +
                                std::to_string(static_cast<int>(argumentCount)) +
                                ".");
        }

        if (native->callback == nullptr) {
            return runtimeError("Unknown native function '" + native->name +
                                "'.");
        }
        return native->callback(*this, *native, argumentCount, calleeIndex);
    }

    if (callee.isNativeBound()) {
        auto bound = callee.asNativeBound();
        return callNativeMethod(bound->id, bound->name, bound->receiver,
                                argumentCount, calleeIndex);
    }

    if (callee.isBoundMethod()) {
        auto bound = callee.asBoundMethod();
        return callClosure(bound->method, argumentCount, bound->receiver);
    }

    if (callee.isClosure()) {
        return callClosure(callee.asClosure(), argumentCount);
    }

    if (callee.isFunction()) {
        auto closure = gcAlloc<ClosureObject>();
        closure->function = callee.asFunction();
        closure->upvalues = {};
        return callClosure(closure, argumentCount);
    }

    return runtimeError(
        "Can only call functions, classes, methods, and natives.");
}

Status VirtualMachine::invokeProperty(size_t instructionOffset,
                                      const std::string& name,
                                      uint8_t argumentCount) {
    size_t calleeIndex =
        m_stack.size() - static_cast<size_t>(argumentCount) - 1;
    Value receiver = m_stack.peek(argumentCount);

    if (receiver.isModule()) {
        auto module = receiver.asModule();
        auto it = module->exports.find(name);
        if (it == module->exports.end()) {
            return runtimeError("Module '" + module->path +
                                "' has no export '" + name + "'.");
        }

        auto typeIt = module->exportTypes.find(name);
        if (typeIt != module->exportTypes.end() &&
            !valueMatchesType(it->second, typeIt->second)) {
            return runtimeError("Type error: module export '" + name +
                                "' from '" + module->path + "' expected '" +
                                typeIt->second->toString() + "', got '" +
                                valueTypeName(it->second) + "'.");
        }

        return callValue(it->second, argumentCount, calleeIndex);
    }

    if (receiver.isArray() || receiver.isDict() || receiver.isSet()) {
        return callNativeMethod(resolveNativeMethodId(receiver, name), name,
                                receiver, argumentCount, calleeIndex);
    }

    if (!receiver.isInstance()) {
        return runtimeError("Only instances have properties.");
    }

    auto instance = receiver.asInstance();
    auto& cache =
        currentFrame().chunk->propertyInlineCacheAt(instructionOffset);
    if (cache.klass == instance->klass) {
        if (cache.kind == PropertyInlineCacheKind::FIELD &&
            cache.slotIndex < instance->fieldSlots.size() &&
            cache.slotIndex < instance->initializedFieldSlots.size() &&
            instance->initializedFieldSlots[cache.slotIndex]) {
            return callValue(instance->fieldSlots[cache.slotIndex],
                             argumentCount, calleeIndex);
        }

        if (cache.kind == PropertyInlineCacheKind::METHOD &&
            cache.method != nullptr) {
            return callClosure(cache.method, argumentCount, instance);
        }
    }

    auto fieldSlotIt = instance->klass->fieldIndexByName.find(name);
    if (fieldSlotIt != instance->klass->fieldIndexByName.end()) {
        cache.klass = instance->klass;
        cache.kind = PropertyInlineCacheKind::FIELD;
        cache.slotIndex = fieldSlotIt->second;
        cache.method = nullptr;
        cache.fieldType.reset();

        if (fieldSlotIt->second < instance->fieldSlots.size() &&
            fieldSlotIt->second < instance->initializedFieldSlots.size() &&
            instance->initializedFieldSlots[fieldSlotIt->second]) {
            return callValue(instance->fieldSlots[fieldSlotIt->second],
                             argumentCount, calleeIndex);
        }
    }

    auto method = findMethodClosure(instance->klass, name);
    if (!method) {
        cache.klass = nullptr;
        cache.kind = PropertyInlineCacheKind::EMPTY;
        cache.slotIndex = 0;
        cache.method = nullptr;
        cache.fieldType.reset();
        return runtimeError("Undefined property '" + name + "'.");
    }

    cache.klass = instance->klass;
    cache.kind = PropertyInlineCacheKind::METHOD;
    cache.slotIndex = 0;
    cache.method = method;
    cache.fieldType.reset();
    return callClosure(method, argumentCount, instance);
}

Status VirtualMachine::invokeSuper(const std::string& name,
                                   uint8_t argumentCount) {
    Value receiver = m_stack.peek(argumentCount);
    if (!receiver.isInstance() || !receiver.asInstance()->klass ||
        !receiver.asInstance()->klass->superclass) {
        return runtimeError("Invalid super lookup.");
    }

    auto instance = receiver.asInstance();
    auto method = findMethodClosure(instance->klass->superclass, name);
    if (!method) {
        return runtimeError("Undefined superclass method '" + name + "'.");
    }

    return callClosure(method, argumentCount, instance);
}

Status VirtualMachine::run(bool printReturnValue, Value& returnValue,
                           size_t stopFrameCount) {
#define VM_OPCODE_LABEL(name) VM_LABEL_##name
#define VM_OPCODE_ADDR(name) &&VM_OPCODE_LABEL(name)
#define VM_CASE(name) VM_OPCODE_LABEL(name):

    static void* dispatchTable[] = {
        VM_OPCODE_ADDR(RETURN),
        VM_OPCODE_ADDR(CONSTANT),
        VM_OPCODE_ADDR(NIL),
        VM_OPCODE_ADDR(TRUE_LITERAL),
        VM_OPCODE_ADDR(FALSE_LITERAL),
        VM_OPCODE_ADDR(NEGATE),
        VM_OPCODE_ADDR(NOT),
        VM_OPCODE_ADDR(EQUAL_OP),
        VM_OPCODE_ADDR(NOT_EQUAL_OP),
        VM_OPCODE_ADDR(ADD),
        VM_OPCODE_ADDR(SUB),
        VM_OPCODE_ADDR(MULT),
        VM_OPCODE_ADDR(DIV),
        VM_OPCODE_ADDR(IADD),
        VM_OPCODE_ADDR(ISUB),
        VM_OPCODE_ADDR(IMULT),
        VM_OPCODE_ADDR(IDIV),
        VM_OPCODE_ADDR(IMOD),
        VM_OPCODE_ADDR(UADD),
        VM_OPCODE_ADDR(USUB),
        VM_OPCODE_ADDR(UMULT),
        VM_OPCODE_ADDR(UDIV),
        VM_OPCODE_ADDR(UMOD),
        VM_OPCODE_ADDR(GREATER_THAN),
        VM_OPCODE_ADDR(LESS_THAN),
        VM_OPCODE_ADDR(GREATER_EQUAL_THAN),
        VM_OPCODE_ADDR(LESS_EQUAL_THAN),
        VM_OPCODE_ADDR(IGREATER),
        VM_OPCODE_ADDR(ILESS),
        VM_OPCODE_ADDR(IGREATER_EQ),
        VM_OPCODE_ADDR(ILESS_EQ),
        VM_OPCODE_ADDR(UGREATER),
        VM_OPCODE_ADDR(ULESS),
        VM_OPCODE_ADDR(UGREATER_EQ),
        VM_OPCODE_ADDR(ULESS_EQ),
        VM_OPCODE_ADDR(POP),
        VM_OPCODE_ADDR(PRINT_OP),
        VM_OPCODE_ADDR(DEFINE_GLOBAL),
        VM_OPCODE_ADDR(GET_GLOBAL),
        VM_OPCODE_ADDR(SET_GLOBAL),
        VM_OPCODE_ADDR(DEFINE_GLOBAL_SLOT),
        VM_OPCODE_ADDR(GET_GLOBAL_SLOT),
        VM_OPCODE_ADDR(SET_GLOBAL_SLOT),
        VM_OPCODE_ADDR(GET_LOCAL),
        VM_OPCODE_ADDR(SET_LOCAL),
        VM_OPCODE_ADDR(GET_UPVALUE),
        VM_OPCODE_ADDR(SET_UPVALUE),
        VM_OPCODE_ADDR(CLASS_OP),
        VM_OPCODE_ADDR(INHERIT),
        VM_OPCODE_ADDR(METHOD),
        VM_OPCODE_ADDR(GET_THIS),
        VM_OPCODE_ADDR(GET_SUPER),
        VM_OPCODE_ADDR(INVOKE_SUPER),
        VM_OPCODE_ADDR(GET_PROPERTY),
        VM_OPCODE_ADDR(INVOKE),
        VM_OPCODE_ADDR(SET_PROPERTY),
        VM_OPCODE_ADDR(CALL),
        VM_OPCODE_ADDR(CLOSURE),
        VM_OPCODE_ADDR(CLOSE_UPVALUE),
        VM_OPCODE_ADDR(BUILD_ARRAY),
        VM_OPCODE_ADDR(BUILD_DICT),
        VM_OPCODE_ADDR(GET_INDEX),
        VM_OPCODE_ADDR(SET_INDEX),
        VM_OPCODE_ADDR(DUP),
        VM_OPCODE_ADDR(DUP2),
        VM_OPCODE_ADDR(JUMP),
        VM_OPCODE_ADDR(JUMP_IF_FALSE),
        VM_OPCODE_ADDR(JUMP_IF_FALSE_POP),
        VM_OPCODE_ADDR(LOOP),
        VM_OPCODE_ADDR(SHIFT_LEFT),
        VM_OPCODE_ADDR(SHIFT_RIGHT),
        VM_OPCODE_ADDR(BITWISE_AND),
        VM_OPCODE_ADDR(BITWISE_OR),
        VM_OPCODE_ADDR(BITWISE_XOR),
        VM_OPCODE_ADDR(BITWISE_NOT),
        VM_OPCODE_ADDR(WIDEN_INT),
        VM_OPCODE_ADDR(NARROW_INT),
        VM_OPCODE_ADDR(INT_TO_FLOAT),
        VM_OPCODE_ADDR(FLOAT_TO_INT),
        VM_OPCODE_ADDR(INT_TO_STR),
        VM_OPCODE_ADDR(CHECK_INSTANCE_TYPE),
        VM_OPCODE_ADDR(INT_NEGATE),
        VM_OPCODE_ADDR(ITER_INIT),
        VM_OPCODE_ADDR(ITER_HAS_NEXT),
        VM_OPCODE_ADDR(ITER_HAS_NEXT_JUMP),
        VM_OPCODE_ADDR(ITER_NEXT),
        VM_OPCODE_ADDR(ITER_NEXT_SET_LOCAL),
        VM_OPCODE_ADDR(IMPORT_MODULE),
        VM_OPCODE_ADDR(EXPORT_NAME),
    };

    constexpr size_t kDispatchTableSize =
        static_cast<size_t>(OpCode::EXPORT_NAME) + 1;
    static_assert(
        sizeof(dispatchTable) / sizeof(dispatchTable[0]) == kDispatchTableSize,
        "Dispatch table must stay aligned with OpCode enum.");

#define DISPATCH_INSTRUCTION()                                         \
    do {                                                               \
        if (m_traceEnabled) {                                          \
            CallFrame& frame = currentFrame();                         \
            m_stack.print();                                           \
            frame.chunk->disassembleInstruction(                       \
                static_cast<int>(frame.ip - frame.chunk->getBytes())); \
        }                                                              \
                                                                       \
        uint8_t instruction = readByte();                              \
        if (static_cast<size_t>(instruction) >= kDispatchTableSize) {  \
            return runtimeError(                                       \
                "Invalid instruction opcode: " +                       \
                std::to_string(static_cast<int>(instruction)) + ".");  \
        }                                                              \
                                                                       \
        goto* dispatchTable[instruction];                              \
    } while (0)

// Use a regular goto between opcode handlers so scoped temporaries are
// destroyed before the next indirect jump. This keeps Clang's protected
// scope analysis satisfied without changing the dispatch table semantics.
#define DISPATCH() goto dispatch

    try {
    dispatch:
        DISPATCH_INSTRUCTION();

        VM_CASE(RETURN) {
            Value result = m_stack.popMove();
            CallFrame finishedFrame = currentFrame();
            closeUpvalues(finishedFrame.slotBase);
            m_frameCount--;

            if (m_frameCount == stopFrameCount) {
                m_activeFrame =
                    (m_frameCount == 0) ? nullptr : &m_frames[m_frameCount - 1];
                returnValue = result;
                if (printReturnValue) {
                    std::cout << "Return constant: " << result << std::endl;
                }
                return Status::OK;
            }

            m_activeFrame = &m_frames[m_frameCount - 1];

            m_stack.popN(m_stack.size() - finishedFrame.calleeIndex);
            m_stack.push(std::move(result));
            DISPATCH();
        }

        VM_CASE(CONSTANT) {
            const Value& val = readConstant();
            m_stack.push(val);
            DISPATCH();
        }

        VM_CASE(NIL) {
            m_stack.push(Value());
            DISPATCH();
        }

        VM_CASE(TRUE_LITERAL) {
            m_stack.push(Value(true));
            DISPATCH();
        }

        VM_CASE(FALSE_LITERAL) {
            m_stack.push(Value(false));
            DISPATCH();
        }

        VM_CASE(NEGATE) {
            const Value& value = m_stack.top();
            if (!value.isAnyNumeric()) {
                return runtimeError("Operand must be a number for unary '-'.");
            }

            Value result;
            if (value.isSignedInt()) {
                result = Value(wrapSignedSub(0, value.asSignedInt()));
            } else if (value.isUnsignedInt()) {
                uint64_t asUnsigned = value.asUnsignedInt();
                result = Value(static_cast<int64_t>(0u - asUnsigned));
            } else {
                result = Value(-value.asNumber());
            }
            m_stack.replaceTop(std::move(result));
            DISPATCH();
        }

        VM_CASE(NOT) {
            m_stack.replaceTop(Value(isFalsey(m_stack.top())));
            DISPATCH();
        }

        VM_CASE(EQUAL_OP) {
            m_stack.replaceTopPair(Value(m_stack.second() == m_stack.top()));
            DISPATCH();
        }

        VM_CASE(NOT_EQUAL_OP) {
            m_stack.replaceTopPair(Value(m_stack.second() != m_stack.top()));
            DISPATCH();
        }

        VM_CASE(ADD) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();

            if (a.isSignedInt() && b.isSignedInt()) {
                Value result(wrapSignedAdd(a.asSignedInt(), b.asSignedInt()));
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                Value result(a.asUnsignedInt() + b.asUnsignedInt());
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            if (a.isAnyNumeric() && b.isAnyNumeric()) {
                double lhs = 0.0;
                double rhs = 0.0;
                valueToDouble(a, lhs);
                valueToDouble(b, rhs);
                Value result(lhs + rhs);
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            if (a.isString() && b.isString()) {
                std::string result = a.asString() + b.asString();
                m_stack.replaceTopPair(makeStringValue(std::move(result)));
                DISPATCH();
            }

            return runtimeError(
                "Operands must be two numbers or two strings for '+'.");
        }

        VM_CASE(SUB) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();
            if (!isNumberPair(a, b)) {
                return runtimeError("Operands must be numbers for '-'.");
            }

            if (a.isSignedInt() && b.isSignedInt()) {
                Value result(wrapSignedSub(a.asSignedInt(), b.asSignedInt()));
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                Value result(a.asUnsignedInt() - b.asUnsignedInt());
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            double lhs = 0.0;
            double rhs = 0.0;
            valueToDouble(a, lhs);
            valueToDouble(b, rhs);
            Value result(lhs - rhs);
            m_stack.replaceTopPair(std::move(result));
            DISPATCH();
        }

        VM_CASE(MULT) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();
            if (!isNumberPair(a, b)) {
                return runtimeError("Operands must be numbers for '*'.");
            }

            if (a.isSignedInt() && b.isSignedInt()) {
                Value result(wrapSignedMul(a.asSignedInt(), b.asSignedInt()));
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                Value result(a.asUnsignedInt() * b.asUnsignedInt());
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            double lhs = 0.0;
            double rhs = 0.0;
            valueToDouble(a, lhs);
            valueToDouble(b, rhs);
            Value result(lhs * rhs);
            m_stack.replaceTopPair(std::move(result));
            DISPATCH();
        }

        VM_CASE(DIV) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();
            if (!isNumberPair(a, b)) {
                return runtimeError("Operands must be numbers for '/'.");
            }

            if (a.isSignedInt() && b.isSignedInt()) {
                if (b.asSignedInt() == 0) {
                    return runtimeError("Division by zero.");
                }
                Value result(a.asSignedInt() / b.asSignedInt());
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                if (b.asUnsignedInt() == 0) {
                    return runtimeError("Division by zero.");
                }
                Value result(a.asUnsignedInt() / b.asUnsignedInt());
                m_stack.replaceTopPair(std::move(result));
                DISPATCH();
            }

            double lhs = 0.0;
            double rhs = 0.0;
            valueToDouble(a, lhs);
            valueToDouble(b, rhs);
            Value result(lhs / rhs);
            m_stack.replaceTopPair(std::move(result));
            DISPATCH();
        }

        VM_CASE(IADD) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(wrapSignedAdd(lhs, rhs)));
            DISPATCH();
        }

        VM_CASE(ISUB) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(wrapSignedSub(lhs, rhs)));
            DISPATCH();
        }

        VM_CASE(IMULT) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(wrapSignedMul(lhs, rhs)));
            DISPATCH();
        }

        VM_CASE(IDIV) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            if (rhs == 0) {
                return runtimeError("Division by zero.");
            }
            m_stack.replaceTopPair(Value(lhs / rhs));
            DISPATCH();
        }

        VM_CASE(IMOD) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            if (rhs == 0) {
                return runtimeError("Division by zero.");
            }
            m_stack.replaceTopPair(Value(lhs % rhs));
            DISPATCH();
        }

        VM_CASE(UADD) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs + rhs));
            DISPATCH();
        }

        VM_CASE(USUB) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs - rhs));
            DISPATCH();
        }

        VM_CASE(UMULT) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs * rhs));
            DISPATCH();
        }

        VM_CASE(UDIV) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            if (rhs == 0) {
                return runtimeError("Division by zero.");
            }
            m_stack.replaceTopPair(Value(lhs / rhs));
            DISPATCH();
        }

        VM_CASE(UMOD) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            if (rhs == 0) {
                return runtimeError("Division by zero.");
            }
            m_stack.replaceTopPair(Value(lhs % rhs));
            DISPATCH();
        }

        VM_CASE(GREATER_THAN) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();
            if (!isNumberPair(a, b)) {
                return runtimeError("Operands must be numbers for '>'.");
            }

            if (a.isSignedInt() && b.isSignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asSignedInt() > b.asSignedInt()));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asUnsignedInt() > b.asUnsignedInt()));
                DISPATCH();
            }

            double lhs = 0.0;
            double rhs = 0.0;
            valueToDouble(a, lhs);
            valueToDouble(b, rhs);
            m_stack.replaceTopPair(Value(lhs > rhs));
            DISPATCH();
        }

        VM_CASE(LESS_THAN) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();
            if (!isNumberPair(a, b)) {
                return runtimeError("Operands must be numbers for '<'.");
            }

            if (a.isSignedInt() && b.isSignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asSignedInt() < b.asSignedInt()));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asUnsignedInt() < b.asUnsignedInt()));
                DISPATCH();
            }

            double lhs = 0.0;
            double rhs = 0.0;
            valueToDouble(a, lhs);
            valueToDouble(b, rhs);
            m_stack.replaceTopPair(Value(lhs < rhs));
            DISPATCH();
        }

        VM_CASE(GREATER_EQUAL_THAN) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();
            if (!isNumberPair(a, b)) {
                return runtimeError("Operands must be numbers for '>='.");
            }

            if (a.isSignedInt() && b.isSignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asSignedInt() >= b.asSignedInt()));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asUnsignedInt() >= b.asUnsignedInt()));
                DISPATCH();
            }

            double lhs = 0.0;
            double rhs = 0.0;
            valueToDouble(a, lhs);
            valueToDouble(b, rhs);
            m_stack.replaceTopPair(Value(lhs >= rhs));
            DISPATCH();
        }

        VM_CASE(LESS_EQUAL_THAN) {
            const Value& b = m_stack.top();
            const Value& a = m_stack.second();
            if (!isNumberPair(a, b)) {
                return runtimeError("Operands must be numbers for '<='.");
            }

            if (a.isSignedInt() && b.isSignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asSignedInt() <= b.asSignedInt()));
                DISPATCH();
            }

            if (a.isUnsignedInt() && b.isUnsignedInt()) {
                m_stack.replaceTopPair(
                    Value(a.asUnsignedInt() <= b.asUnsignedInt()));
                DISPATCH();
            }

            double lhs = 0.0;
            double rhs = 0.0;
            valueToDouble(a, lhs);
            valueToDouble(b, rhs);
            m_stack.replaceTopPair(Value(lhs <= rhs));
            DISPATCH();
        }

        VM_CASE(IGREATER) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs > rhs));
            DISPATCH();
        }

        VM_CASE(ILESS) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs < rhs));
            DISPATCH();
        }

        VM_CASE(IGREATER_EQ) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs >= rhs));
            DISPATCH();
        }

        VM_CASE(ILESS_EQ) {
            int64_t rhs = requireSignedInt(m_stack.top());
            int64_t lhs = requireSignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs <= rhs));
            DISPATCH();
        }

        VM_CASE(UGREATER) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs > rhs));
            DISPATCH();
        }

        VM_CASE(ULESS) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs < rhs));
            DISPATCH();
        }

        VM_CASE(UGREATER_EQ) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs >= rhs));
            DISPATCH();
        }

        VM_CASE(ULESS_EQ) {
            uint64_t rhs = requireUnsignedInt(m_stack.top());
            uint64_t lhs = requireUnsignedInt(m_stack.second());
            m_stack.replaceTopPair(Value(lhs <= rhs));
            DISPATCH();
        }

        VM_CASE(POP) {
            m_stack.pop();
            DISPATCH();
        }

        VM_CASE(PRINT_OP) {
            Value value = m_stack.pop();
            std::cout << value << std::endl;
            DISPATCH();
        }

        VM_CASE(DEFINE_GLOBAL) {
            const std::string& name = readNameConstant();
            Value value = m_stack.pop();
            m_nativeGlobals[name] = value;
            DISPATCH();
        }

        VM_CASE(GET_GLOBAL) {
            const std::string& name = readNameConstant();
            auto it = m_nativeGlobals.find(name);
            if (it == m_nativeGlobals.end()) {
                return runtimeError("Undefined variable '" + name + "'.");
            }

            m_stack.push(it->second);
            DISPATCH();
        }

        VM_CASE(SET_GLOBAL) {
            const std::string& name = readNameConstant();
            auto it = m_nativeGlobals.find(name);
            if (it == m_nativeGlobals.end()) {
                return runtimeError("Undefined variable '" + name + "'.");
            }

            it->second = m_stack.peek(0);
            DISPATCH();
        }

        VM_CASE(DEFINE_GLOBAL_SLOT) {
            uint8_t slot = readByte();
            if (slot >= m_globalValues.size()) {
                return runtimeError("Invalid global slot.");
            }
            m_globalValues[slot] = m_stack.pop();
            m_globalDefined[slot] = true;
            DISPATCH();
        }

        VM_CASE(GET_GLOBAL_SLOT) {
            uint8_t slot = readByte();
            if (slot >= m_globalValues.size() || !m_globalDefined[slot]) {
                std::string name = slot < m_globalNames.size()
                                       ? m_globalNames[slot]
                                       : "<unknown>";
                return runtimeError("Undefined variable '" + name + "'.");
            }

            m_stack.push(m_globalValues[slot]);
            DISPATCH();
        }

        VM_CASE(SET_GLOBAL_SLOT) {
            uint8_t slot = readByte();
            if (slot >= m_globalValues.size() || !m_globalDefined[slot]) {
                std::string name = slot < m_globalNames.size()
                                       ? m_globalNames[slot]
                                       : "<unknown>";
                return runtimeError("Undefined variable '" + name + "'.");
            }

            m_globalValues[slot] = m_stack.peek(0);
            DISPATCH();
        }

        VM_CASE(GET_LOCAL) {
            uint8_t slot = readByte();
            m_stack.push(m_stack.getAt(currentFrame().slotBase + slot));
            DISPATCH();
        }

        VM_CASE(SET_LOCAL) {
            uint8_t slot = readByte();
            m_stack.setAt(currentFrame().slotBase + slot, m_stack.peek(0));
            DISPATCH();
        }

        VM_CASE(GET_UPVALUE) {
            uint8_t slot = readByte();
            auto upvalue = currentFrame().closure->upvalues[slot];
            if (upvalue->isClosed) {
                m_stack.push(upvalue->closed);
            } else {
                m_stack.push(m_stack.getAt(upvalue->stackIndex));
            }
            DISPATCH();
        }

        VM_CASE(SET_UPVALUE) {
            uint8_t slot = readByte();
            auto upvalue = currentFrame().closure->upvalues[slot];
            if (upvalue->isClosed) {
                upvalue->closed = m_stack.peek(0);
            } else {
                m_stack.setAt(upvalue->stackIndex, m_stack.peek(0));
            }
            DISPATCH();
        }

        VM_CASE(CLASS_OP) {
            const std::string& name = readNameConstant();
            auto klass = gcAlloc<ClassObject>();
            klass->name = name;

            const auto& compilerFieldTypes = m_compiler.classFieldTypes();
            auto fieldIt = compilerFieldTypes.find(name);
            if (fieldIt != compilerFieldTypes.end()) {
                klass->fieldTypes = fieldIt->second;
            }

            const auto& compilerMethodTypes =
                m_compiler.classMethodSignatures();
            auto methodIt = compilerMethodTypes.find(name);
            if (methodIt != compilerMethodTypes.end()) {
                klass->methodTypes = methodIt->second;
            }

            rebuildFieldLayout(klass);

            m_stack.push(Value(klass));
            DISPATCH();
        }

        VM_CASE(INHERIT) {
            Value superclassValue = m_stack.pop();
            Value subclassValue = m_stack.peek(0);

            if (!superclassValue.isClass() || !subclassValue.isClass()) {
                return runtimeError("Inheritance requires classes.");
            }

            auto superclass = superclassValue.asClass();
            auto subclass = subclassValue.asClass();
            subclass->superclass = superclass;

            for (const auto& fieldType : superclass->fieldTypes) {
                if (subclass->fieldTypes.find(fieldType.first) ==
                    subclass->fieldTypes.end()) {
                    subclass->fieldTypes[fieldType.first] = fieldType.second;
                }
            }

            for (const auto& methodType : superclass->methodTypes) {
                if (subclass->methodTypes.find(methodType.first) ==
                    subclass->methodTypes.end()) {
                    subclass->methodTypes[methodType.first] = methodType.second;
                }
            }

            for (const auto& method : superclass->methods) {
                if (subclass->methods.find(method.first) ==
                    subclass->methods.end()) {
                    subclass->methods[method.first] = method.second;
                }
            }

            rebuildFieldLayout(subclass);
            DISPATCH();
        }

        VM_CASE(METHOD) {
            const std::string& name = readNameConstant();
            Value method = m_stack.peek(0);
            Value klass = m_stack.peek(1);

            if (!klass.isClass() || !method.isClosure()) {
                return runtimeError("Invalid method declaration.");
            }

            klass.asClass()->methods[name] = method.asClosure();
            m_stack.pop();
            DISPATCH();
        }

        VM_CASE(GET_THIS) {
            auto receiver = currentFrame().receiver;
            if (!receiver) {
                return runtimeError("Cannot use 'this' outside of a method.");
            }

            m_stack.push(Value(receiver));
            DISPATCH();
        }

        VM_CASE(GET_SUPER) {
            const std::string& name = readNameConstant();
            auto receiver = currentFrame().receiver;
            if (!receiver || !receiver->klass || !receiver->klass->superclass) {
                return runtimeError("Invalid super lookup.");
            }

            auto method = findMethodClosure(receiver->klass->superclass, name);
            if (!method) {
                return runtimeError("Undefined superclass method '" + name +
                                    "'.");
            }

            auto bound = gcAlloc<BoundMethodObject>();
            bound->receiver = receiver;
            bound->method = method;
            m_stack.push(Value(bound));
            DISPATCH();
        }

        VM_CASE(INVOKE_SUPER) {
            const std::string& name = readNameConstant();
            uint8_t argumentCount = readByte();
            Status status = invokeSuper(name, argumentCount);
            if (status != Status::OK) {
                return status;
            }
            DISPATCH();
        }

        VM_CASE(GET_PROPERTY) {
            size_t instructionOffset = currentInstructionOffset();
            const std::string& name = readNameConstant();
            Value receiver = m_stack.peek(0);

            if (receiver.isModule()) {
                auto module = receiver.asModule();
                auto it = module->exports.find(name);
                if (it == module->exports.end()) {
                    return runtimeError("Module '" + module->path +
                                        "' has no export '" + name + "'.");
                }

                auto typeIt = module->exportTypes.find(name);
                if (typeIt != module->exportTypes.end() &&
                    !valueMatchesType(it->second, typeIt->second)) {
                    return runtimeError(
                        "Type error: module export '" + name + "' from '" +
                        module->path + "' expected '" +
                        typeIt->second->toString() + "', got '" +
                        valueTypeName(it->second) + "'.");
                }

                m_stack.pop();
                m_stack.push(it->second);
                DISPATCH();
            }

            if (receiver.isArray() || receiver.isDict() || receiver.isSet()) {
                NativeBoundMethodObject* bound = nullptr;
                if (receiver.isArray()) {
                    auto* array = receiver.asArray();
                    auto cacheIt = array->methodCache.find(name);
                    if (cacheIt != array->methodCache.end()) {
                        bound = cacheIt->second;
                    } else {
                        bound = gcAlloc<NativeBoundMethodObject>();
                        bound->name = name;
                        bound->id = resolveNativeMethodId(receiver, name);
                        bound->receiver = receiver;
                        array->methodCache.emplace(name, bound);
                    }
                } else if (receiver.isDict()) {
                    auto* dict = receiver.asDict();
                    auto cacheIt = dict->methodCache.find(name);
                    if (cacheIt != dict->methodCache.end()) {
                        bound = cacheIt->second;
                    } else {
                        bound = gcAlloc<NativeBoundMethodObject>();
                        bound->name = name;
                        bound->id = resolveNativeMethodId(receiver, name);
                        bound->receiver = receiver;
                        dict->methodCache.emplace(name, bound);
                    }
                } else {
                    auto* set = receiver.asSet();
                    auto cacheIt = set->methodCache.find(name);
                    if (cacheIt != set->methodCache.end()) {
                        bound = cacheIt->second;
                    } else {
                        bound = gcAlloc<NativeBoundMethodObject>();
                        bound->name = name;
                        bound->id = resolveNativeMethodId(receiver, name);
                        bound->receiver = receiver;
                        set->methodCache.emplace(name, bound);
                    }
                }

                m_stack.pop();
                m_stack.push(Value(bound));
                DISPATCH();
            }

            if (!receiver.isInstance()) {
                return runtimeError("Only instances have properties.");
            }

            auto instance = receiver.asInstance();
            auto& cache =
                currentFrame().chunk->propertyInlineCacheAt(instructionOffset);
            if (cache.klass == instance->klass) {
                if (cache.kind == PropertyInlineCacheKind::FIELD &&
                    cache.slotIndex < instance->fieldSlots.size() &&
                    cache.slotIndex < instance->initializedFieldSlots.size() &&
                    instance->initializedFieldSlots[cache.slotIndex]) {
                    m_stack.pop();
                    m_stack.push(instance->fieldSlots[cache.slotIndex]);
                    DISPATCH();
                }

                if (cache.kind == PropertyInlineCacheKind::METHOD &&
                    cache.method != nullptr) {
                    auto bound = gcAlloc<BoundMethodObject>();
                    bound->receiver = instance;
                    bound->method = cache.method;

                    m_stack.pop();
                    m_stack.push(Value(bound));
                    DISPATCH();
                }
            }

            auto fieldSlotIt = instance->klass->fieldIndexByName.find(name);
            if (fieldSlotIt != instance->klass->fieldIndexByName.end()) {
                cache.klass = instance->klass;
                cache.kind = PropertyInlineCacheKind::FIELD;
                cache.slotIndex = fieldSlotIt->second;
                cache.method = nullptr;
                cache.fieldType.reset();

                if (fieldSlotIt->second < instance->fieldSlots.size() &&
                    fieldSlotIt->second <
                        instance->initializedFieldSlots.size() &&
                    instance->initializedFieldSlots[fieldSlotIt->second]) {
                    m_stack.pop();
                    m_stack.push(instance->fieldSlots[fieldSlotIt->second]);
                    DISPATCH();
                }
            }

            auto method = findMethodClosure(instance->klass, name);
            if (!method) {
                cache.klass = nullptr;
                cache.kind = PropertyInlineCacheKind::EMPTY;
                cache.slotIndex = 0;
                cache.method = nullptr;
                cache.fieldType.reset();
                return runtimeError("Undefined property '" + name + "'.");
            }

            cache.klass = instance->klass;
            cache.kind = PropertyInlineCacheKind::METHOD;
            cache.slotIndex = 0;
            cache.method = method;
            cache.fieldType.reset();

            auto bound = gcAlloc<BoundMethodObject>();
            bound->receiver = instance;
            bound->method = method;

            m_stack.pop();
            m_stack.push(Value(bound));
            DISPATCH();
        }

        VM_CASE(INVOKE) {
            size_t instructionOffset = currentInstructionOffset();
            const std::string& name = readNameConstant();
            uint8_t argumentCount = readByte();
            Status status =
                invokeProperty(instructionOffset, name, argumentCount);
            if (status != Status::OK) {
                return status;
            }
            DISPATCH();
        }

        VM_CASE(SET_PROPERTY) {
            size_t instructionOffset = currentInstructionOffset();
            const std::string& name = readNameConstant();
            Value value = m_stack.peek(0);
            Value receiver = m_stack.peek(1);
            if (!receiver.isInstance()) {
                return runtimeError("Only instances have fields.");
            }

            auto instance = receiver.asInstance();
            auto& cache =
                currentFrame().chunk->propertyInlineCacheAt(instructionOffset);
            if (cache.klass == instance->klass &&
                cache.kind == PropertyInlineCacheKind::FIELD &&
                cache.slotIndex < instance->fieldSlots.size() &&
                cache.slotIndex < instance->initializedFieldSlots.size() &&
                cache.fieldType) {
                if (!valueMatchesType(value, cache.fieldType)) {
                    return runtimeError(
                        "Type error: field '" + name + "' on class '" +
                        instance->klass->name + "' expects '" +
                        cache.fieldType->toString() + "', got '" +
                        valueTypeName(value) + "'.");
                }

                instance->fieldSlots[cache.slotIndex] = value;
                instance->initializedFieldSlots[cache.slotIndex] = 1;

                m_stack.pop();
                m_stack.pop();
                m_stack.push(value);
                DISPATCH();
            }

            auto fieldSlotIt = instance->klass->fieldIndexByName.find(name);
            if (fieldSlotIt == instance->klass->fieldIndexByName.end()) {
                cache.klass = nullptr;
                cache.kind = PropertyInlineCacheKind::EMPTY;
                cache.slotIndex = 0;
                cache.method = nullptr;
                cache.fieldType.reset();
                return runtimeError("Undefined field '" + name +
                                    "' on class '" + instance->klass->name +
                                    "'.");
            }

            auto fieldTypeIt = instance->klass->fieldTypes.find(name);
            if (fieldTypeIt == instance->klass->fieldTypes.end()) {
                return runtimeError("Undefined field '" + name +
                                    "' on class '" + instance->klass->name +
                                    "'.");
            }

            if (!valueMatchesType(value, fieldTypeIt->second)) {
                return runtimeError("Type error: field '" + name +
                                    "' on class '" + instance->klass->name +
                                    "' expects '" +
                                    fieldTypeIt->second->toString() +
                                    "', got '" + valueTypeName(value) + "'.");
            }

            cache.klass = instance->klass;
            cache.kind = PropertyInlineCacheKind::FIELD;
            cache.slotIndex = fieldSlotIt->second;
            cache.method = nullptr;
            cache.fieldType = fieldTypeIt->second;

            instance->fieldSlots[fieldSlotIt->second] = value;
            instance->initializedFieldSlots[fieldSlotIt->second] = 1;

            m_stack.pop();
            m_stack.pop();
            m_stack.push(value);
            DISPATCH();
        }

        VM_CASE(CALL) {
            uint8_t argumentCount = readByte();
            Value callee = m_stack.peek(argumentCount);
            size_t calleeIndex =
                m_stack.size() - static_cast<size_t>(argumentCount) - 1;
            Status status = callValue(callee, argumentCount, calleeIndex);
            if (status != Status::OK) {
                return status;
            }
            DISPATCH();
        }

        VM_CASE(CLOSURE) {
            Value constant = readConstant();
            if (!constant.isFunction()) {
                return runtimeError("CLOSURE expects a function constant.");
            }

            auto function = constant.asFunction();
            auto closure = gcAlloc<ClosureObject>();
            closure->function = function;
            closure->upvalues.reserve(function->upvalueCount);
            m_stack.push(Value(closure));

            for (uint8_t i = 0; i < function->upvalueCount; ++i) {
                uint8_t isLocal = readByte();
                uint8_t index = readByte();

                if (isLocal) {
                    size_t stackIndex = currentFrame().slotBase + index;
                    closure->upvalues.push_back(captureUpvalue(stackIndex));
                } else {
                    closure->upvalues.push_back(
                        currentFrame().closure->upvalues[index]);
                }
            }
            DISPATCH();
        }

        VM_CASE(CLOSE_UPVALUE) {
            closeUpvalues(m_stack.size() - 1);
            m_stack.pop();
            DISPATCH();
        }

        VM_CASE(BUILD_ARRAY) {
            uint8_t count = readByte();
            auto array = gcAlloc<ArrayObject>();
            array->elements.resize(count);

            auto mergeType = [&](const TypeRef& current,
                                 const TypeRef& next) -> TypeRef {
                if (!current) {
                    return next;
                }
                if (!next) {
                    return current;
                }
                if (current->isAny() || next->isAny()) {
                    return TypeInfo::makeAny();
                }
                if (isAssignable(next, current)) {
                    return current;
                }
                if (isAssignable(current, next)) {
                    return next;
                }
                if (current->isNumeric() && next->isNumeric()) {
                    TypeRef promoted = numericPromotion(current, next);
                    return promoted ? promoted : TypeInfo::makeAny();
                }
                return nullptr;
            };

            TypeRef inferredElementType = nullptr;
            for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
                Value element = m_stack.popMove();
                TypeRef elementType = inferRuntimeType(element);
                TypeRef merged = mergeType(inferredElementType, elementType);
                if (!merged) {
                    return runtimeError(
                        "Array literal elements must have consistent "
                        "types.");
                }
                inferredElementType = merged;
                array->elements[static_cast<size_t>(i)] = std::move(element);
            }

            array->elementType =
                inferredElementType ? inferredElementType : TypeInfo::makeAny();

            m_stack.push(Value(array));
            DISPATCH();
        }

        VM_CASE(BUILD_DICT) {
            uint8_t pairCount = readByte();
            auto dict = gcAlloc<DictObject>();
            dict->map.reserve(pairCount);

            auto mergeType = [&](const TypeRef& current,
                                 const TypeRef& next) -> TypeRef {
                if (!current) {
                    return next;
                }
                if (!next) {
                    return current;
                }
                if (current->isAny() || next->isAny()) {
                    return TypeInfo::makeAny();
                }
                if (isAssignable(next, current)) {
                    return current;
                }
                if (isAssignable(current, next)) {
                    return next;
                }
                if (current->isNumeric() && next->isNumeric()) {
                    TypeRef promoted = numericPromotion(current, next);
                    return promoted ? promoted : TypeInfo::makeAny();
                }
                return nullptr;
            };

            TypeRef keyType = nullptr;
            TypeRef valueType = nullptr;

            for (int i = 0; i < pairCount; ++i) {
                Value value = m_stack.popMove();
                Value keyValue = m_stack.popMove();

                TypeRef mergedKeyType =
                    mergeType(keyType, inferRuntimeType(keyValue));
                if (!mergedKeyType) {
                    return runtimeError(
                        "Dictionary literal keys must have consistent "
                        "types.");
                }
                keyType = mergedKeyType;

                TypeRef mergedValueType =
                    mergeType(valueType, inferRuntimeType(value));
                if (!mergedValueType) {
                    return runtimeError(
                        "Dictionary literal values must have consistent "
                        "types.");
                }
                valueType = mergedValueType;

                dict->map.insert_or_assign(std::move(keyValue),
                                           std::move(value));
            }

            dict->keyType = keyType ? keyType : TypeInfo::makeAny();
            dict->valueType = valueType ? valueType : TypeInfo::makeAny();

            m_stack.push(Value(dict));
            DISPATCH();
        }

        VM_CASE(GET_INDEX) {
            const Value& indexValue = m_stack.top();
            const Value& container = m_stack.second();

            if (container.isArray()) {
                size_t index = 0;
                if (!toArrayIndex(indexValue, index)) {
                    return runtimeError(
                        "Array index must be a non-negative integer.");
                }

                auto array = container.asArray();
                if (index >= array->elements.size()) {
                    return runtimeError("Array index out of bounds.");
                }

                m_stack.replaceTopPair(array->elements[index]);
                DISPATCH();
            }

            if (container.isDict()) {
                auto dict = container.asDict();
                auto it = dict->map.find(indexValue);
                if (it == dict->map.end()) {
                    return runtimeError("Dictionary key not found.");
                }

                m_stack.replaceTopPair(it->second);
                DISPATCH();
            }

            if (container.isSet()) {
                auto set = container.asSet();
                if (!valueMatchesType(indexValue, set->elementType)) {
                    return runtimeError(
                        "Type error: set lookup expects element type '" +
                        set->elementType->toString() + "', got '" +
                        valueTypeName(indexValue) + "'.");
                }
                m_stack.replaceTopPair(
                    Value(setContainsValue(set, indexValue)));
                DISPATCH();
            }

            return runtimeError(
                "Indexing is only supported on array, dict, and set.");
        }

        VM_CASE(SET_INDEX) {
            Value value = m_stack.popMove();
            Value indexValue = m_stack.popMove();
            Value container = m_stack.popMove();

            if (container.isArray()) {
                size_t index = 0;
                if (!toArrayIndex(indexValue, index)) {
                    return runtimeError(
                        "Array index must be a non-negative integer.");
                }

                auto array = container.asArray();
                if (index >= array->elements.size()) {
                    return runtimeError("Array index out of bounds.");
                }

                if (!valueMatchesType(value, array->elementType)) {
                    return runtimeError(
                        "Type error: array assignment expects element "
                        "type '" +
                        array->elementType->toString() + "', got '" +
                        valueTypeName(value) + "'.");
                }

                array->elements[index] = value;
                m_stack.push(std::move(value));
                DISPATCH();
            }

            if (container.isDict()) {
                auto dict = container.asDict();

                if (!valueMatchesType(indexValue, dict->keyType)) {
                    return runtimeError("Type error: dictionary key expects '" +
                                        dict->keyType->toString() + "', got '" +
                                        valueTypeName(indexValue) + "'.");
                }

                if (!valueMatchesType(value, dict->valueType)) {
                    return runtimeError(
                        "Type error: dictionary value expects '" +
                        dict->valueType->toString() + "', got '" +
                        valueTypeName(value) + "'.");
                }

                dict->map.insert_or_assign(std::move(indexValue), value);
                m_stack.push(std::move(value));
                DISPATCH();
            }

            return runtimeError(
                "Indexed assignment is only supported on array and dict.");
        }

        VM_CASE(DUP) {
            m_stack.push(m_stack.peek(0));
            DISPATCH();
        }

        VM_CASE(DUP2) {
            Value second = m_stack.peek(1);
            Value top = m_stack.peek(0);
            m_stack.push(second);
            m_stack.push(top);
            DISPATCH();
        }

        VM_CASE(ITER_INIT) {
            Value iterable = m_stack.popMove();
            auto iterator = gcAlloc<IteratorObject>();

            if (iterable.isArray()) {
                iterator->kind = IteratorObject::ARRAY_ITER;
                iterator->array = iterable.asArray();
            } else if (iterable.isDict()) {
                iterator->kind = IteratorObject::DICT_ITER;
                iterator->dict = iterable.asDict();

                iterator->dictKeys = sortedDictKeys(iterator->dict);
            } else if (iterable.isSet()) {
                iterator->kind = IteratorObject::SET_ITER;
                iterator->set = iterable.asSet();
            } else {
                return runtimeError(
                    "Foreach expects an iterable (array, dict, or set).");
            }

            m_stack.push(Value(iterator));
            DISPATCH();
        }

        VM_CASE(ITER_HAS_NEXT) {
            Value iteratorValue = m_stack.pop();
            if (!iteratorValue.isIterator()) {
                return runtimeError("Internal error: iterator expected.");
            }

            auto iterator = iteratorValue.asIterator();
            bool hasNext = false;

            switch (iterator->kind) {
                case IteratorObject::ARRAY_ITER:
                    hasNext =
                        iterator->array &&
                        iterator->index < iterator->array->elements.size();
                    break;
                case IteratorObject::DICT_ITER:
                    hasNext = iterator->dict &&
                              iterator->index < iterator->dictKeys.size();
                    break;
                case IteratorObject::SET_ITER:
                    hasNext = iterator->set &&
                              iterator->index < iterator->set->elements.size();
                    break;
            }

            m_stack.push(Value(hasNext));
            DISPATCH();
        }

        VM_CASE(ITER_HAS_NEXT_JUMP) {
            uint16_t offset = readShort();
            const Value& iteratorValue = m_stack.top();
            if (!iteratorValue.isIterator()) {
                return runtimeError("Internal error: iterator expected.");
            }

            auto iterator = iteratorValue.asIterator();
            bool hasNext = false;

            switch (iterator->kind) {
                case IteratorObject::ARRAY_ITER:
                    hasNext =
                        iterator->array &&
                        iterator->index < iterator->array->elements.size();
                    break;
                case IteratorObject::DICT_ITER:
                    hasNext = iterator->dict &&
                              iterator->index < iterator->dictKeys.size();
                    break;
                case IteratorObject::SET_ITER:
                    hasNext = iterator->set &&
                              iterator->index < iterator->set->elements.size();
                    break;
            }

            if (!hasNext) {
                currentFrame().ip += offset;
            }
            DISPATCH();
        }

        VM_CASE(ITER_NEXT) {
            Value iteratorValue = m_stack.pop();
            if (!iteratorValue.isIterator()) {
                return runtimeError("Internal error: iterator expected.");
            }

            auto iterator = iteratorValue.asIterator();
            Value nextValue;

            switch (iterator->kind) {
                case IteratorObject::ARRAY_ITER:
                    if (!iterator->array ||
                        iterator->index >= iterator->array->elements.size()) {
                        return runtimeError(
                            "Foreach iterator exhausted unexpectedly.");
                    }
                    nextValue = iterator->array->elements[iterator->index++];
                    break;
                case IteratorObject::DICT_ITER:
                    if (!iterator->dict ||
                        iterator->index >= iterator->dictKeys.size()) {
                        return runtimeError(
                            "Foreach iterator exhausted unexpectedly.");
                    }
                    nextValue = iterator->dictKeys[iterator->index++];
                    break;
                case IteratorObject::SET_ITER:
                    if (!iterator->set ||
                        iterator->index >= iterator->set->elements.size()) {
                        return runtimeError(
                            "Foreach iterator exhausted unexpectedly.");
                    }
                    nextValue = iterator->set->elements[iterator->index++];
                    break;
            }

            m_stack.push(nextValue);
            DISPATCH();
        }

        VM_CASE(ITER_NEXT_SET_LOCAL) {
            uint8_t slot = readByte();
            const Value& iteratorValue = m_stack.top();
            if (!iteratorValue.isIterator()) {
                return runtimeError("Internal error: iterator expected.");
            }

            auto iterator = iteratorValue.asIterator();
            Value nextValue;

            switch (iterator->kind) {
                case IteratorObject::ARRAY_ITER:
                    if (!iterator->array ||
                        iterator->index >= iterator->array->elements.size()) {
                        return runtimeError(
                            "Foreach iterator exhausted unexpectedly.");
                    }
                    nextValue = iterator->array->elements[iterator->index++];
                    break;
                case IteratorObject::DICT_ITER:
                    if (!iterator->dict ||
                        iterator->index >= iterator->dictKeys.size()) {
                        return runtimeError(
                            "Foreach iterator exhausted unexpectedly.");
                    }
                    nextValue = iterator->dictKeys[iterator->index++];
                    break;
                case IteratorObject::SET_ITER:
                    if (!iterator->set ||
                        iterator->index >= iterator->set->elements.size()) {
                        return runtimeError(
                            "Foreach iterator exhausted unexpectedly.");
                    }
                    nextValue = iterator->set->elements[iterator->index++];
                    break;
            }

            m_stack.setAt(currentFrame().slotBase + slot, std::move(nextValue));
            DISPATCH();
        }

        VM_CASE(IMPORT_MODULE) {
            const std::string& path = readConstant().asString();

            auto cached = m_moduleCache.find(path);
            if (cached != m_moduleCache.end()) {
                m_stack.push(Value(cached->second));
                DISPATCH();
            }

            if (m_importStack.find(path) != m_importStack.end()) {
                return runtimeError("Circular import detected: '" + path +
                                    "'.");
            }

            if (isNativeImportTargetId(path)) {
                std::string libraryPath = nativeImportLibraryPath(path);
                NativePackageDescriptor packageDescriptor;
                void* libraryHandle = nullptr;
                std::string packageError;
                if (!loadNativePackageDescriptor(libraryPath, packageDescriptor,
                                                 packageError, true,
                                                 &libraryHandle)) {
                    return runtimeError(packageError);
                }

                auto* module = gcAlloc<ModuleObject>();
                module->path = packageDescriptor.packageId;

                for (const auto& functionDescriptor : packageDescriptor.functions) {
                    auto* nativeFn = gcAlloc<NativeFunctionObject>();
                    nativeFn->name = functionDescriptor.name;
                    nativeFn->arity = functionDescriptor.arity;
                    nativeFn->callback = invokePackageNative;
                    auto& binding = m_nativePackageBindings.emplace_back();
                    binding.packageId = packageDescriptor.packageId;
                    binding.packageNamespace = packageDescriptor.packageNamespace;
                    binding.packageName = packageDescriptor.packageName;
                    binding.function = functionDescriptor.callback;
                    nativeFn->userdata = &binding;
                    module->exports[functionDescriptor.name] = Value(nativeFn);
                    module->exportTypes[functionDescriptor.name] =
                        functionDescriptor.type;
                }

                for (const auto& constantDescriptor : packageDescriptor.constants) {
                    Value value;
                    std::string conversionError;
                    ExprPackageValue constantValue = constantDescriptor.value;
                    if (constantValue.kind == EXPR_PACKAGE_VALUE_STR) {
                        constantValue.as.string_value.data =
                            constantDescriptor.stringValueStorage.c_str();
                        constantValue.as.string_value.length =
                            constantDescriptor.stringValueStorage.size();
                    }

                    if (!packageValueToValue(*this, constantValue, value,
                                             conversionError)) {
                        return runtimeError("Failed to materialize native package "
                                            "constant '" +
                                            constantDescriptor.name + "': " +
                                            conversionError);
                    }

                    module->exports[constantDescriptor.name] = value;
                    module->exportTypes[constantDescriptor.name] =
                        constantDescriptor.type;
                }

                m_moduleCache[path] = module;
                m_stack.push(Value(module));
                if (libraryHandle != nullptr) {
                    m_loadedNativeLibraryHandles.push_back(libraryHandle);
                }
                DISPATCH();
            }

            std::ifstream file(path);
            if (!file) {
                return runtimeError("Failed to open module '" + path + "'.");
            }

            std::string source((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
            std::string_view compileSource = stripStrictDirectiveLine(source);
            bool importStrict =
                m_defaultStrictMode || hasStrictDirective(source);

            m_importStack.insert(path);

            Chunk importedChunk;
            m_compiler.setStrictMode(importStrict);
            if (!m_compiler.compile(compileSource, importedChunk, path)) {
                m_importStack.erase(path);
                return Status::COMPILATION_ERROR;
            }

            auto* module = gcAlloc<ModuleObject>();
            module->path = path;

            auto savedGlobalNames = m_globalNames;
            auto savedGlobalTypes = m_globalTypes;
            auto savedGlobalValues = m_globalValues;
            auto savedGlobalDefined = m_globalDefined;
            ModuleObject* outerModule = m_currentModule;

            m_globalNames = m_compiler.globalNames();
            m_globalTypes = m_compiler.globalTypes();
            if (m_globalTypes.size() < m_globalNames.size()) {
                m_globalTypes.resize(m_globalNames.size(), TypeInfo::makeAny());
            }
            m_globalValues.assign(m_globalNames.size(), Value());
            m_globalDefined.assign(m_globalNames.size(), false);
            for (size_t i = 0; i < m_globalNames.size(); ++i) {
                auto nativeIt = m_nativeGlobals.find(m_globalNames[i]);
                if (nativeIt != m_nativeGlobals.end()) {
                    m_globalValues[i] = nativeIt->second;
                    m_globalDefined[i] = true;
                }
            }

            m_currentModule = module;

            auto function = gcAlloc<FunctionObject>();
            function->name = path;
            function->parameters = {};
            function->chunk = std::make_unique<Chunk>(std::move(importedChunk));
            function->upvalueCount = 0;

            auto closure = gcAlloc<ClosureObject>();
            closure->function = function;
            closure->upvalues = {};

            m_stack.push(Value(closure));
            size_t callerFrameCount = m_frameCount;
            Status callStatus = callClosure(closure, 0);
            if (callStatus != Status::OK) {
                m_stack.pop();
                m_currentModule = outerModule;
                m_globalNames = std::move(savedGlobalNames);
                m_globalTypes = std::move(savedGlobalTypes);
                m_globalValues = std::move(savedGlobalValues);
                m_globalDefined = std::move(savedGlobalDefined);
                m_importStack.erase(path);
                return callStatus;
            }

            Value ignored;
            Status moduleStatus = run(false, ignored, callerFrameCount);
            if (moduleStatus != Status::OK) {
                m_currentModule = outerModule;
                m_globalNames = std::move(savedGlobalNames);
                m_globalTypes = std::move(savedGlobalTypes);
                m_globalValues = std::move(savedGlobalValues);
                m_globalDefined = std::move(savedGlobalDefined);
                m_importStack.erase(path);
                return moduleStatus;
            }

            m_stack.pop();

            m_currentModule = outerModule;
            m_globalNames = std::move(savedGlobalNames);
            m_globalTypes = std::move(savedGlobalTypes);
            m_globalValues = std::move(savedGlobalValues);
            m_globalDefined = std::move(savedGlobalDefined);

            m_moduleCache[path] = module;
            m_importStack.erase(path);
            m_stack.push(Value(module));
            DISPATCH();
        }

        VM_CASE(EXPORT_NAME) {
            const std::string& name = readConstant().asString();
            if (m_currentModule != nullptr) {
                Value value = m_stack.peek(0);
                TypeRef declaredType = TypeInfo::makeAny();
                for (size_t i = 0; i < m_globalNames.size(); ++i) {
                    if (m_globalNames[i] == name) {
                        if (i < m_globalTypes.size() && m_globalTypes[i]) {
                            declaredType = m_globalTypes[i];
                        }
                        break;
                    }
                }

                if (declaredType && !declaredType->isAny() &&
                    !valueMatchesType(value, declaredType)) {
                    return runtimeError("Type error: cannot export '" + name +
                                        "' as '" + declaredType->toString() +
                                        "' from module '" +
                                        m_currentModule->path + "'; got '" +
                                        valueTypeName(value) + "'.");
                }

                m_currentModule->exports[name] = value;
                m_currentModule->exportTypes[name] = declaredType;
            }
            DISPATCH();
        }

        VM_CASE(JUMP) {
            uint16_t offset = readShort();
            currentFrame().ip += offset;
            DISPATCH();
        }

        VM_CASE(JUMP_IF_FALSE) {
            uint16_t offset = readShort();
            Value condition = m_stack.peek(0);
            if (isFalsey(condition)) {
                currentFrame().ip += offset;
            }
            DISPATCH();
        }

        VM_CASE(JUMP_IF_FALSE_POP) {
            uint16_t offset = readShort();
            bool conditionFalsey = isFalsey(m_stack.top());
            m_stack.pop();
            if (conditionFalsey) {
                currentFrame().ip += offset;
            }
            DISPATCH();
        }

        VM_CASE(LOOP) {
            uint16_t offset = readShort();
            currentFrame().ip -= offset;
            DISPATCH();
        }

        VM_CASE(SHIFT_LEFT) {
            Value b = m_stack.pop();
            Value a = m_stack.pop();
            uint32_t amount = 0;
            if (!valueToBitwiseShiftAmount(b, amount)) {
                return runtimeError("Operands must be integers for '<<'.");
            }

            if (a.isUnsignedInt()) {
                m_stack.push(Value(a.asUnsignedInt() << amount));
            } else if (a.isSignedInt()) {
                int64_t lhs = a.asSignedInt();
                m_stack.push(Value(static_cast<int64_t>(
                    static_cast<uint64_t>(lhs) << amount)));
            } else {
                return runtimeError("Operands must be integers for '<<'.");
            }
            DISPATCH();
        }

        VM_CASE(SHIFT_RIGHT) {
            Value b = m_stack.pop();
            Value a = m_stack.pop();
            uint32_t amount = 0;
            if (!valueToBitwiseShiftAmount(b, amount)) {
                return runtimeError("Operands must be integers for '>>'.");
            }

            if (a.isUnsignedInt()) {
                m_stack.push(Value(a.asUnsignedInt() >> amount));
            } else if (a.isSignedInt()) {
                m_stack.push(Value(a.asSignedInt() >> amount));
            } else {
                return runtimeError("Operands must be integers for '>>'.");
            }
            DISPATCH();
        }

        VM_CASE(BITWISE_AND) {
            Value rhsValue = m_stack.pop();
            Value lhsValue = m_stack.pop();
            uint64_t rhs = 0;
            uint64_t lhs = 0;
            if (!valueToBitwiseUnsignedInt(rhsValue, rhs) ||
                !valueToBitwiseUnsignedInt(lhsValue, lhs)) {
                return runtimeError("Operands must be integers for '&'.");
            }
            uint64_t result = lhs & rhs;
            if (lhsValue.isSignedInt() && rhsValue.isSignedInt()) {
                m_stack.push(Value(static_cast<int64_t>(result)));
            } else {
                m_stack.push(Value(result));
            }
            DISPATCH();
        }

        VM_CASE(BITWISE_OR) {
            Value rhsValue = m_stack.pop();
            Value lhsValue = m_stack.pop();
            uint64_t rhs = 0;
            uint64_t lhs = 0;
            if (!valueToBitwiseUnsignedInt(rhsValue, rhs) ||
                !valueToBitwiseUnsignedInt(lhsValue, lhs)) {
                return runtimeError("Operands must be integers for '|'.");
            }
            uint64_t result = lhs | rhs;
            if (lhsValue.isSignedInt() && rhsValue.isSignedInt()) {
                m_stack.push(Value(static_cast<int64_t>(result)));
            } else {
                m_stack.push(Value(result));
            }
            DISPATCH();
        }

        VM_CASE(BITWISE_XOR) {
            Value rhsValue = m_stack.pop();
            Value lhsValue = m_stack.pop();
            uint64_t rhs = 0;
            uint64_t lhs = 0;
            if (!valueToBitwiseUnsignedInt(rhsValue, rhs) ||
                !valueToBitwiseUnsignedInt(lhsValue, lhs)) {
                return runtimeError("Operands must be integers for '^'.");
            }
            uint64_t result = lhs ^ rhs;
            if (lhsValue.isSignedInt() && rhsValue.isSignedInt()) {
                m_stack.push(Value(static_cast<int64_t>(result)));
            } else {
                m_stack.push(Value(result));
            }
            DISPATCH();
        }

        VM_CASE(BITWISE_NOT) {
            Value value = m_stack.pop();
            if (value.isSignedInt()) {
                m_stack.push(Value(static_cast<int64_t>(
                    ~static_cast<uint64_t>(value.asSignedInt()))));
                DISPATCH();
            }
            if (value.isUnsignedInt()) {
                m_stack.push(Value(~value.asUnsignedInt()));
                DISPATCH();
            }
            return runtimeError("Operand must be an integer for '~'.");
        }

        VM_CASE(WIDEN_INT) {
            readByte();
            DISPATCH();
        }

        VM_CASE(NARROW_INT) {
            uint8_t kind = readByte();
            Value value = m_stack.pop();

            switch (static_cast<TypeKind>(kind)) {
                case TypeKind::I8: {
                    int64_t converted = 0;
                    if (!valueToSignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to i8.");
                    }
                    m_stack.push(Value(
                        static_cast<int64_t>(static_cast<int8_t>(converted))));
                    break;
                }
                case TypeKind::I16: {
                    int64_t converted = 0;
                    if (!valueToSignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to i16.");
                    }
                    m_stack.push(Value(
                        static_cast<int64_t>(static_cast<int16_t>(converted))));
                    break;
                }
                case TypeKind::I32: {
                    int64_t converted = 0;
                    if (!valueToSignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to i32.");
                    }
                    m_stack.push(Value(
                        static_cast<int64_t>(static_cast<int32_t>(converted))));
                    break;
                }
                case TypeKind::I64: {
                    int64_t converted = 0;
                    if (!valueToSignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to i64.");
                    }
                    m_stack.push(Value(converted));
                    break;
                }
                case TypeKind::U8: {
                    uint64_t converted = 0;
                    if (!valueToUnsignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to u8.");
                    }
                    m_stack.push(Value(static_cast<uint64_t>(
                        static_cast<uint8_t>(converted))));
                    break;
                }
                case TypeKind::U16: {
                    uint64_t converted = 0;
                    if (!valueToUnsignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to u16.");
                    }
                    m_stack.push(Value(static_cast<uint64_t>(
                        static_cast<uint16_t>(converted))));
                    break;
                }
                case TypeKind::U32: {
                    uint64_t converted = 0;
                    if (!valueToUnsignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to u32.");
                    }
                    m_stack.push(Value(static_cast<uint64_t>(
                        static_cast<uint32_t>(converted))));
                    break;
                }
                case TypeKind::U64:
                case TypeKind::USIZE: {
                    uint64_t converted = 0;
                    if (!valueToUnsignedInt(value, converted)) {
                        return runtimeError("Cannot cast value to u64.");
                    }
                    m_stack.push(Value(converted));
                    break;
                }
                default:
                    m_stack.push(value);
                    break;
            }
            DISPATCH();
        }

        VM_CASE(INT_TO_FLOAT) {
            Value value = m_stack.pop();
            double converted = 0.0;
            if (!valueToDouble(value, converted)) {
                return runtimeError("Cannot cast value to floating-point.");
            }
            m_stack.push(Value(converted));
            DISPATCH();
        }

        VM_CASE(FLOAT_TO_INT) {
            Value value = m_stack.pop();
            int64_t converted = 0;
            if (!valueToSignedInt(value, converted)) {
                return runtimeError("Cannot cast value to integer.");
            }
            m_stack.push(Value(converted));
            DISPATCH();
        }

        VM_CASE(INT_TO_STR) {
            Value value = m_stack.pop();
            if (value.isSignedInt()) {
                m_stack.push(makeStringValue(std::to_string(value.asSignedInt())));
                DISPATCH();
            }
            if (value.isUnsignedInt()) {
                m_stack.push(
                    makeStringValue(std::to_string(value.asUnsignedInt())));
                DISPATCH();
            }
            if (value.isNumber()) {
                m_stack.push(makeStringValue(std::to_string(value.asNumber())));
                DISPATCH();
            }
            return runtimeError("Cannot cast value to str.");
        }

        VM_CASE(CHECK_INSTANCE_TYPE) {
            const std::string& expectedClass = readNameConstant();
            Value value = m_stack.peek(0);

            if (!value.isInstance()) {
                return runtimeError("Type error: expected instance of '" +
                                    expectedClass + "', got '" +
                                    valueTypeName(value) + "'.");
            }

            auto instance = value.asInstance();
            if (!isInstanceOfClass(instance, expectedClass)) {
                std::string actualClass = (instance && instance->klass)
                                              ? instance->klass->name
                                              : std::string("<unknown>");
                return runtimeError("Type error: expected instance of '" +
                                    expectedClass + "', got '" + actualClass +
                                    "'.");
            }
            DISPATCH();
        }

        VM_CASE(INT_NEGATE) {
            int64_t value = requireSignedInt(m_stack.pop());
            m_stack.push(Value(wrapSignedSub(0, value)));
            DISPATCH();
        }

    } catch (const std::exception& error) {
        return runtimeError(error.what());
    }
}

#undef DISPATCH
#undef DISPATCH_INSTRUCTION

#undef VM_OPCODE_LABEL
#undef VM_OPCODE_ADDR
#undef VM_CASE

VirtualMachine::~VirtualMachine() {
    resetRuntimeState();
}

void VirtualMachine::unloadNativeLibraries() {
    for (void* handle : m_loadedNativeLibraryHandles) {
        if (handle != nullptr) {
            dlclose(handle);
        }
    }
    m_loadedNativeLibraryHandles.clear();
}

void VirtualMachine::resetRuntimeState() {
    m_stack.reset();
    m_frameCount = 0;
    m_activeFrame = nullptr;
    m_openUpvaluesHead = nullptr;
    m_globalNames.clear();
    m_globalTypes.clear();
    m_globalValues.clear();
    m_globalDefined.clear();
    m_nativeGlobals.clear();
    m_moduleCache.clear();
    m_importStack.clear();
    m_nativePackageBindings.clear();
    m_currentModule = nullptr;
    m_gc.sweep();
    unloadNativeLibraries();
}

Status VirtualMachine::interpret(std::string_view source, bool printReturnValue,
                                 bool traceEnabled, bool disassembleEnabled,
                                 const std::string& sourcePath,
                                 bool strictMode) {
    std::string_view compileSource = stripStrictDirectiveLine(source);
    Chunk chunk;
    resetRuntimeState();
    m_defaultStrictMode = strictMode || hasStrictDirective(source);
    m_compiler.setGC(&m_gc);
    m_compiler.setPackageSearchPaths(m_packageSearchPaths);
    m_compiler.setStrictMode(m_defaultStrictMode);
    m_traceEnabled = traceEnabled;
    m_disassembleEnabled = disassembleEnabled;

    if (!m_compiler.compile(compileSource, chunk, sourcePath)) {
        return Status::COMPILATION_ERROR;
    }

    if (m_disassembleEnabled) {
        std::cout << "== disassembly ==" << std::endl;
        int offset = 0;
        while (offset < chunk.count()) {
            offset = chunk.disassembleInstruction(offset);
        }
        std::cout << "== end disassembly ==" << std::endl;
    }

    m_globalNames = m_compiler.globalNames();
    m_globalTypes = m_compiler.globalTypes();
    if (m_globalTypes.size() < m_globalNames.size()) {
        m_globalTypes.resize(m_globalNames.size(), TypeInfo::makeAny());
    }
    m_globalValues.assign(m_globalNames.size(), Value());
    m_globalDefined.assign(m_globalNames.size(), false);

    auto defineNative = [&](const std::string& name, int arity) {
        auto nativeFn = gcAlloc<NativeFunctionObject>();
        nativeFn->name = name;
        nativeFn->arity = arity;
        nativeFn->callback = invokeBuiltinNative;
        nativeFn->userdata = builtinNativeTag(name);
        m_nativeGlobals[name] = Value(nativeFn);

        for (size_t i = 0; i < m_globalNames.size(); ++i) {
            if (m_globalNames[i] == name) {
                m_globalValues[i] = Value(nativeFn);
                m_globalDefined[i] = true;
                break;
            }
        }
    };

    for (const auto& descriptor : standardLibraryNatives()) {
        defineNative(descriptor.name, descriptor.arity);
    }

    m_frames[m_frameCount++] =
        CallFrame{&chunk, chunk.getBytes(), 0, 0, nullptr, nullptr};
    m_activeFrame = &m_frames[m_frameCount - 1];

    Value returnValue;
    return run(printReturnValue, returnValue, 0);
}
