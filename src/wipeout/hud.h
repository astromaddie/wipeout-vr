#ifndef HUD_H
#define HUD_H

#include "ship.h"

void hud_load(void);
void hud_draw(ship_t *ship);

#if WIPEOUT_VR
// The cockpit draws its own instruments (src/vr/vr_cockpit.c) with the same
// calls hud_draw makes: these expose the pieces that are private to hud.c.
void hud_draw_speedo_at(vec2i_t facia_pos, int speed, int thrust); // bars 5 ui pixels below the facia, at the current ui scale
uint16_t hud_weapon_icon(int weapon_type); // the wicons texture of a weapon_type (not NONE)
void hud_draw_overlay(ship_t *ship); // what stays on the HUD quad in the cockpit: WRONG WAY and the target reticle
#endif

#endif
