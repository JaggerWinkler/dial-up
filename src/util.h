// File Name:     util.h
// Author:        Jagger Winkler
// Course:        CS60: Computer Networks
// Assignment:    Lab 4: Physical Layer
// Date:          November 2026

// Description: shared types (MAC addresses, frames) and looking up this machine's MAC
// address

#ifndef DIAL_UP_UTIL_H
#define DIAL_UP_UTIL_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using MAC_Address = std::array<uint8_t, 6>;

inline constexpr MAC_Address BROADCAST_MAC = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct Frame {
    std::vector<uint8_t> payload;
    MAC_Address dest;
    MAC_Address source;
};

/**
 * @brief Read the hardware (MAC) address of a network interface.
 * @param iface Interface name, e.g. "en0" on macOS, "wlo1" on Linux,
 *              or "Wi-Fi" on Windows (the adapter's friendly name).
 *              Empty (the default) picks the interface currently connected
 *              to the network.
 * @return The 6-byte MAC, or nullopt if the interface doesn't exist or has none.
 */
std::optional<MAC_Address> get_MAC(const std::string& iface = "");

#endif //DIAL_UP_UTIL_H
