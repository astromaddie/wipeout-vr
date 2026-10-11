# WipEout VR on Steam Frame

This is my Steam Frame VR port of [phoboslab/wipeout-rewrite](https://github.com/phoboslab/wipeout-rewrite), a reimplementation of the 1995 PlayStation WipEout. It runs on SteamOS with SteamVR OpenXR, SDL2 and OpenGL through Turnip and Zink. It is a native Linux ARM64 app, not a PlayStation emulator or a PC-streamed app. See the [architecture notes](docs/VR_ARCHITECTURE.md) for the rendering and input design.

**EXPERIMENTAL.** I made this for personal use, and it's still a work in progress. You'll need your own WipEout disc data; it isn't included here. The [original README](docs/README_PRE_PR0.md) and upstream notices are preserved.

**Want to play on Steam Frame?** Download the prebuilt standalone package from the [v0.1.0 prerelease](https://github.com/astromaddie/wipeout-vr/releases/tag/v0.1.0); no compiler is needed. You still need disc data from your own WipEout copy and must prepare it with the included data tool.

The port renders races in stereo and supports tracked head and Frame controller input. I've played it on my Frame and liked how it feels. A cockpit instrument view is in development; the title, menus and intro use a panel in front of the player. HUD distance and visibility controls are still missing, and the desktop mirror is blank.

## Game data

Gameplay needs PlayStation disc data from your own copy. The documented source is a user-owned NTSC-U disc (SCUS-94301), with the upstream-required PC menu models where available. `tools/data/prepare_psx_data.sh` reads a `.cue` file and prepares the files under `wipeout/`:

```sh
tools/data/prepare_psx_data.sh /path/to/your-disc.cue /tmp/wipeout-data
tools/steam_frame/deploy.sh --data /tmp/wipeout-data
```

The Frame data folder is `$XDG_DATA_HOME/WipEoutVR/wipeout/` (usually `~/.local/share/WipEoutVR/wipeout/`). Saves and `vr.toml` are in `$XDG_DATA_HOME/WipEoutVR/`. The helper skips the intro video.

## Build and launch

The [Steam Frame build and install guide](docs/STEAM-FRAME-BUILD-INSTALL.md) covers ARM64 Docker builds from Linux, macOS, or Windows through WSL2, data preparation, package output, first installation and Steam launch. The Frame package is Linux ARM64; a Windows or desktop VR build is a separate target. The [architecture notes](docs/VR_ARCHITECTURE.md) describe the build, input, cockpit work and known rendering gaps; [docs/PR0_READINESS.md](docs/PR0_READINESS.md) records the experimental source scope and verification status.

The upstream source is published without a license and its README discusses its origins. This fork keeps those notices; it adds no new grant for the original source or game data.
