//
// Created by Jagger Winkler on 11/6/26.
//

#include "data_link_layer.h"

#include <sys/types.h>

static uint32_t calculate_CRC(const std::vector<uint8_t>& input) {
    uint32_t crc = 0xFFFFFFFF;
    for (auto i : input) {
        crc ^= i;
        for (int bit = 0; bit < 8; ++bit){
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            }
            else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

std::vector<uint8_t> data_link_layer::encode(const MAC_Address dst_mac, const std::vector<uint8_t>& payload){
    std::vector<uint8_t> bitstream;
    bitstream.insert(bitstream.end(), dst_mac.address.begin(), dst_mac.address.end());
    auto temp = get_MAC();
    MAC_Address this_mac{};
    if (temp.has_value()) {
        this_mac = temp.value();
    }
    else {
        throw std::runtime_error("No MAC Address found for this device");
    }
    bitstream.insert(bitstream.end(), this_mac.address.begin(), this_mac.address.end());
    bitstream.push_back(payload.size());
    bitstream.insert(bitstream.end(), payload.begin(), payload.end());
    auto crc = calculate_CRC(bitstream);
    bitstream.insert(bitstream.end(), {
        static_cast<uint8_t>((crc >> 24) & 0xFF),
        static_cast<uint8_t>((crc >> 16) & 0xFF),
        static_cast<uint8_t>((crc >> 8)  & 0xFF),
        static_cast<uint8_t>(crc & 0xFF)
    });
    return bitstream;
    //TODO: length check
}

std::optional<Frame> data_link_layer::decode(std::vector<uint8_t>& data){
    return std::nullopt;
}

