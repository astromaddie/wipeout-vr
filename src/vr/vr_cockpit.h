#ifndef VR_COCKPIT_H
#define VR_COCKPIT_H

#include "../types.h"
#include "../wipeout/camera.h"
#include "../wipeout/image.h"
#include "../wipeout/object.h"
#include "../wipeout/ship.h"

#include "vr_cockpit_core.h"

// Cockpit H in the game (WIPEOUT_VR builds; docs/COCKPIT_MOCKUP.md). The maths
// and the data are vr_cockpit_core.h; this is the part that talks to the game's
// ships, camera, HUD and renderer.
//
// Per race frame, from race.c and ship.c:
//   vr_cockpit_begin()     after render_vr_race_view(), before render_set_view(): decides
//                          whether this frame shows the cockpit and moves the eyes to it
//   vr_cockpit_draw()      from ships_draw(), where the internal-view ship was skipped
//   vr_cockpit_hud_draw()  in the 2D section, in place of hud_draw()

// After ships_load() has set every g.ships[].model: matches each model to a
// hull shape, clips it against the cabin, samples its trim from the ship's own
// textures (decoded again from `textures_path`, because the GL atlas is not read
// back), and pre-shades the authored parts. Safe to call again.
void vr_cockpit_load(texture_list_t ship_textures, char *textures_path);
void vr_cockpit_unload(void);
// The same for one ship from its model and its textures' pixels (indexed by the
// game's texture index, as the model's polygons carry it): what vr_cockpit_load
// does per ship, and what the harness calls with synthetic hulls.
void vr_cockpit_build_ship(int ship_index, const Object *model, const vr_cockpit_image_t *images, int image_count);

// True when the cockpit is the view this frame: a stereo frame, `cockpit` on,
// the pilot's ship in the internal view with a race (or attract) internal
// camera, and its hull matched.
bool vr_cockpit_begin(camera_t *camera);
bool vr_cockpit_active(void);

// The pilot's cockpit: the hull with the cabin cut out, the parts, the instrument faces, the glass.
void vr_cockpit_draw(ship_t *ship);

// The instrument faces, once, through the game's own 2D calls. False when the
// cockpit is not the view, and the caller draws the floating HUD instead.
bool vr_cockpit_hud_draw(ship_t *ship);

// What a ship's load made, for the harness
typedef struct {
	bool valid;
	int shape;
	vr_cockpit_trim_t trim;
	int hull_tris, inside_tris, part_tris, glass_tris;
} vr_cockpit_ship_info_t;
bool vr_cockpit_ship_info(int ship_index, vr_cockpit_ship_info_t *out);
// A triangle of a ship's clipped hull (outside copy): three xyz positions in ship space. False past the end.
bool vr_cockpit_hull_triangle(int ship_index, int index, float pos[9]);

// The pose of the frame's cockpit (valid while active), for the harness
const vr_cockpit_pose_t *vr_cockpit_pose_now(void);

#endif
