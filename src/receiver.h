// File Name:     receiver.h
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: interface for the receive side: reading the microphone and passing each
// frame up the network stack

#ifndef DIAL_UP_RECEIVER_H
#define DIAL_UP_RECEIVER_H

#include "data_link_layer.h"
#include "file.h"
#include "fsk.h"
#include "reed_solomon.h"
#include "speaker.h"
#include "util.h"

#include <atomic>
#include <optional>
#include <vector>

// Receive side of the app: reads the microphone and pushes each frame up the
// stack: FSK → convolutional → deinterleave → Reed-Solomon → data link. Text
// is printed with its sender; file chunks go to a file::assembler. Frames
// from our own MAC (our speaker heard by our mic) are ignored.
class receiver {
public:
    receiver(const data_link_layer& link, speaker& spk, const MAC_Address& me, bool verbose);

    /** @brief Listen until `stop` is set or the microphone fails. Meant for its own thread. */
    void run(const std::atomic<bool>& stop);

    /** @brief Act on one received frame (text → stdout, file chunk → assembler). */
    void onFrame(const Frame& f);

private:
    const data_link_layer& link_;
    speaker& spk_;
    MAC_Address me_;
    fsk demod_;
    reed_solomon rs_;   // its own instance: libcorrect handles aren't shared across threads
    file::assembler files_;

    /** @brief Soft values of one demodulated frame → Frame, or nullopt if unrecoverable. */
    std::optional<Frame> demodulate(const std::vector<float>& soft);
};

#endif // DIAL_UP_RECEIVER_H
