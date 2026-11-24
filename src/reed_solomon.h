// File Name:     reed_solomon.h
// Author:        Jagger Winkler
// Date:          November 2026

// Description: interface for the shortened RS(255, 223) Reed-Solomon outer code built
// on libcorrect

#ifndef DIAL_UP_REED_SOLOMON_H
#define DIAL_UP_REED_SOLOMON_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Opaque libcorrect handle (defined in correct.h, which stays out of this header)
struct correct_reed_solomon;

class reed_solomon {
public:
    // Shortened RS(255, 223): frames are padded to kFrameLen and split into
    // kNumBlocks blocks of kBlockLen bytes, each gaining kParityLen parity bytes.
    static constexpr size_t kFrameLen    = 272;
    static constexpr size_t kNumBlocks   = 2;
    static constexpr size_t kBlockLen    = kFrameLen / kNumBlocks;    // 136
    static constexpr size_t kParityLen   = 32;
    static constexpr size_t kCodewordLen = kBlockLen + kParityLen;    // 168
    static constexpr size_t kEncodedLen  = kNumBlocks * kCodewordLen; // 336

    reed_solomon();
    ~reed_solomon();

    // Owns a libcorrect handle, so it can't be copied
    reed_solomon(const reed_solomon&) = delete;
    reed_solomon& operator=(const reed_solomon&) = delete;

    /**
     * @brief Add Reed-Solomon parity to a data link layer frame.
     * @param frame Frame from the data link layer, at most kFrameLen bytes.
     * @return kEncodedLen bytes: kNumBlocks codewords of kCodewordLen bytes each.
     */
    std::vector<uint8_t> encode(const std::vector<uint8_t>& frame);

    /**
     * @brief Correct errors and strip Reed-Solomon parity.
     * @param data kEncodedLen bytes from the deinterleaver.
     * @return The kFrameLen-byte padded frame, or nullopt if any block has
     *         more errors than can be corrected.
     */
    std::optional<std::vector<uint8_t>> decode(const std::vector<uint8_t>& data);

private:
    correct_reed_solomon* rs;
};

#endif // DIAL_UP_REED_SOLOMON_H
