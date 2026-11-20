// File Name:     convolutional.cpp
// Author:        Arthur Ufongene
// Course:        CS60: Computer Networks
// Assignment:    Lab 4: Physical Layer
// Date:          November 2026

// Description: rate-1/2, K=7 convolutional encoding (generators 171/133 octal) and
// soft-decision Viterbi decoding of FSK soft bits

#include "convolutional.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace {

// Rate-1/2, K = 7, generators 171 and 133 (octal), K - 1 = 6 zero tail bits.
constexpr int kConstraintLength = 7;
constexpr int kTailBits         = kConstraintLength - 1;
constexpr int kG1               = 0171;
constexpr int kG2               = 0133;

constexpr int kRegMask   = (1 << kConstraintLength) - 1;   // 7-bit shift register
constexpr int kStates    = 1 << (kConstraintLength - 1);   // 64 trellis states
constexpr int kStateMask = kStates - 1;

int parity(int x) { return std::popcount(static_cast<unsigned>(x)) & 1; }

// The shift register holds the current input bit in bit 0 and the 6 previous
// bits above it. The trellis state is the low 6 bits: the history the next
// input bit will be combined with.
struct Branch { int out1, out2, next; };

Branch step(int state, int bit) {
    const int reg = ((state << 1) | bit) & kRegMask;
    return { parity(reg & kG1), parity(reg & kG2), reg & kStateMask };
}

int getBit(const std::vector<uint8_t>& d, size_t i) { return (d[i / 8] >> (7 - i % 8)) & 1; }
void setBit(std::vector<uint8_t>& d, size_t i)     { d[i / 8] |= static_cast<uint8_t>(0x80 >> (i % 8)); }

#ifndef DIAL_UP_L1_METRIC
// Branch cost of one coded bit: -log P(bit == expected), where s = P(bit = 1).
// Summed along a path this is -log of the path's probability, so Viterbi picks
// the most likely path. The clamp keeps a sure contradiction finite (~13.8).
float bitCost(float s, int expected) {
    const float p = expected ? s : 1.0f - s;
    return -std::log(std::max(p, 1e-6f));
}
#endif

}  // namespace

std::vector<uint8_t> convolutional::encode(const std::vector<uint8_t>& data) {
    const size_t inBits = data.size() * 8 + kTailBits;
    std::vector<uint8_t> out((2 * inBits + 7) / 8, 0);   // 2 coded bits per input bit

    int state = 0;
    size_t o = 0;
    for (size_t i = 0; i < inBits; ++i) {
        const int bit = i < data.size() * 8 ? getBit(data, i) : 0;   // tail = 6 zeros
        const Branch b = step(state, bit);
        if (b.out1) setBit(out, o);
        ++o;
        if (b.out2) setBit(out, o);
        ++o;
        state = b.next;
    }
    return out;   // state is back to 0 thanks to the tail
}

std::vector<uint8_t> convolutional::decode(const std::vector<float>& soft) {
    const size_t steps = soft.size() / 2;          // one trellis step per input bit
    if (steps <= static_cast<size_t>(kTailBits)) return {};
    const size_t dataBits = steps - kTailBits;

    constexpr float kInf = std::numeric_limits<float>::infinity();
    std::array<float, kStates> metric{};
    metric.fill(kInf);
    metric[0] = 0.0f;                              // encoder starts in state 0

    // decision[t][ns] = top bit of the state that won the path into ns at step t.
    std::vector<std::array<uint8_t, kStates>> decision(steps);

    for (size_t t = 0; t < steps; ++t) {
        const float s1 = soft[2 * t], s2 = soft[2 * t + 1];
        std::array<float, kStates> next{};
        next.fill(kInf);

        // Add-compare-select.
        for (int s = 0; s < kStates; ++s) {
            if (metric[s] == kInf) continue;
            for (int bit = 0; bit < 2; ++bit) {
                const Branch b = step(s, bit);
#ifdef DIAL_UP_L1_METRIC
                // Old: distance between the soft value and the expected bit.
                const float cost = std::fabs(s1 - static_cast<float>(b.out1)) + std::fabs(s2 - static_cast<float>(b.out2));
#else
                const float cost = bitCost(s1, b.out1) + bitCost(s2, b.out2);
#endif
                const float m = metric[s] + cost;
                if (m < next[b.next]) {
                    next[b.next] = m;
                    decision[t][b.next] = static_cast<uint8_t>(s >> (kConstraintLength - 2));
                }
            }
        }
        metric = next;
    }

    // Traceback from state 0 (the tail bits flushed the encoder back there).
    // The input bit at step t is the low bit of the state entered at step t.
    std::vector<uint8_t> out((dataBits + 7) / 8, 0);
    int state = 0;
    for (size_t t = steps; t-- > 0;) {
        if (t < dataBits && (state & 1)) setBit(out, t);
        state = (state >> 1) | (decision[t][state] << (kConstraintLength - 2));
    }
    out.resize(dataBits / 8);   // drop a partial trailing byte, if any
    return out;
}
