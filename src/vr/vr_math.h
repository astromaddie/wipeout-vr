#ifndef VR_MATH_H
#define VR_MATH_H

#include "vr_types.h"

// Rotation about +Y (heading) of an orientation quaternion (x, y, z, w): the
// angle of its forward vector (-Z) in the horizontal plane, positive turning
// left. Falls back to the right vector when looking straight up or down.
float vr_yaw_of(const float q[4]);

// Quaternion for a rotation of `yaw` radians about +Y.
void vr_quat_from_yaw(float yaw, float out[4]);

// A world-locked panel `distance` metres ahead of the head (horizontally, at
// head height), facing back at it, levelled: only the head's heading counts.
void vr_panel_pose(const vr_pose_t *head, float distance, vr_pose_t *out);

#endif
