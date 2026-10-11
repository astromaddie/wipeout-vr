# PR0 — wipeout-vr

**EXPERIMENTAL.** Published as personal experimental source. See the [README](../README.md) for features, controls, game-data requirements and build instructions.

| Record | Revision |
| --- | --- |
| Hygiene audit | `ecaff58adff6dda659c88b7ae089ff61599e6052` |
| Checkout when this record was updated | `09f140da5d0be0a5b8d9e7f19f04744ca649815a` |
| Host test source | `09f140da5d0be0a5b8d9e7f19f04744ca649815a` |

Target: Steam Frame / SteamOS / Linux ARM64 / SteamVR OpenXR / SDL2 / X11 / Turnip + Zink.

The ROM-free host suite passed all 20 CTest cases at `09f140da5d0be0a5b8d9e7f19f04744ca649815a`. I rebuilt its registered VR and sfvr test targets with `cmake --build build/mac-vr --parallel 4 --target test_vr_xr test_vr_settings test_vr_math test_vr_clock test_vr_paths test_vr_stage test_vr_compose test_vr_refresh test_vr_view test_vr_cockpit test_vr_input test_vr_input_xr third_party/sfvr/test_view third_party/sfvr/test_turn third_party/sfvr/test_haptics third_party/sfvr/test_room third_party/sfvr/test_perf third_party/sfvr/test_settings third_party/sfvr/test_cxx`, then ran `ctest --test-dir build/mac-vr --output-on-failure`. Logs: `/tmp/pr0-final/wipeout-publication-tests`. No full application build or new device test was run.

The preserved upstream README says its source has no license and may derive from PC source leaked in 2022. Preparing this fork for publication does not add a license grant or establish provenance. The [upstream README](README_PRE_PR0.md) remains preserved with its original wording.

Retail disc data and optional PC menu models are external user inputs; extracted output must not be committed. Keep the original upstream and component notices in their stated scope; they do not grant rights to the commercial game.

[Exact metadata](PR0_METADATA.json) · [Changelog](../CHANGELOG.md)
