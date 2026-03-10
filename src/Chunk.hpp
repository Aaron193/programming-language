#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "GcObject.hpp"
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

    void trace(GC& gc) override;
};

struct BoundMethodObject : GcObject {
    InstanceObject* receiver = nullptr;
    ClosureObject* method = nullptr;

    void trace(GC& gc) override;
};

enum class NativeFunctionId : uint8_t {
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
    UNKNOWN,
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
    NativeFunctionId id = NativeFunctionId::UNKNOWN;

    void trace(GC& gc) override;
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
    GET_PROPERTY,
    SET_PROPERTY,
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
    ITER_NEXT,
    IMPORT_MODULE,
    EXPORT_NAME,
};

enum ValueType {
    NUMBER_VALUE,
    BOOL_VALUE,
    NIL_VALUE,
    STRING_VALUE,
};

struct Value {
    using Storage =
        std::variant<double, int64_t, uint64_t, bool, std::monostate,
                     std::string, FunctionObject*, ClassObject*,
                     InstanceObject*, BoundMethodObject*,
                     NativeFunctionObject*, NativeBoundMethodObject*,
                     ClosureObject*, ArrayObject*, DictObject*, SetObject*,
                     IteratorObject*, ModuleObject*>;

    static constexpr size_t NUMBER_INDEX = 0;
    static constexpr size_t SIGNED_INT_INDEX = 1;
    static constexpr size_t UNSIGNED_INT_INDEX = 2;
    static constexpr size_t BOOL_INDEX = 3;
    static constexpr size_t NIL_INDEX = 4;
    static constexpr size_t STRING_INDEX = 5;
    static constexpr size_t FUNCTION_INDEX = 6;
    static constexpr size_t CLASS_INDEX = 7;
    static constexpr size_t INSTANCE_INDEX = 8;
    static constexpr size_t BOUND_METHOD_INDEX = 9;
    static constexpr size_t NATIVE_INDEX = 10;
    static constexpr size_t NATIVE_BOUND_INDEX = 11;
    static constexpr size_t CLOSURE_INDEX = 12;
    static constexpr size_t ARRAY_INDEX = 13;
    static constexpr size_t DICT_INDEX = 14;
    static constexpr size_t SET_INDEX = 15;
    static constexpr size_t ITERATOR_INDEX = 16;
    static constexpr size_t MODULE_INDEX = 17;

    Storage data;

    Value() : data(std::monostate{}) {}
    Value(double value) : data(value) {}
    Value(int64_t value) : data(value) {}
    Value(uint64_t value) : data(value) {}
    Value(bool value) : data(value) {}
    Value(const std::string& value) : data(value) {}
    Value(const char* value) : data(std::string(value)) {}
    Value(FunctionObject* value) : data(value) {}
    Value(ClassObject* value) : data(value) {}
    Value(InstanceObject* value) : data(value) {}
    Value(BoundMethodObject* value) : data(value) {}
    Value(NativeFunctionObject* value) : data(value) {}
    Value(NativeBoundMethodObject* value) : data(value) {}
    Value(ClosureObject* value) : data(value) {}
    Value(ArrayObject* value) : data(value) {}
    Value(DictObject* value) : data(value) {}
    Value(SetObject* value) : data(value) {}
    Value(IteratorObject* value) : data(value) {}
    Value(ModuleObject* value) : data(value) {}

    bool isNumber() const { return data.index() == NUMBER_INDEX; }
    bool isSignedInt() const { return data.index() == SIGNED_INT_INDEX; }
    bool isUnsignedInt() const { return data.index() == UNSIGNED_INT_INDEX; }
    bool isAnyNumeric() const {
        return isNumber() || isSignedInt() || isUnsignedInt();
    }
    bool isBool() const { return data.index() == BOOL_INDEX; }
    bool isNil() const { return data.index() == NIL_INDEX; }
    bool isString() const { return data.index() == STRING_INDEX; }
    bool isFunction() const { return data.index() == FUNCTION_INDEX; }
    bool isClass() const { return data.index() == CLASS_INDEX; }
    bool isInstance() const { return data.index() == INSTANCE_INDEX; }
    bool isBoundMethod() const { return data.index() == BOUND_METHOD_INDEX; }
    bool isNative() const { return data.index() == NATIVE_INDEX; }
    bool isNativeBound() const { return data.index() == NATIVE_BOUND_INDEX; }
    bool isClosure() const { return data.index() == CLOSURE_INDEX; }
    bool isArray() const { return data.index() == ARRAY_INDEX; }
    bool isDict() const { return data.index() == DICT_INDEX; }
    bool isSet() const { return data.index() == SET_INDEX; }
    bool isIterator() const { return data.index() == ITERATOR_INDEX; }
    bool isModule() const { return data.index() == MODULE_INDEX; }

    double asNumber() const { return std::get<double>(data); }
    int64_t asSignedInt() const { return std::get<int64_t>(data); }
    uint64_t asUnsignedInt() const { return std::get<uint64_t>(data); }
    bool asBool() const { return std::get<bool>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    FunctionObject* asFunction() const {
        return std::get<FunctionObject*>(data);
    }
    ClassObject* asClass() const { return std::get<ClassObject*>(data); }
    InstanceObject* asInstance() const {
        return std::get<InstanceObject*>(data);
    }
    BoundMethodObject* asBoundMethod() const {
        return std::get<BoundMethodObject*>(data);
    }
    NativeFunctionObject* asNative() const {
        return std::get<NativeFunctionObject*>(data);
    }
    NativeBoundMethodObject* asNativeBound() const {
        return std::get<NativeBoundMethodObject*>(data);
    }
    ClosureObject* asClosure() const { return std::get<ClosureObject*>(data); }
    ArrayObject* asArray() const { return std::get<ArrayObject*>(data); }
    DictObject* asDict() const { return std::get<DictObject*>(data); }
    SetObject* asSet() const { return std::get<SetObject*>(data); }
    IteratorObject* asIterator() const {
        return std::get<IteratorObject*>(data);
    }
    ModuleObject* asModule() const { return std::get<ModuleObject*>(data); }
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
    switch (value.data.index()) {
        case Value::FUNCTION_INDEX:
            return reinterpret_cast<uintptr_t>(value.asFunction());
        case Value::CLASS_INDEX:
            return reinterpret_cast<uintptr_t>(value.asClass());
        case Value::INSTANCE_INDEX:
            return reinterpret_cast<uintptr_t>(value.asInstance());
        case Value::BOUND_METHOD_INDEX:
            return reinterpret_cast<uintptr_t>(value.asBoundMethod());
        case Value::NATIVE_INDEX:
            return reinterpret_cast<uintptr_t>(value.asNative());
        case Value::NATIVE_BOUND_INDEX:
            return reinterpret_cast<uintptr_t>(value.asNativeBound());
        case Value::CLOSURE_INDEX:
            return reinterpret_cast<uintptr_t>(value.asClosure());
        case Value::ARRAY_INDEX:
            return reinterpret_cast<uintptr_t>(value.asArray());
        case Value::DICT_INDEX:
            return reinterpret_cast<uintptr_t>(value.asDict());
        case Value::SET_INDEX:
            return reinterpret_cast<uintptr_t>(value.asSet());
        case Value::ITERATOR_INDEX:
            return reinterpret_cast<uintptr_t>(value.asIterator());
        case Value::MODULE_INDEX:
            return reinterpret_cast<uintptr_t>(value.asModule());
        default:
            return 0;
    }
}

inline bool valueSortLess(const Value& lhs, const Value& rhs) {
    if (lhs.data.index() != rhs.data.index()) {
        return lhs.data.index() < rhs.data.index();
    }

    switch (lhs.data.index()) {
        case Value::NUMBER_INDEX:
            return normalizeDoubleBits(lhs.asNumber()) <
                   normalizeDoubleBits(rhs.asNumber());
        case Value::SIGNED_INT_INDEX:
            return lhs.asSignedInt() < rhs.asSignedInt();
        case Value::UNSIGNED_INT_INDEX:
            return lhs.asUnsignedInt() < rhs.asUnsignedInt();
        case Value::BOOL_INDEX:
            return lhs.asBool() < rhs.asBool();
        case Value::NIL_INDEX:
            return false;
        case Value::STRING_INDEX:
            return lhs.asString() < rhs.asString();
        default:
            return valuePointerBits(lhs) < valuePointerBits(rhs);
    }
}

struct ValueHash {
    size_t operator()(const Value& value) const noexcept {
        const size_t indexHash = std::hash<size_t>{}(value.data.index());
        size_t payloadHash = 0;

        switch (value.data.index()) {
            case Value::NUMBER_INDEX:
                payloadHash = std::hash<uint64_t>{}(
                    normalizeDoubleBits(value.asNumber()));
                break;
            case Value::SIGNED_INT_INDEX:
                payloadHash = std::hash<int64_t>{}(value.asSignedInt());
                break;
            case Value::UNSIGNED_INT_INDEX:
                payloadHash = std::hash<uint64_t>{}(value.asUnsignedInt());
                break;
            case Value::BOOL_INDEX:
                payloadHash = std::hash<bool>{}(value.asBool());
                break;
            case Value::NIL_INDEX:
                payloadHash = 0;
                break;
            case Value::STRING_INDEX:
                payloadHash = std::hash<std::string>{}(value.asString());
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
        return lhs.data == rhs.data;
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
    std::unordered_map<std::string, Value> fields;

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

    void trace(GC& gc) override;
};

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
    if (lhs.data.index() != rhs.data.index()) {
        return false;
    }
    return lhs.data == rhs.data;
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
        std::vector<Value> keys;
        keys.reserve(dict->map.size());
        for (const auto& entry : dict->map) {
            keys.push_back(entry.first);
        }
        std::sort(keys.begin(), keys.end(), valueSortLess);

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
    } else {
        stream << value.asString();
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

    // inlined methods
    uint8_t* getBytes() { return this->m_bytes->data(); }
    Value* getConstants() { return this->m_constants->data(); }
    const std::vector<Value>& getConstantsRange() const { return *m_constants; }
};
