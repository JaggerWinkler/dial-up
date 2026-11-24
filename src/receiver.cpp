// File Name:     receiver.cpp
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: the receive loop: demodulating, decoding up the stack, printing text
// messages, and handing file chunks to the file module

#include "receiver.h"

#include "console.h"
#include "convolutional.h"
#include "interleaver.h"
#include "message.h"

#include <chrono>
#include <string>

using console::err;
using console::macToString;

namespace {

// Received text may contain anything; keep it from driving the terminal.
std::string printableText(const uint8_t* d, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = d[i];
        if (c == '\t' || (c >= 0x20 && c != 0x7f)) s += static_cast<char>(c);   // UTF-8 bytes pass through
        else s += '?';
    }
    return s;
}

}  // namespace

receiver::receiver(const data_link_layer& link, speaker& spk, const MAC_Address& me, bool verbose)
    : link_(link), spk_(spk), me_(me), demod_(message::kCodedBits, verbose) {}

void receiver::run(const std::atomic<bool>& stop) {
    using Clock = std::chrono::steady_clock;
    auto lastTick = Clock::now();

    while (!stop) {
        const std::vector<float> chunk = spk_.decode();
        if (chunk.empty()) {
            err("microphone error; receiving stopped");
            return;
        }
        if (const auto soft = demod_.decode(chunk.data(), chunk.size())) {
            if (const auto frame = demodulate(*soft)) onFrame(*frame);
        }
        if (Clock::now() - lastTick > std::chrono::seconds(1)) {
            files_.expire();
            lastTick = Clock::now();
        }
    }
}

std::optional<Frame> receiver::demodulate(const std::vector<float>& soft) {
    const std::vector<uint8_t> deinterleaved = interleaver::decode(convolutional::decode(soft));
    const std::optional<std::vector<uint8_t>> frame = rs_.decode(deinterleaved);
    if (!frame) {
        err("          frame dropped: too many errors for Reed-Solomon");
        return std::nullopt;
    }
    std::optional<Frame> f = link_.decode(*frame);
    if (!f) err("          frame dropped: CRC failed or not addressed to us");
    return f;
}

void receiver::onFrame(const Frame& f) {
    if (f.source == me_) return;          // our own transmission, heard by our mic
    const auto& p = f.payload;
    if (p.empty()) return;
    if (p[0] == message::kTypeText) {
        console::out("[" + macToString(f.source) + "] " + printableText(p.data() + 1, p.size() - 1));
    } else if (p[0] == message::kTypeFile) {
        files_.addChunk(f.source, p);
    } else {
        err("          unknown message type " + std::to_string(p[0]) + " from " + macToString(f.source));
    }
}
