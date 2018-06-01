#pragma once

// STOP!!! Thinking of including this header directly?  Please
// read Note [TH abstraction violation]

#include "THCTensor.h"
#include "THTensor.hpp"
#include "THCStorage.hpp"

#include <atomic>

typedef struct THCTensor
{
    int64_t *size;
    int64_t *stride;
    int nDimension;

    THCStorage *storage;
    ptrdiff_t storageOffset;
    std::atomic<int> refcount;

    char flag;

} THCTensor;
