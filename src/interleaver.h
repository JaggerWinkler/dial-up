// File Name:     interleaver.h
// Author:        Jagger Winkler
// Date:          November 2026

// Description: interface for the block interleaver between Reed-Solomon and the
// convolutional code

#ifndef DIAL_UP_INTERLEAVER_H
#define DIAL_UP_INTERLEAVER_H

#include <cstdint>
#include <vector>

// Block interleaver between Reed-Solomon and the convolutional code. Spreads
// burst errors evenly across the Reed-Solomon codewords.
class interleaver {
public:
    /**
     * @brief Down the stack: reorder bytes so consecutive bytes come from different codewords.
     * @param data Reed-Solomon output (reed_solomon::kEncodedLen bytes).
     * @return The same bytes, interleaved.
     * @throws std::length_error if data's size isn't a multiple of the codeword count.
     */
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& data);

    /**
     * @brief Up the stack: undo encode, restoring the original byte order.
     * @param data Interleaved bytes from the Viterbi decoder.
     * @return The bytes in their original order, ready for Reed-Solomon decoding.
     * @throws std::length_error if data's size isn't a multiple of the codeword count.
     */
    static std::vector<uint8_t> decode(const std::vector<uint8_t>& data);
};

#endif // DIAL_UP_INTERLEAVER_H
