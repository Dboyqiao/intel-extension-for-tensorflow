/* Copyright (c) 2021-2022 Intel Corporation

Copyright 2015 The TensorFlow Authors. All Rights Reserved.

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

#include "itex/core/kernels/gpu/reduction_ops_common.h"
#include "itex/core/utils/op_requires.h"
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"

namespace itex {

typedef Eigen::GpuDevice GPUDevice;

#define REGISTER_GPU_KERNELS_PROD(type)                                     \
  REGISTER_KERNEL_BUILDER(Name("Prod")                                      \
                              .Device(DEVICE_GPU)                           \
                              .TypeConstraint<type>("T")                    \
                              .TypeConstraint<int32>("Tidx")                \
                              .HostMemory("reduction_indices"),             \
                          ReductionOp<GPUDevice, type, int32,               \
                                      Eigen::internal::ProdReducer<type>>); \
  REGISTER_KERNEL_BUILDER(Name("Prod")                                      \
                              .Device(DEVICE_GPU)                           \
                              .TypeConstraint<type>("T")                    \
                              .TypeConstraint<int64>("Tidx")                \
                              .HostMemory("reduction_indices"),             \
                          ReductionOp<GPUDevice, type, int64,               \
                                      Eigen::internal::ProdReducer<type>>);
TF_CALL_int32(REGISTER_GPU_KERNELS_PROD);
TF_CALL_GPU_NUMBER_TYPES(REGISTER_GPU_KERNELS_PROD);

// TODO(itex): register complex64 and complex128 if the registration is
// supported
#ifdef ITEX_ENABLE_DOUBLE
TF_CALL_double(REGISTER_GPU_KERNELS_PROD);
#endif  // ITEX_ENABLE_DOUBLE

#undef REGISTER_GPU_KERNELS_PROD
}  // namespace itex
