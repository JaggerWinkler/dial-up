#ifndef DIAL_UP_SENDER_H
#define DIAL_UP_SENDER_H

#include "data_link_layer.h"
#include "reed_solomon.h"
#include "speaker.h"
#include "util.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Send side of the app: builds text / file-chunk payloads and pushes each one
// down the stack: data link → Reed-Solomon → interleave → convolutional → FSK
// → speaker. Each call takes a destination MAC (BROADCAST_MAC for everyone).
// Calls block until playback finishes and report progress on the console.
class sender {
public:
    sender(const data_link_layer& link, const speaker& spk);

    /** @brief Send a text message (up to message::kMaxText bytes). */
    void sendText(const MAC_Address& dest, const std::string& text);

    /** @brief Read a file, split it into chunks (see file.h) and send them in order. */
    void sendFile(const MAC_Address& dest, const std::filesystem::path& path);

private:
    const data_link_layer& link_;
    const speaker& spk_;
    reed_solomon rs_;   // its own instance: libcorrect handles aren't shared across threads
    uint8_t nextId_;    // transfer id for the next file

    /** @brief One payload → one frame on the air. False on an audio error. */
    bool send(const MAC_Address& dest, const std::vector<uint8_t>& payload);
};

#endif // DIAL_UP_SENDER_H
