/*
 * theme.cpp - Shrapnel's two themes, its fonts and its own widgets (M10-A).
 *
 * The look follows the approved mock-up: quiet flat surfaces, thin borders,
 * 6 px rounding, one accent colour (the user's choice; teal by default) used
 * only for what is on or chosen.  The map and the palette's tiles are drawn
 * as the game draws them in either theme.
 */
#include "theme.h"
#include "imgui_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../third_party/fonts/inter_400.h"
#include "../third_party/fonts/inter_600.h"
#include "../third_party/fonts/symbols.h"
#include "../third_party/fonts/symbols_filled.h"

ThemeColors T;
bool theme_dark = true;
uint32_t theme_accent = 0x2BC4B4;
ImFont *font_body, *font_bold, *font_icons;
ImFont *font_tb, *font_tb_bold, *font_icons_lg;
static float g_scale = 1;

const AccentChoice accent_choices[] = {
    { "Teal", 0x2BC4B4 }, { "Blue", 0x5B9BF0 }, { "Violet", 0x9B7BF0 }, { "Rose", 0xEE6F92 },
    { "Amber", 0xE8B04A }, { "Green", 0x5CC774 }, { "Slate", 0x9AA6B8 },
};
const int n_accent_choices = sizeof accent_choices / sizeof accent_choices[0];

static ImVec4 rgb(uint32_t c, float a = 1) { return ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a); }
static ImVec4 alpha(ImVec4 c, float a) { c.w = a; return c; }
static ImVec4 mix(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }

/* ---------------------------------------------------------------- fonts */
void theme_fonts(float ui_scale, float fb_scale)
{
    ImGuiIO &io = ImGui::GetIO();
    float px = ui_scale * fb_scale;
    static const ImWchar icon_range[] = { ICON_MIN, ICON_MAX, 0 };
    /* what the Inter latin subset has: Latin-1, and the punctuation (bullets, dashes, quotes, ellipsis) and arrows */
    static const ImWchar text_range[] = { 0x0020, 0x00FF, 0x2000, 0x206F, 0x20AC, 0x20AC, 0x2122, 0x2122, 0x2190, 0x2193, 0x2212, 0x2215, 0 };
    auto add = [&](const unsigned char *data, unsigned int size, float sz, bool merge, ImVec2 off, const ImWchar *ranges, float min_adv) {
        ImFontConfig fc;
        fc.FontDataOwnedByAtlas = false;
        fc.MergeMode = merge;
        fc.GlyphOffset = off;
        fc.GlyphMinAdvanceX = min_adv;
        fc.PixelSnapH = true;
        fc.OversampleH = 2;
        return io.Fonts->AddFontFromMemoryTTF((void *)data, (int)size, sz, &fc, ranges);
    };
    /* text, with the icons beside it (a little larger, centred on the line) */
    font_body = add(inter_400_ttf, inter_400_ttf_size, 15.0f * px, false, ImVec2(0, 0), text_range, 0);
    add(symbols_ttf, symbols_ttf_size, 18.0f * px, true, ImVec2(0, 3.5f * px), icon_range, 18.0f * px);
    add(symbols_filled_ttf, symbols_filled_ttf_size, 18.0f * px, true, ImVec2(0, 3.5f * px), icon_range, 18.0f * px);
    font_bold = add(inter_600_ttf, inter_600_ttf_size, 15.0f * px, false, ImVec2(0, 0), text_range, 0);
    add(symbols_ttf, symbols_ttf_size, 18.0f * px, true, ImVec2(0, 3.5f * px), icon_range, 18.0f * px);
    add(symbols_filled_ttf, symbols_filled_ttf_size, 18.0f * px, true, ImVec2(0, 3.5f * px), icon_range, 18.0f * px);
    /* icon buttons */
    font_icons = add(symbols_ttf, symbols_ttf_size, 20.0f * px, false, ImVec2(0, 0), icon_range, 20.0f * px);
    add(symbols_filled_ttf, symbols_filled_ttf_size, 20.0f * px, true, ImVec2(0, 0), icon_range, 20.0f * px);
    /* the toolbar: larger icons, and its words a size up */
    font_icons_lg = add(symbols_ttf, symbols_ttf_size, 24.0f * px, false, ImVec2(0, 0), icon_range, 24.0f * px);
    add(symbols_filled_ttf, symbols_filled_ttf_size, 24.0f * px, true, ImVec2(0, 0), icon_range, 24.0f * px);
    font_tb = add(inter_400_ttf, inter_400_ttf_size, 16.0f * px, false, ImVec2(0, 0), text_range, 0);
    add(symbols_ttf, symbols_ttf_size, 20.0f * px, true, ImVec2(0, 4.0f * px), icon_range, 20.0f * px);
    add(symbols_filled_ttf, symbols_filled_ttf_size, 20.0f * px, true, ImVec2(0, 4.0f * px), icon_range, 20.0f * px);
    font_tb_bold = add(inter_600_ttf, inter_600_ttf_size, 16.0f * px, false, ImVec2(0, 0), text_range, 0);
    add(symbols_ttf, symbols_ttf_size, 20.0f * px, true, ImVec2(0, 4.0f * px), icon_range, 20.0f * px);
    add(symbols_filled_ttf, symbols_filled_ttf_size, 20.0f * px, true, ImVec2(0, 4.0f * px), icon_range, 20.0f * px);
    io.FontDefault = font_body;
    io.FontGlobalScale = 1.0f / fb_scale;
}

/* ---------------------------------------------------------------- the themes */
void theme_apply(float ui_scale)
{
    g_scale = ui_scale;
    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(10, 10);
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.CellPadding = ImVec2(6, 3);
    st.IndentSpacing = 14;
    st.ScrollbarSize = 12;
    st.GrabMinSize = 10;
    st.WindowRounding = 0;
    st.ChildRounding = 8;
    st.FrameRounding = 6;
    st.PopupRounding = 8;
    st.ScrollbarRounding = 6;
    st.GrabRounding = 6;
    st.TabRounding = 6;
    st.WindowBorderSize = 1;
    st.ChildBorderSize = 1;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 1;
    st.TabBorderSize = 0;
    st.DisabledAlpha = 0.45f;               /* clearly unavailable, still legible */
    st.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.SeparatorTextBorderSize = 1;
    st.SeparatorTextPadding = ImVec2(0, 4);
    st.ScaleAllSizes(ui_scale);

    ImVec4 base = rgb(theme_accent);
    ImVec4 *c = st.Colors;
    if (theme_dark) {
        T.text = rgb(0xE6E9EE); T.muted = rgb(0x8B93A1); T.faint = rgb(0x6C7482);
        T.good = rgb(0x4CC38A); T.warn = rgb(0xE8A33A); T.bad = rgb(0xEF6A61); T.info = rgb(0x6CA6F0);
        T.accent = base; T.accent_text = mix(base, ImVec4(1, 1, 1, 1), 0.35f);
        T.panel = rgb(0x181B20); T.panel2 = rgb(0x14161A); T.bar = rgb(0x101216); T.border = rgb(0x2A2F38);
        T.overlay_shadow = IM_COL32(0, 0, 0, 120);
        ImVec4 inset = rgb(0x101216), raised = rgb(0x20252D), hover = rgb(0x2A3039), pop = rgb(0x1E2229);
        c[ImGuiCol_Text] = T.text;                  c[ImGuiCol_TextDisabled] = T.muted;
        c[ImGuiCol_WindowBg] = T.panel;             c[ImGuiCol_ChildBg] = T.panel2;
        c[ImGuiCol_PopupBg] = pop;                  c[ImGuiCol_Border] = T.border;
        c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_FrameBg] = inset;                c[ImGuiCol_FrameBgHovered] = rgb(0x171A1F);
        c[ImGuiCol_FrameBgActive] = rgb(0x1B1F25);
        c[ImGuiCol_TitleBg] = T.bar;                c[ImGuiCol_TitleBgActive] = T.bar;
        c[ImGuiCol_TitleBgCollapsed] = T.bar;       c[ImGuiCol_MenuBarBg] = T.bar;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = rgb(0x323843);  c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x3D4451);
        c[ImGuiCol_ScrollbarGrabActive] = rgb(0x4A5260);
        c[ImGuiCol_Button] = raised;                c[ImGuiCol_ButtonHovered] = hover;
        c[ImGuiCol_ButtonActive] = alpha(base, 0.30f);
        c[ImGuiCol_Header] = alpha(base, 0.16f);    c[ImGuiCol_HeaderHovered] = rgb(0x232830);
        c[ImGuiCol_HeaderActive] = alpha(base, 0.24f);
        c[ImGuiCol_Separator] = T.border;           c[ImGuiCol_SeparatorHovered] = alpha(base, 0.6f);
        c[ImGuiCol_SeparatorActive] = base;
        c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ResizeGripHovered] = alpha(base, 0.6f);
        c[ImGuiCol_ResizeGripActive] = base;
        c[ImGuiCol_TableHeaderBg] = raised;         c[ImGuiCol_TableBorderStrong] = T.border;
        c[ImGuiCol_TableBorderLight] = rgb(0x22262E);
        c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
    } else {
        ImVec4 dark = mix(base, ImVec4(0, 0, 0, 1), 0.45f);     /* the accent, deep enough for white backgrounds */
        T.text = rgb(0x1C2027); T.muted = rgb(0x5E6673); T.faint = rgb(0x7A828E);
        T.good = rgb(0x1E7F49); T.warn = rgb(0x9A5C06); T.bad = rgb(0xC0352C); T.info = rgb(0x2A62B8);
        T.accent = dark; T.accent_text = mix(base, ImVec4(0, 0, 0, 1), 0.55f);
        T.panel = rgb(0xFFFFFF); T.panel2 = rgb(0xF7F8FA); T.bar = rgb(0xECEEF1); T.border = rgb(0xDDE1E6);
        T.overlay_shadow = IM_COL32(20, 30, 40, 50);
        ImVec4 inset = rgb(0xF7F8FA), raised = rgb(0xFFFFFF), hover = rgb(0xEEF1F4);
        c[ImGuiCol_Text] = T.text;                  c[ImGuiCol_TextDisabled] = T.muted;
        c[ImGuiCol_WindowBg] = T.panel;             c[ImGuiCol_ChildBg] = T.panel2;
        c[ImGuiCol_PopupBg] = rgb(0xFFFFFF);        c[ImGuiCol_Border] = rgb(0xD3D8DE);
        c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_FrameBg] = inset;                c[ImGuiCol_FrameBgHovered] = rgb(0xF0F2F5);
        c[ImGuiCol_FrameBgActive] = rgb(0xEBEEF2);
        c[ImGuiCol_TitleBg] = T.bar;                c[ImGuiCol_TitleBgActive] = T.bar;
        c[ImGuiCol_TitleBgCollapsed] = T.bar;       c[ImGuiCol_MenuBarBg] = T.bar;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = rgb(0xC9CED6);  c[ImGuiCol_ScrollbarGrabHovered] = rgb(0xB5BCC6);
        c[ImGuiCol_ScrollbarGrabActive] = rgb(0xA2AAB6);
        c[ImGuiCol_Button] = raised;                c[ImGuiCol_ButtonHovered] = hover;
        c[ImGuiCol_ButtonActive] = alpha(dark, 0.18f);
        c[ImGuiCol_Header] = alpha(dark, 0.12f);    c[ImGuiCol_HeaderHovered] = rgb(0xEEF1F4);
        c[ImGuiCol_HeaderActive] = alpha(dark, 0.18f);
        c[ImGuiCol_Separator] = T.border;           c[ImGuiCol_SeparatorHovered] = alpha(dark, 0.6f);
        c[ImGuiCol_SeparatorActive] = dark;
        c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ResizeGripHovered] = alpha(dark, 0.6f);
        c[ImGuiCol_ResizeGripActive] = dark;
        c[ImGuiCol_TableHeaderBg] = rgb(0xF0F2F5);  c[ImGuiCol_TableBorderStrong] = T.border;
        c[ImGuiCol_TableBorderLight] = rgb(0xE6E9ED);
        c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0.025f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.1f, 0.12f, 0.15f, 0.25f);
    }
    c[ImGuiCol_CheckMark] = T.accent;
    c[ImGuiCol_SliderGrab] = T.accent;              c[ImGuiCol_SliderGrabActive] = T.accent_text;
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_TabSelected] = alpha(theme_dark ? base : T.accent, 0.16f);
    c[ImGuiCol_TabSelectedOverline] = T.accent;
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
    c[ImGuiCol_TextSelectedBg] = alpha(T.accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = T.accent;
    c[ImGuiCol_NavCursor] = T.accent;
    c[ImGuiCol_NavWindowingHighlight] = alpha(T.accent, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.3f);
    c[ImGuiCol_PlotLines] = T.muted;                c[ImGuiCol_PlotLinesHovered] = T.accent;
    c[ImGuiCol_PlotHistogram] = T.accent;           c[ImGuiCol_PlotHistogramHovered] = T.accent_text;
}

/* ---------------------------------------------------------------- widgets */
static void tip(const char *t)
{
    if (t && *t && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", t);
}

/* a square toolbar button showing one icon; `on` = tinted with the accent */
bool icon_button(const char *icon, const char *t, bool on, bool enabled, bool large)
{
    float s = large ? TB_BUTTON * g_scale : ImGui::GetFrameHeight() + 4 * ImGui::GetStyle().FramePadding.y / 5;
    ImGui::PushFont(large ? font_icons_lg : font_icons);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, on ? alpha(T.accent, theme_dark ? 0.18f : 0.12f) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, on ? alpha(T.accent, 0.55f) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? T.accent_text : T.muted);
    if (!enabled) ImGui::BeginDisabled();
    ImGui::PushID(icon);
    bool r = ImGui::Button(icon, ImVec2(s, s));
    ImGui::PopID();
    if (!enabled) ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    tip(t);
    return r;
}

/* two or more choices side by side in one frame (Tiles | Actors) */
bool segmented(const char *id, const char *const *labels, int n, int *sel)
{
    ImGuiStyle &st = ImGui::GetStyle();
    float w = ImGui::GetContentRegionAvail().x, pad = 3 * st.FramePadding.y / 5;
    float h = ImGui::GetFrameHeight() + 2 * pad;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), st.FrameRounding + pad);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_Border), st.FrameRounding + pad);
    bool changed = false;
    ImGui::PushID(id);
    float bw = (w - 2 * pad) / n;
    for (int i = 0; i < n; i++) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad + i * bw, p.y + pad));
        bool on = *sel == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? (theme_dark ? ImVec4(0.149f, 0.173f, 0.208f, 1) : ImVec4(1, 1, 1, 1)) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ImGui::GetStyleColorVec4(ImGuiCol_Button) : ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        ImGui::PushStyleColor(ImGuiCol_Border, on && !theme_dark ? ImGui::GetStyleColorVec4(ImGuiCol_Border) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? T.text : T.muted);
        if (on) ImGui::PushFont(font_bold);
        ImGui::PushID(i);
        if (ImGui::Button(labels[i], ImVec2(bw, ImGui::GetFrameHeight())) && !on) { *sel = i; changed = true; }
        ImGui::PopID();
        if (on) ImGui::PopFont();
        ImGui::PopStyleColor(4);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + st.ItemSpacing.y));
    ImGui::Dummy(ImVec2(w, 0));
    return changed;
}

/* a small rounded choice (a filter, an option) */
bool chip(const char *label, bool on)
{
    ImGuiStyle &st = ImGui::GetStyle();
    float h = ImGui::GetFontSize() + 2 * st.FramePadding.y * 0.6f;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h / 2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x * 1.2f, st.FramePadding.y * 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Button, on ? alpha(T.accent, theme_dark ? 0.18f : 0.12f) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, on ? alpha(T.accent, 0.55f) : ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? T.accent_text : T.muted);
    bool r = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return r;
}

/* a small capitalised heading over a group (LEVELS, BACKDROPS) */
void section_label(const char *text)
{
    ImGui::PushFont(font_bold);
    ImGui::SetWindowFontScale(0.8f);
    ImGui::PushStyleColor(ImGuiCol_Text, T.muted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
}

/* an accent button with a drop-down arrow of its own, which opens popup_id */
bool split_button(const char *label, const char *icon, const char *popup_id, const char *t, ImFont *font)
{
    ImGuiStyle &st = ImGui::GetStyle();
    ImVec4 fill = theme_dark ? mix(T.accent, ImVec4(0, 0, 0, 1), 0.35f) : T.accent;
    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(fill, ImVec4(1, 1, 1, 1), 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(fill, ImVec4(0, 0, 0, 1), 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Border, theme_dark ? T.accent : fill);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    char b[96];
    snprintf(b, sizeof b, "%s %s", icon, label);
    ImGui::PushFont(font ? font : font_bold);
    bool r = ImGui::Button(b);
    ImGui::PopFont();
    tip(t);
    ImGui::SameLine(0, 1);
    char arrow[64];
    snprintf(arrow, sizeof arrow, ICON_EXPAND_MORE "##%s", popup_id);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x * 0.4f, st.FramePadding.y));
    if (ImGui::Button(arrow)) ImGui::OpenPopup(popup_id);
    ImGui::PopStyleVar();
    tip("More ways to play, and what Duke carries");
    ImGui::PopStyleColor(5);
    return r;
}
