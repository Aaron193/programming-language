#pragma once
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
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

struct NativeFunctionObject : GcObject {
    std::string name;
    int arity;

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
    std::variant<double, int64_t, uint64_t, bool, std::monostate, std::string,
                 FunctionObject*, ClassObject*, InstanceObject*,
                 BoundMethodObject*, NativeFunctionObject*,
                 NativeBoundMethodObject*, ClosureObject*, ArrayObject*,
                 DictObject*, SetObject*, IteratorObject*, ModuleObject*>
        data;

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

    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isSignedInt() const { return std::holds_alternative<int64_t>(data); }
    bool isUnsignedInt() const {
        return std::holds_alternative<uint64_t>(data);
    }
    bool isAnyNumeric() const {
        return isNumber() || isSignedInt() || isUnsignedInt();
    }
    bool isBool() const { return std::holds_alternative<bool>(data); }
    bool isNil() const { return std::holds_alternative<std::monostate>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isFunction() const {
        return std::holds_alternative<FunctionObject*>(data);
    }
    bool isClass() const { return std::holds_alternative<ClassObject*>(data); }
    bool isInstance() const {
        return std::holds_alternative<InstanceObject*>(data);
    }
    bool isBoundMethod() const {
        return std::holds_alternative<BoundMethodObject*>(data);
    }
    bool isNative() const {
        return std::holds_alternative<NativeFunctionObject*>(data);
    }
    bool isNativeBound() const {
        return std::holds_alternative<NativeBoundMethodObject*>(data);
    }
    bool isClosure() const {
        return std::holds_alternative<ClosureObject*>(data);
    }
    bool isArray() const { return std::holds_alternative<ArrayObject*>(data); }
    bool isDict() const { return std::holds_alternative<DictObject*>(data); }
    bool isSet() const { return std::holds_alternative<SetObject*>(data); }
    bool isIterator() const {
        return std::holds_alternative<IteratorObject*>(data);
    }
    bool isModule() const {
        return std::holds_alternative<ModuleObject*>(data);
    }

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

struct UpvalueObject : GcObject {
    size_t stackIndex = 0;
    bool isClosed = false;
    Value closed;

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

    void trace(GC& gc) override;
};

struct DictObject : GcObject {
    TypeRef keyType = TypeInfo::makeAny();
    TypeRef valueType = TypeInfo::makeAny();
    std::unordered_map<std::string, Value> map;

    void trace(GC& gc) override;
};

struct SetObject : GcObject {
    TypeRef elementType = TypeInfo::makeAny();
    std::vector<Value> elements;

    void trace(GC& gc) override;
};

struct IteratorObject : GcObject {
    enum Kind { ARRAY_ITER, DICT_ITER, SET_ITER } kind = ARRAY_ITER;

    ArrayObject* array = nullptr;
    DictObject* dict = nullptr;
    SetObject* set = nullptr;
    std::vector<std::string> dictKeys;
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
    Value receiver;

    void trace(GC& gc) override;
};

inline bool operator==(const Value& lhs, const Value& rhs) {
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
        std::vector<std::string> keys;
        keys.reserve(dict->map.size());
        for (const auto& entry : dict->map) {
            keys.push_back(entry.first);
        }
        std::sort(keys.begin(), keys.end());

        stream << "{";
        for (size_t index = 0; index < keys.size(); ++index) {
            if (index > 0) {
                stream << ", ";
            }

            const std::string& key = keys[index];
            stream << '"' << key << '"' << ": ";
            printValueInternal(stream, dict->map.at(key), active, depth + 1);
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
