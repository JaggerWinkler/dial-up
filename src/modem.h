// File Name:     modem.h
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: FSK modem parameters (sample rate, tone plan, symbol timing, preamble)
// shared by the transmitter and receiver

// Both peers must be built with the same values.
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
              "highest tone must stay below the ~22 kHz Nyquist limit of 44.1 kHz audio paths");

constexpr int kPreambleLen = 16;                   // length of the preamble [symbols]
// preamble sent before every frame for detection and sync [tone indices, 0 .. kTones-1]
constexpr int kPreamble[kPreambleLen] ={2, 8, 9, 12, 4, 14, 10, 15, 13, 7, 6, 3, 11, 1, 5, 0};

}  // namespace modem
