//
// Created by Jagger Winkler on 11/6/26.
//

#ifndef DIAL_UP_DATA_LINK_LAYER_H
#define DIAL_UP_DATA_LINK_LAYER_H

#include <vector>
#include "util.h"
#include <optional>

class data_link_layer {
public:
    /**
         * @brief Build a frame: header + payload + CRC-32.
         * @param frame Destination, source, and payload (at most 255 bytes).
         * @return Encoded frame bytes, ready for the physical layer.
    */
    std::vector<uint8_t> encode(Frame);

    std::optional<Frame> decode(std::vector<uint8_t> data);
};



#endif //DIAL_UP_DATA_LINK_LAYER_H
