#ifndef THC_GENERIC_FILE
#define THC_GENERIC_FILE "generic/SpatialMaxUnpooling.cu"
#else

void THNN_(SpatialMaxUnpooling_updateOutput)(
           THCState *state,
           THCTensor *input,
           THCTensor *output,
           THCIndexTensor *indices,
           int owidth, int oheight)
{
  THCUNN_assertSameGPU(state, 3, input, output, indices);
  THCUNN_argCheck(state, !input->is_empty() && (input->dim() == 3 || input->dim() == 4), 2, input,
                  "non-empty 3D or 4D (batch mode) tensor expected for input, but got: %s");
  THCUNN_check_shape_indices(state, indices, input);

  int64_t nInputCols, nInputRows, nInputPlane, batchSize;

  if (input->dim() == 3) {
    nInputCols = THTensor_sizeLegacyNoScalars(input, 2);
    nInputRows = THTensor_sizeLegacyNoScalars(input, 1);
    nInputPlane = THTensor_sizeLegacyNoScalars(input, 0);
    batchSize = 1;
  }
  else
  {
    nInputCols = THTensor_sizeLegacyNoScalars(input, 3);
    nInputRows = THTensor_sizeLegacyNoScalars(input, 2);
    nInputPlane = THTensor_sizeLegacyNoScalars(input, 1);
    batchSize = THTensor_sizeLegacyNoScalars(input, 0);
  }

  input = THCTensor_(newContiguous)(state, input);
  indices = THCIndexTensor_(newContiguous)(state, indices);
  THCTensor_(resize4d)(state, output, batchSize, nInputPlane, oheight, owidth);
  THCTensor_(zero)(state, output);

  int count = THCTensor_(nElement)(state, input);

  MaxUnpoolForward <<< GET_BLOCKS(count), CUDA_NUM_THREADS, 0, THCState_getCurrentStream(state) >>>
      (count, THCTensor_(data)(state, input), THCIndexTensor_(data)(state, indices),
      batchSize, nInputPlane, nInputRows, nInputCols, oheight, owidth, THCTensor_(data)(state, output));
  THCudaCheck(cudaGetLastError());

  if(input->dim() == 3)
    THCTensor_(resize3d)(state, output, nInputPlane, oheight, owidth);

  THCTensor_(free)(state, input);
  THCIndexTensor_(free)(state, indices);
}

void THNN_(SpatialMaxUnpooling_updateGradInput)(
           THCState *state,
           THCTensor *input,
           THCTensor *gradOutput,
           THCTensor *gradInput,
           THCIndexTensor *indices,
           int owidth, int oheight)
{
  THCUNN_assertSameGPU(state, 4, input, gradOutput, indices, gradInput);
  THCUNN_check_shape_indices(state, indices, input);

  int64_t nInputCols, nInputRows, nInputPlane, batchSize;
  int dimw = 2;
  int dimh = 1;

  if (input->dim() == 3) {
    nInputPlane = THTensor_sizeLegacyNoScalars(input, 0);
    batchSize = 1;
  }
  else
  {
    ++dimw;
    ++dimh;
    nInputPlane = THTensor_sizeLegacyNoScalars(input, 1);
    batchSize = THTensor_sizeLegacyNoScalars(input, 0);
  }
  nInputCols = THTensor_sizeLegacyNoScalars(input, dimw);
  nInputRows = THTensor_sizeLegacyNoScalars(input, dimh);

  if(owidth!=THTensor_sizeLegacyNoScalars(gradOutput, dimw) || oheight!=gradOutput->size(dimh)){
     THError("Inconsistent gradOutput size. oheight= %d, owidth= %d, gradOutput: %dx%d",
             oheight, owidth,THTensor_sizeLegacyNoScalars(gradOutput, dimh),gradOutput->size(dimw));
  }

  input = THCTensor_(newContiguous)(state, input);
  indices = THCIndexTensor_(newContiguous)(state, indices);
  gradOutput = THCTensor_(newContiguous)(state, gradOutput);
  THCTensor_(resizeAs)(state, gradInput, input);

  int count = THCTensor_(nElement)(state, input);

  MaxUnpoolBackward <<< GET_BLOCKS(count), CUDA_NUM_THREADS, 0, THCState_getCurrentStream(state) >>>
      (count, THCTensor_(data)(state, gradOutput), THCIndexTensor_(data)(state, indices),
      batchSize, nInputPlane, nInputRows, nInputCols, oheight, owidth, THCTensor_(data)(state, gradInput));
  THCudaCheck(cudaGetLastError());

  // clean
  THCTensor_(free)(state, input);
  THCIndexTensor_(free)(state, indices);
  THCTensor_(free)(state, gradOutput);
}

#endif
