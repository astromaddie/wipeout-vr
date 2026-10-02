#ifndef VR_MATH_H
#define VR_MATH_H

#include "vr_types.h"

// Pure maths for the stereo renderer: no GL, no OpenXR, no game headers.
//
// COORDINATE CONVENTIONS
//
// OpenXR (LOCAL space and every eye or head pose): right-handed, +X right,
// +Y up, -Z forward, metres.
//
// The game's world: +X right, +Y *down*, +Z forward at yaw 0, game units. It is
// a proper right-handed system (x right, y down, z forward is the usual camera
// convention). render_set_view() builds its view matrix as R * T(-pos), with R
// from mat4_set_roll_pitch_yaw(angles.x, -angles.y + M_PI, angles.z + M_PI).
// The M_PI offsets are what turn the Y-down world into the GL view space
// (+X right, +Y up, -Z forward): at angles 0, R is diag(1, -1, -1). The view
// space is therefore the same axes as OpenXR's, so an OpenXR eye pose slots
// straight in: the game camera's view-space axes *are* the OpenXR axes.
//
// Angles are (pitch, yaw, roll) = (x, y, z), radians. All matrices are
// column-major like GL: a 4x4 is m[col * 4 + row], a 3x3 is m[col * 3 + row].

// Rotation about +Y (heading) of an orientation quaternion (x, y, z, w): the
// angle of its forward vector (-Z) in the horizontal plane, positive turning
// left. Falls back to the right vector when looking straight up or down.
float vr_yaw_of(const float q[4]);

// Quaternion for a rotation of `yaw` radians about +Y.
void vr_quat_from_yaw(float yaw, float out[4]);

// A world-locked panel `distance` metres ahead of the head (horizontally, at
// head height), facing back at it, levelled: only the head's heading counts.
void vr_panel_pose(const vr_pose_t *head, float distance, vr_pose_t *out);


// -----------------------------------------------------------------------------
// The game's own camera maths, restated

// R of render_set_view(): view = R * (p - pos). 3x3.
void vr_game_view_rotation(const float angles[3], float rot[9]);

// view_mat of render_set_view(pos, angles). 4x4.
void vr_game_view(const float pos[3], const float angles[3], float out[16]);

// sprite_mat of render_set_view(): the billboard rotation. 3x3.
void vr_game_sprite_rotation(const float angles[3], float rot[9]);


// -----------------------------------------------------------------------------
// The rig: game camera plus head

typedef struct {
	float world_scale; // game units per metre
	float head_translation; // scale of the head centre's displacement; the eye offsets are never scaled
	bool follow_roll; // vr.follow_vehicle_rotation: the anchor takes the camera's roll
	bool horizon_lock; // vr.horizon_lock: the anchor drops the camera's pitch
} vr_rig_t;

// Recentre: the head's yaw and position when it happened. The head is then
// treated as sitting at the game camera, facing where the camera faces.
typedef struct {
	float yaw; // radians
	float origin[3]; // head position in LOCAL space, metres
} vr_recentre_t;

typedef struct {
	float position[3]; // game units
	float angles[3]; // pitch, yaw, roll as the game camera has them
} vr_game_camera_t;

// A camera in game space, ready for the shader.
typedef struct {
	float position[3]; // game units
	float rotation[9]; // view = rotation * (p - position)
	float view[16];
	float sprite[9]; // billboard rotation that faces this camera
} vr_cam_t;

// The anchor's angles: pitch and yaw of the game camera, roll 0, unless
// follow_roll keeps the roll, and pitch 0 when horizon_lock is set.
void vr_anchor_angles(const vr_rig_t *rig, const float cam_angles[3], float out[3]);

// The head centre: midway between the eyes, orientation the normalised sum of both.
void vr_head_pose(const vr_view_t views[2], vr_pose_t *out);

void vr_recentre_from_head(const vr_pose_t *head, vr_recentre_t *out);

// The camera for an OpenXR pose (an eye, or the head centre) on the game
// camera `cam`. `head` is the head centre of the same frame: its displacement
// from the recentre origin is scaled by rig->head_translation, the offset of
// `pose` from the head centre is not. With the recentre origin at the head, an
// identity-orientation head gives exactly the game's own view.
void vr_cam_from_pose(const vr_game_camera_t *cam, const vr_rig_t *rig, const vr_recentre_t *rc,
	const vr_pose_t *head, const vr_pose_t *pose, vr_cam_t *out);

// Rotation of the billboard that faces a view rotation R: R^T * diag(-1, -1, 1).
// At roll 0 it equals vr_game_sprite_rotation() of the same angles.
void vr_sprite_from_view_rotation(const float rot[9], float out[9]);

// Rotates `views` about the vertical axis through the head centre (diag_yaw).
void vr_views_apply_yaw(vr_view_t views[2], float yaw);


// -----------------------------------------------------------------------------
// Projection

// GL clip volume (z in -1..1) from the tangents of the frustum edges at distance 1.
void vr_projection_tan(float left, float right, float up, float down, float near, float far, float out[16]);

// From an OpenXR fov (angles in radians: left, right, up, down; left and down negative).
void vr_projection_fov(const float fov[4], float near, float far, float out[16]);

// out = a * b
void vr_mat4_mul(const float a[16], const float b[16], float out[16]);

// Clip-space position of a point, then the divide by w: ndc[0..2]. Returns w.
float vr_project(const float view[16], const float proj[16], const float point[3], float ndc[3]);


// -----------------------------------------------------------------------------
// The HUD quad

// The head's orientation with its roll removed (yaw then pitch only).
void vr_pose_level(const vr_pose_t *head, vr_pose_t *out);

// The HUD quad: `distance` metres along the head's forward, facing the head,
// following its yaw and pitch and never its roll.
void vr_hud_pose(const vr_pose_t *head, float distance, vr_pose_t *out);

// Width and height in metres: 2 * distance * tan(20 degrees) * scale_percent / 100
// wide (40 degrees at 100%), `aspect` = width / height.
void vr_hud_size(float distance, float scale_percent, float aspect, float *width, float *height);

#endif
