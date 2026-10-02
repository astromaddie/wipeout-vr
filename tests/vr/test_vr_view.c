// The eye and HUD maths against the game's own matrices (src/types.c is linked
// in as the oracle). No GL, no window.

#include <math.h>
#include <string.h>

#include "sfvr_test.h"
#include "types.h"
#include "vr_math.h"

float rand_float(float min, float max) { // types.c's vec3_rand wants it; unused here
	return min + max * 0;
}

#define PI 3.14159265f

static const float angle_sets[][3] = {
	{0, 0, 0},
	{0.3f, 0.9f, 0},
	{-0.4f, -2.2f, 0},
	{0.1f, 3.0f, 0},
	{0.7f, -0.5f, 0.35f},
	{-1.1f, 1.5f, -0.6f},
};
#define ANGLE_SETS ((int)(sizeof(angle_sets) / sizeof(angle_sets[0])))

static const float positions[3] = {1234.5f, -678.0f, 9012.25f};

// What render_set_view() does, verbatim, with the real functions.
static void game_view(const float pos[3], const float a[3], float view[16], float sprite[9]) {
	vec3_t angles = vec3(a[0], a[1], a[2]);
	mat4_t view_mat = mat4_identity();
	mat4_t sprite_mat = mat4_identity();
	mat4_set_translation(&view_mat, vec3(0, 0, 0));
	mat4_set_roll_pitch_yaw(&view_mat, vec3(angles.x, -angles.y + M_PI, angles.z + M_PI));
	mat4_translate(&view_mat, vec3_inv(vec3(pos[0], pos[1], pos[2])));
	mat4_set_yaw_pitch_roll(&sprite_mat, vec3(-angles.x, angles.y - M_PI, 0));
	memcpy(view, view_mat.m, sizeof(view_mat.m));
	for (int c = 0; c < 3; c++) {
		for (int r = 0; r < 3; r++) {
			sprite[c * 3 + r] = sprite_mat.cols[c][r];
		}
	}
}

static void check_mat(const float *got, const float *want, int n, double eps) {
	for (int i = 0; i < n; i++) {
		CHECK_NEAR(got[i], want[i], eps);
	}
}

static void quat_axis(char axis, float angle, float q[4]) {
	q[0] = q[1] = q[2] = 0;
	q[3] = cosf(angle / 2);
	q[axis == 'x' ? 0 : axis == 'y' ? 1 : 2] = sinf(angle / 2);
}

static void quat_mul(const float a[4], const float b[4], float out[4]) {
	out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void views_at(const float pos[3], const float q[4], float ipd, vr_view_t v[2]) {
	for (int e = 0; e < 2; e++) {
		memset(&v[e], 0, sizeof(v[e]));
		memcpy(v[e].pose.orientation, q, 4 * sizeof(float));
		memcpy(v[e].pose.position, pos, 3 * sizeof(float));
		v[e].pose.position[0] += (e == 0 ? -0.5f : 0.5f) * ipd;
		v[e].fov[0] = -0.8f; v[e].fov[1] = 0.7f; v[e].fov[2] = 0.75f; v[e].fov[3] = -0.85f;
	}
}

static const vr_rig_t rig_default = { .world_scale = 128, .head_translation = 1 };

// A recentred rig: the head at `v` just recentred on the game camera `cam`.
static void centre_cam(const vr_game_camera_t *cam, const vr_rig_t *rig, const vr_view_t v[2], int which, vr_cam_t *out) {
	vr_pose_t head;
	vr_head_pose(v, &head);
	vr_recentre_t rc;
	vr_recentre_from_head(&head, &rc);
	if (which < 0) {
		vr_cam_from_pose(cam, rig, &rc, &head, &head, out);
	}
	else {
		vr_cam_from_pose(cam, rig, &rc, &head, &v[which].pose, out);
	}
}

// World forward of a camera: R^T * (0, 0, -1)
static void cam_forward(const vr_cam_t *c, float out[3]) {
	out[0] = -c->rotation[2];
	out[1] = -c->rotation[5];
	out[2] = -c->rotation[8];
}


// -----------------------------------------------------------------------------

static void test_game_matrices_restated(void) {
	for (int i = 0; i < ANGLE_SETS; i++) {
		float view[16], sprite[9], want_view[16], want_sprite[9], rot[9];
		game_view(positions, angle_sets[i], want_view, want_sprite);

		vr_game_view(positions, angle_sets[i], view);
		check_mat(view, want_view, 16, 1e-4);
		vr_game_sprite_rotation(angle_sets[i], sprite);
		check_mat(sprite, want_sprite, 9, 1e-6);

		// the sprite rotation is the view rotation's transpose, flipped about the view axis (roll 0 only)
		if (angle_sets[i][2] == 0) {
			vr_game_view_rotation(angle_sets[i], rot);
			float derived[9];
			vr_sprite_from_view_rotation(rot, derived);
			check_mat(derived, want_sprite, 9, 1e-5);
		}
	}
}

static void test_world_is_y_down_z_forward(void) {
	// angles 0: the camera looks along +Z, +X is right, +Y is down
	float view[16], ndc[3], origin[3] = {0, 0, 0};
	vr_game_view(origin, angle_sets[0], view);
	float proj[16];
	vr_projection_tan(-1, 1, 1, -1, 16, 64000, proj);

	float ahead[3] = {0, 0, 1000};
	CHECK(vr_project(view, proj, ahead, ndc) > 0);
	CHECK_NEAR(ndc[0], 0, 1e-6);
	CHECK_NEAR(ndc[1], 0, 1e-6);

	float right[3] = {500, 0, 1000};
	vr_project(view, proj, right, ndc);
	CHECK_NEAR(ndc[0], 0.5, 1e-5); // +X is screen right

	float below[3] = {0, 500, 1000};
	vr_project(view, proj, below, ndc);
	CHECK_NEAR(ndc[1], -0.5, 1e-5); // +Y (down in the world) is screen down

	float behind[3] = {0, 0, -1000};
	CHECK(vr_project(view, proj, behind, ndc) < 0);
}

static void test_identity_head_gives_the_games_view(void) {
	vr_view_t v[2];
	float q[4] = {0, 0, 0, 1}, head_pos[3] = {0, 1.6f, 0};
	views_at(head_pos, q, 0.064f, v);

	for (int i = 0; i < ANGLE_SETS; i++) {
		vr_game_camera_t cam = { .position = {positions[0], positions[1], positions[2]}, .angles = {angle_sets[i][0], angle_sets[i][1], angle_sets[i][2]} };
		float want[16], sprite[9];
		vr_cam_t c;

		// Default: the anchor has no roll
		float anchor[3] = {cam.angles[0], cam.angles[1], 0};
		game_view(cam.position, anchor, want, sprite);
		centre_cam(&cam, &rig_default, v, -1, &c);
		check_mat(c.view, want, 16, 2e-3);
		check_mat(c.position, cam.position, 3, 1e-3);
		check_mat(c.sprite, sprite, 9, 1e-5);

		// follow_vehicle_rotation: the camera's roll too
		vr_rig_t follow = rig_default;
		follow.follow_roll = true;
		game_view(cam.position, cam.angles, want, sprite);
		centre_cam(&cam, &follow, v, -1, &c);
		check_mat(c.view, want, 16, 2e-3);

		// horizon_lock: no pitch
		vr_rig_t level = rig_default;
		level.horizon_lock = true;
		float flat_angles[3] = {0, cam.angles[1], 0};
		game_view(cam.position, flat_angles, want, sprite);
		centre_cam(&cam, &level, v, -1, &c);
		check_mat(c.view, want, 16, 2e-3);
	}
}

static void test_head_yaw_right_looks_at_the_cameras_right(void) {
	for (int i = 0; i < ANGLE_SETS; i++) {
		vr_game_camera_t cam = { .position = {100, 200, 300}, .angles = {angle_sets[i][0], angle_sets[i][1], 0} };
		vr_view_t v[2];
		float q[4] = {0, 0, 0, 1}, head_pos[3] = {0, 1.6f, 0};
		views_at(head_pos, q, 0.064f, v);
		vr_pose_t head;
		vr_head_pose(v, &head);
		vr_recentre_t rc;
		vr_recentre_from_head(&head, &rc);

		// Turn the head 90 degrees to the right (about +Y, so a negative angle)
		float turned[4];
		vr_quat_from_yaw(-PI / 2, turned);
		vr_view_t t[2];
		views_at(head_pos, turned, 0.064f, t);
		vr_pose_t thead;
		vr_head_pose(t, &thead);
		vr_cam_t c;
		vr_cam_from_pose(&cam, &rig_default, &rc, &thead, &thead, &c);

		float fwd[3];
		cam_forward(&c, fwd);
		mat4_t m = mat4_identity();
		mat4_set_yaw_pitch_roll(&m, vec3(cam.angles[0], cam.angles[1], 0));
		CHECK_NEAR(fwd[0], m.basis.right.vec3.x, 1e-4);
		CHECK_NEAR(fwd[1], m.basis.right.vec3.y, 1e-4);
		CHECK_NEAR(fwd[2], m.basis.right.vec3.z, 1e-4);

		// and the unturned head looks where the game camera looks
		vr_cam_from_pose(&cam, &rig_default, &rc, &head, &head, &c);
		cam_forward(&c, fwd);
		CHECK_NEAR(fwd[0], m.basis.forward.vec3.x, 1e-4);
		CHECK_NEAR(fwd[1], m.basis.forward.vec3.y, 1e-4);
		CHECK_NEAR(fwd[2], m.basis.forward.vec3.z, 1e-4);
	}
}

static void test_eyes_are_half_the_ipd_apart_along_the_cameras_right(void) {
	float ipd = 0.0627f;
	for (int i = 0; i < ANGLE_SETS; i++) {
		for (int s = 0; s < 2; s++) {
			vr_rig_t rig = rig_default;
			rig.world_scale = s ? 300.0f : 128.0f;
			vr_game_camera_t cam = { .position = {100, 200, 300}, .angles = {angle_sets[i][0], angle_sets[i][1], 0} };
			vr_view_t v[2];
			float q[4] = {0, 0, 0, 1}, head_pos[3] = {0, 1.6f, 0};
			views_at(head_pos, q, ipd, v);

			vr_cam_t left, right, centre;
			centre_cam(&cam, &rig, v, 0, &left);
			centre_cam(&cam, &rig, v, 1, &right);
			centre_cam(&cam, &rig, v, -1, &centre);

			mat4_t m = mat4_identity();
			mat4_set_yaw_pitch_roll(&m, vec3(cam.angles[0], cam.angles[1], 0));
			float half = 0.5f * ipd * rig.world_scale;
			CHECK_NEAR(left.position[0] - centre.position[0], -half * m.basis.right.vec3.x, 1e-3);
			CHECK_NEAR(left.position[1] - centre.position[1], -half * m.basis.right.vec3.y, 1e-3);
			CHECK_NEAR(left.position[2] - centre.position[2], -half * m.basis.right.vec3.z, 1e-3);
			CHECK_NEAR(right.position[0] - centre.position[0], half * m.basis.right.vec3.x, 1e-3);
			CHECK_NEAR(right.position[1] - centre.position[1], half * m.basis.right.vec3.y, 1e-3);
			CHECK_NEAR(right.position[2] - centre.position[2], half * m.basis.right.vec3.z, 1e-3);

			// and the eyes are exactly ipd * scale apart
			float d[3] = { right.position[0] - left.position[0], right.position[1] - left.position[1], right.position[2] - left.position[2] };
			CHECK_NEAR(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), ipd * rig.world_scale, 1e-3);
		}
	}
}

static void test_head_translation_scales_the_head_not_the_eyes(void) {
	vr_game_camera_t cam = { .position = {0, 0, 0}, .angles = {0, 0, 0} }; // looking along +Z, right is +X
	vr_view_t v[2];
	float q[4] = {0, 0, 0, 1}, home[3] = {0, 1.6f, 0};
	views_at(home, q, 0.064f, v);
	vr_pose_t head;
	vr_head_pose(v, &head);
	vr_recentre_t rc;
	vr_recentre_from_head(&head, &rc);

	// Lean 0.1 m to the right, 0.2 m up and 0.3 m forward (forward is -Z in OpenXR)
	float leaned[3] = {0.1f, 1.8f, -0.3f};
	vr_view_t l[2];
	views_at(leaned, q, 0.064f, l);

	static const float scales[3] = {0.0f, 1.0f, 2.0f};
	for (int s = 0; s < 3; s++) {
		vr_rig_t rig = rig_default;
		rig.head_translation = scales[s];
		vr_pose_t lhead;
		vr_head_pose(l, &lhead);
		vr_cam_t left, right;
		vr_cam_from_pose(&cam, &rig, &rc, &lhead, &l[0].pose, &left);
		vr_cam_from_pose(&cam, &rig, &rc, &lhead, &l[1].pose, &right);

		float mid[3];
		for (int i = 0; i < 3; i++) {
			mid[i] = 0.5f * (left.position[i] + right.position[i]);
		}
		// game: +X right, -Y up, +Z forward
		float k = rig.head_translation * rig.world_scale;
		CHECK_NEAR(mid[0], 0.1f * k, 1e-3);
		CHECK_NEAR(mid[1], -0.2f * k, 1e-3);
		CHECK_NEAR(mid[2], 0.3f * k, 1e-3);
		CHECK_NEAR(right.position[0] - left.position[0], 0.064f * rig.world_scale, 1e-3);
	}
}

static void test_recentre_absorbs_the_heads_yaw(void) {
	vr_game_camera_t cam = { .position = {50, 60, 70}, .angles = {0.2f, 1.1f, 0} };
	float q[4];
	vr_quat_from_yaw(0.5f, q); // the wearer faces 0.5 rad to the left when they recentre
	float pos[3] = {0.3f, 1.4f, -0.2f};
	vr_view_t v[2];
	views_at(pos, q, 0.064f, v);
	vr_pose_t head;
	vr_head_pose(v, &head);
	vr_recentre_t rc;
	vr_recentre_from_head(&head, &rc);
	CHECK_NEAR(rc.yaw, 0.5, 1e-5);

	vr_cam_t c;
	vr_cam_from_pose(&cam, &rig_default, &rc, &head, &head, &c);
	float want[16], sprite[9];
	game_view(cam.position, cam.angles, want, sprite);
	check_mat(c.view, want, 16, 2e-3);

	// Turning a further 0.4 rad left turns the view 0.4 rad left of the game camera's heading
	float more[4], turned[4];
	vr_quat_from_yaw(0.4f, more);
	quat_mul(more, q, turned);
	views_at(pos, turned, 0.064f, v);
	vr_head_pose(v, &head);
	vr_cam_from_pose(&cam, &rig_default, &rc, &head, &head, &c);
	float fwd[3];
	cam_forward(&c, fwd);

	mat4_t m = mat4_identity();
	mat4_set_yaw_pitch_roll(&m, vec3(cam.angles[0], cam.angles[1], 0));
	float f = cosf(0.4f), l = sinf(0.4f);
	CHECK_NEAR(fwd[0], f * m.basis.forward.vec3.x - l * m.basis.right.vec3.x, 1e-4);
	CHECK_NEAR(fwd[1], f * m.basis.forward.vec3.y - l * m.basis.right.vec3.y, 1e-4);
	CHECK_NEAR(fwd[2], f * m.basis.forward.vec3.z - l * m.basis.right.vec3.z, 1e-4);

	// A rolled head tilts the view's right vector out of the anchor's horizontal
	float roll[4];
	quat_axis('z', 0.3f, roll);
	quat_mul(q, roll, turned);
	views_at(pos, turned, 0.064f, v);
	vr_head_pose(v, &head);
	vr_cam_from_pose(&cam, &rig_default, &rc, &head, &head, &c);
	CHECK(fabsf(c.rotation[3]) > 0.2f);
}

static void test_asymmetric_fov_maps_to_the_clip_planes(void) {
	float fov[4] = {-0.9f, 0.6f, 0.7f, -0.8f}; // left, right, up, down
	float near = 16, far = 64000, p[16];
	vr_projection_fov(fov, near, far, p);

	float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
	float ndc[3];

	// points on the frustum's edge rays at depth 100 (view space looks down -Z)
	float edges[4][3] = {
		{ tanf(fov[0]) * 100, 0, -100 },
		{ tanf(fov[1]) * 100, 0, -100 },
		{ 0, tanf(fov[2]) * 100, -100 },
		{ 0, tanf(fov[3]) * 100, -100 },
	};
	vr_project(view, p, edges[0], ndc); CHECK_NEAR(ndc[0], -1, 1e-5);
	vr_project(view, p, edges[1], ndc); CHECK_NEAR(ndc[0], 1, 1e-5);
	vr_project(view, p, edges[2], ndc); CHECK_NEAR(ndc[1], 1, 1e-5);
	vr_project(view, p, edges[3], ndc); CHECK_NEAR(ndc[1], -1, 1e-5);

	// straight ahead is off-centre by the asymmetry
	float ahead[3] = {0, 0, -100};
	vr_project(view, p, ahead, ndc);
	CHECK_NEAR(ndc[0], -(tanf(fov[1]) + tanf(fov[0])) / (tanf(fov[1]) - tanf(fov[0])), 1e-5);
	CHECK_NEAR(ndc[1], -(tanf(fov[2]) + tanf(fov[3])) / (tanf(fov[2]) - tanf(fov[3])), 1e-5);

	// near and far planes are z = -1 and +1
	float on_near[3] = {0, 0, -near}, on_far[3] = {0, 0, -far};
	vr_project(view, p, on_near, ndc); CHECK_NEAR(ndc[2], -1, 1e-5);
	vr_project(view, p, on_far, ndc); CHECK_NEAR(ndc[2], 1, 1e-4);

	// a symmetric fov is the game's own projection (73.75 degrees vertical, aspect 16:9)
	float game_fov = (float)((73.75 / 180.0) * 3.14159265358);
	float t = tanf(game_fov / 2), aspect = 16.0f / 9.0f, sym[16];
	vr_projection_tan(-t * aspect, t * aspect, t, -t, near, far, sym);
	float f = 1.0f / t, nf = 1.0f / (near - far);
	CHECK_NEAR(sym[0], f / aspect, 1e-5);
	CHECK_NEAR(sym[5], f, 1e-5);
	CHECK_NEAR(sym[10], (far + near) * nf, 1e-6);
	CHECK_NEAR(sym[11], -1, 1e-6);
	CHECK_NEAR(sym[14], 2 * far * near * nf, 1e-2);
	CHECK_NEAR(sym[8], 0, 1e-6);
	CHECK_NEAR(sym[9], 0, 1e-6);
}

static void test_a_near_object_is_further_right_in_the_left_eye(void) {
	// Disparity sign through the whole chain: eyes on the game camera, an object 10 m ahead
	vr_game_camera_t cam = { .position = {0, 0, 0}, .angles = {0, 0, 0} };
	vr_view_t v[2];
	float q[4] = {0, 0, 0, 1}, home[3] = {0, 1.6f, 0};
	views_at(home, q, 0.064f, v);
	vr_cam_t left, right;
	centre_cam(&cam, &rig_default, v, 0, &left);
	centre_cam(&cam, &rig_default, v, 1, &right);

	float fov[4] = {-0.8f, 0.8f, 0.8f, -0.8f}, proj[16];
	vr_projection_fov(fov, 16, 64000, proj);
	float object[3] = {0, 0, 10 * 128.0f}; // straight ahead, 10 m
	float nl[3], nr[3];
	vr_project(left.view, proj, object, nl);
	vr_project(right.view, proj, object, nr);
	CHECK(nl[0] > nr[0]);
	CHECK(nl[0] > 0 && nr[0] < 0);
	CHECK_NEAR(nl[0], -nr[0], 1e-5);
}

static void test_hud_pose_follows_yaw_and_pitch_never_roll(void) {
	float yq[4], pq[4], rq[4], t[4], q[4];
	vr_quat_from_yaw(0.8f, yq);
	quat_axis('x', -0.4f, pq); // looking down
	quat_axis('z', 0.5f, rq); // head rolled
	quat_mul(yq, pq, t);
	quat_mul(t, rq, q);
	vr_pose_t head = { .position = {0.2f, 1.5f, -0.4f} };
	memcpy(head.orientation, q, sizeof(q));

	vr_pose_t hud;
	vr_hud_pose(&head, 2.0f, &hud);

	// 2 m from the head along the head's forward
	{
		float x = head.orientation[0], y = head.orientation[1], z = head.orientation[2], w = head.orientation[3];
		float fwd[3] = { -2 * (x * z + y * w), -2 * (y * z - x * w), -(1 - 2 * (x * x + y * y)) };
		CHECK_NEAR(hud.position[0] - head.position[0], 2 * fwd[0], 1e-5);
		CHECK_NEAR(hud.position[1] - head.position[1], 2 * fwd[1], 1e-5);
		CHECK_NEAR(hud.position[2] - head.position[2], 2 * fwd[2], 1e-5);
	}

	// Level: the quad's right vector (its +X) is horizontal; its forward has the head's pitch and yaw
	{
		float x = hud.orientation[0], y = hud.orientation[1], z = hud.orientation[2], w = hud.orientation[3];
		CHECK_NEAR(2 * (x * y + z * w), 0, 1e-5);
		float fwd_y = -2 * (y * z - x * w);
		float head_fwd_y = -2 * (head.orientation[1] * head.orientation[2] - head.orientation[0] * head.orientation[3]);
		CHECK_NEAR(fwd_y, head_fwd_y, 1e-5);
	}
	CHECK_NEAR(vr_yaw_of(hud.orientation), 0.8f, 1e-5);

	// Size: 40 degrees wide at 100 percent, whatever the distance; hud_scale scales the width
	float w1, h1, w2, h2;
	vr_hud_size(2.0f, 100, 16.0f / 9.0f, &w1, &h1);
	CHECK_NEAR(2 * atanf(w1 / 2 / 2.0f), 40.0 * PI / 180.0, 1e-4);
	CHECK_NEAR(w1 / h1, 16.0 / 9.0, 1e-5);
	vr_hud_size(2.0f, 150, 16.0f / 9.0f, &w2, &h2);
	CHECK_NEAR(w2, 1.5f * w1, 1e-5);
	vr_hud_size(3.0f, 100, 16.0f / 9.0f, &w2, &h2);
	CHECK_NEAR(2 * atanf(w2 / 2 / 3.0f), 40.0 * PI / 180.0, 1e-4);
}

static void test_head_roll_does_not_roll_the_hud_camera(void) {
	// The HUD camera is the head centre with the roll removed.
	vr_game_camera_t cam = { .position = {0, 0, 0}, .angles = {0, 0, 0} };
	float q0[4] = {0, 0, 0, 1}, home[3] = {0, 1.6f, 0};
	vr_view_t v[2];
	views_at(home, q0, 0.064f, v);
	vr_pose_t head0;
	vr_head_pose(v, &head0);
	vr_recentre_t rc;
	vr_recentre_from_head(&head0, &rc);

	float roll[4];
	quat_axis('z', 0.6f, roll);
	views_at(home, roll, 0.064f, v);
	vr_pose_t head, level;
	vr_head_pose(v, &head);
	vr_pose_level(&head, &level);
	vr_cam_t hc, ec;
	vr_cam_from_pose(&cam, &rig_default, &rc, &head, &level, &hc);
	vr_cam_from_pose(&cam, &rig_default, &rc, &head, &head, &ec);

	// A point 1000 units right and 1000 ahead (game axes): the rolled eye sees it
	// tilted off the horizon, the HUD camera does not
	float point[3] = {1000, 0, 1000}, ndc_hud[3], ndc_eye[3], proj_sym[16];
	vr_projection_tan(-1, 1, 1, -1, 16, 64000, proj_sym);
	vr_project(hc.view, proj_sym, point, ndc_hud);
	vr_project(ec.view, proj_sym, point, ndc_eye);
	CHECK_NEAR(ndc_hud[0], 1.0, 1e-4);
	CHECK_NEAR(ndc_hud[1], 0.0, 1e-4);
	CHECK(fabsf(ndc_eye[1]) > 0.3f);

	// A point at the quad's right edge (half the width at 2 m) lands on ndc x = 1 with the quad's frustum
	float hud_w, hud_h;
	vr_hud_size(2.0f, 100, 16.0f / 9.0f, &hud_w, &hud_h);
	float tx = hud_w / 2 / 2.0f, ty = hud_h / 2 / 2.0f, hproj[16];
	vr_projection_tan(-tx, tx, ty, -ty, 16, 64000, hproj);
	float edge[3] = { (hud_w / 2) * 128, 0, 2.0f * 128 };
	vr_project(hc.view, hproj, edge, ndc_hud);
	CHECK_NEAR(ndc_hud[0], 1.0, 1e-4);
	CHECK_NEAR(ndc_hud[1], 0.0, 1e-4);
	float corner[3] = { -(hud_w / 2) * 128, (hud_h / 2) * 128, 2.0f * 128 }; // top-left: -X, and up is -Y
	corner[1] = -corner[1];
	vr_project(hc.view, hproj, corner, ndc_hud);
	CHECK_NEAR(ndc_hud[0], -1.0, 1e-4);
	CHECK_NEAR(ndc_hud[1], 1.0, 1e-4);
}

static void test_diag_yaw_turns_the_head_about_its_centre(void) {
	vr_view_t v[2];
	float q[4] = {0, 0, 0, 1}, pos[3] = {0.5f, 1.6f, -1.0f};
	views_at(pos, q, 0.064f, v);
	vr_pose_t before;
	vr_head_pose(v, &before);

	vr_views_apply_yaw(v, PI / 2); // 90 degrees to the left
	vr_pose_t after;
	vr_head_pose(v, &after);
	CHECK_NEAR(after.position[0], before.position[0], 1e-5);
	CHECK_NEAR(after.position[1], before.position[1], 1e-5);
	CHECK_NEAR(after.position[2], before.position[2], 1e-5);
	CHECK_NEAR(vr_yaw_of(after.orientation), PI / 2, 1e-5);
	// the eyes turned with it: facing left, the head's right (+X) is now +... -Z
	CHECK_NEAR(v[1].pose.position[2] - v[0].pose.position[2], -0.064f, 1e-5);
	CHECK_NEAR(v[1].pose.position[0] - v[0].pose.position[0], 0.0, 1e-5);
}

int main(void) {
	RUN(test_game_matrices_restated);
	RUN(test_world_is_y_down_z_forward);
	RUN(test_identity_head_gives_the_games_view);
	RUN(test_head_yaw_right_looks_at_the_cameras_right);
	RUN(test_eyes_are_half_the_ipd_apart_along_the_cameras_right);
	RUN(test_head_translation_scales_the_head_not_the_eyes);
	RUN(test_recentre_absorbs_the_heads_yaw);
	RUN(test_asymmetric_fov_maps_to_the_clip_planes);
	RUN(test_a_near_object_is_further_right_in_the_left_eye);
	RUN(test_hud_pose_follows_yaw_and_pitch_never_roll);
	RUN(test_head_roll_does_not_roll_the_hud_camera);
	RUN(test_diag_yaw_turns_the_head_about_its_centre);
	return SFVR_TEST_RESULT("vr_view");
}
