#!/bin/sh
# Build and run the cockpit mockup exporter, then the mockup generator.
#
#   tools/mockup/build.sh [data_root]
#
# data_root holds the lowercased PSX tree as wipeout/... (default: $WIPEOUT_DATA,
# else ~/.local/share/WipEoutVR). Everything generated goes under build/, which
# is git-ignored: it is derived from game data and must never be committed.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
data=${1:-${WIPEOUT_DATA:-$HOME/.local/share/WipEoutVR}}
out="$root/build/mockup-data"
bin="$root/build/mockup-tools"
mkdir -p "$out" "$bin"

src="$root/src"
cc -std=gnu99 -O1 -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable \
	-I"$src/libs" \
	-o "$bin/export_mockup" \
	"$here/export_mockup.c" \
	"$src/wipeout/object.c" "$src/wipeout/image.c" "$src/wipeout/track.c" "$src/wipeout/ui.c" \
	"$src/mem.c" "$src/utils.c" "$src/types.c" -lm

"$bin/export_mockup" "$data" "$out"
python3 "$here/build_cockpit_mockup.py"
