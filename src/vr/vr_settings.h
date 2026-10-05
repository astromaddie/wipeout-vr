#ifndef VR_SETTINGS_H
#define VR_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "sfvr/sfvr_settings.h"

// $XDG_DATA_HOME/WipEoutVR/vr.toml: a top-level `version` and a [vr] table.
// The key set is the sfvr registry plus the port keys in vr_settings.c. Every
// key has a default, so a missing key is filled in, a bad value is replaced by
// the default, an out-of-range value is clamped, and keys we do not know (and
// every other line of the file) are kept as they are. The file is rewritten,
// atomically, only when something was missing or repaired. Environment
// variables WIPEOUT_VR_<KEY> override the file for the run and are never
// written back.

#define VR_SETTINGS_MAX_KEYS 64

// The file's schema `version`. sfvr's registry is version 1; the port bumps it
// when it overrides a registry default (see vr_settings.c). Version 2: the
// refresh_rate default moves from 90 to 120 (and eye_size is new). A file older
// than this has a refresh_rate that still holds the old default moved to the
// new one; a value the user changed is kept.
#define VR_SETTINGS_VERSION 2

typedef struct {
	double values[VR_SETTINGS_MAX_KEYS]; // effective: file value, then env override
	double file_values[VR_SETTINGS_MAX_KEYS]; // what the file holds (and save() writes)
	int file_version; // schema version found in the file; 0 when absent
	char *raw; // the file text as read, NUL terminated; NULL when there was none
	size_t raw_len;
	char path[1024];
} vr_settings_t;

size_t vr_settings_count(void);
const sfvr_setting_key *vr_settings_key(size_t index);
int vr_settings_index(const char *name); // -1 when unknown

// Defaults for every key; no file, no env.
void vr_settings_defaults(vr_settings_t *s);

// Parse TOML text into file_values (starting from the defaults) and remember it
// as `raw`. Returns the number of repairs: keys that were missing, invalid,
// clamped or duplicated, plus 1 when the schema version is missing or old. A
// non-zero result means the file should be rewritten.
int vr_settings_parse(vr_settings_t *s, const char *text, size_t len);

// The file text for the current file_values: `raw` with known keys repaired in
// place and missing ones added to [vr]; everything else untouched. malloc'd.
char *vr_settings_format(const vr_settings_t *s, size_t *len);

// Env overrides into `values`. getenv_fn lets tests inject an environment
// (NULL means getenv). Returns how many keys were overridden.
int vr_settings_apply_env(vr_settings_t *s, const char *(*getenv_fn)(const char *));

// Read, repair and rewrite the file at `path` (creating it with defaults when
// missing), then apply the environment. Always leaves `s` usable; false when
// the file existed but could not be read, or a needed rewrite failed.
bool vr_settings_load(vr_settings_t *s, const char *path);

// Atomic write (temp file, fsync, rename) of vr_settings_format().
bool vr_settings_save(const vr_settings_t *s);

double vr_settings_get(const vr_settings_t *s, const char *name); // effective value; 0 for an unknown name
void vr_settings_set(vr_settings_t *s, const char *name, double value); // clamps; sets effective and file value

void vr_settings_free(vr_settings_t *s);

#endif
