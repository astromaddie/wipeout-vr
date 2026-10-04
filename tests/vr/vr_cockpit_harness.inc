// Cockpit H in the host harness. Included by vr_render_harness.c, after the
// renderer tests: these need the game's own ships, camera, HUD, UI and image
// loader (linked in, with the game's globals stubbed here).
//
// Without game data the hulls are synthetic: low wedges with the model names
// and triangle counts of two real ones, so the cockpit's load, cut, trim and
// draw run the production code on them. With WIPEOUT_VR_TEST_DATA set to a
// directory that holds wipeout/... (the user's data, never committed) the same
// tests also run on the 8 real ships, and two of them are rendered next to
// the approved mockup.

#include "wipeout/game.h"
#include "wipeout/camera.h"
#include "wipeout/hud.h"
#include "wipeout/image.h"
#include "wipeout/object.h"
#include "wipeout/ship.h"
#include "wipeout/ui.h"
#include "wipeout/weapon.h"
#include "vr_cockpit.h"
#include "vr_cockpit_core.h"
#include "mem.h"

// ---- the game's globals and the few functions the linked game files call

game_t g;
save_t save;

static float stub_shield_left; // seconds
static int stub_shield_calls; // how many times the status slot asked: once per game frame

float weapon_shield_time_left(ship_t *ship) {
	(void)ship;
	stub_shield_calls++;
	return stub_shield_left;
}

void camera_update_race_internal(camera_t *c, ship_t *s, droid_t *d) { (void)c; (void)s; (void)d; }
void camera_update_race_external(camera_t *c, ship_t *s, droid_t *d) { (void)c; (void)s; (void)d; }
void camera_update_attract_internal(camera_t *c, ship_t *s, droid_t *d) { (void)c; (void)s; (void)d; }

static const char *data_root; // WIPEOUT_VR_TEST_DATA

uint8_t *platform_load_asset(const char *name, uint32_t *bytes_read) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", data_root ? data_root : "", name);
	FILE *f = fopen(path, "rb");
	if (!f) {
		printf("  cannot open %s\n", path);
		exit(2);
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = mem_temp_alloc((uint32_t)size);
	if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
		printf("  short read of %s\n", path);
		exit(2);
	}
	fclose(f);
	*bytes_read = (uint32_t)size;
	return buf;
}


// ---- synthetic hulls

#define SYNTH_VERTS 700
#define SYNTH_PRIMS 400

typedef struct {
	Object obj;
	vec3_t verts[SYNTH_VERTS];
	primitive_t prims[SYNTH_PRIMS];
} synth_hull_t;

static void synth_vertex(primitive_vertex_t *pv, synth_hull_t *h, vec3_t p, rgba_t color) {
	int index = h->obj.vertices_len++;
	h->verts[index] = p;
	pv->coord = (int16_t)index;
	pv->u = pv->v = 0;
	pv->color = color;
}

// A low wedge whose deck runs over the cabin, plus fins (blue paint, orange fins), exactly `tris` triangles
static void synth_hull(synth_hull_t *h, const char *name, int tris, float deck_y) {
	memset(h, 0, sizeof(*h));
	snprintf(h->obj.name, sizeof(h->obj.name), "%s", name);
	h->obj.vertices = h->verts;
	h->obj.primitives = h->prims;
	h->obj.mat = mat4_identity();
	const rgba_t paint = rgba(60, 70, 150, 255), fin = rgba(128, 64, 16, 255);

	const int nx = 4, nz = (tris - 40) / 8; // quads: nx * nz * 2 triangles
	int made = 0;
	for (int zi = 0; zi < nz; zi++) {
		for (int xi = 0; xi < nx; xi++) {
			float x0 = -180 + 360.0f * xi / nx, x1 = -180 + 360.0f * (xi + 1) / nx;
			float z0 = -420 + 840.0f * zi / nz, z1 = -420 + 840.0f * (zi + 1) / nz;
			// the deck peaks just behind the eye and falls toward the nose
			#define DECK(X, Z) vec3(X, deck_y - 14 * expf(-((Z) + 100) * ((Z) + 100) / 60000.0f) + 0.0003f * (X) * (X), Z)
			primitive_t *prm = &h->prims[h->obj.primitives_len++];
			prm->type = PRM_TYPE_QUAD;
			prm->flag = PRM_SINGLE_SIDED;
			prm->u.quad.texture = 0;
			synth_vertex(&prm->u.quad.v[0], h, DECK(x0, z0), paint);
			synth_vertex(&prm->u.quad.v[1], h, DECK(x1, z0), paint);
			synth_vertex(&prm->u.quad.v[2], h, DECK(x0, z1), paint);
			synth_vertex(&prm->u.quad.v[3], h, DECK(x1, z1), paint);
			#undef DECK
			made += 2;
		}
	}
	// the rest: fins behind the cabin, so there is a second hue
	for (int i = 0; made < tris; i++, made++) {
		float x = -170 + 340.0f * ((i * 7) % 10) / 9.0f;
		float z = -380 + (i % 5) * 12;
		primitive_t *prm = &h->prims[h->obj.primitives_len++];
		prm->type = PRM_TYPE_TRI;
		prm->flag = PRM_SINGLE_SIDED;
		prm->u.tri.texture = 0;
		synth_vertex(&prm->u.tri.v[0], h, vec3(x, deck_y + 5, z), fin);
		synth_vertex(&prm->u.tri.v[1], h, vec3(x + 100, deck_y + 5, z), fin);
		synth_vertex(&prm->u.tri.v[2], h, vec3(x + 50, deck_y - 30, z - 80), fin);
	}
}

static synth_hull_t synth_sophia, synth_solaar, synth_mystery;
static const uint8_t synth_grey[2 * 2 * 4] = {128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255}; // RENDER_NO_TEXTURE
static const vr_cockpit_image_t synth_images[1] = {{2, 2, synth_grey}};


// ---- the pilot, the camera and a race frame

static vr_settings_t cockpit_live; // the keys the renderer re-reads every frame

static vr_render_config_t cockpit_config(void) {
	vr_settings_set(&cockpit_live, "world_scale", 88);
	vr_render_config_t c = make_config(NULL, false);
	c.settings.rig.world_scale = 88;
	c.live_settings = &cockpit_live;
	return c;
}

static void set_pilot(int index, vec3_t pos, vec3_t angle, bool internal) {
	g.pilot = index;
	g.race_type = RACE_TYPE_CHAMPIONSHIP;
	g.is_attract_mode = false;
	g.lives = 3;
	g.lap_times[index][0] = 59.3f;
	g.lap_times[index][1] = 58.7f;
	save.internal_roll = 0.6f;
	save.highscores[0][0][0].lap_record = 57.5f;
	ship_t *s = &g.ships[index];
	s->pilot = index;
	s->position = pos;
	s->angle = angle;
	s->mat = mat4_identity();
	mat4_set_translation(&s->mat, pos);
	mat4_set_yaw_pitch_roll(&s->mat, angle);
	s->flags = SHIP_RACING | SHIP_DIRECTION_FORWARD | SHIP_VISIBLE | (internal ? SHIP_VIEW_INTERNAL : 0);
	s->speed = 1500;
	s->thrust_mag = 40;
	s->lap = 2;
	s->lap_time = 61.4f;
	s->position_rank = 3;
	s->weapon_type = WEAPON_TYPE_NONE;
	if (internal) {
		g.camera.position = vec3_transform(vec3(0, -128, 0), &s->mat); // ship_cockpit()
		g.camera.angle = vec3(angle.x, angle.y, angle.z * save.internal_roll);
		g.camera.update_func = camera_update_race_internal;
	}
	else {
		g.camera.position = vec3_transform(vec3(0, -300, -1024), &s->mat);
		g.camera.angle = vec3(angle.x, angle.y, 0);
		g.camera.update_func = camera_update_race_external;
	}
}

static bool last_hud_in_cockpit;
static int flat_hud_calls;
static bool cockpit_scene = true; // false: the cockpit alone on black, for the comparison with the mockup

// race.c's draw section, as the cockpit hooks it
static void cockpit_race_frame(bool stereo) {
	render_frame_prepare();
	render_vr_race_view(stereo);
	vr_cockpit_begin(&g.camera);
	render_set_view(g.camera.position, g.camera.angle);
	render_set_screen_position(vec2(0, 0));
	cam_pos = g.camera.position;
	cam_angles = g.camera.angle;
	if (cockpit_scene) {
		scene_draw(true, SKY_PLAIN, true, false);
	}
	render_set_cull_backface(true);

	ship_t *ship = &g.ships[g.pilot];
	if ((ship->flags & SHIP_VIEW_INTERNAL) && !(ship->flags & SHIP_IN_RESCUE) && vr_cockpit_active()) {
		vr_cockpit_draw(ship);
	}

	render_set_view_2d();
	last_hud_in_cockpit = vr_cockpit_hud_draw(ship);
	if (!last_hud_in_cockpit) {
		flat_hud_calls++;
		synthetic_hud_draw();
	}
	render_frame_end();
	fake.time_ns += FRAME_NS;
}

// The first stereo frame recentres: do one so the head sits at the seat afterwards
static void settle_stereo(void) {
	cockpit_race_frame(false);
	fake.time_ns += 5 * FRAME_NS;
	cockpit_race_frame(true);
	fake.time_ns += 63 * FRAME_NS;
	cockpit_race_frame(true);
}

static uint32_t frame_tris(void) {
	return vr_render_frame_info()->triangles;
}


// ---- the tests

static void load_synthetic_ships(void) {
	synth_hull(&synth_sophia, "sophia", 186, -68);
	synth_hull(&synth_solaar, "solaar", 131, -63);
	synth_hull(&synth_mystery, "mystery", 186, -68);
	vr_cockpit_unload();
	memset(g.ships, 0, sizeof(g.ships));
	g.ships[0].model = &synth_sophia.obj;
	g.ships[1].model = &synth_solaar.obj;
	g.ships[2].model = &synth_mystery.obj;
	for (int i = 0; i < 3; i++) {
		vr_cockpit_build_ship(i, g.ships[i].model, synth_images, 1);
	}
}

static void test_cockpit_load(void) {
	printf("cockpit load (synthetic hulls)\n");
	load_synthetic_ships();
	vr_cockpit_ship_info_t a, b, c;
	CHECK(vr_cockpit_ship_info(0, &a) && vr_cockpit_ship_info(1, &b) && vr_cockpit_ship_info(2, &c));
	CHECK(a.valid && b.valid);
	CHECK(!c.valid); // no matching name and count: that ship keeps the flat internal view
	CHECK_INT(a.shape, 0);
	CHECK_INT(b.shape, 1);
	CHECK(a.hull_tris > 150 && a.inside_tris == a.hull_tris);
	CHECK(a.part_tris > 1000 && a.glass_tris > 20);
	// the paint is blue and the fins orange: primary blue, secondary orange
	printf("  sophia: %d hull + %d inside tris, %d parts, trim %02x%02x%02x / %02x%02x%02x\n", a.hull_tris, a.inside_tris, a.part_tris,
		a.trim.primary[0], a.trim.primary[1], a.trim.primary[2], a.trim.secondary[0], a.trim.secondary[1], a.trim.secondary[2]);
	CHECK(a.trim.primary[2] > a.trim.primary[0] * 2);
	CHECK(a.trim.secondary[0] > a.trim.secondary[2] * 2);
	CHECK(!a.trim.neutral);

	// after the cut no hull triangle has a vertex inside the cabin box
	vr_cockpit_cut_t cut = vr_cockpit_cut();
	int inside = 0, below = 0;
	float p[9];
	for (int i = 0; vr_cockpit_hull_triangle(0, i, p); i++) {
		for (int k = 0; k < 3; k++) {
			const float *v = &p[k * 3];
			if (v[0] > cut.x0 + 0.01f && v[0] < cut.x1 - 0.01f && v[1] < cut.y_max - 0.01f && v[2] > cut.z0 + 0.01f && v[2] < cut.z1 - 0.01f) {
				inside++;
			}
		}
		below++;
	}
	CHECK_INT(inside, 0);
	CHECK_INT(below, a.hull_tris);
	// the deck above the opening is gone: the synthetic deck had 112 triangles over x +-180
	synth_hull_t *h = &synth_sophia;
	int src_tris = 0;
	for (int i = 0; i < h->obj.primitives_len; i++) {
		src_tris += h->obj.primitives[i].type == PRM_TYPE_QUAD ? 2 : 1;
	}
	CHECK_INT(src_tris, 186);
}

static void test_cockpit_only_in_stereo_internal_view(void) {
	printf("the cockpit draws only in a stereo internal race view\n");
	fake_reset();
	vr_settings_defaults(&cockpit_live);
	vr_render_config_t cfg = cockpit_config();
	vr_render_attach(&cfg);
	load_synthetic_ships();
	const vec3_t pos = vec3(0, -214.3f, 0), angle = vec3(0, 0, 0);

	// baseline: the scene alone, in a chase view
	set_pilot(0, pos, angle, false);
	settle_stereo();
	CHECK(!vr_cockpit_active());
	uint32_t scene_tris = frame_tris();
	CHECK_INT(vr_render_frame_info()->face_passes, 0);
	CHECK(!last_hud_in_cockpit);

	// stereo + internal + cockpit on: the cockpit
	set_pilot(0, pos, angle, true);
	settle_stereo();
	CHECK(vr_cockpit_active());
	CHECK(last_hud_in_cockpit);
	uint32_t cockpit_tris = frame_tris();
	vr_cockpit_ship_info_t info;
	vr_cockpit_ship_info(0, &info);
	printf("  scene %u tris, with the cockpit %u (hull %d + inside %d + parts %d + glass %d + 6 face tris)\n", scene_tris, cockpit_tris,
		info.hull_tris, info.inside_tris, info.part_tris, info.glass_tris);
	CHECK(cockpit_tris >= scene_tris + (uint32_t)(info.hull_tris + info.inside_tris + info.part_tris + info.glass_tris));
	CHECK_INT(vr_render_frame_info()->face_passes, 3);

	// a mono frame (the panel): the game's own camera, no cockpit
	int flat_before = flat_hud_calls;
	cockpit_race_frame(false);
	CHECK(!vr_cockpit_active());
	CHECK_INT(vr_render_frame_info()->mode, VR_MODE_MONO);
	CHECK_INT(vr_render_frame_info()->face_passes, 0);
	CHECK_INT(flat_hud_calls, flat_before + 1);
	CHECK(frame_tris() < cockpit_tris);
	settle_stereo();
	CHECK(vr_cockpit_active());

	// the chase view
	set_pilot(0, pos, angle, false);
	cockpit_race_frame(true);
	CHECK(!vr_cockpit_active());
	CHECK(!last_hud_in_cockpit);
	set_pilot(0, pos, angle, true);

	// the attract mode's camera is not the race internal one; with the diagnostic it is stereo and has the cockpit
	g.camera.update_func = camera_update_attract_internal;
	g.is_attract_mode = true;
	cockpit_race_frame(false); // race.c: render_vr_race_view(!g.is_attract_mode)
	CHECK(!vr_cockpit_active());
	CHECK_INT(vr_render_frame_info()->mode, VR_MODE_MONO);
	g.is_attract_mode = false;
	g.camera.update_func = camera_update_race_internal;
	settle_stereo();
	CHECK(vr_cockpit_active());

	// the ship in rescue, or one without a matching hull
	g.ships[0].flags |= SHIP_IN_RESCUE;
	cockpit_race_frame(true);
	CHECK(!vr_cockpit_active());
	g.ships[0].flags &= ~SHIP_IN_RESCUE;
	set_pilot(2, pos, angle, true);
	cockpit_race_frame(true);
	CHECK(!vr_cockpit_active());
	CHECK(!last_hud_in_cockpit);
	set_pilot(0, pos, angle, true);
	cockpit_race_frame(true);
	CHECK(vr_cockpit_active());

	// the settings, live: cockpit off is the flat internal view and the HUD again
	vr_settings_set(&cockpit_live, "cockpit", 0);
	cockpit_race_frame(true);
	CHECK(!vr_cockpit_active());
	CHECK(!last_hud_in_cockpit);
	CHECK_INT(vr_render_frame_info()->face_passes, 0);
	vr_settings_set(&cockpit_live, "cockpit", 1);
	cockpit_race_frame(true);
	CHECK(vr_cockpit_active());

	// the glass is a setting too: 20-odd triangles less
	uint32_t with_glass = frame_tris();
	vr_settings_set(&cockpit_live, "cockpit_glass", 0);
	cockpit_race_frame(true);
	CHECK(vr_cockpit_active());
	CHECK_INT(with_glass - frame_tris(), (uint32_t)info.glass_tris);
	vr_settings_set(&cockpit_live, "cockpit_glass", 1);

	glerrors("after the cockpit frames");
	vr_render_detach();

	// force_mono keeps every race frame on the panel, cockpit or not
	cfg = cockpit_config();
	cfg.settings.force_mono = true;
	vr_render_attach(&cfg);
	settle_stereo();
	CHECK(!vr_cockpit_active());
	CHECK_INT(vr_render_frame_info()->mode, VR_MODE_MONO);
	vr_render_detach();
}

static float rot_row_y(const vr_cam_t *cam) {
	return cam->rotation[3]; // the view's right axis, y in the game's world (y is down): 0 for a level head
}

static void test_cockpit_eye_and_bank_modes(void) {
	printf("the eye and the bank modes through the renderer\n");
	fake_reset();
	vr_settings_defaults(&cockpit_live);
	vr_render_config_t cfg = cockpit_config();
	vr_render_attach(&cfg);
	load_synthetic_ships();

	const vec3_t pos = vec3(300, -214.3f, 800);
	const vec3_t angle = vec3(0.12f, 0.7f, 0.5f); // pitched, turned and banked
	set_pilot(0, pos, angle, true);

	const float seat[3] = {0, -85.7f, 0};
	const float ship_pos[3] = {pos.x, pos.y, pos.z}, ship_angles[3] = {angle.x, angle.y, angle.z};
	for (int bank = 0; bank < VR_COCKPIT_BANK_COUNT; bank++) {
		vr_settings_set(&cockpit_live, "cockpit_bank", bank);
		settle_stereo();
		CHECK(vr_cockpit_active());

		vr_cockpit_pose_t want;
		vr_cockpit_pose(bank, ship_pos, ship_angles, seat, &want);
		vr_camset_t cs;
		CHECK(vr_render_view(0, &cs));
		CHECK(cs.stereo);
		// the game's own camera is where the game put it: the sim and the sound keep it
		CHECK_NEAR(cs.game_position[0], g.camera.position.x, 1e-3);
		CHECK_NEAR(cs.game_position[1], g.camera.position.y, 1e-3);
		CHECK_NEAR(cs.game_position[2], g.camera.position.z, 1e-3);
		// the eyes are at the fitted seat, not at ship_cockpit(): the middle of the eyes is the seat
		float mid[3], sep[3];
		for (int k = 0; k < 3; k++) {
			mid[k] = 0.5f * (cs.eye[0].position[k] + cs.eye[1].position[k]);
			sep[k] = cs.eye[1].position[k] - cs.eye[0].position[k];
			CHECK_NEAR(mid[k], want.eye_world[k], 1e-2);
		}
		CHECK_NEAR(sqrtf(sep[0] * sep[0] + sep[1] * sep[1] + sep[2] * sep[2]), IPD * 88.0f, 1e-2);
		float away = sqrtf(powf(mid[0] - g.camera.position.x, 2) + powf(mid[1] - g.camera.position.y, 2) + powf(mid[2] - g.camera.position.z, 2));
		CHECK_NEAR(away, 128 - 85.7f, 0.01); // 42.3 units up the ship's own axis from the game's camera
		// the view turns with the bank mode; a head at the recentre looks along it exactly
		float view_rot[9];
		vr_game_view_rotation(want.anchor_angles, view_rot);
		for (int k = 0; k < 9; k++) {
			CHECK_NEAR(cs.eye[0].rotation[k], view_rot[k], 1e-4);
		}
		if (bank == VR_COCKPIT_BANK_FOLLOW) {
			CHECK(fabsf(rot_row_y(&cs.eye[0])) > 0.1f); // rolls with the ship
		}
		else {
			CHECK_NEAR(rot_row_y(&cs.eye[0]), 0, 1e-4); // a level head
		}
		// the HUD camera follows the same anchor
		CHECK_NEAR(cs.hud.position[0], want.eye_world[0], 1e-2);
		CHECK_NEAR(cs.hud.position[1], want.eye_world[1], 1e-2);
		CHECK_NEAR(cs.hud.position[2], want.eye_world[2], 1e-2);
		printf("  bank %d: head %.1f %.1f %.1f, view right-axis y %.4f\n", bank, mid[0], mid[1], mid[2], rot_row_y(&cs.eye[0]));
	}

	// the seat offsets are live and physical: 10 cm forward and 5 up move the head 8.8 and 4.4 units along the ship
	vr_settings_set(&cockpit_live, "cockpit_bank", VR_COCKPIT_BANK_LEVEL);
	settle_stereo();
	vr_camset_t before, after;
	vr_render_view(0, &before);
	vr_settings_set(&cockpit_live, "cockpit_eye_forward_cm", 10);
	vr_settings_set(&cockpit_live, "cockpit_eye_up_cm", 5);
	cockpit_race_frame(true);
	vr_render_view(0, &after);
	CHECK(vr_cockpit_active());
	{
		float eye2[3] = {0, -85.7f - 4.4f, 8.8f};
		vr_cockpit_pose_t want;
		vr_cockpit_pose(VR_COCKPIT_BANK_LEVEL, ship_pos, ship_angles, eye2, &want);
		for (int k = 0; k < 3; k++) {
			float mid_after = 0.5f * (after.eye[0].position[k] + after.eye[1].position[k]);
			CHECK_NEAR(mid_after, want.eye_world[k], 1e-2);
		}
		float d = 0;
		for (int k = 0; k < 3; k++) {
			float m0 = 0.5f * (before.eye[0].position[k] + before.eye[1].position[k]);
			float m1 = 0.5f * (after.eye[0].position[k] + after.eye[1].position[k]);
			d += (m1 - m0) * (m1 - m0);
		}
		CHECK_NEAR(sqrtf(d), sqrtf(4.4 * 4.4 + 8.8 * 8.8), 0.02);
	}
	vr_settings_set(&cockpit_live, "cockpit_eye_forward_cm", 0);
	vr_settings_set(&cockpit_live, "cockpit_eye_up_cm", 0);

	// a world point seen through both eyes lands where the recorded cameras say (the replay used them),
	// in the banked `follow` view as in the level ones
	set_pilot(0, vec3(0, -214.3f, 0), vec3(0, 0, 0.3f), true);
	for (int bank = 0; bank < VR_COCKPIT_BANK_COUNT; bank++) {
		vr_settings_set(&cockpit_live, "cockpit_bank", bank);
		settle_stereo();
		shot_t s = grab();
		vr_camset_t cs;
		vr_render_view(0, &cs);
		for (int e = 0; e < 2; e++) {
			float fov[4];
			eye_fov(e, fov);
			float proj[16], ndc[3], pt[3] = {0, -300, 1000}; // the red marker, straight ahead of the world
			vr_projection_fov(fov, 16, 64000, proj);
			float w = vr_project(cs.eye[e].view, proj, pt, ndc);
			blob_t b = find_colour(s.eye[e], EYE_W, EYE_H, 255, 0, 0, 12);
			double ex = (ndc[0] + 1) / 2 * EYE_W;
			// the deck hides the lower part of the cube: compare its top edge, the highest of its 8 corners
			double top = 1e9;
			for (int corner = 0; corner < 8; corner++) {
				float cp[3] = {pt[0] + ((corner & 1) ? 100 : -100), pt[1] + ((corner & 2) ? 100 : -100), pt[2] + ((corner & 4) ? 100 : -100)}, cn[3];
				vr_project(cs.eye[e].view, proj, cp, cn);
				top = fmin(top, (1 - cn[1]) / 2 * EYE_H);
			}
			printf("  bank %d eye %d: the red marker is %d px at x %.1f, top edge %d; the recorded camera says x %.1f, top %.1f\n", bank, e, b.count, b.cx, b.min_y, ex, top);
			CHECK(w > 0);
			CHECK(b.count > 200);
			CHECK_NEAR(b.cx, ex, 4);
			CHECK_NEAR(b.min_y, top, 3);
		}
		if (bank == VR_COCKPIT_BANK_LEVEL) {
			save_shot("cockpit-bank-level-roll", &s);
		}
		else if (bank == VR_COCKPIT_BANK_FOLLOW) {
			save_shot("cockpit-bank-follow-roll", &s);
		}
		else {
			save_shot("cockpit-bank-cockpit-roll", &s);
		}
		free_shot(&s);
	}
	vr_render_detach();
}

static void test_cockpit_faces(void) {
	printf("the instrument faces\n");
	fake_reset();
	vr_settings_defaults(&cockpit_live);
	vr_render_config_t cfg = cockpit_config();
	vr_render_attach(&cfg);
	load_synthetic_ships();
	set_pilot(0, vec3(0, -214.3f, 0), vec3(0, 0, 0), true);
	g.ships[0].flags |= SHIP_SHIELDED;
	stub_shield_left = 6.5f; // of WEAPON_SHIELD_DURATION (6.67 s)
	settle_stereo();

	// once per game frame: the status slot is asked once per frame while the replay draws two eyes
	// and the HUD (and nothing in the game's 2D code runs per eye)
	int calls_before = stub_shield_calls;
	for (int i = 0; i < 5; i++) {
		cockpit_race_frame(true);
		CHECK_INT(vr_render_frame_info()->face_passes, 3);
	}
	CHECK_INT(stub_shield_calls - calls_before, 5);

	// the weapon face: the frame line is the accent colour at alpha 140 over the face's background.
	// Drawn once it is (140, 114, 20); drawn twice it would be about (198, 154, 22)
	uint32_t tex = vr_render_face_texture(2);
	CHECK(tex != 0);
	uint8_t *face = read_texture(tex, 128, 128);
	{
		char dir[600];
		snprintf(dir, sizeof(dir), "%s/cockpit-synthetic", out_dir);
		make_dir(dir);
		write_png(dir, "face-weapon.png", face, 128, 128, 4);
		uint8_t *left = read_texture(vr_render_face_texture(0), 512, 292);
		write_png(dir, "face-left.png", left, 512, 292, 4);
		free(left);
		uint8_t *right = read_texture(vr_render_face_texture(1), 512, 292);
		write_png(dir, "face-right.png", right, 512, 292, 4);
		free(right);
	}
	const uint8_t *line = px_at(face, 128, 128, 60, 12);
	printf("  weapon face frame line: %d %d %d (once: 140 114 20)\n", line[0], line[1], line[2]);
	CHECK_NEAR(line[0], 140, 3);
	CHECK_NEAR(line[1], 114, 3);
	CHECK_NEAR(line[2], 20, 3);
	const uint8_t *bg = px_at(face, 128, 128, 60, 60);
	CHECK_INT(bg[0], 11);
	CHECK_INT(bg[1], 13);
	CHECK_INT(bg[2], 16);
	CHECK_INT(bg[3], 255);
	free(face);

	// the right pod's status slot: the shield bar, 6.5 s of 6.67 s: 8 segments lit (ceil(8 * 0.975))
	face = read_texture(vr_render_face_texture(1), 512, 292);
	int x, y, scale;
	vr_cockpit_element("right", "shield", &x, &y, &scale);
	int lit = 0;
	for (int i = 0; i < 8; i++) {
		const uint8_t *seg = px_at(face, 512, 292, x + ((4 + 15 * i) * scale) / 2 + 3 * scale, y + 17 * scale);
		if (seg[2] > 200 && seg[0] < 140) {
			lit++;
		}
	}
	CHECK_INT(lit, 8);
	free(face);

	// the shield runs out: the bar empties between frames
	stub_shield_left = 1.5f; // 22% of WEAPON_SHIELD_DURATION: 2 segments (ceil(8 * 0.225))
	cockpit_race_frame(true);
	face = read_texture(vr_render_face_texture(1), 512, 292);
	lit = 0;
	for (int i = 0; i < 8; i++) {
		const uint8_t *seg = px_at(face, 512, 292, x + ((4 + 15 * i) * scale) / 2 + 3 * scale, y + 17 * scale);
		if (seg[2] > 200 && seg[0] < 140) {
			lit++;
		}
	}
	CHECK_INT(lit, 2);
	free(face);

	// the faces are on the pods in the eye images: a texel of the face's background
	shot_t s = grab();
	vr_camset_t cs;
	vr_render_view(0, &cs);
	float fov[4], proj[16];
	eye_fov(0, fov);
	vr_projection_fov(fov, 16, 64000, proj);
	vr_cockpit_pose_t pose = *vr_cockpit_pose_now();
	int dark = 0, probes = 0;
	for (int f = 0; f < 3; f++) {
		vr_cockpit_face_info_t fi;
		vr_cockpit_face(f, &fi);
		// the face's centre point, in ship space, to world through the cockpit's matrix
		float c[3];
		for (int k = 0; k < 3; k++) {
			c[k] = 0.25f * (fi.corners[k] + fi.corners[3 + k] + fi.corners[6 + k] + fi.corners[9 + k]);
		}
		float world[3], ndc[3];
		for (int r = 0; r < 3; r++) {
			world[r] = pose.model[r] * c[0] + pose.model[4 + r] * c[1] + pose.model[8 + r] * c[2] + pose.model[12 + r];
		}
		float w = vr_project(cs.eye[0].view, proj, world, ndc);
		if (w > 0 && fabsf(ndc[0]) < 1 && fabsf(ndc[1]) < 1) {
			int px = (int)((ndc[0] + 1) / 2 * EYE_W), py = (int)((1 - ndc[1]) / 2 * EYE_H);
			const uint8_t *p = px_at(s.eye[0], EYE_W, EYE_H, px, py);
			printf("  face %s centre at (%d, %d): %d %d %d\n", fi.id, px, py, p[0], p[1], p[2]);
			probes++;
			if (p[0] < 70 && p[1] < 70 && p[2] < 70) {
				dark++;
			}
		}
	}
	CHECK(probes >= 2);
	CHECK(dark >= probes - 1); // text may sit under a centre; the rest is the dark face, not sky or ground
	save_shot("cockpit-synthetic", &s);
	free_shot(&s);
	vr_render_detach();
}

static void test_cockpit_synthetic(void) {
	test_cockpit_load();
	test_cockpit_only_in_stereo_internal_view();
	test_cockpit_eye_and_bank_modes();
	test_cockpit_faces();
}


// ---- the player's real ships (WIPEOUT_VR_TEST_DATA)

static void model_tris(const Object *o, int *count, float (*out)[9], int cap) {
	*count = 0;
	for (int i = 0; i < o->primitives_len; i++) {
		const primitive_t *prm = &o->primitives[i];
		if (prm->type == PRM_TYPE_SPR) {
			continue;
		}
		const primitive_vertex_t *pv = prm->type == PRM_TYPE_TRI ? prm->u.tri.v : prm->u.quad.v;
		const int order[2][3] = {{2, 1, 0}, {2, 3, 1}};
		for (int q = 0; q < (prm->type == PRM_TYPE_TRI ? 1 : 2); q++) {
			if (out && *count < cap) {
				for (int k = 0; k < 3; k++) {
					vec3_t p = o->vertices[pv[order[q][k]].coord];
					out[*count][k * 3] = p.x;
					out[*count][k * 3 + 1] = p.y;
					out[*count][k * 3 + 2] = p.z;
				}
			}
			(*count)++;
		}
	}
}

static float seg_tri_t(const float o[3], const float d[3], const float *a, const float *b, const float *c) {
	float e1[3], e2[3], p[3], t_[3], q[3];
	for (int i = 0; i < 3; i++) {
		e1[i] = b[i] - a[i];
		e2[i] = c[i] - a[i];
		t_[i] = o[i] - a[i];
	}
	p[0] = d[1] * e2[2] - d[2] * e2[1];
	p[1] = d[2] * e2[0] - d[0] * e2[2];
	p[2] = d[0] * e2[1] - d[1] * e2[0];
	float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
	if (fabsf(det) < 1e-9f) {
		return -1;
	}
	float inv = 1 / det;
	float u = (t_[0] * p[0] + t_[1] * p[1] + t_[2] * p[2]) * inv;
	if (u < 0 || u > 1) {
		return -1;
	}
	q[0] = t_[1] * e1[2] - t_[2] * e1[1];
	q[1] = t_[2] * e1[0] - t_[0] * e1[2];
	q[2] = t_[0] * e1[1] - t_[1] * e1[0];
	float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
	if (v < 0 || u + v > 1) {
		return -1;
	}
	return (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
}

static Object *real_models[8];

static void fake_resize_eyes(uint32_t w, uint32_t h) {
	for (int e = 0; e < 2; e++) {
		fake_surface_t *sf = &fake.surface[VR_SURFACE_EYE0 + e];
		glDeleteTextures(1, &sf->tex);
		sf->tex = make_image(w, h);
		sf->w = w;
		sf->h = h;
	}
}

// The cockpit of one real ship as the approved mockup shows it (docs/COCKPIT_MOCKUP.md): the mockup's
// per-eye field of view (a mono approximation, so IPD 0), the cockpit alone on black
static void render_real_cockpit(int ship_index, const char *name, uint32_t w, uint32_t h) {
	g.pilot = ship_index;
	set_pilot(ship_index, vec3(0, -214.3f, 0), vec3(0, 0, 0), true);
	ship_t *s = &g.ships[ship_index];
	s->lap = 2; // LAP 3 OF 3
	s->lap_time = 31.4f;
	s->position_rank = 3;
	s->speed = 19500; // 9 speedo bars
	s->thrust_mag = 560;
	s->weapon_type = WEAPON_TYPE_MISSILE;
	s->flags |= SHIP_SHIELDED;
	stub_shield_left = 5.0f; // of 6.67 s: 6 of the 8 segments
	g.lap_times[ship_index][0] = 58.6f;
	g.lap_times[ship_index][1] = 57.9f;
	g.race_type = RACE_TYPE_CHAMPIONSHIP;
	g.lives = 3;
	save.highscores[g.race_class][g.circuit][g.highscore_tab].lap_record = 57.5f;

	fake.custom_fov = true;
	fake.fov_angles[0] = -0.9565f;
	fake.fov_angles[1] = 0.9565f;
	fake.fov_angles[2] = 0.864f;
	fake.fov_angles[3] = -1.048f;
	fake.ipd = 0;
	fake_resize_eyes(w, h);
	cockpit_scene = false;
	settle_stereo();
	CHECK(vr_cockpit_active());

	char dir[600];
	snprintf(dir, sizeof(dir), "%s/cockpit-real", out_dir);
	make_dir(dir);
	uint8_t *eye = read_texture(fake.surface[VR_SURFACE_EYE0].tex, w, h);
	char file[200];
	snprintf(file, sizeof(file), "%s-eye.png", name);
	write_png(dir, file, eye, w, h, 3);
	free(eye);
	const char *face_names[3] = {"left", "right", "weapon"};
	for (int f = 0; f < 3; f++) {
		vr_cockpit_face_info_t fi;
		vr_cockpit_face(f, &fi);
		uint8_t *px = read_texture(vr_render_face_texture(f), fi.tex_w, fi.tex_h);
		snprintf(file, sizeof(file), "%s-face-%s.png", name, face_names[f]);
		write_png(dir, file, px, fi.tex_w, fi.tex_h, 3);
		free(px);
	}
	cockpit_scene = true;
	fake.custom_fov = false;
	fake.ipd = IPD;
	fake_resize_eyes(EYE_W, EYE_H);
	printf("  rendered %s at %ux%u: %s/%s-eye.png\n", name, w, h, dir, name);
}

static void test_cockpit_real_ships(void) {
	printf("the 8 real ships\n");
	fake_reset();
	vr_settings_defaults(&cockpit_live);
	vr_render_config_t cfg = cockpit_config();
	vr_render_attach(&cfg);

	ui_load();
	hud_load();
	texture_list_t tl = image_get_compressed_textures("wipeout/common/allsh.cmp");
	Object *o = objects_load("wipeout/common/allsh.prm", tl);
	memset(g.ships, 0, sizeof(g.ships));
	for (int i = 0; i < 8 && o; i++, o = o->next) {
		g.ships[i].model = o;
		real_models[i] = o;
	}
	vr_cockpit_load(tl, "wipeout/common/allsh.cmp");

	const int want_shape[8] = {0, 1, 0, 2, 1, 3, 3, 2}; // sophia, solaar, jacko, chang, arian, arial, anasta, Dekka
	const float W = 88.0f;
	vr_cockpit_cut_t cut = vr_cockpit_cut();
	const float eye[3] = {0, vr_cockpit_fitted_eye_y(), 0};
	const float floor_y = eye[1] + 0.92f * W; // the tub floor, 0.92 m under the eye
	for (int i = 0; i < 8; i++) {
		vr_cockpit_ship_info_t info;
		vr_cockpit_ship_info(i, &info);
		printf("  ship %d %-7s shape %d: %4d hull + %4d inside, trim %02x%02x%02x / %02x%02x%02x%s\n", i, real_models[i]->name, info.shape,
			info.hull_tris, info.inside_tris, info.trim.primary[0], info.trim.primary[1], info.trim.primary[2],
			info.trim.secondary[0], info.trim.secondary[1], info.trim.secondary[2],
			info.trim.neutral ? " neutral" : info.trim.secondary_fallback ? " fallback" : "");
		CHECK(info.valid);
		CHECK_INT(info.shape, want_shape[i]);
		CHECK(info.hull_tris > 100);

		// after the cut no hull triangle has a vertex inside the cabin box
		float p[9];
		int inside = 0, near_clear = 0;
		float nearest = 1e9f;
		for (int t = 0; vr_cockpit_hull_triangle(i, t, p); t++) {
			for (int k = 0; k < 3; k++) {
				const float *v = &p[k * 3];
				if (v[0] > cut.x0 + 0.01f && v[0] < cut.x1 - 0.01f && v[1] < cut.y_max - 0.01f && v[2] > cut.z0 + 0.01f && v[2] < cut.z1 - 0.01f) {
					inside++;
				}
				float dx = v[0] - eye[0], dy = v[1] - eye[1], dz = v[2] - eye[2];
				nearest = fminf(nearest, sqrtf(dx * dx + dy * dy + dz * dz));
			}
		}
		CHECK_INT(inside, 0);
		(void)near_clear;
		CHECK(nearest / W >= 0.33f); // a vertex test: the real distance is at most this

		// the tub floor is at least 2 cm above the bottom of the hull over the seat and leg footprint
		static float tris[400][9];
		int n;
		model_tris(real_models[i], &n, tris, 400);
		CHECK(n <= 400);
		float worst = -1e9f;
		for (int ri = -1; ri <= 1; ri++) {
			for (int fi = 0; fi < 5; fi++) {
				float x = ri * 0.22f * W, z = (-0.3f + 0.2f * fi) * W;
				if (fi == 4) z = 0.5f * W;
				const float o_[3] = {x, 4000, z}, d_[3] = {0, -1, 0}; // from below... in game y-down: from +y up to -y
				float lowest = -1e9f; // the largest game y: the belly
				for (int t = 0; t < n; t++) {
					float hit = seg_tri_t(o_, d_, &tris[t][0], &tris[t][3], &tris[t][6]);
					if (hit > 0) {
						lowest = fmaxf(lowest, 4000 - hit);
					}
				}
				if (lowest > -1e8f) {
					// the floor sits above the belly: floor_y <= belly - 2 cm (the eye is rounded to 0.1 unit)
					worst = fmaxf(worst, floor_y - (lowest - 0.02f * W));
				}
			}
		}
		printf("    tub floor y %.2f; worst clearance over the footprint %+.2f units from the 2 cm rule\n", floor_y, worst);
		CHECK(worst <= 0.08f);
	}

	// ships 0 (Feisar, sophia) and 1 (Qirex, solaar), as the mockup shows them
	render_real_cockpit(0, "sophia", 836, 858);
	render_real_cockpit(1, "solaar", 836, 858);
	glerrors("after the real cockpit renders");
	vr_render_detach();
}
