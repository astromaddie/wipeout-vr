#ifndef VR_COCKPIT_CORE_H
#define VR_COCKPIT_CORE_H

#include <stdbool.h>
#include <stdint.h>

// Cockpit H, the maths and the data (docs/COCKPIT_MOCKUP.md, "Implementation
// plan"). No GL, no game headers: the game-facing half is vr_cockpit.c.
//
// What lives here:
//  - the generated geometry table (vr_cockpit_geometry.inc) and its accessors;
//  - the hull match, and the clip of a ship's polygons against the cabin cut box;
//  - the trim rule (sampled from a ship's own textures);
//  - the pre-shaded authored parts;
//  - the eye and the three bank modes.
//
// Ship space is the game's own: +x right, +y down, +z forward, game units.

#include "vr_cockpit_geometry.inc"

typedef enum {
	VR_COCKPIT_BANK_LEVEL, // yaw and pitch only: the cockpit turns about the eye and never rolls
	VR_COCKPIT_BANK_FOLLOW, // the ship's full angles: view and cockpit roll together
	VR_COCKPIT_BANK_COCKPIT, // the cockpit rolls with the ship around a level head
	VR_COCKPIT_BANK_COUNT
} vr_cockpit_bank_t;

// vr.toml: cockpit, cockpit_bank, cockpit_eye_forward_cm, cockpit_eye_up_cm,
// cockpit_glass. Read from the live settings every frame.
typedef struct vr_cockpit_settings {
	bool enabled;
	int bank; // vr_cockpit_bank_t
	float eye_forward_cm;
	float eye_up_cm;
	bool glass;
	float world_scale; // game units per metre: the seat offsets are physical, so they scale with it
} vr_cockpit_settings_t;

#define VR_COCKPIT_EYE_RANGE_CM 30.0f
#define VR_COCKPIT_SHAPES 4
#define VR_COCKPIT_FACE_MAX 3
#define VR_COCKPIT_GLASS_ALPHA 0.045f // glass tint, normal blend
#define VR_COCKPIT_INSIDE_DIM 0.55f // the inside copy of the hull, against the outside


// -----------------------------------------------------------------------------
// The table

typedef struct {
	float x0, x1; // the cabin cut box: x in [x0, x1], z in [z0, z1], y < y_max (open top)
	float y_max;
	float z0, z1;
} vr_cockpit_cut_t;

typedef struct {
	const char *name;
	const char *group; // struts, sills, tub, seat, bulkhead, headrest, hoop, dash, podL, podR, weaponPod, glass
	int slot; // vr_cockpit_slot_t
	unsigned hull_mask; // bit n: hull shape n uses this part
	int tri_count;
	const float *verts; // 9 floats a triangle
} vr_cockpit_part_info_t;

typedef struct {
	const char *id; // left, right, weapon
	int tex_w, tex_h;
	float corners[12]; // top-left, top-right, bottom-right, bottom-left
} vr_cockpit_face_info_t;

float vr_cockpit_fitted_scale(void); // VR_COCKPIT_WORLD_SCALE
float vr_cockpit_fitted_eye_y(void); // VR_COCKPIT_EYE_Y
vr_cockpit_cut_t vr_cockpit_cut(void);

int vr_cockpit_part_count(void);
bool vr_cockpit_part(int index, vr_cockpit_part_info_t *out);
int vr_cockpit_face_count(void);
bool vr_cockpit_face(int index, vr_cockpit_face_info_t *out);
// The element's texel position and ui_scale, from the table. False when unknown.
bool vr_cockpit_element(const char *face, const char *element, int *x, int *y, int *ui_scale);
const uint8_t *vr_cockpit_slot_color(int slot); // 3 bytes, the authored default (the trim slots are 0,0,0)
const char *vr_cockpit_slot_name(int slot);

// The hull shape of a ship model by name and by the triangles object_draw pushes
// for it, or -1: then no cockpit is drawn.
int vr_cockpit_hull_shape(const char *model_name, int tris);


// -----------------------------------------------------------------------------
// Triangles: the layout of the game's vertex_t

typedef struct {
	float pos[3];
	float uv[2]; // texels of the texture; the renderer adds the atlas offset
	uint8_t color[4]; // the shader doubles it
} vr_cockpit_vertex_t;

typedef struct {
	vr_cockpit_vertex_t v[3];
	int32_t texture; // the game's texture index; -1 for none
} vr_cockpit_tri_t;

typedef struct {
	vr_cockpit_tri_t *tris;
	int count, cap;
} vr_cockpit_trilist_t;

void vr_cockpit_trilist_free(vr_cockpit_trilist_t *l);
void vr_cockpit_trilist_push(vr_cockpit_trilist_t *l, const vr_cockpit_tri_t *t);

// Clips one triangle against the cabin box, keeping what is outside it. The
// pieces are appended to `out` with their winding, uv and colour interpolated.
// Returns the number of pieces. A triangle wholly inside gives none.
int vr_cockpit_clip_triangle(const vr_cockpit_tri_t *tri, const vr_cockpit_cut_t *cut, vr_cockpit_trilist_t *out);

// The same triangles wound the other way and dimmed: the inside of the hull.
void vr_cockpit_inside_copy(const vr_cockpit_trilist_t *front, float dim, vr_cockpit_trilist_t *out);


// -----------------------------------------------------------------------------
// Trim (the rule in tools/mockup/cockpit_geometry.json, "trim_rule")

typedef struct {
	int w, h;
	const uint8_t *rgba; // w * h * 4
} vr_cockpit_image_t;

typedef struct {
	uint8_t primary[3];
	uint8_t secondary[3];
	bool secondary_fallback; // no second hue: the primary at 55%
	bool neutral; // under 0.5% saturated area: the frame slots
	float saturated_share;
} vr_cockpit_trim_t;

// `tris` are a ship model's triangles as the game pushes them; each tri's
// `texture` indexes `images` (out of range, or no image: the 128 grey of RENDER_NO_TEXTURE).
void vr_cockpit_sample_trim(const vr_cockpit_tri_t *tris, int count, const vr_cockpit_image_t *images, int image_count, vr_cockpit_trim_t *out);


// -----------------------------------------------------------------------------
// The authored parts, pre-shaded

// The parts of hull shape `shape`, a flat colour per triangle from the slot and
// a fixed light in ship space. They are drawn untextured, and the game's
// RENDER_NO_TEXTURE is a 128 grey that its shader doubles, so the vertex colour
// is the colour on screen. Opaque parts, then the glass (alpha
// VR_COCKPIT_GLASS_ALPHA). `texture` is -1 everywhere. `trim` is {primary, secondary}.
void vr_cockpit_build_parts(int shape, const uint8_t trim[2][3], vr_cockpit_trilist_t *opaque, vr_cockpit_trilist_t *glass);

// The flat light a triangle gets: ambient plus the sun on a normal that faces `toward`.
float vr_cockpit_light(const float a[3], const float b[3], const float c[3], const float toward[3], float emissive);


// -----------------------------------------------------------------------------
// The eye and the bank modes

typedef struct {
	float eye_ship[3]; // the eye in ship space: the fitted eye plus the user's offsets
	float eye_world[3]; // that point through the ship's full matrix: where the head sits
	float anchor_angles[3]; // pitch, yaw, roll for render_set_view's convention
	float model[16]; // M_c: ship space to world, column-major, for render_set_model_mat
} vr_cockpit_pose_t;

// The eye in ship space. The offsets are physical, so they scale with the
// world: cm / 100 * world_scale game units. +forward is +z, +up is -y.
void vr_cockpit_eye(const vr_cockpit_settings_t *s, float world_scale, float out[3]);

// ship_angles are the ship's (pitch, yaw, roll), the angles its matrix is built from.
void vr_cockpit_pose(int bank, const float ship_pos[3], const float ship_angles[3], const float eye_ship[3], vr_cockpit_pose_t *out);

// Clamps and defaults for a settings snapshot.
void vr_cockpit_settings_sanitize(vr_cockpit_settings_t *s);

#endif
