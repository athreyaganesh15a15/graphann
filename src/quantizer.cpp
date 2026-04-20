#include "quantizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

// ============================================================================
// Train: compute per-dimension min and scale from the dataset
// ============================================================================

void ScalarQuantizer::train(const float* data, uint32_t npts, uint32_t d) {
    dim = d;
    min_.resize(dim);
    scale_.resize(dim);

    std::vector<float> max_(dim);

    // Initialize with first point
    for (uint32_t j = 0; j < dim; j++) {
        min_[j] = data[j];
        max_[j] = data[j];
    }

    // Single pass: track per-dimension min and max
    for (uint32_t i = 1; i < npts; i++) {
        const float* row = data + (size_t)i * dim;
        for (uint32_t j = 0; j < dim; j++) {
            if (row[j] < min_[j]) min_[j] = row[j];
            if (row[j] > max_[j]) max_[j] = row[j];
        }
    }

    // Derive scale: map [min, max] → [0, 255]
    for (uint32_t j = 0; j < dim; j++) {
        float range = max_[j] - min_[j];
        if (range < 1e-10f) {
            // Degenerate dimension: all values identical
            scale_[j] = 1.0f;
        } else {
            scale_[j] = range / 255.0f;
        }
    }
}

// ============================================================================
// Quantize: convert float32 dataset → uint8 using trained min/scale
// ============================================================================

std::vector<uint8_t> ScalarQuantizer::quantize(const float* data, uint32_t npts) const {
    std::vector<uint8_t> quant(static_cast<size_t>(npts) * dim);

    for (uint32_t i = 0; i < npts; i++) {
        const float* row = data + (size_t)i * dim;
        uint8_t* qrow = quant.data() + (size_t)i * dim;
        for (uint32_t j = 0; j < dim; j++) {
            float normalized = (row[j] - min_[j]) / scale_[j];
            // Clamp to [0, 255] and round
            int val = static_cast<int>(std::round(normalized));
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            qrow[j] = static_cast<uint8_t>(val);
        }
    }

    return quant;
}
