# The pinned Linux build environment, after the F-Zero X port's: Ubuntu 22.04 (glibc 2.35, the
# release floor), gcc-12, CMake 3.31.6, SDL2 2.30.x built from source, GLEW from the distribution.
# The image builds for the BuildKit target platform. Use buildx with --platform linux/arm64 and
# emulation to build the Steam Frame package from x86-64.
#
#   docker buildx build --platform linux/arm64 --load -t wipeout-vr-linux \
#     -f tools/docker/linux.Dockerfile tools/docker
#   DOCKER_PLATFORM=linux/arm64 tools/docker/build-linux.sh on
ARG BASE_IMAGE=ubuntu:22.04
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive \
    CC=gcc-12 \
    CXX=g++-12

ARG CMAKE_VERSION=3.31.6
ARG CMAKE_SHA256_AARCH64=b4cc788d63112b2749b40627e719eb5d3b8ed8f00c36d77189f4019cfe64bc9e
ARG CMAKE_SHA256_X86_64=5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf
ARG SDL2_VERSION=2.30.11
ARG SDL2_SHA256=8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-12 g++-12 ninja-build pkg-config \
        git ca-certificates wget file rsync \
        python3 \
        libgl1-mesa-dev libopengl-dev libegl1-mesa-dev libglew-dev \
        libasound2-dev libpulse-dev libudev-dev libdbus-1-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
        libxinerama-dev libxss-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols libdrm-dev libgbm-dev \
    && rm -rf /var/lib/apt/lists/* \
    && git config --global --add safe.directory '*'

RUN set -eux; \
    arch="$(uname -m)"; \
    case "$arch" in aarch64) sha="${CMAKE_SHA256_AARCH64}" ;; x86_64) sha="${CMAKE_SHA256_X86_64}" ;; *) exit 1 ;; esac; \
    wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz"; \
    echo "${sha}  cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz" | sha256sum -c -; \
    mkdir -p /opt/cmake; \
    tar -xzf "cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz" -C /opt/cmake --strip-components=1; \
    rm "cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz"
ENV PATH=/opt/cmake/bin:$PATH

# SDL2 with x11: the Frame's video driver is x11 (gamescope's Xwayland), and the XR GL binding
# takes the GLX context from SDL's x11 window.
RUN set -eux; \
    cd /tmp; \
    wget -q "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.tar.gz"; \
    echo "${SDL2_SHA256}  SDL2-${SDL2_VERSION}.tar.gz" | sha256sum -c -; \
    tar -xzf "SDL2-${SDL2_VERSION}.tar.gz"; \
    cmake -S "SDL2-${SDL2_VERSION}" -B build-sdl2 -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF; \
    cmake --build build-sdl2 --target install; \
    ldconfig; \
    rm -rf /tmp/*

# The host render harness (tests/vr/vr_render_harness.c) needs a GL context: a virtual X server
# and Mesa's software rasteriser (llvmpipe).
RUN apt-get update && apt-get install -y --no-install-recommends \
        xvfb xauth libgl1-mesa-dri mesa-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
