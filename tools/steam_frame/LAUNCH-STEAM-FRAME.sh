#!/usr/bin/env bash
# The devkit title WipEoutVR runs this (deploy.sh registers it). Steam starts it, which is also
# what hands the game the headset: SteamVR owns the controllers, so a binary started from SSH
# gets no input. Logs go to run-stdout.txt beside this script.
#
# Game data is not part of the package. Put it in $XDG_DATA_HOME/WipEoutVR/wipeout/
# (default ~/.local/share/WipEoutVR/wipeout/), laid out as upstream's README describes.
# Settings (vr.toml) and saves live in $XDG_DATA_HOME/WipEoutVR/ too; WIPEOUT_VR_<KEY>
# environment variables override a setting, e.g. WIPEOUT_VR_PROBE_SECONDS=10.
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

# The Frame runs gamescope with Xwayland on :0; the XR OpenGL binding needs SDL's x11 driver.
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export DISPLAY="${DISPLAY:-:0}"

data="${XDG_DATA_HOME:-$HOME/.local/share}/WipEoutVR"
mkdir -p "$data"
{
	echo "[launch] $(date -Is) $here/wipeout $*"
	echo "[launch] data dir $data"
	[ -d "$data/wipeout" ] || echo "[launch] no $data/wipeout/: the game will stop at asset loading"
} > run-stdout.txt

exec ./wipeout "$@" >> run-stdout.txt 2>&1
