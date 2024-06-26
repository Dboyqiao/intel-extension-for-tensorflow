/* Copyright (c) 2021-2022 Intel Corporation

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

#include "third_party/build_option/dpcpp/runtime/eigen_dpcpp_runtime.h"

#include <cassert>
#include <iostream>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "itex/core/utils/logging.h"

#define REQUIRE_SUCCESS(func)                                               \
  do {                                                                      \
    dpcppError_t error = func;                                              \
    if (error != DPCPP_SUCCESS) {                                           \
      ITEX_LOG(ERROR) << "Error call the function " << #func << " because " \
                      << dpruntimeGetErrorName(error);                      \
      return error;                                                         \
    }                                                                       \
  } while (0)

dpcppError_t dpruntimeGetDeviceCount(int* count) {
  return dpcppGetDeviceCount(count);
}

dpcppError_t dpruntimeGetCurrentDevice(DeviceOrdinal* device) {
  return dpcppGetCurrentDeviceOrdinal(device);
}

dpcppError_t dpruntimeGetDevice(DeviceOrdinal* device, int device_ordinal) {
  *device = device_ordinal;
  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeGetRealDPCPPDevice(DPCPPDevice* device,
                                         int device_ordinal) {
  DPCPPDevice* real_device;
  REQUIRE_SUCCESS(dpcppGetDevice(&real_device, device_ordinal));
  *device = *real_device;
  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeSetDevice(int device_ordinal) {
  return dpcppSetCurrentDeviceOrdinal(device_ordinal);
}

static dpcppError_t getCurrentDevice(DPCPPDevice** device) {
  DeviceOrdinal ordinal;
  REQUIRE_SUCCESS(dpcppGetCurrentDeviceOrdinal(&ordinal));
  REQUIRE_SUCCESS(dpcppGetDevice(device, ordinal));
  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeCreateStream(DPCPPStream** stream) {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  return dpcppCreateStream(device, stream);
}

dpcppError_t dpruntimeDestroyStream(DPCPPStream* stream) {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  return dpcppDestroyStream(device, stream);
}

dpcppError_t dpruntimeStreamWaitEvent(DPCPPStream* stream, DPCPPEvent* event) {
  return dpcppStreamWaitEvent(stream, event);
}

dpcppError_t dpruntimeCtxSynchronize() {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  return dpcppCtxSynchronize(device);
}

dpcppError_t dpruntimeStreamSynchronize(DPCPPStream* stream) {
  return dpcppStreamSynchronize(stream);
}

dpcppError_t dpruntimeMemcpyDtoH(void* dstHost, const void* srcDevice,
                                 size_t ByteCount) {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  REQUIRE_SUCCESS(dpcppMemcpyDtoH(dstHost, srcDevice, ByteCount, device));
  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemcpyHtoD(void* dstDevice, const void* srcHost,
                                 size_t ByteCount) {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  REQUIRE_SUCCESS(dpcppMemcpyHtoD(dstDevice, srcHost, ByteCount, device));

  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemcpyDtoD(void* dstDevice, const void* srcDevice,
                                 size_t ByteCount) {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  REQUIRE_SUCCESS(dpcppMemcpyDtoD(dstDevice, srcDevice, ByteCount, device));

  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemcpyDtoHAsync(void* dstHost, const void* srcDevice,
                                      size_t ByteCount, DPCPPStream* stream) {
  REQUIRE_SUCCESS(dpcppMemcpyDtoHAsync(dstHost, srcDevice, ByteCount, stream));

  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemcpyHtoDAsync(void* dstDevice, const void* srcHost,
                                      size_t ByteCount, DPCPPStream* stream) {
  REQUIRE_SUCCESS(dpcppMemcpyHtoDAsync(dstDevice, srcHost, ByteCount, stream));

  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemcpyDtoDAsync(void* dstDevice, const void* srcDevice,
                                      size_t ByteCount, DPCPPStream* stream) {
  REQUIRE_SUCCESS(
      dpcppMemcpyDtoDAsync(dstDevice, srcDevice, ByteCount, stream));

  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemsetD8(void* dstDevice, unsigned char uc, size_t N) {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  REQUIRE_SUCCESS(dpcppMemsetD8(dstDevice, uc, N, device));
  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemsetD8Async(void* dstDevice, unsigned char uc, size_t N,
                                    DPCPPStream* stream) {
  REQUIRE_SUCCESS(dpcppMemsetD8Async(dstDevice, uc, N, stream));
  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemsetD32(void* dstDevice, unsigned int ui, size_t N) {
  DPCPPDevice* device;
  REQUIRE_SUCCESS(getCurrentDevice(&device));
  REQUIRE_SUCCESS(dpcppMemsetD32(dstDevice, ui, N, device));

  return DPCPP_SUCCESS;
}

dpcppError_t dpruntimeMemsetD32Async(void* dstDevice, unsigned int ui, size_t N,
                                     DPCPPStream* stream) {
  REQUIRE_SUCCESS(dpcppMemsetD32Async(dstDevice, ui, N, stream));
  return DPCPP_SUCCESS;
}

void* dpruntimeMalloc(size_t ByteCount) {
  DPCPPDevice* device;
  if (getCurrentDevice(&device) != DPCPP_SUCCESS) {
    return nullptr;
  }

  return dpcppMalloc(device, ByteCount);
}

void dpruntimeFree(void* ptr) {
  DPCPPDevice* device;
  if (getCurrentDevice(&device) != DPCPP_SUCCESS) {
    return;
  }

  dpcppFree(device, ptr);
}

const char* dpruntimeGetErrorName(dpcppError_t error) {
  return dpcppGetErrorName(error);
}

#undef REQUIRE_SUCCESS
