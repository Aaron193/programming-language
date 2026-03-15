#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "GcObject.hpp"
#include "NativePackageAPI.hpp"
#include "RuntimeCommon.hpp"
#include "TypeInfo.hpp"

class Chunk;
class GC;
struct ClassObject;
struct InstanceObject;
struct BoundMethodObject;
struct NativeFunctionObject;
struct NativeBoundMethodObject;
struct ClosureObject;
struct UpvalueObject;
struct ArrayObject;
struct DictObject;
struct SetObject;
struct IteratorObject;
struct ModuleObject;
struct StringObject;
struct NativeHandleObject;
enum class NativeMethodId : uint8_t;

enum class PropertyInlineCacheKind : uint8_t {
    EMPTY,
    FIELD,
    METHOD,
    NATIVE_METHOD,
};

enum class NativeReceiverKind : uint8_t {
    NONE,
    ARRAY,
    DICT,
    SET,
};

struct PropertyInlineCache {
    ClassObject* klass = nullptr;
    PropertyInlineCacheKind kind = PropertyInlineCacheKind::EMPTY;
    size_t slotIndex = 0;
    ClosureObject* method = nullptr;
    TypeRef fieldType;
    NativeReceiverKind nativeReceiverKind = NativeReceiverKind::NONE;
    NativeMethodId nativeMethodId{};
};

enum class CallInlineCacheKind : uint8_t {
    EMPTY,
    CLOSURE,
    BOUND_METHOD,
    NATIVE,
    NATIVE_BOUND,
    CLASS,
};

struct CallInlineCache {
    CallInlineCacheKind kind = CallInlineCacheKind::EMPTY;
    GcObject* target = nullptr;
};

struct FunctionObject : GcObject {
    std::string name;
    std::vector<std::string> parameters;
    std::unique_ptr<Chunk> chunk;
    uint8_t upvalueCount = 0;

    void trace(GC& gc) override;
};

struct ClassObject : GcObject {
    std::string name;
    ClassObject* superclass = nullptr;
    std::unordered_map<std::string, ClosureObject*> methods;
    std::unordered_map<std::string, TypeRef> fieldTypes;
    std::unordered_map<std::string, TypeRef> methodTypes;
    std::vector<std::string> fieldNames;
    std::vector<TypeRef> fieldTypesBySlot;
    std::unordered_map<std::string, size_t> fieldIndexByName;

    void trace(GC& gc) override;
};

struct BoundMethodObject : GcObject {
    InstanceObject* receiver = nullptr;
    ClosureObject* method = nullptr;

    void trace(GC& gc) override;
};

enum class NativeMethodId : uint8_t {
    ARRAY_PUSH,
    ARRAY_POP,
    ARRAY_SIZE,
    ARRAY_HAS,
    ARRAY_INSERT,
    ARRAY_REMOVE,
    ARRAY_CLEAR,
    ARRAY_IS_EMPTY,
    ARRAY_FIRST,
    ARRAY_LAST,
    DICT_GET,
    DICT_SET,
    DICT_HAS,
    DICT_KEYS,
    DICT_VALUES,
    DICT_SIZE,
    DICT_REMOVE,
    DICT_CLEAR,
    DICT_IS_EMPTY,
    DICT_GET_OR,
    SET_ADD,
    SET_HAS,
    SET_REMOVE,
    SET_SIZE,
    SET_TO_ARRAY,
    SET_CLEAR,
    SET_IS_EMPTY,
    SET_UNION,
    SET_INTERSECT,
    SET_DIFFERENCE,
    INVALID,
};

struct NativeFunctionObject : GcObject {
    std::string name;
    int arity;
    NativeInvokeFn callback = nullptr;
    const void* userdata = nullptr;

    void trace(GC& gc) override;
};

struct StringObject : GcObject {
    std::string value;
    size_t hashValue = 0;
    bool isInterned = false;

    void trace(GC& gc) override;
};

struct NativeHandleObject : GcObject {
    std::string packageNamespace;
    std::string packageName;
    std::string packageId;
    std::string typeName;
    void* handleData = nullptr;
    ExprPackageHandleFinalizer finalizer = nullptr;

    void trace(GC& gc) override;
    void release();
    ~NativeHandleObject() override;
};

/*
 OpCode is a bytecode *instruction*
 eg: [CONST 2, CONST 3, ADD] -> 2 + 3 -> 5
*/
enum OpCode {
    RETURN,
    CONSTANT,
    NIL,
    TRUE_LITERAL,
    FALSE_LITERAL,
    NEGATE,
    NOT,
    EQUAL_OP,
    NOT_EQUAL_OP,
    ADD,
    SUB,
    MULT,
    DIV,
    IADD,
    ISUB,
    IMULT,
    IDIV,
    IMOD,
    UADD,
    USUB,
    UMULT,
    UDIV,
    UMOD,
    GREATER_THAN,
    LESS_THAN,
    GREATER_EQUAL_THAN,
    LESS_EQUAL_THAN,
    IGREATER,
    ILESS,
    IGREATER_EQ,
    ILESS_EQ,
    UGREATER,
    ULESS,
    UGREATER_EQ,
    ULESS_EQ,
    POP,
    PRINT_OP,
    DEFINE_GLOBAL,
    GET_GLOBAL,
    SET_GLOBAL,
    DEFINE_GLOBAL_SLOT,
    GET_GLOBAL_SLOT,
    SET_GLOBAL_SLOT,
    GET_LOCAL,
    SET_LOCAL,
    GET_UPVALUE,
    SET_UPVALUE,
    CLASS_OP,
    INHERIT,
    METHOD,
    GET_THIS,
    GET_SUPER,
    INVOKE_SUPER,
    GET_PROPERTY,
    INVOKE,
    SET_PROPERTY,
    GET_FIELD_SLOT,
    SET_FIELD_SLOT,
    CALL,
    CLOSURE,
    CLOSE_UPVALUE,
    BUILD_ARRAY,
    BUILD_DICT,
    GET_INDEX,
    SET_INDEX,
    DUP,
    DUP2,
    JUMP,
    JUMP_IF_FALSE,
    JUMP_IF_FALSE_POP,
    LOOP,
    SHIFT_LEFT,
    SHIFT_RIGHT,
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,
    BITWISE_NOT,
    WIDEN_INT,
    NARROW_INT,
    INT_TO_FLOAT,
    FLOAT_TO_INT,
    INT_TO_STR,
    CHECK_INSTANCE_TYPE,
    INT_NEGATE,
    ITER_INIT,
    ITER_HAS_NEXT,
    ITER_HAS_NEXT_JUMP,
    ITER_NEXT,
    ITER_NEXT_SET_LOCAL,
    IMPORT_MODULE,
    EXPORT_NAME,
};

struct Value {
    enum class Kind : uint8_t {
        NUMBER,
        SIGNED_INT,
        UNSIGNED_INT,
        BOOL,
        NIL,
        STRING,
        FUNCTION,
        CLASS,
        INSTANCE,
        BOUND_METHOD,
        NATIVE,
        NATIVE_BOUND,
        CLOSURE,
        ARRAY,
        DICT,
        SET,
        ITERATOR,
        MODULE,
        NATIVE_HANDLE,
    };

    union Payload {
        double number;
        int64_t signedInt;
        uint64_t unsignedInt;
        bool boolean;
        void* object;

        Payload() : unsignedInt(0) {}
    };

    Kind kind = Kind::NIL;
    Payload payload;

    Value() = default;

    Value(double value) : kind(Kind::NUMBER) { payload.number = value; }

    Value(int64_t value) : kind(Kind::SIGNED_INT) { payload.signedInt = value; }

    Value(uint64_t value) : kind(Kind::UNSIGNED_INT) {
        payload.unsignedInt = value;
    }

    Value(bool value) : kind(Kind::BOOL) { payload.boolean = value; }
    Value(StringObject* value) : kind(Kind::STRING) { payload.object = value; }
    Value(FunctionObject* value) : kind(Kind::FUNCTION) {
        payload.object = value;
    }
    Value(ClassObject* value) : kind(Kind::CLASS) { payload.object = value; }
    Value(InstanceObject* value) : kind(Kind::INSTANCE) {
        payload.object = value;
    }
    Value(BoundMethodObject* value) : kind(Kind::BOUND_METHOD) {
        payload.object = value;
    }
    Value(NativeFunctionObject* value) : kind(Kind::NATIVE) {
        payload.object = value;
    }
    Value(NativeBoundMethodObject* value) : kind(Kind::NATIVE_BOUND) {
        payload.object = value;
    }
    Value(ClosureObject* value) : kind(Kind::CLOSURE) {
        payload.object = value;
    }
    Value(ArrayObject* value) : kind(Kind::ARRAY) { payload.object = value; }
    Value(DictObject* value) : kind(Kind::DICT) { payload.object = value; }
    Value(SetObject* value) : kind(Kind::SET) { payload.object = value; }
    Value(IteratorObject* value) : kind(Kind::ITERATOR) {
        payload.object = value;
    }
    Value(ModuleObject* value) : kind(Kind::MODULE) { payload.object = value; }
    Value(NativeHandleObject* value) : kind(Kind::NATIVE_HANDLE) {
        payload.object = value;
    }

    bool isNumber() const { return kind == Kind::NUMBER; }
    bool isSignedInt() const { return kind == Kind::SIGNED_INT; }
    bool isUnsignedInt() const { return kind == Kind::UNSIGNED_INT; }
    bool isAnyNumeric() const {
        return isNumber() || isSignedInt() || isUnsignedInt();
    }
    bool isBool() const { return kind == Kind::BOOL; }
    bool isNil() const { return kind == Kind::NIL; }
    bool isString() const { return kind == Kind::STRING; }
    bool isFunction() const { return kind == Kind::FUNCTION; }
    bool isClass() const { return kind == Kind::CLASS; }
    bool isInstance() const { return kind == Kind::INSTANCE; }
    bool isBoundMethod() const { return kind == Kind::BOUND_METHOD; }
    bool isNative() const { return kind == Kind::NATIVE; }
    bool isNativeBound() const { return kind == Kind::NATIVE_BOUND; }
    bool isClosure() const { return kind == Kind::CLOSURE; }
    bool isArray() const { return kind == Kind::ARRAY; }
    bool isDict() const { return kind == Kind::DICT; }
    bool isSet() const { return kind == Kind::SET; }
    bool isIterator() const { return kind == Kind::ITERATOR; }
    bool isModule() const { return kind == Kind::MODULE; }
    bool isNativeHandle() const { return kind == Kind::NATIVE_HANDLE; }

    double asNumber() const { return payload.number; }
    int64_t asSignedInt() const { return payload.signedInt; }
    uint64_t asUnsignedInt() const { return payload.unsignedInt; }
    bool asBool() const { return payload.boolean; }
    StringObject* asStringObject() const {
        return static_cast<StringObject*>(payload.object);
    }
    const std::string& asString() const { return asStringObject()->value; }
    FunctionObject* asFunction() const {
        return static_cast<FunctionObject*>(payload.object);
    }
    ClassObject* asClass() const {
        return static_cast<ClassObject*>(payload.object);
    }
    InstanceObject* asInstance() const {
        return static_cast<InstanceObject*>(payload.object);
    }
    BoundMethodObject* asBoundMethod() const {
        return static_cast<BoundMethodObject*>(payload.object);
    }
    NativeFunctionObject* asNative() const {
        return static_cast<NativeFunctionObject*>(payload.object);
    }
    NativeBoundMethodObject* asNativeBound() const {
        return static_cast<NativeBoundMethodObject*>(payload.object);
    }
    ClosureObject* asClosure() const {
        return static_cast<ClosureObject*>(payload.object);
    }
    ArrayObject* asArray() const {
        return static_cast<ArrayObject*>(payload.object);
    }
    DictObject* asDict() const {
        return static_cast<DictObject*>(payload.object);
    }
    SetObject* asSet() const { return static_cast<SetObject*>(payload.object); }
    IteratorObject* asIterator() const {
        return static_cast<IteratorObject*>(payload.object);
    }
    ModuleObject* asModule() const {
        return static_cast<ModuleObject*>(payload.object);
    }
    NativeHandleObject* asNativeHandle() const {
        return static_cast<NativeHandleObject*>(payload.object);
    }
};

inline uint64_t normalizeDoubleBits(double number) {
    if (number == 0.0) {
        return 0;
    }

    uint64_t bits = 0;
    std::memcpy(&bits, &number, sizeof(bits));
    constexpr uint64_t kExponentMask = 0x7ff0000000000000ULL;
    constexpr uint64_t kMantissaMask = 0x000fffffffffffffULL;
    if ((bits & kExponentMask) == kExponentMask &&
        (bits & kMantissaMask) != 0) {
        // Canonicalize NaN payloads for stable hashing/ordering.
        return 0x7ff8000000000000ULL;
    }

    return bits;
}

inline uintptr_t valuePointerBits(const Value& value) {
    switch (value.kind) {
        case Value::Kind::STRING:
            return reinterpret_cast<uintptr_t>(value.asStringObject());
        case Value::Kind::FUNCTION:
            return reinterpret_cast<uintptr_t>(value.asFunction());
        case Value::Kind::CLASS:
            return reinterpret_cast<uintptr_t>(value.asClass());
        case Value::Kind::INSTANCE:
            return reinterpret_cast<uintptr_t>(value.asInstance());
        case Value::Kind::BOUND_METHOD:
            return reinterpret_cast<uintptr_t>(value.asBoundMethod());
        case Value::Kind::NATIVE:
            return reinterpret_cast<uintptr_t>(value.asNative());
        case Value::Kind::NATIVE_BOUND:
            return reinterpret_cast<uintptr_t>(value.asNativeBound());
        case Value::Kind::CLOSURE:
            return reinterpret_cast<uintptr_t>(value.asClosure());
        case Value::Kind::ARRAY:
            return reinterpret_cast<uintptr_t>(value.asArray());
        case Value::Kind::DICT:
            return reinterpret_cast<uintptr_t>(value.asDict());
        case Value::Kind::SET:
            return reinterpret_cast<uintptr_t>(value.asSet());
        case Value::Kind::ITERATOR:
            return reinterpret_cast<uintptr_t>(value.asIterator());
        case Value::Kind::MODULE:
            return reinterpret_cast<uintptr_t>(value.asModule());
        case Value::Kind::NATIVE_HANDLE:
            return reinterpret_cast<uintptr_t>(value.asNativeHandle());
        default:
            return 0;
    }
}

inline bool valueSortLess(const Value& lhs, const Value& rhs) {
    if (lhs.kind != rhs.kind) {
        return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
    }

    switch (lhs.kind) {
        case Value::Kind::NUMBER:
            return normalizeDoubleBits(lhs.asNumber()) <
                   normalizeDoubleBits(rhs.asNumber());
        case Value::Kind::SIGNED_INT:
            return lhs.asSignedInt() < rhs.asSignedInt();
        case Value::Kind::UNSIGNED_INT:
            return lhs.asUnsignedInt() < rhs.asUnsignedInt();
        case Value::Kind::BOOL:
            return lhs.asBool() < rhs.asBool();
        case Value::Kind::NIL:
            return false;
        case Value::Kind::STRING:
            return lhs.asString() < rhs.asString();
        default:
            return valuePointerBits(lhs) < valuePointerBits(rhs);
    }
}

struct ValueHash {
    size_t operator()(const Value& value) const noexcept {
        const size_t indexHash =
            std::hash<uint8_t>{}(static_cast<uint8_t>(value.kind));
        size_t payloadHash = 0;

        switch (value.kind) {
            case Value::Kind::NUMBER:
                payloadHash = std::hash<uint64_t>{}(
                    normalizeDoubleBits(value.asNumber()));
                break;
            case Value::Kind::SIGNED_INT:
                payloadHash = std::hash<int64_t>{}(value.asSignedInt());
                break;
            case Value::Kind::UNSIGNED_INT:
                payloadHash = std::hash<uint64_t>{}(value.asUnsignedInt());
                break;
            case Value::Kind::BOOL:
                payloadHash = std::hash<bool>{}(value.asBool());
                break;
            case Value::Kind::NIL:
                payloadHash = 0;
                break;
            case Value::Kind::STRING:
                payloadHash = value.asStringObject()->hashValue;
                break;
            default:
                payloadHash = std::hash<uintptr_t>{}(valuePointerBits(value));
                break;
        }

        return indexHash ^ (payloadHash + 0x9e3779b97f4a7c15ULL +
                            (indexHash << 6) + (indexHash >> 2));
    }
};

struct ValueEqual {
    bool operator()(const Value& lhs, const Value& rhs) const {
        if (lhs.kind != rhs.kind) {
            return false;
        }

        switch (lhs.kind) {
            case Value::Kind::NUMBER:
                return normalizeDoubleBits(lhs.asNumber()) ==
                       normalizeDoubleBits(rhs.asNumber());
            case Value::Kind::SIGNED_INT:
                return lhs.asSignedInt() == rhs.asSignedInt();
            case Value::Kind::UNSIGNED_INT:
                return lhs.asUnsignedInt() == rhs.asUnsignedInt();
            case Value::Kind::BOOL:
                return lhs.asBool() == rhs.asBool();
            case Value::Kind::NIL:
                return true;
            case Value::Kind::STRING:
                if (lhs.asStringObject()->isInterned &&
                    rhs.asStringObject()->isInterned) {
                    return lhs.asStringObject() == rhs.asStringObject();
                }
                return lhs.asString() == rhs.asString();
            default:
                return valuePointerBits(lhs) == valuePointerBits(rhs);
        }
    }
};

struct UpvalueObject : GcObject {
    size_t stackIndex = 0;
    bool isClosed = false;
    Value closed;
    UpvalueObject* nextOpen = nullptr;

    void trace(GC& gc) override;
};

struct ClosureObject : GcObject {
    FunctionObject* function = nullptr;
    std::vector<UpvalueObject*> upvalues;

    void trace(GC& gc) override;
};

struct InstanceObject : GcObject {
    ClassObject* klass = nullptr;
    std::vector<Value> fieldSlots;
    std::vector<uint8_t> initializedFieldSlots;

    void trace(GC& gc) override;
};

struct ArrayObject : GcObject {
    TypeRef elementType = TypeInfo::makeAny();
    std::vector<Value> elements;
    std::unordered_map<std::string, NativeBoundMethodObject*> methodCache;

    void trace(GC& gc) override;
};

struct DictObject : GcObject {
    TypeRef keyType = TypeInfo::makeAny();
    TypeRef valueType = TypeInfo::makeAny();
    std::unordered_map<Value, Value, ValueHash, ValueEqual> map;
    std::unordered_map<std::string, NativeBoundMethodObject*> methodCache;
    mutable size_t mutationVersion = 0;
    mutable size_t orderedKeysVersion = 0;
    mutable std::vector<Value> orderedKeysCache;

    void trace(GC& gc) override;
};

inline void invalidateDictOrderCache(DictObject* dict) {
    if (dict != nullptr) {
        ++dict->mutationVersion;
    }
}

inline const std::vector<Value>& orderedDictKeys(const DictObject* dict) {
    static const std::vector<Value> empty;
    if (dict == nullptr) {
        return empty;
    }

    if (dict->orderedKeysVersion != dict->mutationVersion) {
        dict->orderedKeysCache.clear();
        dict->orderedKeysCache.reserve(dict->map.size());
        for (const auto& entry : dict->map) {
            dict->orderedKeysCache.push_back(entry.first);
        }
        std::sort(dict->orderedKeysCache.begin(), dict->orderedKeysCache.end(),
                  valueSortLess);
        dict->orderedKeysVersion = dict->mutationVersion;
    }

    return dict->orderedKeysCache;
}

struct SetObject : GcObject {
    TypeRef elementType = TypeInfo::makeAny();
    std::vector<Value> elements;
    std::unordered_map<Value, size_t, ValueHash, ValueEqual> indexByValue;
    std::unordered_map<std::string, NativeBoundMethodObject*> methodCache;

    void trace(GC& gc) override;
};

struct IteratorObject : GcObject {
    enum Kind { ARRAY_ITER, DICT_ITER, SET_ITER } kind = ARRAY_ITER;

    ArrayObject* array = nullptr;
    DictObject* dict = nullptr;
    SetObject* set = nullptr;
    std::vector<Value> dictKeys;
    size_t index = 0;

    void trace(GC& gc) override;
};

struct ModuleObject : GcObject {
    std::string path;
    std::unordered_map<std::string, Value> exports;
    std::unordered_map<std::string, TypeRef> exportTypes;

    void trace(GC& gc) override;
};

struct NativeBoundMethodObject : GcObject {
    std::string name;
    NativeMethodId id = NativeMethodId::INVALID;
    Value receiver;

    void trace(GC& gc) override;
};

inline bool operator==(const Value& lhs, const Value& rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }

    switch (lhs.kind) {
        case Value::Kind::NUMBER:
            return normalizeDoubleBits(lhs.asNumber()) ==
                   normalizeDoubleBits(rhs.asNumber());
        case Value::Kind::SIGNED_INT:
            return lhs.asSignedInt() == rhs.asSignedInt();
        case Value::Kind::UNSIGNED_INT:
            return lhs.asUnsignedInt() == rhs.asUnsignedInt();
        case Value::Kind::BOOL:
            return lhs.asBool() == rhs.asBool();
        case Value::Kind::NIL:
            return true;
        case Value::Kind::STRING:
            if (lhs.asStringObject()->isInterned &&
                rhs.asStringObject()->isInterned) {
                return lhs.asStringObject() == rhs.asStringObject();
            }
            return lhs.asString() == rhs.asString();
        default:
            return valuePointerBits(lhs) == valuePointerBits(rhs);
    }
}

inline bool operator!=(const Value& lhs, const Value& rhs) {
    return !(lhs == rhs);
}

inline void printValueInternal(std::ostream& stream, const Value& value,
                               std::unordered_set<const void*>& active,
                               int depth) {
    constexpr int kMaxPrintDepth = 12;
    if (depth > kMaxPrintDepth) {
        stream << "...";
        return;
    }

    if (value.isNumber()) {
        stream << value.asNumber();
    } else if (value.isSignedInt()) {
        stream << value.asSignedInt();
    } else if (value.isUnsignedInt()) {
        stream << value.asUnsignedInt();
    } else if (value.isBool()) {
        stream << (value.asBool() ? "true" : "false");
    } else if (value.isNil()) {
        stream << "null";
    } else if (value.isFunction()) {
        auto function = value.asFunction();
        stream << "<function " << function->name << ">";
    } else if (value.isClass()) {
        auto klass = value.asClass();
        stream << "<class " << klass->name << ">";
    } else if (value.isInstance()) {
        auto instance = value.asInstance();
        stream << "<instance " << instance->klass->name << ">";
    } else if (value.isBoundMethod()) {
        auto bound = value.asBoundMethod();
        stream << "<bound method " << bound->method->function->name << ">";
    } else if (value.isNative()) {
        auto native = value.asNative();
        stream << "<native " << native->name << ">";
    } else if (value.isNativeBound()) {
        auto bound = value.asNativeBound();
        stream << "<native method " << bound->name << ">";
    } else if (value.isClosure()) {
        auto closure = value.asClosure();
        stream << "<closure " << closure->function->name << ">";
    } else if (value.isArray()) {
        auto array = value.asArray();
        const void* identity = array;
        if (active.find(identity) != active.end()) {
            stream << "[...]";
            return;
        }

        active.insert(identity);
        stream << "[";
        for (size_t index = 0; index < array->elements.size(); ++index) {
            if (index > 0) {
                stream << ", ";
            }
            printValueInternal(stream, array->elements[index], active,
                               depth + 1);
        }
        stream << "]";
        active.erase(identity);
    } else if (value.isDict()) {
        auto dict = value.asDict();
        const void* identity = dict;
        if (active.find(identity) != active.end()) {
            stream << "{...}";
            return;
        }

        active.insert(identity);
        const auto& keys = orderedDictKeys(dict);

        stream << "{";
        for (size_t index = 0; index < keys.size(); ++index) {
            if (index > 0) {
                stream << ", ";
            }

            const Value& key = keys[index];
            if (key.isString()) {
                stream << '"' << key.asString() << '"';
            } else {
                printValueInternal(stream, key, active, depth + 1);
            }
            stream << ": ";

            auto valueIt = dict->map.find(key);
            if (valueIt != dict->map.end()) {
                printValueInternal(stream, valueIt->second, active, depth + 1);
            } else {
                stream << "null";
            }
        }
        stream << "}";
        active.erase(identity);
    } else if (value.isSet()) {
        auto set = value.asSet();
        const void* identity = set;
        if (active.find(identity) != active.end()) {
            stream << "Set(...)";
            return;
        }

        active.insert(identity);
        stream << "Set(";
        for (size_t index = 0; index < set->elements.size(); ++index) {
            if (index > 0) {
                stream << ", ";
            }
            printValueInternal(stream, set->elements[index], active, depth + 1);
        }
        stream << ")";
        active.erase(identity);
    } else if (value.isIterator()) {
        stream << "<iterator>";
    } else if (value.isModule()) {
        auto module = value.asModule();
        stream << "<module " << module->path << ">";
    } else if (value.isNativeHandle()) {
        auto handle = value.asNativeHandle();
        stream << "<native handle " << handle->packageId << ":"
               << handle->typeName << ">";
    } else if (value.isString()) {
        stream << value.asString();
    } else {
        stream << "<unknown>";
    }
}

inline std::ostream& operator<<(std::ostream& stream, const Value& value) {
    std::unordered_set<const void*> active;
    printValueInternal(stream, value, active, 0);

    return stream;
}

class Chunk {
   private:
    // Dynamic array to hold our bytecode
    std::unique_ptr<std::vector<uint8_t>> m_bytes =
        std::make_unique<std::vector<uint8_t>>();

    // dynamic array to hold a lists of per-chunk constants
    std::unique_ptr<std::vector<Value>> m_constants =
        std::make_unique<std::vector<Value>>();

    // dynamic array to hold line numbers
    std::unique_ptr<std::vector<int>> m_lines =
        std::make_unique<std::vector<int>>();
    std::unique_ptr<std::vector<PropertyInlineCache>> m_propertyInlineCaches =
        std::make_unique<std::vector<PropertyInlineCache>>();
    std::unique_ptr<std::vector<CallInlineCache>> m_callInlineCaches =
        std::make_unique<std::vector<CallInlineCache>>();

    void disassemble(std::string label);
    int simpleInstruction(const std::string& label, int offset);
    int constantInstruction(const std::string& label, int offset);

   public:
    Chunk() = default;
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;
    ~Chunk() = default;

    void write(uint8_t byte, int line);
    int addConstant(Value value);
    int disassembleInstruction(int offset);
    int count() const { return static_cast<int>(m_bytes->size()); }
    uint8_t byteAt(int index) const { return m_bytes->at(index); }
    int lineAt(int index) const { return m_lines->at(index); }
    void setByteAt(int index, uint8_t byte) { m_bytes->at(index) = byte; }
    PropertyInlineCache& propertyInlineCacheAt(size_t index) {
        return m_propertyInlineCaches->at(index);
    }
    const std::vector<PropertyInlineCache>& propertyInlineCaches() const {
        return *m_propertyInlineCaches;
    }
    CallInlineCache& callInlineCacheAt(size_t index) {
        return m_callInlineCaches->at(index);
    }
    const std::vector<CallInlineCache>& callInlineCaches() const {
        return *m_callInlineCaches;
    }

    // inlined methods
    uint8_t* getBytes() { return this->m_bytes->data(); }
    Value* getConstants() { return this->m_constants->data(); }
    const std::vector<Value>& getConstantsRange() const { return *m_constants; }
};
