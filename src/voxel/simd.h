#pragma once

#include <cstddef>
#include <cstdint>

namespace directcraft::voxel {

enum class SimdBackend { Scalar, SSE2, AVX2 };

SimdBackend selectedSimdBackend();
const char* simdBackendName();
void zeroMask(std::uint8_t* data, std::size_t count);

} // namespace directcraft::voxel
