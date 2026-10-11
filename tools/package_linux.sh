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

# Keep the disc-data preparer usable on Steam Frame Desktop without a C compiler.
# The source and QOA header remain beside the prebuilt native helper for other hosts.
mkdir -p "$out/tools/data" "$out/src/libs" "$out/THIRD-PARTY-LICENSES"
cp "$root/tools/data/prepare_psx_data.sh" "$root/tools/data/cd2qoa.c" "$out/tools/data/"
cp "$root/src/libs/qoa.h" "$out/src/libs/qoa.h"
cc -O2 -I "$root/src/libs" "$root/tools/data/cd2qoa.c" -o "$out/tools/data/cd2qoa"
chmod +x "$out/tools/data/prepare_psx_data.sh" "$out/tools/data/cd2qoa"
sed -n '3,4p' "$root/src/libs/qoa.h" > "$out/THIRD-PARTY-LICENSES/QOA-MIT.txt"
printf '\n' >> "$out/THIRD-PARTY-LICENSES/QOA-MIT.txt"
# qoa.h declares MIT and carries the QOA copyright/SPDX lines above; use the
# matching full MIT permission text retained in this upstream source tree.
sed -n '/^Permission is hereby granted, free of charge, to any person obtaining a copy of/,$p' \
	"$root/src/libs/pl_mpeg.h" | sed -n '1,/^SOFTWARE\./p' >> "$out/THIRD-PARTY-LICENSES/QOA-MIT.txt"

source_rev="$(git -C "$root" rev-parse HEAD)"
cat > "$out/README.md" <<'EOF'
# WipEoutVR for Steam Frame — experimental 0.1.0

Source: https://github.com/astromaddie/wipeout-vr
Revision: @SOURCE_REV@

In SteamOS Devkit Client, use this package folder with Name `WipEoutVR`, Start Command `LAUNCH-STEAM-FRAME.sh`, and Steam Linux Runtime 3.0 ARM64 (Sniper). Launch it from the Steam library for OpenXR and controller input.

Prepare data from your own supported NTSC-U disc by running `tools/data/prepare_psx_data.sh DISC.cue OUT_DIR`, then copy `OUT_DIR/wipeout` to `~/.local/share/WipEoutVR/wipeout/` on the Frame. The included ARM64 `tools/data/cd2qoa` helper avoids requiring a compiler on Frame Desktop; Python 3 and `7zz` are still needed. Override it with `WIPEOUT_CD2QOA` when preparing data on another architecture.

This package contains no disc image, save, driver, or generated game data. See `THIRD-PARTY-NOTICES.md` and `THIRD-PARTY-LICENSES/` for bundled component notices.
EOF
sed -i "s/@SOURCE_REV@/$source_rev/" "$out/README.md"
cat > "$out/THIRD-PARTY-NOTICES.md" <<'EOF'
# Third-party notices

The packaged data-preparation helper includes QOA by Dominic Szablewski (SPDX-License-Identifier: MIT); its license text is in `THIRD-PARTY-LICENSES/QOA-MIT.txt`. The corresponding source is included at `src/libs/qoa.h`.

The original WipEout source is published without a license. This fork adds no grant for the original source or game data.
EOF
ls -l "$out" "$out/lib"
