#pragma once
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

class Chunk;
struct ClassObject;
struct InstanceObject;

struct FunctionObject {
    std::string name;
    std::vector<std::string> parameters;
    std::shared_ptr<Chunk> chunk;
};

struct ClassObject {
    std::string name;
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
    GREATER_THAN,
    LESS_THAN,
    GREATER_EQUAL_THAN,
    LESS_EQUAL_THAN,
    POP,
    PRINT_OP,
    DEFINE_GLOBAL,
    GET_GLOBAL,
    SET_GLOBAL,
    GET_LOCAL,
    SET_LOCAL,
    CLASS_OP,
    GET_PROPERTY,
    SET_PROPERTY,
    CALL,
    JUMP,
    JUMP_IF_FALSE,
    LOOP,
    SHIFT_LEFT,
    SHIFT_RIGHT,
};

enum ValueType {
    NUMBER_VALUE,
    BOOL_VALUE,
    NIL_VALUE,
    STRING_VALUE,
};

struct Value {
    std::variant<double, bool, std::monostate, std::string,
                 std::shared_ptr<FunctionObject>,
                 std::shared_ptr<ClassObject>,
                 std::shared_ptr<InstanceObject>>
        data;

    Value() : data(std::monostate{}) {}
    Value(double value) : data(value) {}
    Value(bool value) : data(value) {}
    Value(const std::string& value) : data(value) {}
    Value(const char* value) : data(std::string(value)) {}
    Value(std::shared_ptr<FunctionObject> value) : data(std::move(value)) {}
    Value(std::shared_ptr<ClassObject> value) : data(std::move(value)) {}
    Value(std::shared_ptr<InstanceObject> value) : data(std::move(value)) {}

    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isBool() const { return std::holds_alternative<bool>(data); }
    bool isNil() const { return std::holds_alternative<std::monostate>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isFunction() const {
        return std::holds_alternative<std::shared_ptr<FunctionObject>>(data);
    }
    bool isClass() const {
        return std::holds_alternative<std::shared_ptr<ClassObject>>(data);
    }
    bool isInstance() const {
        return std::holds_alternative<std::shared_ptr<InstanceObject>>(data);
    }

    double asNumber() const { return std::get<double>(data); }
    bool asBool() const { return std::get<bool>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    std::shared_ptr<FunctionObject> asFunction() const {
        return std::get<std::shared_ptr<FunctionObject>>(data);
    }
    std::shared_ptr<ClassObject> asClass() const {
        return std::get<std::shared_ptr<ClassObject>>(data);
    }
    std::shared_ptr<InstanceObject> asInstance() const {
        return std::get<std::shared_ptr<InstanceObject>>(data);
    }
};

struct InstanceObject {
    std::shared_ptr<ClassObject> klass;
    std::unordered_map<std::string, Value> fields;
};

inline bool operator==(const Value& lhs, const Value& rhs) {
    return lhs.data == rhs.data;
}

inline bool operator!=(const Value& lhs, const Value& rhs) {
    return !(lhs == rhs);
}

inline std::ostream& operator<<(std::ostream& stream, const Value& value) {
    if (value.isNumber()) {
        stream << value.asNumber();
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
    } else {
        stream << value.asString();
    }

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
    ~Chunk() = default;

    void write(uint8_t byte, int line);
    int addConstant(Value value);
    int disassembleInstruction(int offset);
    int count() const { return static_cast<int>(m_bytes->size()); }
    uint8_t byteAt(int index) const { return m_bytes->at(index); }
    void setByteAt(int index, uint8_t byte) { m_bytes->at(index) = byte; }

    // inlined methods
    uint8_t* getBytes() { return this->m_bytes->data(); }
    Value* getConstants() { return this->m_constants->data(); }
};
