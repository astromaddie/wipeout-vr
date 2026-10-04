#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VR_COCKPIT_GEOMETRY_DATA
#include "vr_cockpit_core.h"
#include "vr_math.h"

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))


// -----------------------------------------------------------------------------
// The table

float vr_cockpit_fitted_scale(void) {
	return VR_COCKPIT_WORLD_SCALE;
}

float vr_cockpit_fitted_eye_y(void) {
	return VR_COCKPIT_EYE_Y;
}

vr_cockpit_cut_t vr_cockpit_cut(void) {
	return (vr_cockpit_cut_t){
		.x0 = VR_COCKPIT_CUT_X0, .x1 = VR_COCKPIT_CUT_X1, .y_max = VR_COCKPIT_CUT_Y_MAX,
		.z0 = VR_COCKPIT_CUT_Z0, .z1 = VR_COCKPIT_CUT_Z1
	};
}

int vr_cockpit_part_count(void) {
	return ARRAY_LEN(vr_cockpit_parts);
}

bool vr_cockpit_part(int index, vr_cockpit_part_info_t *out) {
	if (index < 0 || index >= ARRAY_LEN(vr_cockpit_parts)) {
		return false;
	}
	const vr_cockpit_part_t *p = &vr_cockpit_parts[index];
	out->name = p->name;
	out->group = p->group;
	out->slot = p->slot;
	out->hull_mask = p->hull_mask;
	out->tri_count = (int)p->tri_count;
	out->verts = &vr_cockpit_verts[(size_t)p->first_tri * 9];
	return true;
}

int vr_cockpit_face_count(void) {
	return ARRAY_LEN(vr_cockpit_faces);
}

bool vr_cockpit_face(int index, vr_cockpit_face_info_t *out) {
	if (index < 0 || index >= ARRAY_LEN(vr_cockpit_faces)) {
		return false;
	}
	const vr_cockpit_face_t *f = &vr_cockpit_faces[index];
	out->id = f->id;
	out->tex_w = f->tex_w;
	out->tex_h = f->tex_h;
	memcpy(out->corners, f->corners, sizeof(out->corners));
	return true;
}

bool vr_cockpit_element(const char *face, const char *element, int *x, int *y, int *ui_scale) {
	for (int i = 0; i < ARRAY_LEN(vr_cockpit_elements); i++) {
		const vr_cockpit_element_t *e = &vr_cockpit_elements[i];
		if (strcmp(e->face, face) == 0 && strcmp(e->element, element) == 0) {
			*x = e->x;
			*y = e->y;
			*ui_scale = e->ui_scale;
			return true;
		}
	}
	return false;
}

const uint8_t *vr_cockpit_slot_color(int slot) {
	static const uint8_t black[3] = {0, 0, 0};
	return (slot >= 0 && slot < VR_COCKPIT_SLOT_COUNT) ? vr_cockpit_slot_rgb[slot] : black;
}

const char *vr_cockpit_slot_name(int slot) {
	static const char *const names[VR_COCKPIT_SLOT_COUNT] = {
		"frame", "frame_dark", "cabin", "cabin_dark", "seat", "bezel", "glass", "trim_primary", "trim_secondary"
	};
	return (slot >= 0 && slot < VR_COCKPIT_SLOT_COUNT) ? names[slot] : "?";
}

int vr_cockpit_hull_shape(const char *model_name, int tris) {
	if (!model_name) {
		return -1;
	}
	for (int i = 0; i < ARRAY_LEN(vr_cockpit_hull_models); i++) {
		if (strcmp(vr_cockpit_hull_models[i].model, model_name) == 0 && vr_cockpit_hull_models[i].tris == tris) {
			return vr_cockpit_hull_models[i].shape;
		}
	}
	return -1;
}


// -----------------------------------------------------------------------------
// Triangle lists

void vr_cockpit_trilist_free(vr_cockpit_trilist_t *l) {
	free(l->tris);
	memset(l, 0, sizeof(*l));
}

void vr_cockpit_trilist_push(vr_cockpit_trilist_t *l, const vr_cockpit_tri_t *t) {
	if (l->count == l->cap) {
		int cap = l->cap ? l->cap * 2 : 256;
		vr_cockpit_tri_t *p = realloc(l->tris, (size_t)cap * sizeof(*p));
		if (!p) {
			abort();
		}
		l->tris = p;
		l->cap = cap;
	}
	l->tris[l->count++] = *t;
}


// -----------------------------------------------------------------------------
// The cut: clip polygons against the cabin box and keep the outside parts

typedef struct {
	float pos[3];
	float uv[2];
	float col[4];
} clip_vert_t;

#define CLIP_MAX 12

typedef struct {
	clip_vert_t v[CLIP_MAX];
	int n;
} clip_poly_t;

static clip_vert_t clip_lerp(const clip_vert_t *a, const clip_vert_t *b, float t) {
	clip_vert_t r;
	for (int i = 0; i < 3; i++) {
		r.pos[i] = a->pos[i] + (b->pos[i] - a->pos[i]) * t;
	}
	for (int i = 0; i < 2; i++) {
		r.uv[i] = a->uv[i] + (b->uv[i] - a->uv[i]) * t;
	}
	for (int i = 0; i < 4; i++) {
		r.col[i] = a->col[i] + (b->col[i] - a->col[i]) * t;
	}
	return r;
}

// Splits `poly` by the plane axis/value: `inside` has side >= 0 where side is
// (v - value) when `above`, else (value - v). Either result may be empty (n < 3).
static void clip_split(const clip_poly_t *poly, int axis, float value, bool above, clip_poly_t *inside, clip_poly_t *outside) {
	inside->n = 0;
	outside->n = 0;
	for (int i = 0; i < poly->n; i++) {
		const clip_vert_t *a = &poly->v[i];
		const clip_vert_t *b = &poly->v[(i + 1) % poly->n];
		float sa = above ? a->pos[axis] - value : value - a->pos[axis];
		float sb = above ? b->pos[axis] - value : value - b->pos[axis];
		if (sa >= 0) {
			inside->v[inside->n++] = *a;
		}
		else {
			outside->v[outside->n++] = *a;
		}
		if ((sa >= 0) != (sb >= 0)) {
			clip_vert_t m = clip_lerp(a, b, sa / (sa - sb));
			inside->v[inside->n++] = m;
			outside->v[outside->n++] = m;
		}
	}
}

static float clip_poly_area(const clip_poly_t *p) {
	float ax = 0, ay = 0, az = 0;
	for (int i = 1; i + 1 < p->n; i++) {
		float e1[3], e2[3];
		for (int k = 0; k < 3; k++) {
			e1[k] = p->v[i].pos[k] - p->v[0].pos[k];
			e2[k] = p->v[i + 1].pos[k] - p->v[0].pos[k];
		}
		ax += e1[1] * e2[2] - e1[2] * e2[1];
		ay += e1[2] * e2[0] - e1[0] * e2[2];
		az += e1[0] * e2[1] - e1[1] * e2[0];
	}
	return 0.5f * sqrtf(ax * ax + ay * ay + az * az);
}

static uint8_t to_byte(float v) {
	return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : floorf(v + 0.5f));
}

static int clip_emit(const clip_poly_t *p, int32_t texture, vr_cockpit_trilist_t *out) {
	if (p->n < 3 || clip_poly_area(p) < 1e-3f) {
		return 0;
	}
	int pieces = 0;
	for (int i = 1; i + 1 < p->n; i++) {
		const clip_vert_t *tri[3] = {&p->v[0], &p->v[i], &p->v[i + 1]};
		vr_cockpit_tri_t t;
		t.texture = texture;
		for (int k = 0; k < 3; k++) {
			memcpy(t.v[k].pos, tri[k]->pos, sizeof(t.v[k].pos));
			memcpy(t.v[k].uv, tri[k]->uv, sizeof(t.v[k].uv));
			for (int c = 0; c < 4; c++) {
				t.v[k].color[c] = to_byte(tri[k]->col[c]);
			}
		}
		vr_cockpit_trilist_push(out, &t);
		pieces++;
	}
	return pieces;
}

int vr_cockpit_clip_triangle(const vr_cockpit_tri_t *tri, const vr_cockpit_cut_t *cut, vr_cockpit_trilist_t *out) {
	clip_poly_t poly;
	poly.n = 3;
	for (int k = 0; k < 3; k++) {
		memcpy(poly.v[k].pos, tri->v[k].pos, sizeof(poly.v[k].pos));
		memcpy(poly.v[k].uv, tri->v[k].uv, sizeof(poly.v[k].uv));
		for (int c = 0; c < 4; c++) {
			poly.v[k].col[c] = tri->v[k].color[c];
		}
	}

	// The box's five planes (the top is open): what is outside plane p but inside
	// the earlier ones is outside the box. The rest is inside it and dropped.
	const struct { int axis; float value; bool above; } planes[5] = {
		{0, cut->x0, true}, {0, cut->x1, false},
		{1, cut->y_max, false},
		{2, cut->z0, true}, {2, cut->z1, false}
	};
	int pieces = 0;
	for (int i = 0; i < 5; i++) {
		clip_poly_t inside, outside;
		clip_split(&poly, planes[i].axis, planes[i].value, planes[i].above, &inside, &outside);
		pieces += clip_emit(&outside, tri->texture, out);
		if (inside.n < 3) {
			break;
		}
		poly = inside;
	}
	return pieces;
}

void vr_cockpit_inside_copy(const vr_cockpit_trilist_t *front, float dim, vr_cockpit_trilist_t *out) {
	for (int i = 0; i < front->count; i++) {
		vr_cockpit_tri_t t = front->tris[i];
		vr_cockpit_vertex_t tmp = t.v[1];
		t.v[1] = t.v[2];
		t.v[2] = tmp;
		for (int k = 0; k < 3; k++) {
			for (int c = 0; c < 3; c++) {
				t.v[k].color[c] = to_byte(t.v[k].color[c] * dim);
			}
		}
		vr_cockpit_trilist_push(out, &t);
	}
}


// -----------------------------------------------------------------------------
// Trim. The rule, as tools/mockup/cockpit_geometry.json records it.

#define TRIM_SAMPLES 6
#define TRIM_MIN_SAT 0.35
#define TRIM_MIN_VAL 0.20
#define TRIM_BINS 24
#define TRIM_SECONDARY_MIN_BINS 3
#define TRIM_SECONDARY_MIN_WEIGHT 0.15
#define TRIM_FALLBACK_VALUE 0.55
#define TRIM_VALUE_MIN 0.70
#define TRIM_VALUE_MAX 0.95
#define TRIM_NEUTRAL_BELOW 0.005
// The texel of an untextured polygon: RENDER_NO_TEXTURE is a 128 grey (the shader doubles it)
#define TRIM_NO_TEXTURE_TEXEL 128.0

static const double trim_bary[TRIM_SAMPLES][3] = {
	{2.0 / 3, 1.0 / 6, 1.0 / 6}, {1.0 / 6, 2.0 / 3, 1.0 / 6}, {1.0 / 6, 1.0 / 6, 2.0 / 3},
	{1.0 / 6, 5.0 / 12, 5.0 / 12}, {5.0 / 12, 1.0 / 6, 5.0 / 12}, {5.0 / 12, 5.0 / 12, 1.0 / 6}
};

static void rgb_to_hsv(double r, double g, double b, double *h, double *s, double *v) {
	double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
	double hue = 0;
	if (d > 0) {
		if (mx == r) {
			hue = fmod((g - b) / d, 6.0);
		}
		else if (mx == g) {
			hue = (b - r) / d + 2;
		}
		else {
			hue = (r - g) / d + 4;
		}
		hue *= 60;
		if (hue < 0) {
			hue += 360;
		}
	}
	*h = hue;
	*s = mx > 0 ? d / mx : 0;
	*v = mx / 255.0;
}

static double js_round(double v) {
	return floor(v + 0.5);
}

// The bin's mean RGB around bin i (i-1, i, i+1); false when it holds nothing
static bool trim_mean_around(double acc[TRIM_BINS][4], int i, double out[3]) {
	double m[4] = {0, 0, 0, 0};
	for (int j = i - 1; j <= i + 1; j++) {
		const double *a = acc[(j + TRIM_BINS) % TRIM_BINS];
		for (int c = 0; c < 4; c++) {
			m[c] += a[c];
		}
	}
	if (m[3] <= 0) {
		return false;
	}
	for (int c = 0; c < 3; c++) {
		out[c] = m[c] / m[3];
	}
	return true;
}

// Scale to a max channel / 255 inside the value range, keeping hue and saturation
static void trim_norm(const double rgb[3], uint8_t out[3]) {
	double mx = fmax(fmax(rgb[0], fmax(rgb[1], rgb[2])), 1.0) / 255.0;
	double tgt = fmin(TRIM_VALUE_MAX, fmax(TRIM_VALUE_MIN, mx));
	for (int c = 0; c < 3; c++) {
		out[c] = (uint8_t)js_round(rgb[c] * tgt / mx);
	}
}

void vr_cockpit_sample_trim(const vr_cockpit_tri_t *tris, int count, const vr_cockpit_image_t *images, int image_count, vr_cockpit_trim_t *out) {
	double bins[TRIM_BINS] = {0};
	double acc[TRIM_BINS][4];
	memset(acc, 0, sizeof(acc));
	double total = 0;

	for (int t = 0; t < count; t++) {
		const vr_cockpit_tri_t *tri = &tris[t];
		double e1[3], e2[3];
		for (int k = 0; k < 3; k++) {
			e1[k] = tri->v[1].pos[k] - tri->v[0].pos[k];
			e2[k] = tri->v[2].pos[k] - tri->v[0].pos[k];
		}
		double cx = e1[1] * e2[2] - e1[2] * e2[1];
		double cy = e1[2] * e2[0] - e1[0] * e2[2];
		double cz = e1[0] * e2[1] - e1[1] * e2[0];
		double area = 0.5 * sqrt(cx * cx + cy * cy + cz * cz);
		if (area <= 0) {
			continue;
		}
		double w = area / TRIM_SAMPLES;
		const vr_cockpit_image_t *img = (tri->texture >= 0 && tri->texture < image_count && images) ? &images[tri->texture] : NULL;

		for (int s = 0; s < TRIM_SAMPLES; s++) {
			const double *b = trim_bary[s];
			double u = 0, v = 0, vc[3] = {0, 0, 0};
			for (int k = 0; k < 3; k++) {
				u += b[k] * tri->v[k].uv[0];
				v += b[k] * tri->v[k].uv[1];
				for (int c = 0; c < 3; c++) {
					vc[c] += b[k] * tri->v[k].color[c];
				}
			}
			double rgb[3];
			if (img && img->w > 0 && img->h > 0 && img->rgba) {
				// A sample exactly on a texel boundary takes the lower texel (the first texel at 0),
				// so float rounding cannot flip it. One outside the texture has no texel: skipped.
				int x = (int)ceil(u - 1e-4) - 1, y = (int)ceil(v - 1e-4) - 1;
				x = (x < 0 && u >= 0) ? 0 : x;
				y = (y < 0 && v >= 0) ? 0 : y;
				if (x < 0 || x >= img->w || y < 0 || y >= img->h) {
					continue;
				}
				const uint8_t *px = img->rgba + ((size_t)y * img->w + x) * 4;
				if (px[3] == 0) {
					continue;
				}
				for (int c = 0; c < 3; c++) {
					rgb[c] = fmin(255.0, px[c] * vc[c] / 127.5);
				}
			}
			else {
				for (int c = 0; c < 3; c++) {
					rgb[c] = fmin(255.0, TRIM_NO_TEXTURE_TEXEL * vc[c] / 127.5);
				}
			}
			// the colour on screen is 8 bits: round it, so the hue bins do not depend on float noise
			for (int c = 0; c < 3; c++) {
				rgb[c] = floor(rgb[c] + 0.5);
			}
			total += w;
			double h, sat, val;
			rgb_to_hsv(rgb[0], rgb[1], rgb[2], &h, &sat, &val);
			if (sat < TRIM_MIN_SAT || val < TRIM_MIN_VAL) {
				continue;
			}
			int bi = (int)floor(h / (360.0 / TRIM_BINS)) % TRIM_BINS;
			bins[bi] += w;
			for (int c = 0; c < 3; c++) {
				acc[bi][c] += rgb[c] * w;
			}
			acc[bi][3] += w;
		}
	}

	double sm[TRIM_BINS];
	for (int i = 0; i < TRIM_BINS; i++) {
		sm[i] = 1 * bins[(i + TRIM_BINS - 1) % TRIM_BINS] + 2 * bins[i] + 1 * bins[(i + 1) % TRIM_BINS];
	}
	int pk = 0;
	for (int i = 1; i < TRIM_BINS; i++) {
		if (sm[i] > sm[pk]) {
			pk = i;
		}
	}
	double sat_total = 0;
	for (int i = 0; i < TRIM_BINS; i++) {
		sat_total += bins[i];
	}

	double primary_raw[3] = {128, 128, 128};
	trim_mean_around(acc, pk, primary_raw);
	int sk = -1;
	for (int i = 0; i < TRIM_BINS; i++) {
		int d = abs(i - pk);
		int dist = d < TRIM_BINS - d ? d : TRIM_BINS - d;
		if (dist >= TRIM_SECONDARY_MIN_BINS && sm[i] >= TRIM_SECONDARY_MIN_WEIGHT * sm[pk] && (sk < 0 || sm[i] > sm[sk])) {
			sk = i;
		}
	}

	memset(out, 0, sizeof(*out));
	trim_norm(primary_raw, out->primary);
	double secondary_raw[3];
	if (sk >= 0 && trim_mean_around(acc, sk, secondary_raw)) {
		trim_norm(secondary_raw, out->secondary);
	}
	else {
		out->secondary_fallback = true;
		for (int c = 0; c < 3; c++) {
			out->secondary[c] = (uint8_t)js_round(out->primary[c] * TRIM_FALLBACK_VALUE);
		}
	}
	out->saturated_share = total > 0 ? (float)(sat_total / total) : 0;
	if (out->saturated_share < TRIM_NEUTRAL_BELOW) {
		memcpy(out->primary, vr_cockpit_slot_rgb[VR_COCKPIT_SLOT_FRAME], 3);
		memcpy(out->secondary, vr_cockpit_slot_rgb[VR_COCKPIT_SLOT_FRAME_DARK], 3);
		out->neutral = true;
		out->secondary_fallback = false;
	}
}


// -----------------------------------------------------------------------------
// The authored parts

#define LIGHT_AMBIENT 0.55f
#define SLOT_EMISSIVE_TRIM 0.25f

// The sun of the mockup (cockpit_mockup.js SUN), (0.35, 1, 0.45) in its y-up,
// z-back axes, in the game's: +y down, +z forward.
static const float sun_dir[3] = {0.35f / 1.151087f, -1.0f / 1.151087f, -0.45f / 1.151087f}; // unit: |(0.35, 1, 0.45)| = 1.151087

float vr_cockpit_light(const float a[3], const float b[3], const float c[3], const float toward[3], float emissive) {
	float e1[3], e2[3], n[3];
	for (int k = 0; k < 3; k++) {
		e1[k] = b[k] - a[k];
		e2[k] = c[k] - a[k];
	}
	n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (len < 1e-9f) {
		return LIGHT_AMBIENT + emissive;
	}
	float facing = 0, d = 0;
	for (int k = 0; k < 3; k++) {
		n[k] /= len;
		facing += n[k] * (toward[k] - a[k]);
		d += n[k] * sun_dir[k];
	}
	if (facing < 0) {
		d = -d; // double-sided: light the side that faces the eye
	}
	if (d < 0) {
		d = 0;
	}
	return LIGHT_AMBIENT + (1.0f - LIGHT_AMBIENT) * d + emissive;
}

void vr_cockpit_build_parts(int shape, const uint8_t trim[2][3], vr_cockpit_trilist_t *opaque, vr_cockpit_trilist_t *glass) {
	const float toward[3] = {0, VR_COCKPIT_EYE_Y, 0};
	for (int i = 0; i < ARRAY_LEN(vr_cockpit_parts); i++) {
		const vr_cockpit_part_t *p = &vr_cockpit_parts[i];
		if (!(p->hull_mask & (1u << shape))) {
			continue;
		}
		const uint8_t *rgb = vr_cockpit_slot_rgb[p->slot];
		float emissive = 0;
		if (p->slot == VR_COCKPIT_SLOT_TRIM_PRIMARY) {
			rgb = trim[0];
			emissive = SLOT_EMISSIVE_TRIM;
		}
		else if (p->slot == VR_COCKPIT_SLOT_TRIM_SECONDARY) {
			rgb = trim[1];
			emissive = SLOT_EMISSIVE_TRIM;
		}
		bool is_glass = p->slot == VR_COCKPIT_SLOT_GLASS;

		for (uint32_t t = 0; t < p->tri_count; t++) {
			const float *v = &vr_cockpit_verts[((size_t)p->first_tri + t) * 9];
			vr_cockpit_tri_t tri;
			tri.texture = -1;
			float l = is_glass ? 1.0f : vr_cockpit_light(v, v + 3, v + 6, toward, emissive);
			uint8_t color[4];
			for (int c = 0; c < 3; c++) {
				color[c] = to_byte(fminf(255.0f, rgb[c] * l)); // untextured: RENDER_NO_TEXTURE is a 128 grey, which the shader's x2 makes 1.0
			}
			color[3] = is_glass ? to_byte(VR_COCKPIT_GLASS_ALPHA * 255.0f) : 255;
			for (int k = 0; k < 3; k++) {
				memcpy(tri.v[k].pos, v + k * 3, sizeof(tri.v[k].pos));
				tri.v[k].uv[0] = tri.v[k].uv[1] = 0;
				memcpy(tri.v[k].color, color, 4);
			}
			vr_cockpit_trilist_push(is_glass ? glass : opaque, &tri);
		}
	}
}


// -----------------------------------------------------------------------------
// The eye and the bank modes

void vr_cockpit_settings_sanitize(vr_cockpit_settings_t *s) {
	if (s->bank < 0 || s->bank >= VR_COCKPIT_BANK_COUNT) {
		s->bank = VR_COCKPIT_BANK_LEVEL;
	}
	float *cm[2] = {&s->eye_forward_cm, &s->eye_up_cm};
	for (int i = 0; i < 2; i++) {
		if (!(*cm[i] == *cm[i])) {
			*cm[i] = 0;
		}
		*cm[i] = fminf(VR_COCKPIT_EYE_RANGE_CM, fmaxf(-VR_COCKPIT_EYE_RANGE_CM, *cm[i]));
	}
}

void vr_cockpit_eye(const vr_cockpit_settings_t *s, float world_scale, float out[3]) {
	out[0] = 0;
	out[1] = VR_COCKPIT_EYE_Y - s->eye_up_cm / 100.0f * world_scale;
	out[2] = s->eye_forward_cm / 100.0f * world_scale;
}

static void mat3_mulv(const float m[9], const float v[3], float out[3]) {
	float r[3];
	for (int row = 0; row < 3; row++) {
		r[row] = m[row] * v[0] + m[3 + row] * v[1] + m[6 + row] * v[2];
	}
	memcpy(out, r, sizeof(r));
}

static void model_matrix(const float rot[9], const float t[3], float out[16]) {
	for (int c = 0; c < 3; c++) {
		for (int r = 0; r < 3; r++) {
			out[c * 4 + r] = rot[c * 3 + r];
		}
		out[c * 4 + 3] = 0;
	}
	out[12] = t[0];
	out[13] = t[1];
	out[14] = t[2];
	out[15] = 1;
}

void vr_cockpit_pose(int bank, const float ship_pos[3], const float ship_angles[3], const float eye_ship[3], vr_cockpit_pose_t *out) {
	memcpy(out->eye_ship, eye_ship, sizeof(out->eye_ship));

	// The head sits where the eye is through the ship's full matrix, in every mode
	float ship_rot[9], offset[3];
	vr_game_ship_rotation(ship_angles, ship_rot);
	mat3_mulv(ship_rot, eye_ship, offset);
	for (int i = 0; i < 3; i++) {
		out->eye_world[i] = ship_pos[i] + offset[i];
	}

	out->anchor_angles[0] = ship_angles[0];
	out->anchor_angles[1] = ship_angles[1];
	out->anchor_angles[2] = bank == VR_COCKPIT_BANK_FOLLOW ? ship_angles[2] : 0.0f;

	if (bank == VR_COCKPIT_BANK_LEVEL) {
		// T(E) * R(yaw, pitch, 0) * T(-e): the cockpit turns about the eye and never rolls
		const float level_angles[3] = {ship_angles[0], ship_angles[1], 0.0f};
		float level_rot[9], t[3];
		vr_game_ship_rotation(level_angles, level_rot);
		mat3_mulv(level_rot, eye_ship, t);
		for (int i = 0; i < 3; i++) {
			t[i] = out->eye_world[i] - t[i];
		}
		model_matrix(level_rot, t, out->model);
	}
	else {
		// follow and cockpit: the ship's own matrix
		model_matrix(ship_rot, ship_pos, out->model);
	}
}
