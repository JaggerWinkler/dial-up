# Dial-Up Source Guide

How `main.cpp` and the files in `src/` work and how they connect. Line references are as of 2026-11-19; they will drift as the code changes.

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
 reed_solomon      ── 336 codeword bytes ──────────────▶  reed_solomon      corrects ≤16 B/codeword
        │                                                       ▲
        ▼                                                       │
 interleaver       ── 336 bytes ───────────────────────▶  interleaver       restores byte order
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
| `reed_solomon.*` | implemented (libcorrect) | frame → 336 bytes (2 codewords) | 336 bytes → 272-byte frame, or `nullopt` |
| `interleaver.*` | implemented | 336 bytes → reordered 336 bytes | undoes the reordering |
| `convolutional.*` | implemented | bytes → packed coded bits | soft values → bytes |
| `modem.h` | implemented | — (FSK parameters) | — |
| `fsk.*` | implemented | packed bits → audio samples | mic samples → soft values (per frame) |
| `speaker.*` | implemented | play samples | read one mic chunk |
| `main.cpp`, `sender.*`, `receiver.*`, `file.*` | implemented | REPL: `/send`, `/file` (optional `@mac`) | prints text, saves files |

---

## `util.h` / `util.cpp`: shared types and MAC lookup

- [`util.h:17`](src/util.h#L17) `MAC_Address` is a 6-byte array; [`util.h:19`](src/util.h#L19) `BROADCAST_MAC` is `ff:ff:ff:ff:ff:ff`.
- [`util.h:21`](src/util.h#L21) `Frame` is what the data link layer hands up: `payload`, `dest`, `source`.
- [`util.h:35`](src/util.h#L35) `get_MAC(iface)` reads this machine's hardware address. It has two implementations:
  - Windows, [`util.cpp:34`](src/util.cpp#L34): `GetAdaptersAddresses`. With no `iface` it picks a physical adapter that is up and has a gateway ([`util.cpp:50-52`](src/util.cpp#L50-L52)).
  - macOS / Linux, [`util.cpp:109`](src/util.cpp#L109): `getifaddrs`. With no `iface`, [`defaultInterface`](src/util.cpp#L96) picks the first up, running, non-loopback, non-virtual interface with an IPv4 address. [`isVirtual`](src/util.cpp#L88) filters out docker, VPN and AWDL interfaces. [`linkAddress`](src/util.cpp#L69) pulls the MAC from the `AF_LINK` (macOS) or `AF_PACKET` (Linux) entry.

**Connects to:** `data_link_layer`, which calls `get_MAC` once in its constructor.

---

## `data_link_layer.h` / `.cpp`: framing, addressing, CRC-32

**Frame layout** ([`data_link_layer.cpp:64`](src/data_link_layer.cpp#L64)):

```
[dst MAC 6][src MAC 6][len 1][payload len][CRC-32 4]      17 to 272 bytes
```

- **Constructors** ([`data_link_layer.h:23`](src/data_link_layer.h#L23), [`:26`](src/data_link_layer.h#L26)): look up this device's MAC through `get_MAC` ([`lookup_MAC`, .cpp:29](src/data_link_layer.cpp#L29)), or take one directly to simulate two peers on one machine.
- **`calculate_CRC`** ([`.cpp:13`](src/data_link_layer.cpp#L13)): the Ethernet CRC-32. It uses the reflected polynomial `0xEDB88320`, init `0xFFFFFFFF`, with a final XOR.
- **`encode`** ([`.cpp:42`](src/data_link_layer.cpp#L42)):
  - throws if the payload is over 255 bytes ([`:43`](src/data_link_layer.cpp#L43));
  - writes dst, src, len and payload ([`:48-51`](src/data_link_layer.cpp#L48-L51));
  - appends the CRC least significant byte first, as Ethernet does ([`:53-59`](src/data_link_layer.cpp#L53-L59)).
- **`decode`** ([`.cpp:63`](src/data_link_layer.cpp#L63)) drops the frame (returns `nullopt`) if it is:
  - too short ([`:70`](src/data_link_layer.cpp#L70));
  - shorter than its length byte claims ([`:74-78`](src/data_link_layer.cpp#L74-L78));
  - failing the CRC ([`:86`](src/data_link_layer.cpp#L86));
  - addressed to neither this MAC nor broadcast ([`:95`](src/data_link_layer.cpp#L95)).

  The CRC position comes from the length byte, so any padding after the CRC is ignored. That lets Reed–Solomon pad the frame freely.

**Connects to:** `reed_solomon` (bytes in both directions). The application sits above it.

---

## `reed_solomon.h` / `.cpp`: outer error correction (libcorrect)

**Sizes** ([`reed_solomon.h:23-28`](src/reed_solomon.h#L23-L28)): the frame is padded to 272 bytes and split into 2 blocks of 136. Each block gains 32 parity bytes, giving 2 RS(168, 136) codewords and 336 bytes in total. Each codeword corrects up to 16 bad bytes.

- **Library.** The constructor ([`reed_solomon.cpp:17`](src/reed_solomon.cpp#L17)) creates a libcorrect RS(255, 223) codec with field polynomial `0x11d` and 32 roots; the 168-byte codewords are a shortened form of it. libcorrect is downloaded by CMake (`FetchContent`), and only its Reed–Solomon sources are built (`correct_rs`).
- **One handle per instance.** The object owns the libcorrect handle, so it can't be copied ([`reed_solomon.h:34-35`](src/reed_solomon.h#L34-L35)). The handle keeps scratch buffers, so `main.cpp` gives the send side and the receive thread one instance each.
- **`encode`** ([`:27`](src/reed_solomon.cpp#L27)):
  - throws if the frame is over 272 bytes ([`:29`](src/reed_solomon.cpp#L29));
  - zero-pads it to 272 ([`:32`](src/reed_solomon.cpp#L32));
  - encodes each 136-byte block into a 168-byte codeword ([`:35`](src/reed_solomon.cpp#L35)).
- **`decode`** ([`:40`](src/reed_solomon.cpp#L40)): returns `nullopt` if the input isn't 336 bytes ([`:41`](src/reed_solomon.cpp#L41)) or any codeword has too many errors ([`:46`](src/reed_solomon.cpp#L46)). Otherwise it returns the 272-byte padded frame. `data_link_layer::decode` finds the real end from the length byte, so the padding is harmless.

**Connects to:** `data_link_layer` above and `interleaver` below. Its fixed 336-byte output is what fixes the physical frame size (5388 coded bits).

---

## `interleaver.h` / `.cpp`: spreading bursts across codewords

- **Layout** ([`interleaver.cpp:17`](src/interleaver.cpp#L17)): a matrix with one row per Reed–Solomon codeword (2 rows of 168 bytes).
- **`encode`** ([`:27`](src/interleaver.cpp#L27)) writes rows and reads columns ([`:33`](src/interleaver.cpp#L33)), so transmitted bytes alternate between the two codewords. A burst of noise then costs each codeword only half of its damaged bytes.
- **`decode`** ([`:39`](src/interleaver.cpp#L39)) does the inverse ([`:45`](src/interleaver.cpp#L45)).
- **Size check:** both throw `std::length_error` if the size isn't a multiple of the row count ([`:20`](src/interleaver.cpp#L20)). The convolutional decoder always returns 336 bytes, so this can't happen in the normal pipeline.

**Connects to:** `reed_solomon` above and `convolutional` below.

---

## `convolutional.h` / `.cpp`: rate-1/2 K=7 encoder and soft Viterbi decoder

The header ([`convolutional.h:18-20`](src/convolutional.h#L18-L20)) exposes only `encode` and `decode`. All parameters live in the `.cpp`.

### Parameters ([`convolutional.cpp:19-26`](src/convolutional.cpp#L19-L26))

| Name | Value | Meaning |
|---|---|---|
| `kConstraintLength` | 7 | K: the current bit plus 6 previous bits |
| `kTailBits` | 6 | zero bits appended to flush the encoder back to state 0 |
| `kG1`, `kG2` | `0171`, `0133` | generator polynomials (octal), one per output bit |
| `kStates` | 64 | trellis states = 2^(K−1) |

### Shared core: `step` ([`:35`](src/convolutional.cpp#L35))

- **Model.** The 7-bit shift register holds the new input bit in bit 0 and the six previous bits above it ([`:36`](src/convolutional.cpp#L36)).
- **Outputs.** The two output bits are the parity of `reg & G1` and `reg & G2`. The next state is the low 6 bits of the register ([`:37`](src/convolutional.cpp#L37)).
- **Shared by both sides.** Encoder and decoder both call `step`, so they can't disagree on the bit convention.

### Bit packing

- **Helpers.** [`getBit` / `setBit` (:40-41)](src/convolutional.cpp#L40-L41) read and write bit `i` of a byte vector, most significant bit first. `fsk` reads its input bits in the same order.
- **Coded size.** The output is `(bytes × 8 + 6) × 2` bits. That isn't a whole number of bytes in general, so the caller passes the exact bit count to `fsk::encode`.

### `encode` ([`:55`](src/convolutional.cpp#L55))

1. Loops over every data bit plus 6 tail zeros ([`:61-62`](src/convolutional.cpp#L61-L62)).
2. Each input bit emits two coded bits ([`:63-67`](src/convolutional.cpp#L63-L67)).
3. The tail leaves the encoder back in state 0 ([`:70`](src/convolutional.cpp#L70)). The decoder relies on that.

### `decode` ([`:73`](src/convolutional.cpp#L73))

1. **Soft input convention.** Each input value is P(bit = 1), in [0, 1]: 0.0 means surely a 0, 1.0 means surely a 1, 0.5 means no idea. Two values make one trellis step ([`:74`](src/convolutional.cpp#L74)).
2. **Start state.** Only state 0 starts with a finite path metric ([`:79-81`](src/convolutional.cpp#L79-L81)).
3. **Add-compare-select** ([`:91-109`](src/convolutional.cpp#L91-L109)):
   - For every live state and both possible input bits, the branch cost is the sum of [`bitCost`](src/convolutional.cpp#L47) for the two coded bits ([`:100`](src/convolutional.cpp#L100)).
   - `bitCost(s, e) = −log P(bit = e)`, clamped at P ≥ 1e-6 so a sure contradiction costs about 13.8 rather than infinity. Summed along a path, that is −log of the path's probability, so the cheapest path is the most likely one.
   - Each next state keeps only its cheapest incoming path.
   - It records which predecessor won as one bit: the predecessor's top state bit, the one shifted out ([`:105`](src/convolutional.cpp#L105)).
4. **Traceback from state 0** ([`:112-119`](src/convolutional.cpp#L112-L119)):
   - The input bit at step `t` is the low bit of the state entered at step `t` ([`:117`](src/convolutional.cpp#L117)).
   - The predecessor is rebuilt from the stored bit ([`:118`](src/convolutional.cpp#L118)).
   - Tail bits are dropped ([`:117`](src/convolutional.cpp#L117), `t < dataBits`).

**Old metric kept for comparison.** Building with `-DDIAL_UP_L1_METRIC` switches the branch cost back to `|s1 − out1| + |s2 − out2|` ([`:96-99`](src/convolutional.cpp#L96-L99)).

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
| [16](src/modem.h#L16) | `kSampleRate` | 48000 Hz | fsk, speaker |
| [18](src/modem.h#L18) | `kWindow` | 480 samples (10 ms) | fsk: Goertzel window |
| [19](src/modem.h#L19) | `kSymbolSamples` | 480 samples | fsk: symbol length (no guard) |
| [21](src/modem.h#L21) | `kBitsPerSymbol` | 4 | fsk: bits per tone → 400 bit/s raw |
| [22](src/modem.h#L22) | `kTones` | 16 | fsk: Q in Q-FSK |
| [24](src/modem.h#L24) | `kBaseFreq` | 18000 Hz | fsk: tone 0 |
| [25](src/modem.h#L25) | `kToneSpacing` | 200 Hz | fsk: tone 15 = 21 kHz |
| [26](src/modem.h#L26) | `toneFreq(i)` | Hz | fsk: tone generation and Goertzel coefficients |
| [28](src/modem.h#L28) | `kBinHz` | 100 Hz | the `static_assert`s below it |
| [40](src/modem.h#L40) | `kPreambleLen` | 16 symbols | fsk |
| [42](src/modem.h#L42) | `kPreamble` | Costas array of tone indices | fsk: sent first, searched for on receive |

The `static_assert`s ([`:30-38`](src/modem.h#L30-L38)) check three things:
- every tone fits a whole number of cycles in one window, so tones don't leak into each other's bins;
- adjacent tones are at least 2 bins apart;
- the top tone stays below about 21.6 kHz.

---

## `fsk.h` / `fsk.cpp`: modulator, preamble, and streaming demodulator

### Interface ([`fsk.h`](src/fsk.h))

- [`fsk(frameBits, verbose)`, :26](src/fsk.h#L26): `frameBits` tells the receiver how many coded bits follow a preamble. Frames have a fixed size after Reed–Solomon, so the receiver needs no length field.
- [`encode(data, nbits)`, :36](src/fsk.h#L36): packed bits in, mono samples out.
- [`decode(samples, n)`, :45](src/fsk.h#L45): feed microphone audio in any chunk size. It returns soft values once a whole frame has been demodulated, otherwise `nullopt`.
- The receiver's state is hidden behind a pointer ([`:47-48`](src/fsk.h#L47-L48)), so the header doesn't expose it.

### Transmit path: [`fsk::encode` (fsk.cpp:371)](src/fsk.cpp#L371)

```
generateSoundData( prependPreamble( bitsToTones(data, nbits) ) )
```

1. [`bitsToTones` (:97)](src/fsk.cpp#L97):
   - reads 4 bits at a time, most significant bit first, and pads the last symbol with 0s ([`:105`](src/fsk.cpp#L105));
   - Gray-codes each value with [`grayEncode` (:87)](src/fsk.cpp#L87), so adjacent tones differ by one bit. A tone misread as its neighbour then costs one bit error, not up to four.
2. [`prependPreamble` (:115)](src/fsk.cpp#L115): puts the 16 Costas-array tones from `modem::kPreamble` in front of the data tones. The preamble is kept separate from data packing so either can change on its own.
3. [`generateSoundData` (:124)](src/fsk.cpp#L124):
   - **Continuous phase.** One phase accumulator runs across all symbols, so tone changes don't click.
   - **Fades.** Output starts and ends with a 20 ms raised-cosine fade ([`:126`](src/fsk.cpp#L126), [`:142`](src/fsk.cpp#L142)). The fade happens on extra audio added before the first symbol and after the last, so it never weakens a symbol the receiver analyses.
   - **Amplitude** is 0.75 ([`:33`](src/fsk.cpp#L33)).

### Receive path: [`fsk::Receiver` (:164)](src/fsk.cpp#L164)

It runs as a small state machine on one buffer: [`push` (:169)](src/fsk.cpp#L169) appends samples, then runs search or decode steps until it runs out of buffered audio.

**DSP helpers:**
- [`makeCoeffs` (:47)](src/fsk.cpp#L47) precomputes one Goertzel coefficient per tone.
- [`tonePowers` (:57)](src/fsk.cpp#L57) measures the power of all 16 tones over one 480-sample window.
- [`toFractions` (:73)](src/fsk.cpp#L73) divides by the total, so the result doesn't depend on volume.
- [`argmax` (:83)](src/fsk.cpp#L83) picks the strongest tone; [`grayDecode` (:89)](src/fsk.cpp#L89) reverses the Gray code.

**1. Search** ([`stepSearch` :228](src/fsk.cpp#L228)):
- **Candidate starts.** Every 60 samples (`kHop` = 1/8 symbol, [`:29`](src/fsk.cpp#L29)) it treats that point as a possible preamble start.
- **Score.** For each candidate it adds up, across the 16 slots, the fraction of energy on the expected preamble tone. A perfect lock scores about 16; noise scores about 1.
- **Reuse.** Window results are cached per hop in `fr_`, so each window is measured once.
- **Detection.** A score of 9 or more ([`:38`](src/fsk.cpp#L38)) starts tracking. It keeps going one more symbol to find the peak, then calls `lock`.

**2. Lock** ([`lock` :253](src/fsk.cpp#L253)):
- **Fine timing.** Refines the start to ±8 samples with [`scoreAt` (:222)](src/fsk.cpp#L222).
- **Equalization** ([`:266`](src/fsk.cpp#L266)). The preamble plays each tone exactly once, so its received power in its own slot shows how strongly speaker, room and mic pass that tone. Those per-tone gains go into `eq_`, with a floor of 25% of the mean.

**3. Demodulate** ([`stepDecode` :300](src/fsk.cpp#L300)):
1. Measures the tone powers for each symbol and divides by `eq_` ([`:305`](src/fsk.cpp#L305)).
2. Picks the strongest tone and Gray-decodes it.
3. If the strongest tone holds under 20% of the energy for 3 symbols in a row, the frame is dropped and search resumes ([`:312`](src/fsk.cpp#L312)).
4. Emits 4 soft values per symbol, one per bit ([`:321-332`](src/fsk.cpp#L321-L332); see below).
5. After `frameSymbols_` symbols, it marks the frame ready and goes back to searching ([`:336`](src/fsk.cpp#L336)).

**Housekeeping:** [`trim` (:343)](src/fsk.cpp#L343) throws away audio nothing can still refer to. [`reportLevel` (:355)](src/fsk.cpp#L355) prints the input level and best preamble score each second with `-v`.

**Soft values** ([`:321-332`](src/fsk.cpp#L321-L332)). `p` is each tone's equalized share of the symbol's energy, and it sums to 1. Each tone stands for one 4-bit value, `grayDecode(t)`. So for each bit:

```
P(bit = 1) = Σ p[t]  over the tones t whose grayDecode(t) has that bit set
```

- **Strong symbol:** one tone holds most of the energy, so every bit comes out near 0 or 1.
- **Weak symbol:** energy is spread out, so the bits drift toward 0.5 and Viterbi trusts them less.
- **Two neighbouring tones nearly tied:** because of Gray coding they differ in one bit. Only that bit goes toward 0.5; the other three stay confident.
- **Silent window:** all four bits are 0.5.

**Old hard bits kept for comparison.** Building with `-DDIAL_UP_HARD_BITS` emits 0.0 / 1.0 from the strongest tone instead ([`:317-320`](src/fsk.cpp#L317-L320)).

**Connects to:**
- **Down:** `convolutional::encode` → `fsk::encode` → `speaker::encode`.
- **Up:** `speaker::decode` chunks (or a WAV file) → `fsk::decode` → `convolutional::decode`.

---

## `speaker.h` / `speaker.cpp`: PortAudio playback and capture

- [Constructor / destructor (speaker.cpp:40, :44)](src/speaker.cpp#L40): `Pa_Initialize` and `Pa_Terminate`. The class can't be copied ([`speaker.h:25-26`](src/speaker.h#L25-L26)), so PortAudio is never shut down twice.
- **[`encode` (:52)](src/speaker.cpp#L52)** plays one transmission and blocks until it finishes:
  1. Builds `[100 ms silence][samples][250 ms silence]` as interleaved stereo ([`:55`](src/speaker.cpp#L55)). The leading silence covers device start-up. The trailing silence is there because CoreAudio can drop the last few tens of milliseconds when a stream stops ([`:23-24`](src/speaker.cpp#L23-L24)).
  2. Opens the default output device as a stereo stream with at least 100 ms of latency ([`:72`](src/speaker.cpp#L72)) and writes it all in blocking mode ([`:81`](src/speaker.cpp#L81)).
  3. `Pa_StopStream` waits for playback to finish ([`:86`](src/speaker.cpp#L86)).
- **[`decode` (:91)](src/speaker.cpp#L91)** returns one 25 ms chunk of mono microphone audio (1200 samples, [`:22`](src/speaker.cpp#L22)) per call:
  - On the first call it opens the default input at 48 kHz ([`:107`](src/speaker.cpp#L107)).
  - Each call reads one chunk ([`:119`](src/speaker.cpp#L119)).
  - After 2 s of exact zeros it warns once that macOS microphone permission is probably missing ([`:128`](src/speaker.cpp#L128)).
- **Static helpers.** [`listInputDevices` (:138)](src/speaker.cpp#L138) prints the available microphones. [`readWav` (:148)](src/speaker.cpp#L148) loads a 16-bit or 32-bit-float WAV recording, so a capture can be fed to `fsk::decode` without a microphone.

**Connects to:** `fsk` only, through mono float samples at `modem::kSampleRate` in both directions.

---

## The app: `main.cpp`, `sender`, `receiver`, `file`, `message.h`, `console`

| File | Job |
|---|---|
| `main.cpp` | flags, startup, REPL |
| [`src/sender.*`](src/sender.h) | builds payloads and pushes them down the stack |
| [`src/receiver.*`](src/receiver.h) | background mic loop; pushes frames up the stack and acts on them |
| [`src/file.*`](src/file.h) | file splitting, `/file` search, reassembly and saving |
| [`src/message.h`](src/message.h) | message type bytes, `kCodedBits`, `kFrameSeconds` |
| [`src/console.*`](src/console.h) | mutex-guarded `out` / `err` / `prompt`, `macToString` |

```
dial_up [-s] [-r] [-v] [--mac aa:bb:cc:dd:ee:ff]
```

**Flags** ([`parseArgs`, main.cpp:60](main.cpp#L60)):
- **`-s` / `-r`:** send only or receive only. Neither or both means both. Combined flags like `-sr` work.
- **`-v`:** turns on the demodulator's diagnostics.
- **`--mac`:** replaces the hardware MAC, so two processes on one machine can act as two peers.

**Startup** ([`main`, main.cpp:139](main.cpp#L139)):
1. Creates `received/` ([`:144`](main.cpp#L144); the path is `file::kReceivedDir`).
2. Gets the MAC from `--mac` or `get_MAC()` ([`:150`](main.cpp#L150)).
3. If receiving, builds a `receiver` and runs it on its own thread ([`:167`](main.cpp#L167)).
4. Runs the REPL on the main thread ([`repl`, :98](main.cpp#L98)).

**Commands:**

| Command | What it does |
|---|---|
| `/send [@mac] <text>` | one text frame, up to 254 bytes ([`sender::sendText`, sender.cpp:40](src/sender.cpp#L40)) |
| `/file [@mac] <name>` | an existing path, or a recursive search of the current directory by filename ([`file::find`, file.cpp:81](src/file.cpp#L81)); several matches are listed instead of guessing |
| `/help` | list commands |
| `/quit` | exit (Ctrl-D also works) |

- **Recipient:** an optional `@aa:bb:cc:dd:ee:ff` right after the command sends to that device only ([`main.cpp:114-123`](main.cpp#L114-L123)); without it the message goes to `BROADCAST_MAC`. The receiving `data_link_layer` drops frames addressed to anyone else ([`data_link_layer.cpp:96`](src/data_link_layer.cpp#L96)).
- **Receive-only mode:** `/send` and `/file` print "sending is disabled (-r)".
- **Sending blocks the prompt,** at about 14 s per frame ([`message::kFrameSeconds`, message.h:39](src/message.h#L39)).

### Message format (inside the ≤ 255-byte data-link payload)

Byte 0 is the type ([`message.h:28`](src/message.h#L28)). Multi-byte numbers are big-endian.

```
text        [0x00][UTF-8 text, ≤ 254 B]
file chunk  [0x01][transfer id 1][chunk index 2][chunk count 2]            ← 6-byte header
              chunk 0:  [name length 1][name ≤ 64 B][file data …]
              others:   [file data …]
```

### Splitting a file ([`file::buildChunks`, file.cpp:45](src/file.cpp#L45))

- **Chunk sizes.** Chunk 0 carries the name plus 248 − (name length) bytes of data; every later chunk carries 249. A 1000-byte `photo.bin` becomes 5 frames.
- **Transfer id.** Each `/file` gets the next id, starting from a random byte ([`sender.cpp:25`](src/sender.cpp#L25)), so a new transfer never merges with an older one.
- **Limits.** Up to 65,535 chunks; names longer than 64 bytes are shortened, keeping the extension ([`file::wireName`, file.cpp:73](src/file.cpp#L73)).

### Reassembling a file ([`file::assembler`, file.h:60](src/file.h#L60))

- **Key:** each transfer is keyed by (sender MAC, transfer id). It holds one slot per chunk, the name, a count of chunks received, and the time of the last chunk.
- **New chunk** ([`addChunk`, file.cpp:108](src/file.cpp#L108)):
  - A different chunk count under the same key means a new transfer reused the id, so the old entry is dropped.
  - Duplicates are ignored.
  - Each chunk prints `receiving <name> from <MAC>: k/N`, with `?` as the name until chunk 0 arrives.
  - Chunks may arrive in any order.
- **Complete:** once every slot is filled, the chunks are written in order to `received/` and `received <name> from <MAC>` is printed ([`save`, file.cpp:168](src/file.cpp#L168)).
- **Lost chunks:** a transfer with no new chunk for 3 frame times (~41 s, [`kTransferTimeout`, file.h:34](src/file.h#L34)) is discarded with an "incomplete" message ([`expire`, file.cpp:153](src/file.cpp#L153)). There are no acknowledgements, so nothing is resent.
- **Safe saving:** names are cut to their basename, so `../x` can't leave `received/`, and an existing name becomes `name (1).ext`, `name (2).ext`, and so on (`safeName` / `uniquePath`, [file.cpp:25-37](src/file.cpp#L25-L37)).

### Wiring the stack

- **Send** ([`sender::send`, sender.cpp:34](src/sender.cpp#L34)): data link (to `dest`) → Reed–Solomon → interleave → convolutional → FSK (5388 bits, [`message::kCodedBits`, message.h:36](src/message.h#L36)) → speaker.
- **Receive** ([`receiver::run`, receiver.cpp:39](src/receiver.cpp#L39)):
  - Mic chunks feed the receiver's `fsk` demodulator. Each completed frame goes through [`demodulate` (receiver.cpp:59)](src/receiver.cpp#L59): Viterbi → deinterleave → Reed–Solomon → data link.
  - Failures are logged to stderr and the frame is dropped.
  - [`onFrame` (receiver.cpp:71)](src/receiver.cpp#L71) prints text as `[MAC] text`, with control bytes replaced so a message can't drive the terminal ([`printableText`, :24](src/receiver.cpp#L24)), and hands file chunks to the assembler.
- **Self-echo:** in send + receive mode the mic hears our own speaker, so frames whose source is our MAC are ignored ([`receiver.cpp:72`](src/receiver.cpp#L72)).
- **Threads:**
  - The REPL and the receive thread share one `speaker`. The receive thread uses the input stream and sending opens output streams.
  - `sender` and `receiver` each own a `reed_solomon` ([`sender.h:38`](src/sender.h#L38), [`receiver.h:41`](src/receiver.h#L41)).
  - Console output goes through one mutex ([`console.cpp:16`](src/console.cpp#L16)).

---

## One frame, end to end

**Transmit:**
1. `sender` builds a text or file-chunk payload (≤ 255 bytes).
2. `data_link_layer::encode(dest, payload)` (`dest` is `BROADCAST_MAC` unless `@mac` was given) → up to 272 bytes.
3. `reed_solomon::encode` → 336 bytes (2 × RS(168,136)).
4. `interleaver::encode` → 336 bytes, alternating between the codewords.
5. `convolutional::encode` → (336 × 8 + 6) × 2 = **5388 coded bits**, packed.
6. `fsk::encode(bytes, 5388)` → 16 preamble + 1347 data symbols = 13.6 s of audio at 10 ms per symbol (plus fades).
7. `speaker::encode(samples)` plays it.

**Receive:**
1. Loop: `speaker::decode()` → `fsk::decode(chunk)` until it returns 5388 soft values. The `fsk` object is built with `frameBits = 5388`.
2. `convolutional::decode(soft)` → 336 bytes.
3. `interleaver::decode` → `reed_solomon::decode` → `data_link_layer::decode` → `Frame`, or dropped.
4. `receiver` prints the text or adds the file chunk to its transfer.

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

## Build and tests

- **One library, two kinds of consumer.** `CMakeLists.txt` builds every `src/*.cpp` into one static library, `dial_up_core`, which links PortAudio, `correct_rs` and Threads. `dial_up` is just `main.cpp` linked against it, and every test in `test/` links the same library.
- **Test harness.** [`test/check.h`](test/check.h) holds `CHECK` / `CHECK_EQ` and a seeded RNG, so every run sees the same data.
- **Labels.** Tests labelled `unit` need no hardware. The two labelled `audio` (`test_speaker_audio`, `test_end_to_end`) use the real speaker and microphone.
- **No hard-coded parameters.** The tests read every value from `modem.h`, `reed_solomon.h` and `message.h`, so changing the tone plan or frame size doesn't break them.
- Commands for running them are in the README.

## Open items

- **No retransmission.** A file chunk lost to noise loses the whole file; the receiver reports it as incomplete after ~41 s.
- **Shared `speaker` across threads.** PortAudio doesn't promise thread safety. Separate input and output streams work on CoreAudio; if they don't elsewhere, add a mutex around stream open/close in `speaker`.
- **`kTailBits` is duplicated.** `message.h` repeats the convolutional code's 6 tail bits to compute `kCodedBits`, since `convolutional.h` no longer exports it.
- **Soft values aren't calibrated probabilities.** Energy shares are softer than the true likelihoods, which is likely why the −log P metric hasn't beaten the old metric yet (see "Comparing decoder variants").
- **The "signal lost" check fires before decoding fails.** In white-noise tests, the 3-weak-symbols-in-a-row check (`kMinSymbolConf`, [`fsk.cpp:42`](src/fsk.cpp#L42)) drops frames at noise levels the decoder could still mostly handle.
