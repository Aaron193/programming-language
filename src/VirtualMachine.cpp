#include "VirtualMachine.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static bool isFalsey(const Value& value) {
    if (value.isNil()) return true;
    if (value.isBool()) return !value.asBool();
    return false;
}

static bool isNumberPair(const Value& lhs, const Value& rhs) {
    return lhs.isNumber() && rhs.isNumber();
}

static std::string valueToString(const Value& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

static std::string valueTypeName(const Value& value) {
    if (value.isNumber()) return "number";
    if (value.isBool()) return "bool";
    if (value.isNil()) return "null";
    if (value.isString()) return "string";
    if (value.isArray()) return "array";
    if (value.isDict()) return "dict";
    if (value.isSet()) return "set";
    if (value.isClass()) return "class";
    if (value.isInstance()) return "instance";
    if (value.isNative()) return "native";
    if (value.isNativeBound()) return "native_bound_method";
    if (value.isBoundMethod()) return "bound_method";
    if (value.isClosure()) return "closure";
    if (value.isFunction()) return "function";
    return "unknown";
}

static bool toDictKey(const Value& value, std::string& key) {
    if (value.isString()) {
        key = value.asString();
        return true;
    }

    if (value.isNumber()) {
        std::ostringstream stream;
        stream << value.asNumber();
        key = stream.str();
        return true;
    }

    return false;
}

static bool toArrayIndex(const Value& value, size_t& index) {
    if (!value.isNumber()) {
        return false;
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

static std::shared_ptr<ClosureObject> findMethodClosure(
    std::shared_ptr<ClassObject> klass, const std::string& name) {
    std::shared_ptr<ClassObject> current = klass;
    while (current) {
        auto it = current->methods.find(name);
        if (it != current->methods.end()) {
            return it->second;
        }
        current = current->superclass;
    }

    return nullptr;
}

std::shared_ptr<UpvalueObject> VirtualMachine::captureUpvalue(
    size_t stackIndex) {
    for (const auto& upvalue : m_openUpvalues) {
        if (!upvalue->isClosed && upvalue->stackIndex == stackIndex) {
            return upvalue;
        }
    }

    auto upvalue = std::make_shared<UpvalueObject>();
    upvalue->stackIndex = stackIndex;
    upvalue->isClosed = false;
    m_openUpvalues.push_back(upvalue);
    return upvalue;
}

void VirtualMachine::closeUpvalues(size_t fromStackIndex) {
    for (const auto& upvalue : m_openUpvalues) {
        if (!upvalue->isClosed && upvalue->stackIndex >= fromStackIndex) {
            upvalue->closed = m_stack.getAt(upvalue->stackIndex);
            upvalue->isClosed = true;
        }
    }
}

void VirtualMachine::printStackTrace() {
    std::cerr << "[trace][runtime] stack:" << std::endl;

    for (int index = static_cast<int>(m_frames.size()) - 1; index >= 0;
         --index) {
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

Status VirtualMachine::callClosure(std::shared_ptr<ClosureObject> closure,
                                   uint8_t argumentCount,
                                   std::shared_ptr<InstanceObject> receiver) {
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

    m_frames.push_back(CallFrame{function->chunk.get(),
                                 function->chunk->getBytes(), calleeIndex + 1,
                                 calleeIndex, receiver, closure});
    return Status::OK;
}

Status VirtualMachine::run(bool printReturnValue, Value& returnValue) {
    while (true) {
        CallFrame& frame = currentFrame();

        if (m_traceEnabled) {
            m_stack.print();
            frame.chunk->disassembleInstruction(
                static_cast<int>(frame.ip - frame.chunk->getBytes()));
        }

        uint8_t instruction;
        switch (instruction = readByte()) {
            case OpCode::RETURN: {
                Value result = m_stack.pop();
                CallFrame finishedFrame = currentFrame();
                closeUpvalues(finishedFrame.slotBase);
                m_frames.pop_back();

                if (m_frames.empty()) {
                    returnValue = result;
                    if (printReturnValue) {
                        std::cout << "Return constant: " << result << std::endl;
                    }
                    return Status::OK;
                }

                m_stack.popN(m_stack.size() - finishedFrame.calleeIndex);
                m_stack.push(result);
                break;
            }
            case OpCode::CONSTANT: {
                Value val = readConstant();
                m_stack.push(val);
                break;
            }
            case OpCode::NIL: {
                m_stack.push(Value());
                break;
            }
            case OpCode::TRUE_LITERAL: {
                m_stack.push(Value(true));
                break;
            }
            case OpCode::FALSE_LITERAL: {
                m_stack.push(Value(false));
                break;
            }
            case OpCode::NEGATE: {
                Value value = m_stack.pop();
                if (!value.isNumber()) {
                    return runtimeError(
                        "Operand must be a number for unary '-'.");
                }

                m_stack.push(Value(-value.asNumber()));
                break;
            }
            case OpCode::NOT: {
                Value value = m_stack.pop();
                m_stack.push(Value(isFalsey(value)));
                break;
            }
            case OpCode::EQUAL_OP: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(Value(a == b));
                break;
            }
            case OpCode::NOT_EQUAL_OP: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                m_stack.push(Value(a != b));
                break;
            }
            case OpCode::ADD: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();

                if (a.isNumber() && b.isNumber()) {
                    m_stack.push(Value(a.asNumber() + b.asNumber()));
                    break;
                }

                if (a.isString() && b.isString()) {
                    m_stack.push(Value(a.asString() + b.asString()));
                    break;
                }

                return runtimeError(
                    "Operands must be two numbers or two strings for '+'.");
            }
            case OpCode::SUB: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '-'.");
                }

                m_stack.push(Value(a.asNumber() - b.asNumber()));
                break;
            }
            case OpCode::MULT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '*'.");
                }

                m_stack.push(Value(a.asNumber() * b.asNumber()));
                break;
            }
            case OpCode::DIV: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '/'.");
                }

                m_stack.push(Value(a.asNumber() / b.asNumber()));
                break;
            }
            case OpCode::GREATER_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '>'.");
                }

                m_stack.push(Value(a.asNumber() > b.asNumber()));
                break;
            }
            case OpCode::LESS_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '<'.");
                }

                m_stack.push(Value(a.asNumber() < b.asNumber()));
                break;
            }
            case OpCode::GREATER_EQUAL_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '>='.");
                }

                m_stack.push(Value(a.asNumber() >= b.asNumber()));
                break;
            }
            case OpCode::LESS_EQUAL_THAN: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '<='.");
                }

                m_stack.push(Value(a.asNumber() <= b.asNumber()));
                break;
            }
            case OpCode::POP: {
                m_stack.pop();
                break;
            }
            case OpCode::PRINT_OP: {
                Value value = m_stack.pop();
                std::cout << value << std::endl;
                break;
            }
            case OpCode::DEFINE_GLOBAL: {
                std::string name = readNameConstant();
                Value value = m_stack.pop();
                m_globals[name] = value;
                break;
            }
            case OpCode::GET_GLOBAL: {
                std::string name = readNameConstant();
                auto it = m_globals.find(name);
                if (it == m_globals.end()) {
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                m_stack.push(it->second);
                break;
            }
            case OpCode::SET_GLOBAL: {
                std::string name = readNameConstant();
                auto it = m_globals.find(name);
                if (it == m_globals.end()) {
                    return runtimeError("Undefined variable '" + name + "'.");
                }

                it->second = m_stack.peek(0);
                break;
            }
            case OpCode::GET_LOCAL: {
                uint8_t slot = readByte();
                m_stack.push(m_stack.getAt(currentFrame().slotBase + slot));
                break;
            }
            case OpCode::SET_LOCAL: {
                uint8_t slot = readByte();
                m_stack.setAt(currentFrame().slotBase + slot, m_stack.peek(0));
                break;
            }
            case OpCode::GET_UPVALUE: {
                uint8_t slot = readByte();
                auto upvalue = currentFrame().closure->upvalues[slot];
                if (upvalue->isClosed) {
                    m_stack.push(upvalue->closed);
                } else {
                    m_stack.push(m_stack.getAt(upvalue->stackIndex));
                }
                break;
            }
            case OpCode::SET_UPVALUE: {
                uint8_t slot = readByte();
                auto upvalue = currentFrame().closure->upvalues[slot];
                if (upvalue->isClosed) {
                    upvalue->closed = m_stack.peek(0);
                } else {
                    m_stack.setAt(upvalue->stackIndex, m_stack.peek(0));
                }
                break;
            }
            case OpCode::CLASS_OP: {
                std::string name = readNameConstant();
                auto klass = std::make_shared<ClassObject>();
                klass->name = name;
                m_stack.push(Value(klass));
                break;
            }
            case OpCode::INHERIT: {
                Value superclassValue = m_stack.pop();
                Value subclassValue = m_stack.peek(0);

                if (!superclassValue.isClass() || !subclassValue.isClass()) {
                    return runtimeError("Inheritance requires classes.");
                }

                auto superclass = superclassValue.asClass();
                auto subclass = subclassValue.asClass();
                subclass->superclass = superclass;
                for (const auto& method : superclass->methods) {
                    if (subclass->methods.find(method.first) ==
                        subclass->methods.end()) {
                        subclass->methods[method.first] = method.second;
                    }
                }
                break;
            }
            case OpCode::METHOD: {
                std::string name = readNameConstant();
                Value method = m_stack.peek(0);
                Value klass = m_stack.peek(1);

                if (!klass.isClass() || !method.isClosure()) {
                    return runtimeError("Invalid method declaration.");
                }

                klass.asClass()->methods[name] = method.asClosure();
                m_stack.pop();
                break;
            }
            case OpCode::GET_THIS: {
                auto receiver = currentFrame().receiver;
                if (!receiver) {
                    return runtimeError(
                        "Cannot use 'this' outside of a method.");
                }

                m_stack.push(Value(receiver));
                break;
            }
            case OpCode::GET_SUPER: {
                std::string name = readNameConstant();
                auto receiver = currentFrame().receiver;
                if (!receiver || !receiver->klass ||
                    !receiver->klass->superclass) {
                    return runtimeError("Invalid super lookup.");
                }

                auto method =
                    findMethodClosure(receiver->klass->superclass, name);
                if (!method) {
                    return runtimeError("Undefined superclass method '" + name +
                                        "'.");
                }

                auto bound = std::make_shared<BoundMethodObject>();
                bound->receiver = receiver;
                bound->method = method;
                m_stack.push(Value(bound));
                break;
            }
            case OpCode::GET_PROPERTY: {
                std::string name = readNameConstant();
                Value receiver = m_stack.peek(0);

                if (receiver.isArray() || receiver.isDict() ||
                    receiver.isSet()) {
                    auto bound = std::make_shared<NativeBoundMethodObject>();
                    bound->name = name;
                    bound->receiver = receiver;
                    m_stack.pop();
                    m_stack.push(Value(bound));
                    break;
                }

                if (!receiver.isInstance()) {
                    return runtimeError("Only instances have properties.");
                }

                auto instance = receiver.asInstance();
                auto it = instance->fields.find(name);
                if (it != instance->fields.end()) {
                    m_stack.pop();
                    m_stack.push(it->second);
                    break;
                }

                auto method = findMethodClosure(instance->klass, name);
                if (!method) {
                    return runtimeError("Undefined property '" + name + "'.");
                }

                auto bound = std::make_shared<BoundMethodObject>();
                bound->receiver = instance;
                bound->method = method;

                m_stack.pop();
                m_stack.push(Value(bound));
                break;
            }
            case OpCode::SET_PROPERTY: {
                std::string name = readNameConstant();
                Value value = m_stack.peek(0);
                Value receiver = m_stack.peek(1);
                if (!receiver.isInstance()) {
                    return runtimeError("Only instances have fields.");
                }

                auto instance = receiver.asInstance();
                instance->fields[name] = value;

                m_stack.pop();
                m_stack.pop();
                m_stack.push(value);
                break;
            }
            case OpCode::CALL: {
                uint8_t argumentCount = readByte();
                Value callee = m_stack.peek(argumentCount);
                size_t calleeIndex =
                    m_stack.size() - static_cast<size_t>(argumentCount) - 1;

                if (callee.isClass()) {
                    if (argumentCount != 0) {
                        return runtimeError(
                            "Class '" + callee.asClass()->name +
                            "' expected 0 arguments but got " +
                            std::to_string(static_cast<int>(argumentCount)) +
                            ".");
                    }

                    auto instance = std::make_shared<InstanceObject>();
                    instance->klass = callee.asClass();
                    m_stack.setAt(calleeIndex, Value(instance));
                    break;
                }

                if (callee.isNative()) {
                    auto native = callee.asNative();
                    if (native->arity >= 0 && native->arity != argumentCount) {
                        return runtimeError(
                            "Native function '" + native->name + "' expected " +
                            std::to_string(native->arity) +
                            " arguments but got " +
                            std::to_string(static_cast<int>(argumentCount)) +
                            ".");
                    }

                    std::vector<Value> args;
                    args.reserve(argumentCount);
                    for (uint8_t i = 0; i < argumentCount; ++i) {
                        args.push_back(m_stack.peek(
                            static_cast<size_t>(argumentCount - 1 - i)));
                    }

                    Value result;
                    if (native->name == "clock") {
                        auto now = std::chrono::duration<double>(
                                       std::chrono::system_clock::now()
                                           .time_since_epoch())
                                       .count();
                        result = Value(now);
                    } else if (native->name == "sqrt") {
                        const Value& arg = args[0];
                        if (!arg.isNumber()) {
                            return runtimeError(
                                "Native function 'sqrt' expects a number.");
                        }

                        double number = arg.asNumber();
                        if (number < 0.0) {
                            return runtimeError(
                                "Native function 'sqrt' cannot take negative "
                                "numbers.");
                        }

                        result = Value(std::sqrt(number));
                    } else if (native->name == "len") {
                        const Value& arg = args[0];
                        if (!arg.isString()) {
                            return runtimeError(
                                "Native function 'len' expects a string.");
                        }

                        result =
                            Value(static_cast<double>(arg.asString().length()));
                    } else if (native->name == "type") {
                        result = Value(valueTypeName(args[0]));
                    } else if (native->name == "str") {
                        result = Value(valueToString(args[0]));
                    } else if (native->name == "num") {
                        const Value& arg = args[0];
                        if (arg.isNumber()) {
                            result = arg;
                        } else if (arg.isString()) {
                            const std::string& text = arg.asString();
                            size_t parseIndex = 0;
                            try {
                                double number = std::stod(text, &parseIndex);
                                if (parseIndex != text.size()) {
                                    return runtimeError(
                                        "Native function 'num' expects a "
                                        "numeric string.");
                                }
                                result = Value(number);
                            } catch (const std::exception&) {
                                return runtimeError(
                                    "Native function 'num' expects a numeric "
                                    "string.");
                            }
                        } else {
                            return runtimeError(
                                "Native function 'num' expects a number or "
                                "string.");
                        }
                    } else if (native->name == "Set") {
                        auto set = std::make_shared<SetObject>();
                        for (const auto& arg : args) {
                            if (!containsValue(set->elements, arg)) {
                                set->elements.push_back(arg);
                            }
                        }

                        result = Value(set);
                    } else {
                        return runtimeError("Unknown native function '" +
                                            native->name + "'.");
                    }

                    m_stack.popN(m_stack.size() - calleeIndex);
                    m_stack.push(result);
                    break;
                }

                if (callee.isNativeBound()) {
                    auto bound = callee.asNativeBound();

                    std::vector<Value> args;
                    args.reserve(argumentCount);
                    for (uint8_t i = 0; i < argumentCount; ++i) {
                        args.push_back(m_stack.peek(
                            static_cast<size_t>(argumentCount - 1 - i)));
                    }

                    Value result;
                    const std::string& method = bound->name;
                    Value receiver = bound->receiver;

                    if (receiver.isArray()) {
                        auto array = receiver.asArray();

                        if (method == "push") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Array method 'push' expects 1 argument.");
                            }

                            array->elements.push_back(args[0]);
                            result = Value(
                                static_cast<double>(array->elements.size()));
                        } else if (method == "pop") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'pop' expects 0 arguments.");
                            }

                            if (array->elements.empty()) {
                                return runtimeError(
                                    "Array method 'pop' called on empty "
                                    "array.");
                            }

                            result = array->elements.back();
                            array->elements.pop_back();
                        } else if (method == "size") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Array method 'size' expects 0 arguments.");
                            }

                            result = Value(
                                static_cast<double>(array->elements.size()));
                        } else if (method == "has") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Array method 'has' expects 1 argument.");
                            }

                            result =
                                Value(containsValue(array->elements, args[0]));
                        } else if (method == "insert") {
                            if (argumentCount != 2) {
                                return runtimeError(
                                    "Array method 'insert' expects 2 "
                                    "arguments.");
                            }

                            size_t index = 0;
                            if (!toArrayIndex(args[0], index)) {
                                return runtimeError(
                                    "Array method 'insert' expects a "
                                    "non-negative integer index.");
                            }

                            if (index > array->elements.size()) {
                                return runtimeError(
                                    "Array method 'insert' index out of "
                                    "bounds.");
                            }

                            array->elements.insert(array->elements.begin() +
                                                       static_cast<long>(index),
                                                   args[1]);
                            result = Value(args[1]);
                        } else if (method == "remove") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Array method 'remove' expects 1 "
                                    "argument.");
                            }

                            size_t index = 0;
                            if (!toArrayIndex(args[0], index)) {
                                return runtimeError(
                                    "Array method 'remove' expects a "
                                    "non-negative integer index.");
                            }

                            if (index >= array->elements.size()) {
                                return runtimeError(
                                    "Array method 'remove' index out of "
                                    "bounds.");
                            }

                            result = array->elements[index];
                            array->elements.erase(array->elements.begin() +
                                                  static_cast<long>(index));
                        } else {
                            return runtimeError("Undefined array method '" +
                                                method + "'.");
                        }
                    } else if (receiver.isDict()) {
                        auto dict = receiver.asDict();

                        if (method == "get") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Dict method 'get' expects 1 argument.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            auto it = dict->map.find(key);
                            if (it == dict->map.end()) {
                                return runtimeError(
                                    "Dict method 'get' key not found.");
                            }

                            result = it->second;
                        } else if (method == "set") {
                            if (argumentCount != 2) {
                                return runtimeError(
                                    "Dict method 'set' expects 2 arguments.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            dict->map[key] = args[1];
                            result = args[1];
                        } else if (method == "has") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Dict method 'has' expects 1 argument.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            result =
                                Value(dict->map.find(key) != dict->map.end());
                        } else if (method == "keys") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'keys' expects 0 arguments.");
                            }

                            auto keys = std::make_shared<ArrayObject>();
                            for (const auto& entry : dict->map) {
                                keys->elements.push_back(Value(entry.first));
                            }
                            result = Value(keys);
                        } else if (method == "values") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'values' expects 0 "
                                    "arguments.");
                            }

                            auto values = std::make_shared<ArrayObject>();
                            for (const auto& entry : dict->map) {
                                values->elements.push_back(entry.second);
                            }
                            result = Value(values);
                        } else if (method == "size") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Dict method 'size' expects 0 arguments.");
                            }

                            result =
                                Value(static_cast<double>(dict->map.size()));
                        } else if (method == "remove") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Dict method 'remove' expects 1 argument.");
                            }

                            std::string key;
                            if (!toDictKey(args[0], key)) {
                                return runtimeError(
                                    "Dict keys must be strings or numbers.");
                            }

                            auto it = dict->map.find(key);
                            if (it == dict->map.end()) {
                                return runtimeError(
                                    "Dict method 'remove' key not found.");
                            }

                            result = it->second;
                            dict->map.erase(it);
                        } else {
                            return runtimeError("Undefined dict method '" +
                                                method + "'.");
                        }
                    } else if (receiver.isSet()) {
                        auto set = receiver.asSet();

                        if (method == "add") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'add' expects 1 argument.");
                            }

                            bool inserted = false;
                            if (!containsValue(set->elements, args[0])) {
                                set->elements.push_back(args[0]);
                                inserted = true;
                            }
                            result = Value(inserted);
                        } else if (method == "has") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'has' expects 1 argument.");
                            }

                            result =
                                Value(containsValue(set->elements, args[0]));
                        } else if (method == "remove") {
                            if (argumentCount != 1) {
                                return runtimeError(
                                    "Set method 'remove' expects 1 argument.");
                            }

                            bool removed = false;
                            for (size_t i = 0; i < set->elements.size(); ++i) {
                                if (set->elements[i] == args[0]) {
                                    set->elements.erase(set->elements.begin() +
                                                        static_cast<long>(i));
                                    removed = true;
                                    break;
                                }
                            }
                            result = Value(removed);
                        } else if (method == "size") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Set method 'size' expects 0 arguments.");
                            }

                            result = Value(
                                static_cast<double>(set->elements.size()));
                        } else if (method == "toArray") {
                            if (argumentCount != 0) {
                                return runtimeError(
                                    "Set method 'toArray' expects 0 "
                                    "arguments.");
                            }

                            auto array = std::make_shared<ArrayObject>();
                            array->elements = set->elements;
                            result = Value(array);
                        } else {
                            return runtimeError("Undefined set method '" +
                                                method + "'.");
                        }
                    } else {
                        return runtimeError(
                            "Native bound method receiver is invalid.");
                    }

                    m_stack.popN(m_stack.size() - calleeIndex);
                    m_stack.push(result);
                    break;
                }

                if (callee.isBoundMethod()) {
                    auto bound = callee.asBoundMethod();
                    Status status = callClosure(bound->method, argumentCount,
                                                bound->receiver);
                    if (status != Status::OK) {
                        return status;
                    }
                    break;
                }

                if (callee.isClosure()) {
                    Status status =
                        callClosure(callee.asClosure(), argumentCount);
                    if (status != Status::OK) {
                        return status;
                    }
                    break;
                }

                if (callee.isFunction()) {
                    auto closure = std::make_shared<ClosureObject>();
                    closure->function = callee.asFunction();
                    closure->upvalues = {};
                    Status status = callClosure(closure, argumentCount);
                    if (status != Status::OK) {
                        return status;
                    }
                    break;
                }

                if (!callee.isFunction()) {
                    return runtimeError(
                        "Can only call functions, classes, methods, and "
                        "natives.");
                }
                break;
            }
            case OpCode::CLOSURE: {
                Value constant = readConstant();
                if (!constant.isFunction()) {
                    return runtimeError("CLOSURE expects a function constant.");
                }

                auto function = constant.asFunction();
                auto closure = std::make_shared<ClosureObject>();
                closure->function = function;
                closure->upvalues.reserve(function->upvalueCount);

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

                m_stack.push(Value(closure));
                break;
            }
            case OpCode::CLOSE_UPVALUE: {
                closeUpvalues(m_stack.size() - 1);
                m_stack.pop();
                break;
            }
            case OpCode::BUILD_ARRAY: {
                uint8_t count = readByte();
                auto array = std::make_shared<ArrayObject>();
                array->elements.resize(count);
                for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
                    array->elements[static_cast<size_t>(i)] = m_stack.pop();
                }

                m_stack.push(Value(array));
                break;
            }
            case OpCode::BUILD_DICT: {
                uint8_t pairCount = readByte();
                auto dict = std::make_shared<DictObject>();

                for (int i = 0; i < pairCount; ++i) {
                    Value value = m_stack.pop();
                    Value keyValue = m_stack.pop();
                    std::string key;
                    if (!toDictKey(keyValue, key)) {
                        return runtimeError(
                            "Dictionary keys must be strings or numbers.");
                    }

                    dict->map[key] = value;
                }

                m_stack.push(Value(dict));
                break;
            }
            case OpCode::GET_INDEX: {
                Value indexValue = m_stack.pop();
                Value container = m_stack.pop();

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

                    m_stack.push(array->elements[index]);
                    break;
                }

                if (container.isDict()) {
                    std::string key;
                    if (!toDictKey(indexValue, key)) {
                        return runtimeError(
                            "Dictionary keys must be strings or numbers.");
                    }

                    auto dict = container.asDict();
                    auto it = dict->map.find(key);
                    if (it == dict->map.end()) {
                        return runtimeError("Dictionary key not found.");
                    }

                    m_stack.push(it->second);
                    break;
                }

                if (container.isSet()) {
                    auto set = container.asSet();
                    m_stack.push(
                        Value(containsValue(set->elements, indexValue)));
                    break;
                }

                return runtimeError(
                    "Indexing is only supported on array, dict, and set.");
            }
            case OpCode::SET_INDEX: {
                Value value = m_stack.pop();
                Value indexValue = m_stack.pop();
                Value container = m_stack.pop();

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

                    array->elements[index] = value;
                    m_stack.push(value);
                    break;
                }

                if (container.isDict()) {
                    std::string key;
                    if (!toDictKey(indexValue, key)) {
                        return runtimeError(
                            "Dictionary keys must be strings or numbers.");
                    }

                    auto dict = container.asDict();
                    dict->map[key] = value;
                    m_stack.push(value);
                    break;
                }

                return runtimeError(
                    "Indexed assignment is only supported on array and dict.");
            }
            case OpCode::DUP: {
                m_stack.push(m_stack.peek(0));
                break;
            }
            case OpCode::DUP2: {
                Value second = m_stack.peek(1);
                Value top = m_stack.peek(0);
                m_stack.push(second);
                m_stack.push(top);
                break;
            }
            case OpCode::JUMP: {
                uint16_t offset = readShort();
                currentFrame().ip += offset;
                break;
            }
            case OpCode::JUMP_IF_FALSE: {
                uint16_t offset = readShort();
                Value condition = m_stack.peek(0);
                if (isFalsey(condition)) {
                    currentFrame().ip += offset;
                }
                break;
            }
            case OpCode::LOOP: {
                uint16_t offset = readShort();
                currentFrame().ip -= offset;
                break;
            }
            case OpCode::SHIFT_LEFT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '<<'.");
                }

                m_stack.push(Value(
                    static_cast<double>(static_cast<int>(a.asNumber())
                                        << static_cast<int>(b.asNumber()))));
                break;
            }
            case OpCode::SHIFT_RIGHT: {
                Value b = m_stack.pop();
                Value a = m_stack.pop();
                if (!isNumberPair(a, b)) {
                    return runtimeError("Operands must be numbers for '>>'.");
                }

                m_stack.push(
                    Value(static_cast<double>(static_cast<int>(a.asNumber()) >>
                                              static_cast<int>(b.asNumber()))));
                break;
            }
        }
    }
}

Status VirtualMachine::interpret(std::string_view source, bool printReturnValue,
                                 bool traceEnabled, bool disassembleEnabled) {
    Chunk chunk;
    m_stack.reset();
    m_frames.clear();
    m_openUpvalues.clear();
    m_traceEnabled = traceEnabled;
    m_disassembleEnabled = disassembleEnabled;

    if (!m_compiler.compile(source, chunk)) {
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

    auto clockFn = std::make_shared<NativeFunctionObject>();
    clockFn->name = "clock";
    clockFn->arity = 0;
    m_globals[clockFn->name] = Value(clockFn);

    auto defineNative = [&](const std::string& name, int arity) {
        auto nativeFn = std::make_shared<NativeFunctionObject>();
        nativeFn->name = name;
        nativeFn->arity = arity;
        m_globals[name] = Value(nativeFn);
    };
    defineNative("sqrt", 1);
    defineNative("len", 1);
    defineNative("type", 1);
    defineNative("str", 1);
    defineNative("num", 1);
    defineNative("Set", -1);

    m_frames.push_back(
        CallFrame{&chunk, chunk.getBytes(), 0, 0, nullptr, nullptr});

    Value returnValue;
    return run(printReturnValue, returnValue);
}
