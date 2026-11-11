/*
 * picture.cpp - backdrops: the game's backdrop files, and pictures made into them.
 *
 * A backdrop file (DROPn.DN1 and the like): a 3-byte header, then 13 x 10
 * tiles of 16 x 16, each as 16 rows of two 8-pixel groups; a group is 5
 * bytes - a mask byte (always FFh; the game skips it) and the four EGA
 * planes, leftmost pixel in the top bit.  20,803 bytes in all.
 */
#include "shrapnel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string.h>

#include "third_party/stb_image.h"          /* (built into the core, PNG only: src/port_art.c) */

namespace fs = std::filesystem;

static const int TILES_X = ED_BD_W / 16, TILES = (ED_BD_W / 16) * (ED_BD_H / 16);
static const size_t DROP_BYTES = 3 + (size_t)TILES * 32 * 5;

bool drop_decode(const std::string &f, uint8_t *px)
{
    if (f.size() < 8) return false;
    for (int t = 0; t < TILES; t++)
        for (int row = 0; row < 16; row++)
            for (int half = 0; half < 2; half++) {
                size_t at = 3 + ((size_t)t * 32 + row * 2 + half) * 5;
                uint8_t pl[4];
                for (int p = 0; p < 4; p++) pl[p] = at + 1 + p < f.size() ? (uint8_t)f[at + 1 + p] : 0xFF;   /* as the game reads past the end */
                uint8_t *o = px + ((t / TILES_X) * 16 + row) * ED_BD_W + (t % TILES_X) * 16 + half * 8;
                for (int b = 0; b < 8; b++) {
                    int s = 7 - b;
                    o[b] = (uint8_t)(((pl[0] >> s) & 1) | ((pl[1] >> s) & 1) << 1 | ((pl[2] >> s) & 1) << 2 | ((pl[3] >> s) & 1) << 3);
                }
            }
    return true;
}

std::string drop_encode(const uint8_t *px)
{
    std::string f(DROP_BYTES, '\0');
    for (int t = 0; t < TILES; t++)
        for (int row = 0; row < 16; row++)
            for (int half = 0; half < 2; half++) {
                size_t at = 3 + ((size_t)t * 32 + row * 2 + half) * 5;
                const uint8_t *i = px + ((t / TILES_X) * 16 + row) * ED_BD_W + (t % TILES_X) * 16 + half * 8;
                uint8_t pl[4] = { 0, 0, 0, 0 };
                for (int b = 0; b < 8; b++)
                    for (int p = 0; p < 4; p++) pl[p] |= (uint8_t)(((i[b] >> p) & 1) << (7 - b));
                f[at] = (char)0xFF;
                for (int p = 0; p < 4; p++) f[at + 1 + p] = (char)pl[p];
            }
    return f;
}

/* any picture: scaled to cover 208 x 160 (the middle kept), then each pixel
 * the nearest of the 16 colours - or, dithered, spreading the difference */
bool picture_to_drop(const std::string &path, bool dither, const uint32_t pal[16], uint8_t *px, std::string &err)
{
    std::string data;
    {
        std::ifstream in(fs::u8path(path), std::ios::binary);
        if (!in) { err = "Couldn't read " + path; return false; }
        std::ostringstream s;
        s << in.rdbuf();
        data = s.str();
    }
    int w = 0, h = 0, n = 0;
    uint8_t *img = stbi_load_from_memory((const stbi_uc *)data.data(), (int)data.size(), &w, &h, &n, 4);
    if (!img) { err = "Not a PNG picture Shrapnel can read: " + path_name(path); return false; }
    /* the part of the picture that becomes the backdrop */
    double sc = std::max((double)ED_BD_W / w, (double)ED_BD_H / h);
    double cw = ED_BD_W / sc, ch = ED_BD_H / sc;        /* in source pixels */
    double x0 = (w - cw) / 2, y0 = (h - ch) / 2;
    std::vector<float> rgb((size_t)ED_BD_W * ED_BD_H * 3);
    for (int y = 0; y < ED_BD_H; y++)
        for (int x = 0; x < ED_BD_W; x++) {
            /* the average of the source pixels under this one (area sampling) */
            double sx0 = x0 + x * cw / ED_BD_W, sx1 = x0 + (x + 1) * cw / ED_BD_W;
            double sy0 = y0 + y * ch / ED_BD_H, sy1 = y0 + (y + 1) * ch / ED_BD_H;
            double acc[3] = { 0, 0, 0 }, wt = 0;
            for (int yy = (int)std::floor(sy0); yy < (int)std::ceil(sy1); yy++)
                for (int xx = (int)std::floor(sx0); xx < (int)std::ceil(sx1); xx++) {
                    double fx = std::min(sx1, xx + 1.0) - std::max(sx0, (double)xx);
                    double fy = std::min(sy1, yy + 1.0) - std::max(sy0, (double)yy);
                    if (fx <= 0 || fy <= 0) continue;
                    const uint8_t *p = img + ((size_t)std::min(std::max(yy, 0), h - 1) * w + std::min(std::max(xx, 0), w - 1)) * 4;
                    double a = p[3] / 255.0 * fx * fy;      /* see-through: over black */
                    for (int c = 0; c < 3; c++) acc[c] += p[c] * a;
                    wt += fx * fy;
                }
            for (int c = 0; c < 3; c++) rgb[((size_t)y * ED_BD_W + x) * 3 + c] = wt > 0 ? (float)(acc[c] / wt) : 0;
        }
    stbi_image_free(img);
    float pr[16][3];
    for (int i = 0; i < 16; i++) {
        pr[i][0] = (float)(pal[i] >> 16 & 0xFF); pr[i][1] = (float)(pal[i] >> 8 & 0xFF); pr[i][2] = (float)(pal[i] & 0xFF);
    }
    for (int y = 0; y < ED_BD_H; y++)
        for (int x = 0; x < ED_BD_W; x++) {
            float *c = &rgb[((size_t)y * ED_BD_W + x) * 3];
            int best = 0;
            float bd = 1e30f;
            for (int i = 0; i < 16; i++) {
                float dr = c[0] - pr[i][0], dg = c[1] - pr[i][1], db = c[2] - pr[i][2];
                float d = dr * dr * 0.30f + dg * dg * 0.59f + db * db * 0.11f;
                if (d < bd) { bd = d; best = i; }
            }
            px[y * ED_BD_W + x] = (uint8_t)best;
            if (!dither) continue;
            static const int dx[4] = { 1, -1, 0, 1 }, dy[4] = { 0, 1, 1, 1 };
            static const float wgt[4] = { 7 / 16.f, 3 / 16.f, 5 / 16.f, 1 / 16.f };
            for (int k = 0; k < 4; k++) {
                int nx = x + dx[k], ny = y + dy[k];
                if (nx < 0 || nx >= ED_BD_W || ny >= ED_BD_H) continue;
                float *q = &rgb[((size_t)ny * ED_BD_W + nx) * 3];
                for (int ch2 = 0; ch2 < 3; ch2++) q[ch2] += (c[ch2] - pr[best][ch2]) * wgt[k];
            }
        }
    return true;
}

/* "DROP5.DN2" style: DROP, a number, .DN1..3 - sorted by episode, then number */
std::vector<std::string> game_drop_files()
{
    std::vector<std::string> out;
    std::error_code ec;
    if (!shrapnel_game_dir) return out;
    for (auto &d : fs::directory_iterator(fs::u8path(shrapnel_game_dir), ec)) {
        std::string n = d.path().filename().u8string(), u = n;
        for (char &c : u) c = (char)toupper((unsigned char)c);
        if (u.size() < 9 || u.compare(0, 4, "DROP") || u.size() > 12) continue;
        size_t dot = u.find('.');
        if (dot == std::string::npos || dot == 4 || u.compare(dot, 3, ".DN") || u.size() != dot + 4 || u[dot + 3] < '1' || u[dot + 3] > '3') continue;
        bool digits = true;
        for (size_t i = 4; i < dot; i++) digits = digits && isdigit((unsigned char)u[i]);
        if (digits) out.push_back(u);
    }
    std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
        char ea = a.back(), eb = b.back();
        if (ea != eb) return ea < eb;
        return atoi(a.c_str() + 4) < atoi(b.c_str() + 4);
    });
    return out;
}

std::string stock_drop_name(int level, int which)
{
    const char *n = ed_stock_drop((std::max(1, std::min(10, level)) - 1) * 2, which);
    std::string s = n ? n : "";
    for (char &c : s) c = (char)toupper((unsigned char)c);
    return s;
}

/* a backdrop file of the game folder's, as pixels */
bool game_drop_pixels(const std::string &name, uint8_t *px)
{
    std::ifstream in(fs::u8path(game_path(name.c_str())), std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    return drop_decode(s.str(), px);
}
