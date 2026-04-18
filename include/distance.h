#pragma once

#include <cstdint>

// Squared L2 (Euclidean) distance between two float vectors.
// No sqrt — monotonic, so rankings are preserved.
//
// Uses SIMD intrinsics (AVX2 / SSE4.1) when available, with a scalar
// fallback.  Compiled with -O3 -march=native, the best available path
// is selected at compile time via preprocessor guards.
float compute_l2sq(const float* a, const float* b, uint32_t dim);
