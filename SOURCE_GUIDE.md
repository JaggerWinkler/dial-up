# Dial-Up Source Guide

How the implemented files in `src/` work and how they connect. Line references are as of 2026-11-17; they will drift as the code changes.

## Pipeline

Each stage is one class in its own file. `encode` passes data **down** the stack (toward the speaker), `decode` passes it **up** (toward the application).

```
 TRANSMIT (encode)                                       RECEIVE (decode)
 ─────────────────                                       ────────────────
 payload (≤255 B) + dst MAC                              Frame {payload, dest, source}
        │                                                       ▲
        ▼                                                       │
 data_link_layer   ── frame bytes (17–272 B) ──────────▶  data_link_layer   CRC / address check
        │                                                       ▲
        ▼                                                       │
 reed_solomon      ── codeword bytes ──────────────────▶  reed_solomon      [stub: passthrough]
        │                                                       ▲
        ▼                                                       │
 interleaver       ── bytes ───────────────────────────▶  interleaver       [stub: passthrough]
        │                                                       ▲
        ▼                                                       │
 convolutional     ── packed coded bits ──┐       ┌── soft value per bit ──  convolutional  Viterbi
        │                                 │       │                          ▲
        ▼                                 ▼       │                          │
 fsk               ── mono float samples ─┐       └───────────────────  fsk  preamble sync + demod
        │                                 │                                  ▲
        ▼                                 ▼                                  │
 speaker  ───────────────▶  air  ───────────────▶  microphone ──  speaker (chunks of samples)
```

| File | Status | `encode` (down) | `decode` (up) |
|---|---|---|---|
| `data_link_layer.*` | implemented | dst MAC + payload → frame bytes | frame bytes → `optional<Frame>` |
| `util.*` | implemented | — (MAC lookup, shared types) | — |
| `reed_solomon.*` | stub | returns input unchanged | returns input unchanged |
| `interleaver.*` | stub | returns input unchanged | returns input unchanged |
| `convolutional.*` | implemented | bytes → packed coded bits | soft values → bytes |
| `modem.h` | implemented | — (FSK parameters) | — |
| `fsk.*` | implemented | packed bits → audio samples | mic samples → soft values (per frame) |
| `speaker.*` | implemented | play samples | read one mic chunk |

---

## `util.h` / `util.cpp`: shared types and MAC lookup

- [`util.h:14`](src/util.h#L14) `MAC_Address` is a 6-byte array; [`util.h:16`](src/util.h#L16) `BROADCAST_MAC` is `ff:ff:ff:ff:ff:ff`.
- [`util.h:18`](src/util.h#L18) `Frame` is what the data link layer hands up: `payload`, `dest`, `source`.
- [`util.h:32`](src/util.h#L32) `get_MAC(iface)` reads this machine's hardware address. It has two implementations:
  - Windows, [`util.cpp:29`](src/util.cpp#L29): `GetAdaptersAddresses`. With no `iface` it picks a physical adapter that is up and has a gateway ([`util.cpp:45-47`](src/util.cpp#L45-L47)).
  - macOS / Linux, [`util.cpp:105`](src/util.cpp#L105): `getifaddrs`. With no `iface`, [`defaultInterface`](src/util.cpp#L92) picks the first up, running, non-loopback, non-virtual interface with an IPv4 address. [`isVirtual`](src/util.cpp#L83) filters out docker, VPN and AWDL interfaces. [`linkAddress`](src/util.cpp#L64) pulls the MAC from the `AF_LINK` (macOS) or `AF_PACKET` (Linux) entry.

**Connects to:** `data_link_layer`, which calls `get_MAC` once in its constructor.

---

## `data_link_layer.h` / `.cpp`: framing, addressing, CRC-32

**Frame layout** ([`data_link_layer.cpp:61`](src/data_link_layer.cpp#L61)):

```
[dst MAC 6][src MAC 6][len 1][payload len][CRC-32 4]      17 to 272 bytes
```

- **Constructors** ([`data_link_layer.h:20`](src/data_link_layer.h#L20), [`:23`](src/data_link_layer.h#L23)): look up this device's MAC through `get_MAC` ([`lookup_MAC`, .cpp:26](src/data_link_layer.cpp#L26)), or take one directly to simulate two peers on one machine.
- **`calculate_CRC`** ([`.cpp:10`](src/data_link_layer.cpp#L10)): the Ethernet CRC-32. It uses the reflected polynomial `0xEDB88320`, init `0xFFFFFFFF`, with a final XOR.
- **`encode`** ([`.cpp:39`](src/data_link_layer.cpp#L39)):
  - throws if the payload is over 255 bytes ([`:40`](src/data_link_layer.cpp#L40));
  - writes dst, src, len and payload ([`:45-48`](src/data_link_layer.cpp#L45-L48));
  - appends the CRC least significant byte first, as Ethernet does ([`:50-56`](src/data_link_layer.cpp#L50-L56)).
- **`decode`** ([`.cpp:60`](src/data_link_layer.cpp#L60)) drops the frame (returns `nullopt`) if it is:
  - too short ([`:67`](src/data_link_layer.cpp#L67));
  - shorter than its length byte claims ([`:71-75`](src/data_link_layer.cpp#L71-L75));
  - failing the CRC ([`:83`](src/data_link_layer.cpp#L83));
  - addressed to neither this MAC nor broadcast ([`:92`](src/data_link_layer.cpp#L92)).

  The CRC position comes from the length byte, so any padding after the CRC is ignored. That lets Reed–Solomon pad the frame freely.

**Connects to:** `reed_solomon` (bytes in both directions). The application sits above it.

---

## `convolutional.h` / `.cpp`: rate-1/2 K=7 encoder and soft Viterbi decoder

The header ([`convolutional.h:11-13`](src/convolutional.h#L11-L13)) exposes only `encode` and `decode`. All parameters live in the `.cpp`.

### Parameters ([`convolutional.cpp:11-18`](src/convolutional.cpp#L11-L18))

| Name | Value | Meaning |
|---|---|---|
| `kConstraintLength` | 7 | K: the current bit plus 6 previous bits |
| `kTailBits` | 6 | zero bits appended to flush the encoder back to state 0 |
| `kG1`, `kG2` | `0171`, `0133` | generator polynomials (octal), one per output bit |
| `kStates` | 64 | trellis states = 2^(K−1) |

### Shared core: `step` ([`:27`](src/convolutional.cpp#L27))

- **Model.** The 7-bit shift register holds the new input bit in bit 0 and the six previous bits above it ([`:28`](src/convolutional.cpp#L28)).
- **Outputs.** The two output bits are the parity of `reg & G1` and `reg & G2`. The next state is the low 6 bits of the register ([`:29`](src/convolutional.cpp#L29)).
- **Shared by both sides.** Encoder and decoder both call `step`, so they can't disagree on the bit convention.

### Bit packing

- **Helpers.** [`getBit` / `setBit` (:32-33)](src/convolutional.cpp#L32-L33) read and write bit `i` of a byte vector, most significant bit first. `fsk` reads its input bits in the same order.
- **Coded size.** The output is `(bytes × 8 + 6) × 2` bits. That isn't a whole number of bytes in general, so the caller passes the exact bit count to `fsk::encode`.

### `encode` ([`:47`](src/convolutional.cpp#L47))

1. Loops over every data bit plus 6 tail zeros ([`:53-54`](src/convolutional.cpp#L53-L54)).
2. Each input bit emits two coded bits ([`:55-59`](src/convolutional.cpp#L55-L59)).
3. The tail leaves the encoder back in state 0 ([`:62`](src/convolutional.cpp#L62)). The decoder relies on that.

### `decode` ([`:65`](src/convolutional.cpp#L65))

1. **Soft input convention.** Each input value is P(bit = 1), in [0, 1]: 0.0 means surely a 0, 1.0 means surely a 1, 0.5 means no idea. Two values make one trellis step ([`:66`](src/convolutional.cpp#L66)).
2. **Start state.** Only state 0 starts with a finite path metric ([`:71-73`](src/convolutional.cpp#L71-L73)).
3. **Add-compare-select** ([`:83-101`](src/convolutional.cpp#L83-L101)):
   - For every live state and both possible input bits, the branch cost is the sum of [`bitCost`](src/convolutional.cpp#L39) for the two coded bits ([`:92`](src/convolutional.cpp#L92)).
   - `bitCost(s, e) = −log P(bit = e)`, clamped at P ≥ 1e-6 so a sure contradiction costs about 13.8 rather than infinity. Summed along a path, that is −log of the path's probability, so the cheapest path is the most likely one.
   - Each next state keeps only its cheapest incoming path.
   - It records which predecessor won as one bit: the predecessor's top state bit, the one shifted out ([`:97`](src/convolutional.cpp#L97)).
4. **Traceback from state 0** ([`:104-111`](src/convolutional.cpp#L104-L111)):
   - The input bit at step `t` is the low bit of the state entered at step `t` ([`:109`](src/convolutional.cpp#L109)).
   - The predecessor is rebuilt from the stored bit ([`:110`](src/convolutional.cpp#L110)).
   - Tail bits are dropped ([`:109`](src/convolutional.cpp#L109), `t < dataBits`).

**Old metric kept for comparison.** Building with `-DDIAL_UP_L1_METRIC` switches the branch cost back to `|s1 − out1| + |s2 − out2|` ([`:88-91`](src/convolutional.cpp#L88-L91)).

**Tested** with 326 random bytes:
- clean input and 3% random bit flips: 0 errors;
- heavy Gaussian noise: about 10% raw bit errors down to about 2%.

**Connects to:**
- **Down:** `interleaver::encode` output → `convolutional::encode` → `fsk::encode(bytes, nbits)`.
- **Up:** `fsk::decode` soft values → `convolutional::decode` → `interleaver::decode`.

---

## `modem.h`: FSK parameters shared by both peers

Every constant is labelled with its meaning and units in the file.

| Line | Name | Value | Used by |
|---|---|---|---|
| [9](src/modem.h#L9) | `kSampleRate` | 48000 Hz | fsk, speaker |
| [11](src/modem.h#L11) | `kWindow` | 480 samples (10 ms) | fsk: Goertzel window |
| [12](src/modem.h#L12) | `kSymbolSamples` | 480 samples | fsk: symbol length (no guard) |
| [14](src/modem.h#L14) | `kBitsPerSymbol` | 4 | fsk: bits per tone → 400 bit/s raw |
| [15](src/modem.h#L15) | `kTones` | 16 | fsk: Q in Q-FSK |
| [17](src/modem.h#L17) | `kBaseFreq` | 18000 Hz | fsk: tone 0 |
| [18](src/modem.h#L18) | `kToneSpacing` | 200 Hz | fsk: tone 15 = 21 kHz |
| [19](src/modem.h#L19) | `toneFreq(i)` | Hz | fsk: tone generation and Goertzel coefficients |
| [21](src/modem.h#L21) | `kBinHz` | 100 Hz | the `static_assert`s below it |
| [33](src/modem.h#L33) | `kPreambleLen` | 16 symbols | fsk |
| [35](src/modem.h#L35) | `kPreamble` | Costas array of tone indices | fsk: sent first, searched for on receive |
| [40](src/modem.h#L40) | `crc8` | — | legacy from the old rx/tx; nothing in `src/` uses it |

The `static_assert`s ([`:23-31`](src/modem.h#L23-L31)) check three things:
- every tone fits a whole number of cycles in one window, so tones don't leak into each other's bins;
- adjacent tones are at least 2 bins apart;
- the top tone stays below about 21.6 kHz.

---

## `fsk.h` / `fsk.cpp`: modulator, preamble, and streaming demodulator

### Interface ([`fsk.h`](src/fsk.h))

- [`fsk(frameBits, verbose)`, :19](src/fsk.h#L19): `frameBits` tells the receiver how many coded bits follow a preamble. Frames have a fixed size after Reed–Solomon, so the receiver needs no length field.
- [`encode(data, nbits)`, :29](src/fsk.h#L29): packed bits in, mono samples out.
- [`decode(samples, n)`, :38](src/fsk.h#L38): feed microphone audio in any chunk size. It returns soft values once a whole frame has been demodulated, otherwise `nullopt`.
- The receiver's state is hidden behind a pointer ([`:40-41`](src/fsk.h#L40-L41)), so the header doesn't expose it.

### Transmit path: [`fsk::encode` (fsk.cpp:364)](src/fsk.cpp#L364)

```
generateSoundData( prependPreamble( bitsToTones(data, nbits) ) )
```

1. [`bitsToTones` (:90)](src/fsk.cpp#L90):
   - reads 4 bits at a time, most significant bit first, and pads the last symbol with 0s ([`:98`](src/fsk.cpp#L98));
   - Gray-codes each value with [`grayEncode` (:80)](src/fsk.cpp#L80), so adjacent tones differ by one bit. A tone misread as its neighbour then costs one bit error, not up to four.
2. [`prependPreamble` (:108)](src/fsk.cpp#L108): puts the 16 Costas-array tones from `modem::kPreamble` in front of the data tones. The preamble is kept separate from data packing so either can change on its own.
3. [`generateSoundData` (:117)](src/fsk.cpp#L117):
   - **Continuous phase.** One phase accumulator runs across all symbols, so tone changes don't click.
   - **Fades.** Output starts and ends with a 20 ms raised-cosine fade ([`:119`](src/fsk.cpp#L119), [`:135`](src/fsk.cpp#L135)). The fade happens on extra audio added before the first symbol and after the last, so it never weakens a symbol the receiver analyses.
   - **Amplitude** is 0.5 ([`:26`](src/fsk.cpp#L26)).

### Receive path: [`fsk::Receiver` (:157)](src/fsk.cpp#L157)

It runs as a small state machine on one buffer: [`push` (:162)](src/fsk.cpp#L162) appends samples, then runs search or decode steps until it runs out of buffered audio.

**DSP helpers:**
- [`makeCoeffs` (:40)](src/fsk.cpp#L40) precomputes one Goertzel coefficient per tone.
- [`tonePowers` (:50)](src/fsk.cpp#L50) measures the power of all 16 tones over one 480-sample window.
- [`toFractions` (:66)](src/fsk.cpp#L66) divides by the total, so the result doesn't depend on volume.
- [`argmax` (:76)](src/fsk.cpp#L76) picks the strongest tone; [`grayDecode` (:82)](src/fsk.cpp#L82) reverses the Gray code.

**1. Search** ([`stepSearch` :221](src/fsk.cpp#L221)):
- **Candidate starts.** Every 60 samples (`kHop` = 1/8 symbol, [`:22`](src/fsk.cpp#L22)) it treats that point as a possible preamble start.
- **Score.** For each candidate it adds up, across the 16 slots, the fraction of energy on the expected preamble tone. A perfect lock scores about 16; noise scores about 1.
- **Reuse.** Window results are cached per hop in `fr_`, so each window is measured once.
- **Detection.** A score of 9 or more ([`:31`](src/fsk.cpp#L31)) starts tracking. It keeps going one more symbol to find the peak, then calls `lock`.

**2. Lock** ([`lock` :246](src/fsk.cpp#L246)):
- **Fine timing.** Refines the start to ±8 samples with [`scoreAt` (:215)](src/fsk.cpp#L215).
- **Equalization** ([`:259`](src/fsk.cpp#L259)). The preamble plays each tone exactly once, so its received power in its own slot shows how strongly speaker, room and mic pass that tone. Those per-tone gains go into `eq_`, with a floor of 25% of the mean.

**3. Demodulate** ([`stepDecode` :293](src/fsk.cpp#L293)):
1. Measures the tone powers for each symbol and divides by `eq_` ([`:298`](src/fsk.cpp#L298)).
2. Picks the strongest tone and Gray-decodes it.
3. If the strongest tone holds under 35% of the energy for 3 symbols in a row, the frame is dropped and search resumes ([`:305`](src/fsk.cpp#L305)).
4. Emits 4 soft values per symbol, one per bit ([`:314-325`](src/fsk.cpp#L314-L325); see below).
5. After `frameSymbols_` symbols, it marks the frame ready and goes back to searching ([`:329`](src/fsk.cpp#L329)).

**Housekeeping:** [`trim` (:336)](src/fsk.cpp#L336) throws away audio nothing can still refer to. [`reportLevel` (:348)](src/fsk.cpp#L348) prints the input level and best preamble score each second with `-v`.

**Soft values** ([`:314-325`](src/fsk.cpp#L314-L325)). `p` is each tone's equalized share of the symbol's energy, and it sums to 1. Each tone stands for one 4-bit value, `grayDecode(t)`. So for each bit:

```
P(bit = 1) = Σ p[t]  over the tones t whose grayDecode(t) has that bit set
```

- **Strong symbol:** one tone holds most of the energy, so every bit comes out near 0 or 1.
- **Weak symbol:** energy is spread out, so the bits drift toward 0.5 and Viterbi trusts them less.
- **Two neighbouring tones nearly tied:** because of Gray coding they differ in one bit. Only that bit goes toward 0.5; the other three stay confident.
- **Silent window:** all four bits are 0.5.

**Old hard bits kept for comparison.** Building with `-DDIAL_UP_HARD_BITS` emits 0.0 / 1.0 from the strongest tone instead ([`:310-313`](src/fsk.cpp#L310-L313)).

**Connects to:**
- **Down:** `convolutional::encode` → `fsk::encode` → `speaker::encode`.
- **Up:** `speaker::decode` chunks (or a WAV file) → `fsk::decode` → `convolutional::decode`.

---

## `speaker.h` / `speaker.cpp`: PortAudio playback and capture

- [Constructor / destructor (speaker.cpp:35, :39)](src/speaker.cpp#L35): `Pa_Initialize` and `Pa_Terminate`. The class can't be copied ([`speaker.h:18-19`](src/speaker.h#L18-L19)), so PortAudio is never shut down twice.
- **[`encode` (:47)](src/speaker.cpp#L47)** plays one transmission and blocks until it finishes:
  1. Builds `[100 ms silence][samples][250 ms silence]` as interleaved stereo ([`:50`](src/speaker.cpp#L50)). The leading silence covers device start-up. The trailing silence is there because CoreAudio can drop the last few tens of milliseconds when a stream stops ([`:18-19`](src/speaker.cpp#L18-L19)).
  2. Opens the default output ([`:58`](src/speaker.cpp#L58)) and writes it all in blocking mode ([`:73`](src/speaker.cpp#L73)).
  3. `Pa_StopStream` waits for playback to finish ([`:78`](src/speaker.cpp#L78)).
- **[`decode` (:83)](src/speaker.cpp#L83)** returns one 25 ms chunk of mono microphone audio (1200 samples, [`:17`](src/speaker.cpp#L17)) per call:
  - On the first call it opens the default input at 48 kHz ([`:99`](src/speaker.cpp#L99)).
  - Each call reads one chunk ([`:111`](src/speaker.cpp#L111)).
  - After 2 s of exact zeros it warns once that macOS microphone permission is probably missing ([`:120`](src/speaker.cpp#L120)).
- **Static helpers.** [`listInputDevices` (:130)](src/speaker.cpp#L130) prints the available microphones. [`readWav` (:140)](src/speaker.cpp#L140) loads a 16-bit or 32-bit-float WAV recording, so a capture can be fed to `fsk::decode` without a microphone.

**Connects to:** `fsk` only, through mono float samples at `modem::kSampleRate` in both directions.

---

## One frame, end to end

**Transmit:**
1. `data_link_layer::encode(dst, payload)` → up to 272 bytes.
2. `reed_solomon::encode` → 336 bytes per the spec (2 × RS(168,136)); currently a passthrough.
3. `interleaver::encode` → 336 bytes; currently a passthrough.
4. `convolutional::encode` → (336 × 8 + 6) × 2 = **5388 coded bits**, packed.
5. `fsk::encode(bytes, 5388)` → 16 preamble + 1347 data symbols = 13.6 s of audio at 10 ms per symbol (plus fades).
6. `speaker::encode(samples)` plays it.

**Receive:**
1. Loop: `speaker::decode()` → `fsk::decode(chunk)` until it returns 5388 soft values. The `fsk` object is built with `frameBits = 5388`.
2. `convolutional::decode(soft)` → 336 bytes.
3. `interleaver::decode` → `reed_solomon::decode` → `data_link_layer::decode` → `Frame`, or dropped.

## Comparing decoder variants

Two compile-time switches restore the older behaviour. Neither is defined by default, and they are independent, so all four combinations can be built:

| Macro | File | Defined = old behaviour | Default = new behaviour |
|---|---|---|---|
| `DIAL_UP_HARD_BITS` | `fsk.cpp` | 0.0 / 1.0 from the strongest tone | P(bit = 1) from the tone energy shares |
| `DIAL_UP_L1_METRIC` | `convolutional.cpp` | cost `|s − bit|` | cost `−log P(bit)` |

Set one with `-DDIAL_UP_HARD_BITS` on the compiler command line, or `target_compile_definitions(dial_up PRIVATE DIAL_UP_HARD_BITS)` in CMake.

**Simulated results (2026-11-19).** Setup:
- 8 frames of 336 random bytes (21,504 data bits) through `convolutional` → `fsk` → white noise → `fsk` → `convolutional`;
- light noise on the preamble, so every frame synced;
- the "signal lost" check turned off in a test copy of `fsk.cpp`, since otherwise it drops every frame before any bit goes wrong.

| Noise RMS on data | soft + −log P | soft + L1 | hard + either |
|---|---|---|---|
| 1.5 | 0 | 0 | 4 |
| 2.0 | 116 | 108 | 476 |
| 2.5 | 3925 | 3910 | 4456 |
| ≥ 3.0 | ~50% (decoder overwhelmed) | ~50% | ~50% |

Soft values cut errors about 4× where it matters. The two metrics are within noise of each other: the energy shares aren't calibrated probabilities, so −log P has nothing extra to work with yet. Real-room recordings may differ; the switches exist to check that.

## Open items

- `reed_solomon` and `interleaver` are passthrough stubs.
- **Frame size is hard-coded twice.** The 5388-bit size depends on the Reed–Solomon output size, and both the sender's `nbits` and the receiver's `frameBits` must match it. Nothing in the code computes it yet.
- **Soft values aren't calibrated probabilities.** Energy shares are softer than the true likelihoods, which is likely why the −log P metric hasn't beaten the old metric yet (see below).
- **The "signal lost" check fires before decoding fails.** In white-noise tests, the 3-weak-symbols-in-a-row check (`kMinSymbolConf`, [`fsk.cpp:35`](src/fsk.cpp#L35)) drops frames at noise levels the decoder could still mostly handle.
- **Legacy leftovers:**
  - `modem.h`'s header comment and `crc8` ([`:1`](src/modem.h#L1), [`:37-49`](src/modem.h#L37-L49)) still describe the old rx/tx frame format.
  - `rx.cpp` / `tx.cpp` in the repo root no longer build, since `modem.h` moved to `src/`.
