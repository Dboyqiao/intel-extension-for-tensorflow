/* Copyright (c) 2021-2022 Intel Corporation

Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#include "itex/core/kernels/gpu/linalg/einsum_op_impl.h"

namespace itex {

Status ParseEinsumEquation(const string& equation,
                           gtl::InlinedVector<string, 2>* input_subscripts,
                           string* output_subscript) {
  gtl::InlinedVector<string, 2> inputs_and_output_subscripts =
      absl::StrSplit(equation, "->");
  if (inputs_and_output_subscripts.size() != 2) {
    return errors::InvalidArgument(
        "Expecting exactly one '->' in einsum equation: ", equation);
  }
  *output_subscript = std::move(inputs_and_output_subscripts[1]);
  *input_subscripts =
      absl::StrSplit(std::move(inputs_and_output_subscripts[0]), ',');
  if (input_subscripts->size() != 1 && input_subscripts->size() != 2) {
    return errors::InvalidArgument(
        "Expecting 1 or 2 input subscripts in equation '", equation,
        "' but got: ", input_subscripts->size());
  }
  return Status::OK();
}

#define REGISTER_EINSUM(D, TYPE)                                   \
  REGISTER_KERNEL_BUILDER(                                         \
      Name("Einsum").Device(DEVICE_##D).TypeConstraint<TYPE>("T"), \
      EinsumOp<D##Device, TYPE>);

#define REGISTER_GPU(TYPE) REGISTER_EINSUM(GPU, TYPE)
TF_CALL_bfloat16(REGISTER_GPU);
TF_CALL_float(REGISTER_GPU);
TF_CALL_half(REGISTER_GPU);
#undef REGISTER_GPU

#undef REGISTER_EINSUM

}  // namespace itex
