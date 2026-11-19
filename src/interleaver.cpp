#include "interleaver.h"

#include <stdexcept>

#include "reed_solomon.h"

// write the data into a matrix row by row, read it out column
// by column. One row per Reed-Solomon codeword, so consecutive transmitted bytes
// alternate between codewords and a burst of noise is split evenly across them.
constexpr size_t kRows = reed_solomon::kNumBlocks;

// Number of matrix columns (bytes per row) for this input
static size_t columns(const std::vector<uint8_t>& data) {
    if (data.size() % kRows != 0) {
        throw std::length_error("interleaver input must be a multiple of the row count");
    }
    return data.size() / kRows;
}

std::vector<uint8_t> interleaver::encode(const std::vector<uint8_t>& data) {
    const size_t cols = columns(data);
    std::vector<uint8_t> out(data.size());
    // Read row-major input, write column-major output
    for (size_t r = 0; r < kRows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            out[c * kRows + r] = data[r * cols + c];
        }
    }
    return out;
}

std::vector<uint8_t> interleaver::decode(const std::vector<uint8_t>& data) {
    const size_t cols = columns(data);
    std::vector<uint8_t> out(data.size());
    // Inverse of encode: read column-major input, write row-major output
    for (size_t r = 0; r < kRows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            out[r * cols + c] = data[c * kRows + r];
        }
    }
    return out;
}
