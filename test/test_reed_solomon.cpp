// File Name:     test_reed_solomon.cpp
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: testing Reed-Solomon padding, the 16-byte-per-codeword correction limit,
// and bad input

// Reed-Solomon: padding, the 16-byte-per-codeword correction limit, bad input.
#include "check.h"
#include "src/reed_solomon.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace {

// Corrupt `count` distinct bytes of codeword `block` (XOR with a non-zero value).
void corrupt(std::vector<uint8_t>& enc, size_t block, size_t count) {
    std::vector<size_t> pos(reed_solomon::kCodewordLen);
    std::iota(pos.begin(), pos.end(), 0);
    std::shuffle(pos.begin(), pos.end(), test::rng());
    std::uniform_int_distribution<int> d(1, 255);
    for (size_t i = 0; i < count; ++i)
        enc[block * reed_solomon::kCodewordLen + pos[i]] ^= static_cast<uint8_t>(d(test::rng()));
}

}  // namespace

int main() {
    reed_solomon rs;
    constexpr size_t kCorrectable = reed_solomon::kParityLen / 2;

    TEST("short frame is padded and comes back") {
        const auto frame = test::randomBytes(20);
        const auto enc = rs.encode(frame);
        CHECK_EQ(enc.size(), reed_solomon::kEncodedLen);
        const auto dec = rs.decode(enc);
        CHECK(dec.has_value());
        if (dec) {
            CHECK_EQ(dec->size(), reed_solomon::kFrameLen);
            CHECK(std::equal(frame.begin(), frame.end(), dec->begin()));
            CHECK(std::all_of(dec->begin() + 20, dec->end(), [](uint8_t b) { return b == 0; }));
        }
    }

    TEST("full frame round trip") {
        const auto frame = test::randomBytes(reed_solomon::kFrameLen);
        const auto dec = rs.decode(rs.encode(frame));
        CHECK(dec && *dec == frame);
    }

    TEST("corrects kParityLen/2 errors in every codeword") {
        for (int trial = 0; trial < 10; ++trial) {
            const auto frame = test::randomBytes(reed_solomon::kFrameLen);
            auto enc = rs.encode(frame);
            for (size_t b = 0; b < reed_solomon::kNumBlocks; ++b) corrupt(enc, b, kCorrectable);
            const auto dec = rs.decode(enc);
            CHECK(dec && *dec == frame);
        }
    }

    TEST("too many errors are not silently 'corrected' to the original") {
        for (int trial = 0; trial < 10; ++trial) {
            const auto frame = test::randomBytes(reed_solomon::kFrameLen);
            auto enc = rs.encode(frame);
            corrupt(enc, 0, kCorrectable + 4);
            const auto dec = rs.decode(enc);
            CHECK(!dec || *dec != frame);   // usually nullopt; a mis-correction is also possible
        }
    }

    TEST("bad input") {
        CHECK(!rs.decode(std::vector<uint8_t>(reed_solomon::kEncodedLen - 1)).has_value());
        bool threw = false;
        try {
            (void)rs.encode(std::vector<uint8_t>(reed_solomon::kFrameLen + 1));
        } catch (const std::length_error&) {
            threw = true;
        }
        CHECK(threw);
    }

    TEST("two instances agree") {
        reed_solomon other;
        const auto frame = test::randomBytes(100);
        CHECK(rs.encode(frame) == other.encode(frame));
    }

    return test::finish();
}
