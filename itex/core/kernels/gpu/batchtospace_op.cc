/* Copyright (c) 2021-2022 Intel Corporation

Copyright 2016 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <string>

#include "itex/core/kernels/common/fill_functor.h"
#include "itex/core/kernels/gpu/spacetobatch_functor.h"
#include "itex/core/utils/logging.h"
#include "itex/core/utils/op_kernel.h"
#include "itex/core/utils/op_requires.h"
#include "itex/core/utils/plugin_tensor.h"
#include "itex/core/utils/register_types.h"
#include "itex/core/utils/tensor_shape.h"
#include "itex/core/utils/types.h"
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"

namespace itex {

typedef Eigen::GpuDevice GPUDevice;

template <typename Device, typename T>
static void BatchToSpaceOpCompute(OpKernelContext* context,
                                  const Tensor& orig_input_tensor,
                                  const Tensor& orig_block_shape,
                                  const Tensor& orig_crops) {
  const int input_dims = orig_input_tensor.dims();
  OP_REQUIRES(
      context, TensorShapeUtils::IsVector(orig_block_shape.shape()),
      errors::InvalidArgument("block_shape rank should be 1 instead of ",
                              orig_block_shape.dims()));

  const int block_dims = orig_block_shape.dim_size(0);
  OP_REQUIRES(
      context, orig_input_tensor.dims() >= 1 + block_dims,
      errors::InvalidArgument("input rank should be >= ", 1 + block_dims,
                              " instead of ", orig_input_tensor.dims()));

  OP_REQUIRES(context,
              TensorShapeUtils::IsMatrix(orig_crops.shape()) &&
                  block_dims == orig_crops.dim_size(0) &&
                  2 == orig_crops.dim_size(1),
              errors::InvalidArgument("crops should have shape [", block_dims,
                                      ", 2] instead of ",
                                      orig_crops.shape().DebugString()));
  // To avoid out-of-bounds access in the case that the block_shape and/or
  // crops tensors are concurrently modified, we must copy the values.
  gtl::InlinedVector<int64, 4> block_shape;
  gtl::InlinedVector<int64, 8> crops;
  internal::spacetobatch::SubtleMustCopyFlat(orig_block_shape, &block_shape);
  internal::spacetobatch::SubtleMustCopyFlat(orig_crops, &crops);

  // Determine the length of the prefix of block dims that can be combined
  // into the batch dimension due to having no padding and block_shape=1.
  int removed_prefix_block_dims = 0;
  for (; removed_prefix_block_dims < block_dims; ++removed_prefix_block_dims) {
    const int dim = removed_prefix_block_dims;
    if (crops[2 * dim] != 0 || crops[2 * dim + 1] != 0 ||
        block_shape[dim] != 1) {
      break;
    }
  }

  // Determine the length of the suffix of block dims that can be combined
  // into the depth dimension due to having no padding and block_shape=1.
  int removed_suffix_block_dims = 0;
  for (; removed_suffix_block_dims < block_dims - removed_prefix_block_dims;
       ++removed_suffix_block_dims) {
    const int dim = block_dims - 1 - removed_suffix_block_dims;
    if (crops[2 * dim] != 0 || crops[2 * dim + 1] != 0 ||
        block_shape[dim] != 1) {
      break;
    }
  }

  // Compute the product of the block_shape values.
  int64 block_shape_product = 1;
  for (int block_dim = 0; block_dim < block_dims; ++block_dim) {
    block_shape_product *= block_shape[block_dim];
  }
  OP_REQUIRES(
      context, block_shape_product > 0,
      errors::InvalidArgument("Product of block sizes must be positive, got ",
                              block_shape_product));

  const int64 orig_input_batch_size = orig_input_tensor.dim_size(0);
  OP_REQUIRES(
      context, orig_input_batch_size % block_shape_product == 0,
      errors::InvalidArgument("Input batch dimension (", orig_input_batch_size,
                              ") is not divisible by product of block sizes (",
                              block_shape_product, ")"));

  const int internal_block_dims =
      block_dims - removed_prefix_block_dims - removed_suffix_block_dims;
  OP_REQUIRES(context, internal_block_dims <= kMaxSpaceToBatchBlockDims,
              errors::InvalidArgument(
                  "Maximum number of non-combined block dimensions is ",
                  internal_block_dims, " but must not exceed ",
                  kMaxSpaceToBatchBlockDims));

  if (internal_block_dims == 0) {
    context->set_output(0, orig_input_tensor);
    return;
  }

  // For the purpose of computing the result, the input will be treated as
  // having this shape, of rank 2 + internal_block_dims.
  TensorShape internal_input_shape;

  // For the purpose of computing the result, the output will be treated as
  // having this shape, of rank 2 + internal_block_dims.
  TensorShape internal_output_shape;

  // The actual output shape exposed to callers.
  TensorShape external_output_shape;

  external_output_shape.AddDim(orig_input_batch_size / block_shape_product);

  int64 input_batch_size = orig_input_batch_size;
  for (int block_dim = 0; block_dim < removed_prefix_block_dims; ++block_dim) {
    const int64 size = orig_input_tensor.dim_size(block_dim + 1);
    input_batch_size *= size;
    external_output_shape.AddDim(size);
  }
  internal_input_shape.AddDim(input_batch_size);
  internal_output_shape.AddDim(input_batch_size / block_shape_product);

  for (int block_dim = removed_prefix_block_dims;
       block_dim < block_dims - removed_suffix_block_dims; ++block_dim) {
    const int64 crop_start = crops[2 * block_dim],
                crop_end = crops[2 * block_dim + 1];
    OP_REQUIRES(context, crop_start >= 0 && crop_end >= 0,
                errors::InvalidArgument("Crops must be non-negative"));
    const int64 input_size = orig_input_tensor.dim_size(block_dim + 1);
    const int64 block_shape_value = block_shape[block_dim];
    const int64 cropped_size =
        input_size * block_shape_value - crop_start - crop_end;
    OP_REQUIRES(context, cropped_size >= 0,
                errors::InvalidArgument("cropped_shape[", block_dim, "]=",
                                        cropped_size, " must be non-negative"));
    internal_input_shape.AddDim(input_size);
    internal_output_shape.AddDim(cropped_size);
    external_output_shape.AddDim(cropped_size);
  }

  int64 depth = 1;
  for (int dim = block_dims - removed_suffix_block_dims + 1; dim < input_dims;
       ++dim) {
    const int64 size = orig_input_tensor.dim_size(dim);
    external_output_shape.AddDim(size);
    depth *= size;
  }
  internal_input_shape.AddDim(depth);
  internal_output_shape.AddDim(depth);

  // Allocate output tensor.
  Tensor* output_tensor = nullptr;
  OP_REQUIRES_OK(context, context->allocate_output(0, external_output_shape,
                                                   &output_tensor));
  if (output_tensor->NumElements() == 0) {
    functor::SetZeroFunctor<Device, T> fill;
    fill(context->eigen_gpu_device(), output_tensor->flat<T>());
    return;
  }

  const int64* internal_crops = &crops[2 * removed_prefix_block_dims];
  const int64* internal_block_shape = &block_shape[removed_prefix_block_dims];

  switch (internal_block_dims) {
#define TF_BATCHTOSPACE_BLOCK_DIMS_CASE(NUM_BLOCK_DIMS)                   \
  case NUM_BLOCK_DIMS: {                                                  \
    OP_REQUIRES_OK(                                                       \
        context,                                                          \
        (functor::SpaceToBatchFunctor<Device, T, NUM_BLOCK_DIMS, true>()( \
            context->eigen_device<Device>(),                              \
            output_tensor->shaped<T, NUM_BLOCK_DIMS + 2>(                 \
                internal_output_shape.dim_sizes()),                       \
            internal_block_shape, internal_crops,                         \
            orig_input_tensor.shaped<T, NUM_BLOCK_DIMS + 2>(              \
                internal_input_shape.dim_sizes()))));                     \
  } break;                                                                \
    /**/
    TF_SPACETOBATCH_FOR_EACH_NUM_BLOCK_DIMS(TF_BATCHTOSPACE_BLOCK_DIMS_CASE)
#undef TF_BATCHTOSPACE_BLOCK_DIMS_CASE
  }
}

template <typename Device, typename T>
class BatchToSpaceNDOp : public OpKernel {
 public:
  explicit BatchToSpaceNDOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* context) override {
    const Tensor& orig_input_tensor = context->input(0);
    const Tensor& orig_block_shape = context->input(1);
    const Tensor& orig_crops = context->input(2);
    BatchToSpaceOpCompute<Device, T>(context, orig_input_tensor,
                                     orig_block_shape, orig_crops);
  }
};

template <typename Device, typename T>
class BatchToSpaceOp : public OpKernel {
 public:
  explicit BatchToSpaceOp(OpKernelConstruction* context) : OpKernel(context) {
    OP_REQUIRES_OK(context, context->GetAttr("block_size", &block_size_));
    OP_REQUIRES(
        context, block_size_ > 1,
        errors::InvalidArgument("Block size should be > 1: ", block_size_));
  }

  void Compute(OpKernelContext* context) override {
    // This is not the same as proper's code. It should be set in the
    // constructor, but due to has no allocate temp for OpKernelConstruction, we
    // have to do it in every Compute. Of cause, it will only init once.
    initBlockShape(context);

    const Tensor& in0 = context->input(0);
    const Tensor& in1 = context->input(1);
    const int dims = in0.dims();

    // Check on the input dimensions first.
    // The input is presumed to be [batch, height, width, depth]
    static const int kRequiredDims = 4;
    OP_REQUIRES(context, kRequiredDims == dims,
                errors::InvalidArgument("Input rank should be: ", kRequiredDims,
                                        "instead of: ", dims));
    BatchToSpaceOpCompute<Device, T>(context, in0, block_shape_, in1);
  }

 private:
  void initBlockShape(OpKernelContext* context) {
    if (block_shape_.NumElements() == 0) {
      // We don't use context->allocate_persistent because the allocation must
      // happen on the CPU regardless of Device.
      AllocatorAttributes alloc_attrs;
      alloc_attrs.set_on_host(true);
      OP_REQUIRES_OK(context,
                     context->allocate_temp(itex::DT_INT64, TensorShape({2}),
                                            &block_shape_, alloc_attrs));
      auto block_shape_vec = block_shape_.vec<int64>();
      block_shape_vec(0) = block_size_;
      block_shape_vec(1) = block_size_;
    }
  }

  int block_size_;
  Tensor block_shape_;
};

#define REGISTER(T)                                        \
  REGISTER_KERNEL_BUILDER(Name("BatchToSpaceND")           \
                              .Device(DEVICE_GPU)          \
                              .TypeConstraint<T>("T")      \
                              .HostMemory("block_shape")   \
                              .HostMemory("crops"),        \
                          BatchToSpaceNDOp<GPUDevice, T>); \
  REGISTER_KERNEL_BUILDER(Name("BatchToSpace")             \
                              .Device(DEVICE_GPU)          \
                              .TypeConstraint<T>("T")      \
                              .HostMemory("crops"),        \
                          BatchToSpaceOp<GPUDevice, T>);

TF_CALL_GPU_NUMBER_TYPES(REGISTER);
#ifdef ITEX_ENABLE_DOUBLE
TF_CALL_double(REGISTER);
#endif  // ITEX_ENABLE_DOUBLE
#undef REGISTER

}  // end namespace itex
