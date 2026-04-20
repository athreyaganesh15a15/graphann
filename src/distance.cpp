#include "distance.h"

#include <cfloat>
#include <cmath>

// ============================================================================
// Exact float32 L2² distance
// ============================================================================

float compute_l2sq(const float* a, const float* b, uint32_t dim) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < dim; i++) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return sum;
}

// ============================================================================
// Optimization A: Asymmetric quantized L2² distance
// ============================================================================
// float32 query vs uint8 data with per-dimension min/scale dequantization.
// reconstructed[i] = quant[i] * scale[i] + min[i]

float compute_l2sq_asymmetric(const float* query, const uint8_t* quant,
                              const float* dim_min, const float* dim_scale,
                              uint32_t dim) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < dim; i++) {
        float reconstructed = quant[i] * dim_scale[i] + dim_min[i];
        float diff = query[i] - reconstructed;
        sum += diff * diff;
    }
    return sum;
}

// ============================================================================
// Optimization B: Early-abandoning exact L2²
// ============================================================================
// Checks running sum against threshold every 16 dimensions.
// Returns FLT_MAX if the partial sum already exceeds threshold.
// The 16-dim interval aligns with AVX2/AVX-512 register boundaries.

float compute_l2sq_ea(const float* a, const float* b, uint32_t dim,
                      float threshold) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < dim; i++) {
        float diff = a[i] - b[i];
        sum += diff * diff;
        // Check every 16 dimensions (SIMD-friendly boundary)
        if ((i & 15) == 15 && sum >= threshold) {
            return FLT_MAX;
        }
    }
    return sum;
}

// ============================================================================
// Optimization B: Early-abandoning asymmetric L2²
// ============================================================================
// Combines asymmetric dequantization with early-exit logic.

float compute_l2sq_asymmetric_ea(const float* query, const uint8_t* quant,
                                 const float* dim_min, const float* dim_scale,
                                 uint32_t dim, float threshold) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < dim; i++) {
        float reconstructed = quant[i] * dim_scale[i] + dim_min[i];
        float diff = query[i] - reconstructed;
        sum += diff * diff;
        // Check every 16 dimensions (SIMD-friendly boundary)
        if ((i & 15) == 15 && sum >= threshold) {
            return FLT_MAX;
        }
    }
    return sum;
}
