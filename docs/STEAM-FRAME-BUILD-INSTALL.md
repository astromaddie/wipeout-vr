# Steam Frame build and install

This guide builds the standalone Steam Frame target: a Linux ARM64 package. The build host can be Linux, macOS, or Windows running a Linux shell under WSL2; it does not need to be ARM64. A Windows or desktop VR executable is a different target and is not the Frame package.

## Build host

The build host needs Bash, Git, Docker with Buildx, and for game-data preparation Python 3.8 or newer, `7zz`, and a C compiler. On Windows, use Docker Desktop with WSL2 integration, clone and build inside the Linux distribution's filesystem, and run these commands from Bash. See Microsoft's [install WSL](https://learn.microsoft.com/windows/wsl/install) instructions and Docker's [WSL2 integration](https://docs.docker.com/desktop/features/wsl/use-wsl/) and [multi-platform builds with emulation](https://docs.docker.com/build/building/multi-platform/) guides. `docker buildx inspect --bootstrap` should list `linux/arm64` under supported platforms. ARM64 emulation can make this compiler and game-code build slow.

On an ARM64 Linux Docker host, the default Docker platform is already the target. On x86-64 Linux, macOS, or WSL2, set `DOCKER_PLATFORM=linux/arm64`; the wrapper builds and loads the target image with Buildx, then runs the build inside that ARM64 image.

## Build on Frame Desktop

The wrapper supports a native Frame Desktop build if Docker and an ARM64-capable Buildx builder are already available from a checkout under your writable home directory. Leave `DOCKER_PLATFORM` unset and run:

```sh
tools/docker/build-linux.sh image
JOBS=4 tools/docker/build-linux.sh on
tools/docker/build-linux.sh package
tools/docker/build-linux.sh abi
```

The repository does not set up Docker or add packages to SteamOS. If Docker is unavailable, build on an external Linux, macOS, or WSL2 host.

## Build the Frame package

```sh
git clone --recurse-submodules --branch main https://github.com/astromaddie/wipeout-vr.git
cd wipeout-vr
export DOCKER_PLATFORM=linux/arm64
tools/docker/build-linux.sh image
JOBS=4 tools/docker/build-linux.sh on
tools/docker/build-linux.sh package
tools/docker/build-linux.sh abi
```

On an ARM64 Linux Docker host, omit the `DOCKER_PLATFORM` export. The package is written to `build/package/WipEoutVR/`; it contains the ARM64 executable, required libraries and `LAUNCH-STEAM-FRAME.sh`, but no PlayStation disc data.

## Prepare game data

Run the data preparer with Python 3.8 or newer and `7zz`, using your own supported NTSC-U WipEout `.cue` file and a new output directory. The Linux ARM64 package includes a native `cd2qoa` helper, so Frame Desktop users do not need a C compiler. A source checkout without that helper can still compile `tools/data/cd2qoa.c` as a fallback when a C compiler is available.

```sh
tools/data/prepare_psx_data.sh "$HOME/game-data/wipeout.cue" "$HOME/game-data/wipeout-data"
```

When preparing data on a different architecture, set `WIPEOUT_CD2QOA` to a native helper for that host. The packaged ARM64 helper is at `tools/data/cd2qoa`.

The output is `wipeout-data/wipeout/` with the disc's lowercased `WIPEOUT/` files and converted `wipeout/music/trackNN.qoa` audio files. The preparer skips the intro video. There is no repository-pinned hash for this user-generated output; keep it outside the source tree and package.

## First install and Steam launch

For a first install from another computer, follow Valve's [Steam Frame development setup](https://partner.steamgames.com/doc/steamhardware/steamframe/setup) to enable Developer Mode and pair the host. In SteamOS Devkit Client, connect to the Frame by IP, then upload the package directory above. Set **Name** to `WipEoutVR`, **Local Folder** to the package directory, **Start Command** to `LAUNCH-STEAM-FRAME.sh`, and **Runtime** to **Steam Linux Runtime 3.0 ARM64 (Sniper)**. The [Valve upload guide](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames) describes those fields. Uploading creates the Steam library entry.

Before launching, transfer the prepared directory to the Frame. In a Frame Desktop terminal, from a folder containing the transferred `wipeout-data/`, copy it to the exact runtime path:

```sh
mkdir -p "$HOME/.local/share/WipEoutVR"
cp -a "$HOME/game-data/wipeout-data/wipeout" "$HOME/.local/share/WipEoutVR/"
```

The resulting path must be `~/.local/share/WipEoutVR/wipeout/`; saves and `vr.toml` are stored in `~/.local/share/WipEoutVR/`. Start the title from the Frame's Steam library so Steam supplies the OpenXR and controller runtime. The package and game data are separate inputs.

If the package and data are already on the Frame, keep the package in a writable folder under your home directory, add `LAUNCH-STEAM-FRAME.sh` through Steam's **Add a Non-Steam Game** flow, and start it from the Steam library. Copy the `wipeout/` data directory to the path above first. Start it from Steam so Steam supplies SteamVR and controller input.
