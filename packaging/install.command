#!/bin/bash
#
#  Double-click me. (Right-click -> Open the first time: this is unsigned,
#  because it was built by a robot and not by a company with an Apple
#  developer account.)
#
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
APP_DIR="$HOME/Applications"

echo ""
echo "Installing GUSH..."
mkdir -p "$VST3_DIR" "$AU_DIR" "$APP_DIR"

install_bundle() {
    local src="$1" dest="$2"
    [ -d "$src" ] || return 0
    rm -rf "${dest:?}/$(basename "$src")"
    cp -R "$src" "$dest/"
    # macOS quarantines anything that arrived from the internet, and a
    # quarantined plugin is silently refused by the host.
    xattr -dr com.apple.quarantine "$dest/$(basename "$src")" 2>/dev/null || true
    echo "  $dest/$(basename "$src")"
}

install_bundle GUSH.vst3      "$VST3_DIR"
install_bundle GUSH.component "$AU_DIR"
install_bundle GUSH.app       "$APP_DIR"

cat <<'DONE'

Installed.

  In Ableton:  Preferences -> Plug-Ins -> switch on VST3 (and Audio Units)
               -> Rescan -> drag GUSH onto an AUDIO track.

               It is an effect, not an instrument. It needs something going
               into it: a guitar input, the Digitakt, or an audio clip.

  With no DAW: open GUSH from your Applications folder and choose your
               interface in its audio settings.

DONE
echo "Press return to close."
read -r _
