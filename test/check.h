// File Name:     check.h
// Author:        Arthur Ufongene
// Course:        CS60: Computer Networks
// Assignment:    Lab 4: Physical Layer
// Date:          November 2026

// Description: a minimal test harness (CHECK / CHECK_EQ and a summary) plus shared test
// helpers

// check.h — a minimal test harness: CHECK / CHECK_EQ count failures, finish()
// prints a summary and gives main its exit code. Plus shared test helpers.
#ifndef DIAL_UP_TEST_CHECK_H
#define DIAL_UP_TEST_CHECK_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>

namespace test {

inline int g_checks = 0;
inline int g_failures = 0;

inline bool report(bool ok, const char* file, int line, const char* expr) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, expr);
    }
    return ok;
}

template <class A, class B>
bool reportEq(const A& a, const B& b, const char* file, int line, const char* ea, const char* eb) {
    const bool ok = (a == b);
    if (!report(ok, file, line, ea)) std::cerr << "    " << ea << " = " << a << ", " << eb << " = " << b << "\n";
    return ok;
}

// Run one named section; prints its name so failures are easy to place.
#define TEST(name) std::fprintf(stderr, "-- %s\n", name);

inline int finish() {
    std::fprintf(stderr, "%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Fixed seed: every run sees the same "random" data.
inline std::mt19937& rng() {
    static std::mt19937 r(12345);
    return r;
}

inline std::vector<uint8_t> randomBytes(size_t n) {
    std::uniform_int_distribution<int> d(0, 255);
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = static_cast<uint8_t>(d(rng()));
    return v;
}

inline void addNoise(std::vector<float>& x, double rms) {
    std::normal_distribution<float> d(0.0f, static_cast<float>(rms));
    for (auto& s : x) s += d(rng());
}

// Packed MSB-first bits → one exact soft value (0.0 / 1.0) per bit.
inline std::vector<float> softFromBytes(const std::vector<uint8_t>& bytes, size_t nbits) {
    std::vector<float> soft(nbits);
    for (size_t i = 0; i < nbits; ++i) soft[i] = static_cast<float>((bytes[i / 8] >> (7 - i % 8)) & 1);
    return soft;
}

inline int bitOf(const std::vector<uint8_t>& bytes, size_t i) { return (bytes[i / 8] >> (7 - i % 8)) & 1; }

}  // namespace test

#define CHECK(cond) ::test::report(static_cast<bool>(cond), __FILE__, __LINE__, #cond)
#define CHECK_EQ(a, b) ::test::reportEq((a), (b), __FILE__, __LINE__, #a, #b)

#endif // DIAL_UP_TEST_CHECK_H
