#include "voxel/simd.h"

#if defined(_MSC_VER)
#include <intrin.h>
#include <immintrin.h>
#endif

namespace directcraft::voxel {

SimdBackend selectedSimdBackend() {
#if defined(_MSC_VER) && defined(_M_X64)
    int registers[4]{};
    __cpuid(registers, 0);
    if (registers[0] >= 7) {
        __cpuidex(registers, 7, 0);
        if ((registers[1] & (1 << 5)) != 0) return SimdBackend::AVX2;
    }
    return SimdBackend::SSE2;
#else
    return SimdBackend::Scalar;
#endif
}

const char* simdBackendName() {
    switch (selectedSimdBackend()) {
    case SimdBackend::AVX2: return "AVX2";
    case SimdBackend::SSE2: return "SSE2";
    default: return "Scalar";
    }
}

void zeroMask(std::uint8_t* data, std::size_t count) {
#if defined(_MSC_VER) && defined(_M_X64)
    if (selectedSimdBackend() == SimdBackend::AVX2) {
        const __m256i zero = _mm256_setzero_si256();
        while (count >= 32) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(data), zero);
            data += 32;
            count -= 32;
        }
    } else {
        const __m128i zero = _mm_setzero_si128();
        while (count >= 16) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(data), zero);
            data += 16;
            count -= 16;
        }
    }
#endif
    while (count-- > 0) *data++ = 0;
}

} // namespace directcraft::voxel
