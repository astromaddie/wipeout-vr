#!/usr/bin/env bash
# Stages the Linux VR build for the Steam Frame, run inside the build container:
#
#   tools/package_linux.sh <build-dir> <out-dir>
#
# The package holds the wipeout binary (RPATH $ORIGIN/lib), the shared libraries it needs
# that the Frame's Steam Linux Runtime does not provide (SDL2, GLEW), the launcher, and
# nothing else. It never contains game data: that goes in $XDG_DATA_HOME/WipEoutVR/wipeout/.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:?build dir}"
out="${2:?output dir}"

rm -rf "$out"
mkdir -p "$out/lib"
cp "$build/wipeout" "$out/wipeout"
cp "$root/tools/steam_frame/LAUNCH-STEAM-FRAME.sh" "$out/LAUNCH-STEAM-FRAME.sh"
chmod +x "$out/wipeout" "$out/LAUNCH-STEAM-FRAME.sh"

# The libraries the binary needs that are not system libraries (see check_linux_abi.py)
for lib in $(python3 "$root/tools/check_linux_abi.py" --list-unbundled "$out/wipeout"); do
	path="$(ldd "$out/wipeout" | awk -v lib="$lib" '$1 == lib { print $3 }')"
	[ -n "$path" ] || { echo "cannot find $lib" >&2; exit 1; }
	cp -L "$path" "$out/lib/$lib"
done
ls -l "$out" "$out/lib"
