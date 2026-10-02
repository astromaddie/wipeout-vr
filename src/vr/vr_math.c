#include <math.h>

#include "vr_math.h"

static void rotate(const float q[4], const float v[3], float out[3]) {
	// v' = v + 2w (qv x v) + 2 qv x (qv x v)
	float c1[3] = {
		q[1] * v[2] - q[2] * v[1],
		q[2] * v[0] - q[0] * v[2],
		q[0] * v[1] - q[1] * v[0]
	};
	float t[3] = { 2 * c1[0], 2 * c1[1], 2 * c1[2] };
	out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
	out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
	out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

float vr_yaw_of(const float q[4]) {
	float forward_in[3] = {0, 0, -1};
	float f[3];
	rotate(q, forward_in, f);
	if (f[0] * f[0] + f[2] * f[2] > 1e-6f) {
		return atan2f(-f[0], -f[2]);
	}

	float right_in[3] = {1, 0, 0};
	float r[3];
	rotate(q, right_in, r);
	return atan2f(-r[2], r[0]);
}

void vr_quat_from_yaw(float yaw, float out[4]) {
	out[0] = 0;
	out[1] = sinf(yaw * 0.5f);
	out[2] = 0;
	out[3] = cosf(yaw * 0.5f);
}

void vr_panel_pose(const vr_pose_t *head, float distance, vr_pose_t *out) {
	float yaw = vr_yaw_of(head->orientation);
	vr_quat_from_yaw(yaw, out->orientation);
	out->position[0] = head->position[0] - sinf(yaw) * distance;
	out->position[1] = head->position[1];
	out->position[2] = head->position[2] - cosf(yaw) * distance;
}
