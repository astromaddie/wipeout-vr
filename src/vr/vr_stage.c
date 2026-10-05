#include <string.h>

#include "vr_stage.h"

void vr_render_settings_from(vr_render_settings_t *out, const vr_settings_t *s) {
	memset(out, 0, sizeof(*out));
	out->rig.world_scale = (float)vr_settings_get(s, "world_scale");
	out->rig.head_translation = (float)vr_settings_get(s, "head_translation");
	out->rig.follow_roll = vr_settings_get(s, "follow_vehicle_rotation") != 0;
	out->rig.horizon_lock = vr_settings_get(s, "horizon_lock") != 0;
	out->hud_distance = (float)vr_settings_get(s, "hud_distance");
	out->hud_scale = (float)vr_settings_get(s, "hud_scale");
	out->hud_mode = (int)vr_settings_get(s, "hud_mode");
	out->force_mono = vr_settings_get(s, "force_mono") != 0;
	out->diag_attract_stereo = vr_settings_get(s, "diag_attract_stereo") != 0;
	out->panel_distance = (float)vr_settings_get(s, "panel_distance");
	out->panel_width = (float)vr_settings_get(s, "panel_width");
	out->diag_yaw = (float)(vr_settings_get(s, "diag_yaw") * 3.14159265358979323846 / 180.0);
	out->dump_frame = (int)vr_settings_get(s, "dump_frame");
	out->timing = vr_settings_get(s, "timing") != 0;
	out->timing_gpu = vr_settings_get(s, "timing_gpu") != 0;
	out->ui_width = VR_UI_WIDTH;
	out->ui_height = VR_UI_HEIGHT;
	vr_cockpit_settings_from(&out->cockpit, s);
}

void vr_cockpit_settings_from(vr_cockpit_settings_t *out, const vr_settings_t *s) {
	out->enabled = vr_settings_get(s, "cockpit") != 0;
	out->bank = (int)vr_settings_get(s, "cockpit_bank");
	out->eye_forward_cm = (float)vr_settings_get(s, "cockpit_eye_forward_cm");
	out->eye_up_cm = (float)vr_settings_get(s, "cockpit_eye_up_cm");
	out->glass = vr_settings_get(s, "cockpit_glass") != 0;
	out->world_scale = (float)vr_settings_get(s, "world_scale");
	vr_cockpit_settings_sanitize(out);
}

float vr_fade_alpha(float seconds) {
	if (seconds < 0) {
		return 1.0f;
	}
	if (seconds < VR_FADE_HOLD_S) {
		return 1.0f;
	}
	float t = (seconds - VR_FADE_HOLD_S) / VR_FADE_OUT_S;
	return t >= 1.0f ? 0.0f : 1.0f - t;
}

void vr_stage_init(vr_stage_t *s) {
	memset(s, 0, sizeof(*s));
	s->mode = VR_MODE_NONE;
}

void vr_stage_request_recentre(vr_stage_t *s) {
	s->recentre_pending = true;
	s->panel_pending = true;
}

bool vr_stage_set_mode(vr_stage_t *s, vr_mode_t mode, int64_t now_ns) {
	if (mode == s->mode) {
		return false;
	}
	s->mode = mode;
	s->mode_switches++;
	s->fade_active = true;
	s->fade_start_ns = now_ns;
	vr_stage_request_recentre(s);
	return true;
}

bool vr_stage_apply_recentre(vr_stage_t *s, const vr_view_t views[2]) {
	if (!s->recentre_pending && s->recentre_valid) {
		return false;
	}
	vr_pose_t head;
	vr_head_pose(views, &head);
	vr_recentre_from_head(&head, &s->recentre);
	s->recentre_valid = true;
	s->recentre_pending = false;
	s->recentres_applied++;
	return true;
}

void vr_stage_panel_pose(vr_stage_t *s, const vr_view_t views[2], bool views_valid, float distance, vr_pose_t *out) {
	if (s->panel_pending || !s->panel_valid) {
		vr_pose_t head = { .orientation = {0, 0, 0, 1} };
		if (views_valid) {
			vr_head_pose(views, &head);
		}
		vr_panel_pose(&head, distance, &s->panel_pose);
		s->panel_valid = views_valid;
		s->panel_pending = !views_valid;
	}
	*out = s->panel_pose;
}

float vr_stage_fade_alpha(vr_stage_t *s, int64_t now_ns) {
	if (!s->fade_active) {
		return 0.0f;
	}
	float seconds = (float)((double)(now_ns - s->fade_start_ns) * 1e-9);
	if (seconds >= VR_FADE_HOLD_S + VR_FADE_OUT_S) {
		s->fade_active = false;
		return 0.0f;
	}
	return vr_fade_alpha(seconds);
}
