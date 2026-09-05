# Building GUSH

You need two things installed once, then one command. No code involved.

## Once

**macOS**

```sh
xcode-select --install     # Apple's compiler. Skip if you already have Xcode.
brew install cmake ninja   # If brew is missing: https://brew.sh
```

**Windows** — install [Visual Studio Community](https://visualstudio.microsoft.com/)
with the "Desktop development with C++" workload, and
[CMake](https://cmake.org/download/).

**Linux**

```sh
sudo apt install cmake ninja-build build-essential \
     libasound2-dev libx11-dev libxrandr-dev libxinerama-dev \
     libxcursor-dev libxcomposite-dev libfreetype6-dev
```

## Every time

```sh
git clone https://github.com/roibiton5320/NewGUSH.git
cd NewGUSH
./build.sh
```

That is it. The script downloads JUCE, builds VST3, AU and a standalone app,
runs the DSP test to prove the engine is sane, and tells you where everything
landed. On macOS it also installs the plugin for you.

The first build takes 5–15 minutes, almost all of it JUCE compiling once.
Every build after that takes seconds.

| Flag | What it does |
|---|---|
| `./build.sh --fast` | Skips the universal binary. Roughly half the time — but see the Rosetta note below. |
| `./build.sh --demos` | Also renders demo WAVs into `demos/`, so you can hear it without a DAW. |
| `./build.sh --clean` | Throws `build/` away and starts fresh. Try this first if anything is weird. |

## Getting it into Ableton

1. Live → Preferences → **Plug-Ins**
2. Switch on **Use VST3 Plug-In System Folders** (and **Use Audio Units** if you
   prefer AU)
3. Hit **Rescan**
4. Drag **GUSH** onto an **audio track**

It is an effect, not an instrument, so it needs something going into it — a
guitar input, the Digitakt, or an audio clip. On a MIDI track with a synth,
put GUSH after the synth in the chain.

## No DAW at all

The build also makes a standalone app. Open it, pick your interface in its
audio settings, and play. Fastest way to hear the thing.

## When it does not work

**"GUSH does not appear in Ableton."** Rescan again with the Live preferences
window closed and reopened. If it is still missing, check the file is actually
at `~/Library/Audio/Plug-Ins/VST3/GUSH.vst3`.

**"It appears but will not load."** Almost always the Rosetta trap: if Live is
running under Rosetta on an Apple Silicon Mac, it cannot load an arm64-only
plugin, and it does not tell you that. Build without `--fast` and it works —
that is exactly what the universal build is for.

**"macOS says the plugin cannot be opened."** It is unsigned, because it was
built on your own machine. Right-click it in Finder → Open, once.

**The build fails partway.** `./build.sh --clean` and try again. A half-finished
JUCE download is the usual cause.

**AU specifically.** macOS caches Audio Unit scans aggressively. Force a
revalidation with:

```sh
auval -a | grep -i gush        # is it registered at all?
killall -9 AudioComponentRegistrar
```

## Changing the sound

Everything that makes noise lives in `Source/DSP/`, and none of it depends on
JUCE, so you can rebuild and re-test the engine alone in about a second:

```sh
cmake --build build --target gush_dsp_test && ./build/gush_dsp_test
```

Run that after any change. It catches the failures a quick listen will not —
decay drifting, freeze sagging, feedback running away. `README.md` explains what
each part does and why.
