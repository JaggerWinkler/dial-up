#ifndef DIAL_UP_SPEAKER_H
#define DIAL_UP_SPEAKER_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Audio I/O via PortAudio: plays samples out of the default speaker and
// captures them from the default microphone.
//
// macOS: the first run asks for microphone permission for your terminal app.
// If you declined, re-enable it in System Settings → Privacy & Security →
// Microphone; without it, CoreAudio silently delivers all-zero samples.
class speaker {
public:
    speaker();
    ~speaker();
    speaker(const speaker&) = delete;
    speaker& operator=(const speaker&) = delete;

    /**
     * @brief Down the stack: play mono samples (modem::kSampleRate) on the
     *        default output device. Blocks until playback finishes.
     * @return false on an audio error.
     */
    [[nodiscard]] bool encode(const std::vector<float>& samples) const;

    /**
     * @brief Up the stack: read the next chunk from the default input device.
     *        Opens the input stream on the first call.
     * @return Mono samples at modem::kSampleRate; empty on an audio error.
     */
    std::vector<float> decode();

    /** @brief Print the available input devices (default marked with *). */
    static void listInputDevices();

    /**
     * @brief Load a recording instead of using the mic (first channel only;
     *        16-bit PCM or 32-bit float).
     */
    static bool readWav(const char* path, std::vector<float>& out, uint32_t& rate);

private:
    bool initialized_ = false;
    void* in_ = nullptr;          // PaStream*
    std::vector<float> chunk_;
    bool allZero_ = true;
    int silentChunks_ = 0;
    bool warnedSilence_ = false;
};

#endif // DIAL_UP_SPEAKER_H
