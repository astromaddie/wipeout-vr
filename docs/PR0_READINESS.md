# PR0 readiness: WipEout VR

**Disposition: HOLD · EXPERIMENTAL · PRIVATE.** Upstream has no license and describes possible leaked-source provenance. A code or port license cannot resolve that. Keep the repository private pending a concrete source-rights route; see the [2026-10-11 PR0 audit](https://github.com/astromaddie/steam-frame-vr-playbook/blob/main/reports/pr0-2026-10-11/README.md#material-findings).

## Source identity and checkout evidence

- Repository: wipeout-vr; branch main; source HEAD at review: 90bbe946c13bd0c68eb0503f7170580e6f777b0d.
- Remotes: upstream is phoboslab/wipeout-rewrite; origin is astromaddie/wipeout-vr.
- Initial worktree was clean. No fresh clone was made. The previous root README is archived byte-for-byte at docs/README_PRE_PR0.md.
- No game data or release artifacts were inspected or used in this documentation pass.

## Platform and build route

Target is Steam Frame on SteamOS, SteamVR OpenXR, SDL2 / X11, Turnip + Zink; package is Linux ARM64 ELF with SteamLinuxRuntime_4-arm64, registered as WipEoutVR. Docker is the documented route: build tools/docker/linux.Dockerfile, then run tools/docker/build-linux.sh all. It builds the flat and VR variants, runs host tests, stages the VR package and checks the ABI. The VR CMake target is -DWIPEOUT_VR=ON; configure-time OpenXR SDK retrieval is SHA-256 checked. No build, host test, package or device action was run for this documentation pass.

## Historical capability evidence and limits

docs/VR_ARCHITECTURE.md records earlier host unit and renderer harness coverage, device OpenXR/Zink probes and stereo attract-race eye dumps. It records one general wearer session on build 739a8d7 (2026-10-05) with a broad positive verdict; individual features exercised were not itemized. That evidence does not certify source HEAD.

The current documentation describes per-eye race rendering, a HUD quad, world-locked mono title/menu panel, and OpenXR actions mapped into the game's existing pad input. Specific controller paths, haptics, recenter, cockpit implementation, menu behavior, sustained race performance and comfort require dedicated verification. The prior 72 Hz measurement was an unattended attract-race scene, not a real race performance result. The runtime refused the app's refresh-rate request; SteamVR per-app settings govern the display. Far/leashed HUD modes and HUD visibility controls are not implemented, and the desktop mirror is blank.

## External data and rights

Full retail disc data remain external inputs; this documentation does not clear game-derived material in the repository. The documented user-supplied source is the NTSC-U WipEout disc (SCUS-94301), extracted by tools/data/prepare_psx_data.sh from a .cue with MODE2/2352 data and CD-audio tracks; the game also expects the upstream-layout PC menu models where available. The helper produces wipeout/ and optional QOA music under the XDG data root. Never commit extracted output.

Upstream's lack of a license and stated source provenance concern block publication. The audit is a finding, not permission.

## Pending acceptance

After rights are resolved and a clean, identified source snapshot is built, verify package launch and SteamVR OpenXR on Frame; actual Frame controller input, head tracking, stereo race depth, menus and pause; recenter and haptics; save/load and clean exit; and a sustained real-race wearer session with measured frame timing. Confirm cockpit features individually rather than extending the broad historical gameplay report. No current-HEAD build, headset or worn performance result is claimed.
