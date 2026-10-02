#include <math.h>

#include "sfvr_test.h"
#include "vr_math.h"

#define PI 3.14159265358979f

static void quat_about_x(float angle, float q[4]) {
	q[0] = sinf(angle / 2); q[1] = 0; q[2] = 0; q[3] = cosf(angle / 2);
}

static void quat_mul(const float a[4], const float b[4], float out[4]) {
	out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void test_yaw(void) {
	float identity[4] = {0, 0, 0, 1};
	CHECK_NEAR(vr_yaw_of(identity), 0.0, 1e-5);

	// turning left by 90 degrees about +Y: forward -Z becomes -X
	float left[4];
	vr_quat_from_yaw(PI / 2, left);
	CHECK_NEAR(vr_yaw_of(left), PI / 2, 1e-5);

	float right[4];
	vr_quat_from_yaw(-PI / 4, right);
	CHECK_NEAR(vr_yaw_of(right), -PI / 4, 1e-5);

	float behind[4];
	vr_quat_from_yaw(PI * 0.99f, behind);
	CHECK_NEAR(vr_yaw_of(behind), PI * 0.99f, 1e-4);
}

static void test_yaw_ignores_pitch_and_roll(void) {
	float yaw[4], pitch[4], q[4];
	vr_quat_from_yaw(0.7f, yaw);
	quat_about_x(-0.5f, pitch); // looking down 29 degrees
	quat_mul(yaw, pitch, q);
	CHECK_NEAR(vr_yaw_of(q), 0.7, 1e-5);

	quat_about_x(0.9f, pitch);
	quat_mul(yaw, pitch, q);
	CHECK_NEAR(vr_yaw_of(q), 0.7, 1e-5);
}

static void test_yaw_looking_straight_down(void) {
	// Pitch -90 degrees after a yaw of 0.7: forward is vertical, so the right
	// vector decides, and that still holds the heading.
	float yaw[4], pitch[4], q[4];
	vr_quat_from_yaw(0.7f, yaw);
	quat_about_x(-PI / 2, pitch);
	quat_mul(yaw, pitch, q);
	CHECK_NEAR(vr_yaw_of(q), 0.7, 1e-3);
}

static void test_panel_pose(void) {
	vr_pose_t out;

	// Facing -Z from (0, 1.6, 0): 2 m ahead at head height
	vr_pose_t head = { .orientation = {0, 0, 0, 1}, .position = {0, 1.6f, 0} };
	vr_panel_pose(&head, 2.0f, &out);
	CHECK_NEAR(out.position[0], 0.0, 1e-5);
	CHECK_NEAR(out.position[1], 1.6, 1e-5);
	CHECK_NEAR(out.position[2], -2.0, 1e-5);
	CHECK_NEAR(out.orientation[3], 1.0, 1e-5);

	// Head turned 90 degrees left, standing at (1, 1.5, 3): the panel is to -X of it
	vr_quat_from_yaw(PI / 2, head.orientation);
	head.position[0] = 1; head.position[1] = 1.5f; head.position[2] = 3;
	vr_panel_pose(&head, 2.0f, &out);
	CHECK_NEAR(out.position[0], -1.0, 1e-5);
	CHECK_NEAR(out.position[1], 1.5, 1e-5);
	CHECK_NEAR(out.position[2], 3.0, 1e-5);
	CHECK_NEAR(vr_yaw_of(out.orientation), PI / 2, 1e-5);
	CHECK_NEAR(out.orientation[0], 0.0, 1e-6); // upright
	CHECK_NEAR(out.orientation[2], 0.0, 1e-6);

	// Looking down 45 degrees does not tilt the panel or move it down
	float yaw[4], pitch[4];
	vr_quat_from_yaw(0, yaw);
	quat_about_x(-PI / 4, pitch);
	quat_mul(yaw, pitch, head.orientation);
	head.position[0] = 0; head.position[1] = 1.6f; head.position[2] = 0;
	vr_panel_pose(&head, 2.0f, &out);
	CHECK_NEAR(out.position[1], 1.6, 1e-5);
	CHECK_NEAR(out.position[2], -2.0, 1e-4);
	CHECK_NEAR(out.orientation[0], 0.0, 1e-6);
}

int main(void) {
	RUN(test_yaw);
	RUN(test_yaw_ignores_pitch_and_roll);
	RUN(test_yaw_looking_straight_down);
	RUN(test_panel_pose);
	return SFVR_TEST_RESULT("vr_math");
}
