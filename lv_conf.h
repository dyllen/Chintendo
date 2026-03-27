#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * Project-wide LVGL overrides for SquareLine-generated sources.
 * This header is picked up by LVGL in every translation unit (e.g. ui.c,
 * ui_helpers.c, and the sketch), so settings here are consistent everywhere.
 */

#ifndef LV_COLOR_16_SWAP
#define LV_COLOR_16_SWAP 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_8 1
#endif

#define LV_FONT_MONTSERRAT_16 0

/*
 * Never macro-remap lv_event_send in lv_conf.h.
 * A function-like macro named lv_event_send rewrites LVGL's own declaration in
 * lv_event.h and causes parse errors like:
 *   expected declaration specifiers or '...' before '(' token
 */
#ifdef lv_event_send
#undef lv_event_send
#endif

#endif /* LV_CONF_H */
