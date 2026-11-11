/*
 * theme.h - Shrapnel's look (M10-A): two themes, dark and light, with an
 * accent colour of the user's choice; the interface fonts (Inter, and
 * Material Symbols for icons); and the few widgets the layout is built from.
 */
#pragma once
#include "imgui.h"
#include "../third_party/fonts/icons.h"
#include <stdint.h>

struct ThemeColors {
    ImVec4 text, muted, faint;                  /* words: normal, secondary, hints */
    ImVec4 good, warn, bad, info;               /* results: fine, warning, broken, a note */
    ImVec4 accent, accent_text;                 /* the accent, and words in it */
    ImVec4 panel, panel2, bar, border;          /* surfaces: panels, the inset ones, menu / status bars, lines */
    ImU32 overlay_shadow;
};
extern ThemeColors T;
extern bool theme_dark;
extern uint32_t theme_accent;                   /* 0xRRGGBB */
extern ImFont *font_body, *font_bold, *font_icons;   /* text (with icons), headings, icon buttons */
extern ImFont *font_tb, *font_tb_bold, *font_icons_lg;   /* the toolbar's: a size up */
#define TB_BUTTON 38.0f                             /* a toolbar button, at 100% */

struct AccentChoice { const char *name; uint32_t rgb; };
extern const AccentChoice accent_choices[];
extern const int n_accent_choices;

void theme_fonts(float ui_scale, float fb_scale);      /* before the first frame */
void theme_apply(float ui_scale);                     /* theme_dark / theme_accent changed */

/* widgets */
bool icon_button(const char *icon, const char *tip, bool on = false, bool enabled = true, bool large = false);
bool segmented(const char *id, const char *const *labels, int n, int *sel);
bool chip(const char *label, bool on);
void section_label(const char *text);
bool split_button(const char *label, const char *icon, const char *popup_id, const char *tip, ImFont *font = NULL);
