#ifndef DIAL_UP_REED_SOLOMON_H
#define DIAL_UP_REED_SOLOMON_H

#include <cstdint>
#include <optional>
#include <vector>


class reed_solomon {
public:
    std::vector<uint8_t> encode(const std::vector<uint8_t>& frame);

    std::optional<std::vector<uint8_t>> decode(const std::vector<uint8_t>& data);
};

#endif // DIAL_UP_REED_SOLOMON_H
