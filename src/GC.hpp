#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GcObject.hpp"

struct Value;
struct StringObject;

class GC {
   public:
    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
        static_assert(std::is_base_of<GcObject, T>::value,
                      "GC can only allocate GcObject types");

        m_bytesAllocated += sizeof(T);

        T* obj = new T(std::forward<Args>(args)...);
        obj->gcSize = sizeof(T);
        obj->next = m_objects;
        m_objects = obj;
        return obj;
    }

    void markValue(const Value& value);
    void markObject(GcObject* obj);
    void drainGrayStack();
    void sweep();
    StringObject* makeString(std::string text);
    StringObject* internString(std::string text);

    size_t bytesAllocated() const { return m_bytesAllocated; }

    ~GC();

   private:
    static constexpr size_t STRING_BLOCK_CAPACITY = 4096;

    GcObject* m_objects = nullptr;
    size_t m_bytesAllocated = 0;
    std::vector<GcObject*> m_grayStack;
    std::unordered_map<std::string, StringObject*> m_internedStrings;
    std::vector<void*> m_stringBlocks;
    size_t m_nextStringSlot = STRING_BLOCK_CAPACITY;
    StringObject* m_freeStringObjects = nullptr;

    void freeObject(GcObject* obj);
    void removeInternedString(const StringObject* obj);
    StringObject* allocateStringObject();
};
