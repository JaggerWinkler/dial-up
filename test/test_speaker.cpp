// File Name:     test_speaker.cpp
// Author:        Arthur Ufongene
// Course:        CS60: Computer Networks
// Assignment:    Lab 4: Physical Layer
// Date:          November 2026

// Description: testing WAV file reading on files written by the test

// speaker::readWav on WAV files written by the test (no audio hardware).
#include "check.h"
#include "src/speaker.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void put16(std::string& s, uint16_t v) { s += static_cast<char>(v & 0xff); s += static_cast<char>(v >> 8); }
void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xff); }

// RIFF/WAVE with one fmt chunk and (optionally) one data chunk.
std::string wav(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits, const std::string& data,
                bool withData = true) {
    std::string fmt;
    put16(fmt, format); put16(fmt, channels); put32(fmt, rate);
    put32(fmt, rate * channels * bits / 8); put16(fmt, static_cast<uint16_t>(channels * bits / 8)); put16(fmt, bits);

    std::string body = "WAVE";
    body += "fmt "; put32(body, static_cast<uint32_t>(fmt.size())); body += fmt;
    if (withData) { body += "data"; put32(body, static_cast<uint32_t>(data.size())); body += data; }

    std::string out = "RIFF";
    put32(out, static_cast<uint32_t>(body.size()));
    return out + body;
}

std::string writeTemp(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / ("dial_up_" + name);
    std::ofstream(path, std::ios::binary) << contents;
    return path.string();
}

}  // namespace

int main() {
    TEST("16-bit PCM mono") {
        const int16_t in[] = {0, 16384, -16384, 32767, -32768};
        std::string d;
        for (int16_t v : in) put16(d, static_cast<uint16_t>(v));
        const auto path = writeTemp("pcm16.wav", wav(1, 1, 44100, 16, d));

        std::vector<float> out;
        uint32_t rate = 0;
        CHECK(speaker::readWav(path.c_str(), out, rate));
        CHECK_EQ(rate, 44100u);
        CHECK_EQ(out.size(), 5u);
        if (out.size() == 5)
            for (int i = 0; i < 5; ++i) CHECK(std::fabs(out[i] - in[i] / 32768.0f) < 1.0f / 32768);
    }

    TEST("32-bit float stereo keeps the first channel") {
        const float left[] = {0.25f, -0.5f, 0.75f}, right[] = {9.0f, 9.0f, 9.0f};
        std::string d;
        for (int i = 0; i < 3; ++i) {
            uint32_t l, r;
            std::memcpy(&l, &left[i], 4);
            std::memcpy(&r, &right[i], 4);
            put32(d, l);
            put32(d, r);
        }
        const auto path = writeTemp("f32.wav", wav(3, 2, 48000, 32, d));

        std::vector<float> out;
        uint32_t rate = 0;
        CHECK(speaker::readWav(path.c_str(), out, rate));
        CHECK_EQ(rate, 48000u);
        CHECK(out == std::vector<float>(left, left + 3));
    }

    TEST("rejects bad files") {
        std::vector<float> out;
        uint32_t rate = 0;
        CHECK(!speaker::readWav(writeTemp("text.wav", "this is not a wav file").c_str(), out, rate));
        CHECK(!speaker::readWav(writeTemp("nodata.wav", wav(1, 1, 48000, 16, "", false)).c_str(), out, rate));
        CHECK(!speaker::readWav(writeTemp("pcm8.wav", wav(1, 1, 48000, 8, "abcd")).c_str(), out, rate));
        CHECK(!speaker::readWav("/nonexistent/dir/file.wav", out, rate));
    }

    return test::finish();
}
