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
| Build flag | CMake `-DWIPEOUT_VR=ON` defines `WIPEOUT_VR=1` (default OFF). With the flag off, the flat build is unchanged (section 9 has the proof) |
| GL profile | Core 4.3 context (SDL requests it, forward-compatible), with GLSL 330 variants of the shaders in `src/vr/vr_shaders_gl330.h`. See section 9 |
| Base | wipeout-rewrite: C11, SDL2 + OpenGL 2.x/GLES2 renderer (`src/render_gl.c`), about 11k lines |
| Rejected base | `alexbeavs-ps1-ports/wipeout-recomp` (psxrecomp). It emulates the PS1 GPU, which sees only 2D screen-space triangles, and the game culls to its native frustum. Stereo would need PGXP depth reconstruction plus MIPS reverse engineering. Decided with the user on 2026-10-02 |
| Legal | Upstream has no license and may derive from the 2022 leaked PC source. Keep this repo **private**. Never commit game data |
| Game data | User-supplied PSX NTSC data plus the PC menu models, laid out as upstream's README describes, under `$XDG_DATA_HOME/WipEoutVR/wipeout/` (default `~/.local/share`). Saves and `vr.toml` go in `$XDG_DATA_HOME/WipEoutVR/`. The VR build resolves both at runtime and ignores `PATH_ASSETS` and `PATH_USERDATA` |

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

**Current state (phase 1).** Steps 1, 2 and 5 exist. `platform_sdl.c` calls `vr_frame_begin()` after pumping SDL events, `system_update()` takes `dt` from `vr_frame_dt()` (the change in `predictedDisplayTime`, then the game's 0.1 s clamp) and `platform_end_frame()` calls `vr_present_flat_frame()`. That copies the window's back buffer into a quad swapchain and submits it as a world-locked quad layer, `vr.panel_width` wide, `vr.panel_distance` metres ahead of the head (levelled, heading only), re-anchored on `XrEventDataReferenceSpaceChangePending` and on every focus gain. The game still renders flat into its own backbuffer. Vsync is off, since `xrWaitFrame` paces the loop. While `shouldRender` is false or the app lacks focus, `dt` is 0 and SDL audio is paused, but the loop keeps waiting, beginning and ending frames. Steps 3 and 4 (command list, per-eye replay) are the next milestone; the seams are `vr_eye_acquire()`/`vr_eye_release()`/`vr_submit_projection()` in `src/vr/vr.h`.

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

The settings file is `$XDG_DATA_HOME/WipEoutVR/vr.toml` (`~/.local/share/WipEoutVR/vr.toml` when `XDG_DATA_HOME` is unset or relative). It has a top-level `version` (the sfvr schema, 1) and a `[vr]` table. `src/vr/vr_settings.c` reads it with the sfvr key registry (`third_party/sfvr`, which is a registry only and does no file IO) plus the port keys below:

- A missing key gets its default, an invalid value (not a number, unknown enum or bool text) gets the default, an out-of-range value is clamped. Unknown keys, other tables, comments and layout are kept as they are. The file is rewritten, atomically (temp file, `fsync`, `rename`), only when something was added or repaired.
- `WIPEOUT_VR_<KEY>` (for example `WIPEOUT_VR_RESOLUTION_SCALE=1.2`) overrides a key for the run. It is clamped the same way and never written back.

Port keys (not in the sfvr registry):

| Key | Default | Meaning |
| --- | --- | --- |
| `world_scale` | 128.0 (**placeholder**, 16 to 1024) | Game units per metre. The chase camera sits 1024 units behind the ship, which should be about 8 m. Derive it from the ship hull length and confirm at first wear |
| `force_mono` | false | Keep race frames on the mono panel, with no stereo (next milestone) |
| `panel_distance` | 2.0 m (0.8 to 20) | The flat panel's distance |
| `panel_width` | 2.4 m (0.5 to 10) | The flat panel's width; its height follows the window's aspect |

The doc's other keys are sfvr standard keys with sfvr's defaults: `follow_vehicle_rotation` (off), `horizon_lock` (off), `hud_mode` (`near`), `hud_distance` (2.0 m), `hud_scale` (100), `resolution_scale` (1.0), `refresh_rate` (90), `camera_shake` (off), and the rest of the standard set. The settings page (phase 4) is not built.

Diagnostics: `force_render`, `diag_yaw`, `dump_frame`, `probe_seconds`, `timing` are registry keys. `probe_seconds` and `force_render` (synthetic head at 1.6 m, layers submitted in standby) are implemented. `diag_yaw`, `dump_frame` and `timing` are read but do nothing yet, and the `[vr-perf]` line is not emitted yet. Log prefixes are `[vr]` for state changes and `[vr-perf]` every 10 s.

**Probe.** `vr.probe_seconds = N` (or `WIPEOUT_VR_PROBE_SECONDS=N`) makes the VR build clear the left eye red and the right eye blue for N seconds after the session is running, before `system_init()` loads anything. It logs the swapchain format, eye size and graphics binding with the `[vr]` prefix. It waits up to 20 s for the session to start, and says so if standby (`shouldRender` false) is all it gets.

## 8. Status

Levels: **worn** (the user played it in the headset), **device** (unattended run on the Frame), **host** (Mac or Linux render or test), **not started**.

| Item | Phase | Level | Notes |
| --- | --- | --- | --- |
| Base chosen, fork created (private) | 0 | done | `astromaddie/wipeout-vr` |
| `WIPEOUT_VR` flag; flat build unchanged | 0 | host | Proof in section 9: Mac clang and Linux arm64 gcc-12 |
| OpenXR host (instance, session, spaces, swapchains, frame loop, extensions) | 0 | host | `src/vr/vr_xr.c`. Fake-runtime tests only; never run against SteamVR |
| GL binding from SDL's x11 window | 0 | not run | `src/vr/vr_gl.c`. Built on Linux arm64; the F-Zero X probe ran the same calls on the Frame |
| OpenXR probe (colour per eye) | 0 | not run | Implemented (`vr.probe_seconds`). **The phase-0 gate is still open: it has not run on the Frame** |
| Settings (`vr.toml`, env overrides) | 1 | host | Unit-tested |
| Linux ARM64 build (Docker), ABI check, tests | 1 | host | Section 9 |
| Flat game on a world-locked panel | 1 | not run | Implemented; no game data and no headset, so it has never drawn a frame |
| Device scripts (`LAUNCH-STEAM-FRAME.sh`, `deploy.sh`) | 1 | not run | Syntax-checked and dry-run only; the Frame was offline |
| Devkit title, data dir on the Frame | 1 | not started | |
| Command list and per-eye replay | 2 | not started | |
| HUD quad, mono panel, fades | 2 | not started | |
| Draw inventory rows | 2 | not started | See section 5 |
| Actions and system layer | 2 | not started | |
| Eye dumps at 0, 90 and 180° yaw | 2 | not started | |
| First wear | 3 | not started | Needs game data on the device |

## 9. Build, test and deploy

**Build.** `cmake -S . -B build -DWIPEOUT_VR=ON` (Linux needs SDL2 with x11, GLEW, the X11 and GLX headers). The VR build:

- fetches OpenXR-SDK release-1.1.63 (commit `f2448a8797c85814aa892efc1ab8707900fbcc78`) at configure time, checks the archive's SHA-256 (`5b8dac06…a88b6f`), and links its loader **statically**; the executable also links libstdc++ statically, so the only shared libraries to ship are SDL2 and GLEW (RPATH `$ORIGIN/lib`);
- builds `third_party/sfvr` as a static library (copied unmodified, see its `VENDORED.md`) and `src/vr/` as `wipeout_vr_core` (everything except SDL and GL), then adds `vr.c`, `vr_gl.c` and `vr_xr_loader.c` to the game;
- on Linux adds `-fsigned-char` (ARM64's `char` is unsigned).

On macOS the VR build configures, compiles, links and passes its unit tests, but `vr_init()` fails there (no GLX), so it runs flat.

**Unit tests (CTest).** The six sfvr tests plus its C++ header check, and `vr_xr` (a fake runtime behind the `vr_xr_api_t` function table: extension negotiation, session creation, the state machine from IDLE to FOCUSED, standby keeping the loop alive, stop and restart, exit, reference-space change, dt from display time, refresh-rate choice, failures, destroy order), `vr_settings`, `vr_math` (panel placement), `vr_clock` and `vr_paths`.

**GL profile decision (2026-10-02): core 4.3 context plus GLSL 330 shaders in the VR build.** The alternative was a compatibility-profile context that keeps the game's GLSL 1.10 shaders.

- SteamVR's `xrGetOpenGLGraphicsRequirementsKHR` reports 4.3 to 4.6, so the VR build must ask for at least 4.3.
- Evidence for core: the F-Zero X M0-A probe on the Frame asked SDL for a 4.1 core (forward-compatible) context and got Zink on Turnip, Mesa 26.3.0-devel, **4.6 core**, a GLX context, and `xrCreateSession` with the Xlib binding succeeded.
- Nothing measured a compatibility profile on the Frame, and Mesa's Zink documentation does not say whether it offers one. Choosing it would bet the phase-0 gate on an unmeasured path.
- Cost of core: `render_gl.c` already uses VAOs and VBOs only, so the game's GL calls are valid in core. Only the shader text changes: `src/vr/vr_shaders_gl330.h` has `#version 330 core` variants of the game and the two post-effect programs (default, CRT), selected in `render_gl.c` by `#if WIPEOUT_VR`. The sampler is renamed `tex`, since `texture` is a built-in function in 330. Edits to upstream's file are the fences and one include.
- Host evidence: in the Linux arm64 container under Xvfb with Mesa llvmpipe, the VR binary asked for 4.3 core, got a 4.5 core GLX context, built the Xlib binding, found no OpenXR runtime and fell back to flat, and `render_init()` compiled and linked all five GLSL 330 shaders (it reached `game_init()`, which stopped at the missing game data). The Frame's Zink driver has not compiled them.

**GL binding.** SDL's video driver is forced to x11 (`SDL_VIDEODRIVER`, unless set). After `SDL_GL_CreateContext`, `vr_gl_init()` checks `SDL_GetWindowWMInfo` says x11, then reads `glXGetCurrentDisplay/Context/Drawable` and the context's FBConfig (`glXQueryContext` plus `glXChooseFBConfig`) through `SDL_GL_GetProcAddress`, and builds an `XrGraphicsBindingOpenGLXlibKHR` (`visualid` 0, as the F-Zero X probe did). The runtime's minimum GL version is checked against the context's `GL_MAJOR_VERSION`/`GL_MINOR_VERSION` before `xrCreateSession`.

**Swapchains.** Colour only: `GL_SRGB8_ALPHA8` if offered, else `GL_SRGB8`, else RGBA8 with a warning. Eye size is the runtime's recommended size times `vr.resolution_scale`, clamped to the runtime's maximum. Copies into the images run with `GL_FRAMEBUFFER_SRGB` off (and restore it), so the already gamma-encoded frame lands unconverted.

**Extensions.** `XR_KHR_opengl_enable` is required. `XR_VALVE_frame_controller_interaction` (enabled for the actions milestone) and `XR_FB_display_refresh_rate` (the nearest offered rate to `vr.refresh_rate` is requested at session start) are enabled only when the runtime offers them. The 72 Hz fallback logic is not built.

**Linux ARM64 build.** `tools/docker/linux.Dockerfile` (Ubuntu 22.04, gcc-12, CMake 3.31.6, SDL2 2.30.11 built from source, GLEW from the distribution) and `tools/docker/build-linux.sh [off|on|package|abi|verify|all]`. `tools/check_linux_abi.py` (copied from F-Zero X) fails above glibc 2.35, on an unbundled library, or on a CPU level the code does not use.

**Flat build unchanged.** `tools/verify_flat_build_unchanged.sh` builds upstream's HEAD (`b425ecb`) and the working tree with `WIPEOUT_VR=OFF`, the same compiler and flags (`-ffile-prefix-map` so `__FILE__` matches), and compares every object file. All are byte-identical except `render_gl.c.o`: the fences shift later lines, and upstream's `die()` prints `__LINE__`. For that file the preprocessed source is identical once the line number in those messages is masked, and the disassembled code is identical.

**Device.** `tools/steam_frame/deploy.sh` rsyncs `build/package/WipEoutVR/` to `steamos@10.86.200.233:~/devkit-game/WipEoutVR/` and registers the devkit title `WipEoutVR` through `~/devkit-utils/steam-client-create-shortcut` (compat tool `SteamLinuxRuntime_4-arm64`, `steam_play=0`). `LAUNCH-STEAM-FRAME.sh` sets `SDL_VIDEODRIVER=x11`, logs to `run-stdout.txt` and execs the binary. Launch from the Frame's Steam library, never over SSH.
