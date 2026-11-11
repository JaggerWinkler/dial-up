//
// Created by Jagger Winkler on 11/6/26.
//

#ifndef DIAL_UP_UTIL_H
#define DIAL_UP_UTIL_H

#include <vector>
#include <array>
struct Frame {
    std::vector<uint8_t> payload;
};

struct MAC_Address {
    std::array<uint8_t, 6>;
};
#endif //DIAL_UP_UTIL_H
