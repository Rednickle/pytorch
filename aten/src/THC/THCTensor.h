#ifndef THC_TENSOR_INC
#define THC_TENSOR_INC

#include "THTensor.h"
#include "THCStorage.h"
#include "THCGeneral.h"

#define THCTensor_(NAME)   TH_CONCAT_4(TH,CReal,Tensor_,NAME)

#define THC_DESC_BUFF_LEN 64

typedef struct THC_CLASS THCDescBuff
{
    char str[THC_DESC_BUFF_LEN];
} THCDescBuff;

#include "generic/THCTensor.h"
#include "THCGenerateAllTypes.h"

// This exists to have a data-type independent way of freeing (necessary for THPPointer).
TH_API void THCTensor_free(THCState *state, THCTensor *self);

#endif
