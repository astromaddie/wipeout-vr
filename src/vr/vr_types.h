#ifndef VR_TYPES_H
#define VR_TYPES_H

#include <stdbool.h>
#include <stdint.h>

// Plain data types of the VR host. No OpenXR or GL headers, so game code can
// include them. All poses are in the LOCAL reference space, in metres.

typedef struct {
	float orientation[4]; // quaternion x, y, z, w
	float position[3];
} vr_pose_t;

typedef struct {
	vr_pose_t pose;
	float fov[4]; // angles in radians: left, right, up, down (left and down are negative)
} vr_view_t;

typedef struct {
	int64_t predicted_display_time; // nanoseconds, the runtime's clock
	int64_t predicted_display_period;
	bool should_render; // false in standby: keep looping, submit nothing
	bool views_valid; // pose and fov of both eyes are usable
	vr_view_t views[2]; // left, right
} vr_frame_t;

#endif
