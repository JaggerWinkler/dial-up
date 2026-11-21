# Dial Up: Networking over sound

Send text messages and files between computers using near-ultrasonic sound (18–21 kHz)
from the speaker to the microphone. Each message goes down a small network stack and
back up on the other side:

```
data link (MACs, CRC-32) → Reed–Solomon → interleaver → convolutional (rate 1/2) → 16-FSK → speaker
                                                                                          ⋮ air
speaker ← FSK demodulator (soft bits) ← Viterbi ← deinterleaver ← Reed–Solomon ← data link ← mic
```

The full design is in [`spec/spec.pdf`](spec/spec.pdf). A file-by-file walkthrough of the
code is in [`SOURCE_GUIDE.md`](SOURCE_GUIDE.md).

## Download

Prebuilt binaries for Windows, macOS (Apple Silicon and Intel) and Linux are on the
[Releases](https://github.com/JaggerWinkler/dial-up/releases) page. Each is a single file
with nothing else to install. Run it from a terminal:

- **Windows:** double-click `dial_up-windows.exe`, or run it from PowerShell. If
  SmartScreen warns about an unknown publisher, click *More info → Run anyway*.
- **macOS:** the binary is unsigned, so clear the download quarantine first:
  `xattr -d com.apple.quarantine dial_up-macos && chmod +x dial_up-macos && ./dial_up-macos`
- **Linux:** pick the file matching `uname -m`: `dial_up-linux-x86_64` for Intel/AMD,
  `dial_up-linux-arm64` for `aarch64` (Raspberry Pi, Linux VMs on Apple Silicon Macs). Then
  `chmod +x dial_up-linux-* && ./dial_up-linux-*` (needs ALSA, which every desktop distro has).


## Requirements (building from source)

- CMake ≥ 3.25 and a C++20 compiler (Xcode's clang on macOS, MSVC on Windows)
- Linux only: ALSA headers (`sudo apt install libasound2-dev`)

```sh
brew install cmake
```

PortAudio and libcorrect (for Reed–Solomon) are downloaded and linked statically the
first time CMake runs.

## Build

From the repository root:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release   # once, to configure
cmake --build build                          # after every change
```

This produces `build/dial_up` and the test programs in `build/test/`. If the `build`
folder gets into a bad state, delete it and run both commands again.

Please build in `build/` (`-B build`). Running `cmake .` in the repository root writes
generated files such as a `Makefile` next to the source.

## Run

```sh
./build/dial_up [-s] [-r] [-v] [--mac aa:bb:cc:dd:ee:ff]
```

| Flag | Meaning |
|---|---|
| `-s` | send only (never opens the microphone) |
| `-r` | receive only |
| *(neither, or both)* | send and receive |
| `-v` | print demodulator diagnostics: input level, preamble score, per-tone gains |
| `--mac` | use this MAC address instead of the computer's own |

On startup the program prints its MAC address, creates a `received/` folder in the
current directory, and shows a `>` prompt:

| Command | What it does |
|---|---|
| `/send [@mac] <text>` | send a text message (up to 254 bytes) |
| `/file [@mac] <name>` | send a file, given as a path or a filename to search for under the current directory |
| `/help` | list the commands |
| `/quit` | exit (Ctrl-D works too) |

- **Destination:** with `@aa:bb:cc:dd:ee:ff`, only that device accepts the message.
  Without it, the message is broadcast to everyone listening.
- **Received text** is printed as `[sender MAC] text`.
- **Received files** are saved to `received/`, and the program prints
  `received <name> from <MAC>`.
- **Speed:** each frame takes a few seconds of air time, and sending blocks the prompt
  until it finishes. A file is sent as one frame per ~249 bytes.

### Try it on one computer

A computer ignores frames carrying its own MAC, because its mic hears its own speaker.
To run two peers on one machine, give each one a made-up MAC:

```sh
# terminal 1
./build/dial_up -r --mac 02:00:00:00:00:01

# terminal 2
./build/dial_up -s --mac 02:00:00:00:00:02
> /send hello
> /file README.md
```

### Two computers

Run `./build/dial_up` on both, with the volume up and the computers close together.
Each one prints its MAC at startup; use it with `@` to message one computer directly.

### macOS microphone permission

The first time the program records, macOS asks to let your terminal app use the
microphone. If you said no, the mic delivers pure silence and the program warns you.
To fix it, enable your terminal in **System Settings → Privacy & Security → Microphone**,
then restart the terminal.

## Tests

The tests are in [`test/`](test/), and CTest runs them.

```sh
cmake --build build
ctest --test-dir build -L unit  --output-on-failure   # no audio needed, about 2 s
ctest --test-dir build -L audio --output-on-failure   # plays sound: speakers on, quiet room, mic permission
ctest --test-dir build                                # both
```

| Test | Label | Checks |
|---|---|---|
| `test_convolutional` | unit | output size, encoder impulse response, Viterbi round trip with bit errors and erasures |
| `test_reed_solomon` | unit | padding, correcting 16 byte errors per codeword, rejecting bad input |
| `test_fsk` | unit | signal length, round trip through noise and an uneven channel, no false frames, back-to-back frames |
| `test_speaker` | unit | WAV parsing (16-bit PCM, 32-bit float, stereo, bad files) |
| `test_pipeline` | unit | the whole stack, payload → samples → payload, including MAC filtering |
| `test_speaker_audio` | audio | plays a tone on the speaker and finds it in the mic recording |
| `test_end_to_end` | audio | one peer plays a frame over the air, and another decodes it from the mic |

A test program can also be run on its own, for example
`./build/test/test_end_to_end -v`, which adds the demodulator diagnostics.

## Decoder variants

Two compile-time switches restore older decoder behaviour, so it can be compared with
the current one:

```sh
cmake -B build -DCMAKE_CXX_FLAGS="-DDIAL_UP_HARD_BITS"   # hard 0/1 bits instead of soft probabilities
cmake -B build -DCMAKE_CXX_FLAGS="-DDIAL_UP_L1_METRIC"   # |s − bit| Viterbi cost instead of −log P
```

[`SOURCE_GUIDE.md`](SOURCE_GUIDE.md#comparing-decoder-variants) explains each switch and
gives simulated results.

## Third-party licenses

### libcorrect

Reed–Solomon encoding and decoding uses [libcorrect](https://github.com/quiet/libcorrect),
which is compiled into the `dial_up` binaries under the following license:

```
Copyright (c) 2016, Brian Armstrong
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of
   conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other materials
   provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used
   to endorse or promote products derived from this software without specific prior
   written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### PortAudio

Audio input and output uses [PortAudio](https://www.portaudio.com) v19.7.0, which is
compiled into the `dial_up` binaries under the following license:

```
PortAudio Portable Real-Time Audio Library
Copyright (c) 1999-2006 Ross Bencina and Phil Burk

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

The text above constitutes the entire PortAudio license; however, the PortAudio community
also makes the following non-binding requests:

Any person wishing to distribute modifications to the Software is requested to send the
modifications to the original developer so that they can be incorporated into the
canonical version. It is also requested that these non-binding requests be included along
with the license above.
```
