/*
 * keys.h - Shrapnel's keyboard shortcuts, which the user can change
 * (Edit > Keyboard shortcuts).  Every action has up to two key chords; they
 * are kept in settings.txt as "key.<action> = Ctrl+Shift+Z, Ctrl+Y".
 *
 * Mouse modifiers (Shift and Alt with the tools, Space to pan) are not
 * actions and stay as they are.
 */
#pragma once
#include "imgui.h"
#include <stdio.h>
#include <string>

enum Act {
    A_TOOL_SELECT, A_TOOL_MOVE, A_TOOL_MAGIC, A_TOOL_PENCIL, A_TOOL_ERASER,
    A_TOOL_LINE, A_TOOL_RECT, A_TOOL_FILL, A_TOOL_PICK,
    A_UNDO, A_REDO, A_CUT, A_COPY, A_PASTE, A_DUPLICATE, A_DELETE,
    A_SELECT_ALL, A_DESELECT, A_INVERT, A_FLIP_H, A_FLIP_V, A_FIND,
    A_PREV_BRUSH, A_FACE, A_PASTE_MIRROR, A_PASTE_FLIP,
    A_OPEN, A_SAVE, A_SAVE_AS,
    A_GRID, A_BACKDROP, A_REACH, A_PANELS,
    A_ZOOM1, A_ZOOM2, A_ZOOM3, A_ZOOM4, A_ZOOM_ALL,
    A_PAN_LEFT, A_PAN_RIGHT, A_PAN_UP, A_PAN_DOWN,
    A_PLAY_HERE, A_PLAY_START, A_PLAY_TECHBOT, A_CHECK,
    N_ACTS
};

/* when an action's keys work: always, while editing (no paste following
 * the mouse), or only while a paste follows the mouse */
enum ActWhen { W_ALWAYS, W_EDIT, W_PASTE };

struct ActInfo {
    const char *id;                     /* in settings.txt */
    const char *group, *name;
    ActWhen when;
    ImGuiKeyChord def[2];               /* the usual keys */
};
extern const ActInfo act_info[N_ACTS];
extern ImGuiKeyChord keymap[N_ACTS][2];

bool act_pressed(Act a);                 /* one of its chords pressed this frame (exact modifiers; panning repeats and takes Shift too) */
std::string act_keys(Act a);             /* its first chord's name, for menus and tips ("" if none) */
std::string chord_name(ImGuiKeyChord c);
ImGuiKeyChord chord_parse(const std::string &s);   /* 0 if not a key */

void keys_save(FILE *f);
bool keys_load(const char *line);        /* a "key.<id> = ..." line; false if not one */

bool keys_capturing(void);               /* the shortcuts window waits for a key: no shortcuts meanwhile */
void keys_window(bool *open, float ui_scale, void (*save)(void));
