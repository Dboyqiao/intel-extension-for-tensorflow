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

#include "itex/core/kernels/gpu/stateless_random_ops_v2.h"
#include "itex/core/kernels/common/random_ops_util.h"
#include "itex/core/kernels/gpu/random_op_gpu.h"
#include "itex/core/kernels/gpu/stateless_random_ops.h"
#include "itex/core/utils/errors.h"
#include "itex/core/utils/lib/random/philox_random.h"
#include "itex/core/utils/lib/random/random_distributions.h"
#include "itex/core/utils/op_kernel.h"
#include "itex/core/utils/op_requires.h"
#include "itex/core/utils/plugin_tensor.h"
#include "itex/core/utils/register_types.h"
#include "itex/core/utils/tensor_shape.h"
#include "itex/core/utils/types.h"
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"

namespace itex {

typedef Eigen::GpuDevice GPUDevice;

template <typename T>
Status GetScalar(const Tensor& tensor, int input_idx, T* result) {
  auto dtype = DataTypeToEnum<T>::v();
  if (tensor.dims() != 0) {
    return errors::InvalidArgument("input ", std::to_string(input_idx),
                                   " (0-based) must have shape [], not ",
                                   tensor.shape().DebugString());
  }
  if (tensor.dtype() != dtype) {
    return errors::InvalidArgument("dtype of input ", std::to_string(input_idx),
                                   " (0-based) must be ", DataTypeString(dtype),
                                   ", not ", DataTypeString(tensor.dtype()));
  }
  *result = tensor.flat<T>()(0);
  return Status::OK();
}

class StatelessRandomOpBaseV2 : public OpKernel {
 public:
  explicit StatelessRandomOpBaseV2(OpKernelConstruction* ctx) : OpKernel(ctx) {}

  void Compute(OpKernelContext* ctx) override {
    // Sanitize input
    const Tensor& shape_t = ctx->input(0);
    const Tensor& key_t = ctx->input(1);
    const Tensor& counter_t = ctx->input(2);
    const int alg_input_idx = 3;
    const Tensor& alg_t = ctx->input(alg_input_idx);

    int alg_id;
    OP_REQUIRES_OK(ctx, GetScalar(alg_t, alg_input_idx, &alg_id));
    Algorithm alg = Algorithm(alg_id);
    if (alg == RNG_ALG_AUTO_SELECT) {
      alg = RNG_ALG_PHILOX;
    }

    TensorShape shape;
    OP_REQUIRES_OK(ctx, MakeShape(shape_t, &shape));
    OP_REQUIRES_OK(ctx,
                   CheckKeyCounterShape(alg, key_t.shape(), counter_t.shape()));

    // Allocate output
    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, shape, &output));
    if (shape.num_elements() == 0) {
      return;
    }

    // Fill in the random numbers
    Fill(ctx, alg, key_t, counter_t, output);
  }

  // The part of Compute that depends on device, type, and distribution
  virtual void Fill(OpKernelContext* ctx, Algorithm alg, const Tensor& key,
                    const Tensor& counter, Tensor* output) = 0;
};

template <typename Device, typename Distribution>
class StatelessRandomV2Op : public StatelessRandomOpBaseV2 {
 public:
  using StatelessRandomOpBaseV2::StatelessRandomOpBaseV2;

  void Fill(OpKernelContext* ctx, Algorithm alg, const Tensor& key,
            const Tensor& counter, Tensor* output) override {
    typedef typename Distribution::ResultElementType T;
    auto flat = output->flat<T>();
    if (alg == RNG_ALG_PHILOX) {
      // Reuse the compute kernels from the stateful random ops
      auto key_data = key.flat<uint64>().data();
      auto counter_data = counter.flat<uint64>().data();
      functor::FillPhiloxRandom<Device, Distribution>()(
          ctx, ctx->eigen_device<Device>(), random::PhiloxRandom() /*dummy*/,
          flat.data(), flat.size(), Distribution(), key_data, counter_data);
    } else {
      OP_REQUIRES(ctx, false,
                  errors::InvalidArgument("Unsupported algorithm id: ", alg));
    }
  }
};

template <typename Device, typename IntType>
class StatelessRandomUniformIntV2Op : public StatelessRandomOpBaseV2 {
 public:
  using StatelessRandomOpBaseV2::StatelessRandomOpBaseV2;

  void Fill(OpKernelContext* ctx, Algorithm alg, const Tensor& key,
            const Tensor& counter, Tensor* output) override {
    const Tensor& minval = ctx->input(4);
    const Tensor& maxval = ctx->input(5);
    OP_REQUIRES(ctx, TensorShapeUtils::IsScalar(minval.shape()),
                errors::InvalidArgument("minval must be 0-D, got shape ",
                                        minval.shape().DebugString()));
    OP_REQUIRES(ctx, TensorShapeUtils::IsScalar(maxval.shape()),
                errors::InvalidArgument("maxval must be 0-D, got shape ",
                                        maxval.shape().DebugString()));

    // Verify that minval < maxval.  Note that we'll never reach this point for
    // empty output.  Zero impossible things are fine.
    const auto lo = minval.scalar<IntType>()();
    const auto hi = maxval.scalar<IntType>()();
    OP_REQUIRES(
        ctx, lo < hi,
        errors::InvalidArgument("Need minval < maxval, got ", lo, " >= ", hi));

    // Build distribution
    typedef random::UniformDistribution<random::PhiloxRandom, IntType>
        Distribution;
    Distribution dist(lo, hi);

    auto flat = output->flat<IntType>();
    if (alg == RNG_ALG_PHILOX) {
      // Reuse the compute kernels from the stateful random ops
      auto key_data = key.flat<uint64>().data();
      auto counter_data = counter.flat<uint64>().data();
      functor::FillPhiloxRandom<Device, Distribution>()(
          ctx, ctx->eigen_device<Device>(), random::PhiloxRandom() /*dummy*/,
          flat.data(), flat.size(), dist, key_data, counter_data);
    } else {
      OP_REQUIRES(ctx, false,
                  errors::InvalidArgument("Unsupported algorithm id: ", alg));
    }
  }
};

template <typename Device, typename IntType>
class StatelessRandomUniformFullIntV2Op : public StatelessRandomOpBaseV2 {
 public:
  using StatelessRandomOpBaseV2::StatelessRandomOpBaseV2;

  void Fill(OpKernelContext* ctx, Algorithm alg, const Tensor& key,
            const Tensor& counter, Tensor* output) override {
    // Build distribution
    typedef random::UniformFullIntDistribution<random::PhiloxRandom, IntType>
        Distribution;
    Distribution dist;

    auto flat = output->flat<IntType>();
    if (alg == RNG_ALG_PHILOX) {
      // Reuse the compute kernels from the stateful random ops
      auto key_data = key.flat<uint64>().data();
      auto counter_data = counter.flat<uint64>().data();
      functor::FillPhiloxRandom<Device, Distribution>()(
          ctx, ctx->eigen_device<Device>(), random::PhiloxRandom() /*dummy*/,
          flat.data(), flat.size(), dist, key_data, counter_data);
    } else {
      OP_REQUIRES(ctx, false,
                  errors::InvalidArgument("Unsupported algorithm id: ", alg));
    }
  }
};

class GetKeyCounterAlgOp : public OpKernel {
 public:
  explicit GetKeyCounterAlgOp(OpKernelConstruction* ctx) : OpKernel(ctx) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& seed_t = ctx->input(0);
    OP_REQUIRES(ctx, seed_t.dims() == 1 && seed_t.dim_size(0) == 2,
                errors::InvalidArgument("seed must have shape [2], not ",
                                        seed_t.shape().DebugString()));
    // Allocate outputs
    Tensor* key_output;
    OP_REQUIRES_OK(
        ctx, ctx->allocate_output(0, TensorShape({RNG_KEY_SIZE}), &key_output));
    Tensor* counter_output;
    OP_REQUIRES_OK(ctx,
                   ctx->allocate_output(1, TensorShape({RNG_MAX_COUNTER_SIZE}),
                                        &counter_output));
    Tensor* alg_output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(2, TensorShape({}), &alg_output));

    random::PhiloxRandom::Key key;
    random::PhiloxRandom::ResultType counter;
    OP_REQUIRES_OK(ctx, GenerateKey(seed_t, &key, &counter));
    WriteKeyToMem(key, key_output->flat<uint64>().data());
    WriteCounterToMem(counter, counter_output->flat<uint64>().data());
    alg_output->flat<int>()(0) = RNG_ALG_PHILOX;
  }
};

class GetKeyCounterOp : public OpKernel {
 public:
  explicit GetKeyCounterOp(OpKernelConstruction* ctx) : OpKernel(ctx) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& seed_t = ctx->input(0);
    OP_REQUIRES(ctx, seed_t.dims() == 1 && seed_t.dim_size(0) == 2,
                errors::InvalidArgument("seed must have shape [2], not ",
                                        seed_t.shape().DebugString()));
    // Allocate outputs
    Tensor* key_output;
    OP_REQUIRES_OK(
        ctx, ctx->allocate_output(0, TensorShape({RNG_KEY_SIZE}), &key_output));
    Tensor* counter_output;
    OP_REQUIRES_OK(ctx,
                   ctx->allocate_output(1, TensorShape({RNG_MAX_COUNTER_SIZE}),
                                        &counter_output));

    random::PhiloxRandom::Key key;
    random::PhiloxRandom::ResultType counter;
    OP_REQUIRES_OK(ctx, GenerateKey(seed_t, &key, &counter));
    WriteKeyToMem(key, key_output->flat<uint64>().data());
    WriteCounterToMem(counter, counter_output->flat<uint64>().data());
  }
};

class GetAlgOp : public OpKernel {
 public:
  explicit GetAlgOp(OpKernelConstruction* ctx) : OpKernel(ctx) {}

  void Compute(OpKernelContext* ctx) override {
    Tensor* alg_output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape({}), &alg_output));
    alg_output->flat<int>()(0) = RNG_ALG_PHILOX;
  }
};

#define REGISTER(DEVICE, TYPE)                                                \
  REGISTER_KERNEL_BUILDER(                                                    \
      Name("StatelessRandomUniformV2")                                        \
          .Device(DEVICE_##DEVICE)                                            \
          .HostMemory("shape")                                                \
          .HostMemory("alg")                                                  \
          .TypeConstraint<TYPE>("dtype"),                                     \
      StatelessRandomV2Op<DEVICE##Device, random::UniformDistribution<        \
                                              random::PhiloxRandom, TYPE> >); \
  REGISTER_KERNEL_BUILDER(                                                    \
      Name("StatelessRandomNormalV2")                                         \
          .Device(DEVICE_##DEVICE)                                            \
          .HostMemory("shape")                                                \
          .HostMemory("alg")                                                  \
          .TypeConstraint<TYPE>("dtype"),                                     \
      StatelessRandomV2Op<DEVICE##Device, random::NormalDistribution<         \
                                              random::PhiloxRandom, TYPE> >); \
  REGISTER_KERNEL_BUILDER(                                                    \
      Name("StatelessTruncatedNormalV2")                                      \
          .Device(DEVICE_##DEVICE)                                            \
          .HostMemory("shape")                                                \
          .HostMemory("alg")                                                  \
          .TypeConstraint<TYPE>("dtype"),                                     \
      StatelessRandomV2Op<                                                    \
          DEVICE##Device,                                                     \
          random::TruncatedNormalDistribution<                                \
              random::SingleSampleAdapter<random::PhiloxRandom>, TYPE> >)

#define REGISTER_FULL_INT(DEVICE, TYPE)       \
  REGISTER_KERNEL_BUILDER(                    \
      Name("StatelessRandomUniformFullIntV2") \
          .Device(DEVICE_##DEVICE)            \
          .HostMemory("shape")                \
          .HostMemory("alg")                  \
          .TypeConstraint<TYPE>("dtype"),     \
      StatelessRandomUniformFullIntV2Op<DEVICE##Device, TYPE>)

#define REGISTER_INT(DEVICE, TYPE)                            \
  REGISTER_FULL_INT(DEVICE, TYPE);                            \
  REGISTER_KERNEL_BUILDER(Name("StatelessRandomUniformIntV2") \
                              .Device(DEVICE_##DEVICE)        \
                              .HostMemory("shape")            \
                              .HostMemory("alg")              \
                              .HostMemory("minval")           \
                              .HostMemory("maxval")           \
                              .TypeConstraint<TYPE>("dtype"), \
                          StatelessRandomUniformIntV2Op<DEVICE##Device, TYPE>)

#define REGISTER_GPU(TYPE) REGISTER(GPU, TYPE)
#define REGISTER_INT_GPU(TYPE) REGISTER_INT(GPU, TYPE)
#define REGISTER_FULL_INT_GPU(TYPE) REGISTER_FULL_INT(GPU, TYPE)

#define REGISTER_GET_KCA(DEVICE)                                               \
  REGISTER_KERNEL_BUILDER(Name("StatelessRandomGetKeyCounterAlg")              \
                              .Device(DEVICE_##DEVICE)                         \
                              .HostMemory("seed")                              \
                              .HostMemory("key")                               \
                              .HostMemory("counter")                           \
                              .HostMemory("alg"),                              \
                          GetKeyCounterAlgOp)                                  \
  REGISTER_KERNEL_BUILDER(Name("StatelessRandomGetKeyCounter")                 \
                              .Device(DEVICE_##DEVICE)                         \
                              .HostMemory("seed")                              \
                              .HostMemory("key")                               \
                              .HostMemory("counter"),                          \
                          GetKeyCounterOp)                                     \
  REGISTER_KERNEL_BUILDER(                                                     \
      Name("StatelessRandomGetAlg").Device(DEVICE_##DEVICE).HostMemory("alg"), \
      GetAlgOp)

TF_CALL_half(REGISTER_GPU);
TF_CALL_bfloat16(REGISTER_GPU);
TF_CALL_float(REGISTER_GPU);
#ifdef ITEX_ENABLE_DOUBLE
TF_CALL_double(REGISTER_GPU);
#endif
TF_CALL_int32(REGISTER_INT_GPU);
TF_CALL_int64(REGISTER_INT_GPU);
TF_CALL_uint32(REGISTER_FULL_INT_GPU);
TF_CALL_uint64(REGISTER_FULL_INT_GPU);

REGISTER_GET_KCA(GPU);

#undef REGISTER
#undef REGISTER_INT
#undef REGISTER_GPU
#undef REGISTER_INT_GPU
#undef REGISTER_FULL_INT_GPU

#undef REGISTER_GET_KCA

}  // namespace itex
