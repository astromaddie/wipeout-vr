// Cockpit H: the table, the hull match and cut, the trim rule on synthetic
// textures, the pre-shaded parts, the eye and the three bank modes. The bank
// maths is checked against the game's own matrices (src/types.c is linked in as
// the oracle). No GL, no window, no game data.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sfvr_test.h"
#include "types.h"
#include "vr_cockpit_core.h"
#include "vr_math.h"
#include "vr_stage.h"

float rand_float(float min, float max) { // types.c's vec3_rand wants it; unused here
	return min + max * 0;
}

#define WS 88.0f


// -----------------------------------------------------------------------------
// Small geometry helpers

static void sub3(const float *a, const float *b, float *out) {
	for (int i = 0; i < 3; i++) {
		out[i] = a[i] - b[i];
	}
}

static float dot3(const float *a, const float *b) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3(const float *a, const float *b, float *out) {
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static float tri_area(const vr_cockpit_tri_t *t) {
	float e1[3], e2[3], n[3];
	sub3(t->v[1].pos, t->v[0].pos, e1);
	sub3(t->v[2].pos, t->v[0].pos, e2);
	cross3(e1, e2, n);
	return 0.5f * sqrtf(dot3(n, n));
}

// Moller-Trumbore, segment o + t d, t in (0, tmax)
static bool seg_tri(const float o[3], const float d[3], const float *a, const float *b, const float *c, float tmax) {
	float e1[3], e2[3], p[3], t_[3], q[3];
	sub3(b, a, e1);
	sub3(c, a, e2);
	cross3(d, e2, p);
	float det = dot3(e1, p);
	if (fabsf(det) < 1e-9f) {
		return false;
	}
	float inv = 1.0f / det;
	sub3(o, a, t_);
	float u = dot3(t_, p) * inv;
	if (u < 0 || u > 1) {
		return false;
	}
	cross3(t_, e1, q);
	float v = dot3(d, q) * inv;
	if (v < 0 || u + v > 1) {
		return false;
	}
	float t = dot3(e2, q) * inv;
	return t > 1e-4f && t < tmax;
}

// Distance from point p to triangle abc (Ericson, Real-Time Collision Detection)
static float point_tri_distance(const float p[3], const float *a, const float *b, const float *c) {
	float ab[3], ac[3], ap[3];
	sub3(b, a, ab);
	sub3(c, a, ac);
	sub3(p, a, ap);
	float d1 = dot3(ab, ap), d2 = dot3(ac, ap);
	float q[3];
	if (d1 <= 0 && d2 <= 0) {
		memcpy(q, a, sizeof(q));
	}
	else {
		float bp[3];
		sub3(p, b, bp);
		float d3 = dot3(ab, bp), d4 = dot3(ac, bp);
		float cp[3];
		sub3(p, c, cp);
		float d5 = dot3(ab, cp), d6 = dot3(ac, cp);
		float vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
		if (d3 >= 0 && d4 <= d3) {
			memcpy(q, b, sizeof(q));
		}
		else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
			float v = d1 / (d1 - d3);
			for (int i = 0; i < 3; i++) q[i] = a[i] + v * ab[i];
		}
		else if (d6 >= 0 && d5 <= d6) {
			memcpy(q, c, sizeof(q));
		}
		else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
			float w = d2 / (d2 - d6);
			for (int i = 0; i < 3; i++) q[i] = a[i] + w * ac[i];
		}
		else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
			float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			for (int i = 0; i < 3; i++) q[i] = b[i] + w * (c[i] - b[i]);
		}
		else {
			float denom = 1.0f / (va + vb + vc);
			float v = vb * denom, w = vc * denom;
			for (int i = 0; i < 3; i++) q[i] = a[i] + ab[i] * v + ac[i] * w;
		}
	}
	float d[3];
	sub3(p, q, d);
	return sqrtf(dot3(d, d));
}

static bool inside_cut(const vr_cockpit_cut_t *c, const float p[3], float eps) {
	return p[0] > c->x0 + eps && p[0] < c->x1 - eps && p[1] < c->y_max - eps && p[2] > c->z0 + eps && p[2] < c->z1 - eps;
}

static unsigned rng_state = 12345;
static float rnd(float lo, float hi) {
	rng_state = rng_state * 1664525u + 1013904223u;
	return lo + (hi - lo) * ((rng_state >> 8) / 16777216.0f);
}


// -----------------------------------------------------------------------------
// The table

static void test_table(void) {
	CHECK_NEAR(vr_cockpit_fitted_scale(), 88, 1e-6);
	CHECK_NEAR(vr_cockpit_fitted_eye_y(), -85.7, 1e-4);
	vr_cockpit_cut_t cut = vr_cockpit_cut();
	CHECK(cut.x0 < 0 && cut.x1 > 0 && cut.z0 < 0 && cut.z1 > 0 && cut.y_max < 0);
	CHECK_NEAR(cut.x1, -cut.x0, 1e-4);

	// every part: a known slot, a hull mask, a range of triangles that tiles the array
	int parts = vr_cockpit_part_count();
	CHECK_INT(parts, 90);
	int total = 0;
	int per_shape[VR_COCKPIT_SHAPES] = {0};
	const float *expect_next = NULL;
	for (int i = 0; i < parts; i++) {
		vr_cockpit_part_info_t p;
		CHECK(vr_cockpit_part(i, &p));
		CHECK(p.slot >= 0 && p.slot < VR_COCKPIT_SLOT_COUNT);
		CHECK(p.hull_mask != 0 && p.hull_mask < (1u << VR_COCKPIT_SHAPES));
		CHECK(p.tri_count > 0);
		CHECK(p.name && p.name[0] && p.group && p.group[0]);
		if (expect_next) {
			CHECK(p.verts == expect_next); // contiguous
		}
		expect_next = p.verts + (size_t)p.tri_count * 9;
		total += p.tri_count;
		for (int s = 0; s < VR_COCKPIT_SHAPES; s++) {
			if (p.hull_mask & (1u << s)) {
				per_shape[s] += p.tri_count;
			}
		}
		for (int k = 0; k < p.tri_count * 9; k++) {
			CHECK(isfinite(p.verts[k]));
		}
	}
	CHECK(!vr_cockpit_part(parts, &(vr_cockpit_part_info_t){0}));
	CHECK(!vr_cockpit_part(-1, &(vr_cockpit_part_info_t){0}));
	CHECK_INT(total, 4640);
	for (int s = 0; s < VR_COCKPIT_SHAPES; s++) {
		CHECK(per_shape[s] > 1200 && per_shape[s] < 1700); // about 1,430 triangles a hull
	}

	// the slot names and the authored colours
	CHECK_STR(vr_cockpit_slot_name(VR_COCKPIT_SLOT_FRAME), "frame");
	CHECK_STR(vr_cockpit_slot_name(VR_COCKPIT_SLOT_TRIM_SECONDARY), "trim_secondary");
	CHECK_INT(vr_cockpit_slot_color(VR_COCKPIT_SLOT_FRAME)[0], 169);
	CHECK_INT(vr_cockpit_slot_color(VR_COCKPIT_SLOT_TRIM_PRIMARY)[1], 0);

	// the faces: three, the texture sizes of the asset sheet, planar rectangles of the planned size
	CHECK_INT(vr_cockpit_face_count(), 3);
	const struct { const char *id; int w, h; float width_m, height_m; } want[3] = {
		{"left", 512, 292, 0.28f, 0.16f}, {"right", 512, 292, 0.28f, 0.16f}, {"weapon", 128, 128, 0.07f, 0.07f}
	};
	for (int i = 0; i < 3; i++) {
		vr_cockpit_face_info_t f;
		CHECK(vr_cockpit_face(i, &f));
		CHECK_STR(f.id, want[i].id);
		CHECK_INT(f.tex_w, want[i].w);
		CHECK_INT(f.tex_h, want[i].h);
		const float *tl = f.corners, *tr = f.corners + 3, *br = f.corners + 6, *bl = f.corners + 9;
		float top[3], left[3], bottom[3], right[3];
		sub3(tr, tl, top);
		sub3(bl, tl, left);
		sub3(br, bl, bottom);
		sub3(br, tr, right);
		CHECK_NEAR(sqrtf(dot3(top, top)), want[i].width_m * WS, 0.05 * WS * 0.1);
		CHECK_NEAR(sqrtf(dot3(left, left)), want[i].height_m * WS, 0.05 * WS * 0.1);
		for (int k = 0; k < 3; k++) {
			CHECK_NEAR(top[k], bottom[k], 0.2); // a parallelogram (the table has 0.1 unit rounding)
			CHECK_NEAR(left[k], right[k], 0.2);
		}
		CHECK_NEAR(dot3(top, left) / (sqrtf(dot3(top, top)) * sqrtf(dot3(left, left))), 0, 0.02); // with square corners
		// one texel is 0.547 mm: 20 texels a degree at 0.62 m
		CHECK_NEAR(sqrtf(dot3(top, top)) / WS / f.tex_w * 1000.0, 0.547, 0.01);
	}
	CHECK(!vr_cockpit_face(3, &(vr_cockpit_face_info_t){0}));

	// the elements: a place inside each face, an integer ui scale
	const struct { const char *face, *el; } els[] = {
		{"left", "lap"}, {"left", "position"}, {"left", "lives"}, {"left", "prevlaps"}, {"left", "laptime"},
		{"right", "speedo"}, {"right", "shield"}, {"weapon", "weapon"}
	};
	for (size_t i = 0; i < sizeof(els) / sizeof(els[0]); i++) {
		int x, y, scale;
		CHECK(vr_cockpit_element(els[i].face, els[i].el, &x, &y, &scale));
		CHECK(x >= 0 && y >= 0 && scale >= 2 && scale <= 3);
		int w = strcmp(els[i].face, "weapon") == 0 ? 128 : 512, h = strcmp(els[i].face, "weapon") == 0 ? 128 : 292;
		CHECK(x < w && y < h);
	}
	int x, y, scale;
	CHECK(!vr_cockpit_element("left", "nonsense", &x, &y, &scale));
}

// The pods are where the mockup put them: the canopy design itself is
// approved, so the numbers the faces and the nearest parts depend on must not move
static void test_authored_geometry_clearances(void) {
	const float eye[3] = {0, vr_cockpit_fitted_eye_y(), 0};
	for (int shape = 0; shape < VR_COCKPIT_SHAPES; shape++) {
		float nearest = 1e9f;
		vr_cockpit_trilist_t opaque = {0}, glass = {0};
		const uint8_t trim[2][3] = {{200, 40, 40}, {40, 40, 200}};
		vr_cockpit_build_parts(shape, trim, &opaque, &glass);
		CHECK(opaque.count > 1000 && glass.count > 20);
		for (int i = 0; i < opaque.count; i++) {
			const vr_cockpit_tri_t *t = &opaque.tris[i];
			float d = point_tri_distance(eye, t->v[0].pos, t->v[1].pos, t->v[2].pos);
			if (d < nearest) {
				nearest = d;
			}
		}
		// the nearest part is 0.34 m away (the canopy header and the headrest): at least 0.33 m
		CHECK(nearest / WS >= 0.33f);

		// rays from the eye to a 5 x 4 grid on every face hit no part (the hull is not here: the
		// harness checks it with the player's real hulls)
		for (int f = 0; f < vr_cockpit_face_count(); f++) {
			vr_cockpit_face_info_t face;
			vr_cockpit_face(f, &face);
			int blocked = 0;
			for (int gy = 0; gy < 4; gy++) {
				for (int gx = 0; gx < 5; gx++) {
					float u = 0.1f + 0.8f * gx / 4.0f, v = 0.1f + 0.8f * gy / 3.0f;
					float p[3], d[3];
					for (int k = 0; k < 3; k++) {
						float top = face.corners[k] + (face.corners[3 + k] - face.corners[k]) * u;
						float bot = face.corners[9 + k] + (face.corners[6 + k] - face.corners[9 + k]) * u;
						p[k] = top + (bot - top) * v;
					}
					sub3(p, eye, d);
					for (int i = 0; i < opaque.count; i++) {
						const vr_cockpit_tri_t *t = &opaque.tris[i];
						if (seg_tri(eye, d, t->v[0].pos, t->v[1].pos, t->v[2].pos, 0.995f)) {
							blocked++;
							break;
						}
					}
				}
			}
			CHECK_INT(blocked, 0);
		}
		vr_cockpit_trilist_free(&opaque);
		vr_cockpit_trilist_free(&glass);
	}
}

static void test_hull_match(void) {
	const struct { const char *name; int tris, shape; } models[8] = {
		{"sophia", 186, 0}, {"jacko", 186, 0}, {"solaar", 131, 1}, {"arian", 131, 1},
		{"chang", 148, 2}, {"Dekka", 148, 2}, {"arial", 137, 3}, {"anasta", 137, 3}
	};
	for (int i = 0; i < 8; i++) {
		CHECK_INT(vr_cockpit_hull_shape(models[i].name, models[i].tris), models[i].shape);
		CHECK_INT(vr_cockpit_hull_shape(models[i].name, models[i].tris + 1), -1); // a changed model matches nothing
		CHECK_INT(vr_cockpit_hull_shape(models[i].name, models[(i + 2) % 8].tris == models[i].tris ? 1 : models[(i + 2) % 8].tris), -1);
	}
	CHECK_INT(vr_cockpit_hull_shape("unknown", 186), -1);
	CHECK_INT(vr_cockpit_hull_shape(NULL, 186), -1);
	CHECK_INT(vr_cockpit_hull_shape("Sophia", 186), -1); // case matters: the data's names do
}


// -----------------------------------------------------------------------------
// The cut

static vr_cockpit_tri_t make_tri(const float a[3], const float b[3], const float c[3]) {
	vr_cockpit_tri_t t;
	memset(&t, 0, sizeof(t));
	const float *p[3] = {a, b, c};
	for (int k = 0; k < 3; k++) {
		memcpy(t.v[k].pos, p[k], sizeof(t.v[k].pos));
		// attributes that are linear in position, so interpolation is checkable
		t.v[k].uv[0] = 2 * p[k][0] + 1;
		t.v[k].uv[1] = -0.5f * p[k][2];
		t.v[k].color[0] = (uint8_t)floorf(100 + p[k][0] + 0.5f);
		t.v[k].color[1] = 128;
		t.v[k].color[2] = 200;
		t.v[k].color[3] = 255;
	}
	t.texture = 7;
	return t;
}

static void test_cut(void) {
	vr_cockpit_cut_t cut = {.x0 = -10, .x1 = 10, .y_max = -5, .z0 = -20, .z1 = 30};

	// wholly outside: kept as it is
	{
		const float a[3] = {20, -20, 0}, b[3] = {30, -20, 0}, c[3] = {20, -30, 5};
		vr_cockpit_tri_t t = make_tri(a, b, c);
		vr_cockpit_trilist_t out = {0};
		CHECK_INT(vr_cockpit_clip_triangle(&t, &cut, &out), 1);
		CHECK(memcmp(out.tris[0].v[0].pos, a, sizeof(a)) == 0);
		CHECK_INT(out.tris[0].texture, 7);
		vr_cockpit_trilist_free(&out);
	}
	// wholly inside: dropped
	{
		const float a[3] = {-5, -20, 0}, b[3] = {5, -20, 0}, c[3] = {0, -30, 5};
		vr_cockpit_tri_t t = make_tri(a, b, c);
		vr_cockpit_trilist_t out = {0};
		CHECK_INT(vr_cockpit_clip_triangle(&t, &cut, &out), 0);
		vr_cockpit_trilist_free(&out);
	}
	// below the floor (y > y_max, y is down): outside, kept
	{
		const float a[3] = {-5, 0, 0}, b[3] = {5, 0, 0}, c[3] = {0, 2, 5};
		vr_cockpit_tri_t t = make_tri(a, b, c);
		vr_cockpit_trilist_t out = {0};
		CHECK_INT(vr_cockpit_clip_triangle(&t, &cut, &out), 1);
		vr_cockpit_trilist_free(&out);
	}
	// the open top: a triangle high above the box is inside its x/z footprint, so it is cut
	{
		const float a[3] = {-5, -500, 0}, b[3] = {5, -500, 0}, c[3] = {0, -600, 5};
		vr_cockpit_tri_t t = make_tri(a, b, c);
		vr_cockpit_trilist_t out = {0};
		CHECK_INT(vr_cockpit_clip_triangle(&t, &cut, &out), 0);
		vr_cockpit_trilist_free(&out);
	}

	// random triangles straddling the box: nothing left inside, the area adds up, the
	// attributes stay linear, the winding is kept
	int straddlers = 0;
	for (int n = 0; n < 3000; n++) {
		float p[3][3];
		float scale = rnd(5, 60);
		float cx = rnd(-15, 15), cy = rnd(-40, 5), cz = rnd(-10, 20);
		for (int k = 0; k < 3; k++) {
			p[k][0] = cx + rnd(-scale, scale);
			p[k][1] = cy + rnd(-scale, scale);
			p[k][2] = cz + rnd(-scale, scale);
		}
		vr_cockpit_tri_t t = make_tri(p[0], p[1], p[2]);
		float area = tri_area(&t);
		if (area < 1.0f) {
			continue;
		}
		vr_cockpit_trilist_t out = {0};
		int pieces = vr_cockpit_clip_triangle(&t, &cut, &out);
		CHECK_INT(pieces, out.count);

		float e1[3], e2[3], n0[3];
		sub3(p[1], p[0], e1);
		sub3(p[2], p[0], e2);
		cross3(e1, e2, n0);

		float out_area = 0;
		for (int i = 0; i < out.count; i++) {
			const vr_cockpit_tri_t *o = &out.tris[i];
			out_area += tri_area(o);
			float centroid[3] = {0, 0, 0};
			for (int k = 0; k < 3; k++) {
				CHECK(!inside_cut(&cut, o->v[k].pos, 1e-2f)); // no vertex inside the box
				CHECK_NEAR(o->v[k].uv[0], 2 * o->v[k].pos[0] + 1, 2e-3 * (1 + fabsf(o->v[k].uv[0])));
				CHECK_NEAR(o->v[k].uv[1], -0.5f * o->v[k].pos[2], 2e-3 * (1 + fabsf(o->v[k].uv[1])));
				CHECK_NEAR(o->v[k].color[0], 100 + o->v[k].pos[0], 1.0 + 0.5);
				CHECK_INT(o->v[k].color[1], 128);
				for (int c = 0; c < 3; c++) {
					centroid[c] += o->v[k].pos[c] / 3;
				}
			}
			CHECK(!inside_cut(&cut, centroid, 1e-3f));
			CHECK_INT(o->texture, 7);
			float f1[3], f2[3], nn[3];
			sub3(o->v[1].pos, o->v[0].pos, f1);
			sub3(o->v[2].pos, o->v[0].pos, f2);
			cross3(f1, f2, nn);
			CHECK(dot3(nn, n0) > 0); // same winding
		}

		// the part of the triangle outside the box, by sampling it
		int outside = 0, samples = 400;
		for (int i = 0; i < samples; i++) {
			float u = rnd(0, 1), v = rnd(0, 1);
			if (u + v > 1) {
				u = 1 - u;
				v = 1 - v;
			}
			float q[3];
			for (int c = 0; c < 3; c++) {
				q[c] = p[0][c] + u * (p[1][c] - p[0][c]) + v * (p[2][c] - p[0][c]);
			}
			if (!inside_cut(&cut, q, 0)) {
				outside++;
			}
		}
		CHECK(out_area <= area * 1.001f);
		CHECK_NEAR(out_area / area, (float)outside / samples, 0.12);
		if (out.count > 0 && out_area < area * 0.99f) {
			straddlers++;
		}
		vr_cockpit_trilist_free(&out);
	}
	CHECK(straddlers > 300); // the random set really did cut things

	// the inside copy: the other winding, dimmed
	{
		const float a[3] = {20, -20, 0}, b[3] = {30, -20, 0}, c[3] = {20, -30, 5};
		vr_cockpit_tri_t t = make_tri(a, b, c);
		vr_cockpit_trilist_t front = {0}, inside = {0};
		vr_cockpit_trilist_push(&front, &t);
		vr_cockpit_inside_copy(&front, VR_COCKPIT_INSIDE_DIM, &inside);
		CHECK_INT(inside.count, 1);
		float f1[3], f2[3], n_front[3], g1[3], g2[3], n_in[3];
		sub3(front.tris[0].v[1].pos, front.tris[0].v[0].pos, f1);
		sub3(front.tris[0].v[2].pos, front.tris[0].v[0].pos, f2);
		cross3(f1, f2, n_front);
		sub3(inside.tris[0].v[1].pos, inside.tris[0].v[0].pos, g1);
		sub3(inside.tris[0].v[2].pos, inside.tris[0].v[0].pos, g2);
		cross3(g1, g2, n_in);
		CHECK(dot3(n_front, n_in) < 0);
		CHECK_NEAR(inside.tris[0].v[0].color[2], 200 * VR_COCKPIT_INSIDE_DIM, 1.01);
		CHECK_INT(inside.tris[0].v[0].color[3], 255); // alpha untouched
		CHECK_INT(inside.tris[0].texture, 7);
		vr_cockpit_trilist_free(&front);
		vr_cockpit_trilist_free(&inside);
	}
}


// -----------------------------------------------------------------------------
// The trim rule

// A textured triangle covering the whole of a w x h image: uv over its corners
static vr_cockpit_tri_t image_tri(float u0, float u1, float size, int texture) {
	vr_cockpit_tri_t t;
	memset(&t, 0, sizeof(t));
	// a right triangle with legs `size` units: area size^2 / 2
	const float p[3][3] = {{0, 0, 0}, {size, 0, 0}, {0, size, 0}};
	const float uv[3][2] = {{u0, 0}, {u1, 0}, {u0, 0}};
	for (int k = 0; k < 3; k++) {
		memcpy(t.v[k].pos, p[k], sizeof(t.v[k].pos));
		t.v[k].uv[0] = uv[k][0];
		t.v[k].uv[1] = uv[k][1];
		memset(t.v[k].color, 128, 3);
		t.v[k].color[3] = 255;
	}
	t.texture = texture;
	return t;
}

// A flat texture (every texel the same) of `rgba`
static vr_cockpit_image_t flat_image(uint8_t *buf, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	for (int i = 0; i < w * h; i++) {
		buf[i * 4 + 0] = r;
		buf[i * 4 + 1] = g;
		buf[i * 4 + 2] = b;
		buf[i * 4 + 3] = a;
	}
	return (vr_cockpit_image_t){w, h, buf};
}

static void test_trim_rule(void) {
	uint8_t red_px[4 * 4 * 4], blue_px[4 * 4 * 4], grey_px[4 * 4 * 4], clear_px[4 * 4 * 4], dark_px[4 * 4 * 4];
	vr_cockpit_image_t images[5] = {
		flat_image(red_px, 4, 4, 200, 40, 40, 255),
		flat_image(blue_px, 4, 4, 40, 60, 210, 255),
		flat_image(grey_px, 4, 4, 180, 180, 180, 255),
		flat_image(clear_px, 4, 4, 255, 0, 0, 0), // transparent: skipped
		flat_image(dark_px, 4, 4, 90, 20, 20, 255)
	};

	// two hues: the bigger one is primary, the other secondary
	{
		vr_cockpit_tri_t tris[2] = {image_tri(0, 3, 20, 0), image_tri(0, 3, 10, 1)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 2, images, 5, &trim);
		CHECK(!trim.neutral && !trim.secondary_fallback);
		CHECK_INT(trim.primary[0], 201); // 200 * 128 / 127.5, within 0.70..0.95 already
		CHECK(trim.primary[0] > trim.primary[2] * 3);
		CHECK(trim.secondary[2] > trim.secondary[0] * 3); // blue
		CHECK(trim.saturated_share > 0.99f);
	}
	// swap the areas: the primary follows the area
	{
		vr_cockpit_tri_t tris[2] = {image_tri(0, 3, 10, 0), image_tri(0, 3, 20, 1)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 2, images, 5, &trim);
		CHECK(trim.primary[2] > trim.primary[0] * 3);
		CHECK(trim.secondary[0] > trim.secondary[2] * 3);
	}
	// one hue only: the secondary is the primary at 55%
	{
		vr_cockpit_tri_t tris[1] = {image_tri(0, 3, 12, 0)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 1, images, 5, &trim);
		CHECK(trim.secondary_fallback && !trim.neutral);
		for (int c = 0; c < 3; c++) {
			CHECK_NEAR(trim.secondary[c], floor(trim.primary[c] * 0.55 + 0.5), 0.01);
		}
	}
	// dark paint is brought up to a value of 0.70 (the PSX paint is dark and noisy)
	{
		vr_cockpit_tri_t tris[1] = {image_tri(0, 3, 12, 4)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 1, images, 5, &trim);
		int mx = trim.primary[0] > trim.primary[1] ? trim.primary[0] : trim.primary[1];
		CHECK_NEAR(mx, 0.70 * 255, 1.0);
		CHECK(trim.primary[1] * 4 < trim.primary[0] * 1.2); // the hue is kept (g/r = 20/90)
	}
	// white and grey: neutral, the frame slots
	{
		vr_cockpit_tri_t tris[1] = {image_tri(0, 3, 12, 2)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 1, images, 5, &trim);
		CHECK(trim.neutral && !trim.secondary_fallback);
		CHECK(memcmp(trim.primary, vr_cockpit_slot_color(VR_COCKPIT_SLOT_FRAME), 3) == 0);
		CHECK(memcmp(trim.secondary, vr_cockpit_slot_color(VR_COCKPIT_SLOT_FRAME_DARK), 3) == 0);
		CHECK(trim.saturated_share < 0.005f);
	}
	// transparent texels are skipped: they neither count as area nor as colour
	{
		vr_cockpit_tri_t tris[2] = {image_tri(0, 3, 40, 3), image_tri(0, 3, 10, 1)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 2, images, 5, &trim);
		CHECK(trim.primary[2] > trim.primary[0] * 3); // the small blue triangle wins
		CHECK(trim.saturated_share > 0.99f);
	}
	// a little saturated area on a mostly white hull still gives its colours (anasta's case: about 1%)
	{
		vr_cockpit_tri_t tris[2] = {image_tri(0, 3, 100, 2), image_tri(0, 3, 10, 0)}; // red is 1% of the area
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 2, images, 5, &trim);
		CHECK(!trim.neutral);
		CHECK(trim.primary[0] > trim.primary[2] * 3);
		CHECK_NEAR(trim.saturated_share, 0.0099, 0.002);
	}
	// 0.4% saturated: under the 0.5% threshold, neutral
	{
		vr_cockpit_tri_t tris[2] = {image_tri(0, 3, 100, 2), image_tri(0, 3, 6.3f, 0)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 2, images, 5, &trim);
		CHECK(trim.neutral);
	}
	// untextured polygons (texture -1, or none in range) are the vertex colour: the 128 grey texel the shader doubles
	{
		vr_cockpit_tri_t t = image_tri(0, 0, 12, -1);
		for (int k = 0; k < 3; k++) {
			t.v[k].color[0] = 100;
			t.v[k].color[1] = 30;
			t.v[k].color[2] = 30;
		}
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(&t, 1, images, 5, &trim);
		CHECK(!trim.neutral);
		CHECK(trim.primary[0] > trim.primary[1] * 3); // (100, 30, 30), brought up to a value of 0.70
		CHECK_NEAR(trim.primary[0], 0.70 * 255, 1.0);
	}
	// texel boundaries and the texture's edges: a sample exactly on a boundary takes the lower
	// texel, one outside the texture has no texel and is skipped
	{
		uint8_t two_px[2 * 1 * 4] = {200, 40, 40, 255, 40, 60, 210, 255}; // red | blue
		vr_cockpit_image_t two_tex[1] = {{2, 1, two_px}};
		vr_cockpit_trim_t trim;
		vr_cockpit_tri_t t = image_tri(1.0f, 1.0f, 10, 0); // every uv exactly 1.0: the border between red and blue
		vr_cockpit_sample_trim(&t, 1, two_tex, 1, &trim);
		CHECK(trim.primary[0] > trim.primary[2] * 3); // red, the lower texel
		t = image_tri(0.0f, 0.0f, 10, 0); // exactly 0: the first texel
		vr_cockpit_sample_trim(&t, 1, two_tex, 1, &trim);
		CHECK(trim.primary[0] > trim.primary[2] * 3);
		t = image_tri(1.5f, 1.5f, 10, 0); // inside the second texel
		vr_cockpit_sample_trim(&t, 1, two_tex, 1, &trim);
		CHECK(trim.primary[2] > trim.primary[0] * 3);
		t = image_tri(2.0f, 2.0f, 10, 0); // exactly the right edge: the last texel
		vr_cockpit_sample_trim(&t, 1, two_tex, 1, &trim);
		CHECK(trim.primary[2] > trim.primary[0] * 3);
		t = image_tri(2.5f, 2.5f, 10, 0); // outside: skipped, nothing sampled, neutral
		vr_cockpit_sample_trim(&t, 1, two_tex, 1, &trim);
		CHECK(trim.neutral);
		t = image_tri(-0.5f, -0.5f, 10, 0);
		vr_cockpit_sample_trim(&t, 1, two_tex, 1, &trim);
		CHECK(trim.neutral);
	}
	// a hue across the red wrap (350 and 10 degrees) is one colour, not two
	{
		uint8_t a_px[4 * 4 * 4], b_px[4 * 4 * 4];
		vr_cockpit_image_t two[2] = {flat_image(a_px, 4, 4, 230, 40, 70, 255), flat_image(b_px, 4, 4, 230, 70, 40, 255)};
		vr_cockpit_tri_t tris[2] = {image_tri(0, 3, 10, 0), image_tri(0, 3, 10, 1)};
		vr_cockpit_trim_t trim;
		vr_cockpit_sample_trim(tris, 2, two, 2, &trim);
		CHECK(trim.secondary_fallback); // no second hue
		CHECK(trim.primary[0] > 200);
		CHECK_NEAR(trim.primary[1], trim.primary[2], 14); // the mean of the two sits between them
	}
}


// -----------------------------------------------------------------------------
// The pre-shaded parts

static void test_light(void) {
	// a triangle whose normal is the sun's direction, seen from its front: full light
	const float sun[3] = {0.35f / 1.151087f, -1.0f / 1.151087f, -0.45f / 1.151087f};
	float u[3] = {1, 0, 0}, v[3];
	cross3(sun, u, v);
	float n = sqrtf(dot3(v, v));
	for (int i = 0; i < 3; i++) v[i] /= n;
	float w[3];
	cross3(sun, v, w); // u' and v span the plane perpendicular to the sun
	float a[3] = {0, 0, 0}, b[3], c[3];
	for (int i = 0; i < 3; i++) {
		b[i] = w[i] * 10;
		c[i] = v[i] * 10;
	}
	// the viewer on the sun's side sees it lit, whichever way the winding faces: double-sided
	float toward_front[3] = {sun[0] * 50, sun[1] * 50, sun[2] * 50};
	float toward_back[3] = {-toward_front[0], -toward_front[1], -toward_front[2]};
	// the lit side is full, the other side ambient
	CHECK_NEAR(vr_cockpit_light(a, b, c, toward_front, 0), 1.0, 1e-3);
	CHECK_NEAR(vr_cockpit_light(a, b, c, toward_back, 0), 0.55, 1e-3);
	CHECK_NEAR(vr_cockpit_light(a, b, c, toward_front, 0.25f), 1.25, 1e-3); // trim glows
	// edge-on to the sun: ambient
	float flat_b[3] = {10, 0, 0}, flat_c[3] = {0, 0, 10}; // a deck: normal along y
	float up_eye[3] = {0, -50, 0};
	float l = vr_cockpit_light((float[3]){0, 0, 0}, flat_c, flat_b, up_eye, 0);
	CHECK(l > 0.55f && l < 1.0f);
	CHECK_NEAR(l, 0.55 + 0.45 * (1.0 / 1.151087), 1e-3); // a deck faces up: the sun's y is 1 / 1.151
	// a degenerate triangle does not blow up
	CHECK_NEAR(vr_cockpit_light(a, a, a, up_eye, 0), 0.55, 1e-6);
}

static void test_parts(void) {
	const uint8_t trim[2][3] = {{201, 41, 40}, {40, 61, 211}};
	for (int shape = 0; shape < VR_COCKPIT_SHAPES; shape++) {
		vr_cockpit_trilist_t opaque = {0}, glass = {0};
		vr_cockpit_build_parts(shape, trim, &opaque, &glass);

		int expect_opaque = 0, expect_glass = 0;
		for (int i = 0; i < vr_cockpit_part_count(); i++) {
			vr_cockpit_part_info_t p;
			vr_cockpit_part(i, &p);
			if (p.hull_mask & (1u << shape)) {
				(p.slot == VR_COCKPIT_SLOT_GLASS ? &expect_glass : &expect_opaque)[0] += p.tri_count;
			}
		}
		CHECK_INT(opaque.count, expect_opaque);
		CHECK_INT(glass.count, expect_glass);
		CHECK(opaque.count > 1000 && glass.count > 20);

		for (int i = 0; i < opaque.count; i++) {
			const vr_cockpit_tri_t *t = &opaque.tris[i];
			CHECK_INT(t->texture, -1);
			CHECK_INT(t->v[0].color[3], 255);
			CHECK(memcmp(t->v[0].color, t->v[1].color, 3) == 0 && memcmp(t->v[0].color, t->v[2].color, 3) == 0); // flat
			CHECK(t->v[0].uv[0] == 0 && t->v[0].uv[1] == 0);
		}
		for (int i = 0; i < glass.count; i++) {
			// the tint: the glass colour (158, 217, 255) at alpha 0.045
			CHECK_INT(glass.tris[i].v[0].color[3], 11);
			CHECK_INT(glass.tris[i].v[0].color[0], 158);
			CHECK_INT(glass.tris[i].v[0].color[2], 255);
		}
		vr_cockpit_trilist_free(&opaque);
		vr_cockpit_trilist_free(&glass);
	}

	// the colours on screen are the slot's times the light (ambient 0.55 to 1): the silver frame
	// is bright, the cabin dark
	{
		vr_cockpit_trilist_t o = {0}, gl = {0};
		vr_cockpit_build_parts(0, trim, &o, &gl);
		for (int i = 0; i < vr_cockpit_part_count(); i++) {
			vr_cockpit_part_info_t p;
			vr_cockpit_part(i, &p);
			if (!(p.hull_mask & 1u) || (p.slot != VR_COCKPIT_SLOT_FRAME && p.slot != VR_COCKPIT_SLOT_CABIN_DARK)) {
				continue;
			}
			const uint8_t *rgb = vr_cockpit_slot_color(p.slot);
			CHECK(p.tri_count > 0);
			// find this part's first triangle in the built list: the parts come in table order
			int offset = 0;
			for (int j = 0; j < i; j++) {
				vr_cockpit_part_info_t q;
				vr_cockpit_part(j, &q);
				if ((q.hull_mask & 1u) && q.slot != VR_COCKPIT_SLOT_GLASS) {
					offset += q.tri_count;
				}
			}
			for (int t = 0; t < p.tri_count; t++) {
				const uint8_t *c = o.tris[offset + t].v[0].color;
				CHECK(c[0] >= rgb[0] * 0.55 - 1.5 && c[0] <= rgb[0] + 1.5);
				CHECK(c[2] >= rgb[2] * 0.55 - 1.5 && c[2] <= rgb[2] + 1.5);
			}
		}
		vr_cockpit_trilist_free(&o);
		vr_cockpit_trilist_free(&gl);
	}

	// the trim slots take the ship's colours: swap the trim and the parts of those slots change
	vr_cockpit_trilist_t a = {0}, b = {0}, ga = {0}, gb = {0};
	const uint8_t other[2][3] = {{40, 200, 40}, {200, 200, 40}};
	vr_cockpit_build_parts(0, trim, &a, &ga);
	vr_cockpit_build_parts(0, other, &b, &gb);
	CHECK_INT(a.count, b.count);
	int changed = 0, same = 0;
	for (int i = 0; i < a.count; i++) {
		if (memcmp(a.tris[i].v[0].color, b.tris[i].v[0].color, 3) != 0) {
			changed++;
		}
		else {
			same++;
		}
	}
	CHECK(changed > 20 && changed < a.count / 4); // the stripe, the cap, the keystone surround, the pod lips
	CHECK(same > a.count / 2);
	vr_cockpit_trilist_free(&a);
	vr_cockpit_trilist_free(&b);
	vr_cockpit_trilist_free(&ga);
	vr_cockpit_trilist_free(&gb);
}


// -----------------------------------------------------------------------------
// The eye and the bank modes

static const float ship_poses[][6] = {
	// x, y, z, pitch, yaw, roll
	{0, 0, 0, 0, 0, 0},
	{1234.5f, -678.0f, 9012.25f, 0.3f, 0.9f, 0},
	{-300.0f, 50.0f, 4000.0f, -0.4f, -2.2f, 0.35f},
	{5000.0f, -200.0f, -700.0f, 0.1f, 3.0f, -0.55f},
	{10.0f, 20.0f, 30.0f, 0.7f, -0.5f, 0.6f},
	{-4500.0f, 900.0f, 123.0f, -1.1f, 1.5f, -0.2f},
};
#define POSES ((int)(sizeof(ship_poses) / sizeof(ship_poses[0])))

static void ship_matrix(const float p[6], mat4_t *m) {
	*m = mat4_identity();
	mat4_set_translation(m, vec3(p[0], p[1], p[2]));
	mat4_set_yaw_pitch_roll(m, vec3(p[3], p[4], p[5]));
}

static void test_ship_rotation_is_the_games(void) {
	for (int i = 0; i < POSES; i++) {
		mat4_t m;
		ship_matrix(ship_poses[i], &m);
		float rot[9];
		vr_game_ship_rotation(&ship_poses[i][3], rot);
		for (int c = 0; c < 3; c++) {
			for (int r = 0; r < 3; r++) {
				CHECK_NEAR(rot[c * 3 + r], m.cols[c][r], 1e-6);
			}
		}
	}
}

static void test_eye(void) {
	vr_cockpit_settings_t s = {.enabled = true, .bank = 0, .eye_forward_cm = 0, .eye_up_cm = 0, .glass = true, .world_scale = WS};
	float e[3];
	vr_cockpit_eye(&s, WS, e);
	CHECK_NEAR(e[0], 0, 1e-6);
	CHECK_NEAR(e[1], -85.7, 1e-4); // the fitted eye, not ship_cockpit()'s -128
	CHECK_NEAR(e[2], 0, 1e-6);

	// physical centimetres: cm / 100 * world_scale game units; +forward is +z, +up is -y
	s.eye_forward_cm = 10;
	s.eye_up_cm = 5;
	vr_cockpit_eye(&s, WS, e);
	CHECK_NEAR(e[2], 8.8, 1e-4);
	CHECK_NEAR(e[1], -85.7 - 4.4, 1e-4);
	vr_cockpit_eye(&s, 100.0f, e); // a different world_scale: the same centimetres, more units
	CHECK_NEAR(e[2], 10.0, 1e-4);
	CHECK_NEAR(e[1], -85.7 - 5.0, 1e-4);
	s.eye_forward_cm = -30;
	s.eye_up_cm = -30;
	vr_cockpit_eye(&s, WS, e);
	CHECK_NEAR(e[2], -26.4, 1e-4);
	CHECK_NEAR(e[1], -85.7 + 26.4, 1e-4);

	// sanitising: range, NaN, an out-of-range bank
	vr_cockpit_settings_t bad = {.enabled = true, .bank = 9, .eye_forward_cm = 500, .eye_up_cm = NAN, .glass = true, .world_scale = WS};
	vr_cockpit_settings_sanitize(&bad);
	CHECK_INT(bad.bank, VR_COCKPIT_BANK_LEVEL);
	CHECK_NEAR(bad.eye_forward_cm, 30, 1e-6);
	CHECK_NEAR(bad.eye_up_cm, 0, 1e-6);
	bad.bank = -1;
	bad.eye_forward_cm = -500;
	vr_cockpit_settings_sanitize(&bad);
	CHECK_INT(bad.bank, VR_COCKPIT_BANK_LEVEL);
	CHECK_NEAR(bad.eye_forward_cm, -30, 1e-6);
}

static void mat4_point(const float m[16], const float p[3], float out[3]) {
	for (int r = 0; r < 3; r++) {
		out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
	}
}

static void test_bank_modes(void) {
	const float seats[][3] = {{0, -85.7f, 0}, {0, -85.7f - 4.4f, 8.8f}, {0, -85.7f + 26.4f, -26.4f}};
	for (int sp = 0; sp < POSES; sp++) {
		const float *p = ship_poses[sp];
		mat4_t ship;
		ship_matrix(p, &ship);
		const float angles[3] = {p[3], p[4], p[5]};
		for (int se = 0; se < 3; se++) {
			const float *eye = seats[se];
			vec3_t want = vec3_transform(vec3(eye[0], eye[1], eye[2]), &ship);

			for (int bank = 0; bank < VR_COCKPIT_BANK_COUNT; bank++) {
				vr_cockpit_pose_t pose;
				vr_cockpit_pose(bank, p, angles, eye, &pose);

				// the head is at the seat through the ship's full matrix, in every mode
				CHECK_NEAR(pose.eye_world[0], want.x, 1e-2);
				CHECK_NEAR(pose.eye_world[1], want.y, 1e-2);
				CHECK_NEAR(pose.eye_world[2], want.z, 1e-2);

				// and the eye is a fixed point of the cockpit's matrix
				float at[3];
				mat4_point(pose.model, eye, at);
				CHECK_NEAR(at[0], pose.eye_world[0], 1e-2);
				CHECK_NEAR(at[1], pose.eye_world[1], 1e-2);
				CHECK_NEAR(at[2], pose.eye_world[2], 1e-2);

				// the anchor's roll: none, none, the ship's own
				CHECK_NEAR(pose.anchor_angles[0], p[3], 1e-6);
				CHECK_NEAR(pose.anchor_angles[1], p[4], 1e-6);
				CHECK_NEAR(pose.anchor_angles[2], bank == VR_COCKPIT_BANK_FOLLOW ? p[5] : 0, 1e-6);

				if (bank == VR_COCKPIT_BANK_LEVEL) {
					// the rotation is the ship's with the roll taken out: the cockpit never rolls
					mat4_t level;
					level = mat4_identity();
					mat4_set_yaw_pitch_roll(&level, vec3(p[3], p[4], 0));
					for (int c = 0; c < 3; c++) {
						for (int r = 0; r < 3; r++) {
							CHECK_NEAR(pose.model[c * 4 + r], level.cols[c][r], 1e-5);
						}
					}
					// its right axis is horizontal (y is down): no roll
					CHECK_NEAR(pose.model[1], 0, 1e-5);
				}
				else {
					// follow and cockpit: the ship's own matrix, roll and all
					for (int k = 0; k < 16; k++) {
						CHECK_NEAR(pose.model[k], ship.m[k], 1e-3);
					}
				}

				// The head looks along the anchor's view. With a centred head the cockpit has
				// to sit in front of the eyes just as it sits on the ship: the view's rotation
				// times the cockpit's is a 180 degree turn about x (the game's own diag(1,-1,-1)),
				// except in `cockpit` mode, where the cockpit is rolled by the ship's roll
				float view[9];
				vr_game_view_rotation(pose.anchor_angles, view);
				float rot[9] = {0};
				for (int c = 0; c < 3; c++) {
					for (int r = 0; r < 3; r++) {
						rot[c * 3 + r] = pose.model[c * 4 + r];
					}
				}
				float prod[9];
				for (int c = 0; c < 3; c++) {
					for (int r = 0; r < 3; r++) {
						float sum = 0;
						for (int k = 0; k < 3; k++) {
							sum += view[k * 3 + r] * rot[c * 3 + k];
						}
						prod[c * 3 + r] = sum;
					}
				}
				// the cockpit's forward (+z) stays straight ahead (-z in view space) in all modes
				CHECK_NEAR(prod[6], 0, 1e-4);
				CHECK_NEAR(prod[7], 0, 1e-4);
				CHECK_NEAR(prod[8], -1, 1e-4);
				if (bank == VR_COCKPIT_BANK_COCKPIT) {
					// rolled about the forward axis by the ship's roll: its right axis is turned by it
					CHECK_NEAR(prod[0], cosf(p[5]), 1e-4);
					CHECK_NEAR(fabsf(prod[1]), fabsf(sinf(p[5])), 1e-4);
				}
				else {
					CHECK_NEAR(prod[0], 1, 1e-4);
					CHECK_NEAR(prod[1], 0, 1e-4);
					CHECK_NEAR(prod[2], 0, 1e-4);
					CHECK_NEAR(prod[3], 0, 1e-4);
					CHECK_NEAR(prod[4], -1, 1e-4);
					CHECK_NEAR(prod[5], 0, 1e-4);
				}
			}
		}
	}
}

// Through the renderer's own eye maths (vr_cam_from_pose with the cockpit anchor): a
// centred head sits at the seat, and a head 10 cm to the right moves 10 cm of world
// along the anchor's right, in the ship's frame in `follow`, level in `level`
static void test_eye_through_the_rig(void) {
	vr_rig_t rig = {.world_scale = WS, .head_translation = 1.0f, .follow_roll = true, .horizon_lock = false};
	vr_pose_t head = {.orientation = {0, 0, 0, 1}, .position = {0, 1.2f, 0}};
	vr_recentre_t rc = {.yaw = 0, .origin = {0, 1.2f, 0}};
	vr_pose_t moved = head;
	moved.position[0] += 0.10f;

	const float *p = ship_poses[3];
	const float angles[3] = {p[3], p[4], p[5]};
	mat4_t ship;
	ship_matrix(p, &ship);
	const float eye[3] = {0, -85.7f, 0};

	for (int bank = 0; bank < VR_COCKPIT_BANK_COUNT; bank++) {
		vr_cockpit_pose_t pose;
		vr_cockpit_pose(bank, p, angles, eye, &pose);
		vr_game_camera_t gc = {.position = {pose.eye_world[0], pose.eye_world[1], pose.eye_world[2]},
			.angles = {pose.anchor_angles[0], pose.anchor_angles[1], pose.anchor_angles[2]}};
		vr_cam_t cam, cam_moved;
		vr_cam_from_pose(&gc, &rig, &rc, &head, &head, &cam);
		vr_cam_from_pose(&gc, &rig, &rc, &head, &moved, &cam_moved);
		for (int k = 0; k < 3; k++) {
			CHECK_NEAR(cam.position[k], pose.eye_world[k], 1e-3);
		}
		float d[3] = {cam_moved.position[0] - cam.position[0], cam_moved.position[1] - cam.position[1], cam_moved.position[2] - cam.position[2]};
		CHECK_NEAR(sqrtf(dot3(d, d)), 0.10f * WS, 1e-3); // 10 cm of head is 8.8 game units
		// along the anchor's right: the roll only tilts it in `follow`
		if (bank == VR_COCKPIT_BANK_FOLLOW) {
			CHECK_NEAR(d[0] * ship.basis.right.vec3.x + d[1] * ship.basis.right.vec3.y + d[2] * ship.basis.right.vec3.z, 0.10f * WS, 1e-3);
		}
		else {
			CHECK_NEAR(d[1], 0, 1e-3); // level head: a horizontal step
		}
	}
}

int main(void) {
	RUN(test_table);
	RUN(test_authored_geometry_clearances);
	RUN(test_hull_match);
	RUN(test_cut);
	RUN(test_trim_rule);
	RUN(test_light);
	RUN(test_parts);
	RUN(test_ship_rotation_is_the_games);
	RUN(test_eye);
	RUN(test_bank_modes);
	RUN(test_eye_through_the_rig);
	return SFVR_TEST_RESULT("vr_cockpit");
}
