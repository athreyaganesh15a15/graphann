#include "distance.h"

// ============================================================================
// SIMD-optimized squared L2 distance
// ============================================================================
// Processes 8 floats/cycle (AVX2) or 4 floats/cycle (SSE4.1), with a
// scalar tail loop for remaining elements.  -march=native selects the
// best path at compile time.

#if defined(__AVX2__) || defined(__AVX__)
// ---- AVX2 / AVX path: 8 floats at a time ----
#include <immintrin.h>

float compute_l2sq(const float* a, const float* b, uint32_t dim) {
    __m256 sum8 = _mm256_setzero_ps();

    uint32_t i = 0;
    // Main loop: process 16 floats per iteration (2x unrolled)
    for (; i + 15 < dim; i += 16) {
        __m256 va0 = _mm256_loadu_ps(a + i);
        __m256 vb0 = _mm256_loadu_ps(b + i);
        __m256 d0  = _mm256_sub_ps(va0, vb0);
        sum8 = _mm256_fmadd_ps(d0, d0, sum8);

        __m256 va1 = _mm256_loadu_ps(a + i + 8);
        __m256 vb1 = _mm256_loadu_ps(b + i + 8);
        __m256 d1  = _mm256_sub_ps(va1, vb1);
        sum8 = _mm256_fmadd_ps(d1, d1, sum8);
    }
    // Process remaining 8-float chunks
    for (; i + 7 < dim; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        __m256 d  = _mm256_sub_ps(va, vb);
        sum8 = _mm256_fmadd_ps(d, d, sum8);
    }

    // Horizontal sum of 8 floats -> scalar
    // sum8 = [s0, s1, s2, s3, s4, s5, s6, s7]
    __m128 lo = _mm256_castps256_ps128(sum8);
    __m128 hi = _mm256_extractf128_ps(sum8, 1);
    __m128 sum4 = _mm_add_ps(lo, hi);
    // sum4 = [s0+s4, s1+s5, s2+s6, s3+s7]
    __m128 shuf = _mm_movehdup_ps(sum4);         // [s1+s5, s1+s5, s3+s7, s3+s7]
    __m128 sums = _mm_add_ps(sum4, shuf);        // [s0+s4+s1+s5, -, s2+s6+s3+s7, -]
    shuf = _mm_movehl_ps(shuf, sums);            // [s2+s6+s3+s7, -, -, -]
    sums = _mm_add_ss(sums, shuf);               // total in element 0
    float result = _mm_cvtss_f32(sums);

    // Scalar tail for remaining elements
    for (; i < dim; i++) {
        float diff = a[i] - b[i];
        result += diff * diff;
    }
    return result;
}

#elif defined(__SSE4_1__) || defined(__SSE2__)
// ---- SSE path: 4 floats at a time ----
#include <xmmintrin.h>
#include <smmintrin.h>

float compute_l2sq(const float* a, const float* b, uint32_t dim) {
    __m128 sum4 = _mm_setzero_ps();

    uint32_t i = 0;
    // Main loop: process 8 floats per iteration (2x unrolled)
    for (; i + 7 < dim; i += 8) {
        __m128 va0 = _mm_loadu_ps(a + i);
        __m128 vb0 = _mm_loadu_ps(b + i);
        __m128 d0  = _mm_sub_ps(va0, vb0);
        sum4 = _mm_add_ps(sum4, _mm_mul_ps(d0, d0));

        __m128 va1 = _mm_loadu_ps(a + i + 4);
        __m128 vb1 = _mm_loadu_ps(b + i + 4);
        __m128 d1  = _mm_sub_ps(va1, vb1);
        sum4 = _mm_add_ps(sum4, _mm_mul_ps(d1, d1));
    }
    for (; i + 3 < dim; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        __m128 d  = _mm_sub_ps(va, vb);
        sum4 = _mm_add_ps(sum4, _mm_mul_ps(d, d));
    }

    // Horizontal sum of 4 floats
    __m128 shuf = _mm_movehdup_ps(sum4);
    __m128 sums = _mm_add_ps(sum4, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);
    float result = _mm_cvtss_f32(sums);

    // Scalar tail
    for (; i < dim; i++) {
        float diff = a[i] - b[i];
        result += diff * diff;
    }
    return result;
}

#else
// ---- Scalar fallback ----
float compute_l2sq(const float* a, const float* b, uint32_t dim) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < dim; i++) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return sum;
}
#endif
