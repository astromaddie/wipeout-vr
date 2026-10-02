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
| Legal | Personal, experimental use. Upstream has no license and may derive from the 2022 leaked PC source; the user does not mind (2026-10-02). The repo is private. Never commit game data |
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

**Current state (phase 2, host-verified only).** All five steps exist.

- `platform_sdl.c` calls `vr_frame_begin()` after pumping SDL events; `system_update()` takes `dt` from `vr_frame_dt()` (the change in `predictedDisplayTime`, then the game's 0.1 s clamp). `render_frame_prepare()` starts recording and `render_frame_end()` replays; `platform_end_frame()` calls `vr_present_frame()`, which ends the XR frame with the layers the replay produced.
- The renderer (`src/vr/vr_render_gl.inc`, compiled inside `render_gl.c`) asks a provider for the frame's views and for images to draw into. `vr.c` is the provider for the runtime; the host harness has a fake one.
- Layers, bottom to top (`vr_compose.c`): the projection layer (stereo), the HUD quad (stereo), the world-locked panel quad (mono), a black fade quad in VIEW space while a mono/stereo switch fades.
- While `shouldRender` is false or the app lacks focus, `dt` is 0 and SDL audio is paused, and the loop keeps waiting, beginning and ending frames. A standby frame records but replays nothing (unless `force_render`). Vsync is off, since `xrWaitFrame` paces the loop.
- Which frames are stereo: `race.c` calls `render_vr_race_view(!g.is_attract_mode)` before the race camera's `render_set_view`, so every race frame is stereo (including the intro fly-over and the pause menu, whose 2D goes on the HUD quad) except the attract mode's cutting cameras, and any frame while `vr.force_mono` is set, or while the head cannot be tracked. Everything else (intro video, title, menus, the main menu's 3D models, highscores) is mono.
- A mode switch (including the first frame) starts the fade and a recentre. A recentre is also requested by `vr_take_recentre_request()` (the input worker's contract; a weak stub in `vr.c` stands in until `vr_input.c` merges), by focus gain and by `XrEventDataReferenceSpaceChangePending`. It is applied at the start of the next stereo frame (the mono panel is re-placed at the next mono frame).
- Not built: `hud_mode` far and leash (near is used, with a log line), `hud_visible`, a desktop mirror of the frame (the window shows nothing), GPU timing.

Every per-frame state change (pickup colour cycling, particles, `cycle_time`) happens inside `game_update`, so the per-eye replay cannot advance state twice.

## 4. Stereo renderer

- **Command list** (`vr_cmd.[ch]`, pure data; the hooks are one fenced `if (vrr.recording)` per function in `render_gl.c`): the commands are
  - `VIEW(pos, angles)` and `VIEW_2D`;
  - `MODEL_MAT` (flagged when it is the sky), `DEPTH_WRITE`, `DEPTH_TEST`, `DEPTH_OFFSET`, `BLEND`, `CULL`, `SCREEN_POS`;
  - `TRIS(first, count)`, an index range into the frame's triangle arena, with atlas UVs already applied; consecutive pushes are one range;
  - `SPRITE(pos, size, color, tex)`.

  Texture creation and upload stay immediate, because they are resources rather than draws. Recording reproduces the flat path's state semantics: `VIEW` resets depth test and write and the model matrix, `render_set_blend_mode` ignores a repeat, `view_mat` and `sprite_mat` are kept for `render_transform`, and the blend and cull state persist across frames (the list records the state a frame started in). Each replay pass sets its whole GL state explicitly, with `GL_FRAMEBUFFER_SRGB` off (the game's colours are gamma-encoded already), so no pass depends on another. The arena goes to the GPU once per frame; sprites are expanded per pass, into a region of their own per pass.
- **Segments**: a command recorded after `VIEW_2D` is in the HUD segment. Each pass runs every state command and draws only its segment: eyes draw the 3D segment, the HUD passes the 2D one, a mono pass both.
- **Coordinate conventions** (derived from `render_set_view` and `mat4_set_*` in `src/types.c`, restated and tested in `vr_math.c`):
  - OpenXR: right-handed, +X right, +Y up, -Z forward, metres.
  - The game's world: +X right, **+Y down**, +Z forward at yaw 0, game units. Right-handed (the usual camera triple).
  - `render_set_view(pos, angles)` builds `R * T(-pos)` with `R = mat4_set_roll_pitch_yaw(pitch, -yaw + M_PI, roll + M_PI)`. The two `M_PI` offsets are what turn the Y-down world into GL view space: at angles 0, `R = diag(1, -1, -1)`.
  - GL view space (+X right, +Y up, -Z forward) has the same axes as OpenXR, so an OpenXR pose is already in the view space of the game camera. No axis swap is needed.
  - The game's `sprite_mat` is `R^T * diag(-1, -1, 1)` at roll 0.
- **Eye view**: with `A` the anchor's view rotation (from `vr_anchor_angles`), `Rrc` the rotation by minus the recentre yaw about +Y and `M` the eye's orientation, the eye's view rotation is `(Rrc * M)^T * A`. Its position is the game camera's plus `A^T * world_scale * (head_translation * Rrc * (head - origin) + Rrc * (eye - head))`: the head centre (midway between the eyes) is scaled by `head_translation`, the eye's offset from it never is, and the IPD comes only from the runtime's eye poses. With an identity head at the recentre origin, the centre eye's view equals the game's own `view_mat` exactly (tested).
- **Eye projection**: the runtime's asymmetric fov in the game's GL clip convention (`z` in -1..1), near 16, far 64000 (`NEAR_PLANE`, `FAR_PLANE`). The `camera_pos` uniform of the distance fade is the eye's position.
- **Anchor**: the game camera's position, yaw and pitch, with roll 0, so pitch follows the track. Roll follows the camera (`save.internal_roll`) only when `vr.follow_vehicle_rotation` is on. `vr.horizon_lock` also drops pitch. The eye images are submitted with the poses and fovs they were rendered with.
- **Recentre**: records the head's yaw and position (`vr_recentre_t`); the head then sits at the game camera, facing where the camera faces, so the yaw is transferred into the VR offset. It is triggered by the input layer's request (L View held 1 s, the input worker), `XrEventDataReferenceSpaceChangePending`, focus gain, and every mono/stereo switch, and applied at the start of a stereo frame. `diag_yaw` turns the head after the recentre, so it stays visible.
- **HUD** (`hud_mode near`): a quad layer `hud_distance` metres (default 2.0) along the head's forward, following its yaw and pitch and never its roll (`vr_hud_pose`), 40 degrees wide at `hud_scale` 100% (the width scales with the percentage), 16:9, in a 1920x1080 texture (the 2D layout size, which is also `render_size()` in VR; `ui_scale` is 3). The 2D segment is replayed twice, over black and over white, and a resolve pass solves for the alpha (`a = 1 - mean(over_white - over_black)`, colour = over_black / a, straight alpha, `UNPREMULTIPLIED` layer flag). The game's own framebuffer alpha is never read.
- **Target reticle**: `render_transform` in a stereo frame projects through the head-centre camera with its roll removed onto the HUD quad's frustum, so the reticle sits over its target as seen from the head. Parallax between the two eyes at the quad's 2 m is not corrected.
- **Mono panel**: the 1920x1080 texture, the game's camera and native projection, one pass, on a quad `panel_width` metres wide, `panel_distance` ahead of the head (levelled, heading only), world-locked, re-placed on recentre requests.
- **Fade**: a 4x4 black swapchain image at the fade's alpha on a quad in VIEW space 0.35 m ahead, 2.6 m wide, 0.25 s at alpha 1 then 0.35 s linear to 0, on every mono/stereo switch and the first frame.
- **Diagnostics** (section 7): `diag_yaw`, `dump_frame`, `timing`, `force_render`.

## 5. Draw inventory (camera-relative and screen-space draws)

| File:function | What it draws | Flat rule | VR rule | Verified by |
| --- | --- | --- | --- | --- |
| `scene.c:scene_draw` (sky) | Sky object translated to `camera->position + sky_offset`, depth write off | Follows the camera | `render_vr_sky(true)` around the draw tags its `MODEL_MAT`; replay re-translates it from the game camera to each eye | Host harness "sky at infinity": the +x/+z sky edge stays within 2 px when the head moves 1 m (without the re-translate it moves 9 px). `scene.c` itself has not run (no game data) |
| `scene.c:scene_draw` (objects) | Cull: `dot(cam_pos - origin, cam_fwd) < radius` and distance < 64000 | Forward half-space cull from the game camera | `render_vr_wide_cull()` in a stereo frame skips the half-space test: distance only | Host harness at head yaw 180: the marker behind the camera is drawn and centred in both eyes (it fails with the hook forced off). The harness mirrors the cull logic through the same hook; `scene.c` has not run |
| `track.c:track_draw` | Sections, culled by the camera-forward plane | Forward half-space cull | Same hook: distance only, `FAR + radius` | Hook shared with the row above; the harness ground strip is not culled. `track.c` has not run; the diff is reviewable and flat-unchanged |
| `render_gl.c:render_push_sprite` | Billboards expanded with `sprite_mat` from the game camera angles | Faces the game camera | Recorded as `SPRITE`; expanded per pass with `R_eye^T * diag(-1, -1, 1)`, facing that eye | Host harness "sprite facing": head turned 30 degrees, both eyes, aspect 0.979 (a game-camera billboard would be 0.87). The mono replay of the same sprite equals the flat renderer pixel for pixel. `vr_view` pins the matrix against `sprite_mat` |
| `render_gl.c` vertex shader `fade` | Alpha fade by distance from `camera_pos` | Game camera | `camera_pos` is the eye's position, set at each `VIEW` | Host harness: a cube 55400 units away, head leaning 3 m forward, blue channel within 0.2 levels of the analytic smoothstep for both eyes (it fails with the game camera's position) |
| `race.c` `render_set_screen_position(g.camera.shake)` | Screen-space shake offset (`screen` uniform) | Clip-space offset | `SCREEN_POS` is ignored in eye and HUD passes (kept in the mono panel, where the main menu uses it to place models). The `camera_shake` key does nothing yet. The optional `impact` haptic is the input worker's | Host harness: every stereo frame sets a (0.3, 0.3) shake and the markers still land on their analytic pixels; the mono frame with the same shake equals the flat renderer |
| `ship.c` / `particle.c` depth offset −32 | Shadows and particles | Polygon offset | Unchanged, per eye (`DEPTH_OFFSET` replays as `glPolygonOffset`) | The harness sprite is drawn with it, additive and without depth write, equal to flat in mono. Shadows have not drawn (no data) |
| `hud.c:hud_draw_target_icon` | Target reticle from `render_transform` (mono view and projection) | Screen projection | In a stereo frame `render_transform` projects through the head-centre camera (roll removed) onto the HUD quad's frustum | Host harness: straight ahead lands at NDC (0, 0), 10 degrees right at 0.4845 (`tan 10 / tan 20`), behind the head z >= 1; `vr_view` pins the HUD camera. **Headset-unverified**: the reticle is a 2 m overlay while its target is far, so the eyes see parallax between them |
| `hud.c`, `ui.c`, `menu.c` 2D | All 2D text and quads, after `render_set_view_2d` | Ortho over the backbuffer | HUD swapchain on a quad. Alpha derived from renders over black and over white (lesson 6) | Host harness: authored alpha 128, 255 and 64 come back as 128, 255 and 64 (colour within 10), 88% of the HUD transparent where nothing was drawn (it fails with alpha forced to 1). The real `hud.c`, `ui.c` and `menu.c` have not run; text legibility at 40 degrees is unjudged |
| `main_menu.c` 3D ship and logo models | `render_set_view` at the origin, plus a screen offset | Mono 3D | Mono frame on the world-locked panel (no `render_vr_race_view`) | Host harness: a mono frame with `render_set_screen_position`, depth offsets and additive sprites is 0 of 518400 pixels different from the flat renderer. `main_menu.c` has not run |
| `intro.c` MPEG video | 2D texture | Fullscreen | Mono panel (cinema quad later). `render_texture_replace_pixels` stays immediate | The mono path as above. The video has not played |
| `render_gl.c` post effects (CRT) and `RENDER_RES_240P/480P` | Backbuffer post-process | Fullscreen | Ignored while the renderer is attached; `render_size()` is the 2D layout size. The eyes are the runtime's recommended size times `resolution_scale` (phase 1) | Host harness: with CRT and 240p requested, `render_size()` stays 960x540 and the mono replay still equals the flat renderer without them |

## 6. Controls (`src/vr/vr_input.{h,c}`)

OpenXR actions feed the game's own pad layer, so its bindings, prompts and menus stay valid. One action set (`wipeout`) holds 20 actions: 14 booleans (A, B, X, Y, the two bumpers or grips, Menu, View, the two stick clicks, the D-pad), 2 floats (the triggers), 2 vec2 (the sticks) and 2 vibration outputs. Setup order: `xrCreateActionSet`, every `xrCreateAction`, `xrSuggestInteractionProfileBindings` (Frame, Index, Touch), `xrAttachSessionActionSets`, all right after `xrCreateSession` (`vr_input_create`). The Frame profile is suggested **only when `XR_VALVE_frame_controller_interaction` is enabled** (`vr_xr_t.ext_frame_controller`, which is true only when the runtime offered it). A runtime that rejects a profile is logged and skipped.

**Merge.** Each frame, after `xrBeginFrame` (`vr_frame_begin` in `vr.c`), `vr_input_update` calls `xrSyncActions` and reads every action. It then calls `input_set_button_state(INPUT_GAMEPAD_*, value)` **only for codes whose value changed** since the last frame. Sticks and triggers become the game's split-axis codes the way `platform_sdl.c` does it: a trigger is its own code (0..1), and each stick axis is a pair (`LEFT`/`RIGHT`, `UP`/`DOWN`) with at most one non-zero; stick up is `UP`. With no focus, no running session or a sync that is not `XR_SUCCESS` (`XR_SESSION_NOT_FOCUSED`), every non-zero code is written back to 0, the L View press is cancelled without an event, queued haptics are dropped and playing ones are stopped.

**Game codes, by profile.** The same game code on every profile. The columns are the OpenXR component paths that were suggested (all under `/user/hand/`).

| Game code | Game action | Frame (`/interaction_profiles/valve/frame_controller_valve`) | Index (`/interaction_profiles/valve/index_controller`) | Touch (`/interaction_profiles/oculus/touch_controller`) |
| --- | --- | --- | --- | --- |
| `L_STICK_*` (`LEFT/RIGHT/UP/DOWN`) | Steer and pitch (VR alias); menu navigation (system layer) | `left/input/thumbstick` | `left/input/thumbstick` | `left/input/thumbstick` |
| `R_STICK_*` | Unbound by default; free for the controls menu | `right/input/thumbstick` | `right/input/thumbstick` | `right/input/thumbstick` |
| `DPAD_*` | Steer, pitch (game default); menus | `left/input/dpad_{up,down,left,right}/click` | none | none |
| `A` | Thrust (game default), menu select | `right/input/a/click` | `right/input/a/click` | `right/input/a/click` |
| `B` | Menu back | `right/input/b/click` | none | `left/input/x/click` |
| `X` | Fire (game default), menu back | `right/input/x/click` | `right/input/b/click` | `right/input/b/click` |
| `Y` | Change view | `right/input/y/click` | `right/input/thumbstick/click` | `right/input/thumbstick/click` |
| `L_SHOULDER` / `R_SHOULDER` | Air brakes | `left/input/bumper/click` / `right/input/bumper/click` | `left/input/squeeze/value` / `right/input/squeeze/value` | `left/input/squeeze/value` / `right/input/squeeze/value` |
| `L_TRIGGER` | Unbound by default | `left/input/trigger/value` | `left/input/trigger/value` | `left/input/trigger/value` |
| `R_TRIGGER` | Thrust (VR alias) | `right/input/trigger/value` | `right/input/trigger/value` | `right/input/trigger/value` |
| `START` | Pause / start (system layer: R Menu) | `right/input/menu/click` | `left/input/b/click` | `left/input/menu/click` |
| `SELECT` | Menu back (system layer: L View short press); never bound in the game's own layer | `left/input/view/click` | `left/input/a/click` | `left/input/y/click` |
| `L_STICK_PRESS` | Unbound by default | `left/input/thumbstick/click` | `left/input/thumbstick/click` | `left/input/thumbstick/click` |
| `R_STICK_PRESS` | Unbound by default | `right/input/thumbstick/click` | none (the right stick click is `Y`) | none (the right stick click is `Y`) |
| Haptics | `system`, `impact`, `ui_click` | `left/output/haptic`, `right/output/haptic` | same | same |

Sources of the component paths (all unverified on the device; the Frame was offline):

- **Frame**: `/Users/mads/code/F-Zero X/port/vr/gdx_openxr.cpp` (the `frame_controller_valve` suggestion, about lines 663 to 682) and `/Users/mads/code/HaloCE VR/port/android/host/host_xr.c` (the `frame[]` table, about lines 317 to 351). Both cite the Steamworks "Steam Frame Input" page. They agree on every path used here but one: F-Zero X binds the triggers as `trigger/click`, Halo as `trigger/value`. This port uses `trigger/value`, because thrust is analog and the actions are floats. Stick clicks and the right thumbstick come from Halo's table. The skill's `platform.md` gives only the layout (D-pad left, ABXY right, sticks, triggers, grips, bumpers, Menu, View).
- **Index** and **Touch** paths: F-Zero X's suggestions (`gdx_openxr.cpp` about lines 684 to 709: thumbsticks, `a/click`, `b/click`, `squeeze/value`, `trigger/value`, the left `a`/`b` or `y`/`menu` stand-ins for View and Menu) plus Halo's Touch table (`left/input/x/click`, `y/click`, `menu/click`) and Index table (`thumbstick/click`). The assignment of those inputs to game codes is this port's own: neither has bumpers, a D-pad, View or a right Menu button, so the grips are the air brakes, a left face button stands in for View, and another for Menu.
- On the Frame the physical letters are the game's letters (the game prints `A`, `X`, `Y`, `LSHLDR` in its controls menu). Index and Touch cannot match them (right `B` is `X`). The final map still needs the user's approval (phase 4).

**Default aliases (VR build, once, at the end of `game_init`).** The game's own bindings are kept as they are. `vr_input_apply_default_bindings(save.buttons)` adds, in the **user** input layer:

- `L_STICK_LEFT/RIGHT/UP/DOWN` to `A_LEFT/A_RIGHT/A_UP/A_DOWN`;
- `R_TRIGGER` to `A_THRUST`.

An alias is added only when nothing is bound to its button in the user layer and the action's joystick slot in `save.buttons` still holds the game's default (D-pad direction, or `A`). That is, it is skipped when the user has bound the stick direction or trigger, or has rebound or cleared that action's joystick slot. The aliases sit outside `save.buttons`, which has one joystick slot per action and is written to the save file. So the D-pad and `A` stay in the slot, a flat build reading the same save does not see the aliases, and the controls menu (which shows the slot) shows what the user set; it does not list the aliases. The system layer is untouched, because `L_STICK_*` is already bound there to the menu actions. A binding made later from the controls menu replaces the alias for that button (`input_bind` keeps one action per button per layer). `SELECT` is bound to `A_MENU_BACK` in the system layer unless something is bound there already (`input_layer_bound_to_action`, added behind `WIPEOUT_VR` at the end of `input.c`). Upstream's controls menu does not unbind the old button of a rebound action until restart; that is unchanged.

**System layer** (`vr_pad_update`, L View through `sfvr_view` at the frame's predicted display time):

| Input | Result |
| --- | --- |
| R Menu | `INPUT_GAMEPAD_START` |
| L View, short press | `INPUT_GAMEPAD_SELECT` for one frame (then `A_MENU_BACK` in menus) |
| L View held 1 s, and again at 3 s | `vr_request_recentre()` and the `system` haptic on both hands. The renderer consumes `vr_take_recentre_request()`; the input module recentres nothing. There is no standing height in a seated racer, so 3 s does the same as 1 s |
| System button | Never bound |

A View press that was already down when focus returned is ignored until it is released and pressed again.

**Haptics** (the `sfvr_haptics` queue, flushed once a frame in `vr_input_update` through `xrApplyHapticFeedback`, scaled by `vr.haptics` (default 0.6), `XR_FREQUENCY_UNSPECIFIED`, coalesced per hand to the strongest amplitude and longest pulse):

| Event | Trigger | Pulse (before `vr.haptics`) | Hands |
| --- | --- | --- | --- |
| `system` | L View hold, 1 s and 3 s | 0.6 / 80 ms | both |
| `impact` | `camera_set_shake` (`camera.c`, one fenced call; ship hits and weapon hits). Not gated by `vr.camera_shake` | 0.35 / 30 ms for a 0 s shake, up to 1.0 / 80 ms at `CAMERA_SHAKE_LONG` (2/3 s), linear in the duration. `CAMERA_SHAKE_SHORT` gives 0.415 / 37 ms | both |
| `ui_click` | `sfx_play(SFX_MENU_SELECT)` (`sfx.c`, one fenced call): every menu select, toggle, back and the title screen's start | 0.5 / 30 ms | both. There is no pointing hand: select is on the right and navigation on the left |
| `ui_hover` | not hooked (`SFX_MENU_MOVE` would be the place) | | |

Handedness (`dominant_hand`) is not applied: thrust is always the right trigger. The settings page, which the standard calls for, is not built.

## 7. Settings and diagnostics (`vr.*` keys; env `WIPEOUT_VR_<KEY>`)

The settings file is `$XDG_DATA_HOME/WipEoutVR/vr.toml` (`~/.local/share/WipEoutVR/vr.toml` when `XDG_DATA_HOME` is unset or relative). It has a top-level `version` (the sfvr schema, 1) and a `[vr]` table. `src/vr/vr_settings.c` reads it with the sfvr key registry (`third_party/sfvr`, which is a registry only and does no file IO) plus the port keys below:

- A missing key gets its default, an invalid value (not a number, unknown enum or bool text) gets the default, an out-of-range value is clamped. Unknown keys, other tables, comments and layout are kept as they are. The file is rewritten, atomically (temp file, `fsync`, `rename`), only when something was added or repaired.
- `WIPEOUT_VR_<KEY>` (for example `WIPEOUT_VR_RESOLUTION_SCALE=1.2`) overrides a key for the run. It is clamped the same way and never written back.

Port keys (not in the sfvr registry):

| Key | Default | Meaning |
| --- | --- | --- |
| `world_scale` | 128.0 (**placeholder**, 16 to 1024) | Game units per metre. The chase camera sits 1024 units behind the ship, which should be about 8 m. Derive it from the ship hull length and confirm at first wear |
| `force_mono` | false | Keep race frames on the mono panel, with no stereo |
| `panel_distance` | 2.0 m (0.8 to 20) | The flat panel's distance |
| `panel_width` | 2.4 m (0.5 to 10) | The flat panel's width; its height follows the window's aspect |

The doc's other keys are sfvr standard keys with sfvr's defaults: `follow_vehicle_rotation` (off), `horizon_lock` (off), `hud_mode` (`near`), `hud_distance` (2.0 m), `hud_scale` (100), `resolution_scale` (1.0), `refresh_rate` (90), `camera_shake` (off), and the rest of the standard set. The settings page (phase 4) is not built.

Diagnostics: `force_render`, `diag_yaw`, `dump_frame`, `probe_seconds`, `timing` are registry keys, all implemented. Log prefixes are `[vr]` for state changes and `[vr-perf]` every 10 s.

- `force_render`: submit layers in standby, with a synthetic head at 1.6 m (`vr_xr.c`), so unattended runs draw.
- `diag_yaw` (degrees, positive turns left): turns the head about its centre after the recentre, so a synthetic head yaw is visible. The eyes are submitted with the turned poses.
- `dump_frame=N`: the N-th replayed frame (counting from 1) writes `vr-eye0.bmp`, `vr-eye1.bmp` (24-bit) and `vr-hud.bmp` (32-bit, straight alpha) for a stereo frame, `vr-panel.bmp` for a mono one, and `vr-state.txt` (mode, recentre, head and eye poses, game-space eye positions, HUD pose, fade, counts, timings) into `<data>/vr-dumps/`. The pixels are copied into a PBO behind a fence and written by a later frame, so the frame never waits; nothing reads back in a normal frame.
- `timing`: a `[vr-perf]` line every 10 s through `sfvr_perf`: `fps`, `missed` (display time advanced by more than 1.5 periods), `cpu` and the stages `record`, `replay` (both eyes) and `hud`, `eye=` left/right, `gpu=n/a`. All are CPU submission times. `record` is measured with a clock read per recorded call, so it only costs anything under `timing`.

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
| Settings (`vr.toml`, env overrides) | 1 | host | Unit-tested; `diag_yaw`, `dump_frame` and `timing` now do something |
| Linux ARM64 build (Docker), ABI check, tests | 1 | host | Section 9 |
| Flat game on a world-locked panel | 1 | host | Now the mono path: the panel is rendered into directly (no back-buffer copy). Equal to the flat renderer in the harness; never drawn a real frame, no game data and no headset |
| Device scripts (`LAUNCH-STEAM-FRAME.sh`, `deploy.sh`) | 1 | not run | Syntax-checked and dry-run only; the Frame was offline |
| Devkit title, data dir on the Frame | 1 | not started | |
| Command list and per-eye replay | 2 | host | Recorded and replayed in `render_gl.c` (`vr_render_gl.inc`). Host harness: 324 pixel and state checks pass under Xvfb + llvmpipe; the mono replay equals the flat renderer. Never run against the Frame's Zink |
| HUD quad, mono panel, fades | 2 | host | HUD alpha solve, quad layer, panel and fade quads built (`vr_compose`, unit-tested) and rendered in the harness. Never submitted to a runtime; fade and HUD placement unworn |
| Draw inventory rows | 2 | host | All rows implemented and verified as section 5 says. The game's own `scene.c`, `track.c`, `hud.c`, `main_menu.c` have not run: no game data |
| Actions, input merge, aliases, system layer (View, Menu, recentre request) | 2 | host | `src/vr/vr_input.c`. Unit-tested against a fake runtime and the game's real input layer; never run against SteamVR, never worn. Binding paths are copied from F-Zero X and Halo, unverified (section 6) |
| Haptics (`system`, `impact`, `ui_click`) | 2 | host | Same tests. The buzz has never been felt. `ui_hover` is not hooked |
| Recentre itself | 2 | host | View hold 1 s calls `vr_request_recentre()`; the renderer takes it at the next frame boundary (`vr_stage` tests). Never worn |
| Eye dumps at 0, 90 and 180° yaw | 2 | host | `build/vr-harness/yaw-{000,090-right,180}/`: the product's own BMP dumps (`dump_frame`), plus PNG copies. Synthetic scene, not the game |
| First wear | 3 | not started | Needs game data on the device |

## 9. Build, test and deploy

**Build.** `cmake -S . -B build -DWIPEOUT_VR=ON` (Linux needs SDL2 with x11, GLEW, the X11 and GLX headers). The VR build:

- fetches OpenXR-SDK release-1.1.63 (commit `f2448a8797c85814aa892efc1ab8707900fbcc78`) at configure time, checks the archive's SHA-256 (`5b8dac06…a88b6f`), and links its loader **statically**; the executable also links libstdc++ statically, so the only shared libraries to ship are SDL2 and GLEW (RPATH `$ORIGIN/lib`);
- builds `third_party/sfvr` as a static library (copied unmodified, see its `VENDORED.md`) and `src/vr/` as `wipeout_vr_core` (everything except SDL and GL), then adds `vr.c`, `vr_gl.c` and `vr_xr_loader.c` to the game;
- on Linux adds `-fsigned-char` (ARM64's `char` is unsigned).

On macOS the VR build configures, compiles, links and passes its unit tests, but `vr_init()` fails there (no GLX), so it runs flat.

**Unit tests (CTest).** The six sfvr tests plus its C++ header check, and `vr_xr` (a fake runtime behind the `vr_xr_api_t` function table: extension negotiation, session creation, the state machine from IDLE to FOCUSED, standby keeping the loop alive, stop and restart, exit, reference-space change, dt from display time, refresh-rate choice, failures, destroy order), `vr_settings`, `vr_math` (panel placement), `vr_clock` and `vr_paths`, and the phase-2 ones:

- `vr_input`: the pad mapping with write-on-change and release on focus loss, the `sfvr_view` timing, the alias rules against the game's real `src/input.c`, and the haptic table.
- `vr_input_xr`: a fake runtime checks setup order, the Frame profile only when the extension is enabled, bindings, per-frame sync and release, and the haptic calls.

- `vr_view`: the eye and HUD maths against the game's own matrices (`src/types.c` is linked as the oracle). A centred identity head equals `render_set_view`'s matrix (default, with `follow_vehicle_rotation`, with `horizon_lock`); head yaw to the right looks along the game camera's right; each eye is half the IPD along the camera's right, scaled by `world_scale`; `head_translation` scales the head and not the eyes; recentre absorbs yaw; asymmetric fov hits the clip planes; the disparity sign; the HUD pose follows yaw and pitch and never roll; `diag_yaw`.
- `vr_stage`: the command list (order, coalescing, growth), the fade curve, mode switches, recentre applied once, the panel placed then held, and the `[vr-perf]` line.
- `vr_compose`: the layers a frame submits (projection with the renderer's poses, HUD flags, mono panel, fade in VIEW space, order, absent layers).

**Host render harness** (`tests/vr/vr_render_harness.c`, CTest `vr_render_harness`, Linux only). It links the real renderer (`render_gl.c` with `WIPEOUT_VR`, the command list, the replay, the HUD alpha solve, the dumps) and drives it with a synthetic scene, a hidden SDL window, and a fake view provider standing in for the runtime. No game data. It needs a GL context: run it under Xvfb with Mesa llvmpipe, which `tools/docker/linux.Dockerfile` installs (`xvfb`, `libgl1-mesa-dri`); CTest registers it through `xvfb-run` when that exists. `vr_render_harness <dir>` writes the dumps under `<dir>` (`build/vr-harness/` for the manual run, `<build>/vr-harness/` under CTest). It asserts the mono replay equals the flat renderer, that recording draws nothing, markers centred per eye at head yaw 0, 90 and 180 against an analytic pixel expectation, the disparity sign and size, sprite facing, the sky at infinity, the fade measured from the eye, HUD alpha, `diag_yaw`, recentre, standby, `force_mono`, `timing` and the dump files. Mutation checks (wide cull off, no sky re-translate, game-camera sprites, shake applied, alpha forced to 1, fade from the game camera) each fail it.

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
