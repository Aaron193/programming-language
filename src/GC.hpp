#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "GcObject.hpp"

struct Value;

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

    size_t bytesAllocated() const { return m_bytesAllocated; }

    ~GC();

   private:
    GcObject* m_objects = nullptr;
    size_t m_bytesAllocated = 0;
    std::vector<GcObject*> m_grayStack;

    void freeObject(GcObject* obj);
};
