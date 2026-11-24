// File Name:     test_fsk.cpp
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: testing the FSK modulator and demodulator on in-memory samples

// FSK modulator / demodulator on in-memory samples (no audio hardware).
#include "check.h"
#include "src/fsk.h"
#include "src/modem.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

constexpr size_t kBits = 1000;   // not a multiple of 4: exercises last-symbol padding
constexpr size_t kChunk = 1000;  // odd chunk size, not a multiple of a symbol

std::vector<float> silence(double seconds) {
    return std::vector<float>(static_cast<size_t>(seconds * modem::kSampleRate), 0.0f);
}

void append(std::vector<float>& a, const std::vector<float>& b) { a.insert(a.end(), b.begin(), b.end()); }

// Feed the samples in kChunk pieces; collect every frame the demodulator emits.
std::vector<std::vector<float>> demodulate(const std::vector<float>& x, size_t nbits = kBits) {
    fsk rx(nbits);
    std::vector<std::vector<float>> frames;
    for (size_t i = 0; i < x.size(); i += kChunk) {
        if (auto f = rx.decode(x.data() + i, std::min(kChunk, x.size() - i))) frames.push_back(std::move(*f));
    }
    return frames;
}

size_t bitErrors(const std::vector<float>& soft, const std::vector<uint8_t>& bytes) {
    size_t errors = 0;
    for (size_t i = 0; i < soft.size(); ++i) errors += (soft[i] > 0.5f) != (test::bitOf(bytes, i) == 1);
    return errors;
}

}  // namespace

int main() {
    const auto data = test::randomBytes((kBits + 7) / 8);
    const auto signal = fsk::encode(data, kBits);

    TEST("signal length and level") {
        const size_t symbols = modem::kPreambleLen + (kBits + modem::kBitsPerSymbol - 1) / modem::kBitsPerSymbol;
        const size_t body = symbols * modem::kSymbolSamples;
        CHECK(signal.size() > body);                                       // body + fade in/out
        CHECK(signal.size() < body + static_cast<size_t>(0.1 * modem::kSampleRate));
        float peak = 0;
        for (float s : signal) peak = std::max(peak, std::fabs(s));
        CHECK(peak <= 1.0f);
        CHECK(peak > 0.1f);
    }

    TEST("clean round trip") {
        auto x = silence(0.3);
        append(x, signal);
        append(x, silence(0.3));
        const auto frames = demodulate(x);
        CHECK_EQ(frames.size(), 1u);
        if (!frames.empty()) {
            CHECK_EQ(frames[0].size(), kBits);
            CHECK(std::all_of(frames[0].begin(), frames[0].end(), [](float s) { return s >= 0.0f && s <= 1.0f; }));
            CHECK_EQ(bitErrors(frames[0], data), 0u);
        }
    }

    TEST("quiet signal in noise") {
        auto x = silence(0.3);
        append(x, signal);
        append(x, silence(0.3));
        for (auto& s : x) s *= 0.2f;
        test::addNoise(x, 0.02);
        const auto frames = demodulate(x);
        CHECK_EQ(frames.size(), 1u);
        if (!frames.empty()) CHECK_EQ(bitErrors(frames[0], data), 0u);
    }

    TEST("uneven tone gains (low-passed channel)") {
        auto x = silence(0.3);
        append(x, signal);
        append(x, silence(0.3));
        // One-pole low-pass: the highest tones arrive noticeably weaker than the lowest.
        float y = 0;
        for (auto& s : x) { y += 0.6f * (s - y); s = y; }
        test::addNoise(x, 0.002);
        const auto frames = demodulate(x);
        CHECK_EQ(frames.size(), 1u);
        if (!frames.empty()) CHECK_EQ(bitErrors(frames[0], data), 0u);
    }

    TEST("noise alone is not a frame") {
        auto x = silence(3.0);
        test::addNoise(x, 0.05);
        CHECK(demodulate(x).empty());
    }

    TEST("two frames back to back") {
        const auto data2 = test::randomBytes((kBits + 7) / 8);
        auto x = silence(0.2);
        append(x, signal);
        append(x, silence(0.3));
        append(x, fsk::encode(data2, kBits));
        append(x, silence(0.3));
        const auto frames = demodulate(x);
        CHECK_EQ(frames.size(), 2u);
        if (frames.size() == 2) {
            CHECK_EQ(bitErrors(frames[0], data), 0u);
            CHECK_EQ(bitErrors(frames[1], data2), 0u);
        }
    }

    return test::finish();
}
