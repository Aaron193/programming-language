#pragma once

#include <cstddef>

class GC;

struct GcObject {
    bool marked = false;
    GcObject* next = nullptr;
    size_t gcSize = 0;

    virtual void trace(GC& gc) = 0;
    virtual ~GcObject() = default;
};
