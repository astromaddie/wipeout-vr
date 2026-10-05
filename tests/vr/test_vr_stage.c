// The command list, the stereo/mono policy (mode switches, recentre, fade,
// panel) and the [vr-perf] feed. No GL, no runtime.

#include <string.h>

#include "sfvr_test.h"
#include "vr_cmd.h"
#include "vr_perf.h"
#include "vr_stage.h"

#define SEC 1000000000LL

static vr_vertex_t vertex(float x) {
	vr_vertex_t v;
	memset(&v, 0, sizeof(v));
	v.pos[0] = x;
	return v;
}

static void push_triangle(vr_cmdlist_t *l, float x, uint8_t flags) {
	vr_vertex_t v[3] = { vertex(x), vertex(x + 1), vertex(x + 2) };
	vr_cmdlist_push_tris(l, v, flags);
}

static void test_cmdlist_orders_and_coalesces(void) {
	vr_cmdlist_t l;
	vr_cmdlist_init(&l);
	vr_cmdlist_reset(&l, 1, false);
	CHECK_INT(l.initial_blend, 1);
	CHECK(!l.initial_cull);

	push_triangle(&l, 0, 0);
	push_triangle(&l, 10, 0);
	push_triangle(&l, 20, 0);
	CHECK_INT(l.cmd_len, 1); // one range
	CHECK_INT(l.cmds[0].u.tris.first, 0);
	CHECK_INT(l.cmds[0].u.tris.count, 3);

	vr_cmd_t *c = vr_cmdlist_push(&l, VR_CMD_DEPTH_WRITE, 0);
	c->u.on = false;
	push_triangle(&l, 30, 0); // a state change ends the range
	CHECK_INT(l.cmd_len, 3);
	CHECK_INT(l.cmds[2].u.tris.first, 3);
	CHECK_INT(l.cmds[2].u.tris.count, 1);

	push_triangle(&l, 40, VR_CMD_FLAG_2D); // another segment is its own range
	CHECK_INT(l.cmd_len, 4);
	CHECK_INT(l.cmds[3].flags, VR_CMD_FLAG_2D);
	CHECK_INT(l.cmds[3].u.tris.first, 4);

	CHECK_INT(l.vert_len, 15);
	CHECK_NEAR(l.verts[4 * 3].pos[0], 40, 0);
	CHECK_INT(l.tris_3d, 4);
	CHECK_INT(l.tris_2d, 1);

	// a sprite is a command of its own
	c = vr_cmdlist_push(&l, VR_CMD_SPRITE, 0);
	c->u.sprite.texture = 7;
	CHECK_INT(l.sprite_count, 1);

	// reset keeps nothing but the storage
	vr_cmdlist_reset(&l, 0, true);
	CHECK_INT(l.cmd_len, 0);
	CHECK_INT(l.vert_len, 0);
	CHECK_INT(l.sprite_count, 0);
	push_triangle(&l, 50, 0);
	CHECK_INT(l.cmds[0].u.tris.first, 0);

	// growth past the first allocation keeps earlier data
	vr_cmdlist_reset(&l, 0, true);
	for (int i = 0; i < 5000; i++) {
		push_triangle(&l, (float)i, 0);
		if (i % 100 == 0) {
			vr_cmdlist_push(&l, VR_CMD_CULL, 0);
		}
	}
	CHECK_INT(l.vert_len, 15000);
	CHECK_NEAR(l.verts[4999 * 3].pos[0], 4999, 0);
	CHECK_INT(l.cmds[0].u.tris.first, 0);
	vr_cmdlist_free(&l);
}

static void test_cmdlist_cameras(void) {
	vr_cmdlist_t l;
	vr_cmdlist_init(&l);
	vr_cmdlist_reset(&l, 0, true);
	vr_camset_t cam;
	memset(&cam, 0, sizeof(cam));
	cam.game_position[0] = 5;
	CHECK_INT(vr_cmdlist_push_cam(&l, &cam), 0);
	cam.game_position[0] = 6;
	CHECK_INT(vr_cmdlist_push_cam(&l, &cam), 1);
	CHECK_NEAR(l.cams[0].game_position[0], 5, 0);
	CHECK_NEAR(l.cams[1].game_position[0], 6, 0);
	vr_cmdlist_free(&l);
}

static void test_fade_curve(void) {
	CHECK_NEAR(vr_fade_alpha(0), 1, 1e-6);
	CHECK_NEAR(vr_fade_alpha(0.249f), 1, 1e-6); // 0.25 s of black
	CHECK_NEAR(vr_fade_alpha(0.25f + 0.175f), 0.5, 1e-5); // then 0.35 s out
	CHECK_NEAR(vr_fade_alpha(0.6f), 0, 1e-6);
	CHECK_NEAR(vr_fade_alpha(5), 0, 1e-6);
}

static vr_view_t eye_at(float yaw, float x) {
	vr_view_t v;
	memset(&v, 0, sizeof(v));
	vr_quat_from_yaw(yaw, v.pose.orientation);
	v.pose.position[0] = x;
	v.pose.position[1] = 1.6f;
	return v;
}

static void test_mode_switch_fades_and_recentres(void) {
	vr_stage_t s;
	vr_stage_init(&s);
	CHECK_INT(s.mode, VR_MODE_NONE);

	// the first frame counts as a switch (fade in from black)
	CHECK(vr_stage_set_mode(&s, VR_MODE_MONO, 1 * SEC));
	CHECK(!vr_stage_set_mode(&s, VR_MODE_MONO, 2 * SEC));
	CHECK_NEAR(vr_stage_fade_alpha(&s, 1 * SEC), 1, 1e-6);

	// mono -> stereo: a new fade, and a recentre pending
	vr_stage_t before = s;
	CHECK(vr_stage_set_mode(&s, VR_MODE_STEREO, 10 * SEC));
	CHECK_INT(s.mode_switches, before.mode_switches + 1);
	CHECK(s.recentre_pending);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC), 1, 1e-6);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC + 249000000), 1, 1e-6);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC + 425000000), 0.5, 1e-3);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC + 700000000), 0, 1e-6);
	CHECK(!s.fade_active);

	// stereo -> mono also fades
	CHECK(vr_stage_set_mode(&s, VR_MODE_MONO, 20 * SEC));
	CHECK_NEAR(vr_stage_fade_alpha(&s, 20 * SEC + 100000000), 1, 1e-6);
}

static void test_recentre_applies_once_at_a_stereo_frame(void) {
	vr_stage_t s;
	vr_stage_init(&s);
	vr_view_t v[2] = { eye_at(0.5f, -0.03f), eye_at(0.5f, 0.03f) };
	vr_stage_set_mode(&s, VR_MODE_STEREO, SEC);

	// nothing is applied until the stereo frame asks
	CHECK(!s.recentre_valid);
	CHECK(vr_stage_apply_recentre(&s, v));
	CHECK_NEAR(s.recentre.yaw, 0.5, 1e-5);
	CHECK_NEAR(s.recentre.origin[1], 1.6, 1e-5);
	CHECK(!s.recentre_pending);

	// a second call in a later frame does nothing: the offset holds while the head turns
	vr_view_t turned[2] = { eye_at(1.2f, -0.03f), eye_at(1.2f, 0.03f) };
	CHECK(!vr_stage_apply_recentre(&s, turned));
	CHECK_NEAR(s.recentre.yaw, 0.5, 1e-5);

	// a request (button, focus gain, reference-space change) re-applies it at the next frame
	vr_stage_request_recentre(&s);
	CHECK(vr_stage_apply_recentre(&s, turned));
	CHECK_NEAR(s.recentre.yaw, 1.2, 1e-5);
	CHECK_INT(s.recentres_applied, 2);

	// a request during mono frames waits for the next stereo frame
	vr_stage_set_mode(&s, VR_MODE_MONO, 5 * SEC);
	vr_stage_request_recentre(&s);
	CHECK(s.recentre_pending);
	vr_stage_set_mode(&s, VR_MODE_STEREO, 6 * SEC);
	CHECK(vr_stage_apply_recentre(&s, v));
	CHECK_NEAR(s.recentre.yaw, 0.5, 1e-5);
	CHECK(!vr_stage_apply_recentre(&s, v));
}

static void test_panel_is_placed_then_held(void) {
	vr_stage_t s;
	vr_stage_init(&s);
	vr_view_t v[2] = { eye_at(0, -0.03f), eye_at(0, 0.03f) };
	vr_pose_t p;

	vr_stage_panel_pose(&s, v, true, 2.0f, &p);
	CHECK_NEAR(p.position[0], 0, 1e-5);
	CHECK_NEAR(p.position[1], 1.6, 1e-5);
	CHECK_NEAR(p.position[2], -2.0, 1e-5);

	// the head turns: the panel stays where it was put
	vr_view_t turned[2] = { eye_at(1.0f, -0.03f), eye_at(1.0f, 0.03f) };
	vr_stage_panel_pose(&s, turned, true, 2.0f, &p);
	CHECK_NEAR(p.position[2], -2.0, 1e-5);

	// a recentre request puts it in front of the head again
	vr_stage_request_recentre(&s);
	vr_stage_panel_pose(&s, turned, true, 2.0f, &p);
	CHECK(p.position[2] > -1.9f);
	CHECK_NEAR(vr_yaw_of(p.orientation), 1.0, 1e-5);

	// with no head pose it is straight ahead of the origin and stays pending
	vr_stage_t t;
	vr_stage_init(&t);
	vr_stage_panel_pose(&t, v, false, 2.0f, &p);
	CHECK_NEAR(p.position[2], -2.0, 1e-5);
	CHECK(t.panel_pending);
}

static void test_perf_line(void) {
	vr_perf_t p;
	vr_perf_init(&p);
	char line[256];
	const float eyes[2] = { 2.2f, 2.1f };
	int emitted = 0;
	int64_t period = 11111111; // 90 Hz
	int64_t display = 100 * SEC;
	for (int i = 0; i < 1000; i++) {
		double now = i * (1.0 / 90.0);
		// frame 300 takes two periods: one missed
		display += (i == 300) ? 2 * period : period;
		if (vr_perf_frame(&p, now, display, period, 1.0f, 4.4f, 0.5f, eyes, -1.0f, line, sizeof(line))) {
			emitted++;
			CHECK(strncmp(line, "[vr-perf] fps=", 14) == 0);
			CHECK(strstr(line, "missed=1") != NULL);
			CHECK(strstr(line, "record=1.00 replay=4.40 hud=0.50") != NULL);
			CHECK(strstr(line, "eye=2.20/2.10ms") != NULL);
			CHECK(strstr(line, "cpu=5.90ms") != NULL);
			CHECK(strstr(line, "gpu=n/a") != NULL);
		}
	}
	CHECK_INT(emitted, 1); // 1000 frames at 90 Hz is 11 s: one 10 s window
}

// timing_gpu: gpu= is the mean of the frames that had a GPU time, and a second line gives each pass
static void test_perf_gpu_line(void) {
	vr_perf_t p;
	vr_perf_init(&p);
	char line[256], gpu[256];
	const float cpu_eyes[2] = { 2.2f, 2.1f };
	const float eyes_a[2] = { 3.0f, 3.2f };
	const float eyes_b[2] = { 4.0f, 4.2f };
	const float none[2] = { 0, 0 };
	int emitted = 0;
	int64_t period = 8333333; // 120 Hz
	int64_t display = 100 * SEC;
	for (int i = 0; i < 1300; i++) {
		double now = i * (1.0 / 120.0);
		display += period;
		// The queries finish a few frames late and not every frame: every other frame has a sample
		float gpu_ms = -1.0f;
		if (i % 2 == 0 && i >= 4) {
			bool b = (i % 4 == 0);
			if (i % 10 == 0) { // some mono frames
				vr_perf_gpu_sample(&p, false, 0, none, 0, 0.5f, 0.5f);
				gpu_ms = 0.5f;
			}
			else {
				vr_perf_gpu_sample(&p, true, 0.1f, b ? eyes_a : eyes_b, 0.3f, 0, 0.1f + (b ? 6.2f : 8.2f) + 0.3f);
				gpu_ms = 0.1f + (b ? 6.2f : 8.2f) + 0.3f;
			}
		}
		if (vr_perf_frame(&p, now, display, period, 1.0f, 4.4f, 0.5f, cpu_eyes, gpu_ms, line, sizeof(line))) {
			emitted++;
			CHECK(strstr(line, "eye=2.20/2.10ms") != NULL); // the CPU per-eye stays
			CHECK(strstr(line, "gpu=n/a") == NULL);
			CHECK(strstr(line, "gpu=") != NULL);
			vr_perf_gpu_line(&p, gpu, sizeof(gpu));
			CHECK(strncmp(gpu, "[vr-perf] gpu eye=", 18) == 0);
			CHECK(strstr(gpu, "eye=3.50/3.70ms") != NULL); // the stereo frames' mean per eye
			CHECK(strstr(gpu, "face=0.10 hud=0.30 mono=0.50") != NULL);
			CHECK(strstr(gpu, "max=8.60ms") != NULL);
			// a window with no GPU frame gives no line at all
			vr_perf_gpu_line(&p, gpu, sizeof(gpu));
			CHECK_STR(gpu, "");
		}
	}
	CHECK_INT(emitted, 1);

	// off: nothing is ever fed, gpu= stays n/a and there is no second line
	vr_perf_init(&p);
	emitted = 0;
	for (int i = 0; i < 1300; i++) {
		display += period;
		if (vr_perf_frame(&p, i * (1.0 / 120.0), display, period, 1.0f, 4.4f, 0.5f, cpu_eyes, -1.0f, line, sizeof(line))) {
			emitted++;
			CHECK(strstr(line, "gpu=n/a") != NULL);
			vr_perf_gpu_line(&p, gpu, sizeof(gpu));
			CHECK_STR(gpu, "");
		}
	}
	CHECK_INT(emitted, 1);
}

int main(void) {
	RUN(test_cmdlist_orders_and_coalesces);
	RUN(test_cmdlist_cameras);
	RUN(test_fade_curve);
	RUN(test_mode_switch_fades_and_recentres);
	RUN(test_recentre_applies_once_at_a_stereo_frame);
	RUN(test_panel_is_placed_then_held);
	RUN(test_perf_line);
	RUN(test_perf_gpu_line);
	return SFVR_TEST_RESULT("vr_stage");
}
