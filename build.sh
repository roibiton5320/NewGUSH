#!/usr/bin/env bash
#
#  Build GUSH. One command, from a fresh clone.
#
#      ./build.sh              build everything, then run the DSP test
#      ./build.sh --fast       skip the universal binary (macOS, ~half the time)
#      ./build.sh --demos      also render demo WAVs into demos/
#      ./build.sh --clean      throw the build directory away and start over
#
set -euo pipefail

cd "$(dirname "$0")"

FAST=0; DEMOS=0; CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --fast)  FAST=1 ;;
        --demos) DEMOS=1 ;;
        --clean) CLEAN=1 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^#\s\?//'; exit 0 ;;
        *) echo "unknown option: $arg  (try --help)"; exit 1 ;;
    esac
done

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

#-- prerequisites -------------------------------------------------------------
command -v cmake >/dev/null || fail \
"cmake is not installed.
  macOS:    brew install cmake
  Ubuntu:   sudo apt install cmake ninja-build"

if [[ "$(uname)" == "Darwin" ]]; then
    xcode-select -p >/dev/null 2>&1 || fail \
"Xcode command line tools are missing. Run:
  xcode-select --install
then run this script again."
fi

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)

CONFIG=(-DCMAKE_BUILD_TYPE=Release)
[[ $FAST -eq 1 ]] && CONFIG+=(-DGUSH_UNIVERSAL=OFF)

[[ $CLEAN -eq 1 ]] && { say "Removing build/"; rm -rf build; }

#-- configure -----------------------------------------------------------------
say "Configuring (the first run downloads JUCE, so give it a minute)"
cmake -B build "${GEN[@]}" "${CONFIG[@]}"

#-- build ---------------------------------------------------------------------
say "Building"
cmake --build build --parallel

#-- prove the DSP is sane -----------------------------------------------------
say "Running the DSP test"
./build/gush_dsp_test

#-- optional demo render ------------------------------------------------------
if [[ $DEMOS -eq 1 ]]; then
    say "Rendering demos into demos/"
    mkdir -p demos
    ./build/gush_render demos
fi

#-- where everything went -----------------------------------------------------
say "Done. The plugin is here:"
find build/GUSH_artefacts -maxdepth 3 \
     \( -name '*.vst3' -o -name '*.component' -o -name 'GUSH' -o -name 'GUSH.app' \) \
     -print 2>/dev/null | sed 's/^/  /'

if [[ "$(uname)" == "Darwin" ]]; then
    cat <<'NEXT'

It has also been installed for you:
  ~/Library/Audio/Plug-Ins/VST3/GUSH.vst3
  ~/Library/Audio/Plug-Ins/Components/GUSH.component

In Ableton: Preferences -> Plug-Ins -> switch on VST3 (and Audio Units) ->
Rescan. Then drop GUSH onto an AUDIO track. It is an effect, so it needs
something going into it.

To try it with no DAW at all, open the standalone app printed above and pick
your interface in its audio settings.
NEXT
fi
