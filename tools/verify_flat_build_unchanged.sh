#!/usr/bin/env bash
# Proves the flat (WIPEOUT_VR=OFF) build is unchanged by the VR port.
#
#   tools/verify_flat_build_unchanged.sh [BASE_REV] [WORK_DIR]
#
# Builds BASE_REV (default b425ecb, the upstream wipeout-rewrite HEAD this fork
# started from) and the working tree with the same compiler and flags, flag OFF,
# then compares every object file. Source paths are mapped to "." so __FILE__
# is the same in both. A file that differs is checked two more ways:
#   - the preprocessed source (-E -P) must be identical once the line number
#     in die()/error_if() messages is masked. Inserting a fenced #if shifts the
#     lines after it, and die() prints __LINE__, so the .o differs in those
#     strings and in nothing else;
#   - the disassembled code must be identical.
# Needs git, cmake, a C compiler and python3. Run it inside tools/docker for
# the Linux ARM64 result. Set CMAKE_ARGS for extra configure arguments, CC to
# pick a compiler.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
base_rev="${1:-b425ecb}"
work="${2:-$(mktemp -d)}"
base_src="$work/base-src"
base_build="$work/base-build"
tree_build="$work/tree-build"

rm -rf "$base_src" "$base_build" "$tree_build"
mkdir -p "$base_src"
git -C "$repo" archive "$base_rev" | tar -x -C "$base_src"

configure_and_build() { # source build
	cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DWIPEOUT_VR=OFF \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DCMAKE_C_FLAGS="-ffile-prefix-map=$1=." ${CMAKE_ARGS:-} > "$2.configure.log" 2>&1
	cmake --build "$2" -j "$(getconf _NPROCESSORS_ONLN)" > "$2.build.log" 2>&1
}
configure_and_build "$base_src" "$base_build"
configure_and_build "$repo" "$tree_build"

compiler="$(grep -m1 CMAKE_C_COMPILER: "$tree_build/CMakeCache.txt" | cut -d= -f2)"
echo "compiler: $("$compiler" --version | head -1)"
echo "base:     $base_rev"
echo "tree:     $(git -C "$repo" rev-parse --short HEAD)$(git -C "$repo" diff --quiet || echo '+uncommitted')"

disassemble() {
	if objdump --version 2>/dev/null | grep -q GNU; then
		objdump -d --no-show-raw-insn "$1" | sed '1,/^Disassembly/d'
	else
		otool -tV "$1" | tail -n +2
	fi
}

preprocess() { # compile_commands.json source-suffix output
	local command
	command="$(python3 - "$1" "$2" <<'PY'
import json, sys
for e in json.load(open(sys.argv[1])):
    if e["file"].endswith(sys.argv[2]):
        print(e["command"])
PY
)"
	command="$(echo "$command" | sed -E "s# -o [^ ]+# -o $3#; s# -c # -E -P #")"
	(cd "$(dirname "$1")" && eval "$command")
}

identical=0
differing=()
while IFS= read -r object; do
	rel="${object#$base_build/}"
	if cmp -s "$object" "$tree_build/$rel"; then
		identical=$((identical + 1))
	else
		differing+=("$rel")
	fi
done < <(find "$base_build/CMakeFiles/wipeout.dir" -name '*.o' | sort)
echo "object files: $identical byte-identical, ${#differing[@]} differ"

status=0
for rel in ${differing[@]+"${differing[@]}"}; do
	source="${rel#CMakeFiles/wipeout.dir/}"
	source="${source%.o}"
	preprocess "$base_build/compile_commands.json" "$source" "$work/base.i"
	preprocess "$tree_build/compile_commands.json" "$source" "$work/tree.i"
	mask='s/" line " "[0-9]+"/" line " "N"/g'
	if diff <(sed -E "$mask" "$work/base.i") <(sed -E "$mask" "$work/tree.i") > /dev/null; then
		pre="preprocessed source identical (die() line numbers masked)"
	else
		pre="PREPROCESSED SOURCE DIFFERS"; status=1
	fi
	if diff <(disassemble "$base_build/$rel") <(disassemble "$tree_build/$rel") > /dev/null; then
		code="disassembly identical"
	else
		code="DISASSEMBLY DIFFERS"; status=1
	fi
	echo "  $source: $pre; $code"
done

if [ "$status" -eq 0 ]; then
	echo "RESULT: the flat build is unchanged"
else
	echo "RESULT: the flat build CHANGED"
fi
exit "$status"
