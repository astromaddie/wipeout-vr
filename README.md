# WipEout VR on Steam Frame (experimental)

This private port adds a native Steam Frame VR path to [phoboslab/wipeout-rewrite](https://github.com/phoboslab/wipeout-rewrite), a reimplementation of the 1995 PlayStation WipEout. It targets Steam Frame on SteamOS using SteamVR OpenXR, SDL2 and GL through Turnip + Zink; it is not a PS1 emulator or a PC-streamed headset app. See [docs/VR_ARCHITECTURE.md](docs/VR_ARCHITECTURE.md) for the rendering and input design.

**Status: PRIVATE · EXPERIMENTAL · HOLD.** Upstream has no source license and describes possible leaked-source provenance. Keep this project private pending a resolved source-rights route. The [PR0 readiness record](docs/PR0_READINESS.md) and [Steam Frame PR0 audit](https://github.com/astromaddie/steam-frame-vr-playbook/blob/main/reports/pr0-2026-10-11/README.md#material-findings) document the hold; neither grants permission. The previous upstream README is preserved at [docs/README_PRE_PR0.md](docs/README_PRE_PR0.md).

## Capabilities and evidence

| Capability | Evidence recorded in the project | Limit for this source revision |
| --- | --- | --- |
| Stereo race rendering | Earlier Frame runs rendered real attract-race stereo eyes; host harness covers synthetic scenes and mono parity. | Current HEAD was not rebuilt or tested in this documentation pass; the cockpit and other feature rows have their own limits in the architecture record. |
| OpenXR and controls | Earlier device probes created a SteamVR OpenXR session and swapchains. A prior wear on build 739a8d7 received a broad positive gameplay report. | The wear did not itemize cockpit, recenter, haptics or individual controls. No claim of complete gameplay acceptance. |
| Frame pacing | Earlier unattended measurements of a simple attract-race scene are documented at 72 Hz. | No sustained real-race or worn performance result is established for current HEAD. xrRequestDisplayRefreshRateFB was refused; SteamVR's per-app display setting controls the rate. |
| UI and rendering gaps | Race is intended for stereo; title, menus and intro use a world-locked panel. | Far/leashed HUD modes and HUD visibility controls are not built; desktop mirror is blank. The app does not control refresh rate. |

## Game data

Gameplay requires external disc data; source and game-derived material permissions remain unresolved. The documented data source is a user-owned NTSC-U PlayStation disc (SCUS-94301), plus the upstream-required PC menu models where available. The project helper accepts a .cue with a MODE2/2352 data track and CD-audio tracks and creates wipeout/ plus optional wipeout/music/trackNN.qoa:

    tools/data/prepare_psx_data.sh /path/to/your-disc.cue /tmp/wipeout-data
    tools/steam_frame/deploy.sh --data /tmp/wipeout-data

The Frame data tree is under $XDG_DATA_HOME/WipEoutVR/wipeout/ (default ~/.local/share/WipEoutVR/wipeout/); saves and vr.toml are under $XDG_DATA_HOME/WipEoutVR/. The intro video is skipped by the data helper.

## Build route

The documented package is a Linux ARM64 executable for SteamLinuxRuntime_4-arm64, launched as the WipEoutVR devkit title. With Docker installed, the repository's pinned ARM64 environment builds the VR target, runs its host tests, stages the package and checks the ABI:

    docker build -t wipeout-vr-linux -f tools/docker/linux.Dockerfile tools/docker
    tools/docker/build-linux.sh all

The core configuration is cmake -S . -B build -DWIPEOUT_VR=ON, followed by cmake --build build and the configured CTest suite. These are documented steps; this source commit was not built or tested in this documentation pass. See [docs/VR_ARCHITECTURE.md](docs/VR_ARCHITECTURE.md) for historical evidence and remaining checks.
