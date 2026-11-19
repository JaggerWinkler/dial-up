//
// Created by Jagger Winkler on 11/6/26.
//

#ifndef DIAL_UP_DATA_LINK_LAYER_H
#define DIAL_UP_DATA_LINK_LAYER_H

#include <vector>
#include "util.h"
#include <optional>
#include <string>

class data_link_layer {
public:
    /**
     * @brief Look up this device's MAC once and use it for every frame.
     * @param iface Interface to read the MAC from; empty picks the connected one.
     * @throws std::runtime_error if no MAC address can be found.
     */
    explicit data_link_layer(const std::string& iface = "");

    /** @brief Use a given MAC instead of the hardware one (e.g. to simulate two peers). */
    explicit data_link_layer(const MAC_Address& mac);

    /**
     * @brief Wrap a payload in a frame: [dst MAC][src MAC][len][payload][CRC-32].
     * @param dst_mac Destination MAC address, or BROADCAST_MAC to send to everyone.
     * @param payload Data to send, at most 255 bytes.
     * @return The frame bytes (17-272 bytes), ready for the physical layer.
     * @throws std::length_error if the payload is longer than 255 bytes.
     */
    [[nodiscard]] std::vector<uint8_t> encode(const MAC_Address& dst_mac, const std::vector<uint8_t>& payload) const;

    /**
     * @brief Check CRC and unwrap a frame received from the physical layer.
     * @param bitstream Received frame bytes. Bytes after the CRC (e.g. padding) are ignored.
     * @return The frame's payload and addresses, or nullopt if the frame is
     *         too short, fails the CRC check, or is addressed to another device.
     */
    [[nodiscard]] std::optional<Frame> decode(const std::vector<uint8_t>& bitstream) const;

private:
    MAC_Address this_mac;
};



#endif //DIAL_UP_DATA_LINK_LAYER_H
