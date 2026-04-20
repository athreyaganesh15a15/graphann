#pragma once

#include <cstdint>

// Squared L2 (Euclidean) distance between two float vectors.
// No sqrt — monotonic, so rankings are preserved.
// Compiler auto-vectorizes this with -O3 -march=native.
float compute_l2sq(const float* a, const float* b, uint32_t dim);

// --- Optimization A: Asymmetric quantized L2² ---
// Computes L2² between a float32 query and a uint8 quantized vector.
// Uses per-dimension min/scale to dequantize on-the-fly:
//   reconstructed[i] = quant[i] * scale[i] + min[i]
//   diff = query[i] - reconstructed[i]
float compute_l2sq_asymmetric(const float* query, const uint8_t* quant,
                              const float* dim_min, const float* dim_scale,
                              uint32_t dim);

// --- Optimization B: Early-abandoning variants ---
// Same as compute_l2sq, but aborts early if the running sum exceeds
// `threshold` at any 16-dimension checkpoint. Returns FLT_MAX on abort.
float compute_l2sq_ea(const float* a, const float* b, uint32_t dim,
                      float threshold);

// Early-abandoning asymmetric variant.
float compute_l2sq_asymmetric_ea(const float* query, const uint8_t* quant,
                                 const float* dim_min, const float* dim_scale,
                                 uint32_t dim, float threshold);
