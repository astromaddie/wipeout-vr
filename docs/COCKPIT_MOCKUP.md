# Cockpit C (full canopy): mockup and spec

This is the mockup stage only. Nothing here is built into the game yet, and nothing gets built until a variation is approved. The UI working agreement goes mockup with variations, then assets, then build.

The idea: in the VR internal view you sit inside your own ship. The player's real ship model is drawn double-sided around the eye, with a cabin cut into it. Built geometry adds a canopy frame, a rear bulkhead and a roll hoop, plus a dash with diegetic instruments, and the floating HUD panel goes away. By default the cockpit follows ship yaw and pitch but stays level in banks; bank-follow is opt-in.

Precedent is the Star Fox cockpit (`starfox-enhanced` 99712f5, 82fd741, ddee4ff, 1d68555) and lessons 2, 29, 30 and 32 in the playbook.

## Files

| Path | What |
| --- | --- |
| `tools/mockup/export_mockup.c` | Exporter. It links the game's own `object.c`, `image.c`, `track.c` and `ui.c` against a capture stub for `render.h`. The stub keeps texture pixels and records the triangles `object_draw()` pushes, in model space |
| `tools/mockup/build.sh` | Builds and runs the exporter, then the generator. `tools/mockup/build.sh <data_root>`, where the root holds `wipeout/...` |
| `tools/mockup/build_cockpit_mockup.py` | Writes `build/mockups/cockpit-mockup.html`: one file with three.js r158 (fetched once, SHA-256 pinned, cached in `build/mockup-cache/`) and the data inlined as base64. Opens from `file://` |
| `tools/mockup/cockpit_mockup.js` | The mockup itself: cabin, variations, instruments, checks, track animation, UI |
| `tools/mockup/shoot_cockpit_mockup.py` | Headless Chrome screenshots, `checks.json` and the contact sheet |

Everything generated lands in `build/` (git-ignored): `build/mockup-data/` (exported geometry and textures), `build/mockups/cockpit-mockup.html`, `build/mockups/shots/`, `build/mockups/checks.json` and `build/mockups/cockpit-contact-sheet.png`. All of it is derived from game data, so none of it gets committed.

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
| Coaming | A rim along the cut edge, on the hull surface, with a team-colour stripe on the sides: 3.5 x 2.5 / 6 x 4 / 3 x 2 cm (C1/C2/C3) | 4.5 x 3.2 / 7.7 x 5.1 / 3.8 x 2.6 |
| Seat | Cushion 0.42 wide, 0.46 deep, 0.14 tall. Back reclined to 0.30 below the eye | |
| Rear bulkhead | 0.05 thick at f -0.40, 0.80–0.84 wide, from the floor to the eye height minus 0.06 / 0.00 / 0.10 (C1/C2/C3). A headrest tower (0.26 wide) rises to 0.10 above the eye and carries a 0.20 x 0.22 pad. Team-colour cap. Clearance from the eye is 0.34 m | z = -51 to -58 |
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

## The variations

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

All three variations pass on all four hull shapes (ships 0, 1, 3 and 5), looking forward at 128 u/m:

| | Smallest text | Blocked | Poke-through | Nose ridge visible | Nearest frame | Nearest hull |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | 0.74° | 0 of 120 rays | 0 | 88–92% | 0.34 m (header) | 0.54–0.59 m |
| C2 | 0.82° | 0 of 120 | 0 | 100% | 0.34 m (spine) | 0.54–0.59 m |
| C3 | 0.78° | 0 of 120 | 0 | 100% | 0.34 m (bulkhead) | 0.54–0.59 m |

At the Frame's roughly 16 px per degree, 0.74° is about 12 px. Every overhead and rear part is at least 0.34 m from the eye. The hull is at least 0.54 m away, which beats the 0.35 m the Star Fox rig test asks for.

## The mockup page

Open `build/mockups/cockpit-mockup.html`. Drag to look (head yaw and pitch). The view is a mono stand-in for the Frame's per-eye frustum: ±0.9565 rad horizontal (the mean of L 1.028 and R 0.885), and the real vertical split, U 0.864 and D 1.048. That's about 110 x 110°, letterboxed.

The panel has:

- the ship (all 8) and the variation;
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

## Open questions for the user

1. Which variation, and is there anything to mix? For example, C1's slim frame with C2's split pods, or C3's glass readouts in C2's frame.
2. Should `world_scale` stay at 128? It fits the game's eye point; see above.
3. The eye: keep the game's (0, -128, 0)? It sits 0.46–0.51 m above the deck, so the canopy has to stand about 0.8 m above the deck. A default 5–10 cm lower would make a sleeker canopy, at the cost of a lower view than the flat game's internal camera.
4. Shield: WipEout has no energy meter. Is the shield-pickup timer what you want there, or should that slot show something else, like thrust as a number or the lives stars in championship mode?
5. C2's weapon readout is 0.41 m away, overhead. Is that comfortable to refocus on mid-race, or should it move down onto a pod?
6. Bank: is level-in-banks still the default after seeing it? And is the third mode (cockpit banks, head level) worth keeping as a setting?
7. Team colours: the trim uses one accent per team, picked by me (AG Systems gold, Auricom red, Qirex green, Feisar blue). Keep those, or sample them from each ship's textures?
8. Weapon target reticle and the WRONG WAY warning aren't instruments yet. The standard wants the reticle drawn at the target's depth; that's separate work.

Not done in the mockup: stereo, the real renderer and the headset. Readability and comfort still need a wear session after the build (lesson 1).
