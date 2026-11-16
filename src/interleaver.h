#ifndef DIAL_UP_INTERLEAVER_H
#define DIAL_UP_INTERLEAVER_H

#include <cstdint>
#include <vector>

class interleaver {
public:

    std::vector<uint8_t> encode(const std::vector<uint8_t>& data);


    std::vector<uint8_t> decode(const std::vector<uint8_t>& data);
};

#endif // DIAL_UP_INTERLEAVER_H
