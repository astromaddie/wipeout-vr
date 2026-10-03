# Cockpit H (full canopy): mockup, assets and build plan

Status (2026-10-04): the mockup round is done and the user picked a design, cockpit H. The asset set below is waiting for the user's approval. Nothing is built into the game yet, and `src/` is untouched. The UI working agreement goes mockup with variations, then all the UI's assets for approval, then the build.

The idea: in the VR internal view you sit inside your own ship. The player's real ship model is drawn double-sided around the eye, with a cabin cut into it. Built geometry adds a canopy frame, a rear bulkhead and a roll hoop, plus a dash with diegetic instruments, and the floating HUD panel goes away. By default the cockpit follows ship yaw and pitch but stays level in banks.

Precedent is the Star Fox cockpit (`starfox-enhanced` 99712f5, 82fd741, ddee4ff, 1d68555) and lessons 2, 29, 30 and 32 in the playbook.

## Decisions (user, 2026-10-04)

| Topic | Decision |
| --- | --- |
| Design | A hybrid: C1's slim teardrop frame (thin struts, raked windscreen) with C2's dash (full-width shell, two angled pods at ±27°, the nose clear, a weapon readout). C1's centre binnacle and all of C3 are dropped |
| Weapon readout | It has to suit the slim frame (C1 has no heavy header), with the 0.41 m refocus in mind. My choice is below |
| world_scale | Stays 128 |
| Eye | The game's own point, `ship_cockpit()` = (0, -128, 0), unchanged |
| Shield slot | Keep the shield-pickup timer for now. The slot stays generic, since the user may change it |
| Banking | All three modes become a setting; level-in-banks is the default |
| Trim | Sampled from each ship's own textures (all 8), not hand-picked team colours |

## Files

| Path | What |
| --- | --- |
| `tools/mockup/export_mockup.c` | Exporter. It links the game's own `object.c`, `image.c`, `track.c` and `ui.c` against a capture stub for `render.h`. The stub keeps texture pixels and records the triangles `object_draw()` pushes, in model space |
| `tools/mockup/build.sh` | Builds and runs the exporter, then the generator. `tools/mockup/build.sh <data_root>`, where the root holds `wipeout/...` |
| `tools/mockup/build_cockpit_mockup.py` | Writes `build/mockups/cockpit-mockup.html`: one file with three.js r158 (fetched once, SHA-256 pinned, cached in `build/mockup-cache/`) and the data inlined as base64. Opens from `file://` |
| `tools/mockup/cockpit_mockup.js` | The mockup itself: cabin, cockpit H plus C1-C3 for comparison, instruments and pod faces, trim sampling, checks, track animation, UI. With `?sheet=assets` it draws the asset sheet; with `?export=geometry` it exports the parts |
| `tools/mockup/shoot_cockpit_mockup.py` | Headless Chrome: the hybrid shots, `checks.json`, the hybrid sheet and the asset sheet. With `--variations`, the C1-C3 set too |
| `tools/mockup/export_cockpit_geometry.py` | Writes `tools/mockup/cockpit_geometry.json` from the page's own builders. With `--c OUT`, it also writes the C table |
| `tools/mockup/cockpit_geometry.json` | Committed. Cockpit H's geometry for the build: parts per hull shape in game units, material slots, faces, the cut box and the trim rule. It holds no game data |

Everything generated lands in `build/` (git-ignored) and is derived from game data, so none of it gets committed:

- `build/mockup-data/`: the exported geometry and textures.
- `build/mockups/`: `cockpit-mockup.html`, `shots/`, `checks.json`, `cockpit-hybrid-sheet.png`, `cockpit-assets.png` and, from round 1, `cockpit-contact-sheet.png`.
- `build/cockpit/vr_cockpit_geometry.inc`: the C table, which is no game data, but generated.

The exporter takes all 8 player ships, track01 (Terramax: track, scenery and sky, through `track_load` and `objects_load`), `speedo.tim`, `wicons.cmp` and the `drfonts.cmp` font. The glyph table comes from drawing each glyph through `ui_draw_text`. The speedo bar table is parsed from `src/wipeout/hud.c` at build time, so the bars match the game. Exporting the track turned out cheap, so the mockup drives the real track01 loop instead of a stylised one.

## Coordinates and the eye

Game space is +X right, +Y down, +Z forward, in game units. The eye is the game's own `ship_cockpit()` = (0, -128, 0) in ship space, 128 units above the ship origin. The nose point is (0, 0, 512); the meshes reach z = ±416 to ±425.

The cockpit is authored as part of the ship, in metres at 128 units per metre relative to that eye: r right, u up, f forward. A point (r, u, f) is game (128 r, -128 - 128 u, 128 f). Because the cockpit belongs to the ship, `world_scale` scales the hull, the cockpit and the world together about the eye (lesson 29). So it changes no angle at all. A mono view looks identical at every scale. What changes is how big everything is against your body: stereo depth, head translation, your knees, the eye offset in centimetres. The mockup shows that with a 1.8 m reference pilot drawn at physical size (x-ray in the outside view), plus the implied sizes next to the slider.

Eye adjust (fore/aft and height, ±30 cm) is physical, so it maps to `cm / 100 * world_scale` units.

## Ship dimensions

Measured from the captured triangles. The 8 ships are 4 hull shapes, one per team. Every polygon in `allsh.prm` is flagged single-sided, which is why the cockpit draws the hull double-sided (back faces dimmed, so the inside reads as an interior). The deck is the hull top on the centre line at the eye station (z = 0).

| # | Model | Team | Pilot | Tris | L x W x H (units) | L x W x H at 128 u/m | Deck y at z = 0 | Eye above deck |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | sophia | Feisar | Sofia de la Rente | 186 | 850 x 362 x 164 | 6.64 x 2.83 x 1.28 m | -68 | 0.47 m |
| 1 | solaar | Qirex | Kel Solaar | 131 | 840 x 342 x 154 | 6.56 x 2.67 x 1.20 m | -63 | 0.51 m |
| 2 | jacko | Feisar | Paul Jackson | 186 | 850 x 362 x 164 | 6.64 x 2.83 x 1.28 m | -68 | 0.47 m |
| 3 | chang | AG Systems | Daniel Chang | 148 | 832 x 366 x 166 | 6.50 x 2.86 x 1.30 m | -70 | 0.46 m |
| 4 | arian | Qirex | Arian Tetsuo | 131 | 840 x 342 x 154 | 6.56 x 2.67 x 1.20 m | -63 | 0.51 m |
| 5 | arial | Auricom | Arial Tetsuo | 137 | 840 x 366 x 166 | 6.56 x 2.86 x 1.30 m | -70 | 0.45 m |
| 6 | anasta | Auricom | Anastasia Cherovoski | 137 | 840 x 366 x 166 | 6.56 x 2.86 x 1.30 m | -70 | 0.45 m |
| 7 | Dekka | AG Systems | John Dekka | 148 | 832 x 366 x 166 | 6.50 x 2.86 x 1.30 m | -70 | 0.46 m |

Widths include the rear pods. All four hulls are low wedges. The deck ridge rises from y ≈ -27 at z = 300 to a peak of -82 around z = -100, just behind the eye, and the fuselage at the eye station is only 0.5 m deep at 128 u/m. The Feisar hull is the most different: it's deeper (down to y = +50) and wider forward. The Qirex hull is a narrow, shallow wedge with a trough along its top. That's why the review shots use ships 0 and 1.

## world_scale: keep 128

My recommendation is to keep the placeholder, 128 units per metre. It makes a ship about 6.6 m long, 2.8 m wide over the pods and 1.3 m tall.

My real-world assumption is that a WipEout craft is a single-seat racer a bit bigger than an F1 car (5.6 x 2.0 x 0.95 m). The game's own eye point decides it, though, because a pilot has to fit the hull with their eye there:

- At 128 a seated pilot fits naturally. The eye is 0.46–0.51 m above the deck, which puts your chest at deck level and your hips inside the fuselage, with the cabin floor 0.92 m below the eye. Your legs run forward under the deck. See the "world_scale 128" side view.
- At 192 (a 4.4 m ship) the same pilot's legs come out through the hull's underside and the head fills the canopy.
- At 96 (an 8.8 m ship) the pilot sits low in a tub that's too big.

The rest agrees. Track01 is 2600–3500 units wide, which is 20–27 m: wide, but it fits 8 craft that are 2.8 m wide. The Terramax Venom lap record (57.5 s over a 586,000-unit centre line) averages 80 m/s, about 290 km/h. The chase camera's 1024 units come to 8 m.

Below about 110 the canopy gets roomy and the ship starts to feel like a bus. Above about 150 the pilot no longer fits the hull. Either way it's one key and easy to try in the headset.

## Components (shared)

Dimensions are at 128 u/m. Units are game units (metres x 128).

| Component | Size and placement | Units |
| --- | --- | --- |
| Cabin cut-out | A box from r -0.34 to +0.34, f -0.36 to +0.25 (behind the seat to just ahead of the chest), from the floor up. Hull polygons are clipped against it, keeping the parts outside it (uv and colour interpolated). 5 (Qirex) to 11 (Feisar) polygons drop out entirely | x ±43.5, z -46 to +32, y < -10 |
| Tub | Four walls from the floor up to the hull surface, ray-cast per ship, so the tub never shows outside the hull. Floor 0.02 m thick, 0.92 m below the eye | floor y = -10 |
| Coaming | A rim along the cut edge, on the hull surface, with a trim stripe (`trim_primary`) on the sides: 3.5 x 2.5 / 6 x 4 / 3 x 2 cm (C1/C2/C3) | 4.5 x 3.2 / 7.7 x 5.1 / 3.8 x 2.6 |
| Seat | Cushion 0.42 wide, 0.46 deep, 0.14 tall. Back reclined to 0.30 below the eye | |
| Rear bulkhead | 0.05 thick at f -0.40, 0.80–0.84 wide, from the floor to the eye height minus 0.06 / 0.00 / 0.10 (C1/C2/C3). A headrest tower (0.26 wide) rises to 0.10 above the eye and carries a 0.20 x 0.22 pad. Trim cap (`trim_primary`). Clearance from the eye is 0.34 m | z = -51 to -58 |
| Roll hoop | At f -0.42: posts from the bulkhead's top corners (r ±0.37) to the canopy rail tips (r ±0.27, u +0.30), with a crossbar between the tips. 3.0 / 5.5 / 3.2 cm square. This is the Star Fox 1d68555 load path, so the canopy frame visibly lands on the cabin | z = -54 |
| Canopy sill | On the hull deck, ray-cast per ship. Half-width 0.41 at the head, narrowing toward the front | |
| Glass | Faint tint (4.5% plus a fresnel edge) inside, stronger in the outside view. Never an occluder in the checks | |

Instruments are the game's HUD content, drawn with the game's own font, `speedo.tim`, the speedo bar table and `wicons`. They're laid out in HUD pixels (hud.c's units before `ui_scale`), with a size per variation in mm per HUD pixel:

- Speed and thrust: the speedo facia with the bars from `hud_draw_speedo`, thrust in red under speed, 132 x 26 px.
- Shield: "SHIELD" plus an 8-segment bar. WipEout (1995) has no hull energy; the only shield state is the shield pickup's 6.7 s timer (`SHIP_SHIELDED`, `WEAPON_SHIELD_DURATION`), so the bar shows that timer.
- Lap: "LAP n OF 3".
- Position: "POSITION n".
- Lap time: the current lap time in the 16 px font, then "LAP RECORD" and the record in the 8 px font.
- Weapon: the current `wicons` icon in a frame.

## Cockpit H: the approved design

H is C1's frame on C2's dash. The canopy is C1's slim teardrop:
- 2.4 cm silver struts;
- a raked windscreen whose sill starts 0.85 m up the deck;
- A-pillars to a header at (0, +0.345, +0.08), so the header sits above and slightly ahead of your head rather than in front of you;
- top rails back to the roll hoop, which lands on the bulkhead.

Under it sits C2's dash: a full-width shell (r ±0.40, low in the middle at 0.455 m below the eye, resting on the deck) carrying two angled pods.

The pods are 0.28 x 0.16 m, slightly bigger than C2's, so each face can be one texture. They're centred at (±0.235, -0.335, +0.47): 0.62 m from the eye, 33° down and about 27° to each side. Each has a glareshield lip with a `trim_secondary` edge.

- Left pod: LAP, POSITION and the lap time with the lap record.
- Right pod: the speedo (speed and thrust) over the status slot, which shows the shield timer for now.

The nose runs clear between them, with 96–100% of the ridge visible.

### Where the weapon readout went, and why

It sits on a small keystone display standing on the dash centre, between the pods: a 70 mm square face at (0, -0.405, +0.555), 0.69 m away and 36° down, with a `trim_primary` surround.

The slim frame has nothing heavy to hang a display from. Its header is a 2.4 cm tube that sits almost overhead, and anything hung near the raked windscreen would be under 0.3 m from the eye, closer still than C2's 0.41 m. On the dash it sits at the pods' focal distance:

| Readout | Distance | Optical power (1/distance) |
| --- | --- | --- |
| Pods | 0.62 m | 1.61 D |
| H keystone | 0.69 m | 1.46 D |
| C2 header | 0.41 m | 2.44 D |

So glancing between the pods and the weapon costs no refocus.

It also sits on the centre line, right under the nose: where you're already looking when you aim. The game's own HUD puts the weapon top centre. The nearest in spirit would be projecting it onto the windscreen like C3, which is listed as an open question rather than done.

## Asset set (for approval)

`build/mockups/cockpit-assets.png` (linked from the page) lays out every modular component of H on its own:

- the frame struts, canopy glass, sills and coaming, tub, seat, rear bulkhead, headrest and roll hoop;
- the dash shell, left pod, right pod and weapon display.

Each tile gives the size in metres and game units, the triangle count, its material slots, and whether it varies per hull. The parts that follow the deck vary per hull: struts (the A-pillar feet), glass, sills, tub and dash shell. They're shown on all four hulls. Everything else is shared.

The sheet also shows the trim sampled for all 8 ships, and the three instrument faces at their planned texture resolution:

| Face | Texture | Size |
| --- | --- | --- |
| Left pod | 512 x 292 texels | 0.28 x 0.16 m |
| Right pod | 512 x 292 texels | 0.28 x 0.16 m |
| Weapon | 128 x 128 texels | 0.07 x 0.07 m |

One texel is 0.547 mm, which is about 20 texels per degree at the pods. The Frame shows about 16 px per degree, so the faces don't undersample.

The faces are drawn the way the build will draw them, with the game's own fonts and art at an integer `ui_scale` (positions in texels):

| Face | Element | Position | Scale | Drawn with |
| --- | --- | --- | --- | --- |
| Left pod | LAP n OF 3 | (18, 18) | x2 | `UI_SIZE_8` accent and `UI_SIZE_16`, from `drfonts.cmp` |
| Left pod | POSITION n | (334, 18) | x2 | `UI_SIZE_8` accent and `UI_SIZE_16` |
| Left pod | Lap time | (61, 202) | x2 | `UI_SIZE_16`, then LAP RECORD and the record in `UI_SIZE_8` |
| Right pod | Speedo | (58, 30) | x3 | `hud_draw_speedo`'s bars and the `speedo.tim` facia |
| Right pod | Status slot | (180, 190) | x3 | "SHIELD" in `UI_SIZE_8` plus 8 segments, from the shield timer |
| Weapon | Weapon icon | (10, 10) | x3 | the `wicons.cmp` icon (32 px) in a frame |

The smallest text, 8 px at x2, is 0.78° from the eye; the shield label at x3 is 1.21°.

`build/mockups/cockpit-hybrid-sheet.png` shows H on all four hulls (forward, look left, look back, outside) plus the three bank modes on the same turn.

## Trim sampling rule

The trim is sampled per ship, in the same way at runtime and in the mockup, and it's recorded in `cockpit_geometry.json` as data:

1. For every triangle of the ship model, take 6 barycentric samples: the permutations of (2/3, 1/6, 1/6) and (1/6, 5/12, 5/12). Each is weighted by area / 6 (game units squared).
2. The sample colour is the nearest texel times the vertex colour times 2, as the game's shader shows it, clamped. Transparent texels are skipped.
3. Keep samples with HSV saturation of at least 0.35 and value of at least 0.20. This drops white, grey and black: logos, numbers, glass and grilles.
4. Histogram the kept area by hue in 24 bins of 15°, smoothed [1 2 1] around the circle. Primary is the weighted mean colour of the peak bin and its two neighbours.
5. Secondary is the same around the strongest bin at least 45° from the primary, holding at least 15% of the primary's weight.
6. Scale both so the brightest channel is between 0.70 and 0.95, keeping hue and saturation. The PSX paint is dark and noisy, and trim has to read as an accent. With no secondary, use the primary at 55%.
7. If less than 0.5% of the area is saturated, use neutral trim (the frame silver and grey slots).

On the 8 ships it picks:

| Ship | Primary | Secondary |
| --- | --- | --- |
| Feisar sophia | yellow | blue |
| Feisar jacko | blue | crimson |
| Qirex solaar | green | blue-grey |
| Qirex arian | orange-red | its darker shade (fallback) |
| AG Systems chang | gold | violet-blue |
| AG Systems Dekka | gold | green |
| Auricom arial | red | its darker shade (fallback) |
| Auricom anasta | blue-grey | orange |

The weakest case is anasta, a mostly white hull with 1% saturated area. The rule still finds its blue and orange, and the neutral fallback exists for a hull that's even whiter.

## Geometry data for the build

`tools/mockup/cockpit_geometry.json` (format `wipeout-vr-cockpit-geometry/1`, about 390 kB, 99 parts, 7,700 triangles over the four hulls) is what the build consumes. It holds:

- Ship space in game units with the game's own axes (+y down, +z forward), the eye (0, -128, 0) and reference scale 128.
- The cabin cut box: x ±44.16, y < -11.52 (open top), z -46.08 to +32. The build clips the player's hull against it at load.
- The hull shapes, with the model names and captured triangle counts used to match a ship to its shape: Feisar sophia/jacko, Qirex solaar/arian, AG Systems chang/Dekka, Auricom arial/anasta. With no match, the build draws no cockpit.
- Material slots: `frame`, `frame_dark`, `cabin`, `cabin_dark`, `seat`, `bezel`, `glass`, `trim_primary` and `trim_secondary`. The fixed slots carry an authored default; the trim slots are sampled.
- The trim rule as parameters and steps. No sampled colour is stored.
- The instrument catalogue in HUD pixels.
- The three faces: corners in ship space, texture size, texel size, and elements with texel position and `ui_scale`.
- The parts, one per line: name, component group (the mockup's toggles), slot, `hulls` (`"all"` or one hull id), and triangle vertices.

The parts are authored geometry only. No game mesh, texture or pixel is in the file. The parts that follow the deck (tub wall tops, sill rails, dash shell feet, the A-pillar feet, the glass's lower edge) were fitted to each hull by ray casting in the mockup. So those vertices imply a few dozen deck heights per hull, but nothing of the hull itself.

Recommendation: keep the JSON as the committed, reviewable source, and compile it into C tables rather than parsing JSON at runtime. That's the Star Fox pattern (`generate_cockpit_assets.py` writing `cockpit_assets.inc`).

`export_cockpit_geometry.py --from-json --c src/vr/vr_cockpit_geometry.inc` writes the table: slot enum and default colours, hull model table, part table (`group`, `slot`, `hull_mask`, triangle range), faces, face elements and one flat vertex array. Today it writes `build/cockpit/vr_cockpit_geometry.inc` (540 kB of C, which compiles cleanly with `-Wall`), and the build will commit its output under `src/vr/`. No JSON parser, no file IO, no new dependency. The vertex array could be deduplicated or quantised to int16 later if the binary size matters; 7,700 triangles x 36 bytes is about 280 kB.

## Implementation plan (not started)

Everything lives behind `WIPEOUT_VR`, with the flat build unchanged (`tools/verify_flat_build_unchanged.sh`). New code goes in `src/vr/vr_cockpit.{h,c}` plus the generated `src/vr/vr_cockpit_geometry.inc`. The game files get a few fenced hooks only.

### When the cockpit draws

It draws in a stereo race frame (`render_vr_race_view(true)`), when the player's ship has `SHIP_VIEW_INTERNAL` and the camera is `camera_update_race_internal`, and `vr.cockpit` is on. The attract mode, the chase view, the intro fly-over and the mono panel keep today's behaviour.

### Load (`ships_load`, one fenced call)

`vr_cockpit_load(ship_models, ship_textures)` does three things:

1. It matches each of the 8 models to a hull shape by name and triangle count.
2. It triangulates and clips each model's polygons once against the cut box, the same way `object_draw` would push them. It keeps the parts outside the box, with interpolated uv and colour, plus a reversed copy at 55% colour for the inside of the hull.
3. It runs the trim rule on each model. The pixels come from decoding `allsh.cmp` a second time with `image_load_compressed`/`image_load_from_bytes`, because the GL atlas is not read back (lesson 10). That's about 150 triangles x 6 samples per ship, once.

The authored parts are pre-shaded once: a flat colour per triangle from its slot and a fixed light in ship space, stored as vertex colour / 2 because the game's shader doubles it.

### Draw (`ships_draw`, one fenced branch)

Where `ships_draw` skips the internal-view ship, it calls `vr_cockpit_draw(ship)` instead. That function:

1. Sets the model matrix M_c (see below) with `render_set_model_mat`.
2. Pushes the clipped hull with its texture indices, `render_set_cull_backface(true)`; the inside copy is wound the other way.
3. Pushes the authored parts with `RENDER_NO_TEXTURE` and culling off, since they're double-sided.
4. Pushes the three face quads with a new `VR_CMD_FACE` command.
5. Pushes the glass last, with depth write off and normal blend at alpha 0.045.

The parts are 2,180 triangles per hull, recorded into the frame's command list like any other draw. They replay per eye with no per-eye game work. The cockpit's nearest geometry is 0.34 m (43 units at 128), so the near plane of 16 units (`NEAR_PLANE`, 0.125 m) is fine.

### Instrument faces (the game's own 2D UI into small textures)

1. Each face gets its own GL texture and FBO, owned by the renderer: 512 x 292, 512 x 292 and 128 x 128, created at attach.
2. In cockpit mode, `race.c`'s 2D section calls `vr_cockpit_hud_draw(ship)` instead of `hud_draw(ship)`.
3. That function brackets each face with `render_vr_face_begin(id)` / `render_vr_face_end()`. In between it calls the same code `hud_draw` uses, with `ui_set_scale(n)` and texel positions: `ui_draw_text`, `ui_draw_number`, `ui_draw_time`, the speedo, the weapon icon.
4. The speedo and the icon list are static in `hud.c`, so `hud.c` gets a fenced `hud_draw_speedo_at(pos, scale)` and `hud_weapon_icon(type)`.
5. The recorder tags those 2D commands with a face flag and index. The replay draws each face's commands once per frame into its FBO, with an orthographic projection over the face's texels, before the eye passes.
6. `VR_CMD_FACE(id)` in the 3D segment then draws the face quad with that texture, per eye.

The faces are replayed once and sampled twice, so nothing advances twice (lesson 4). Alpha isn't involved: the faces are opaque, cleared to the bezel colour.

The status slot is a small table of draw functions (`vr_cockpit_status_t`), with one entry, `shield` (`SHIP_SHIELDED` and the remaining shield time), so a later change is a new entry.

The floating HUD quad is not drawn in cockpit mode. The target reticle and WRONG WAY still need a home (open question).

### Bank modes (`vr_math.c`, `vr_render_gl.inc`)

A new port key, `vr.cockpit_bank` = `level` | `follow` | `cockpit` (default `level`), replaces the anchor rule while the cockpit is active. The anchor takes the ship's angles, not the camera's, because the internal camera only takes `internal_roll` (0.6) of the ship's roll. Let E be the eye in world space, `ship_cockpit()` through the full ship matrix, and e = (0, -128, 0) the eye in ship space:

| Mode | Anchor | Cockpit matrix M_c |
| --- | --- | --- |
| `level` | yaw and pitch, roll 0 | T(E) · R(yaw, pitch, 0) · T(-e): the cockpit turns about the eye, never rolls |
| `follow` | the ship's full yaw, pitch and roll | the ship's own matrix: the view and cockpit roll together |
| `cockpit` | yaw and pitch, roll 0 | the ship's own matrix: the cockpit rolls around a level head |

`follow_vehicle_rotation` (the sfvr standard key) keeps governing the chase view. In the cockpit, `cockpit_bank` decides, and the settings page shows the three modes in one row. Steering stays in the ship's frame; WipEout steers in analogue, so the Star Fox 8-way re-projection (lesson 22) isn't needed. That still needs checking in the headset in `follow`.

### Settings keys (port keys, `vr_settings.c`)

| Key | Default | Range | Meaning |
| --- | --- | --- | --- |
| `cockpit` | true | bool | The internal view becomes the cockpit; off keeps the flat internal view plus the HUD panel |
| `cockpit_bank` | `level` | `level`, `follow`, `cockpit` | Bank mode (above) |
| `cockpit_eye_forward_cm` | 0 | -30..30 | Physical eye offset, `cm / 100 * world_scale` units |
| `cockpit_eye_up_cm` | 0 | -30..30 | Same, vertical |
| `cockpit_glass` | true | bool | Canopy glass tint on or off |
| `world_scale` | 128 | 16..1024 | Now decided rather than a placeholder (doc section 7) |

### Tests (host harness, before the first wear session)

- The table matches the JSON: part and triangle counts, and every slot is known.
- Each of the 8 models matches a shape.
- After clipping, no hull triangle has a vertex inside the cut box.
- Eye clearance is at least 0.33 m.
- The eye rays to the face grids hit nothing, as in the mockup's checks.
- A face replay produces the expected texels for a fixed HUD state.
- M_c per bank mode: the eye is a fixed point, and the roll is 0, full or full as the table says.
- The flat build is unchanged.

Then a wear session (lesson 1): readability, the keystone, the bank modes, and comfort with the hull this close.

## Open questions (asset set)

1. Approve the asset set as shown on the sheet, or call out a component to change.
2. Weapon readout: the keystone on the dash centre, or project it onto the windscreen top centre (C3's reflector idea, where the game's HUD has it)?
3. The target reticle and WRONG WAY: draw the reticle at the target's depth (standard section 4), and put WRONG WAY on a pod or the windscreen?
4. The left pod has room to spare between the position and the lap time. Previous lap times (the game shows them under the current one) or the lives stars in championship mode could go there.
5. Trim: keep the rule's output as is? In particular, anasta comes out blue-grey with orange.
6. Should the settings page offer `cockpit_glass` and the eye offsets, or only `cockpit_bank`?

## Round 1 variations (C1-C3, kept for comparison)

All three are complete cockpits on the same cabin, bulkhead and hoop. They differ in frame density, dash layout and where the instruments sit. The ship geometry pushed one change into all three: the nose ridge runs straight down the middle of the forward view, from about 15° below the horizon (the tip) to 37° (the deck near the cabin). That's your aiming line, so no dash sits between 15° and 30° on the centre line.

### C1: slim frame, centre cluster

A teardrop canopy with 2.4 cm silver struts. The sill starts 0.85 m ahead on the deck. A-pillars rake back to a header at (0, +0.345, +0.08), and top rails run back to the hoop.

The instruments sit on one binnacle on the centre line, under the nose line: a 0.40 x 0.19 m face, 6 cm deep, centred at (0, -0.40, +0.48). That's 0.62 m from the eye and 40° below the horizon. It has a short hood and stands on a 0.14 m pedestal from the deck. On the face, from top to bottom:

- the speedo, 1.25 mm/px (165 x 33 mm);
- LAP, the weapon icon and POSITION, at 1.15 mm/px;
- the lap time and the shield, at 1.05 mm/px.

92% of the nose ridge stays visible past it.

### C2: fighter frame, split dash

A heavy gunmetal frame: 5.5 x 4.5 cm struts and more upright A-pillars. The header is lower, at (0, +0.215, +0.38), and a second canopy bow crowns at +0.385 just behind the eye. A 5 cm centre spine runs from the header over the head to the hoop.

The dash is a full-width body (r ±0.38, top 0.455 below the eye in the middle) carrying two pods, 0.26 x 0.15 m each. They're centred at (±0.235, -0.335, +0.47): 0.62 m away, 33° down and 27° to each side. The centre between them stays open, so 100% of the nose is visible.

- Left pod: LAP and POSITION, with the lap time under them, at 1.12 mm/px.
- Right pod: the speedo (1.35 mm/px) over the shield.
- The weapon icon hangs from the header on a short stalk: 58 mm square at (0, +0.125, +0.39), 0.41 m away and 18° up. That's where the game puts it, top centre.

This one is the most enclosed.

### C3: minimal frame, reflector glass

Almost frameless: 1.8 cm sill rails, the bulkhead and the hoop, under a bubble crowning at +0.30. The readouts are lit onto the lower windscreen as a reflector HUD, in the zone where the glass sits about 0.68 m away.

The image is rectified for the eye, so it's modelled as a plate facing the eye: 0.50 x 0.20 m at (0, -0.255, +0.63), 22° down. It's drawn additively over a faint tinted zone.

- The speedo runs across the top at 1.45 mm/px.
- LAP and POSITION sit at the sides, with the weapon in the middle, at 1.3 mm/px.
- The lap time and the shield are at the bottom, at 1.2 mm/px.

A flat projector slot sits on the deck below, 50° down. The readouts float over the nose rather than hiding it.

## Checks (in the page, and `build/mockups/checks.json`)

The page recomputes these for the current ship, variation, eye and scale:

- Legibility. Smallest glyph height as seen from the eye, foreshortening included, against the 0.6° floor (standard section 3).
- Occlusion. Rays from the eye to a 5 x 4 grid on each instrument face, tested against the hull and every non-glass part.
- Poke-through. Hull and part edges crossing an instrument face (lesson 2: nothing nearer may intrude on an instrument).
- Eye clearance. The nearest point on the hull and on each part group.
- Nose ridge. The share of the centre-line ridge, from 0.9 m ahead to the tip, that isn't hidden by cockpit parts.

All the variations, including H, pass on all four hull shapes (ships 0, 1, 3 and 5), looking forward at 128 u/m:

| | Smallest text | Blocked | Poke-through | Nose ridge visible | Nearest frame | Nearest hull |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | 0.74° | 0 of 120 rays | 0 | 88–92% | 0.34 m (header) | 0.54–0.59 m |
| C2 | 0.82° | 0 of 120 | 0 | 100% | 0.34 m (spine) | 0.54–0.59 m |
| C3 | 0.78° | 0 of 120 | 0 | 100% | 0.34 m (bulkhead) | 0.54–0.59 m |
| H | 0.78° | 0 of 120 | 0 | 96–100% | 0.34 m (header) | 0.54–0.59 m |

At the Frame's roughly 16 px per degree, 0.74° is about 12 px. Every overhead and rear part is at least 0.34 m from the eye. The hull is at least 0.54 m away, which beats the 0.35 m the Star Fox rig test asks for.

## The mockup page

Open `build/mockups/cockpit-mockup.html`. Drag to look (head yaw and pitch). The view is a mono stand-in for the Frame's per-eye frustum: ±0.9565 rad horizontal (the mean of L 1.028 and R 0.885), and the real vertical split, U 0.864 and D 1.048. That's about 110 x 110°, letterboxed.

The panel has:

- the ship (all 8), with its sampled trim swatches, and the variation (H by default; C1–C3 for comparison);
- links to the asset sheet and the hybrid screenshots;
- cockpit or outside view (cabin in ship, with the reference pilot as an x-ray), plus look presets;
- eye fore/aft and height;
- `world_scale` (64–256), with the implied ship size, eye height, track width and speed;
- the bank mode;
- play, speed and lap position;
- a toggle for every component and instrument;
- the live checks.

The ship drives the real track01 loop. Its bank follows yaw rate, the way the game's `angle.z` follows `angular_velocity.y`, up to 35°.

There are three bank modes:

- Level in banks (default): the view and cockpit take yaw and pitch only.
- Bank-follow (opt-in): the view and cockpit roll with the ship, camber included.
- Cockpit banks, head level: an extra option for comparison. The cockpit rolls around a level head, so the horizon stays put.

URL parameters reproduce any shot, for example `?var=C2&ship=1&look=back&at=0.6&bank=follow&ws=128&hide=glass,body`. Add `shot=1` for a static frame without the panel.

## Round 1 questions (answered 2026-10-04)

Variation: the hybrid H. world_scale: 128. Eye: unchanged. Shield slot: the pickup timer, kept generic. The C2 weapon at 0.41 m: replaced (see cockpit H). Bank modes: all three as a setting, level by default. Team colours: sampled from the ship textures. The reticle and WRONG WAY are still open (below).

Not done in the mockup: stereo, the real renderer and the headset. Readability and comfort still need a wear session after the build (lesson 1).
