#include "ATen/ATen.h"
#include "ATen/NativeFunctions.h"


namespace at { namespace native {

Tensor pairwise_distance(const Tensor& x1, const Tensor& x2, double p, double eps, bool keepdim) {
  return at::native::norm(x1 - x2 + eps, p, 1, keepdim);
}
}}  // namespace at::native
