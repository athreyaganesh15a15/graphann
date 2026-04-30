#include "distance.h"

#include <cfloat>
#include <cmath>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

// ============================================================================
// Exact float32 L2² distance
// ============================================================================

float compute_l2sq(const float* a, const float* b, uint32_t dim) {
#ifdef __ARM_NEON
    float32x4_t sum0 = vdupq_n_f32(0.0f);
    float32x4_t sum1 = vdupq_n_f32(0.0f);
    float32x4_t sum2 = vdupq_n_f32(0.0f);
    float32x4_t sum3 = vdupq_n_f32(0.0f);

    uint32_t i = 0;
    // Process 16 floats per iteration (4 NEON registers × 4 lanes)
    for (; i + 16 <= dim; i += 16) {
        float32x4_t diff0 = vsubq_f32(vld1q_f32(a + i),      vld1q_f32(b + i));
        float32x4_t diff1 = vsubq_f32(vld1q_f32(a + i + 4),  vld1q_f32(b + i + 4));
        float32x4_t diff2 = vsubq_f32(vld1q_f32(a + i + 8),  vld1q_f32(b + i + 8));
        float32x4_t diff3 = vsubq_f32(vld1q_f32(a + i + 12), vld1q_f32(b + i + 12));
        sum0 = vfmaq_f32(sum0, diff0, diff0);
        sum1 = vfmaq_f32(sum1, diff1, diff1);
        sum2 = vfmaq_f32(sum2, diff2, diff2);
        sum3 = vfmaq_f32(sum3, diff3, diff3);
    }

    sum0 = vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3));
    float result = vaddvq_f32(sum0);

    // Handle remaining dimensions
    for (; i < dim; i++) {
        float diff = a[i] - b[i];
        result += diff * diff;
    }
    return result;
#else
    float sum = 0.0f;
    for (uint32_t i = 0; i < dim; i++) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return sum;
#endif
}

// ============================================================================
// Optimization A: Asymmetric quantized L2² distance
// ============================================================================
// float32 query vs uint8 data with per-dimension min/scale dequantization.
// reconstructed[i] = quant[i] * scale[i] + min[i]

float compute_l2sq_asymmetric(const float* query, const uint8_t* quant,
                              const float* dim_min, const float* dim_scale,
                              uint32_t dim) {
#ifdef __ARM_NEON
    float32x4_t sum0 = vdupq_n_f32(0.0f);
    float32x4_t sum1 = vdupq_n_f32(0.0f);
    float32x4_t sum2 = vdupq_n_f32(0.0f);
    float32x4_t sum3 = vdupq_n_f32(0.0f);

    uint32_t i = 0;
    // Process 16 uint8 values per iteration
    for (; i + 16 <= dim; i += 16) {
        uint8x16_t raw = vld1q_u8(quant + i);
        uint16x8_t lo16 = vmovl_u8(vget_low_u8(raw));
        uint16x8_t hi16 = vmovl_u8(vget_high_u8(raw));

        // Group 0: dims [i..i+3]
        float32x4_t qf0 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(lo16)));
        float32x4_t recon0 = vfmaq_f32(vld1q_f32(dim_min + i), qf0, vld1q_f32(dim_scale + i));
        float32x4_t diff0 = vsubq_f32(vld1q_f32(query + i), recon0);
        sum0 = vfmaq_f32(sum0, diff0, diff0);

        // Group 1: dims [i+4..i+7]
        float32x4_t qf1 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(lo16)));
        float32x4_t recon1 = vfmaq_f32(vld1q_f32(dim_min + i + 4), qf1, vld1q_f32(dim_scale + i + 4));
        float32x4_t diff1 = vsubq_f32(vld1q_f32(query + i + 4), recon1);
        sum1 = vfmaq_f32(sum1, diff1, diff1);

        // Group 2: dims [i+8..i+11]
        float32x4_t qf2 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(hi16)));
        float32x4_t recon2 = vfmaq_f32(vld1q_f32(dim_min + i + 8), qf2, vld1q_f32(dim_scale + i + 8));
        float32x4_t diff2 = vsubq_f32(vld1q_f32(query + i + 8), recon2);
        sum2 = vfmaq_f32(sum2, diff2, diff2);

        // Group 3: dims [i+12..i+15]
        float32x4_t qf3 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(hi16)));
        float32x4_t recon3 = vfmaq_f32(vld1q_f32(dim_min + i + 12), qf3, vld1q_f32(dim_scale + i + 12));
        float32x4_t diff3 = vsubq_f32(vld1q_f32(query + i + 12), recon3);
        sum3 = vfmaq_f32(sum3, diff3, diff3);
    }

    sum0 = vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3));
    float result = vaddvq_f32(sum0);

    for (; i < dim; i++) {
        float reconstructed = quant[i] * dim_scale[i] + dim_min[i];
        float diff = query[i] - reconstructed;
        result += diff * diff;
    }
    return result;
#else
    float sum = 0.0f;
    for (uint32_t i = 0; i < dim; i++) {
        float reconstructed = quant[i] * dim_scale[i] + dim_min[i];
        float diff = query[i] - reconstructed;
        sum += diff * diff;
    }
    return sum;
#endif
}

// ============================================================================
// Optimization B: Early-abandoning exact L2²
// ============================================================================
// Checks running sum against threshold every 16 dimensions.
// Returns FLT_MAX if the partial sum already exceeds threshold.
// The 16-dim interval aligns with NEON register boundaries.

float compute_l2sq_ea(const float* a, const float* b, uint32_t dim,
                      float threshold) {
#ifdef __ARM_NEON
    float32x4_t sum0 = vdupq_n_f32(0.0f);
    float32x4_t sum1 = vdupq_n_f32(0.0f);
    float32x4_t sum2 = vdupq_n_f32(0.0f);
    float32x4_t sum3 = vdupq_n_f32(0.0f);

    uint32_t i = 0;
    for (; i + 16 <= dim; i += 16) {
        float32x4_t diff0 = vsubq_f32(vld1q_f32(a + i),      vld1q_f32(b + i));
        float32x4_t diff1 = vsubq_f32(vld1q_f32(a + i + 4),  vld1q_f32(b + i + 4));
        float32x4_t diff2 = vsubq_f32(vld1q_f32(a + i + 8),  vld1q_f32(b + i + 8));
        float32x4_t diff3 = vsubq_f32(vld1q_f32(a + i + 12), vld1q_f32(b + i + 12));
        sum0 = vfmaq_f32(sum0, diff0, diff0);
        sum1 = vfmaq_f32(sum1, diff1, diff1);
        sum2 = vfmaq_f32(sum2, diff2, diff2);
        sum3 = vfmaq_f32(sum3, diff3, diff3);

        // Early-abandon check every 16 dims
        float32x4_t total = vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3));
        if (vaddvq_f32(total) >= threshold) return FLT_MAX;
    }

    float32x4_t total = vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3));
    float result = vaddvq_f32(total);

    for (; i < dim; i++) {
        float diff = a[i] - b[i];
        result += diff * diff;
    }
    return result;
#else
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
#endif
}

// ============================================================================
// Optimization B: Early-abandoning asymmetric L2²
// ============================================================================
// Combines asymmetric dequantization with early-exit logic.

float compute_l2sq_asymmetric_ea(const float* query, const uint8_t* quant,
                                 const float* dim_min, const float* dim_scale,
                                 uint32_t dim, float threshold) {
#ifdef __ARM_NEON
    float32x4_t sum0 = vdupq_n_f32(0.0f);
    float32x4_t sum1 = vdupq_n_f32(0.0f);
    float32x4_t sum2 = vdupq_n_f32(0.0f);
    float32x4_t sum3 = vdupq_n_f32(0.0f);

    uint32_t i = 0;
    for (; i + 16 <= dim; i += 16) {
        uint8x16_t raw = vld1q_u8(quant + i);
        uint16x8_t lo16 = vmovl_u8(vget_low_u8(raw));
        uint16x8_t hi16 = vmovl_u8(vget_high_u8(raw));

        float32x4_t qf0 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(lo16)));
        float32x4_t recon0 = vfmaq_f32(vld1q_f32(dim_min + i), qf0, vld1q_f32(dim_scale + i));
        float32x4_t diff0 = vsubq_f32(vld1q_f32(query + i), recon0);
        sum0 = vfmaq_f32(sum0, diff0, diff0);

        float32x4_t qf1 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(lo16)));
        float32x4_t recon1 = vfmaq_f32(vld1q_f32(dim_min + i + 4), qf1, vld1q_f32(dim_scale + i + 4));
        float32x4_t diff1 = vsubq_f32(vld1q_f32(query + i + 4), recon1);
        sum1 = vfmaq_f32(sum1, diff1, diff1);

        float32x4_t qf2 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(hi16)));
        float32x4_t recon2 = vfmaq_f32(vld1q_f32(dim_min + i + 8), qf2, vld1q_f32(dim_scale + i + 8));
        float32x4_t diff2 = vsubq_f32(vld1q_f32(query + i + 8), recon2);
        sum2 = vfmaq_f32(sum2, diff2, diff2);

        float32x4_t qf3 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(hi16)));
        float32x4_t recon3 = vfmaq_f32(vld1q_f32(dim_min + i + 12), qf3, vld1q_f32(dim_scale + i + 12));
        float32x4_t diff3 = vsubq_f32(vld1q_f32(query + i + 12), recon3);
        sum3 = vfmaq_f32(sum3, diff3, diff3);

        // Early-abandon check every 16 dims
        float32x4_t total = vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3));
        if (vaddvq_f32(total) >= threshold) return FLT_MAX;
    }

    float32x4_t total = vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3));
    float result = vaddvq_f32(total);

    for (; i < dim; i++) {
        float reconstructed = quant[i] * dim_scale[i] + dim_min[i];
        float diff = query[i] - reconstructed;
        result += diff * diff;
    }
    return result;
#else
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
#endif
}
