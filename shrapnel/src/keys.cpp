/*
 * keys.cpp - the keyboard shortcuts, and the window that changes them.
 */
#include "keys.h"
#include "theme.h"
#include <string.h>
#include <ctype.h>
#include <vector>

#define C(k) (ImGuiMod_Ctrl | ImGuiKey_##k)
#define CS(k) (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_##k)
#define S(k) (ImGuiMod_Shift | ImGuiKey_##k)
#define K(k) (ImGuiKey_##k)

const ActInfo act_info[N_ACTS] = {
    { "tool.select",  "Tools", "Select",                   W_EDIT,   { K(M) } },
    { "tool.move",    "Tools", "Move",                     W_EDIT,   { K(V) } },
    { "tool.magic",   "Tools", "Magic select",             W_EDIT,   { K(W) } },
    { "tool.pencil",  "Tools", "Pencil",                   W_EDIT,   { K(B) } },
    { "tool.eraser",  "Tools", "Eraser",                   W_EDIT,   { K(E) } },
    { "tool.line",    "Tools", "Line",                     W_EDIT,   { K(L) } },
    { "tool.rect",    "Tools", "Rectangle",                W_EDIT,   { K(R) } },
    { "tool.fill",    "Tools", "Fill",                     W_EDIT,   { K(G) } },
    { "tool.pick",    "Tools", "Pick",                     W_EDIT,   { K(I) } },
    { "undo",         "Edit",  "Undo",                     W_ALWAYS, { C(Z) } },
    { "redo",         "Edit",  "Redo",                     W_ALWAYS, { C(Y), CS(Z) } },
    { "cut",          "Edit",  "Cut",                      W_ALWAYS, { C(X) } },
    { "copy",         "Edit",  "Copy",                     W_ALWAYS, { C(C) } },
    { "paste",        "Edit",  "Paste",                    W_ALWAYS, { C(V) } },
    { "duplicate",    "Edit",  "Duplicate",                W_ALWAYS, { C(D) } },
    { "delete",       "Edit",  "Delete the selection",     W_EDIT,   { K(Delete) } },
    { "select_all",   "Edit",  "Select all",               W_ALWAYS, { C(A) } },
    { "deselect",     "Edit",  "Select none / cancel a paste", W_ALWAYS, { K(Escape) } },
    { "invert",       "Edit",  "Invert the selection",     W_ALWAYS, { CS(I) } },
    { "flip_h",       "Edit",  "Flip horizontal",          W_EDIT,   { S(H) } },
    { "flip_v",       "Edit",  "Flip vertical",            W_EDIT,   { S(J) } },
    { "find",         "Edit",  "Find and replace",         W_ALWAYS, { C(F) } },
    { "prev_brush",   "Edit",  "Previous brush",           W_EDIT,   { K(X) } },
    { "face",         "Edit",  "Actor: face the other way", W_EDIT,  { K(F) } },
    { "paste_mirror", "Edit",  "Paste: mirror it",         W_PASTE,  { K(X) } },
    { "paste_flip",   "Edit",  "Paste: flip it",           W_PASTE,  { K(Y) } },
    { "open",         "File",  "Open a project",           W_ALWAYS, { C(O) } },
    { "save",         "File",  "Save",                     W_ALWAYS, { C(S) } },
    { "save_as",      "File",  "Save as",                  W_ALWAYS, { CS(S) } },
    { "grid",         "View",  "Grid",                     W_ALWAYS, { C(G) } },
    { "backdrop",     "View",  "Backdrop",                 W_ALWAYS, { C(B) } },
    { "reach",        "View",  "Reach",                    W_ALWAYS, { C(R) } },
    { "panels",       "View",  "Side panels",              W_EDIT,   { K(Tab) } },
    { "zoom1",        "View",  "Zoom 100%",                W_EDIT,   { K(1) } },
    { "zoom2",        "View",  "Zoom 200%",                W_EDIT,   { K(2) } },
    { "zoom3",        "View",  "Zoom 300%",                W_EDIT,   { K(3) } },
    { "zoom4",        "View",  "Zoom 400%",                W_EDIT,   { K(4) } },
    { "zoom_all",     "View",  "Zoom to the whole level",  W_EDIT,   { K(0) } },
    { "pan_left",     "View",  "Pan left (Move tool: nudge)",  W_ALWAYS, { K(LeftArrow) } },
    { "pan_right",    "View",  "Pan right (Move tool: nudge)", W_ALWAYS, { K(RightArrow) } },
    { "pan_up",       "View",  "Pan up (Move tool: nudge)",    W_ALWAYS, { K(UpArrow) } },
    { "pan_down",     "View",  "Pan down (Move tool: nudge)",  W_ALWAYS, { K(DownArrow) } },
    { "play_here",    "Play",  "Play here",                W_ALWAYS, { K(F4) } },
    { "play_start",   "Play",  "Play from the start",      W_ALWAYS, { S(F4) } },
    { "play_techbot", "Play",  "Play in Techbot",          W_ALWAYS, { C(F5) } },
    { "check",        "Play",  "Check",                    W_ALWAYS, { K(F7) } },
};

ImGuiKeyChord keymap[N_ACTS][2];
static bool keymap_ready;
static void keys_defaults(void)
{
    for (int a = 0; a < N_ACTS; a++) { keymap[a][0] = act_info[a].def[0]; keymap[a][1] = act_info[a].def[1]; }
    keymap_ready = true;
}
static void ready(void) { if (!keymap_ready) keys_defaults(); }

static bool is_pan(int a) { return a >= A_PAN_LEFT && a <= A_PAN_DOWN; }

bool act_pressed(Act a)
{
    ready();
    ImGuiIO &io = ImGui::GetIO();
    for (int k = 0; k < 2; k++) {
        ImGuiKeyChord c = keymap[a][k];
        if (!c) continue;
        ImGuiKey key = (ImGuiKey)(c & ~ImGuiMod_Mask_);
        int mods = c & ImGuiMod_Mask_, now = io.KeyMods;
        if (is_pan(a)) now &= ~ImGuiMod_Shift;  /* (Shift pans further) */
        if (now == mods && ImGui::IsKeyPressed(key, is_pan(a) || a == A_UNDO || a == A_REDO)) return true;
    }
    return false;
}

std::string chord_name(ImGuiKeyChord c)
{
    if (!c) return "";
    std::string s;
    if (c & ImGuiMod_Ctrl) s += "Ctrl+";
    if (c & ImGuiMod_Shift) s += "Shift+";
    if (c & ImGuiMod_Alt) s += "Alt+";
    if (c & ImGuiMod_Super) s += "Super+";
    ImGuiKey key = (ImGuiKey)(c & ~ImGuiMod_Mask_);
    const char *n = ImGui::GetKeyName(key);
    if (!strcmp(n, "Escape")) n = "Esc";
    else if (!strcmp(n, "Delete")) n = "Del";
    return s + n;
}

std::string act_keys(Act a) { ready(); return chord_name(keymap[a][0] ? keymap[a][0] : keymap[a][1]); }

static bool same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return !*a && !*b;
}

ImGuiKeyChord chord_parse(const std::string &in)
{
    std::string s;
    for (char ch : in) if (ch != ' ') s += ch;
    int mods = 0;
    for (;;) {
        size_t p = s.find('+');
        if (p == std::string::npos || p + 1 >= s.size()) break;     /* ("Ctrl++": the + key isn't one) */
        std::string m = s.substr(0, p);
        if (same(m.c_str(), "Ctrl")) mods |= ImGuiMod_Ctrl;
        else if (same(m.c_str(), "Shift")) mods |= ImGuiMod_Shift;
        else if (same(m.c_str(), "Alt")) mods |= ImGuiMod_Alt;
        else if (same(m.c_str(), "Super")) mods |= ImGuiMod_Super;
        else return 0;
        s = s.substr(p + 1);
    }
    if (same(s.c_str(), "Esc")) s = "Escape";
    if (same(s.c_str(), "Del")) s = "Delete";
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++)
        if (same(ImGui::GetKeyName((ImGuiKey)k), s.c_str())) return mods | k;
    return 0;
}

void keys_save(FILE *f)
{
    ready();
    for (int a = 0; a < N_ACTS; a++) {          /* (only the ones changed from the usual) */
        if (keymap[a][0] == act_info[a].def[0] && keymap[a][1] == act_info[a].def[1]) continue;
        fprintf(f, "key.%s = %s, %s\n", act_info[a].id, keymap[a][0] ? chord_name(keymap[a][0]).c_str() : "none",
                keymap[a][1] ? chord_name(keymap[a][1]).c_str() : "none");
    }
}

bool keys_load(const char *line)
{
    ready();
    if (strncmp(line, "key.", 4)) return false;
    const char *eq = strchr(line, '=');
    if (!eq) return true;
    std::string id(line + 4, eq - line - 4);
    while (!id.empty() && id.back() == ' ') id.pop_back();
    for (int a = 0; a < N_ACTS; a++) {
        if (id != act_info[a].id) continue;
        std::string v = eq + 1;
        size_t comma = v.find(',');
        std::string k0 = comma == std::string::npos ? v : v.substr(0, comma), k1 = comma == std::string::npos ? "" : v.substr(comma + 1);
        auto trim = [](std::string t) {
            while (!t.empty() && (t.back() == ' ' || t.back() == '\n' || t.back() == '\r')) t.pop_back();
            while (!t.empty() && t[0] == ' ') t.erase(0, 1);
            return t;
        };
        keymap[a][0] = chord_parse(trim(k0));
        keymap[a][1] = chord_parse(trim(k1));
    }
    return true;
}

/* ---- the window */
static int cap_act = -1, cap_slot = 0;        /* waiting for a key for this action's chord */
bool keys_capturing(void) { return cap_act >= 0; }

static bool overlap(ActWhen a, ActWhen b) { return a == W_ALWAYS || b == W_ALWAYS || a == b; }

/* the action (other than `skip`) already using chord c where `when` applies, else -1 */
static int used_by(ImGuiKeyChord c, ActWhen when, int skip, int *slot)
{
    for (int a = 0; a < N_ACTS; a++)
        for (int k = 0; k < 2; k++)
            if (a != skip && keymap[a][k] == c && overlap(act_info[a].when, when)) { *slot = k; return a; }
    return -1;
}

static std::string taken_note;

static void assign(int a, int slot, ImGuiKeyChord c)
{
    int other_slot, other;
    taken_note.clear();
    while (c && (other = used_by(c, act_info[a].when, a, &other_slot)) >= 0) {   /* a key does one thing: the other loses it */
        keymap[other][other_slot] = 0;
        taken_note = chord_name(c) + " was " + act_info[other].name + "'s; it is now this one's.";
    }
    if (c && keymap[a][slot ^ 1] == c) keymap[a][slot ^ 1] = 0;
    keymap[a][slot] = c;
}

static ImGuiKeyChord key_now(void)              /* a key pressed this frame, with the modifiers held */
{
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        ImGuiKey key = (ImGuiKey)k;
        if (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown) continue;
        if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) continue;
        if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl || key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
            key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt || key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper ||
            key == ImGuiKey_ReservedForModCtrl || key == ImGuiKey_ReservedForModShift || key == ImGuiKey_ReservedForModAlt ||
            key == ImGuiKey_ReservedForModSuper)
            continue;
        if (ImGui::IsKeyPressed(key, false)) return ImGui::GetIO().KeyMods | k;
    }
    return 0;
}

void keys_window(bool *open, float ui_scale, void (*save)(void))
{
    ready();
    ImGuiIO &io = ImGui::GetIO();               /* (no keyboard navigation while a key is awaited: Space, Enter, Tab are keys too) */
    if (!*open) cap_act = -1;
    if (cap_act >= 0) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    else io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(560 * ui_scale, 620 * ui_scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Keyboard shortcuts", open, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if (!*open) cap_act = -1;

    static char filter[64];
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150 * ui_scale);
    ImGui::InputTextWithHint("##kfind", ICON_SEARCH "  Find an action or a key", filter, sizeof filter);
    ImGui::SameLine();
    if (ImGui::Button("Reset all", ImVec2(-1, 0))) { keys_defaults(); taken_note.clear(); cap_act = -1; save(); }
    ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
    ImGui::TextWrapped("Click a key to change it, then press the new one (with Ctrl, Shift or Alt if you like). "
                       "Each action can have two. A key already in use moves to the new action.");
    ImGui::PopStyleColor();
    if (!taken_note.empty()) ImGui::TextColored(T.warn, ICON_INFO " %s", taken_note.c_str());

    if (cap_act >= 0) {                         /* waiting for the key */
        ImGuiKeyChord c = key_now();
        if (c) { assign(cap_act, cap_slot, c); cap_act = -1; save(); }
    }

    ImGui::BeginChild("##keys", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const char *group = "";
    float kw = 130 * ui_scale;
    for (int a = 0; a < N_ACTS; a++) {
        const ActInfo &in = act_info[a];
        if (filter[0]) {
            std::string hay = std::string(in.group) + " " + in.name + " " + chord_name(keymap[a][0]) + " " + chord_name(keymap[a][1]);
            std::string h, f;
            for (char ch : hay) h += (char)tolower((unsigned char)ch);
            for (const char *p = filter; *p; p++) f += (char)tolower((unsigned char)*p);
            if (h.find(f) == std::string::npos) continue;
        }
        if (strcmp(group, in.group)) { group = in.group; ImGui::Spacing(); section_label(group); }
        ImGui::PushID(a);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(in.name);
        if (in.when == W_PASTE) { ImGui::SameLine(); ImGui::TextDisabled("(while pasting)"); }
        float x = ImGui::GetWindowContentRegionMax().x - kw * 2 - ImGui::GetFrameHeight() - 12 * ui_scale;
        for (int k = 0; k < 2; k++) {
            ImGui::SameLine(x + k * (kw + 4 * ui_scale));
            ImGui::PushID(k);
            bool waiting = cap_act == a && cap_slot == k;
            std::string lbl = waiting ? "Press a key..." : keymap[a][k] ? chord_name(keymap[a][k]) : "-";
            if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button((lbl + "##k").c_str(), ImVec2(kw, 0))) { if (waiting) cap_act = -1; else { cap_act = a; cap_slot = k; } }
            if (waiting) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(waiting ? "Press the new key (click again to leave it as it was)" :
                                  "Click, then press a key. Right-click: no key here.");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { keymap[a][k] = 0; if (waiting) cap_act = -1; save(); }
            ImGui::PopID();
        }
        ImGui::SameLine();
        bool usual = keymap[a][0] == in.def[0] && keymap[a][1] == in.def[1];
        ImGui::BeginDisabled(usual);
        bool reset = ImGui::Button(ICON_HISTORY "##usual", ImVec2(ImGui::GetFrameHeight(), 0));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The usual: %s%s%s", in.def[0] ? chord_name(in.def[0]).c_str() : "none", in.def[1] ? ", " : "",
                              in.def[1] ? chord_name(in.def[1]).c_str() : "");
        if (reset) {
            assign(a, 0, in.def[0]);
            assign(a, 1, in.def[1]);
            save();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}
