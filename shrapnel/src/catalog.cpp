/*
 * catalog.cpp - "shrapnel --dir <game> --catalog <file.json>": every tile and
 * actor of each episode in the folder, as the game draws them, for naming.
 *
 * Writes one JSON file: per episode, the palette, every map tile (16 x 16
 * palette indices, as hex), every actor's look (its pieces, and what a crate
 * holds), and how the stock levels use each value.  tools/dn1_catalog.py
 * turns it into a spreadsheet.
 */
#include "shrapnel.h"
#include <stdio.h>

static void hex_px(FILE *f, const uint8_t px[256])
{
    static const char d[] = "0123456789abcdef";
    fputc('"', f);
    for (int i = 0; i < 256; i++) {
        uint8_t v = px[i] == 255 ? 0xFF : (uint8_t)(px[i] & 15);
        fputc(d[v >> 4], f);
        fputc(d[v & 15], f);
    }
    fputc('"', f);
}
static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}
static void pieces(FILE *f, const std::vector<Piece> &v)
{
    fputc('[', f);
    for (size_t k = 0; k < v.size(); k++) {
        fprintf(f, "%s{\"dx\":%d,\"dy\":%d,\"px\":", k ? "," : "", v[k].dx, v[k].dy);
        hex_px(f, v[k].px);
        fputc('}', f);
    }
    fputc(']', f);
}

int write_catalog(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "can't write %s\n", path); return 1; }
    static Episode ep;
    int any = 0;
    fprintf(f, "{\"episodes\":[");
    for (int e = 1; e <= 3; e++) {
        if (!ep.load(e)) continue;
        fprintf(f, "%s\n{\"number\":%d,\"palette\":[", any ? "," : "", e);
        for (int i = 0; i < 16; i++) fprintf(f, "%s%u", i ? "," : "", (unsigned)ep.pal[i]);
        fprintf(f, "],\n\"tiles\":{");
        /* every map tile: 40h..2FE0h in steps of 20h (0 and 20h are the backdrops) */
        uint8_t px[256];
        for (int v = 0x40; v <= 0x2FE0; v += 0x20) {
            ed_tile((uint16_t)v, px);
            fprintf(f, "%s\n\"%04X\":", v > 0x40 ? "," : "", v);
            hex_px(f, px);
        }
        fprintf(f, "},\n\"actors\":[");
        for (int i = 0; i < N_ACTORS; i++) {
            uint16_t id = (uint16_t)(0x3000 + i);
            int cap = 0, table = ed_actor_limit(id, &cap);
            fprintf(f, "%s\n{\"id\":\"%04X\",\"name\":", i ? "," : "", id);
            json_str(f, actor_name(id) ? actor_name(id) : "");
            fprintf(f, ",\"after\":\"%04X\",\"crate\":%s,\"table\":%d,\"cap\":%d,\"pieces\":",
                    ep.looks[i].after, actor_crate(id) ? "true" : "false", table, cap);
            pieces(f, ep.looks[i].pieces);
            fprintf(f, ",\"contents\":");
            pieces(f, ep.looks[i].contents);
            fputc('}', f);
        }
        /* how the stock levels use each value: cells, and which levels */
        std::map<uint16_t, std::pair<int, std::string>> use;
        for (int slot = 0; slot <= 20; slot++) {
            if ((slot & 1) && slot != 1) continue;          /* (one hallway map serves them all) */
            uint16_t m[ED_MAP_CELLS];
            std::string file = ep.slot_file(slot);
            if (file.empty() || !read_map_file(game_path(file.c_str()), m)) continue;
            std::string label = (slot & 1) ? "H" : "L" + std::to_string(slot / 2 + 1);
            std::map<uint16_t, int> here;
            for (int k = 0; k < ED_MAP_CELLS; k++) here[m[k]]++;
            for (auto &kv : here) {
                auto &u = use[kv.first];
                u.first += kv.second;
                u.second += (u.second.empty() ? "" : " ") + label;
            }
        }
        fprintf(f, "],\n\"usage\":{");
        bool first = true;
        for (auto &kv : use) {
            fprintf(f, "%s\n\"%04X\":{\"cells\":%d,\"levels\":\"%s\"}", first ? "" : ",", kv.first, kv.second.first, kv.second.second.c_str());
            first = false;
        }
        fprintf(f, "}}");
        any = 1;
    }
    fprintf(f, "\n]}\n");
    fclose(f);
    if (!any) { fprintf(stderr, "no episodes (DN1/2/3.EXE) in the game folder\n"); return 1; }
    printf("wrote %s\n", path);
    return 0;
}
