#include "fsk.h"

#include "modem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>

namespace {

using modem::kSampleRate;
using modem::kTones;
using modem::kPreambleLen;
using modem::kPreamble;

constexpr int    kBitsPerSymbol  = modem::kBitsPerSymbol;
constexpr int    kWindow         = modem::kWindow;          // samples analysed per symbol
constexpr int    kSym            = modem::kSymbolSamples;   // symbol stride
constexpr int    kHopsPerSymbol  = 8;
constexpr int    kHop            = kSym / kHopsPerSymbol;   // search step
constexpr double kTwoPi          = 6.283185307179586;
static_assert(kSym % kHopsPerSymbol == 0, "search hop must divide the symbol");

constexpr double kAmp         = 0.5;    // speakers are weaker near 20 kHz
constexpr double kFadeSeconds = 0.020;  // slow enough that switching on/off isn't heard

// Preamble score = sum over the 16 slots of the fraction of in-band energy on
// the expected tone. Perfect lock ≈ 16, noise ≈ 1.
constexpr double kDetectThreshold = 9.0;

// A data symbol whose strongest tone holds less than this fraction of the
// energy is treated as "no signal". Several in a row abort the frame.
constexpr float kMinSymbolConf  = 0.35f;
constexpr int   kMaxWeakSymbols = 3;

using Powers = std::array<float, kTones>;

std::array<double, kTones> makeCoeffs() {
    std::array<double, kTones> c{};
    for (int t = 0; t < kTones; ++t) c[t] = 2.0 * std::cos(kTwoPi * modem::toneFreq(t) / kSampleRate);
    return c;
}
const std::array<double, kTones> kCoeff = makeCoeffs();

// Goertzel power of every tone over one analysis window x[0..kWindow).
// Each tone fits a whole number of cycles in kWindow samples, so they don't leak
// into each other's bins when the window is aligned.
Powers tonePowers(const float* x) {
    Powers p{};
    for (int t = 0; t < kTones; ++t) {
        const double c = kCoeff[t];
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < kWindow; ++i) {
            const double s = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        p[t] = static_cast<float>(s1 * s1 + s2 * s2 - c * s1 * s2);
    }
    return p;
}

// Normalize to fractions of total in-band energy (volume independent).
Powers toFractions(Powers p) {
    double sum = 0.0;
    for (float v : p) sum += v;
    if (sum <= 1e-20) { p.fill(0.0f); return p; }
    for (float& v : p) v = static_cast<float>(v / sum);
    return p;
}

Powers fractions(const float* x) { return toFractions(tonePowers(x)); }

int argmax(const Powers& p) {
    return static_cast<int>(std::max_element(p.begin(), p.end()) - p.begin());
}

int grayEncode(int n) { return n ^ (n >> 1); }

int grayDecode(int g) {   // inverse of n ^ (n >> 1) for 4-bit values
    g ^= g >> 1;
    g ^= g >> 2;
    return g;
}

// Tone index for every data symbol: kBitsPerSymbol bits each, read MSB first
// from the packed bytes, Gray-coded. A partial last symbol is padded with 0 bits.
std::vector<int> bitsToTones(const std::vector<uint8_t>& data, size_t nbits) {
    std::vector<int> tones;
    nbits = std::min(nbits, data.size() * 8);
    tones.reserve((nbits + kBitsPerSymbol - 1) / kBitsPerSymbol);
    for (size_t pos = 0; pos < nbits; pos += kBitsPerSymbol) {
        int value = 0;
        for (int b = 0; b < kBitsPerSymbol; ++b) {
            const size_t i = pos + b;
            const int bit = i < nbits ? (data[i / 8] >> (7 - i % 8)) & 1 : 0;
            value = (value << 1) | bit;
        }
        tones.push_back(grayEncode(value));
    }
    return tones;
}

// Put the preamble's tones in front of the data tones, so the receiver can
// detect the frame and lock onto its symbol timing.
std::vector<int> prependPreamble(const std::vector<int>& dataTones) {
    std::vector<int> tones(kPreamble, kPreamble + kPreambleLen);
    tones.insert(tones.end(), dataTones.begin(), dataTones.end());
    return tones;
}

// One phase accumulator across every symbol → continuous-phase FSK.
// The first and last tones are extended by the fade length, so the fades
// happen outside the symbols and never attenuate a symbol the receiver analyses.
std::vector<float> generateSoundData(const std::vector<int>& tones) {
    const int n_symbols = static_cast<int>(tones.size());
    const int fade = static_cast<int>(kFadeSeconds * kSampleRate);
    const int num_samples = kSym * n_symbols + 2 * fade;
    std::vector<float> out(static_cast<size_t>(num_samples));

    double phase = 0;
    for (int i = 0; i < num_samples; i++) {
        const int t = i - fade;   // time relative to the first symbol's start
        const int sym = t < 0 ? 0 : std::min(t / kSym, n_symbols - 1);
        const double inc = kTwoPi * modem::toneFreq(tones[sym]) / kSampleRate;

        out[i] = static_cast<float>(std::sin(phase) * kAmp);

        phase += inc;
        if (phase >= kTwoPi) phase -= kTwoPi;
    }

    // Raised-cosine fade in/out on the extensions.
    for (int i = 0; i < fade; ++i) {
        const float g = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi / 2 * i / fade));
        out[i] *= g;
        out[num_samples - 1 - i] *= g;
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Streaming receiver. Feed it samples in any chunk size with push().
//
// Search: every kHop samples, treat that point as a possible symbol start and
// compute tone fractions for its analysis window. Because
// kSym is a multiple of kHop, the window for preamble
// slot k at candidate start s is the one computed at hop s + 8k, so every
// candidate's score is a sum of 16 stored values; nothing is recomputed.
// When a score crosses the threshold, keep going for one symbol to find the
// peak, refine the position to ±8 samples, then demodulate symbol by symbol.
// ---------------------------------------------------------------------------
class fsk::Receiver {
public:
    Receiver(size_t frameBits, bool verbose)
        : frameSymbols_((frameBits + kBitsPerSymbol - 1) / kBitsPerSymbol), verbose_(verbose) {}

    std::optional<std::vector<float>> push(const float* x, size_t n) {
        buf_.insert(buf_.end(), x, x + n);
        for (size_t i = 0; i < n; ++i) levelSum_ += double(x[i]) * x[i];
        levelCount_ += n;

        while (state_ == State::Search ? stepSearch() : stepDecode()) {}
        trim();

        if (levelCount_ >= static_cast<size_t>(kSampleRate)) reportLevel();

        if (!ready_) return std::nullopt;
        ready_ = false;
        return std::move(soft_);
    }

private:
    enum class State { Search, Decode };

    // --- sample buffer with absolute indexing --------------------------------
    std::vector<float> buf_;
    long long base_ = 0;  // absolute sample index of buf_[0]

    bool avail(long long pos, long long len) const {
        return pos >= base_ && pos + len <= base_ + static_cast<long long>(buf_.size());
    }
    const float* at(long long pos) const { return buf_.data() + (pos - base_); }

    // --- search state --------------------------------------------------------
    State state_ = State::Search;
    long long searchStart_ = 0;     // absolute sample of hop 0
    long long nextHop_ = 0;
    std::deque<Powers> fr_;          // fractions per hop
    long long frBase_ = 0;           // hop index of fr_[0]
    static constexpr size_t kFrKeep = kPreambleLen * kHopsPerSymbol + 2 * kHopsPerSymbol;
    bool tracking_ = false;
    long long best_ = 0;
    double bestScore_ = 0.0;

    // --- decode state --------------------------------------------------------
    size_t frameSymbols_;
    long long dataStart_ = 0;
    long long nextSym_ = 0;
    int weakRun_ = 0;
    std::vector<float> soft_;
    bool ready_ = false;
    Powers eq_{};   // per-tone received power, measured from this frame's preamble

    // --- diagnostics ---------------------------------------------------------
    bool verbose_;
    double levelSum_ = 0.0;
    size_t levelCount_ = 0;
    double maxScoreThisSecond_ = 0.0;

    double scoreAt(long long pos) const {
        double sc = 0.0;
        for (int k = 0; k < kPreambleLen; ++k) sc += fractions(at(pos + static_cast<long long>(k) * kSym))[kPreamble[k]];
        return sc;
    }

    bool stepSearch() {
        const long long start = searchStart_ + nextHop_ * kHop;
        if (!avail(start, kWindow)) return false;

        fr_.push_back(fractions(at(start)));
        if (fr_.size() > kFrKeep) { fr_.pop_front(); ++frBase_; }

        const long long s = nextHop_ - (kPreambleLen - 1) * kHopsPerSymbol;
        ++nextHop_;
        if (s < frBase_) return true;    // not enough history yet

        double sc = 0.0;
        for (int k = 0; k < kPreambleLen; ++k)
            sc += fr_[static_cast<size_t>(s + k * kHopsPerSymbol - frBase_)][kPreamble[k]];
        maxScoreThisSecond_ = std::max(maxScoreThisSecond_, sc);

        if (!tracking_) {
            if (sc >= kDetectThreshold) { tracking_ = true; best_ = s; bestScore_ = sc; }
        } else {
            if (sc > bestScore_) { best_ = s; bestScore_ = sc; }
            if (s >= best_ + kHopsPerSymbol) lock();   // peak is behind us
        }
        return true;
    }

    void lock() {
        const long long coarse = searchStart_ + best_ * kHop;
        long long bestPos = coarse;
        double bestSc = -1.0;
        for (int d = -kHop; d <= kHop; d += 8) {
            const long long p = coarse + d;
            if (!avail(p, static_cast<long long>(kPreambleLen) * kSym)) continue;
            const double sc = scoreAt(p);
            if (sc > bestSc) { bestSc = sc; bestPos = p; }
        }
        std::fprintf(stderr, "[%7.3f s] preamble detected (score %.1f/16)\n",
                     double(bestPos) / kSampleRate, bestSc);

        // Channel equalization: the preamble sends each tone exactly once, so
        // its power in that slot is how strongly the speaker/room/mic pass that
        // tone. Decoding divides it back out, so a resonant tone can't win
        // symbols that aren't its own. Floor at 25% of the mean (at most +6 dB boost) so a tone that
        // was barely heard (speaker roll-off) isn't boosted into pure noise.
        double mean = 0.0;
        for (int k = 0; k < kPreambleLen; ++k) {
            eq_[kPreamble[k]] = tonePowers(at(bestPos + static_cast<long long>(k) * kSym))[kPreamble[k]];
            mean += eq_[kPreamble[k]];
        }
        mean /= kTones;
        for (float& g : eq_) g = static_cast<float>(std::max<double>(g, 0.25 * mean) / std::max(mean, 1e-20));
        if (verbose_) {
            std::fprintf(stderr, "          tone gains (dB vs mean):");
            for (int t = 0; t < kTones; ++t) std::fprintf(stderr, " %+.0f", 10.0 * std::log10(eq_[t]));
            std::fprintf(stderr, "\n");
        }

        state_ = State::Decode;
        dataStart_ = bestPos + static_cast<long long>(kPreambleLen) * kSym;
        nextSym_ = 0;
        weakRun_ = 0;
        soft_.clear();
    }

    void resumeSearch(long long pos) {
        state_ = State::Search;
        searchStart_ = std::max(pos, base_);
        nextHop_ = 0;
        fr_.clear();
        frBase_ = 0;
        tracking_ = false;
    }

    bool stepDecode() {
        const long long start = dataStart_ + nextSym_ * kSym;
        if (!avail(start, kWindow)) return false;

        Powers raw = tonePowers(at(start));
        for (int t = 0; t < kTones; ++t) raw[t] /= eq_[t];
        const Powers p = toFractions(raw);
        const int g = argmax(p);
        ++nextSym_;

        weakRun_ = (p[g] < kMinSymbolConf) ? weakRun_ + 1 : 0;
        if (weakRun_ >= kMaxWeakSymbols) {
            std::fprintf(stderr, "          signal lost after %lld symbols, frame dropped\n", nextSym_);
            resumeSearch(start + kSym - kMaxWeakSymbols * static_cast<long long>(kSym));
            return true;
        }

#ifdef DIAL_UP_HARD_BITS
        // Old: hard bits (0.0 / 1.0) from the strongest tone only.
        const int value = grayDecode(g);
        for (int b = kBitsPerSymbol - 1; b >= 0; --b) soft_.push_back(float((value >> b) & 1));
#else
        // Soft bits: P(bit = 1) = total energy share of every tone whose
        // (Gray-decoded) value has that bit set. p sums to 1, so this is in [0, 1].
        // A silent window (p all zero) gives 0.5 = no information.
        double total = 0.0;
        for (float v : p) total += v;
        for (int b = kBitsPerSymbol - 1; b >= 0; --b) {
            double ones = 0.0;
            for (int t = 0; t < kTones; ++t)
                if ((grayDecode(t) >> b) & 1) ones += p[t];
            soft_.push_back(total > 0.0 ? static_cast<float>(ones / total) : 0.5f);
        }
#endif

        if (static_cast<size_t>(nextSym_) == frameSymbols_) {
            ready_ = true;
            resumeSearch(start + kSym);
        }
        return true;
    }

    // Drop samples nothing can refer to any more.
    void trim() {
        long long keep;
        if (state_ == State::Search)
            keep = searchStart_ + std::max(0LL, nextHop_ - static_cast<long long>(kFrKeep) - kHopsPerSymbol) * kHop - kHop;
        else
            keep = dataStart_ + std::max(0LL, nextSym_ - kMaxWeakSymbols - 1) * kSym;
        if (keep - base_ > kSampleRate) {
            buf_.erase(buf_.begin(), buf_.begin() + (keep - base_));
            base_ = keep;
        }
    }

    void reportLevel() {
        if (verbose_) {
            const double rms = std::sqrt(levelSum_ / double(levelCount_));
            const double db = rms > 0 ? 20.0 * std::log10(rms) : -999.0;
            std::fprintf(stderr, "level %6.1f dBFS   best preamble score %4.1f/16%s\n", db,
                         maxScoreThisSecond_, state_ == State::Decode ? "   (decoding)" : "");
        }
        levelSum_ = 0.0;
        levelCount_ = 0;
        maxScoreThisSecond_ = 0.0;
    }
};

fsk::fsk(size_t frameBits, bool verbose) : rx_(std::make_unique<Receiver>(frameBits, verbose)) {}
fsk::~fsk() = default;

std::vector<float> fsk::encode(const std::vector<uint8_t>& data, size_t nbits) {
    return generateSoundData(prependPreamble(bitsToTones(data, nbits)));
}

std::optional<std::vector<float>> fsk::decode(const float* samples, size_t n) {
    return rx_->push(samples, n);
}
