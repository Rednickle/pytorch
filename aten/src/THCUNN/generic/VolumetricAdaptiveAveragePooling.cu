#ifndef THC_GENERIC_FILE
#define THC_GENERIC_FILE "generic/VolumetricAdaptiveAveragePooling.cu"
#else

#include "../common.h"

// 5d tensor B x D x T x H x W

void THNN_(VolumetricAdaptiveAveragePooling_updateOutput)(
           THCState *state,
           THCTensor *input,
           THCTensor *output,
           int osizeT,
           int osizeW,
           int osizeH)
{
  THCUNN_assertSameGPU(state, 2, input, output);

  THCUNN_argCheck(state, !input->is_empty() && (input->dim() == 4 || input->dim() == 5), 2, input,
                  "non-empty 4D or 5D (batch mode) tensor expected for input, but got: %s");


  real *output_data;
  real *input_data;

  int64_t sizeD, isizeT, isizeH, isizeW;
  int64_t istrideD, istrideT, istrideH, istrideW;
  int64_t totalZ;

  if (input->dim() == 4) {
    sizeD = THTensor_sizeLegacyNoScalars(input, 0);
    isizeT = THTensor_sizeLegacyNoScalars(input, 1);
    isizeH = THTensor_sizeLegacyNoScalars(input, 2);
    isizeW = THTensor_sizeLegacyNoScalars(input, 3);

    istrideD = THTensor_strideLegacyNoScalars(input, 0);
    istrideT = THTensor_strideLegacyNoScalars(input, 1);
    istrideH = THTensor_strideLegacyNoScalars(input, 2);
    istrideW = THTensor_strideLegacyNoScalars(input, 3);

    THCTensor_(resize4d)(state, output, sizeD, osizeT, osizeH, osizeW);

    totalZ = sizeD * osizeT;
  } else {
    input = THCTensor_(newContiguous)(state, input);

    int64_t sizeB = THTensor_sizeLegacyNoScalars(input, 0);
    sizeD = THTensor_sizeLegacyNoScalars(input, 1);
    isizeT = THTensor_sizeLegacyNoScalars(input, 2);
    isizeH = THTensor_sizeLegacyNoScalars(input, 3);
    isizeW = THTensor_sizeLegacyNoScalars(input, 4);

    istrideD = THTensor_strideLegacyNoScalars(input, 1);
    istrideT = THTensor_strideLegacyNoScalars(input, 2);
    istrideH = THTensor_strideLegacyNoScalars(input, 3);
    istrideW = THTensor_strideLegacyNoScalars(input, 4);

    THCTensor_(resize5d)(state, output, sizeB, sizeD, osizeT, osizeH, osizeW);

    totalZ = sizeB * sizeD * osizeT;
  }

  input_data = THCTensor_(data)(state, input);
  output_data = THCTensor_(data)(state, output);

  int64_t offsetZ = 0;
  dim3 threads(32, 8);
  // each H*W plane is processed by blocksH thread blocks
  int blocksH = max((int)(16L / totalZ), 1);
  while (totalZ > 0) {
    dim3 blocks(totalZ > 65535 ? 65535 : totalZ, blocksH);
    cunn_VolumetricAdaptiveAveragePooling_updateOutput_kernel
      <<<blocks, threads, 0, THCState_getCurrentStream(state)>>>(
        input_data, output_data, isizeT, isizeH, isizeW, osizeT, osizeH, osizeW,
        istrideD, istrideT, istrideH, istrideW, offsetZ
      );

    totalZ -= 65535;
    offsetZ += 65535;
    THCudaCheck(cudaGetLastError());
  }

  if (input->dim() == 5) {
    // clean
    THCTensor_(free)(state, input);
  }
}

void THNN_(VolumetricAdaptiveAveragePooling_updateGradInput)(
           THCState *state,
           THCTensor *input,
           THCTensor *gradOutput,
           THCTensor *gradInput)
{
  THCUNN_assertSameGPU(state, 3, input, gradOutput, gradInput);

  gradOutput = THCTensor_(newContiguous)(state, gradOutput);

  THCTensor_(resizeAs)(state, gradInput, input);
  THCTensor_(zero)(state, gradInput);

  real *gradInput_data;
  real *gradOutput_data;

  int64_t sizeD, isizeT, isizeH, isizeW;
  int64_t osizeT, osizeH, osizeW;
  int64_t totalZ;

  if (input->dim() == 4) {
    sizeD = THTensor_sizeLegacyNoScalars(input, 0);
    isizeT = THTensor_sizeLegacyNoScalars(input, 1);
    isizeH = THTensor_sizeLegacyNoScalars(input, 2);
    isizeW = THTensor_sizeLegacyNoScalars(input, 3);

    osizeT = THTensor_sizeLegacyNoScalars(gradOutput, 1);
    osizeH = THTensor_sizeLegacyNoScalars(gradOutput, 2);
    osizeW = THTensor_sizeLegacyNoScalars(gradOutput, 3);
  } else {
    sizeD = THTensor_sizeLegacyNoScalars(input, 1);
    isizeT = THTensor_sizeLegacyNoScalars(input, 2);
    isizeH = THTensor_sizeLegacyNoScalars(input, 3);
    isizeW = THTensor_sizeLegacyNoScalars(input, 4);

    osizeT = THTensor_sizeLegacyNoScalars(gradOutput, 2);
    osizeH = THTensor_sizeLegacyNoScalars(gradOutput, 3);
    osizeW = THTensor_sizeLegacyNoScalars(gradOutput, 4);
  }

  // somehow nonatomic is passing all test for volumetric case.
  bool atomic = false; //(isizeW%osizeW != 0) || (isizeH%osizeH != 0) || (isizeT%osizeT != 0);

  if (input->dim() == 4) {
    totalZ = atomic ? sizeD * osizeT : sizeD * isizeT;
  } else {
    int sizeB = THTensor_sizeLegacyNoScalars(input, 0);
    totalZ = atomic ? sizeB * sizeD * osizeT : sizeB * sizeD * isizeT;
  }

  gradInput_data = THCTensor_(data)(state, gradInput);
  gradOutput_data = THCTensor_(data)(state, gradOutput);

  int64_t offsetZ = 0;
  dim3 threads(32, 8);
  // each H*W plane is processed by blocksH thread blocks
  int blocksH = max((int)(16L / totalZ), 1);
  while (totalZ > 0) {
    dim3 blocks(totalZ > 65535 ? 65535 : totalZ, blocksH);

    if (atomic)
    {
      cunn_atomic_VolumetricAdaptiveAveragePooling_updateGradInput_kernel
        <<<blocks, threads, 0, THCState_getCurrentStream(state)>>>(
          gradInput_data, gradOutput_data, isizeT, isizeH, isizeW,
          osizeT, osizeH, osizeW, offsetZ
        );
    } else {
        cunn_VolumetricAdaptiveAveragePooling_updateGradInput_kernel
          <<<blocks, threads, 0, THCState_getCurrentStream(state)>>>(
            gradInput_data, gradOutput_data, isizeT, isizeH, isizeW,
            osizeT, osizeH, osizeW, offsetZ
          );
    }

    totalZ -= 65535;
    offsetZ += 65535;
    THCudaCheck(cudaGetLastError());
  }
  // clean
  THCTensor_(free)(state, gradOutput);

}

#endif
