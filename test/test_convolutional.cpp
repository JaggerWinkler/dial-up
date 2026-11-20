// File Name:     test_convolutional.cpp
// Author:        Arthur Ufongene
// Course:        CS60: Computer Networks
// Assignment:    Lab 4: Physical Layer
// Date:          November 2026

// Description: testing convolutional code sizes, known encoder output, and Viterbi round
// trips with clean, erroneous, and erased soft values

// Convolutional code: sizes, known encoder output, and Viterbi round trips
// with clean, erroneous and erased soft values.
#include "check.h"
#include "src/convolutional.h"

#include <algorithm>
#include <numeric>

namespace {

constexpr size_t kTail = 6;

size_t codedBits(size_t bytes) { return (bytes * 8 + kTail) * 2; }

}  // namespace

int main() {
    TEST("output size") {
        for (size_t n : {1u, 2u, 20u, 336u}) {
            const auto out = convolutional::encode(std::vector<uint8_t>(n, 0xA5));
            CHECK_EQ(out.size(), (codedBits(n) + 7) / 8);
        }
    }

    TEST("all zeros encode to all zeros") {
        const auto out = convolutional::encode(std::vector<uint8_t>(16, 0));
        CHECK(std::all_of(out.begin(), out.end(), [](uint8_t b) { return b == 0; }));
    }

    TEST("impulse response is the generators 0171 / 0133") {
        // A single 1 (the first bit) walks through the 7-bit register; at step k
        // the outputs are bit k of each generator: G1 = 1111001b, G2 = 1011011b.
        const auto out = convolutional::encode({0x80});
        const int g1 = 0171, g2 = 0133;
        for (int k = 0; k < 7; ++k) {
            CHECK_EQ(test::bitOf(out, 2 * k), (g1 >> k) & 1);
            CHECK_EQ(test::bitOf(out, 2 * k + 1), (g2 >> k) & 1);
        }
        for (size_t i = 14; i < codedBits(1); ++i) CHECK_EQ(test::bitOf(out, i), 0);
    }

    TEST("clean round trip") {
        for (int trial = 0; trial < 4; ++trial) {
            const auto data = test::randomBytes(336);
            const auto coded = convolutional::encode(data);
            CHECK(convolutional::decode(test::softFromBytes(coded, codedBits(data.size()))) == data);
        }
    }

    TEST("corrects spaced bit errors") {
        const auto data = test::randomBytes(336);
        const auto coded = convolutional::encode(data);
        auto soft = test::softFromBytes(coded, codedBits(data.size()));
        int flipped = 0;
        for (size_t i = 7; i < soft.size(); i += 41) {   // one confident error every ~20 trellis steps
            soft[i] = soft[i] > 0.5f ? 0.1f : 0.9f;
            ++flipped;
        }
        CHECK(flipped > 100);
        CHECK(convolutional::decode(soft) == data);
    }

    TEST("corrects 10% erasures") {
        const auto data = test::randomBytes(336);
        const auto coded = convolutional::encode(data);
        auto soft = test::softFromBytes(coded, codedBits(data.size()));
        std::vector<size_t> idx(soft.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::shuffle(idx.begin(), idx.end(), test::rng());
        for (size_t i = 0; i < soft.size() / 10; ++i) soft[idx[i]] = 0.5f;   // "don't know"
        CHECK(convolutional::decode(soft) == data);
    }

    TEST("too short input decodes to nothing") {
        CHECK(convolutional::decode(std::vector<float>(2 * kTail, 0.0f)).empty());
    }

    return test::finish();
}
