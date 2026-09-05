# GUSH

A stereo ambient processor: granular tape echo → modal resonator → shimmer
reverb, with a function generator moving all of it. VST3 / AU / Standalone,
built on JUCE.

**This is a standalone project.** One person, one repository, one plugin. It
has no parent project, no sibling repositories, no shared files with anything
else, and no split of ownership between people or folders. If a session ever
finds itself reasoning about another codebase's conventions here, that context
does not belong to this repo — drop it and read this file instead.

Read `README.md` for the architecture, the signal chain and the reasoning
behind the parts that are not obvious. This file is the working rules.

## The one structural rule

**`Source/DSP/` must never include JUCE.**

Everything in there is plain C++17: `<cmath>`, `<vector>`, `<array>`,
`<cstdint>`. Only the four files above it — `PluginProcessor`, `PluginEditor`,
`Parameters` — know JUCE exists.

This is not tidiness. It is why the whole engine compiles and runs in under a
second without a plugin host, which is what makes `Tools/dsp_smoke_test.cpp`
possible. Adding one `#include <juce_...>` inside `Source/DSP/` costs that
permanently. If the DSP needs something JUCE provides, write it in
`DspCommon.h`.

## Audio-thread rules, non-negotiable

Anything reachable from `Engine::process`:

- No allocation. No `new`, no `std::vector::resize`, no `std::string`. All
  buffers are sized in `prepare()`.
- No locks, no file or network I/O, no logging.
- Denormal-guard every recursive state — use `flush()` from `DspCommon.h`.
- Recompute coefficients at control rate (one tick per 32 samples), never per
  sample. `Engine::applyControlBlock` is where that happens.
- Every continuous parameter reaching the signal path goes through a
  `Smoother`. A knob that zippers is a bug.
- No clicks at grain, loop or crossfade boundaries. Every crossfade in this
  codebase uses two Hann windows half a period apart, because they sum to
  exactly 1.0.

## Before you commit

Build and run the test. It takes seconds and it has already caught a real
regression (a FREEZE that quietly sagged 8 dB over eight seconds):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/gush_dsp_test
```

All 40 checks must pass. If you change DSP behaviour on purpose, update the
test to assert the new behaviour rather than loosening a threshold.

**Add a test for anything that can only fail after time passes** — decay,
freeze drift, feedback runaway, denormal collapse. Those are the failures a
compiler and a quick listen both miss.

## Conventions

- JUCE house style: 4 spaces, braces on their own line, `camelCase` members,
  a space before the parenthesis in a call.
- Comments explain *why*, not *what*. The DSP here is full of decisions that
  look arbitrary until the reason is stated — say the reason.
- Never commit `build/`, plugin binaries or presets. They regenerate.

## Not done yet

The designed front panel (the editor is JUCE's generic one on purpose), tempo
sync, presets, MIDI note input.
