#include "ATen/ATen.h"
#include "ATen/Dispatch.h"
#include "ATen/ExpandUtils.h"
#include "ATen/NativeFunctions.h"
#include "ATen/WrapDimUtils.h"
#include "cpu/ReduceOpsKernel.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <vector>

#include <map>

namespace at {
namespace native {

static inline Tensor integer_upcast(const Tensor& self, optional<ScalarType> dtype) {
  ScalarType scalarType = self.type().scalarType();
  ScalarType upcast_scalarType = dtype.value_or(at::isIntegralType(scalarType) ? ScalarType::Long : scalarType);
  return self.toType(upcast_scalarType);
}

Tensor cumsum(const Tensor& self, int64_t dim, optional<ScalarType> dtype) {
  return at::_cumsum(integer_upcast(self, dtype), dim);
}

Tensor& cumsum_out(Tensor& result, const Tensor& self, int64_t dim, optional<ScalarType> dtype) {
  // result type is favored over dtype
  return at::_cumsum_out(result, self.toType(result.type().scalarType()), dim);
}

Tensor cumprod(const Tensor& self, int64_t dim, optional<ScalarType> dtype) {
  return at::_cumprod(integer_upcast(self, dtype), dim);
}

Tensor& cumprod_out(Tensor& result, const Tensor& self, int64_t dim, optional<ScalarType> dtype) {
  // result type is favored over dtype
  return at::_cumprod_out(result, self.toType(result.type().scalarType()), dim);
}

// ALL REDUCE #################################################################

Tensor sum(const Tensor &self, optional<ScalarType> dtype) {
  return at::_sum(integer_upcast(self, dtype));
}


Tensor _sum_cpu(const Tensor& self) {
  if (self.is_contiguous()) {
    Tensor result = self.type().tensor({});
    sum_kernel(result, self, at::nullopt);
    return result;
  }
  return self._sumall();
}

Tensor prod(const Tensor &self, optional<ScalarType> dtype) {
  return at::_prod(integer_upcast(self, dtype));
}

Tensor _prod_cpu(const Tensor &self) {
  if (self.is_contiguous()) {
    Tensor result = self.type().tensor({});
    prod_kernel(result, self, at::nullopt);
    return result;
  }
  return self._prodall();
}

Tensor _sum_cuda(const Tensor &self_) { return self_._sumall(); }

Tensor _prod_cuda(const Tensor &self_) { return self_._prodall(); }

// \ALL REDUCE ################################################################

// DIM REDUCE #################################################################

static bool _dimreduce_return_trivial(Tensor &result, const Tensor &self,
                                      int64_t ident) {
  if (self.numel() == 1 && self.ndimension() == 0) {
    result.resize_({});
    result.fill_(self);
    return true;
  }
  // Return identity
  if (self.numel() == 0 && self.ndimension() == 1) {
    result.resize_({0});
    result.fill_(ident);
    return true;
  }
  return false;
}

static Tensor &_dimreduce_setup(Tensor &result, const Tensor &self,
                                int64_t dim) {
  IntList self_sizes = self.sizes();
  std::vector<int64_t> result_sizes;
  result_sizes.insert(result_sizes.end(), self_sizes.begin(), self_sizes.end());
  result_sizes[dim] = 1;
  result.resize_(result_sizes);
  return result;
}

Tensor& sum_out(Tensor& result, const Tensor& self, int64_t dim,
                bool keepdim, optional<ScalarType> dtype) {
  // result type is favored over dtype
  return at::_sum_out(result, self.toType(result.type().scalarType()), dim, keepdim);
}

Tensor &_sum_out_cpu(Tensor &result, const Tensor &self, int64_t dim_,
                     bool keepdim) {
  int64_t dim = maybe_wrap_dim(dim_, self.dim());
  if (_dimreduce_return_trivial(result, self, 0))
    return result;
  if (self.is_contiguous() && result.is_contiguous()) {
    _dimreduce_setup(result, self, dim);
    sum_kernel(result, self, dim);
    if (!keepdim) result.squeeze_(dim);
    return result;
  }
  return at::_th_sum_out(result, self, dim, keepdim);
}

Tensor &prod_out(Tensor &result, const Tensor &self, int64_t dim,
                 bool keepdim, optional<ScalarType> dtype) {
  // result type is favored over dtype
  return at::_prod_out(result, self.toType(result.type().scalarType()), dim);
}

Tensor &_prod_out_cpu(Tensor &result, const Tensor &self, int64_t dim_,
                      bool keepdim) {
  int64_t dim = maybe_wrap_dim(dim_, self.dim());
  if (_dimreduce_return_trivial(result, self, 1))
    return result;
  if (self.is_contiguous() && result.is_contiguous()) {
    _dimreduce_setup(result, self, dim);
    prod_kernel(result, self, dim);
    if (!keepdim) result.squeeze_(dim);
    return result;
  }
  return at::_th_prod_out(result, self, dim, keepdim);
}

Tensor &_sum_out_cuda(Tensor &result, const Tensor &self, int64_t dim,
                      bool keepdim) {
  return at::_th_sum_out(result, self, dim, keepdim);
}

Tensor &_prod_out_cuda(Tensor &result, const Tensor &self, int64_t dim,
                       bool keepdim) {
  return at::_th_prod_out(result, self, dim, keepdim);
}

Tensor sum(const Tensor &self, int64_t dim_, bool keepdim, optional<ScalarType> dtype) {
  at::_sum(integer_upcast(self, dtype), dim_, keepdim);
}

Tensor _sum(const Tensor &self, int64_t dim_, bool keepdim) {
  int64_t dim = maybe_wrap_dim(dim_, self.dim());
  Tensor result = self.type().tensor();
  return at::sum_out(result, self, dim, keepdim);
}

Tensor prod(const Tensor &self, int64_t dim_, bool keepdim, optional<ScalarType> dtype) {
  return at::_prod(integer_upcast(self, dtype), dim_, keepdim);
}

Tensor _prod(const Tensor &self, int64_t dim_, bool keepdim) {
  int64_t dim = maybe_wrap_dim(dim_, self.dim());
  Tensor result = self.type().tensor();
  return at::prod_out(result, self, dim, keepdim);
}

// \DIM REDUCE ################################################################
}
}
