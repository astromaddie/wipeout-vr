# WipEout VR: architecture (current state)

A native standalone VR port of WipEout for the Valve Steam Frame. It is built on
[phoboslab/wipeout-rewrite](https://github.com/phoboslab/wipeout-rewrite) (upstream
remote `upstream`) and follows the Steam Frame VR port playbook and standard
(`~/.claude/skills/steam-frame-vr-port/`).

This file describes the current state. Put chronology in `docs/VR_LAB_LOG.md` if a log becomes necessary.

## 1. Target and base

| Item | Value |
| --- | --- |
| Device | Steam Frame, SteamOS `vr`, SteamVR OpenXR, Turnip + Zink GL 4.6 |
| Package route | Linux ARM64 ELF, compat tool `SteamLinuxRuntime_4-arm64`, devkit title `WipEoutVR` |
| Build flag | CMake `-DWIPEOUT_VR=ON` defines `WIPEOUT_VR=1`. With the flag off, the flat build is unchanged |
| Base | wipeout-rewrite: C11, SDL2 + OpenGL 2.x/GLES2 renderer (`src/render_gl.c`), about 11k lines |
| Rejected base | `alexbeavs-ps1-ports/wipeout-recomp` (psxrecomp). It emulates the PS1 GPU, which sees only 2D screen-space triangles, and the game culls to its native frustum. Stereo would need PGXP depth reconstruction plus MIPS reverse engineering. Decided with the user on 2026-10-02 |
| Legal | Upstream has no license and may derive from the 2022 leaked PC source. Keep this repo **private**. Never commit game data |
| Game data | User-supplied PSX NTSC data plus the PC menu models, laid out as upstream's README describes, under `$XDG_DATA_HOME/WipEoutVR/wipeout/`. Saves go under `$XDG_DATA_HOME/WipEoutVR/` |

## 2. Triage facts (from the upstream source, 2026-10-02)

| Question | Answer | Consequence |
| --- | --- | --- |
| Sim tick | Variable step. `system_update()` takes `dt = min(real_delta, 0.1) * time_scale` from `platform_now()`, then `game_update()` simulates **and** issues draws | No display-time scheduler. In VR, `dt` comes from the change in `predictedDisplayTime`, and the sim runs once per XR frame |
| Interpolation | None; not needed with a variable step | |
| Camera | `camera_t` (`src/wipeout/camera.c`): position plus yaw/pitch/roll angles. Race modes are external chase (`camera_update_race_external`) and internal cockpit (`camera_update_race_internal`, roll = `save.internal_roll`). `render_set_view(pos, angles)` builds the view matrix | Anchor = game camera. Head pose composes on top |
| Projection | `render_setup_3d_projection_mat`: fixed 73.75° vertical FOV, near 16, far 64000 (`RENDER_FADEOUT_FAR`) | Replace with each eye's asymmetric FOV, keeping the same near and far |
| Renderer | Immediate mode. State calls plus `render_push_tris/sprite/2d` go into a 2048-tri batch that is flushed on state change. One 2048² texture atlas. One shader (`SHADER_GAME_VS/FS`) with a distance fade from the `camera_pos` uniform | Record a per-frame command list, then replay it per eye (section 4) |
| HUD and 2D | `render_set_view_2d()` switches to an orthographic projection over the backbuffer. HUD, menus and text are all `render_push_2d*`. `hud_draw_target_icon` projects a world point with `render_transform` (mono view) | The 2D segment goes to its own HUD texture, shown on a quad layer |
| Input | `input_set_button_state(INPUT_GAMEPAD_*, value)`, the game's own pad layer, which SDL feeds. Actions are bound per layer (`game.c` defaults: thrust A, fire X, view Y, brakes L/R shoulder, steer D-pad, menu A/B/X/Start) | OpenXR actions feed the same `INPUT_GAMEPAD_*` codes, so the game's bindings and prompts stay valid |
| Existing Linux port | Yes: SDL2 + GLEW, CMake and a Makefile | Same route as F-Zero X and Star Fox |

## 3. Frame protocol (VR build)

1. `vr_frame_begin()`: `xrWaitFrame`, `xrBeginFrame`, and `xrLocateViews` at `predictedDisplayTime`. Poll actions and merge them into the input layer. Run the system-layer state machine (`sfvr_view`).
2. `system_update()` computes `dt` from the change in `predictedDisplayTime`, clamped to the game's 0.1 s. During standby (`shouldRender = false`) or focus loss, it freezes the sim (`dt = 0`) and pauses audio, while still polling.
3. `game_update()` runs **once**. In VR, renderer calls append to the frame's command list and issue no GL.
4. Replay:
   - **Stereo frame** (race scene, with a `render_set_view` from the race camera): the world segment is replayed per eye into the projection-layer swapchains, and the 2D segment is replayed once into the HUD swapchain.
   - **Mono frame** (intro, title, menus, highscores; and race while `vr.force_mono` is set): the whole list is replayed once with the game camera and native projection into the panel swapchain, shown on a world-locked quad.
5. `vr_frame_end()`: `xrEndFrame` with the projection layer (stereo only), plus the HUD quad or the panel quad, plus the fade quad when one is active.

Every per-frame state change (pickup colour cycling, particles, `cycle_time`) happens inside `game_update`, so the per-eye replay cannot advance state twice.

## 4. Stereo renderer

- **Command list** (`render_gl.c`, behind `WIPEOUT_VR`): the commands are
  - `VIEW(pos, angles)` and `VIEW_2D`;
  - `MODEL_MAT`, `DEPTH_WRITE`, `DEPTH_TEST`, `DEPTH_OFFSET`, `BLEND`, `CULL`, `SCREEN_POS`;
  - `TRIS(first, count)`, an index range into the frame's triangle arena, with atlas UVs already applied;
  - `SPRITE(pos, size, color, tex)`.

  Texture creation and upload stay immediate, because they are resources rather than draws. Record mode reproduces the flat path's state semantics, including `render_set_view` resetting depth state and the model matrix.
- **Eye view**: `eye_view = inverse(anchor · head_from_local · eye_from_head)`. The runtime gives the eye pose in metres; scale its translation by `vr.world_scale` (game units per metre). IPD comes only from the runtime eye poses.
- **Eye projection**: the runtime's asymmetric FOV in the game's GL clip convention, with near 16 and far 64000.
- **Anchor**: the game camera's position, yaw and pitch, with roll 0, so pitch follows the track. Roll follows `save.internal_roll` only when `vr.follow_vehicle_rotation` is on. `vr.horizon_lock` also drops pitch.
- **Recentre**: transfers head yaw into a VR yaw offset at a stereo frame boundary. It is triggered by L View held 1 s, `XrEventDataReferenceSpaceChangePending`, focus gain, and every mono↔stereo switch.
- **Fade**: a black compositor quad, 0.25 s hold then 0.35 s fade, on every mono↔stereo switch.

## 5. Draw inventory (camera-relative and screen-space draws)

| File:function | What it draws | Flat rule | VR rule | Verified by |
| --- | --- | --- | --- | --- |
| `scene.c:scene_draw` (sky) | Sky object translated to `camera->position + sky_offset`, depth write off | Follows the camera | Tag as `SKY`. At replay, re-translate the model matrix to each eye's position | not started |
| `scene.c:scene_draw` (objects) | Cull: `dot(cam_pos - origin, cam_fwd) < radius` and distance < 64000 | Forward half-space cull from the game camera | Distance-only cull in VR (`vr_wide_cull`), so a turned head sees scenery | not started |
| `track.c:track_draw` | Sections, culled by the camera-forward plane | Forward half-space cull | Distance-only cull in VR | not started |
| `render_gl.c:render_push_sprite` | Billboards expanded with `sprite_mat` from the game camera angles | Faces the game camera | Record as `SPRITE`; expand per eye, facing that eye | not started |
| `render_gl.c` vertex shader `fade` | Alpha fade by distance from `camera_pos` | Game camera | Per-eye position | not started |
| `race.c` `render_set_screen_position(g.camera.shake)` | Screen-space shake offset (`screen` uniform) | Clip-space offset | Ignored in VR (`camera_shake` off). Optional `impact` haptic instead | not started |
| `ship.c` / `particle.c` depth offset −32 | Shadows and particles | Polygon offset | Unchanged, per eye | not started |
| `hud.c:hud_draw_target_icon` | Target reticle from `render_transform` (mono view and projection) | Screen projection | Project through the head-centre camera onto the HUD quad's frustum, so the reticle sits over its target. Mark headset-unverified | not started |
| `hud.c`, `ui.c`, `menu.c` 2D | All 2D text and quads, after `render_set_view_2d` | Ortho over the backbuffer | HUD swapchain on a quad. Alpha derived from renders over black and over white (lesson 6) | not started |
| `main_menu.c` 3D ship and logo models | `render_set_view` at the origin, plus a screen offset | Mono 3D | Mono frame on the world-locked panel | not started |
| `intro.c` MPEG video | 2D texture | Fullscreen | Mono panel (cinema quad later) | not started |
| `render_gl.c` post effects (CRT) and `RENDER_RES_240P/480P` | Backbuffer post-process | Fullscreen | Disabled in VR; eyes render at the runtime's recommended size × `resolution_scale` | not started |

## 6. Controls (Frame profile; game codes via `input_set_button_state`)

| Frame input | Game code | Game action |
| --- | --- | --- |
| L stick | `INPUT_GAMEPAD_L_STICK_*` (VR default: also bound to steer and pitch) | Steer, pitch; menu navigation |
| D-pad | `INPUT_GAMEPAD_DPAD_*` | Steer, pitch; menus |
| A | `INPUT_GAMEPAD_A` | Thrust; menu select |
| R trigger (dominant) | `INPUT_GAMEPAD_R_TRIGGER`, a VR alias bound to thrust | Thrust (role alias) |
| X | `INPUT_GAMEPAD_X` | Fire; menu back |
| Y | `INPUT_GAMEPAD_Y` | Change view |
| B | `INPUT_GAMEPAD_B` | Menu back |
| L / R bumper | `INPUT_GAMEPAD_L_SHOULDER` / `R_SHOULDER` | Air brakes |
| R Menu | `INPUT_GAMEPAD_START` | Pause / start (system layer) |
| L View, short | `INPUT_GAMEPAD_SELECT` → menu back | System layer |
| L View, hold 1 s / 3 s | none | Recentre / recentre (no height in a seated racer) |

Fallback profiles: Index and Touch. The final map needs the user's approval (phase 4).

## 7. Settings and diagnostics (`vr.*` keys; env `WIPEOUT_VR_<KEY>`)

The settings file is `$XDG_DATA_HOME/WipEoutVR/vr.toml`, using the standard keys from the `sfvr` registry, plus:

- `world_scale`: game units per metre. The default is derived from the ship hull length and must be confirmed at first wear.
- `follow_vehicle_rotation` (off), `horizon_lock` (off), `hud_mode` (`near`), `hud_distance`, `hud_scale`.

Diagnostics: `force_render`, `diag_yaw`, `dump_frame`, `probe_seconds`, `timing`. Log prefixes are `[vr]` for state changes and `[vr-perf]` every 10 s.

## 8. Status

Levels: **worn** (the user played it in the headset), **device** (unattended run on the Frame), **host** (Mac or Linux render or test), **not started**.

| Item | Phase | Level | Notes |
| --- | --- | --- | --- |
| Base chosen, fork created (private) | 0 | done | `astromaddie/wipeout-vr` |
| OpenXR probe (colour per eye) | 0 | not started | |
| Linux ARM64 flat build, devkit title, data dir | 1 | not started | |
| Command list and per-eye replay | 2 | not started | |
| HUD quad, mono panel, fades | 2 | not started | |
| Draw inventory rows | 2 | not started | See section 5 |
| Actions and system layer | 2 | not started | |
| Eye dumps at 0, 90 and 180° yaw | 2 | not started | |
| First wear | 3 | not started | Needs game data on the device |
