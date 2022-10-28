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

#ifndef ITEX_CORE_KERNELS_GPU_PAD_OP_H_
#define ITEX_CORE_KERNELS_GPU_PAD_OP_H_

#include "itex/core/utils/tensor_types.h"
#include "itex/core/utils/types.h"

namespace itex {
namespace functor {

// Functor used by PadOp to do the computations.
template <typename Device, typename T, typename Tpadding, int Dims>
struct Pad {
  // Pad "input" into "output", as specified by "paddings" and "pad_value".
  // See pad_op.cc for details.
  void operator()(const Device& d, typename TTypes<T, Dims>::Tensor output,
                  typename TTypes<T, Dims>::ConstTensor input,
                  Eigen::array<Eigen::IndexPair<Tpadding>, Dims> paddings,
                  T pad_value) {
    output.device(d) = input.pad(paddings, pad_value);
  }
};

template <typename Device, typename T, typename Tpadding>
struct Pad<Device, T, Tpadding, 0> {
  // In the scalar case we simply copy the input.
  void operator()(const Device& d, typename TTypes<T, 0>::Tensor output,
                  typename TTypes<T, 0>::ConstTensor input,
                  Eigen::array<Eigen::IndexPair<Tpadding>, 0>, T) {
    output.device(d) = input;
  }
};
}  // namespace functor
}  // namespace itex

#endif  // ITEX_CORE_KERNELS_GPU_PAD_OP_H_