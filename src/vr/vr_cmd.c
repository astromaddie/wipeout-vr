#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr_cmd.h"
#include "vr_log.h"

static void *grow(void *p, uint32_t *cap, uint32_t needed, size_t size) {
	if (needed <= *cap) {
		return p;
	}
	uint32_t new_cap = *cap ? *cap : 64;
	while (new_cap < needed) {
		new_cap *= 2;
	}
	void *r = realloc(p, (size_t)new_cap * size);
	if (!r) {
		vr_logf("out of memory growing the command list to %u entries", new_cap);
		abort();
	}
	*cap = new_cap;
	return r;
}

void vr_cmdlist_init(vr_cmdlist_t *l) {
	memset(l, 0, sizeof(*l));
}

void vr_cmdlist_free(vr_cmdlist_t *l) {
	free(l->cmds);
	free(l->verts);
	free(l->cams);
	memset(l, 0, sizeof(*l));
}

void vr_cmdlist_reset(vr_cmdlist_t *l, int32_t blend, bool cull) {
	l->cmd_len = 0;
	l->vert_len = 0;
	l->cam_len = 0;
	l->initial_blend = blend;
	l->initial_cull = cull;
	l->sprite_count = 0;
	l->tris_2d = 0;
	l->tris_3d = 0;
}

vr_cmd_t *vr_cmdlist_push(vr_cmdlist_t *l, vr_cmd_type_t type, uint8_t flags) {
	l->cmds = grow(l->cmds, &l->cmd_cap, l->cmd_len + 1, sizeof(vr_cmd_t));
	vr_cmd_t *c = &l->cmds[l->cmd_len++];
	memset(c, 0, sizeof(*c));
	c->type = (uint8_t)type;
	c->flags = flags;
	if (type == VR_CMD_SPRITE) {
		l->sprite_count++;
	}
	return c;
}

void vr_cmdlist_push_tris(vr_cmdlist_t *l, const vr_vertex_t v[3], uint8_t flags) {
	l->verts = grow(l->verts, &l->vert_cap, l->vert_len + 3, sizeof(vr_vertex_t));
	uint32_t index = l->vert_len / 3;
	memcpy(&l->verts[l->vert_len], v, 3 * sizeof(vr_vertex_t));
	l->vert_len += 3;

	if (flags & VR_CMD_FLAG_2D) {
		l->tris_2d++;
	}
	else {
		l->tris_3d++;
	}

	if (l->cmd_len > 0) {
		vr_cmd_t *last = &l->cmds[l->cmd_len - 1];
		if (last->type == VR_CMD_TRIS && last->flags == flags && last->u.tris.first + last->u.tris.count == index) {
			last->u.tris.count++;
			return;
		}
	}
	vr_cmd_t *c = vr_cmdlist_push(l, VR_CMD_TRIS, flags);
	c->u.tris.first = index;
	c->u.tris.count = 1;
}

int32_t vr_cmdlist_push_cam(vr_cmdlist_t *l, const vr_camset_t *cam) {
	l->cams = grow(l->cams, &l->cam_cap, l->cam_len + 1, sizeof(vr_camset_t));
	l->cams[l->cam_len] = *cam;
	return (int32_t)l->cam_len++;
}
