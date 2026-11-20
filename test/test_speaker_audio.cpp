// speaker hardware: play a tone on the speaker, record it with the mic, and
// check the tone is there. Needs speakers on, a mic, and mic permission.
#include "check.h"
#include "src/modem.h"
#include "src/speaker.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Goertzel power of `freq` over the whole buffer, normalised by its length.
double power(const std::vector<float>& x, double freq) {
    const double c = 2.0 * std::cos(kTwoPi * freq / modem::kSampleRate);
    double s1 = 0, s2 = 0;
    for (float v : x) {
        const double s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / (static_cast<double>(x.size()) * x.size());
}

}  // namespace

int main() {
    speaker spk;
    const double played = modem::toneFreq(5), other = modem::toneFreq(12);

    TEST("play a tone and hear it") {
        std::vector<float> tone(modem::kSampleRate / 2);   // 0.5 s
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.5f * static_cast<float>(std::sin(kTwoPi * played * static_cast<double>(i) / modem::kSampleRate));

        // Record ~1.2 s on this thread; start playback 0.2 s in on another.
        std::atomic<bool> playedOk{false};
        std::vector<float> rec;
        (void)spk.decode();   // open the mic before playback starts
        std::thread player([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            playedOk = spk.encode(tone);
        });
        while (rec.size() < static_cast<size_t>(1.2 * modem::kSampleRate)) {
            const auto chunk = spk.decode();
            if (chunk.empty()) break;
            rec.insert(rec.end(), chunk.begin(), chunk.end());
        }
        player.join();

        CHECK(playedOk.load());
        CHECK(!rec.empty());
        bool allZero = true;
        for (float s : rec) allZero &= (s == 0.0f);
        if (!CHECK(!allZero))
            std::fprintf(stderr, "    the mic delivered pure silence: allow microphone access for this terminal "
                                 "(System Settings > Privacy & Security > Microphone)\n");

        const double pPlayed = power(rec, played), pOther = power(rec, other);
        std::fprintf(stderr, "    power at %.0f Hz (played): %.3g, at %.0f Hz (not played): %.3g, ratio %.1f\n",
                     played, pPlayed, other, pOther, pPlayed / std::max(pOther, 1e-20));
        CHECK(pPlayed > 10.0 * pOther);
    }

    return test::finish();
}
