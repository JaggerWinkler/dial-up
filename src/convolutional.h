// File Name:     convolutional.h
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: interface for the rate-1/2, K=7 convolutional encoder and soft-decision
// Viterbi decoder

#ifndef DIAL_UP_CONVOLUTIONAL_H
#define DIAL_UP_CONVOLUTIONAL_H

#include <cstddef>
#include <cstdint>
#include <vector>


class convolutional {
public:
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& data);

    static std::vector<uint8_t> decode(const std::vector<float>& soft);
};

#endif // DIAL_UP_CONVOLUTIONAL_H
