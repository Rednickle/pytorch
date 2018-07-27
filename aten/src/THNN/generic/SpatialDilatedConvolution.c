#ifndef TH_GENERIC_FILE
#define TH_GENERIC_FILE "generic/SpatialDilatedConvolution.c"
#else

static inline void THNN_(SpatialDilatedConvolution_shapeCheck)(
	THTensor *input, THTensor *gradOutput,
	THTensor *weight, THTensor *bias,
	int kH, int kW, int dH, int dW, int padH, int padW,
	int dilationH, int dilationW, int weight_nullable) {
  THArgCheck(kW > 0 && kH > 0, 9,
             "kernel size should be greater than zero, but got kH: %d kW: %d", kH, kW);
  THArgCheck(dW > 0 && dH > 0, 11,
             "stride should be greater than zero, but got dH: %d dW: %d", dH, dW);
  THArgCheck(dilationW > 0 && dilationH > 0, 15,
             "dilation should be greater than zero, but got dilationH: %d, dilationW: %d",
             dilationH, dilationW);

  if (weight != NULL) {
    THNN_ARGCHECK(!weight->is_empty() && weight->dim() == 4, 4, weight,
                  "non-empty 4D weight tensor (nOutputPlane, nInputPlane, kH, kW) expected, "
                  "but got: %s");
    if (bias != NULL) {
      THNN_CHECK_DIM_SIZE(bias, 1, 0, THTensor_sizeLegacyNoScalars(weight, 0));
    }
  } else if (!weight_nullable) {
    THError("weight tensor is expected to be non-nullable");
  }

  int ndim = input->dim();
  int dimf = 0;
  int dimh = 1;
  int dimw = 2;

  if (ndim == 4) {
    dimf++;
    dimh++;
    dimw++;
  }

  THNN_ARGCHECK(!input->is_empty() && (ndim == 3 || ndim == 4), 2, input,
		"non-empty 3D or 4D input tensor expected but got: %s");

  int64_t inputHeight  = THTensor_sizeLegacyNoScalars(input, dimh);
  int64_t inputWidth   = THTensor_sizeLegacyNoScalars(input, dimw);

  int64_t outputHeight = (inputHeight + 2*padH - (dilationH * (kH - 1) + 1)) / dH + 1;
  int64_t outputWidth  = (inputWidth + 2*padW - (dilationW * (kW - 1) + 1)) / dW + 1;

  if (outputWidth < 1 || outputHeight < 1) {
    THError("Given input size per channel: (%ld x %ld). "
      "Calculated output size per channel: (%ld x %ld). Output size is too small",
      inputHeight, inputWidth, outputHeight, outputWidth);
  }

  if (weight != NULL) {
    int64_t nInputPlane = THTensor_sizeLegacyNoScalars(weight, 1);
    THNN_CHECK_DIM_SIZE(input, ndim, dimf, nInputPlane);
  }

  if (gradOutput != NULL) {
    if (weight != NULL) {
      int64_t nOutputPlane = THTensor_sizeLegacyNoScalars(weight, 0);
      THNN_CHECK_DIM_SIZE(gradOutput, ndim, dimf, nOutputPlane);
    } else if (bias != NULL) {
      int64_t nOutputPlane = THTensor_sizeLegacyNoScalars(bias, 0);
      THNN_CHECK_DIM_SIZE(gradOutput, ndim, dimf, nOutputPlane);
    }
    THNN_CHECK_DIM_SIZE(gradOutput, ndim, dimh, outputHeight);
    THNN_CHECK_DIM_SIZE(gradOutput, ndim, dimw, outputWidth);
  }
}

void THNN_(SpatialDilatedConvolution_updateOutput)(
    THNNState *state,
    THTensor *input,
    THTensor *output,
    THTensor *weight,
    THTensor *bias,
    THTensor *columns,
    THTensor *ones,
    int kW, int kH,
    int dW, int dH,
    int padW, int padH,
    int dilationW, int dilationH)
{

  THNN_(SpatialDilatedConvolution_shapeCheck)
    (input, NULL, weight, bias, kH, kW, dH, dW, padH, padW,
     dilationH, dilationW, 0);

  // Params:
  int nInputPlane = THTensor_sizeLegacyNoScalars(weight, 1);
  int nOutputPlane = THTensor_sizeLegacyNoScalars(weight, 0);

  input = THTensor_(newContiguous)(input);
  weight = THTensor_(newContiguous)(weight);
  THArgCheck(THTensor_(isContiguous)(columns), 5, "columns needs to be contiguous");
  if (bias) {
    bias = THTensor_(newContiguous)(bias);
    THArgCheck(THTensor_(isContiguous)(ones), 6, "ones needs to be contiguous");
  }
  int is_batch = 1;
  if (input->dim() == 3) {
    // Force batch
    is_batch = 0;
    THTensor_(resize4d)(input, 1, THTensor_sizeLegacyNoScalars(input, 0), THTensor_sizeLegacyNoScalars(input, 1), THTensor_sizeLegacyNoScalars(input, 2));
  }
  int64_t inputWidth   = THTensor_sizeLegacyNoScalars(input, 3);
  int64_t inputHeight  = THTensor_sizeLegacyNoScalars(input, 2);
  int64_t outputWidth  = (inputWidth + 2*padW - (dilationW * (kW - 1) + 1)) / dW + 1;
  int64_t outputHeight = (inputHeight + 2*padH - (dilationH * (kH - 1) + 1)) / dH + 1;

  // Batch size + input planes
  int64_t batchSize = THTensor_sizeLegacyNoScalars(input, 0);

  // Resize output
  THTensor_(resize4d)(output, batchSize, nOutputPlane, outputHeight, outputWidth);
  THTensor_(zero)(output);

  // Resize temporary columns
  THTensor_(resize2d)(columns, nInputPlane*kW*kH, outputHeight*outputWidth);

  // Define a buffer of ones, for bias accumulation
  // Note: this buffer can be shared with other modules, it only ever gets increased,
  // and always contains ones.
  if (!THTensor_(isContiguous)(ones) || ones->dim() != 2 ||
      THTensor_sizeLegacyNoScalars(ones, 0)*THTensor_sizeLegacyNoScalars(ones, 1) < outputHeight*outputWidth) {
    // Resize plane and fill with ones...
    THTensor_(resize2d)(ones, outputHeight, outputWidth);
    THTensor_(fill)(ones, 1);
  }

  // Helpers
  THTensor *input_n = THTensor_(new)();
  THTensor *output_n = THTensor_(new)();

  // For each elt in batch, do:
  for (int elt = 0; elt < batchSize; elt ++) {
    // Matrix mulitply per output:
    THTensor_(select)(input_n, input, 0, elt);
    THTensor_(select)(output_n, output, 0, elt);

    // Do Bias first:
    // M,N,K are dims of matrix A and B
    int64_t m_ = nOutputPlane;
    int64_t n_ = outputHeight * outputWidth;
    int64_t k_ = 1;

    // Do GEMM (note: this is a bit confusing because gemm assumes column-major matrices)
    if (bias) {
      THBlas_(gemm)(
        't', 'n',
        n_, m_, k_,
        1,
        THTensor_(data)(ones), k_,
        THTensor_(data)(bias), k_,
        0,
        THTensor_(data)(output_n), n_
      );
    } else {
      THTensor_(zero)(output_n);
    }

    // Extract columns:
    THNN_(im2col)(
      THTensor_(data)(input_n),
      nInputPlane, inputHeight, inputWidth,
      outputHeight, outputWidth,
      kH, kW, padH, padW, dH, dW,
      dilationH, dilationW,
      THTensor_(data)(columns)
    );

    // M,N,K are dims of matrix A and B
    int64_t m = nOutputPlane;
    int64_t n = THTensor_sizeLegacyNoScalars(columns, 1);
    int64_t k = nInputPlane*kH*kW;

    // Do GEMM (note: this is a bit confusing because gemm assumes column-major matrices)
    THBlas_(gemm)(
      'n', 'n',
      n, m, k,
      1,
      THTensor_(data)(columns), n,
      THTensor_(data)(weight), k,
      1,
      THTensor_(data)(output_n), n
    );
  }

  // Free
  THTensor_(free)(input_n);
  THTensor_(free)(output_n);

  // Resize output
  if (is_batch == 0) {
    THTensor_(resize3d)(output, nOutputPlane, outputHeight, outputWidth);
    THTensor_(resize3d)(input, nInputPlane, inputHeight, inputWidth);
  }

  THTensor_(free)(input);
  THTensor_(free)(weight);
  if (bias) THTensor_(free)(bias);
}

void THNN_(SpatialDilatedConvolution_updateGradInput)(
    THNNState *state,
    THTensor *input,
    THTensor *gradOutput,
    THTensor *gradInput,
    THTensor *weight,
    THTensor *gradColumns,
    int kW, int kH,
    int dW, int dH,
    int padW, int padH,
    int dilationW, int dilationH)
{
  THNN_(SpatialDilatedConvolution_shapeCheck)
    (input, gradOutput, weight, NULL, kH, kW, dH, dW, padH, padW,
     dilationH, dilationW, 0);

  // Params
  int64_t nInputPlane = THTensor_sizeLegacyNoScalars(weight, 1);
  int64_t nOutputPlane = THTensor_sizeLegacyNoScalars(weight, 0);

  input = THTensor_(newContiguous)(input);
  weight = THTensor_(newContiguous)(weight);
  gradOutput = THTensor_(newContiguous)(gradOutput);
  THArgCheck(THTensor_(isContiguous)(gradColumns), 5, "gradColumns needs to be contiguous");
  int is_batch = 1;
  if (input->dim() == 3) {
    // Force batch
    is_batch = 0;
    THTensor_(resize4d)(input, 1, THTensor_sizeLegacyNoScalars(input, 0), THTensor_sizeLegacyNoScalars(input, 1), THTensor_sizeLegacyNoScalars(input, 2));
    THTensor_(resize4d)(gradOutput, 1, THTensor_sizeLegacyNoScalars(gradOutput, 0), THTensor_sizeLegacyNoScalars(gradOutput, 1),
			THTensor_sizeLegacyNoScalars(gradOutput, 2));
  }

  int64_t inputWidth   = THTensor_sizeLegacyNoScalars(input, 3);
  int64_t inputHeight  = THTensor_sizeLegacyNoScalars(input, 2);
  int64_t outputWidth  = (inputWidth + 2*padW - (dilationW * (kW - 1) + 1)) / dW + 1;
  int64_t outputHeight = (inputHeight + 2*padH - (dilationH * (kH - 1) + 1)) / dH + 1;

  // Batch size + input planes
  int64_t batchSize = THTensor_sizeLegacyNoScalars(input, 0);

  // Resize output
  THTensor_(resize4d)(gradInput, batchSize, nInputPlane, inputHeight, inputWidth);

  // Resize temporary columns
  THTensor_(resize2d)(gradColumns, nInputPlane*kW*kH, outputHeight*outputWidth);
  THTensor_(zero)(gradColumns);

  // Helpers
  THTensor *gradInput_n = THTensor_(new)();
  THTensor *gradOutput_n = THTensor_(new)();

  // For each elt in batch, do:
  for (int elt = 0; elt < batchSize; elt ++) {
    // Matrix mulitply per sample:
    THTensor_(select)(gradInput_n, gradInput, 0, elt);
    THTensor_(select)(gradOutput_n, gradOutput, 0, elt);

    // M,N,K are dims of matrix A and B
    int64_t m = nInputPlane*kW*kH;
    int64_t n = THTensor_sizeLegacyNoScalars(gradColumns, 1);
    int64_t k = nOutputPlane;

    // Do GEMM (note: this is a bit confusing because gemm assumes column-major matrices)
    THBlas_(gemm)(
        'n', 't',
        n, m, k,
        1,
        THTensor_(data)(gradOutput_n), n,
        THTensor_(data)(weight), m,
        0,
        THTensor_(data)(gradColumns), n
    );

    // Unpack columns back into input:
    THNN_(col2im)(
      THTensor_(data)(gradColumns),
      nInputPlane, inputHeight, inputWidth, outputHeight, outputWidth,
      kH, kW, padH, padW, dH, dW,
      dilationH, dilationW,
      THTensor_(data)(gradInput_n)
    );
  }

  // Free
  THTensor_(free)(gradInput_n);
  THTensor_(free)(gradOutput_n);

  // Resize output
  if (is_batch == 0) {
    THTensor_(resize3d)(gradOutput, nOutputPlane, outputHeight, outputWidth);
    THTensor_(resize3d)(input, nInputPlane, inputHeight, inputWidth);
    THTensor_(resize3d)(gradInput, nInputPlane, inputHeight, inputWidth);
  }

  THTensor_(free)(input);
  THTensor_(free)(gradOutput);
  THTensor_(free)(weight);
}


void THNN_(SpatialDilatedConvolution_accGradParameters)(
    THNNState *state,
    THTensor *input,
    THTensor *gradOutput,
    THTensor *gradWeight,
    THTensor *gradBias,
    THTensor *columns,
    THTensor *ones,
    int kW, int kH,
    int dW, int dH,
    int padW, int padH,
    int dilationW, int dilationH,
    accreal scale_)
{
  real scale = TH_CONVERT_ACCREAL_TO_REAL(scale_);
  THNN_(SpatialDilatedConvolution_shapeCheck)
    (input, gradOutput, gradWeight, gradBias, kH, kW, dH, dW, padH, padW,
     dilationH, dilationW, 1);

  // Params
  input = THTensor_(newContiguous)(input);
  gradOutput = THTensor_(newContiguous)(gradOutput);
  if (gradWeight) {
    THArgCheck(THTensor_(isContiguous)(gradWeight), 4, "gradWeight needs to be contiguous");
  }
  THArgCheck(THTensor_(isContiguous)(columns), 6, "columns needs to be contiguous");
  if (gradBias) {
    THArgCheck(THTensor_(isContiguous)(gradBias), 5, "gradBias needs to be contiguous");
    THArgCheck(THTensor_(isContiguous)(ones), 7, "ones needs to be contiguous");
  }
  int is_batch = 1;
  if (input->dim() == 3) {
    // Force batch
    is_batch = 0;
    THTensor_(resize4d)(input, 1, THTensor_sizeLegacyNoScalars(input, 0), THTensor_sizeLegacyNoScalars(input, 1), THTensor_sizeLegacyNoScalars(input, 2));
    THTensor_(resize4d)(gradOutput, 1, THTensor_sizeLegacyNoScalars(gradOutput, 0),
			THTensor_sizeLegacyNoScalars(gradOutput, 1), THTensor_sizeLegacyNoScalars(gradOutput, 2));
  }

  int64_t nInputPlane = THTensor_sizeLegacyNoScalars(input, 1);
  int64_t nOutputPlane = THTensor_sizeLegacyNoScalars(gradOutput, 1);
  int64_t inputWidth   = THTensor_sizeLegacyNoScalars(input, 3);
  int64_t inputHeight  = THTensor_sizeLegacyNoScalars(input, 2);
  int64_t outputWidth  = (inputWidth + 2*padW - (dilationW * (kW - 1) + 1)) / dW + 1;
  int64_t outputHeight = (inputHeight + 2*padH - (dilationH * (kH - 1) + 1)) / dH + 1;

  // Batch size + input planes
  int64_t batchSize = THTensor_sizeLegacyNoScalars(input, 0);

  // Resize temporary columns
  THTensor_(resize2d)(columns, nInputPlane*kW*kH, outputHeight*outputWidth);

  // Helpers
  THTensor *input_n = THTensor_(new)();
  THTensor *gradOutput_n = THTensor_(new)();

  // For each elt in batch, do:
  for (int elt = 0; elt < batchSize; elt ++) {
    // Matrix mulitply per output:
    THTensor_(select)(gradOutput_n, gradOutput, 0, elt);

    // Do Weight:
    if (gradWeight) {
      // Matrix mulitply per output:
      THTensor_(select)(input_n, input, 0, elt);

      // Extract columns:
      THNN_(im2col)(
        THTensor_(data)(input_n),
        nInputPlane, inputHeight, inputWidth,
        outputHeight, outputWidth,
        kH, kW, padH, padW, dH, dW,
        dilationH, dilationW,
        THTensor_(data)(columns)
      );

      // M,N,K are dims of matrix A and B
      int64_t m = nOutputPlane;
      int64_t n = nInputPlane*kW*kH;
      int64_t k = THTensor_sizeLegacyNoScalars(columns, 1);

      // Do GEMM (note: this is a bit confusing because gemm assumes column-major matrices)
      THBlas_(gemm)(
          't', 'n',
          n, m, k,
          scale,
          THTensor_(data)(columns), k,
          THTensor_(data)(gradOutput_n), k,
          1,
          THTensor_(data)(gradWeight), n
      );
    }

    // Do Bias:
    if (gradBias) {
      // M,N,K are dims of matrix A and B
      int64_t m_ = nOutputPlane;
      int64_t k_ = outputHeight * outputWidth;

      // Do GEMV (note: this is a bit confusing because gemv assumes column-major matrices)
      // Define a buffer of ones, for bias accumulation
      if (ones->dim() != 2 || THTensor_sizeLegacyNoScalars(ones, 0)*THTensor_sizeLegacyNoScalars(ones, 1) < outputHeight*outputWidth) {
        // Resize plane and fill with ones...
        THTensor_(resize2d)(ones, outputHeight, outputWidth);
        THTensor_(fill)(ones, 1);
      }
      THBlas_(gemv)(
          't',
          k_, m_,
          scale,
          THTensor_(data)(gradOutput_n), k_,
          THTensor_(data)(ones), 1,
          1,
          THTensor_(data)(gradBias), 1
      );
    }
  }

  // Free
  THTensor_(free)(input_n);
  THTensor_(free)(gradOutput_n);

  // Resize
  if (is_batch == 0) {
    THTensor_(resize3d)(gradOutput, nOutputPlane, outputHeight, outputWidth);
    THTensor_(resize3d)(input, nInputPlane, inputHeight, inputWidth);
  }

  THTensor_(free)(input);
  THTensor_(free)(gradOutput);
}

#endif
