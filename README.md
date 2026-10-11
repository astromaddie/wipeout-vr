# WipEout VR on Steam Frame

This is my Steam Frame VR port of [phoboslab/wipeout-rewrite](https://github.com/phoboslab/wipeout-rewrite), a reimplementation of the 1995 PlayStation WipEout. It runs on SteamOS with SteamVR OpenXR, SDL2 and OpenGL through Turnip and Zink. It is a native Linux ARM64 app, not a PlayStation emulator or a PC-streamed app. See the [architecture notes](docs/VR_ARCHITECTURE.md) for the rendering and input design.

**EXPERIMENTAL.** I made this for personal use, and I'm keeping it private for now. The upstream project has no source license and describes possible leaked-source origins, so the source rights need sorting out before a public release. See the [rights and cleanup notes](docs/PR0_READINESS.md). The [earlier upstream README](docs/README_PRE_PR0.md) is preserved.

The port renders races in stereo and supports tracked head and Frame controller input. I've played it on my Frame and liked how it feels. A cockpit instrument view is in development; the title, menus and intro use a panel in front of the player. HUD distance and visibility controls are still missing, and the desktop mirror is blank.

## Game data

Gameplay needs PlayStation disc data from your own copy. The documented source is a user-owned NTSC-U disc (SCUS-94301), with the upstream-required PC menu models where available. `tools/data/prepare_psx_data.sh` reads a `.cue` file and prepares the files under `wipeout/`:

```sh
tools/data/prepare_psx_data.sh /path/to/your-disc.cue /tmp/wipeout-data
tools/steam_frame/deploy.sh --data /tmp/wipeout-data
```

The Frame data folder is `$XDG_DATA_HOME/WipEoutVR/wipeout/` (usually `~/.local/share/WipEoutVR/wipeout/`). Saves and `vr.toml` are in `$XDG_DATA_HOME/WipEoutVR/`. The helper skips the intro video.

## Build and launch

The target is a Linux ARM64 package for `SteamLinuxRuntime_4-arm64`. With Docker installed, build the pinned environment and its package:

```sh
docker build -t wipeout-vr-linux -f tools/docker/linux.Dockerfile tools/docker
tools/docker/build-linux.sh all
```

Install the staged package, copy the prepared game data and register the `WipEoutVR` devkit title with:

```sh
tools/steam_frame/deploy.sh --data /tmp/wipeout-data
```

Launch it from the Steam library so the app receives controller input. The [architecture notes](docs/VR_ARCHITECTURE.md) describe the build, input, cockpit work and known rendering gaps; [docs/PR0_READINESS.md](docs/PR0_READINESS.md) explains the source-rights hold.
