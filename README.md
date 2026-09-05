# GUSH

A stereo ambient processor for guitar, drum machine or synth: a granular tape
echo, a modal resonator and a shimmer reverb, all quietly moved around by a
function generator. Built to chase the sound of a Eurorack ambient rig —
Strymon Magneto, Microcell, StarLab, Mutable Rings, Make Noise Maths — inside
one plugin, in a signal chain you can actually follow.

VST3 + Standalone everywhere, AU on macOS. Zero latency.

---

## The chain

```
                                     ┌──────── MODULATION ────────┐
                                     │  LFO 1 · LFO 2 · Random    │
                                     │  Walk · Envelope Follower  │
                                     └──┬────────┬────────┬───────┘
                                        │        │        │
  in ──> INPUT ──┬──────────────── dry ─────────────────────────────────┐
                 │                      │        │        │             │
                 └──> GRANULAR ─────> RESONATOR ───> REVERB ──> TONE ────┤
                      TAPE ECHO                                         │
                                                                    MIX ─┴──> out
```

Each wet stage blends against whatever came into it, so the box can be a clean
multi-head tape echo, a bowed resonator, an endless cathedral, or all three
stacked. `MIX` at the end sets how much of any of it you hear.

| Stage | What it is | Where it came from |
|---|---|---|
| **Input** | gain + tape-style saturation | the pre-amp on any tape machine |
| **Granular tape echo** | 4-head tape delay, grain cloud, pitch, reverse, freeze | Magneto + Microcell (which is Mutable Clouds in 14hp) |
| **Resonator** | 28-mode modal filter bank | Rings |
| **Reverb** | 8-line FDN with an octave-up in the feedback path | StarLab |
| **Modulation** | 2 LFOs, a seeded random walk, an envelope follower, 4 routing slots | Maths |

---

## Architecture

```
Source/
  DSP/                     ← plain C++17. No JUCE. Compiles and tests anywhere.
    DspCommon.h            smoothers, filters, RNG, ADAA saturator, interpolation
    DelayBuffer.h          DelayLine · TapeBuffer (absolute-addressed) · ReverseReader
    PitchShifter.h         zero-latency dual-tap crossfading shifter
    InputStage.h           gain + pre-emphasis/tanh/de-emphasis tape colour
    GranularTapeEcho.*     heads, grain cloud, varispeed, reverse, freeze, regen
    ModalResonator.*       stiff-string partial series, damping, strike position
    Reverb.*               predelay → allpass diffusion → Hadamard FDN → shimmer
    Modulation.h           LFO · RandomWalk · EnvelopeFollower · ModMatrix
    Engine.*               owns the chain, runs control rate, applies modulation

  Parameters.*             APVTS layout + a cached lock-free read into EngineParams
  PluginProcessor.*        prepareToPlay / processBlock / state
  PluginEditor.*           generic editor — working, not designed

Tools/
  dsp_smoke_test.cpp       the whole engine, no JUCE, no DAW
```

**The DSP folder has no JUCE dependency at all.** That is deliberate: the
engine can be compiled, tested and profiled in half a second without a plugin
host, which is why the smoke test below exists and why it runs in CI-time
rather than in a DAW.

---

## The parts worth explaining

**Varispeed is the smoother.** Reading a delay line at `writePos - d(t)` plays
back at rate `1 - d'(t)`. So the delay-time smoother *is* the tape motor: move
the DELAY knob or pull TAPE SPEED down and the pitch bends and settles exactly
the way slowing a tape does. RAMP TIME sets how long the machine takes to get
there. No separate pitch code is involved.

**Fixed intervals cannot come from a glide,** because a glide has to stop. So
±12 comes from two other places: the grain playback rate, and a pitch shifter
inside the feedback loop, which is what makes each repeat land an octave from
the one before.

**The pitch shifter is two taps, not an FFT.** A delay pointer drifting against
the write pointer transposes; the wrap is a click; two taps half a window apart
with Hann envelopes that sum to exactly 1.0 hide the wrap, because each tap's
window is at zero precisely when that tap wraps. Zero latency, no FFT, and the
slight burble at an octave is the tape-flavoured artefact we actually want.
Measured accuracy: within 1.5% at ±12 semitones.

**Grains address the tape absolutely, not as "N samples ago".** A grain names a
point on the tape and reads from it at its own speed while the write head runs
on ahead. A "delay of N samples" address cannot express that.

**Freeze is an exact copy, not high feedback.** The first version forced
feedback to unity, and the loop still sagged 8 dB over eight seconds, because
the regen path runs through the head normalisation and panning. It now
re-writes the tape onto itself at a delay rounded to a whole sample, with no
gain, no damping and no clipper — reading at an integer position returns the
stored sample untouched, so the loop holds at 0.0 dB indefinitely. The test
suite checks this.

**Saturation is antiderivative anti-aliased, not oversampled.** ADAA integrates
tanh across each sample interval, killing most of the aliasing for the price of
one extra log — and costing zero latency, which matters when someone is playing
a guitar through it live.

**Randomness is seeded.** Every random decision comes from one number that is
saved with the preset, so the same seed renders bit-identical audio forever.
Roll until you like it, then it is locked. The test suite checks that too.

**Modulation runs at control rate** — one tick per 32 samples, ~1.5 kHz. Far
finer than any knob needs and 32× cheaper than per-sample.

---

## Building

Nothing to install but CMake and a compiler; JUCE is fetched automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Artefacts land in `build/GUSH_artefacts/Release/`, and `COPY_PLUGIN_AFTER_BUILD`
installs them to the usual plugin folder on macOS and Windows.

Run the DSP test on its own:

```sh
./build/gush_dsp_test
```

*Linux note:* JUCE needs the usual X11/freetype/ALSA dev packages
(`libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev
libasound2-dev`).

---

## What the test suite covers

`Tools/dsp_smoke_test.cpp` runs the real engine — the same `.cpp` files the
plugin links — and checks the things that only appear when audio has been
running for a while:

- pitch shifter transposition accuracy, measured by autocorrelation
- the full chain at 44.1 / 48 / 96 kHz
- seven hostile settings (feedback at 1.05, freeze + shimmer, reverse with
  octave regen, 120 grains/sec, a 40 Hz resonator with zero damping, and
  everything at once with all four mod slots at full depth) — no NaN, no runaway
- the reverb has a tail, and the tail ends
- FREEZE holds for eight seconds without sagging or climbing
- silence in produces exact silence out
- the same seed renders bit-identical audio, a different one does not
- cost: **~2.4% of one core** at 48 kHz with grains, resonator, shimmer and
  modulation all running (≈42× realtime)

All 40 checks pass.

---

## Parameters

| Group | Controls |
|---|---|
| Input | Input, Drive, Mix, Output |
| Tape echo | Delay Time, Tape Speed, Ramp Time, Heads (1/2/3/4), Feedback, Tape Tone, Pitch, Pitch Regen, Reverse, Freeze, Echo Mix |
| Grains | Grain Blend, Grain Size, Grain Density, Spray, Spread, Pitch Jitter |
| Resonator | Resonate, Res Tune, Structure, Brightness, Damping, Position |
| Reverb | Reverb, Size, Decay, Rev Damping, Shimmer, Pre-delay, Rev Movement, Rev Freeze |
| Tone | Cutoff, Resonance |
| Modulation | LFO 1/2 Rate + Shape, Random Rate, Random Glide, Seed, and 4 × (Source, Target, Amount) |

---

## Not done yet

- **The front panel.** The editor is JUCE's generic one: every parameter is
  there and automatable, nothing is designed. The DSP does not depend on it.
- **Tempo sync.** Delay time and LFO rates are free-running.
- **Presets.**
- **MIDI.** No note input; the resonator is tuned by a knob, not a keyboard.
