#!/usr/bin/env bash
# Builds the game's data dir from the user's own PSX disc dump (a .cue with a
# MODE2/2352 data track and CD-audio music tracks). Never commit the output.
#
#   tools/data/prepare_psx_data.sh DISC.cue OUT_DIR
#
# OUT_DIR gets wipeout/ (the disc's WIPEOUT/ tree, lowercased) and
# wipeout/music/trackNN.qoa (the audio tracks in order). Deploy it with
# tools/steam_frame/deploy.sh --data OUT_DIR. Needs python3 and 7zz.
# If no prebuilt helper is available, the script falls back to compiling cd2qoa.c.
# The intro video (an STR file) is not converted; the game skips it.
set -euo pipefail

cue="$1"; out="$2"
root="$(cd "$(dirname "$0")/../.." && pwd)"
disc_dir="$(cd "$(dirname "$cue")" && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

# The track files in cue order: the first is data, the rest are audio
tracks=()
while IFS= read -r f; do tracks+=("$disc_dir/$f"); done < <(sed -n 's/^FILE "\(.*\)" BINARY.*/\1/p' "$cue")

# Raw 2352-byte Mode 2 Form 1 sectors to a 2048-byte ISO
python3 - "${tracks[0]}" "$work/data.iso" <<'PY'
import sys
src, dst = open(sys.argv[1], 'rb'), open(sys.argv[2], 'wb')
while (s := src.read(2352)) and len(s) == 2352:
	dst.write(s[24:24 + 2048])
PY
7zz x -y -o"$work/iso" "$work/data.iso" 'WIPEOUT/*' >/dev/null 2>&1 || true # the STR video at the end is Form 2, so 7zz reports a short archive
[ -d "$work/iso/WIPEOUT" ] || { echo "no WIPEOUT/ on the data track" >&2; exit 1; }

mkdir -p "$out"
(cd "$work/iso" && find WIPEOUT -type f) | while IFS= read -r f; do
	l="$(printf '%s' "$f" | tr 'A-Z' 'a-z')"
	mkdir -p "$out/$(dirname "$l")"
	cp "$work/iso/$f" "$out/$l"
done

# CD audio (s16le stereo 44.1 kHz) to QOA, skipping each track's 2 s pregap
cd2qoa="${WIPEOUT_CD2QOA:-$root/tools/data/cd2qoa}"
if [[ ! -x "$cd2qoa" ]]; then
	if [[ -n "${WIPEOUT_CD2QOA:-}" ]]; then
		echo "WIPEOUT_CD2QOA is not executable: $WIPEOUT_CD2QOA" >&2
		exit 1
	fi
	command -v cc >/dev/null || { echo "need a prebuilt cd2qoa helper or a C compiler (set WIPEOUT_CD2QOA)" >&2; exit 1; }
	cc -O2 -I "$root/src/libs" "$root/tools/data/cd2qoa.c" -o "$work/cd2qoa"
	cd2qoa="$work/cd2qoa"
fi
mkdir -p "$out/wipeout/music"
for ((i = 1; i < ${#tracks[@]}; i++)); do
	"$cd2qoa" "${tracks[$i]}" $((150 * 2352)) "$out/wipeout/music/$(printf 'track%02d.qoa' "$i")"
done
echo "wrote $out/wipeout ($(du -sh "$out/wipeout" | cut -f1)), $(( ${#tracks[@]} - 1 )) music tracks"
