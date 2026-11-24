// File Name:     message.h
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: application-level message formats (text and file chunks) and frame
// timing constants shared by the sender, receiver, and file code

#ifndef DIAL_UP_MESSAGE_H
#define DIAL_UP_MESSAGE_H

#include "modem.h"
#include "reed_solomon.h"

#include <cstddef>
#include <cstdint>

// App-level message format and frame timing, shared by sender, receiver and file.
//
// Every data-link payload starts with a type byte:
//   text:        [0x00][text ...]
//   file chunk:  [0x01][transfer id][index hi][index lo][count hi][count lo]
//                  chunk 0:  + [name length][name][data]
//                  others:   + [data]
namespace message {

inline constexpr size_t kMaxPayload = 255;   // data link layer limit

inline constexpr uint8_t kTypeText = 0x00;
inline constexpr uint8_t kTypeFile = 0x01;

inline constexpr size_t kMaxText = kMaxPayload - 1;   // 254

// Coded bits per physical frame: Reed-Solomon always emits kEncodedLen bytes,
// and the convolutional code doubles them after adding 6 tail bits.
inline constexpr size_t kTailBits  = 6;
inline constexpr size_t kCodedBits = (reed_solomon::kEncodedLen * 8 + kTailBits) * 2;   // 5388

// Air time of one frame (s): preamble + data symbols, plus speaker lead/tail silence and fades.
inline constexpr double kFrameSeconds =
    static_cast<double>(modem::kPreambleLen + (kCodedBits + modem::kBitsPerSymbol - 1) / modem::kBitsPerSymbol) *
        static_cast<double>(modem::kSymbolSamples) / modem::kSampleRate +
    0.35 + 0.04;

}  // namespace message

#endif // DIAL_UP_MESSAGE_H
