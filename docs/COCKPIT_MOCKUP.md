# Cockpit H (full canopy): mockup, assets and build plan

Status (2026-10-04): built, host-verified, not yet worn. The user decided `world_scale` 88 and approved the asset set. Cockpit H is in the game behind `WIPEOUT_VR`: the hull match and cut, per-ship trim, the draw in stereo internal race frames, the instrument faces, the three bank modes and the settings keys. The in-headset settings page does not exist yet. "Implementation (built)" below is the current state; the rest of this file is the design record. The UI working agreement was mockup with variations, then all the UI's assets for approval, then the build.

The idea: in the VR internal view you sit inside your own ship. The player's real ship model is drawn double-sided around the eye, with a cabin cut into it. Built geometry adds a canopy frame, a rear bulkhead and a roll hoop, plus a dash with diegetic instruments, and the floating HUD panel goes away. By default the cockpit follows ship yaw and pitch but stays level in banks.

Precedent is the Star Fox cockpit (`starfox-enhanced` 99712f5, 82fd741, ddee4ff, 1d68555) and lessons 2, 29, 30 and 32 in the playbook.

## Decisions (user, 2026-10-04)

| Topic | Decision |
| --- | --- |
| Design | A hybrid: C1's slim teardrop frame (thin struts, raked windscreen) with C2's dash (full-width shell, two angled pods at ±27°, the nose clear, a weapon readout). C1's centre binnacle and all of C3 are dropped |
| Weapon readout | It has to suit the slim frame (C1 has no heavy header), with the 0.41 m refocus in mind. My choice is below |
| world_scale | Stays 128 (superseded in round 3, below) |
| Eye | The game's own point, `ship_cockpit()` = (0, -128, 0), unchanged (superseded in round 3, below) |
| Shield slot | Keep the shield-pickup timer for now. The slot stays generic, since the user may change it |
| Banking | All three modes become a setting; level-in-banks is the default |
| Trim | Sampled from each ship's own textures (all 8), not hand-picked team colours |

## Decisions and feedback (user, 2026-10-04, round 3)

| Topic | Decision |
| --- | --- |
| Scale | The 1.8 m reference pilot looked too big for the cockpit under him. Make the cockpit and the camera smaller against the ship, so the pilot fits the hull convincingly: a lower `world_scale`, the cockpit parts re-fitted in metres, and the eye re-derived from a seated pilot. This supersedes "keep the eye height". Show three candidates; the user picks |
| Left pod | Add the championship lives and keep the lap time and lap record. Add the previous lap times if they fit legibly |
| Settings page | The bank mode and the eye tweaks (`cockpit_eye_forward_cm`, `cockpit_eye_up_cm`) go on the in-headset settings page. `cockpit_glass` stays config-only |
| Weapon | It stays on the dash-centre keystone |
| Trim | anasta's trim stays as the rule produces it, for now |

## Final decisions (user, 2026-10-04)

| Topic | Decision |
| --- | --- |
| world_scale | **88**, not the 100 recommended below. It is the default everywhere, the chase view included. The geometry is refitted at 88 (eye y -85.7, cut box x ±33.9, z -31.7 to +54.6, y < -5.6) |
| Eye | The fitted eye (`VR_COCKPIT_EYE_Y`) is for the VR cockpit view only. The game's own camera position stays `ship_cockpit()` for the sim and the sound |
| Assets | Approved: the C1 slim frame, the C2 split dash with the cabin opened over the legs, the weapon keystone on the dash centre, the left pod (lap, position, lives stars, two previous laps, lap time, lap record), the right pod (speedo plus a generic status slot showing the shield timer). Trim is sampled per ship from its own textures at load |
| Settings | `cockpit` (on/off), `cockpit_bank` (`level` default, `follow`, `cockpit`), `cockpit_eye_forward_cm` and `cockpit_eye_up_cm` are user-facing, for the settings page. `cockpit_glass` is config-only. The page itself is designed separately |

## Files

| Path | What |
| --- | --- |
| `tools/mockup/export_mockup.c` | Exporter. It links the game's own `object.c`, `image.c`, `track.c` and `ui.c` against a capture stub for `render.h`. The stub keeps texture pixels and records the triangles `object_draw()` pushes, in model space |
| `tools/mockup/build.sh` | Builds and runs the exporter, then the generator. `tools/mockup/build.sh <data_root>`, where the root holds `wipeout/...` |
| `tools/mockup/build_cockpit_mockup.py` | Writes `build/mockups/cockpit-mockup.html`: one file with three.js r158 (fetched once, SHA-256 pinned, cached in `build/mockup-cache/`) and the data inlined as base64. Opens from `file://` |
| `tools/mockup/cockpit_mockup.js` | The mockup itself: cabin, cockpit H plus C1-C3 for comparison, instruments and pod faces, trim sampling, checks, track animation, UI. With `?sheet=assets` it draws the asset sheet; with `?export=geometry` it exports the parts |
| `tools/mockup/shoot_cockpit_mockup.py` | Headless Chrome: the hybrid shots, `checks.json`, the hybrid sheet and the asset sheet. With `--variations`, the C1-C3 set too |
| `tools/mockup/export_cockpit_geometry.py` | Writes `tools/mockup/cockpit_geometry.json` from the page's own builders. With `--c OUT`, it also writes the C table |
| `tools/mockup/check_cockpit_geometry.py` | Checks the committed C table is what the committed JSON generates, and that neither holds anything but authored geometry. A CTest |
| `tools/mockup/compare_cockpit_render.py` | Compares a harness render of the real cockpit with the page's shot of the same ship (`bg=black` hides track and sky) and writes a build / mockup / difference montage |
| `tools/mockup/cockpit_geometry.json` | Committed. Cockpit H's geometry for the build: parts per hull shape in game units, material slots, faces, the cut box and the trim rule. It holds no game data |

Everything generated lands in `build/` (git-ignored) and is derived from game data, so none of it gets committed:

- `build/mockup-data/`: the exported geometry and textures.
- `build/mockups/`: `cockpit-mockup.html`, `shots/`, `checks.json`, `cockpit-hybrid-sheet.png`, `cockpit-assets.png` and, from round 1, `cockpit-contact-sheet.png`.
- `build/cockpit/vr_cockpit_geometry.inc`: the C table, which is no game data, but generated.

The exporter takes all 8 player ships, track01 (Terramax: track, scenery and sky, through `track_load` and `objects_load`), `speedo.tim`, `wicons.cmp` and the `drfonts.cmp` font. The glyph table comes from drawing each glyph through `ui_draw_text`. The speedo bar table is parsed from `src/wipeout/hud.c` at build time, so the bars match the game. Exporting the track turned out cheap, so the mockup drives the real track01 loop instead of a stylised one.

## Coordinates and the eye

Game space is +X right, +Y down, +Z forward, in game units. The game's internal camera sits at `ship_cockpit()` = (0, -128, 0) in ship space, 128 units above the ship origin. The nose point is (0, 0, 512); the meshes reach z = ±416 to ±425.

The cockpit is authored in metres for a real pilot, relative to the eye: r right, u up, f forward. Since round 3 it is built at the chosen `world_scale` W (units per metre) with an eye height E (units above the ship origin). A point (r, u, f) is game (W r, -E - W u, W f).

E is fitted from a seated pilot:
- The tub floor is 0.92 m below the eye (a 0.14 m cushion plus 0.78 m seated eye height).
- The floor has to clear the bottom of every hull shape by at least 2 cm, over the seat and leg footprint (r -0.22 to +0.22, f -0.30 to +0.50).
- That gives one eye point for all 8 ships: E = (highest hull bottom in that footprint) + 0.94 W.

The round-2 reference is W = 128 with the game's own E = 128.

Inside the headset the cockpit is part of the ship, so once built it scales with the world about the eye (lesson 29). Changing `world_scale` at runtime then changes no angle, only how big everything is against your body. Choosing W here is different: it decides how big the pilot is against the ship. A smaller W puts a smaller pilot (in game units) in the same hull, sitting deeper.

Eye adjust (fore/aft and height, ±30 cm) is physical, so it maps to `cm / 100 * world_scale` units.

## Ship dimensions

Measured from the captured triangles. The last two columns are for the game's own eye (round 2); the fitted eyes per `world_scale` are in the next section. The 8 ships are 4 hull shapes, one per team. Every polygon in `allsh.prm` is flagged single-sided, which is why the cockpit draws the hull double-sided (back faces dimmed, so the inside reads as an interior). The deck is the hull top on the centre line at the eye station (z = 0).

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

## world_scale: three options (round 3), 100 recommended

`build/mockups/cockpit-scale-options.png` compares the round-2 reference (128 with the game's eye) against three fitted options. For each it shows the side view with the 1.8 m pilot (an x-ray, with the ship framed the same at every scale), the forward cockpit view and the look-back view, on Feisar and on AG Systems. AG Systems and Auricom leave the least room above the deck (the tightest hulls). In the page, the scale buttons switch between them.

The hulls justify 112 to 88. At every scale the deepest seat is set by the same thing: the hull bottom under the seat at y = -3 (Feisar, AG Systems, Auricom). The eye then sits this far above the deck ridge on the tightest hull:

| world_scale | Eye above the deck ridge |
| --- | --- |
| 112 | 0.34 m |
| 100 | 0.27 m |
| 88 | 0.18 m |
| 80 | 0.10 m |

At 80 the head is in the hull and the deck is seen edge-on, so the span stops at 88.

Checks over all four hulls, looking forward. Eye above the deck is per hull (Feisar, Qirex, AG Systems, Auricom); the floor is the tub floor's game y; the clearances are the nearest part and the nearest hull, from the eye:

| world_scale | Ship length | Eye (game units) | Eye above deck ridge | Floor y | Smallest text | Blocked rays | Poke-through | Nose visible | Nearest part | Nearest hull |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 128 (game eye) | 6.64 m | (0, -128, 0) | 0.47, 0.51, 0.46, 0.45 m | -10.2 | 0.78° | 0 | 0 | 96% | 0.34 m | 0.55 m |
| 112 | 7.59 m | (0, -108.3, 0) | 0.36, 0.40, 0.34, 0.34 m | -5.3 | 0.78° | 0 | 0 | 100% | 0.34 m | 0.47 m |
| 100 | 8.50 m | (0, -97.0, 0) | 0.29, 0.34, 0.27, 0.27 m | -5.0 | 0.78° | 0 | 0 | 100% | 0.34 m | 0.42 m |
| 88 | 9.66 m | (0, -85.7, 0) | 0.20, 0.26, 0.18, 0.18 m | -4.7 | 0.78° | 0 | 0 | 100% | 0.34 m | 0.38 m |

The text sizes and the nearest-part distances don't move, because the cockpit is in metres and the dash sits in the cabin opening (see cockpit H). The hull only comes closer. The nearest frame part is the canopy header, 0.34 m from the eye, at every option.

| world_scale | Ship | Track width | Lap-record pace | IPD in game units (64 mm) |
| --- | --- | --- | --- | --- |
| 112 | 7.6 x 3.2 x 1.5 m | 23–31 m | 330 km/h | 7.2 |
| 100 | 8.5 x 3.6 x 1.6 m | 26–35 m | 370 km/h | 6.4 |
| 88 | 9.7 x 4.1 x 1.9 m | 30–40 m | 420 km/h | 5.6 |
| 128 (round 2) | 6.6 x 2.8 x 1.3 m | 20–27 m | 290 km/h | 8.2 |

The lap-record pace is Terramax Venom: 57.5 s over a 586,000-unit centre line.

My recommendation is 100. The pilot sits properly inside the hull: hips at the hull bottom, the eye 0.27–0.34 m above the deck, head and shoulders under a canopy that's now low on the ship, in the side view. Forward, the nose is still a clear aiming line ahead of the dash; at 88 it flattens into a thin strip seen nearly edge-on, and the dash and deck fill more of the lower view. 112 is a modest change from round 2: the pilot still sits fairly high (0.34–0.40 m above the deck). At 100 the ship comes to 8.5 m, the size of a big single-seat racer, and the track is wide for 8 of them abreast.

Stereo: the IPD is physical (the runtime's eye poses), so in game units it shrinks with `world_scale`. At 100 the eyes are 6.4 units apart instead of 8.2, and the world reads 1.28 times bigger in stereo than at 128 (1.45 times at 88). Head movement covers fewer game units too. That's the intent (a smaller pilot in a bigger world), but it also makes the speed feel higher (370 km/h at the lap-record pace). That needs judging in the headset.

## Components (shared)

Dimensions are in metres, eye-relative. Units are game units at the recommended `world_scale` 100 (metres x 100), with the eye at (0, -97, 0).

| Component | Size and placement | Units at 100 |
| --- | --- | --- |
| Cabin cut-out | A box from the floor up. Hull polygons are clipped against it, keeping the parts outside it (uv and colour interpolated). Round 1 (C1–C3): r ±0.34, f -0.36 to +0.25, behind the seat to just ahead of the chest. H (round 3): r ±0.38, f -0.36 to +0.62, opened forward over the legs, so the dash sits in the opening | H: x ±38.5, z -36 to +62, y < -6 |
| Tub | Four walls from the floor up to the hull surface, ray-cast per ship, so the tub never shows outside the hull. Floor 0.02 m thick, 0.92 m below the eye, at least 2 cm above every hull's bottom (that's how the eye is fitted) | floor y = -5 |
| Coaming | A rim along the cut edge, on the hull surface, with a trim stripe (`trim_primary`) on the sides: 3.5 x 2.5 / 6 x 4 / 3 x 2 cm (C1/C2/C3) | 4.5 x 3.2 / 7.7 x 5.1 / 3.8 x 2.6 |
| Seat | Cushion 0.42 wide, 0.46 deep, 0.14 tall. Back reclined to 0.30 below the eye | |
| Rear bulkhead | 0.05 thick at f -0.40, 0.80–0.84 wide, from the floor to the eye height minus 0.06 / 0.00 / 0.10 (C1/C2/C3). A headrest tower (0.26 wide) rises to 0.10 above the eye and carries a 0.20 x 0.22 pad. Trim cap (`trim_primary`). Clearance from the eye is 0.34 m | z = -51 to -58 |
| Roll hoop | At f -0.42: posts from the bulkhead's top corners (r ±0.37) to the canopy rail tips (r ±0.27, u +0.30), with a crossbar between the tips. 3.0 / 5.5 / 3.2 cm square. This is the Star Fox 1d68555 load path, so the canopy frame visibly lands on the cabin | z = -54 |
| Canopy sill | On the hull deck, ray-cast per ship. Round 1: half-width 0.41 at the head, narrowing toward the front. H: 0.43 (5 cm outside the opening) back from the front of the opening, narrowing to 0.27 at the windscreen's foot, 0.85 m ahead | |
| Glass | Faint tint (4.5% plus a fresnel edge) inside, stronger in the outside view. Never an occluder in the checks | |

Instruments are the game's HUD content, drawn with the game's own font, `speedo.tim`, the speedo bar table and `wicons`. They're laid out in HUD pixels (hud.c's units before `ui_scale`), with a size per variation in mm per HUD pixel:

- Speed and thrust: the speedo facia with the bars from `hud_draw_speedo`, thrust in red under speed, 132 x 26 px.
- Shield: "SHIELD" plus an 8-segment bar. WipEout (1995) has no hull energy; the only shield state is the shield pickup's 6.7 s timer (`SHIP_SHIELDED`, `WEAPON_SHIELD_DURATION`), so the bar shows that timer.
- Lap: "LAP n OF 3".
- Position: "POSITION n".
- Lap time: the current lap time in the 16 px font, then "LAP RECORD" and the record in the 8 px font.
- Previous laps (H, round 3): up to two earlier lap times in the 8 px font, accent colour, 10 px apart, as `hud_draw` shows them above the current time.
- Lives (H, round 3): one `UI_ICON_STAR` (`drfonts.cmp` #9, 12 x 12 px) per life, 13 px apart, as `hud_draw` shows them in championship races (`NUM_LIVES` 3).
- Weapon: the current `wicons` icon in a frame.

## Cockpit H: the approved design

H is C1's frame on C2's dash. The canopy is C1's slim teardrop:
- 2.4 cm silver struts;
- a raked windscreen whose sill starts 0.85 m up the deck;
- A-pillars to a header at (0, +0.345, +0.08), so the header sits above and slightly ahead of your head rather than in front of you;
- top rails back to the roll hoop, which lands on the bulkhead.

Under it sits C2's dash: a full-width shell carrying two angled pods. The shell spans the cabin opening, r ±0.38, 0.455 m below the eye in the middle, with its underside 0.52 m below the eye, above the knees.

In round 3, H's cabin opening runs forward over the legs to f +0.62 and widens to ±0.38, and the dash sits in it. That keeps the instruments and the nose clear at every fitted scale. At 100 the deck is only 0.27–0.34 m under the eye, and the round-2 dash, which stood on the deck, would have been inside the hull. The canopy sill moves out to 5 cm beyond the opening.

The pods are 0.28 x 0.16 m, slightly bigger than C2's, so each face can be one texture. They're centred at (±0.235, -0.335, +0.47): 0.62 m from the eye, 33° down and about 27° to each side. Each has a glareshield lip with a `trim_secondary` edge.

- Left pod: LAP n OF 3 and POSITION across the top. The championship lives sit under POSITION. The two previous lap times sit above the current lap time and the lap record, as the game stacks them. All of it fits at the 8 px floor; nothing had to be dropped.
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
| Left pod | Lives (championship) | (412, 92) | x2 | `UI_ICON_STAR` per life, from `drfonts.cmp` |
| Left pod | Previous laps | (65, 154) | x2 | up to 2 times in `UI_SIZE_8` accent |
| Left pod | Lap time | (61, 202) | x2 | `UI_SIZE_16`, then LAP RECORD and the record in `UI_SIZE_8` |
| Right pod | Speedo | (58, 30) | x3 | `hud_draw_speedo`'s bars and the `speedo.tim` facia |
| Right pod | Status slot | (160, 190) | x3 | "SHIELD" in `UI_SIZE_8` plus 8 segments, from the shield timer |
| Weapon | Weapon icon | (10, 10) | x3 | the `wicons.cmp` icon (32 px) in a frame |

The smallest text, 8 px at x2 (the labels, the previous laps and the record), is 0.78° from the eye; the shield label at x3 is 1.21°. The stars are 12 px at x2: 13 mm, or 1.2°.

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

`tools/mockup/cockpit_geometry.json` (format `wipeout-vr-cockpit-geometry/1`, about 230 kB) is what the build consumes. It's exported at `world_scale` 88 (the user's decision): 90 parts, about 1,424 triangles per hull and 4,640 over the four hulls. It holds:

- Ship space in game units with the game's own axes (+y down, +z forward).
- `world_scale` (88) and the fitted eye (0, -85.7, 0), which replaces `ship_cockpit()` in the cockpit view, plus the fit rule.
- The cabin cut box: x ±33.9, y < -5.6 (open top), z -31.7 to +54.6 (at 88; the metres are those of the table below). The build clips the player's hull against it at load.
- The hull shapes, with the model names and captured triangle counts used to match a ship to its shape: Feisar sophia/jacko, Qirex solaar/arian, AG Systems chang/Dekka, Auricom arial/anasta. With no match, the build draws no cockpit.
- Material slots: `frame`, `frame_dark`, `cabin`, `cabin_dark`, `seat`, `bezel`, `glass`, `trim_primary` and `trim_secondary`. The fixed slots carry an authored default; the trim slots are sampled.
- The trim rule as parameters and steps. No sampled colour is stored.
- The instrument catalogue in HUD pixels.
- The three faces: corners in ship space, texture size, texel size, and elements with texel position and `ui_scale`.
- The parts, one per line: name, component group (the mockup's toggles), slot, `hulls` (`"all"` or one hull id), and triangle vertices.

The parts are authored geometry only. No game mesh, texture or pixel is in the file. The parts that follow the deck (tub wall tops, sill rails, the A-pillar feet, the glass's lower edge) were fitted to each hull by ray casting in the mockup. So those vertices imply a few dozen deck heights per hull, but nothing of the hull itself.

The part sizes depend on the chosen `world_scale`. The cockpit is sized in metres for a real pilot, so in game units it's built for one scale, and the eye and the deck-following parts are fitted at that scale. When the user picks a scale, the data is regenerated for it at dev time, on a machine with the user's game data and the mockup page built:

```
tools/mockup/build.sh <data_root>                                     # page from the user's data
python3 tools/mockup/export_cockpit_geometry.py --world-scale 112     # refit, rewrites cockpit_geometry.json
python3 tools/mockup/export_cockpit_geometry.py --from-json --c src/vr/vr_cockpit_geometry.inc
```

Both outputs get committed, and neither holds game data (`check_cockpit_geometry.py` checks it). In the headset, `world_scale` stays a setting. Moving it away from the fitted value scales the whole cockpit with the ship and the world (it's in ship units), so the pilot-to-hull fit only holds at the fitted value. That's why the setting's default is the chosen scale, 88.

Recommendation: keep the JSON as the committed, reviewable source, and compile it into C tables rather than parsing JSON at runtime. That's the Star Fox pattern (`generate_cockpit_assets.py` writing `cockpit_assets.inc`).

`export_cockpit_geometry.py --from-json --c src/vr/vr_cockpit_geometry.inc` writes the table: slot enum and default colours, hull model table, part table (`group`, `slot`, `hull_mask`, triangle range), faces, face elements and one flat vertex array. It writes `src/vr/vr_cockpit_geometry.inc`, which is committed (330 kB of C, clean with `-Wall`, with `VR_COCKPIT_WORLD_SCALE` and `VR_COCKPIT_EYE_Y` defines). The defines and the slot enum are always visible; the tables only where `VR_COCKPIT_GEOMETRY_DATA` is defined first (`vr_cockpit_core.c`), so the vertices live in one translation unit. No JSON parser, no file IO, no new dependency. The vertex array is 4,660 triangles x 36 bytes, about 170 kB.

## Implementation (built)

Everything lives behind `WIPEOUT_VR`, with the flat build unchanged (`tools/verify_flat_build_unchanged.sh`). The pure part is `src/vr/vr_cockpit_core.{h,c}` (in `wipeout_vr_core`: the table, the cut, the trim rule, the parts, the eye and the bank maths; no game headers) with the generated `src/vr/vr_cockpit_geometry.inc`. The game-facing part is `src/vr/vr_cockpit.{h,c}`. The game files get fenced hooks only: `ship.c` (two), `race.c` (two), `hud.c` (three functions), `weapon.c` (one accessor), `render.h`.

### When the cockpit draws

`vr_cockpit_begin(&g.camera)` runs in `race.c` right after `render_vr_race_view()`, before `render_set_view()`. The cockpit is the view when all of these hold:

- the frame is stereo (`render_vr_stereo()`; the attract mode's cutting cameras and `force_mono` are mono);
- `cockpit` is on;
- the pilot's ship has `SHIP_VIEW_INTERNAL`, is not in rescue, and its hull matched a shape;
- the camera is `camera_update_race_internal`, or `camera_update_attract_internal`, which only reaches a stereo frame under `vr.diag_attract_stereo`.

Otherwise nothing changes: the chase view, the intro fly-over, the mono panel and `cockpit = off` keep the floating HUD panel. The harness checks each of these.

### Eye point

The head sits at the fitted eye through the ship's full matrix, `S * e'`, with `e' = (0, VR_COCKPIT_EYE_Y - up_cm / 100 * world_scale, forward_cm / 100 * world_scale)`. `render_vr_cockpit_anchor()` hands the renderer that point and the bank mode's angles; `vr_rec_view` uses them in place of the game camera for the eyes and the HUD camera (with `follow_vehicle_rotation` and `horizon_lock` out of the way, since the angles already carry the right roll). The game camera stays where `camera_update_race_internal` put it (the recorded VIEW still carries it, the sim and the sound use it); in the cockpit the head is 42.3 units (0.48 m) above it along the ship's up axis. The seat offsets are physical, so they apply live with no rebuild. The sprite facing and the distance fade follow the eyes as before.

### Load (`ships_load`, one fenced call)

`vr_cockpit_load(ship_textures, "wipeout/common/allsh.cmp")`, once at game init, for each of the 8 ships:

1. Matches the model to a hull shape by name and by the triangles `object_draw` pushes. No match: that ship keeps the flat internal view (logged).
2. Clips every triangle against the cut box (the five planes of the open-topped box, keeping the outside parts, uv and colour interpolated) and adds a reversed copy at 55% colour for the inside. The pieces keep their texture index and are pushed with `render_push_tris` like `object_draw` does. Engine sprites are not part of the cockpit hull.
3. Samples the trim from the ship's own textures. The pixels come from decoding `allsh.cmp` again, copied out of the game's temp memory (eight objects at most) and freed at the end of the load, because the atlas is not read back (lesson 10).
4. Pre-shades the authored parts: a flat colour per triangle from its slot and the mockup's light in ship space (ambient 0.55, the sun (0.35, 1, 0.45) in the page's axes, the trim slots +0.25, the normal turned toward the fitted eye). The parts are untextured and `RENDER_NO_TEXTURE` is a 128 grey that the shader's x2 makes 1.0, so the vertex colour is the colour on screen.

The trim rule in `vr_cockpit_core.c` is the page's, with one tie-break the page leaves to float rounding: a sample exactly on a texel boundary takes the lower texel, and a sample outside the texture has no texel and is skipped. Without it anasta's orange secondary flipped to violet on one sample (it is 1.35% saturated). Run on the real data it picks, against the page: sophia b39900 / 0000b3 (page the same), solaar 01b275 / 607fb2 (01b374 / 5f7fb3), jacko 0707b2 / b20532 (0707b2 / b20533), chang b38f02 / 321fb3 (b39001 / 3120b3), arian b22c02 / 621801 fallback (b32e04 / 621902), arial b20d0d / 620707 fallback (b30d0d / 620707), anasta 5d7db3 / b27405 (5d7eb3 / b27505), Dekka b38f01 / 00b23f (b38f01 / 00b23f). Every channel is within 3 levels, with the same hues and the same fallbacks.

### Draw (`ships_draw`, one fenced branch)

Where `ships_draw` skips the internal-view ship it calls `vr_cockpit_draw(ship)` when the cockpit is active. In order, all through the game's own `render_*` calls:

1. `render_set_model_mat(M_c)`, then the hull (outside copy) and its inside copy with backface culling on, with the ship's textures.
2. Culling off, the authored parts (about 1,364 triangles per hull).
3. The three face quads, through `render_vr_face_quad`: a `BIND_FACE` command, two triangles, a `BIND_FACE` back to the atlas.
4. Depth write off, the glass (60 triangles, alpha 11/255 = 0.043), depth write on. The fresnel edge of the page's glass is not built (the game's shader has no place for it). `cockpit_glass` off skips it.
5. Culling back on.

About 2,400 triangles in the recorded list for a ship with a 300-triangle hull. They replay per eye with no per-eye game work.

### Instrument faces (the game's own 2D UI into small textures)

- The renderer owns the textures (512 x 292, 512 x 292, 128 x 128, created at the first `FACE_BEGIN` that names them, RGBA8, trilinear with mipmaps regenerated once a frame, anisotropy 8 where the driver offers it).
- `race.c` calls `vr_cockpit_hud_draw(ship)` in place of `hud_draw()` when the cockpit is active. For each face it brackets the draws with `render_vr_face_begin(face, w, h, clear)` / `render_vr_face_end()`. Between them it calls `ui_set_scale(n)` and the game's own `ui_draw_text`, `ui_draw_number`, `ui_draw_time`, `ui_draw_icon`, `render_push_2d`, and the new `hud_draw_speedo_at(pos, speed, thrust)` and `hud_weapon_icon(type)` (the speedo bars and the icon list are static in `hud.c`), at the texel positions and `ui_scale` the geometry table gives. It restores the game's ui scale afterwards.
- The recorder tags the 2D commands between begin and end `VR_CMD_FLAG_FACE`. The replay runs one face pass before the eye passes: each `FACE_BEGIN` binds that face's texture to the shared FBO, sets an orthographic projection over its texels, clears it to the face colour and draws only the face-flagged triangles. The eye and HUD passes skip them. The face quad then samples the texture in both eyes. So the faces replay once and are sampled twice, and the game's 2D code (and what it reads: the shield timer, the lap time) runs once a frame. The harness counts it.
- The faces are drawn whenever the cockpit is active, racing or not. Previous laps show for laps already finished (two at most), the lives stars only in championship races, the position only outside time trials.
- The status slot is `vr_cockpit_status_t status_slots[]` with one entry, `shield` (`SHIP_SHIELDED` and `weapon_shield_time_left()` over `WEAPON_SHIELD_DURATION`, in 8 segments), so a later change is one more entry.
- What stays on the HUD quad in the cockpit: WRONG WAY and the target reticle (`hud_draw_overlay`), the pause and other menus. The quad is otherwise empty. Open question 4 still stands: they need a home in the cockpit.

### Bank modes (`vr_cockpit_pose`)

`cockpit_bank` replaces the anchor rule while the cockpit is active. With `S` the ship's matrix (`ship->position`, `ship->angle`), `e'` the seat, `E = S * e'`:

| Mode | Anchor angles | Cockpit matrix M_c |
| --- | --- | --- |
| `level` | the ship's pitch and yaw, roll 0 | `T(E) * R(yaw, pitch, 0) * T(-e')`: the cockpit turns about the eye and never rolls |
| `follow` | the ship's pitch, yaw and roll | `S`: the view and the cockpit roll together |
| `cockpit` | pitch and yaw, roll 0 | `S`: the cockpit rolls around a level head |

The eye is a fixed point of M_c in every mode, and the head is at `E` in every mode (so with a roll it moves sideways with the ship, as the game's own camera does). In `level` and `follow` the anchor's view rotation times the cockpit's is the game's `diag(1, -1, -1)`: the cockpit does not move against the head. All of that is a unit test against the game's own matrices. `follow_vehicle_rotation` keeps governing the chase view only. `follow` still needs a check in the headset (lesson 22: steering in the ship's frame is analogue, so no 8-way re-projection).

### Settings keys (port keys, `vr_settings.c`)

| Key | Default | Range | Settings page | Meaning |
| --- | --- | --- | --- | --- |
| `cockpit_bank` | `level` | `level`, `follow`, `cockpit` | **page-bound** (user, 2026-10-04) | Bank mode (above) |
| `cockpit_eye_forward_cm` | 0 | -30..30 | **page-bound** | Physical seat offset from the fitted eye, `cm / 100 * world_scale` units, applied live |
| `cockpit_eye_up_cm` | 0 | -30..30 | **page-bound** | Same, vertical |
| `cockpit` | true | bool | **page-bound** | The internal view becomes the cockpit; off keeps the flat internal view plus the HUD panel |
| `cockpit_glass` | true | bool | no, config only | Canopy glass tint on or off |
| `world_scale` | **88** | 16..1024 | as today | The cockpit is fitted at this value (`VR_COCKPIT_WORLD_SCALE`) |

All five have defaults, clamping, `WIPEOUT_VR_<KEY>` overrides and a live re-read: the renderer reads them from `vr.settings` at every frame (`vr_render_config_t.live_settings`), so a settings page that calls `vr_settings_set()` takes effect on the next frame and nothing is rebuilt. The page (not built; designed separately) is to have a Cockpit group with Bank (Level / Follow / Cockpit banks), Eye forward and Eye up (1 cm steps, previewed live behind the panel) and a "Reset eye" action that sets both offsets to 0.

### Tests

| Level | Where | What |
| --- | --- | --- |
| unit | `tests/vr/test_vr_cockpit.c` (`vr_cockpit`) | The table (90 parts, 4,640 triangles, contiguous ranges, known slots, hull masks, the three faces as planar rectangles of the planned size, every element's place); the hull match of the 8 models and its negatives; the cut on 3,000 random triangles (no vertex inside the box, the area adds up, uv and colour interpolated, winding kept) and the inside copy; the trim rule on synthetic textures (two hues, area weights, one hue, dark paint, grey and white, transparent texels, 1% and 0.4% saturation, untextured polygons, texel boundaries, the red wrap); the light; the parts; the eye in cm at two world scales; the three bank modes over six ship poses and three seats against `src/types.c` (head at the seat, the eye fixed, the roll, the view rotation); the eye through `vr_cam_from_pose`; eye clearance of the authored parts (at least 0.33 m on all four hulls) and the eye rays to a 5 x 4 grid on every face hitting no part |
| unit | `tests/vr/test_vr_settings.c`, `test_vr_stage.c` | The cockpit keys: defaults, enum names, clamping, env override, live re-read; 88 as the world_scale default |
| table | `tools/mockup/check_cockpit_geometry.py` (`vr_cockpit_geometry`) | The C table is what the JSON generates; no game data in either |
| harness | `tests/vr/vr_render_harness.c` with `vr_cockpit_harness.inc` | With synthetic hulls (the model names and triangle counts of two real ships), through the real renderer: the cockpit draws only in a stereo internal race view (chase, mono, attract, rescue, unmatched hull, `cockpit` off, `force_mono` each fail it); the eyes at the seat for each bank mode and the seat offsets, live; a world marker lands where the recorded cameras say in both eyes; the faces replay once a frame (the status slot is asked once per game frame while two eyes and the HUD replay; a half-alpha line in the face is single-blended); the shield bar and the pods in the eye image |
| harness, with data | the same, `WIPEOUT_VR_TEST_DATA=<root holding wipeout/...>` | All 8 real ships: valid, the right hull shape, no hull vertex in the cabin box, the tub floor at least 2 cm above the hull's bottom over the seat and leg footprint, the trim; then two real cockpits are rendered at the mockup's field of view |

Verified against the approved mockup (`compare_cockpit_render.py`, ships 0 and 1, the cockpit alone on black at the mockup's field of view): 98.7% and 97.7% of the pixels either draws are drawn by both, the mean colour difference is 10 levels and 95% of the pixels are within 32 levels. The faces differ in what they show.

Then a wear session (lesson 1): readability, the keystone, the bank modes, and comfort with the hull this close.

## Open questions

1. Answered (2026-10-04): `world_scale` 88, the asset set approved, `cockpit` on the settings page, `cockpit_glass` config only.
2. The target reticle and WRONG WAY still need a home in the cockpit: the reticle at the target's depth (standard section 4), and WRONG WAY on a pod or the windscreen. They stay on the otherwise empty HUD quad for now.
3. To judge in the headset: the stereo size of the world and the sense of speed at 88 (420 km/h at the lap-record pace), whether `follow` steers well banked, and the glass without the page's fresnel edge.
4. The settings page itself: the Cockpit group is specified above; it needs its own design.

## Round 1 variations (C1-C3, kept for comparison)

These are round-1 designs, made for `world_scale` 128 with the game's eye and the shorter cabin opening. Look at them in the page with the "128, game eye" scale button; at the fitted scales their dashes sit too low for the deck. The screenshot script shoots them at that setup (`--variations`).

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

All the variations pass on all four hull shapes (ships 0, 1, 3 and 5), looking forward. C1–C3 are at 128 u/m with the game's eye; H is at the round-3 recommendation, 100 u/m with the fitted eye (the other scales are in the world_scale section):

| | Smallest text | Blocked | Poke-through | Nose ridge visible | Nearest frame | Nearest hull |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | 0.74° | 0 of 120 rays | 0 | 88–92% | 0.34 m (header) | 0.54–0.59 m |
| C2 | 0.82° | 0 of 120 | 0 | 100% | 0.34 m (spine) | 0.54–0.59 m |
| C3 | 0.78° | 0 of 120 | 0 | 100% | 0.34 m (bulkhead) | 0.54–0.59 m |
| H (100) | 0.78° | 0 of 160 | 0 | 100% | 0.34 m (header) | 0.42–0.47 m |

At the Frame's roughly 16 px per degree, 0.74° is about 12 px. Every overhead and rear part is at least 0.34 m from the eye. The hull is at least 0.54 m away, which beats the 0.35 m the Star Fox rig test asks for.

## The mockup page

Open `build/mockups/cockpit-mockup.html`. Drag to look (head yaw and pitch). The view is a mono stand-in for the Frame's per-eye frustum: ±0.9565 rad horizontal (the mean of L 1.028 and R 0.885), and the real vertical split, U 0.864 and D 1.048. That's about 110 x 110°, letterboxed.

The panel has:

- the ship (all 8), with its sampled trim swatches, and the variation (H by default; C1–C3 for comparison);
- links to the asset sheet and the hybrid screenshots;
- cockpit or outside view (cabin in ship, with the reference pilot as an x-ray), plus look presets;
- eye fore/aft and height;
- the scale options (128 with the game's eye, 112, 100, 88) and a free `world_scale` slider (64–256), refitting the cockpit and the eye, with the implied ship size, eye point, track width and speed;
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
