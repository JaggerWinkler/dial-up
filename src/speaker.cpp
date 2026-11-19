#include "speaker.h"

#include "modem.h"

#include <portaudio.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

constexpr double        kSampleRate  = modem::kSampleRate;
constexpr unsigned long kFrames      = 256;    // output frames per buffer
constexpr size_t        kChunk       = 1200;   // input chunk, 25 ms
constexpr double        kLeadSeconds = 0.10;   // silence before the frame (device start-up)
constexpr double        kTailSeconds = 0.25;   // silence after it (CoreAudio can drop the last
                                               // few tens of ms when the stream stops)

bool check(PaError err, const char* what) {
    if (err != paNoError) {
        std::fprintf(stderr, "%s failed: %s\n", what, Pa_GetErrorText(err));
        return false;
    }
    return true;
}

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)); }

}  // namespace

speaker::speaker() : chunk_(kChunk) {
    initialized_ = check(Pa_Initialize(), "Pa_Initialize");
}

speaker::~speaker() {
    if (in_) {
        Pa_StopStream(static_cast<PaStream*>(in_));
        Pa_CloseStream(static_cast<PaStream*>(in_));
    }
    if (initialized_) Pa_Terminate();
}

bool speaker::encode(const std::vector<float>& samples) const {
    if (!initialized_) return false;

    // [lead silence][samples][tail silence], interleaved stereo
    std::vector<float> buf(static_cast<size_t>(kLeadSeconds * kSampleRate) * 2, 0.0f);
    buf.reserve(buf.size() + samples.size() * 2 + static_cast<size_t>(kTailSeconds * kSampleRate) * 2);
    for (float s : samples) { buf.push_back(s); buf.push_back(s); }
    buf.insert(buf.end(), static_cast<size_t>(kTailSeconds * kSampleRate) * 2, 0.0f);
    const unsigned long frames = buf.size() / 2;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenDefaultStream(&stream,
                                       0,          // no input
                                       2,          // stereo output
                                       paFloat32,
                                       kSampleRate,
                                       kFrames,
                                       nullptr,    // blocking mode
                                       nullptr);
    if (!check(err, "Pa_OpenDefaultStream")) return false;

    if (!check(Pa_StartStream(stream), "Pa_StartStream")) {
        Pa_CloseStream(stream);
        return false;
    }

    err = Pa_WriteStream(stream, buf.data(), frames);
    bool ok = true;
    if (err == paOutputUnderflowed) std::fprintf(stderr, "underflow\n");
    else ok = check(err, "Pa_WriteStream");

    ok &= check(Pa_StopStream(stream), "Pa_StopStream");   // waits for playback to finish
    ok &= check(Pa_CloseStream(stream), "Pa_CloseStream");
    return ok;
}

std::vector<float> speaker::decode() {
    if (!initialized_) return {};

    if (!in_) {
        const PaDeviceIndex dev = Pa_GetDefaultInputDevice();
        if (dev == paNoDevice) { std::fprintf(stderr, "no input device\n"); return {}; }
        const PaDeviceInfo* info = Pa_GetDeviceInfo(dev);

        PaStreamParameters in{};
        in.device           = dev;
        in.channelCount     = 1;
        in.sampleFormat     = paFloat32;
        in.suggestedLatency = std::max(0.05, info->defaultHighInputLatency);

        PaStream* stream = nullptr;
        // CoreAudio converts from the device's native rate if it isn't 48 kHz.
        if (!check(Pa_OpenStream(&stream, &in, nullptr, kSampleRate, paFramesPerBufferUnspecified,
                                 paClipOff, nullptr, nullptr), "Pa_OpenStream")) {
            return {};
        }
        if (!check(Pa_StartStream(stream), "Pa_StartStream")) {
            Pa_CloseStream(stream);
            return {};
        }
        in_ = stream;
        std::fprintf(stderr, "Listening on \"%s\"\n", info->name);
    }

    const PaError err = Pa_ReadStream(static_cast<PaStream*>(in_), chunk_.data(), chunk_.size());
    if (err == paInputOverflowed) {
        std::fprintf(stderr, "warning: input overflow (samples dropped)\n");
    } else if (!check(err, "Pa_ReadStream")) {
        return {};
    }

    if (!warnedSilence_) {
        for (float s : chunk_) allZero_ &= (s == 0.0f);
        if (allZero_ && ++silentChunks_ == 80) {   // 2 s of exact zeros
            std::fprintf(stderr,
                "warning: microphone is delivering pure digital silence. Check System Settings → "
                "Privacy & Security → Microphone for your terminal app.\n");
            warnedSilence_ = true;
        }
    }
    return chunk_;
}

void speaker::listInputDevices() {
    const PaDeviceIndex def = Pa_GetDefaultInputDevice();
    for (int i = 0; i < Pa_GetDeviceCount(); ++i) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
        if (d->maxInputChannels > 0)
            std::printf("%c[%d] %s  (%.0f Hz, %d in ch)\n", i == def ? '*' : ' ', i, d->name,
                        d->defaultSampleRate, d->maxInputChannels);
    }
}

bool speaker::readWav(const char* path, std::vector<float>& out, uint32_t& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return false; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        std::fprintf(stderr, "%s is not a WAV file\n", path);
        return false;
    }

    uint16_t fmt = 0, channels = 0, bits = 0;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    for (size_t pos = 12; pos + 8 <= d.size();) {
        const uint32_t size = le32(&d[pos + 4]);
        const uint8_t* body = &d[pos + 8];
        const size_t avail = std::min<size_t>(size, d.size() - pos - 8);
        if (!std::memcmp(&d[pos], "fmt ", 4) && avail >= 16) {
            fmt = le16(body); channels = le16(body + 2); rate = le32(body + 4); bits = le16(body + 14);
            if (fmt == 0xFFFE && avail >= 26) fmt = le16(body + 24);   // WAVE_FORMAT_EXTENSIBLE
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            data = body; dataLen = avail;
        }
        pos += 8 + size + (size & 1);
    }
    if (!data || channels == 0) { std::fprintf(stderr, "%s: missing fmt or data chunk\n", path); return false; }

    const size_t frameBytes = static_cast<size_t>(channels) * bits / 8;
    const size_t frames = dataLen / frameBytes;
    out.resize(frames);
    if (fmt == 1 && bits == 16) {
        for (size_t i = 0; i < frames; ++i) out[i] = static_cast<float>(static_cast<int16_t>(le16(data + i * frameBytes))) / 32768.0f;
    } else if (fmt == 3 && bits == 32) {
        for (size_t i = 0; i < frames; ++i) std::memcpy(&out[i], data + i * frameBytes, 4);
    } else {
        std::fprintf(stderr, "%s: unsupported format (need 16-bit PCM or 32-bit float)\n", path);
        return false;
    }
    return true;
}
