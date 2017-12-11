#pragma once

#include <sstream>

namespace at {

/*
 * The basic strategy for apply is as follows:
 *
 * 1. Starting with the outermost index, loop until we reach a dimension where the
 * data is no longer contiguous, i.e. the stride at that dimension is not equal to
 * the size of the tensor defined by the outer dimensions. Let's call this outer
 * (contiguous) tensor A. Note that if the Tensor is contiguous, then A is equal
 * to the entire Tensor. Let's call the inner tensor B.
 *
 * 2. We loop through the indices in B, starting at its outermost dimension. For
 * example, if B is a 2x2 matrix, then we do:
 *
 * B[0][0]
 * B[0][1]
 * B[1][0]
 * B[1][1]
 *
 * We set the offset into the underlying storage as (storageOffset + stride_B * index_B),
 * i.e. basically we compute the offset into the storage as we would normally for a
 * Tensor. But because we are guaranteed the subsequent data is contiguous in memory, we
 * can simply loop for sizeof(A) iterations and perform the operation, without having to
 * follow the order described by the strides of A.
 *
 * 3. As an optimization, we merge dimensions of A that are contiguous in memory. For
 * example, if A is a 3x3x3x3 tensor narrowed from a 3x3x4x3 tensor, then the first two
 * dimensions can be merged for the purposes of APPLY, reducing the number of nested
 * loops.
 */

#define __ATH_TENSOR_APPLYX_PREAMBLE(TYPE, ATENSOR, DIM, ALLOW_CONTIGUOUS) \
  TYPE *ATENSOR##_data = NULL; \
  int64_t *ATENSOR##_counter = NULL, *ATENSOR##_sizes = NULL, *ATENSOR##_strides = NULL, *ATENSOR##_dimOffset = NULL; \
  int64_t ATENSOR##_stride = 0, ATENSOR##_size = 0, ATENSOR##_dim = 0, ATENSOR##_i; \
  int ATENSOR##_contiguous = ALLOW_CONTIGUOUS && DIM < 0; \
\
  if(ATENSOR.dim() == 0) \
    TH_TENSOR_APPLY_hasFinished = true; \
  else \
  { \
    ATENSOR##_data = ATENSOR.data<TYPE>(); \
    ATENSOR##_size = 1; \
    ATENSOR##_stride = 1; \
    for(ATENSOR##_i = ATENSOR.dim() - 1; ATENSOR##_i >= 0; ATENSOR##_i--) { \
      if(ATENSOR.sizes()[ATENSOR##_i] != 1) { \
        if(ATENSOR.strides()[ATENSOR##_i] == ATENSOR##_size && ATENSOR##_i != DIM) \
          ATENSOR##_size *= ATENSOR.sizes()[ATENSOR##_i]; \
        else{ \
          ATENSOR##_contiguous = 0; \
          break; \
        } \
      } \
    } \
    if (!ATENSOR##_contiguous) { \
      /* Find the dimension of contiguous sections */ \
      ATENSOR##_dim = 1; \
      for(ATENSOR##_i = ATENSOR.dim() - 2; ATENSOR##_i >= 0; ATENSOR##_i--) \
      { \
        if(ATENSOR.strides()[ATENSOR##_i] != ATENSOR.strides()[ATENSOR##_i+1] * ATENSOR.sizes()[ATENSOR##_i+1] || ATENSOR##_i == DIM || ATENSOR##_i+1 == DIM) \
          ATENSOR##_dim++; \
      } \
      /* Allocate an array of 3*dim elements, where dim is the number of contiguous sections */ \
      ATENSOR##_counter = new int64_t[3*ATENSOR##_dim]; \
      ATENSOR##_sizes = ATENSOR##_counter + ATENSOR##_dim; \
      ATENSOR##_strides = ATENSOR##_counter + 2*ATENSOR##_dim; \
      TH_TENSOR_dim_index = ATENSOR##_dim-1; \
      ATENSOR##_dimOffset = (DIM == ATENSOR.dim()-1) ? &ATENSOR##_i : &ATENSOR##_counter[DIM]; \
      ATENSOR##_sizes[TH_TENSOR_dim_index] = ATENSOR.sizes()[ATENSOR.dim()-1]; \
      ATENSOR##_strides[TH_TENSOR_dim_index] = ATENSOR.strides()[ATENSOR.dim()-1]; \
      /* ATENSOR##_counter tracks where we are in the storage. The offset into the */ \
      /* storage is given by storage_offset + (i * j), where i is the stride */ \
      /* vector and j is tensor_counter vector. This sets the starting position for the loop. */ \
      for(ATENSOR##_i = ATENSOR##_dim-1; ATENSOR##_i >= 0; --ATENSOR##_i) { \
        ATENSOR##_counter[ATENSOR##_i] = 0; \
      } \
      for(ATENSOR##_i = ATENSOR.dim()-2; ATENSOR##_i >= 0; --ATENSOR##_i) { \
        if (ATENSOR.strides()[ATENSOR##_i] == ATENSOR.strides()[ATENSOR##_i+1] * ATENSOR.sizes()[ATENSOR##_i+1] && ATENSOR##_i != DIM && ATENSOR##_i+1 != DIM) { \
          ATENSOR##_sizes[TH_TENSOR_dim_index] = ATENSOR.sizes()[ATENSOR##_i] * ATENSOR##_sizes[TH_TENSOR_dim_index]; \
          if (DIM != ATENSOR.dim()-1 && ATENSOR##_i < DIM) \
            ATENSOR##_dimOffset--; \
        } else { \
          --TH_TENSOR_dim_index; \
          ATENSOR##_sizes[TH_TENSOR_dim_index] = ATENSOR.sizes()[ATENSOR##_i]; \
          ATENSOR##_strides[TH_TENSOR_dim_index] = ATENSOR.strides()[ATENSOR##_i]; \
        } \
      } \
      /* Size of the inner most section */ \
      ATENSOR##_size = ATENSOR##_sizes[ATENSOR##_dim-1]; \
      /* Stride of the inner most section */ \
      ATENSOR##_stride = ATENSOR##_strides[ATENSOR##_dim-1]; \
    } \
  } \
  ATENSOR##_i = 0;

#define  __ATH_TENSOR_APPLYX_UPDATE_COUNTERS(ATENSOR, ALWAYS_UPDATE) \
  if(ATENSOR##_i == ATENSOR##_size || ALWAYS_UPDATE) \
  { \
    if(ATENSOR##_contiguous) \
      break; \
\
    if(ATENSOR##_dim == 1) \
       break; \
\
    /* Reset pointer to beginning of loop */ \
    ATENSOR##_data -= ATENSOR##_size*ATENSOR##_stride; \
    for(ATENSOR##_i = ATENSOR##_dim-2; ATENSOR##_i >= 0; ATENSOR##_i--) \
    { \
      ATENSOR##_counter[ATENSOR##_i]++; \
      /* Jump ahread by the stride of this dimension */ \
      ATENSOR##_data += ATENSOR##_strides[ATENSOR##_i]; \
\
      if(ATENSOR##_counter[ATENSOR##_i]  == ATENSOR##_sizes[ATENSOR##_i]) \
      { \
        if(ATENSOR##_i == 0) \
        { \
          TH_TENSOR_APPLY_hasFinished = true; \
          break; \
        } \
          else \
        { \
          /* Reset the pointer to the beginning of the chunk defined by this dimension */ \
          ATENSOR##_data -= ATENSOR##_counter[ATENSOR##_i]*ATENSOR##_strides[ATENSOR##_i]; \
          ATENSOR##_counter[ATENSOR##_i] = 0; \
        } \
      } \
      else \
        break; \
    } \
    ATENSOR##_i = 0; \
  } \

#define ATH_TENSOR_APPLY2_D(TYPE, ATENSOR1, ATENSOR2, DIM, CODE) \
{ \
  bool TH_TENSOR_APPLY_hasFinished = false; \
  int64_t TH_TENSOR_dim_index = 0; \
  __ATH_TENSOR_APPLYX_PREAMBLE(TYPE, ATENSOR1, DIM, 1) \
  __ATH_TENSOR_APPLYX_PREAMBLE(TYPE, ATENSOR2, DIM, 1) \
\
  auto t1_numel = ATENSOR1.numel(); \
  auto t2_numel = ATENSOR2.numel(); \
  if(t1_numel != t2_numel) {                                    \
    std::ostringstream oss; \
    oss << "inconsistent tensor size, expected " << ATENSOR1.sizes() << " and " << ATENSOR2.sizes() \
        << " to have the same number of elements, but got " << t1_numel << " and " << t2_numel << " elements respectively"; \
    throw std::runtime_error(oss.str()); \
  }                                                                   \
  while(!TH_TENSOR_APPLY_hasFinished) \
  { \
    /* Loop through the inner most region of the Tensor */ \
    for(; ATENSOR1##_i < ATENSOR1##_size && ATENSOR2##_i < ATENSOR2##_size; ATENSOR1##_i++, ATENSOR2##_i++, ATENSOR1##_data += ATENSOR1##_stride, ATENSOR2##_data += ATENSOR2##_stride) /* 0 et pas TENSOR##_dim! */ \
    { \
      CODE \
    } \
    __ATH_TENSOR_APPLYX_UPDATE_COUNTERS(ATENSOR1, 0) \
    __ATH_TENSOR_APPLYX_UPDATE_COUNTERS(ATENSOR2, 0) \
  } \
  if(ATENSOR1##_counter != NULL) \
    delete [] ATENSOR1##_counter; \
  if(ATENSOR2##_counter != NULL) \
    delete [] ATENSOR2##_counter; \
}

template <typename ScalarType, typename Op>
void tensor_apply2(Tensor& tensor1, Tensor& tensor2, int64_t dim, Op& op) {
  bool TH_TENSOR_APPLY_hasFinished = false;
  int64_t TH_TENSOR_dim_index = 0;
  __ATH_TENSOR_APPLYX_PREAMBLE(ScalarType, tensor1, dim, 1)
  __ATH_TENSOR_APPLYX_PREAMBLE(ScalarType, tensor2, dim, 1)
  auto t1_numel = tensor1.numel();
  auto t2_numel = tensor2.numel();
  if(t1_numel != t2_numel) {
    std::ostringstream oss;
    oss << "inconsistent tensor size, expected " << tensor1.sizes() << " and " << tensor2.sizes()
        << " to have the same number of elements, but got " << t1_numel << " and " << t2_numel << " elements respectively";
    throw std::runtime_error(oss.str());
  }
  while(!TH_TENSOR_APPLY_hasFinished)
  {
    /*/* Loop through the inner most region of the Tensor */
    for(; tensor1_i < tensor1_size && tensor2_i < tensor2_size; tensor1_i++, tensor2_i++, tensor1_data += tensor1_stride, tensor2_data += tensor2_stride)
    {
      op(*tensor1_data, *tensor2_data, TH_TENSOR_APPLY_hasFinished);
    }
    __ATH_TENSOR_APPLYX_UPDATE_COUNTERS(tensor1, 0)
    __ATH_TENSOR_APPLYX_UPDATE_COUNTERS(tensor2, 0)
  }
  if(tensor1_counter != NULL)
    delete [] tensor1_counter;
  if(tensor2_counter != NULL)
    delete [] tensor2_counter;
}

#define ATH_TENSOR_APPLY2(TYPE, ATENSOR1, ATENSOR2, CODE) \
  ATH_TENSOR_APPLY2_D(TYPE, ATENSOR1, ATENSOR2, -1, CODE)


template<typename ScalarType, typename Op>
void tensor_apply2_op(Tensor tensor1, Tensor tensor2, Op& op) {
  tensor_apply2<ScalarType, Op>(tensor1, tensor2, -1, op);
}

/*#define ATH_TENSOR_APPLY2_OP(TYPE, ATENSOR1, ATENSOR2, OP) \
//tensor_apply2<TYPE, Op>
  //tensor_apply2(TYPE, ATENSOR1, ATENSOR2, -1, OP)
*/
}
