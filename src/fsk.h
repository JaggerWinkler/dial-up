#ifndef DIAL_UP_FSK_H
#define DIAL_UP_FSK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Q-FSK modulator / demodulator, including the preamble. Tone plan, symbol
// timing and preamble are in modem.h.
class fsk {
public:
    /**
     * @param frameBits Number of coded bits in one frame (the demodulator stops
     *                  after this many bits following a preamble).
     * @param verbose   Print input level, preamble scores and tone gains.
     */
    explicit fsk(size_t frameBits, bool verbose = false);
    ~fsk();

    /**
     * @brief Down the stack: prepend the preamble and modulate bits into audio.
     * @param data  Coded bits packed MSB first.
     * @param nbits Number of valid bits in data. If it isn't a multiple of
     *              modem::kBitsPerSymbol, the last symbol is padded with 0s.
     * @return Mono float samples at modem::kSampleRate, faded in and out.
     */
    std::vector<float> encode(const std::vector<uint8_t>& data, size_t nbits);

    /**
     * @brief Up the stack: feed microphone samples in any chunk size. Searches
     *        for the preamble, syncs, and demodulates the frame that follows.
     * @return One soft value per coded bit once a whole frame has been
     *         demodulated, otherwise nullopt.
     */
    std::optional<std::vector<float>> decode(const float* samples, size_t n);

private:
    class Receiver;
    std::unique_ptr<Receiver> rx_;
};

#endif // DIAL_UP_FSK_H
