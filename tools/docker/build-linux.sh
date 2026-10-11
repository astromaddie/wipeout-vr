#!/usr/bin/env bash
# Builds and tests WipEout for Linux inside the pinned environment from linux.Dockerfile. Set
# DOCKER_PLATFORM=linux/arm64 to target the Steam Frame from an x86-64 Docker host with ARM64
# emulation enabled.
#
#   docker build -t wipeout-vr-linux -f tools/docker/linux.Dockerfile tools/docker
#   tools/docker/build-linux.sh [step]
#
# Steps (default: all):
#   off      flat build (WIPEOUT_VR=OFF), staged in build/package-flat, then the ABI check on it
#   on       VR build (WIPEOUT_VR=ON), then its unit tests (sfvr and the fake-runtime tests)
#   package  stage the VR build as build/package/WipEoutVR: the binary, its libraries, the launcher
#   abi      the ABI check on the staged package (fails above glibc 2.35)
#   verify   prove the flat build is byte-for-byte what upstream's HEAD gives
#   all      off, on, package, abi, verify
#
# Build trees live under build/linux-<arch>-{off,on}/, which are git-ignored.
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
docker_platform="${DOCKER_PLATFORM:-}"
arch="$(docker info --format '{{.Architecture}}')"
if [[ "$docker_platform" == linux/arm64 ]]; then arch=aarch64; fi
image="${WIPEOUT_VR_IMAGE:-wipeout-vr-linux}"
step="${1:-all}"

build_image() {
	if [[ -n "$docker_platform" ]]; then
		docker buildx build --platform "$docker_platform" --load -t "$image" \
			-f "$repo/tools/docker/linux.Dockerfile" "$repo/tools/docker"
	else
		docker build -t "$image" -f "$repo/tools/docker/linux.Dockerfile" "$repo/tools/docker"
	fi
}

in_container() {
	local platform_args=()
	[[ -z "$docker_platform" ]] || platform_args+=(--platform "$docker_platform")
	docker run ${platform_args[@]+"${platform_args[@]}"} --rm -v "$repo":/src -w /src "$image" bash -c "set -euo pipefail; $1"
}

step_off() {
	in_container "
		cmake -S . -B build/linux-$arch-off -GNinja -DCMAKE_BUILD_TYPE=Release -DWIPEOUT_VR=OFF
		cmake --build build/linux-$arch-off
		tools/package_linux.sh build/linux-$arch-off build/package-flat > /dev/null
		python3 tools/check_linux_abi.py --no-runpath-check --lib-dir build/package-flat/lib build/package-flat/wipeout
	"
}

step_on() {
	in_container "
		cmake -S . -B build/linux-$arch-on -GNinja -DCMAKE_BUILD_TYPE=Release -DWIPEOUT_VR=ON
		cmake --build build/linux-$arch-on
		ctest --test-dir build/linux-$arch-on --output-on-failure
	"
}

step_package() {
	in_container "tools/package_linux.sh build/linux-$arch-on build/package/WipEoutVR"
}

step_abi() {
	in_container "python3 tools/check_linux_abi.py --lib-dir build/package/WipEoutVR/lib build/package/WipEoutVR/wipeout"
}

step_verify() {
	in_container "tools/verify_flat_build_unchanged.sh b425ecb /tmp/verify-flat"
}

case "$step" in
	image) build_image ;;
	off) step_off ;;
	on) step_on ;;
	package) step_package ;;
	abi) step_abi ;;
	verify) step_verify ;;
	all) step_off; step_on; step_package; step_abi; step_verify ;;
	*) echo "unknown step $step" >&2; exit 2 ;;
esac
