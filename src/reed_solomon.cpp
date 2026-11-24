// File Name:     reed_solomon.cpp
// Author:        Jagger Winkler
// Date:          November 2026

// Description: padding frames, encoding them into Reed-Solomon codewords, and correcting
// up to 16 bad bytes per codeword on decode

#include "reed_solomon.h"

#include <stdexcept>

// correct.h is a C header without extern "C" guards
extern "C" {
#include <correct.h>
}

reed_solomon::reed_solomon()
    : rs(correct_reed_solomon_create(correct_rs_primitive_polynomial_8_4_3_2_0, // field polynomial 0x11d
                                     1,             // first consecutive root
                                     1,             // generator root gap
                                     kParityLen)) {}

reed_solomon::~reed_solomon() {
    correct_reed_solomon_destroy(rs);
}

std::vector<uint8_t> reed_solomon::encode(const std::vector<uint8_t>& frame) {
    if (frame.size() > kFrameLen) {
        throw std::length_error("frame exceeds 272 byte max size"); //this is a fault in the data link layer
    }
    std::vector<uint8_t> padded = frame;
    padded.resize(kFrameLen, 0);
    std::vector<uint8_t> out(kEncodedLen);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        correct_reed_solomon_encode(rs, padded.data() + b * kBlockLen, kBlockLen, out.data() + b * kCodewordLen);
    }
    return out;
}

std::optional<std::vector<uint8_t>> reed_solomon::decode(const std::vector<uint8_t>& data) {
    if (data.size() != kEncodedLen) {
        return std::nullopt;
    }
    std::vector<uint8_t> out(kFrameLen);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        if (correct_reed_solomon_decode(rs, data.data() + b * kCodewordLen, kCodewordLen, out.data() + b * kBlockLen) < 0) {
            return std::nullopt;
        }
    }
    return out;
}
