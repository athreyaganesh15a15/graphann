#pragma once

#include <cstdint>
#include <vector>
#include <string>

// Per-dimension scalar quantizer: float32 → uint8.
// For each dimension d: quant[d] = round((val[d] - min_[d]) / scale_[d])
// Reconstruction:        recon[d] = quant[d] * scale_[d] + min_[d]
//
// Memory: ~128 MB for SIFT1M (1M × 128 × 1 byte) vs ~488 MB float32.
// Metadata overhead: 2 × dim × 4 bytes ≈ 1 KB.
struct ScalarQuantizer {
    uint32_t dim = 0;
    std::vector<float> min_;      // [dim] per-dimension minimum
    std::vector<float> scale_;    // [dim] per-dimension scale = (max-min)/255

    // Train on dataset: compute per-dim min/max, derive scale.
    void train(const float* data, uint32_t npts, uint32_t dim);

    // Quantize entire dataset. Returns flat uint8 array [npts × dim].
    std::vector<uint8_t> quantize(const float* data, uint32_t npts) const;

    // Get quantized row pointer (convenience for flat array).
    const uint8_t* get_row(const uint8_t* quant_data, uint32_t id) const {
        return quant_data + (size_t)id * dim;
    }
};
