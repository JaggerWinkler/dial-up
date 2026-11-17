//
// Created by Jagger Winkler on 11/6/26.
//

#include "data_link_layer.h"

#include <algorithm>
#include <stdexcept>

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

static MAC_Address lookup_MAC(const std::string& iface) {
    auto mac = get_MAC(iface);
    if (!mac.has_value()) {
        throw std::runtime_error("No MAC Address found for this device");
        //this is a fault in the MAC address utility in util.h/util.cpp
    }
    return *mac;
}

data_link_layer::data_link_layer(const std::string& iface) : this_mac(lookup_MAC(iface)) {}

data_link_layer::data_link_layer(const MAC_Address& mac) : this_mac(mac) {}

std::vector<uint8_t> data_link_layer::encode(const MAC_Address& dst_mac, const std::vector<uint8_t>& payload) const {
    if (payload.size() > 255) {
        throw std::length_error("payload exceeds 255 byte max size"); //this is a fault in the upper level function
    }

    std::vector<uint8_t> bitstream;
    bitstream.insert(bitstream.end(), dst_mac.begin(), dst_mac.end());
    bitstream.insert(bitstream.end(), this_mac.begin(), this_mac.end());
    bitstream.push_back(payload.size());
    bitstream.insert(bitstream.end(), payload.begin(), payload.end());
    // Ethernet FCS: CRC-32 appended least significant byte first
    auto crc = calculate_CRC(bitstream);
    bitstream.insert(bitstream.end(), {
        static_cast<uint8_t>(crc & 0xFF),
        static_cast<uint8_t>((crc >> 8)  & 0xFF),
        static_cast<uint8_t>((crc >> 16) & 0xFF),
        static_cast<uint8_t>((crc >> 24) & 0xFF)
    });
    return bitstream;
}

std::optional<Frame> data_link_layer::decode(const std::vector<uint8_t>& bitstream) const {
    // Frame: [dst MAC 6][src MAC 6][len 1][payload len][CRC-32 4]
    constexpr size_t kMacLen    = 6;
    constexpr size_t kHeaderLen = 2 * kMacLen + 1;
    constexpr size_t kCrcLen    = 4;

    // Damaged frame, drop
    if (bitstream.size() < kHeaderLen + kCrcLen) {
        return std::nullopt;
    }
    // Locate the CRC from the length byte
    const size_t len = bitstream[kHeaderLen - 1];
    const size_t crc_pos = kHeaderLen + len;
    if (bitstream.size() < crc_pos + kCrcLen) {
        return std::nullopt;
    }

    // Ethernet FCS is stored least significant byte first
    const uint32_t received =  static_cast<uint32_t>(bitstream[crc_pos])            |
                              (static_cast<uint32_t>(bitstream[crc_pos + 1]) <<  8) |
                              (static_cast<uint32_t>(bitstream[crc_pos + 2]) << 16) |
                              (static_cast<uint32_t>(bitstream[crc_pos + 3]) << 24);
    // if CRC doesnt match, drop
    if (calculate_CRC({bitstream.begin(), bitstream.begin() + crc_pos}) != received) {
        return std::nullopt;
    }

    Frame frame{};
    std::copy_n(bitstream.begin(), kMacLen, frame.dest.begin());
    std::copy_n(bitstream.begin() + kMacLen, kMacLen, frame.source.begin());

    // drop frame addressed to other device
    if (frame.dest != BROADCAST_MAC && frame.dest != this_mac) {
        return std::nullopt;
    }

    frame.payload.assign(bitstream.begin() + kHeaderLen, bitstream.begin() + crc_pos);
    return frame;
}

