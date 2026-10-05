#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sfvr_test.h"
#include "vr_log.h"
#include "vr_settings.h"
#include "vr_stage.h"

static void quiet(const char *line) { (void)line; }

static double get(const vr_settings_t *s, const char *name) {
	return vr_settings_get(s, name);
}

static double file_value(const vr_settings_t *s, const char *name) {
	return s->file_values[vr_settings_index(name)];
}

// Parse `text` into a fresh settings struct; returns the number of repairs.
static int parse(vr_settings_t *s, const char *text) {
	memset(s, 0, sizeof(*s));
	vr_settings_defaults(s);
	int repairs = vr_settings_parse(s, text, strlen(text));
	for (size_t i = 0; i < vr_settings_count(); i++) {
		s->values[i] = s->file_values[i];
	}
	return repairs;
}

static char *format(const vr_settings_t *s) {
	char *text = vr_settings_format(s, NULL);
	return text ? text : strdup("");
}

static bool has(const char *haystack, const char *needle) {
	return strstr(haystack, needle) != NULL;
}

static char *read_all(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return strdup("");
	}
	char *buf = calloc(1, 1 << 16);
	size_t n = fread(buf, 1, (1 << 16) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static void write_all(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

static const char *no_env(const char *name) {
	(void)name;
	return NULL;
}


static void test_registry(void) {
	// the standard keys the doc's section 7 lists, and the port's own
	const char *names[] = {
		"world_scale", "follow_vehicle_rotation", "horizon_lock", "hud_mode", "hud_distance", "hud_scale",
		"force_mono", "diag_attract_stereo", "diag_attract_cockpit", "panel_distance", "panel_width",
		"cockpit", "cockpit_bank", "cockpit_eye_forward_cm", "cockpit_eye_up_cm", "cockpit_glass",
		"resolution_scale", "eye_size", "timing_gpu", "refresh_rate", "probe_seconds", "force_render", "diag_yaw", "dump_frame", "timing",
		"turn_mode", "snap_angle", "vignette", "seated", "haptics", "dominant_hand"
	};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		CHECK(vr_settings_index(names[i]) >= 0);
	}
	CHECK_INT(vr_settings_index("nonsense"), -1);
	CHECK_INT(vr_settings_index(NULL), -1);
	CHECK(vr_settings_count() <= VR_SETTINGS_MAX_KEYS);
	CHECK_INT(vr_settings_count(), sfvr_settings_count() + 12);

	// env names
	char name[64];
	CHECK(sfvr_settings_env_name("WIPEOUT", "resolution_scale", name, sizeof(name)));
	CHECK_STR(name, "WIPEOUT_VR_RESOLUTION_SCALE");
}

static void test_defaults_and_creation(void) {
	vr_settings_t s;
	CHECK_INT(parse(&s, ""), (int)vr_settings_count() + 1); // every key and the version
	CHECK_NEAR(get(&s, "snap_angle"), 30, 1e-9);
	CHECK_NEAR(get(&s, "resolution_scale"), 1.0, 1e-9); // a multiplier on eye_size
	CHECK_NEAR(get(&s, "eye_size"), 2160, 1e-9); // the port's display defaults; refresh_rate is not sfvr's 90
	CHECK_NEAR(get(&s, "refresh_rate"), 120, 1e-9);
	CHECK_NEAR(get(&s, "world_scale"), 88, 1e-9);
	CHECK_NEAR(get(&s, "force_mono"), 0, 1e-9);
	CHECK_NEAR(get(&s, "panel_distance"), 2.0, 1e-9);
	CHECK_NEAR(get(&s, "probe_seconds"), 0, 1e-9);
	CHECK_NEAR(get(&s, "hud_mode"), 0, 1e-9);
	CHECK_NEAR(get(&s, "nonsense"), 0, 1e-9);

	char *text = format(&s);
	CHECK(has(text, "version = 2\n"));
	CHECK(has(text, "[vr]\n"));
	CHECK(has(text, "turn_mode = \"snap\"\n"));
	CHECK(has(text, "snap_angle = 30\n"));
	CHECK(has(text, "resolution_scale = 1.0\n"));
	CHECK(has(text, "eye_size = 2160\n"));
	CHECK(has(text, "refresh_rate = 120\n"));
	CHECK(has(text, "vignette = false\n"));
	CHECK(has(text, "world_scale = 88.0\n"));
	CHECK(has(text, "hud_mode = \"near\"\n"));
	CHECK(has(text, "WIPEOUT_VR_<KEY>"));

	// what was written reads back with nothing to repair, and is a fixed point
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	for (size_t i = 0; i < vr_settings_count(); i++) {
		CHECK_NEAR(back.file_values[i], s.file_values[i], 1e-6);
	}
	char *again = format(&back);
	CHECK_STR(again, text);
	free(text);
	free(again);
	vr_settings_free(&s);
	vr_settings_free(&back);
}

static void test_missing_keys_get_defaults(void) {
	vr_settings_t s;
	const char *file =
		"version = 2\n"
		"\n"
		"[vr]\n"
		"snap_angle = 45   # my favourite\n"
		"turn_mode = \"smooth\"\n";
	int repairs = parse(&s, file);
	CHECK_INT(repairs, (int)vr_settings_count() - 2);
	CHECK_NEAR(get(&s, "snap_angle"), 45, 1e-9);
	CHECK_NEAR(get(&s, "turn_mode"), 1, 1e-9);
	CHECK_NEAR(get(&s, "resolution_scale"), 1.0, 1e-9);

	char *text = format(&s);
	// the user's lines are untouched, comment included; the rest is added in [vr]
	CHECK(has(text, "snap_angle = 45   # my favourite\n"));
	CHECK(has(text, "turn_mode = \"smooth\"\n"));
	CHECK(has(text, "refresh_rate = 120\n"));
	CHECK(strstr(text, "refresh_rate") > strstr(text, "[vr]"));
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	free(text);
	vr_settings_free(&s);
	vr_settings_free(&back);
}

static void test_bad_values_are_clamped_or_defaulted(void) {
	vr_settings_t s;
	const char *file =
		"version = 2\n"
		"[vr]\n"
		"snap_angle = 500\n"             // above the range: 90
		"smooth_turn_speed = 1\n"        // below: 30
		"resolution_scale = 0.1\n"
		"refresh_rate = 1000\n"
		"hud_distance = 99.5\n"
		"lean_limit = \"fast\"\n"        // not a number: default
		"head_translation = nan\n"       // not usable: default
		"vignette_strength = 2\n"
		"hud_mode = \"banana\"\n"        // unknown enum: default
		"dominant_hand = \"LEFT\"\n"     // case-insensitive
		"turn_mode = 7\n"                // an index is not a name
		"vignette = maybe\n"
		"seated = yes\n"
		"roomscale = TRUE\n"
		"duck_crouch = 0\n"
		"snap_angle_extra = 3\n"         // unknown, kept
		"dump_frame = 12.6\n"            // an int key given a float
		"probe_seconds = -4\n"
		"haptics = 0.25\n";
	int repairs = parse(&s, file);
	CHECK(repairs > 0);
	CHECK_NEAR(get(&s, "snap_angle"), 90, 1e-9);
	CHECK_NEAR(get(&s, "smooth_turn_speed"), 30, 1e-9);
	CHECK_NEAR(get(&s, "resolution_scale"), 0.75, 1e-6);
	CHECK_NEAR(get(&s, "refresh_rate"), 144, 1e-9);
	CHECK_NEAR(get(&s, "hud_distance"), 20.0, 1e-6);
	CHECK_NEAR(get(&s, "lean_limit"), 0.35, 1e-6);
	CHECK_NEAR(get(&s, "head_translation"), 1.0, 1e-6);
	CHECK_NEAR(get(&s, "vignette_strength"), 1.0, 1e-6);
	CHECK_NEAR(get(&s, "hud_mode"), 0, 1e-9);
	CHECK_NEAR(get(&s, "dominant_hand"), 1, 1e-9);
	CHECK_NEAR(get(&s, "turn_mode"), 0, 1e-9);
	CHECK_NEAR(get(&s, "vignette"), 0, 1e-9);
	CHECK_NEAR(get(&s, "seated"), 1, 1e-9);
	CHECK_NEAR(get(&s, "roomscale"), 1, 1e-9);
	CHECK_NEAR(get(&s, "duck_crouch"), 0, 1e-9);
	CHECK_NEAR(get(&s, "dump_frame"), 13, 1e-9);
	CHECK_NEAR(get(&s, "probe_seconds"), 0, 1e-9);
	CHECK_NEAR(get(&s, "haptics"), 0.25, 1e-6);

	// the repaired file shows the repaired values and keeps the unknown key
	char *text = format(&s);
	CHECK(has(text, "snap_angle = 90\n"));
	CHECK(has(text, "resolution_scale = 0.75\n"));
	CHECK(has(text, "lean_limit = 0.35\n"));
	CHECK(has(text, "hud_mode = \"near\"\n"));
	CHECK(has(text, "dominant_hand = \"LEFT\"\n")); // valid as it was: untouched
	CHECK(has(text, "snap_angle_extra = 3\n"));
	CHECK(!has(text, "snap_angle = 500"));
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	CHECK_NEAR(get(&back, "snap_angle"), 90, 1e-9);
	free(text);
	vr_settings_free(&s);
	vr_settings_free(&back);
}

static void test_unknown_content_is_kept(void) {
	vr_settings_t s;
	const char *file =
		"# my notes\n"
		"title = \"hello\"\n"
		"\n"
		"[game]\n"
		"snap_angle = 12\n"             // another table's key with a vr name
		"name = 'x'\n"
		"\n"
		"[vr]\n"
		"mystery = 5\n"
		"# a comment inside\n"
		"snap_angle = 60\n"
		"future_flag = true\n"
		"\n"
		"[later]\n"
		"a = 1\n";
	parse(&s, file);
	CHECK_NEAR(get(&s, "snap_angle"), 60, 1e-9); // the [vr] one, not [game]'s

	char *text = format(&s);
	CHECK(has(text, "# my notes\n"));
	CHECK(has(text, "title = \"hello\"\n"));
	CHECK(has(text, "[game]\nsnap_angle = 12\nname = 'x'\n"));
	CHECK(has(text, "mystery = 5\n"));
	CHECK(has(text, "# a comment inside\n"));
	CHECK(has(text, "future_flag = true\n"));
	CHECK(has(text, "[later]\na = 1\n"));
	// the added keys went into [vr], before [later]
	CHECK(strstr(text, "refresh_rate") > strstr(text, "[vr]"));
	CHECK(strstr(text, "refresh_rate") < strstr(text, "[later]"));
	CHECK(strstr(text, "version = 2") != NULL);
	CHECK(strstr(text, "version = 2") < strstr(text, "[game]"));

	// the first lines stay on top: the version goes after the comment block
	CHECK(strncmp(text, "# my notes\n", 11) == 0);
	free(text);
	vr_settings_free(&s);
}

static void test_version(void) {
	vr_settings_t s;
	char *text;

	parse(&s, "[vr]\nsnap_angle = 30\n");
	CHECK_INT(s.file_version, 0);
	text = format(&s);
	CHECK(has(text, "version = 2\n"));
	CHECK(strstr(text, "version = 2") < strstr(text, "[vr]"));
	free(text);
	vr_settings_free(&s);

	parse(&s, "version = 0\n[vr]\n");
	text = format(&s);
	CHECK(has(text, "version = 2\n"));
	CHECK(!has(text, "version = 0"));
	free(text);
	vr_settings_free(&s);

	// a file from a newer build keeps its version
	parse(&s, "version = 7\n[vr]\n");
	CHECK_INT(s.file_version, 7);
	text = format(&s);
	CHECK(has(text, "version = 7\n"));
	free(text);
	vr_settings_free(&s);
}

// Schema 2 moved refresh_rate's default (90 -> 120) and added eye_size (2160). The Frame's file stores
// every key, so a stored old refresh default has to move; a user's choice stays. resolution_scale
// is now a multiplier on eye_size and keeps its default of 1.0, so a stored 1.0 is already right.
static void test_migration_to_v2(void) {
	vr_settings_t s;
	char *text;

	// the Frame's file as 739a8d7 wrote it: version 1, the old defaults stored explicitly
	const char *old_defaults =
		"# my notes\n"
		"version = 1\n"
		"\n"
		"[vr]\n"
		"refresh_rate = 90\n"
		"resolution_scale = 1.0\n"
		"snap_angle = 45\n";
	int repairs = parse(&s, old_defaults);
	CHECK_INT(s.file_version, 1);
	CHECK_NEAR(file_value(&s, "refresh_rate"), 120, 1e-9);
	CHECK_NEAR(file_value(&s, "resolution_scale"), 1.0, 1e-9);
	CHECK_NEAR(file_value(&s, "eye_size"), 2160, 1e-9);
	CHECK_NEAR(file_value(&s, "snap_angle"), 45, 1e-9); // other keys are never touched
	// refresh_rate moved, the version, and every key the file did not have
	CHECK_INT(repairs, (int)vr_settings_count() - 3 + 1 + 1);
	text = format(&s);
	CHECK(has(text, "version = 2\n"));
	CHECK(!has(text, "version = 1"));
	CHECK(has(text, "refresh_rate = 120\n"));
	CHECK(has(text, "resolution_scale = 1.0\n"));
	CHECK(has(text, "eye_size = 2160\n"));
	CHECK(!has(text, "refresh_rate = 90"));
	CHECK(has(text, "# my notes\n"));
	CHECK(has(text, "snap_angle = 45\n"));
	// written once: it reads back clean, so the next start rewrites nothing
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	CHECK_INT(back.file_version, 2);
	free(text);
	vr_settings_free(&back);
	vr_settings_free(&s);

	// values the user chose are kept: 72 Hz, a lowered scale
	parse(&s, "version = 1\n[vr]\nrefresh_rate = 72\nresolution_scale = 0.9\n");
	CHECK_NEAR(file_value(&s, "refresh_rate"), 72, 1e-9);
	CHECK_NEAR(file_value(&s, "resolution_scale"), 0.9, 1e-6);
	text = format(&s);
	CHECK(has(text, "version = 2\n"));
	CHECK(has(text, "refresh_rate = 72\n"));
	CHECK(has(text, "resolution_scale = 0.9\n"));
	free(text);
	vr_settings_free(&s);

	// ... and so is anything else that is not the old default
	parse(&s, "version = 1\n[vr]\nrefresh_rate = 120\nresolution_scale = 1.5\n");
	CHECK_NEAR(file_value(&s, "refresh_rate"), 120, 1e-9);
	CHECK_NEAR(file_value(&s, "resolution_scale"), 1.5, 1e-9);
	vr_settings_free(&s);
	parse(&s, "version = 1\n[vr]\nrefresh_rate = 144\n");
	CHECK_NEAR(file_value(&s, "refresh_rate"), 144, 1e-9);
	vr_settings_free(&s);

	// a file with no version is older than 2 too
	parse(&s, "[vr]\nrefresh_rate = 90\nresolution_scale = 1.0\n");
	CHECK_NEAR(file_value(&s, "refresh_rate"), 120, 1e-9);
	vr_settings_free(&s);

	// a version-2 file that holds refresh_rate 90 chose it: kept
	CHECK_INT(parse(&s, "version = 2\n[vr]\nrefresh_rate = 90\nresolution_scale = 1.0\n"), (int)vr_settings_count() - 2);
	CHECK_NEAR(file_value(&s, "refresh_rate"), 90, 1e-9);
	text = format(&s);
	CHECK(has(text, "refresh_rate = 90\n"));
	free(text);
	vr_settings_free(&s);

	// a file from a newer build is left alone
	parse(&s, "version = 3\n[vr]\nrefresh_rate = 90\n");
	CHECK_NEAR(file_value(&s, "refresh_rate"), 90, 1e-9);
	vr_settings_free(&s);

	// eye_size is clamped; 0 is the old behaviour (the runtime's recommended size)
	parse(&s, "version = 2\n[vr]\neye_size = 9000\n");
	CHECK_NEAR(file_value(&s, "eye_size"), 4096, 1e-9);
	vr_settings_free(&s);
	parse(&s, "version = 2\n[vr]\neye_size = 0\n");
	CHECK_NEAR(file_value(&s, "eye_size"), 0, 1e-9);
	vr_settings_free(&s);
}

static void test_duplicates_and_odd_files(void) {
	vr_settings_t s;
	char *text;

	// the first of a duplicated key wins; the second is dropped
	parse(&s, "version = 2\n[vr]\nsnap_angle = 20\nsnap_angle = 80\n");
	CHECK_NEAR(get(&s, "snap_angle"), 20, 1e-9);
	text = format(&s);
	CHECK(has(text, "snap_angle = 20\n"));
	CHECK(!has(text, "snap_angle = 80"));
	free(text);
	vr_settings_free(&s);

	// CRLF line endings and no final newline
	parse(&s, "version = 2\r\n[vr]\r\nsnap_angle = 75\r\nvignette = true");
	CHECK_NEAR(get(&s, "snap_angle"), 75, 1e-9);
	CHECK_NEAR(get(&s, "vignette"), 1, 1e-9);
	text = format(&s);
	CHECK(has(text, "snap_angle = 75\r\n"));
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	free(text);
	vr_settings_free(&s);
	vr_settings_free(&back);

	// garbage lines are not keys and not an error
	parse(&s, "version = 2\n[vr]\n= = =\nsnap_angle 45\nsnap_angle = 45\n");
	CHECK_NEAR(get(&s, "snap_angle"), 45, 1e-9);
	vr_settings_free(&s);

	// a '#' inside a quoted value is not a comment
	parse(&s, "[vr]\nhud_mode = \"fa#r\"\n");
	CHECK_NEAR(get(&s, "hud_mode"), 0, 1e-9); // invalid enum: default
	vr_settings_free(&s);
}

static const char *env_names[8];
static const char *env_values[8];
static int env_count;

static const char *fake_getenv(const char *name) {
	for (int i = 0; i < env_count; i++) {
		if (strcmp(env_names[i], name) == 0) {
			return env_values[i];
		}
	}
	return NULL;
}

static void test_env_overrides(void) {
	vr_settings_t s;
	parse(&s, "version = 2\n[vr]\nresolution_scale = 1.1\nsnap_angle = 45\n");

	env_count = 0;
#define SET_ENV(n, v) do { env_names[env_count] = n; env_values[env_count] = v; env_count++; } while (0)
	SET_ENV("WIPEOUT_VR_RESOLUTION_SCALE", "1.25");
	SET_ENV("WIPEOUT_VR_SNAP_ANGLE", "1000");        // clamped
	SET_ENV("WIPEOUT_VR_PROBE_SECONDS", "5");
	SET_ENV("WIPEOUT_VR_FORCE_RENDER", "1");
	SET_ENV("WIPEOUT_VR_TURN_MODE", "Smooth");
	SET_ENV("WIPEOUT_VR_HUD_SCALE", "not a number"); // ignored
	SET_ENV("WIPEOUT_VR_WORLD_SCALE", "");           // empty: ignored
	SET_ENV("OTHER_VR_REFRESH_RATE", "72");          // another prefix: ignored
#undef SET_ENV

	vr_log_set_sink(quiet);
	int applied = vr_settings_apply_env(&s, fake_getenv);
	CHECK_INT(applied, 5);
	CHECK_NEAR(get(&s, "resolution_scale"), 1.25, 1e-6);
	CHECK_NEAR(get(&s, "snap_angle"), 90, 1e-9);
	CHECK_NEAR(get(&s, "probe_seconds"), 5, 1e-9);
	CHECK_NEAR(get(&s, "force_render"), 1, 1e-9);
	CHECK_NEAR(get(&s, "turn_mode"), 1, 1e-9);
	CHECK_NEAR(get(&s, "hud_scale"), 100, 1e-9);
	CHECK_NEAR(get(&s, "world_scale"), 88, 1e-9);
	CHECK_NEAR(get(&s, "refresh_rate"), 120, 1e-9);

	// the file's values are not touched, and so are not written back
	CHECK_NEAR(file_value(&s, "resolution_scale"), 1.1, 1e-6);
	CHECK_NEAR(file_value(&s, "snap_angle"), 45, 1e-9);
	CHECK_NEAR(file_value(&s, "probe_seconds"), 0, 1e-9);
	char *text = format(&s);
	CHECK(has(text, "resolution_scale = 1.1\n"));
	CHECK(has(text, "snap_angle = 45\n"));
	CHECK(has(text, "probe_seconds = 0.0\n"));
	free(text);

	CHECK_INT(vr_settings_apply_env(&s, no_env), 0);
	vr_settings_free(&s);
}

static void test_set(void) {
	vr_settings_t s;
	parse(&s, "");
	vr_settings_set(&s, "snap_angle", 500);
	CHECK_NEAR(get(&s, "snap_angle"), 90, 1e-9);
	CHECK_NEAR(file_value(&s, "snap_angle"), 90, 1e-9);
	vr_settings_set(&s, "vignette", 7);
	CHECK_NEAR(get(&s, "vignette"), 1, 1e-9);
	vr_settings_set(&s, "turn_mode", 9);
	CHECK_NEAR(get(&s, "turn_mode"), 2, 1e-9);
	vr_settings_set(&s, "nonsense", 1); // no effect, no crash
	vr_settings_free(&s);
}


// The cockpit keys: defaults, the enum, clamping, an env override and a live re-read
static const char *cockpit_env(const char *name) {
	if (strcmp(name, "WIPEOUT_VR_COCKPIT_BANK") == 0) {
		return "follow";
	}
	if (strcmp(name, "WIPEOUT_VR_COCKPIT_EYE_UP_CM") == 0) {
		return "-500";
	}
	return NULL;
}

static void test_cockpit_keys(void) {
	vr_settings_t s;
	parse(&s, "");
	vr_cockpit_settings_t c;
	vr_cockpit_settings_from(&c, &s);
	CHECK(c.enabled);
	CHECK_INT(c.bank, VR_COCKPIT_BANK_LEVEL);
	CHECK_NEAR(c.eye_forward_cm, 0, 1e-9);
	CHECK_NEAR(c.eye_up_cm, 0, 1e-9);
	CHECK(c.glass);

	char *text = format(&s);
	CHECK(has(text, "cockpit = true\n"));
	CHECK(has(text, "cockpit_bank = \"level\"\n"));
	CHECK(has(text, "cockpit_eye_forward_cm = 0.0\n"));
	CHECK(has(text, "cockpit_glass = true\n"));
	free(text);
	vr_settings_free(&s);

	// the file: enum names (any case), out-of-range cm, bad bools
	CHECK(parse(&s, "version = 2\n[vr]\ncockpit_bank = \"Cockpit\"\ncockpit_eye_forward_cm = 80\n"
		"cockpit_eye_up_cm = -12.5\ncockpit = off\ncockpit_glass = maybe\n") > 0);
	vr_cockpit_settings_from(&c, &s);
	CHECK(!c.enabled);
	CHECK_INT(c.bank, VR_COCKPIT_BANK_COCKPIT);
	CHECK_NEAR(c.eye_forward_cm, 30, 1e-9); // clamped
	CHECK_NEAR(c.eye_up_cm, -12.5, 1e-6);
	CHECK(c.glass); // an invalid bool takes the default
	vr_settings_free(&s);

	// an env override wins for the run and is clamped like the file
	parse(&s, "");
	CHECK_INT(vr_settings_apply_env(&s, cockpit_env), 2);
	vr_cockpit_settings_from(&c, &s);
	CHECK_INT(c.bank, VR_COCKPIT_BANK_FOLLOW);
	CHECK_NEAR(c.eye_up_cm, -30, 1e-9);
	CHECK_NEAR(file_value(&s, "cockpit_bank"), 0, 1e-9); // never written back

	// live: the page sets a key and the next read sees it, with no rebuild
	vr_settings_set(&s, "cockpit_eye_forward_cm", 7);
	vr_settings_set(&s, "cockpit_bank", 2);
	vr_cockpit_settings_from(&c, &s);
	CHECK_NEAR(c.eye_forward_cm, 7, 1e-9);
	CHECK_INT(c.bank, VR_COCKPIT_BANK_COCKPIT);
	vr_settings_set(&s, "cockpit_eye_forward_cm", 99);
	vr_cockpit_settings_from(&c, &s);
	CHECK_NEAR(c.eye_forward_cm, 30, 1e-9);
	vr_settings_free(&s);
}

static void test_load_and_save_files(void) {
	char dir[] = "/tmp/vr_settings_test_XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	char path[512], tmp[520];
	snprintf(path, sizeof(path), "%s/vr.toml", dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	vr_log_set_sink(quiet);
	vr_settings_t s;

	// no file: created with every key
	CHECK(vr_settings_load(&s, path));
	CHECK_NEAR(get(&s, "snap_angle"), 30, 1e-9);
	char *text = read_all(path);
	CHECK(has(text, "version = 2\n"));
	CHECK(has(text, "[vr]\n"));
	CHECK(has(text, "snap_angle = 30\n"));
	CHECK(has(text, "world_scale = 88.0\n"));
	free(text);
	CHECK(access(tmp, F_OK) != 0); // the temp file was renamed away
	struct stat st;
	CHECK(stat(path, &st) == 0 && st.st_size > 100);
	vr_settings_free(&s);

	// the user edits it: a bad value, an unknown key, one missing key
	write_all(path,
		"version = 2\n[vr]\nsnap_angle = 5\nmystery = \"keep me\"\n"
		"turn_mode = \"snap\"\n");
	CHECK(vr_settings_load(&s, path));
	CHECK_NEAR(get(&s, "snap_angle"), 15, 1e-9);
	text = read_all(path);
	CHECK(has(text, "snap_angle = 15\n"));
	CHECK(has(text, "mystery = \"keep me\"\n"));
	CHECK(has(text, "refresh_rate = 120\n"));
	free(text);
	vr_settings_free(&s);

	// a complete, valid file is not rewritten
	CHECK(vr_settings_load(&s, path));
	vr_settings_free(&s);
	struct stat before, after;
	CHECK(stat(path, &before) == 0);
	CHECK(vr_settings_load(&s, path));
	CHECK(stat(path, &after) == 0);
	CHECK(before.st_ino == after.st_ino); // same file: rename would have changed it
	vr_settings_free(&s);

	// save() changes only what changed in memory
	CHECK(vr_settings_load(&s, path));
	vr_settings_set(&s, "resolution_scale", 1.3);
	CHECK(vr_settings_save(&s));
	vr_settings_free(&s);
	text = read_all(path);
	CHECK(has(text, "resolution_scale = 1.3\n"));
	CHECK(has(text, "mystery = \"keep me\"\n"));
	free(text);
	CHECK(access(tmp, F_OK) != 0);

	// a failed write leaves the old file and no temp file behind
	char *old = read_all(path);
	CHECK(vr_settings_load(&s, path));
	snprintf(s.path, sizeof(s.path), "%s/no/such/dir/vr.toml", dir);
	CHECK(!vr_settings_save(&s));
	vr_settings_free(&s);
	text = read_all(path);
	CHECK_STR(text, old);
	free(text);
	free(old);

	// the Frame's file as 739a8d7 left it: load migrates it, writes it once, and keeps the rest
	write_all(path, "version = 1\n[vr]\nrefresh_rate = 90\nresolution_scale = 1.0\nsnap_angle = 45\nmystery = 1\n");
	CHECK(vr_settings_load(&s, path));
	CHECK_NEAR(get(&s, "refresh_rate"), 120, 1e-9);
	CHECK_NEAR(get(&s, "resolution_scale"), 1.0, 1e-9);
	CHECK_NEAR(get(&s, "eye_size"), 2160, 1e-9);
	vr_settings_free(&s);
	text = read_all(path);
	CHECK(has(text, "version = 2\n"));
	CHECK(has(text, "refresh_rate = 120\n"));
	CHECK(has(text, "eye_size = 2160\n"));
	CHECK(has(text, "snap_angle = 45\n"));
	CHECK(has(text, "mystery = 1\n"));
	free(text);
	CHECK(stat(path, &before) == 0);
	CHECK(vr_settings_load(&s, path)); // and the next start leaves it alone
	CHECK(stat(path, &after) == 0);
	CHECK(before.st_ino == after.st_ino);
	vr_settings_free(&s);

	// a missing directory: load still returns usable defaults, and says it could not write
	snprintf(path, sizeof(path), "%s/no/such/dir/vr.toml", dir);
	CHECK(!vr_settings_load(&s, path));
	CHECK_NEAR(get(&s, "refresh_rate"), 120, 1e-9);
	vr_settings_free(&s);

	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	CHECK(system(cmd) == 0);
}

int main(void) {
	vr_log_set_sink(quiet);
	RUN(test_registry);
	RUN(test_defaults_and_creation);
	RUN(test_missing_keys_get_defaults);
	RUN(test_bad_values_are_clamped_or_defaulted);
	RUN(test_unknown_content_is_kept);
	RUN(test_version);
	RUN(test_migration_to_v2);
	RUN(test_duplicates_and_odd_files);
	RUN(test_env_overrides);
	RUN(test_set);
	RUN(test_cockpit_keys);
	RUN(test_load_and_save_files);
	return SFVR_TEST_RESULT("vr_settings");
}
