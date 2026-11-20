// File Name:     test_end_to_end.cpp
// Author:        Arthur Ufongene
// Course:        CS60: Computer Networks
// Assignment:    Lab 4: Physical Layer
// Date:          November 2026

// Description: testing one text frame sent over the air from speaker to microphone and
// decoded up the full stack

// Over the air: "alice" plays one text frame on the speaker while "bob", on a
// background thread, listens on the microphone and decodes it up the full
// stack. Needs speakers on, a mic, mic permission, and a reasonably quiet room.
//
//   ./test_end_to_end [-v]     -v: print fsk diagnostics (level, preamble, tone gains)
#include "check.h"
#include "src/convolutional.h"
#include "src/data_link_layer.h"
#include "src/fsk.h"
#include "src/interleaver.h"
#include "src/message.h"
#include "src/reed_solomon.h"
#include "src/speaker.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

const MAC_Address kAlice{0x02, 0, 0, 0, 0, 0x0a};
const MAC_Address kBob{0x02, 0, 0, 0, 0, 0x0b};

// How far bob got, for the failure message.
enum class Stage { NoPreamble, RsFailed, CrcFailed, Received };

const char* stageName(Stage s) {
    switch (s) {
        case Stage::NoPreamble: return "no frame demodulated (preamble never detected)";
        case Stage::RsFailed:   return "frame demodulated, but too many errors for Reed-Solomon";
        case Stage::CrcFailed:  return "Reed-Solomon passed, but CRC failed or wrong address";
        case Stage::Received:   return "received";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    const bool verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;

    speaker spk;
    const data_link_layer alice(kAlice), bob(kBob);
    const std::string text = "hello over the air";
    std::vector<uint8_t> payload{message::kTypeText};
    payload.insert(payload.end(), text.begin(), text.end());

    // --- bob: listen until a frame arrives or the deadline passes ------------
    std::atomic<bool> stop{false};
    std::atomic<Stage> stage{Stage::NoPreamble};
    std::mutex frameMutex;
    std::optional<Frame> got;

    (void)spk.decode();   // open the mic before alice starts playing
    std::thread bobThread([&] {
        fsk demod(message::kCodedBits, verbose);
        reed_solomon rs;
        while (!stop) {
            const auto chunk = spk.decode();
            if (chunk.empty()) return;
            const auto soft = demod.decode(chunk.data(), chunk.size());
            if (!soft) continue;
            const auto frame = rs.decode(interleaver::decode(convolutional::decode(*soft)));
            if (!frame) { stage = Stage::RsFailed; continue; }
            auto f = bob.decode(*frame);
            if (!f) { stage = Stage::CrcFailed; continue; }
            std::lock_guard<std::mutex> lock(frameMutex);
            got = std::move(f);
            stage = Stage::Received;
            return;
        }
    });

    // --- alice: send one frame down the stack and play it --------------------
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::fprintf(stderr, "playing one frame (~%.0f s)...\n", message::kFrameSeconds);
    reed_solomon rs;
    const auto coded = convolutional::encode(interleaver::encode(rs.encode(alice.encode(kBob, payload))));
    const bool played = spk.encode(fsk::encode(coded, message::kCodedBits));
    CHECK(played);

    // Playback blocks until done; give bob a few more seconds to finish decoding.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (stage != Stage::Received && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop = true;
    bobThread.join();

    TEST("bob received alice's frame") {
        std::fprintf(stderr, "    result: %s\n", stageName(stage));
        CHECK(stage == Stage::Received);
        if (got) {
            CHECK(got->source == kAlice);
            CHECK(got->dest == kBob);
            CHECK(got->payload == payload);
        }
    }

    return test::finish();
}
