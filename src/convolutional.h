#ifndef DIAL_UP_CONVOLUTIONAL_H
#define DIAL_UP_CONVOLUTIONAL_H

#include <cstddef>
#include <cstdint>
#include <vector>


class convolutional {
public:
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data);

    std::vector<uint8_t> decode(const std::vector<float>& soft);
};

#endif // DIAL_UP_CONVOLUTIONAL_H
