// modem.h — parameters shared by tx.cpp and rx.cpp. Both must agree exactly.
#pragma once

#include <cstddef>
#include <cstdint>

namespace modem {

constexpr int kSampleRate = 48000;                 // audio sample rate for playback and capture [samples/s = Hz]

constexpr int kWindow        = 480;                // Goertzel analysis window length [samples] (= 10 ms)
constexpr int kSymbolSamples = kWindow;            // length of one FSK symbol [samples]

constexpr int kBitsPerSymbol = 4;                  // data bits carried by each symbol [bits/symbol]
constexpr int kTones         = 1 << kBitsPerSymbol; // number of distinct tones, Q in Q-FSK [tones]

constexpr int kBaseFreq    = 18000;                // frequency of tone 0, the lowest tone [Hz]
constexpr int kToneSpacing = 200;                  // gap between adjacent tones [Hz]
constexpr double toneFreq(int i) { return kBaseFreq + kToneSpacing * i; }   // frequency of tone i, 0 <= i < kTones [Hz]

constexpr int kBinHz = kSampleRate / kWindow;      // width of one Goertzel frequency bin [Hz] (= 100 Hz)

static_assert(kSampleRate % kWindow == 0, "whole Hz per bin");

static_assert(kBaseFreq % kBinHz == 0 && kToneSpacing % kBinHz == 0,
              "every tone must fit a whole number of cycles in the window");

static_assert(kToneSpacing >= kBinHz, "tones need at least 2 bins of separation");

static_assert(kBaseFreq + kToneSpacing * (kTones - 1) <= 21600,
              "stay below the ~20 kHz cutoff of 44.1 kHz resamplers/anti-alias filters");

constexpr int kPreambleLen = 16;                   // length of the preamble [symbols]
// preamble sent before every frame for detection and sync [tone indices, 0 .. kTones-1]
constexpr int kPreamble[kPreambleLen] ={2, 8, 9, 12, 4, 14, 10, 15, 13, 7, 6, 3, 11, 1, 5, 0};

// Frame after the preamble: [length 1..255][payload][crc8 over length+payload].
// Each byte → two symbols, high nibble first, nibble n on tone n ^ (n >> 1).
// CRC-8 checksum of d[0..n), poly 0x07, init 0x00 [n: bytes; returns: 1 byte]
inline uint8_t crc8(const uint8_t* d, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                               : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

}  // namespace modem
