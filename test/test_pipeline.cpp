// File Name:     test_pipeline.cpp
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: testing the whole stack from payload to audio samples and back, with
// added noise, without audio hardware

// The whole stack, payload → audio samples → payload, without audio hardware:
// data link → Reed-Solomon → interleave → convolutional → FSK → (noise) → back up.
#include "check.h"
#include "src/convolutional.h"
#include "src/data_link_layer.h"
#include "src/fsk.h"
#include "src/interleaver.h"
#include "src/message.h"
#include "src/reed_solomon.h"

#include <algorithm>
#include <optional>

namespace {

const MAC_Address kAlice{0x02, 0, 0, 0, 0, 0x0a};
const MAC_Address kBob{0x02, 0, 0, 0, 0, 0x0b};
const MAC_Address kCarol{0x02, 0, 0, 0, 0, 0x0c};

std::vector<float> transmit(const data_link_layer& link, reed_solomon& rs, const MAC_Address& dest,
                            const std::vector<uint8_t>& payload) {
    const auto coded = convolutional::encode(interleaver::encode(rs.encode(link.encode(dest, payload))));
    return fsk::encode(coded, message::kCodedBits);
}

// Run samples through a fresh demodulator and the up-stack; nullopt if no frame survives.
std::optional<Frame> receive(const data_link_layer& link, reed_solomon& rs, std::vector<float> x) {
    x.insert(x.begin(), static_cast<size_t>(0.25 * modem::kSampleRate), 0.0f);
    x.insert(x.end(), static_cast<size_t>(0.25 * modem::kSampleRate), 0.0f);
    test::addNoise(x, 0.01);

    fsk rx(message::kCodedBits);
    constexpr size_t kChunk = 1200;
    for (size_t i = 0; i < x.size(); i += kChunk) {
        if (const auto soft = rx.decode(x.data() + i, std::min(kChunk, x.size() - i))) {
            const auto frame = rs.decode(interleaver::decode(convolutional::decode(*soft)));
            if (!frame) return std::nullopt;
            return link.decode(*frame);
        }
    }
    return std::nullopt;
}

}  // namespace

int main() {
    const data_link_layer alice(kAlice), bob(kBob), carol(kCarol);
    reed_solomon rsTx, rsRx;

    TEST("text addressed to bob") {
        const std::vector<uint8_t> payload{message::kTypeText, 'h', 'e', 'l', 'l', 'o'};
        const auto f = receive(bob, rsRx, transmit(alice, rsTx, kBob, payload));
        CHECK(f.has_value());
        if (f) {
            CHECK(f->payload == payload);
            CHECK(f->source == kAlice);
            CHECK(f->dest == kBob);
        }
    }

    TEST("full 255-byte payload, broadcast") {
        const auto payload = test::randomBytes(message::kMaxPayload);
        const auto f = receive(bob, rsRx, transmit(alice, rsTx, BROADCAST_MAC, payload));
        CHECK(f && f->payload == payload);
    }

    TEST("frame for someone else is dropped") {
        const std::vector<uint8_t> payload{message::kTypeText, 'x'};
        CHECK(!receive(carol, rsRx, transmit(alice, rsTx, kBob, payload)).has_value());
    }

    return test::finish();
}
