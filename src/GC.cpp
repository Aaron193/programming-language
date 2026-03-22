#include "GC.hpp"

#include "Chunk.hpp"

#include <cstddef>
#include <functional>
#include <new>

void GC::markValue(const Value& value) {
    if (value.isString()) {
        markObject(value.asStringObject());
    } else if (value.isFunction()) {
        markObject(value.asFunction());
    } else if (value.isClass()) {
        markObject(value.asClass());
    } else if (value.isInstance()) {
        markObject(value.asInstance());
    } else if (value.isBoundMethod()) {
        markObject(value.asBoundMethod());
    } else if (value.isNative()) {
        markObject(value.asNative());
    } else if (value.isNativeBound()) {
        markObject(value.asNativeBound());
    } else if (value.isClosure()) {
        markObject(value.asClosure());
    } else if (value.isArray()) {
        markObject(value.asArray());
    } else if (value.isDict()) {
        markObject(value.asDict());
    } else if (value.isSet()) {
        markObject(value.asSet());
    } else if (value.isIterator()) {
        markObject(value.asIterator());
    } else if (value.isModule()) {
        markObject(value.asModule());
    } else if (value.isNativeHandle()) {
        markObject(value.asNativeHandle());
    }
}

void GC::markObject(GcObject* obj) {
    if (obj == nullptr || obj->marked) {
        return;
    }

    obj->marked = true;
    m_grayStack.push_back(obj);
}

void GC::drainGrayStack() {
    while (!m_grayStack.empty()) {
        GcObject* obj = m_grayStack.back();
        m_grayStack.pop_back();
        obj->trace(*this);
    }
}

StringObject* GC::makeString(std::string text) {
    auto* stringObject = allocateStringObject();
    stringObject->value = std::move(text);
    stringObject->hashValue = std::hash<std::string>{}(stringObject->value);
    stringObject->isInterned = false;
    return stringObject;
}

StringObject* GC::internString(std::string text) {
    auto existing = m_internedStrings.find(text);
    if (existing != m_internedStrings.end()) {
        return existing->second;
    }

    auto* stringObject = makeString(std::move(text));
    stringObject->isInterned = true;
    m_internedStrings.emplace(stringObject->value, stringObject);
    return stringObject;
}

void GC::sweep() {
    GcObject** current = &m_objects;
    while (*current != nullptr) {
        GcObject* obj = *current;
        if (!obj->marked) {
            *current = obj->next;
            if (auto* stringObject = dynamic_cast<StringObject*>(obj);
                stringObject != nullptr && stringObject->isInterned) {
                removeInternedString(stringObject);
            }
            freeObject(obj);
        } else {
            obj->marked = false;
            current = &obj->next;
        }
    }
}

GC::~GC() {
    GcObject* current = m_objects;
    while (current != nullptr) {
        GcObject* next = current->next;
        if (auto* stringObject = dynamic_cast<StringObject*>(current);
            stringObject != nullptr) {
            stringObject->~StringObject();
        } else {
            delete current;
        }
        current = next;
    }

    for (void* block : m_stringBlocks) {
        ::operator delete[](block,
                            std::align_val_t{alignof(StringObject)});
    }
}

void GC::freeObject(GcObject* obj) {
    if (m_bytesAllocated >= obj->gcSize) {
        m_bytesAllocated -= obj->gcSize;
    } else {
        m_bytesAllocated = 0;
    }

    if (auto* stringObject = dynamic_cast<StringObject*>(obj);
        stringObject != nullptr) {
        stringObject->~StringObject();
        stringObject->next = m_freeStringObjects;
        m_freeStringObjects = stringObject;
        return;
    }

    delete obj;
}

void GC::removeInternedString(const StringObject* obj) {
    if (obj == nullptr || !obj->isInterned) {
        return;
    }

    auto it = m_internedStrings.find(obj->value);
    if (it != m_internedStrings.end() && it->second == obj) {
        m_internedStrings.erase(it);
    }
}

StringObject* GC::allocateStringObject() {
    StringObject* stringObject = nullptr;
    if (m_freeStringObjects != nullptr) {
        stringObject = m_freeStringObjects;
        m_freeStringObjects = static_cast<StringObject*>(m_freeStringObjects->next);
        new (stringObject) StringObject();
    } else {
        if (m_nextStringSlot >= STRING_BLOCK_CAPACITY) {
            m_stringBlocks.push_back(::operator new[](
                sizeof(StringObject) * STRING_BLOCK_CAPACITY,
                std::align_val_t{alignof(StringObject)}));
            m_nextStringSlot = 0;
        }

        auto* block = static_cast<std::byte*>(m_stringBlocks.back());
        void* storage =
            block + (sizeof(StringObject) * m_nextStringSlot++);
        stringObject = new (storage) StringObject();
    }

    m_bytesAllocated += sizeof(StringObject);
    stringObject->gcSize = sizeof(StringObject);
    stringObject->next = m_objects;
    m_objects = stringObject;
    return stringObject;
}
