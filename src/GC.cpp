#include "GC.hpp"

#include "Chunk.hpp"

void GC::markValue(const Value& value) {
    if (value.isFunction()) {
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

void GC::sweep() {
    GcObject** current = &m_objects;
    while (*current != nullptr) {
        GcObject* obj = *current;
        if (!obj->marked) {
            *current = obj->next;
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
        delete current;
        current = next;
    }
}

void GC::freeObject(GcObject* obj) {
    if (m_bytesAllocated >= obj->gcSize) {
        m_bytesAllocated -= obj->gcSize;
    } else {
        m_bytesAllocated = 0;
    }
    delete obj;
}
