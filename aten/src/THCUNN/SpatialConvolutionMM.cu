#include <THCUNN/THCUNN.h>
#include <THC/THCTensor.hpp>
#include <THCUNN/common.h>
#include <THCUNN/im2col.h>

#include <TH/THHalf.h>
#include <THC/THCNumerics.cuh>

#include <THCUNN/generic/SpatialConvolutionMM.cu>
#include <THC/THCGenerateFloatTypes.h>
