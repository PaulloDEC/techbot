/*
 * port_render.c - smooth motion: in-between frames at the display rate.
 *
 * The game still updates at its original rate (~16 Hz).  While it waits for
 * the next update, the play window is redrawn from a record of the last two
 * frames with the camera and every sprite placed between their two
 * positions.  Everything drawn comes from the same data the original uses
 * (tile cache in EGA memory, sprite buffers), on the original 320x200 pixel
 * grid, so the look is unchanged: only positions between frames are new.
 *
 * What is recorded per game frame (between dl_begin_frame and dl_end_frame):
 *   - the camera (scroll_x/scroll_y) and anim_tick when the map is drawn,
 *     plus a copy of the map cells around the view (actors may change the
 *     live map after it was drawn);
 *   - every tile, sprite, glyph and mirror copy drawn inside the play window
 *     except the map tiles themselves, with its world position and a group
 *     tag (which routine drew it) used to match it to the previous frame.
 * Frames in which a dialog was drawn, and large camera jumps (respawn,
 * teleport, level change), are shown exactly as the original drew them.
 */
#include "dn1.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define WIN_X0   16          /* play window: byte columns 2..27, lines 16..175 */
#define WIN_Y0   16
#define WIN_W    208
#define WIN_H    160
#define MAP_COLS 48          /* map cells kept around the view (wide views too) */
#define VIEW_MAX 640         /* widest play area in pixels                       */
#define VIEW_HMAX 200        /* tallest play area (immersive: the whole screen)  */
#define MAP_ROWS 20          /* map rows kept: 5 above the view (mirror floors), 15 down */
#define MAP_H    1440        /* 90 rows */
#define ABOVE    64          /* rows drawn above the window for mirror reflections */
#define MAX_OPS  400
#define SNAP_PX  48          /* camera moves larger than this are not slid     */
#define MATCH_PX 40          /* an object moving further than this is new      */

typedef struct {
    uint8_t type, group;
    const uint8_t *src;      /* sprite / glyph data                            */
    uint16_t tile;           /* tile cache offset (DL_TILE)                    */
    int16_t wx, wy;          /* world pixels (screen-fixed for glyphs)         */
    int16_t back;            /* mirror shimmer offset                          */
    int16_t match;           /* index of the same object in the previous frame */
    int16_t rsx, rsy;        /* test aid: where the last refresh drew it (screen) */
    uint8_t drawn;
} Op;

typedef struct {
    int valid, dirty, has_world;
    int mirror_top;          /* a mirror reflects from above the window        */
    int dx0, dy0, dx1, dy1;  /* dialog rectangle (window pixels) when dirty    */
    int cam_x, cam_y;        /* world pixels of the window's top-left          */
    uint16_t anim;
    int c0, r0;
    uint16_t cells[MAP_ROWS][MAP_COLS];
    int nops;
    Op ops[MAX_OPS];
} Frame;

static Frame frames[3];      /* previous, current, being recorded              */
static Frame *prev = &frames[0], *cur = &frames[1], *rec = &frames[2];
static unsigned serial;
int dl_dbg_cx, dl_dbg_cy, dl_dbg_obj;   /* test aid: last rendered camera / Duke */
static int jumplog;                     /* test aid: DL_JUMPLOG set */
int dl_capturing, dl_in_world, dl_waiting;
static int direct, ddx0, ddy0, ddx1, ddy1;   /* direct drawing since the last frame began */
static int dlgd, dgx0, dgy0, dgx1, dgy1;     /* ... of which dialogs and text (screen-fixed) */
int port_in_play;
uint8_t dl_group;

int dl_wide, dl_view_w = WIN_W, dl_view_h = WIN_H, dl_immersive, dl_hud;
int dl_ext_bd2;                     /* the platform draws the second backdrop (index 17) */
int port_act_left, port_act_right, port_act_up, port_act_down, port_aud_left;

static int smooth_on(void) { return cfg.smooth != 0 && cfg.mode == MODE_ENHANCED; }
/* recording runs whenever the port draws the play area itself */
int dl_active(void) { return cfg.mode == MODE_ENHANCED && (cfg.smooth != 0 || cfg.widescreen || cfg.hud); }

void dl_set_view(int wide, int view_w, int view_h)
{
    dl_wide = wide && (cfg.widescreen || cfg.hud) && cfg.mode == MODE_ENHANCED;
    dl_immersive = dl_wide && cfg.hud != HUD_CLASSIC && view_h > WIN_H;
    if (view_w > VIEW_MAX) view_w = VIEW_MAX;
    if (view_h > VIEW_HMAX) view_h = VIEW_HMAX;
    dl_view_w = dl_wide && view_w > WIN_W ? view_w : WIN_W;
    dl_view_h = dl_immersive ? view_h : WIN_H;
    if (!dl_wide) port_act_left = port_act_right = port_aud_left = 0;
    if (!dl_immersive) port_act_up = port_act_down = 0;
}
int dl_play_x0(void) { return dl_immersive ? 0 : WIN_X0; }
int dl_play_y0(void) { return dl_immersive ? 0 : WIN_Y0; }

/* the display camera: the wide area centred on the game's view, stopped at
 * the map's edges (128 tiles = 2048 px) */
static int disp_cam(int cam_x)
{
    int x = cam_x - (dl_view_w - WIN_W) / 2;
    if (x > 2048 - dl_view_w) x = 2048 - dl_view_w;
    if (x < 0) x = 0;
    return x;
}
/* ... and vertically (immersive: a taller area, centred the same way and
 * stopped at the top and bottom of the map - but never so far that part of
 * the game's own view would be cut off) */
/* rows of the taller area above the game's own view: centred in the part
 * the Immersive HUD bar leaves visible, so nothing the original showed is
 * ever behind the bar (the extra rows below it are) */
int hud_bar_top(void);
static int view_above(void)
{
    int extra = dl_view_h - WIN_H, a;
    if (extra <= 0) return 0;
    /* (no bar: centred in the whole area) */
    a = ((cfg.hud == HUD_OFF ? dl_view_h : hud_bar_top()) - WIN_H) / 2;
    return a < 0 ? 0 : a > extra ? extra : a;
}
static int disp_cam_y(int cam_y)
{
    int y, extra = dl_view_h - WIN_H;
    if (extra <= 0) return cam_y;
    y = cam_y - view_above();
    if (y > MAP_H - dl_view_h) y = MAP_H - dl_view_h;
    if (y < 0) y = 0;
    if (y > cam_y) y = cam_y;
    if (y < cam_y - extra) y = cam_y - extra;
    return y;
}
/* the frame widescreen composes from: the last finished one or, before any
 * frame has finished (a popup in the first frame of a level or after a
 * load), the one being recorded once its camera and map are known */
static Frame *view_frame(void)
{
    if (cur->valid) return cur;
    if (dl_capturing && rec->has_world) return rec;
    return NULL;
}
int dl_view_offset(void) { Frame *f = view_frame(); return f ? f->cam_x - disp_cam(f->cam_x) : (dl_view_w - WIN_W) / 2; }
int dl_view_offset_y(void) { Frame *f = view_frame(); return f ? f->cam_y - disp_cam_y(f->cam_y) : view_above(); }
int dl_frame_ready(void) { return dl_active() && view_frame() != NULL; }
unsigned dl_serial(void) { return serial; }

int dl_owns_screen(void)
{
    if (dl_capturing && rec->dirty) return 0;          /* a dialog is up in this frame */
    if (dl_wide && dl_active() && cur->valid) return 1;   /* wide: composed by the port */
    if (!smooth_on() || !cur->valid || cur->dirty) return 0;
    return 1;
}

void dl_reset(void) { prev->valid = cur->valid = rec->valid = 0; dl_capturing = 0; }

void dl_begin_frame(void)
{
    if (!dl_active()) { dl_capturing = 0; return; }
    rec->valid = 1; rec->dirty = 0; rec->has_world = 0; rec->nops = 0;
    dl_capturing = 1;
    direct = 0; dlgd = 0;
    /* widescreen: the activity window covers the visible area (8-px columns)
     * plus a margin.  An object's position is its left (and lowest) piece:
     * the original's window starts exactly at its left edge, so an object
     * half off the left side is not drawn at all and pops in - at the edge
     * of a large screen that shows, so here objects are woken a tile early */
    if (dl_wide) {
        int cx = scroll_x * 8, d = disp_cam(cx);
        port_act_left = (cx - d + 7) / 8 + 2;
        port_aud_left = port_act_left - 2;
        port_act_right = (d + dl_view_w - (cx + WIN_W) + 7) / 8;   /* (the original has 3 columns spare) */
    } else port_act_left = port_act_right = port_aud_left = 0;
    /* immersive: and the rows above and below it (map units, 8 per pixel),
     * with the same margin above (the original's half tile, made a tile) and
     * two tiles below, where tall objects' upper pieces show first */
    if (dl_immersive) {
        int cy = (scroll_y >> 7) * 16, d = disp_cam_y(cy);
        port_act_up = (cy - d) * 8 + 0x40;
        port_act_down = (d + dl_view_h - (cy + WIN_H)) * 8 + 0x100;
    } else port_act_up = port_act_down = 0;
}

/* A sprite's line as the original computes it: some routines take the
 * height above the view as an unsigned 16-bit difference, so an object just
 * above the window gets a line near 8192 - which lands in the right place
 * only because the original's 16-bit screen offsets wrap.  Here it becomes
 * the negative line it stands for. */
static int real_line(int y) { return y >= 0x1000 ? y - 0x2000 : y; }

int dl_clip(int type, int x, int y, int wbytes)
{
    /* an actor's sprite outside the original window: only the wide renderer
     * shows it; EGA memory there holds the borders and the status panel */
    /* (whenever play is on, not only inside recorded frames: the game also
     * draws after the page flip and in in-between sequences) */
    /* any row: an object's lower pieces can start below the play area, and a
     * negative column there wraps into the status panel's corner */
    if (!dl_wide || !port_in_play || dl_hud || (type != DL_SPRITE && type != DL_MIRROR)) return 0;
    if (x < 2 || x + wbytes > 28) return 1;
    /* immersive: actors above and below the original window are active too;
     * anything the original could not have drawn there (it draws objects from
     * half a tile above the window down to its bottom) is the renderer's alone */
    if (dl_immersive) { y = real_line(y); return y < 8 || y >= WIN_Y0 + WIN_H; }
    return 0;
}

/* rectangles in play-window pixels, clamped to the original 208 x 160 view */
static void rect_add(int *x0, int *y0, int *x1, int *y1, int first, int x, int y, int w, int h)
{
    int a = x - WIN_X0, b = y - WIN_Y0, c = a + w, d = b + h;
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (c > WIN_W) c = WIN_W;
    if (d > WIN_H) d = WIN_H;
    if (c <= a || d <= b) return;
    if (first) { *x0 = a; *y0 = b; *x1 = c; *y1 = d; return; }
    if (a < *x0) *x0 = a;
    if (b < *y0) *y0 = b;
    if (c > *x1) *x1 = c;
    if (d > *y1) *y1 = d;
}

void dl_mark_dirty(int x, int y, int w, int h)
{
    if (!dl_capturing) {             /* a dialog outside a frame: screen-fixed */
        if (port_in_play && !dl_hud && ega_draw_page == ega_display_page &&
            !(x + w <= WIN_X0 || x >= WIN_X0 + WIN_W || y + h <= WIN_Y0 || y >= WIN_Y0 + WIN_H)) {
            rect_add(&dgx0, &dgy0, &dgx1, &dgy1, !dlgd, x, y, w, h);
            dlgd = 1;
        }
        return;
    }
    rect_add(&rec->dx0, &rec->dy0, &rec->dx1, &rec->dy1, !rec->dirty, x, y, w, h);
    rec->dirty = 1;
}

/* drawing in the play window outside a recorded frame (after the page flip,
 * dialogs, in-between sequences): widescreen shows that area from EGA memory
 * until the next frame begins */
void dl_note_direct(int x, int y, int w, int h)
{
    if (!port_in_play || dl_hud) return;
    if (ega_draw_page != ega_display_page) return;      /* not on screen yet: nothing to show */
    if (x + w <= WIN_X0 || x >= WIN_X0 + WIN_W || y + h <= WIN_Y0 || y >= WIN_Y0 + WIN_H) return;
    /* inside a dialog already up (its tiles, its text, the menu cursor): part
     * of that dialog, which is laid over as a whole */
    if (dlgd && x - WIN_X0 >= dgx0 && x + w - WIN_X0 <= dgx1 && y - WIN_Y0 >= dgy0 && y + h - WIN_Y0 <= dgy1) return;
    rect_add(&ddx0, &ddy0, &ddx1, &ddy1, !direct, x, y, w, h);
    direct = 1;
}

static int cam_x_now(void) { return scroll_x * 8; }
static int cam_y_now(void) { return (scroll_y >> 7) * 16; }

void dl_world(void)
{
    int r, c;
    if (!dl_capturing) return;
    rec->has_world = 1;
    rec->cam_x = cam_x_now(); rec->cam_y = cam_y_now();
    rec->anim = anim_tick;
    rec->c0 = (disp_cam(rec->cam_x) >> 4) - 4;
    rec->r0 = (disp_cam_y(rec->cam_y) >> 4) - 5;
    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++) {
            int mr = rec->r0 + r, mc = rec->c0 + c;
            rec->cells[r][c] = (mr < 0 || mr >= 90 || mc < 0 || mc >= 128) ? 0
                             : MAP16((mr << 8) + (mc << 1));
        }
}

void dl_record(int type, const uint8_t *src, uint16_t tile, int col, int line, int back)
{
    Op *o;
    int sx = col * 8, w = (type == DL_GLYPH || type == DL_GLYPH_OPAQUE) ? 8 : 16;
    int left = WIN_X0, right = WIN_X0 + WIN_W, top = WIN_Y0, bottom = WIN_Y0 + WIN_H;
    if (!dl_capturing || dl_in_world || dl_hud || rec->nops >= MAX_OPS) return;
    /* only what goes onto the page being built: popups (hints, level notes,
     * menus) are drawn onto the page on screen, over the game, and vanish when
     * they close - they are shown live by the overlay, never replayed */
    if (ega_draw_page == ega_display_page) return;
    if (dl_wide && (type == DL_SPRITE || type == DL_MIRROR)) {   /* the wide play area */
        int off = cam_x_now() - disp_cam(cam_x_now());
        left -= off; right += dl_view_w - WIN_W - off;
        if (dl_immersive) {
            int offy = cam_y_now() - disp_cam_y(cam_y_now());
            top -= offy; bottom += dl_view_h - WIN_H - offy;
        }
    }
    if (type == DL_SPRITE || type == DL_MIRROR) line = real_line(line);
    /* only what lands in the play area (the status panel is left alone) */
    if (sx + w <= left || sx >= right || line + (type == DL_MIRROR ? 32 : 16) <= top || line >= bottom) return;
    o = &rec->ops[rec->nops++];
    o->type = (uint8_t)type; o->group = dl_group; o->src = src; o->tile = tile; o->back = (int16_t)back;
    o->match = -1; o->drawn = 0;
    if (type == DL_GLYPH || type == DL_GLYPH_OPAQUE) {      /* text: fixed to the screen */
        o->wx = (int16_t)(sx - WIN_X0); o->wy = (int16_t)(line - WIN_Y0);
    } else {
        o->wx = (int16_t)(sx - WIN_X0 + cam_x_now()); o->wy = (int16_t)(line - WIN_Y0 + cam_y_now());
    }
}

/* Match each object of the new frame to the same object in the previous one.
 *
 * Every routine (group) draws its objects - and each object's 16x16 pieces -
 * in a fixed order.  So within a group the two lists of pieces are aligned
 * in order, like comparing two versions of a list: pieces may be added or
 * removed (objects appearing, dying, blinking), but matched pieces never
 * swap places.  Order matters more than distance: an object moving exactly
 * one piece-width or piece-height per frame (Duke falling 16 px with the
 * camera following) has an old piece lying exactly where a different new
 * piece is now; nearest-distance matching paired those and tore the object
 * apart while it glided.  The alignment cost of a pair is its distance,
 * cheaper for the same graphic; pairs further apart than MATCH_PX are not
 * allowed. */
#define GAP_COST (MATCH_PX * MATCH_PX / 2)
static void align_group(Frame *a, const int16_t *ia, int na, Frame *b, const int16_t *ib, int nb)
{
    static int32_t cost[MAX_OPS + 1][MAX_OPS + 1];
    static uint8_t how[MAX_OPS + 1][MAX_OPS + 1];     /* 0 match, 1 skip new, 2 skip old */
    int i, j;
    for (i = 0; i <= nb; i++) for (j = 0; j <= na; j++) {
        int32_t best, c;
        uint8_t h;
        if (i == 0 && j == 0) { cost[0][0] = 0; continue; }
        best = 0x7FFFFFFF; h = 0;
        if (i > 0 && j > 0) {
            const Op *o = &b->ops[ib[i - 1]], *p = &a->ops[ia[j - 1]];
            int dx = o->wx - p->wx, dy = o->wy - p->wy;
            int32_t d = dx * dx + dy * dy;
            if (d <= MATCH_PX * MATCH_PX) {
                if (p->src == o->src && p->tile == o->tile) d /= 2;     /* same graphic: cheaper */
                c = cost[i - 1][j - 1] + d;
                if (c < best) { best = c; h = 0; }
            }
        }
        if (i > 0) { c = cost[i - 1][j] + GAP_COST; if (c < best) { best = c; h = 1; } }
        if (j > 0) { c = cost[i][j - 1] + GAP_COST; if (c < best) { best = c; h = 2; } }
        cost[i][j] = best; how[i][j] = h;
    }
    for (i = nb, j = na; i > 0 || j > 0; ) {
        if (how[i][j] == 0 && i > 0 && j > 0) { b->ops[ib[i - 1]].match = ia[j - 1]; i--; j--; }
        else if (how[i][j] == 1 && i > 0) i--;
        else j--;
    }
}

static void match_ops(Frame *a, Frame *b)
{
    static int16_t ia[MAX_OPS], ib[MAX_OPS];
    static uint8_t done[MAX_OPS];
    int i, j;
    memset(done, 0, sizeof done);
    for (i = 0; i < b->nops; i++) b->ops[i].match = -1;
    for (i = 0; i < b->nops; i++) {
        Op *o = &b->ops[i];
        int na = 0, nb = 0;
        if (done[i] || o->type == DL_GLYPH || o->type == DL_GLYPH_OPAQUE) continue;
        for (j = i; j < b->nops; j++)
            if (b->ops[j].group == o->group && b->ops[j].type == o->type) { ib[nb++] = (int16_t)j; done[j] = 1; }
        for (j = 0; j < a->nops; j++)
            if (a->ops[j].group == o->group && a->ops[j].type == o->type) ia[na++] = (int16_t)j;
        if (na) align_group(a, ia, na, b, ib, nb);
    }
}

void dl_end_frame(void)
{
    Frame *t;
    if (!dl_capturing) return;
    dl_capturing = 0;
    if (!rec->has_world) rec->valid = 0;
    {   /* a reflection reaching above the play window (the original copies the
         * border and memory outside the page there; the port reflects the world) */
        int i;
        rec->mirror_top = 0;
        for (i = 0; i < rec->nops; i++)
            if (rec->ops[i].type == DL_MIRROR && rec->ops[i].wy - disp_cam_y(rec->cam_y) < 61) rec->mirror_top = 1;
    }
    if (rec->valid && cur->valid) match_ops(cur, rec);
    t = prev; prev = cur; cur = rec; rec = t;
    serial++;
}

/* ---------------------------------------------------------------- drawing */
static uint8_t winbuf[ABOVE + VIEW_HMAX][VIEW_MAX];
#define win (winbuf + ABOVE)        /* win[y], y from -ABOVE (above the window) */
static int view_w = WIN_W;          /* width being drawn                        */
static int view_h = WIN_H;          /* height being drawn                       */
static int wide_backdrop;           /* backdrop-1 pixels left as index 16       */

/* a + (b - a) * t, rounded to the nearest pixel the same way in both directions */
static int lerp_px(int a, int b, double t) { return (int)floor(a + (b - a) * t + 0.5); }

static int tile_pixel(uint16_t off, int x, int y)
{
    uint16_t o = (uint16_t)(0x4000 + off + y * 2 + (x >> 3));
    int s = 7 - (x & 7);
    return ((ega_planes[0][o] >> s) & 1) | (((ega_planes[1][o] >> s) & 1) << 1) |
           (((ega_planes[2][o] >> s) & 1) << 2) | (((ega_planes[3][o] >> s) & 1) << 3);
}

/* backdrop pixel at play-area position (px,py): fixed to the screen, 13 x 10
 * tiles; a wider (taller) area stretches it to fill */
static int backdrop_pixel(uint16_t cache, int px, int py)
{
    int bx = view_w == WIN_W ? px : px * WIN_W / view_w, by;
    if (py < 0) py += view_h;                 /* above the window (reflections only) */
    by = view_h == WIN_H ? py : py * WIN_H / view_h;
    return tile_pixel((uint16_t)(cache + (((by >> 4) * 13 + (bx >> 4)) << 5)), bx & 15, by & 15);
}

void dl_backdrop(uint8_t *out)
{
    int x, y;
    for (y = 0; y < WIN_H; y++)
        for (x = 0; x < WIN_W; x++)
            out[y * WIN_W + x] = (uint8_t)tile_pixel((uint16_t)(0x4000 + (((y >> 4) * 13 + (x >> 4)) << 5)), x & 15, y & 15);
}

static void draw_map(const Frame *f, int cx, int cy)
{
    int px, py;
    for (py = f->mirror_top ? -ABOVE : 0; py < view_h; py++) {
        int wy = cy + py, mr = (wy >> 4) - f->r0;
        for (px = 0; px < view_w; px++) {
            int wx = cx + px, mc = (wx >> 4) - f->c0;
            uint16_t c = (mr >= 0 && mr < MAP_ROWS && mc >= 0 && mc < MAP_COLS) ? f->cells[mr][mc] : 0;
            if (c <= 0x20) {
                if (c != 0x20 && wide_backdrop) win[py][px] = 16;   /* stretched by the platform layer */
                else if (c == 0x20 && wide_backdrop && dl_ext_bd2) win[py][px] = 17;   /* replacement art */
                else win[py][px] = (uint8_t)backdrop_pixel(c == 0x20 ? 0x8100 : 0x4000, px, py);
            } else {
                uint16_t t = c < 0x5E0 ? (uint16_t)(c + f->anim) : c;
                win[py][px] = (uint8_t)tile_pixel(t, wx & 15, wy & 15);
            }
        }
    }
}

static void plot(int x, int y, int c) { if (x >= 0 && x < view_w && y >= -ABOVE && y < view_h) win[y][x] = (uint8_t)c; }

/* 5-byte groups per 8 pixels: mask (1 = opaque), then planes 0..3 */
static void draw_masked(const uint8_t *src, int bytes_w, int rows, int x, int y, int opaque)
{
    int r, b, bit;
    for (r = 0; r < rows; r++)
        for (b = 0; b < bytes_w; b++) {
            const uint8_t *g = src + (r * bytes_w + b) * 5;
            for (bit = 0; bit < 8; bit++) {
                int s = 7 - bit;
                if (!opaque && !((g[0] >> s) & 1)) continue;
                plot(x + b * 8 + bit, y + r,
                     ((g[1] >> s) & 1) | (((g[2] >> s) & 1) << 1) | (((g[3] >> s) & 1) << 2) | (((g[4] >> s) & 1) << 3));
            }
        }
}

static void draw_tile_at(uint16_t t, int x, int y)
{
    int r, c;
    for (r = 0; r < 16; r++) for (c = 0; c < 16; c++) plot(x + c, y + r, tile_pixel(t, c, r));
}

/* the mirror floor: line y+2+k receives line y-1-back-2k (16 px wide).
 * Near the top of the window the source rises above it: the original then
 * copies the top border and, further up, bytes from outside the display page
 * (black or stale - and different on each page); here the world above is
 * reflected, drawn into the rows above the window. */
static void mirror_at(int x, int y, int back)
{
    int k, c;
    for (k = 0; k < 30; k++) {
        int dy = y + 2 + k, sy = y - 1 - back / 40 - 2 * k;
        if (dy < 0 || dy >= view_h || sy < -ABOVE || sy >= view_h) continue;
        for (c = 0; c < 16; c++) if (x + c >= 0 && x + c < view_w) win[dy][x + c] = win[sy][x + c];
    }
}

/* draw the current frame (gliding from the previous one by 'alpha', or at
 * its own positions when 'glide' is 0) into win[][] at width view_w */
static void render_view(double alpha, int glide)
{
    int cx, cy, dcx, dcy, i;
    if (glide) {
        cx = lerp_px(prev->cam_x, cur->cam_x, alpha);
        cy = lerp_px(prev->cam_y, cur->cam_y, alpha);
    } else { cx = cur->cam_x; cy = cur->cam_y; alpha = 1.0; }
    dcx = view_w == WIN_W ? cx : disp_cam(cx);
    dcy = view_h == WIN_H ? cy : disp_cam_y(cy);
    dl_dbg_cx = cx; dl_dbg_cy = cy; dl_dbg_obj = -9999;
    {   static int checked; if (!checked) { checked = 1; jumplog = getenv("DL_JUMPLOG") != NULL; } }
    draw_map(cur, dcx, dcy);
    for (i = 0; i < cur->nops; i++) {
        const Op *o = &cur->ops[i];
        int wx = o->wx, wy = o->wy, sx, sy;
        if (glide && o->match >= 0) {
            const Op *p = &prev->ops[o->match];
            int dx = o->wx - p->wx, dy = o->wy - p->wy;
            if (dx * dx + dy * dy <= MATCH_PX * MATCH_PX) {
                wx = lerp_px(p->wx, o->wx, alpha);
                wy = lerp_px(p->wy, o->wy, alpha);
            }
        }
        /* text drawn as part of a frame is fixed to the screen: like the game's
         * boxes, it keeps its place in the original window, which widescreen
         * centres in the wide area (see dl_compose_wide) */
        if (o->type == DL_GLYPH || o->type == DL_GLYPH_OPAQUE) { sx = wx + (view_w - WIN_W) / 2; sy = wy + (view_h == WIN_H ? 0 : view_above()); }
        else { sx = wx - dcx; sy = wy - dcy; }
        if (dl_dbg_obj == -9999 && o->group == 0x80) dl_dbg_obj = wx;     /* Duke, world x */
        /* test aid: jump of this object since the last refresh of the previous step */
        if (jumplog && glide && o->match >= 0 && !o->drawn && prev->ops[o->match].drawn &&
            o->type == DL_SPRITE) {
            int jx = sx - prev->ops[o->match].rsx, jy = sy - prev->ops[o->match].rsy;
            if (abs(jx) > 3 || abs(jy) > 3)
                printf("J serial %u op %d group %d jump %d,%d (now %d,%d was %d,%d) alpha %.3f\n", serial, i, o->group, jx, jy, sx, sy,
                       prev->ops[o->match].rsx, prev->ops[o->match].rsy, alpha);
        }
        ((Op *)o)->rsx = (int16_t)sx; ((Op *)o)->rsy = (int16_t)sy; ((Op *)o)->drawn = 1;
        switch (o->type) {
        case DL_SPRITE:       draw_masked(o->src, 2, 16, sx, sy, 0); break;
        case DL_GLYPH:        draw_masked(o->src, 1, 8, sx, sy, 0); break;
        case DL_GLYPH_OPAQUE: draw_masked(o->src, 1, 8, sx, sy, 1); break;
        case DL_TILE:         draw_tile_at(o->tile, sx, sy); break;
        case DL_MIRROR:       mirror_at(sx, sy, o->back); break;
        }
    }
}

/* may the frame glide from the previous one at this alpha? */
static int can_glide(double alpha)
{
    if (!smooth_on() || alpha >= 1.0 || cur->dirty || !prev->valid || prev->dirty) return 0;
    if ((direct || dlgd) && dl_wide) return 0; /* the game drew directly: hold still */
    if (abs(cur->cam_x - prev->cam_x) > SNAP_PX || abs(cur->cam_y - prev->cam_y) > SNAP_PX) return 0;
    return 1;
}

int dl_render(double alpha, uint8_t *idx)
{
    int y;
    if (!smooth_on() || !cur->valid || cur->dirty) return 0;
    if (alpha < 0) alpha = 0;
    view_w = WIN_W; view_h = WIN_H; wide_backdrop = 0;
    if (!can_glide(alpha)) {
        /* show the exact frame - unless it has a mirror reflecting from above
         * the window, which the port draws differently: switching between the
         * two would make the mirror flicker */
        if (!cur->mirror_top || direct || dlgd || cur->dirty) return 0;
        render_view(1.0, 0);
    } else render_view(alpha, 1);
    for (y = 0; y < WIN_H; y++) memcpy(idx + (WIN_Y0 + y) * 320 + WIN_X0, win[y], WIN_W);
    return 1;
}

int dl_render_wide(double alpha, uint8_t *dst, int stride)
{
    int y;
    Frame *f = view_frame();
    if (!dl_active() || !f) return 0;
    if (alpha < 0) alpha = 0;
    view_w = dl_view_w; view_h = dl_view_h; wide_backdrop = 1;
    if (f == cur) render_view(alpha, can_glide(alpha));
    else {                                       /* first frame: no previous one to glide from */
        Frame *save = cur;
        cur = f; render_view(1.0, 0); cur = save;
    }
    for (y = 0; y < view_h; y++) memcpy(dst + y * stride, win[y], (size_t)view_w);
    view_w = WIN_W; view_h = WIN_H; wide_backdrop = 0;
    return f->dirty ? 3 : 1;
}

/* test aid: pieces of one object (consecutive ops of the same group drawn
 * within 16 px of each other) that would glide by different amounts */
int dl_split_pieces(void)
{
    int i, n = 0;
    if (!cur->valid || !prev->valid) return 0;
    for (i = 1; i < cur->nops; i++) {
        const Op *a = &cur->ops[i - 1], *b = &cur->ops[i];
        int adx, ady, bdx, bdy;
        if (a->group != b->group || a->type != DL_SPRITE || b->type != DL_SPRITE) continue;
        if (abs(a->wx - b->wx) > 16 || abs(a->wy - b->wy) > 16) continue;
        if (a->match < 0 || b->match < 0) continue;
        adx = a->wx - prev->ops[a->match].wx; ady = a->wy - prev->ops[a->match].wy;
        bdx = b->wx - prev->ops[b->match].wx; bdy = b->wy - prev->ops[b->match].wy;
        if (adx != bdx || ady != bdy) {
            n++;
            if (jumplog)
                printf("  split g%d ops %d,%d  a w(%d,%d) d(%d,%d) src %p m%d | b w(%d,%d) d(%d,%d) src %p m%d | counts cur/prev in group\n",
                       a->group, i - 1, i, a->wx, a->wy, adx, ady, (const void *)a->src, a->match, b->wx, b->wy, bdx, bdy, (const void *)b->src, b->match);
        }
    }
    return n;
}

/* test aid: camera movement between the last two frames */
int dl_cam_dx(void) { return (cur->valid && prev->valid) ? cur->cam_x - prev->cam_x : 0; }
int dl_cam_dy(void) { return (cur->valid && prev->valid) ? cur->cam_y - prev->cam_y : 0; }
int dl_moving_actors(void)
{
    int i, n = 0;
    if (!cur->valid || !prev->valid) return 0;
    for (i = 0; i < cur->nops; i++) {
        const Op *o = &cur->ops[i];
        if (o->group >= 0x80 || o->match < 0) continue;
        if (o->wx != prev->ops[o->match].wx || o->wy != prev->ops[o->match].wy) n++;
    }
    return n;
}

/* test aid: 2x2 objects (four consecutive sprites of one group laid out
 * TL, TR, BL, BR, like Duke) whose matched pieces in the previous frame do
 * not form the same block in the same order - they would tear while gliding.
 * *checked receives how many matched 2x2 objects were examined. */
int dl_torn_blocks(int *checked)
{
    static const int ox[4] = { 0, 16, 0, 16 }, oy[4] = { 0, 0, 16, 16 };
    int i, k, torn = 0;
    *checked = 0;
    if (!cur->valid || !prev->valid) return 0;
    for (i = 0; i + 3 < cur->nops; i++) {
        const Op *q = &cur->ops[i];
        int ok = 1, all = 1;
        for (k = 0; k < 4 && ok; k++) {
            const Op *o = &cur->ops[i + k];
            if (o->type != DL_SPRITE || o->group != q->group || o->wx - q->wx != ox[k] || o->wy - q->wy != oy[k]) ok = 0;
            if (o->match < 0) all = 0;
        }
        if (!ok || !all) continue;
        (*checked)++;
        {
            const Op *p0 = &prev->ops[q->match];
            for (k = 1; k < 4; k++) {
                const Op *p = &prev->ops[cur->ops[i + k].match];
                if (p->wx - p0->wx != ox[k] || p->wy - p0->wy != oy[k]) { torn++; break; }
            }
        }
        i += 3;
    }
    return torn;
}

/* widescreen picture, w x 200 indices: left border and status panel from the
 * EGA page as they are; top and bottom borders extended by repeating a plain
 * 16-px section of the original (keeping the top border's left end with its
 * indicators, and the F1 banner centred under the play area); the play area
 * from the renderer.  'overlay' lays the original 208 x 160 view over the
 * middle - dialogs and anything else the game drew there itself. */
int port_level_intro;
int dl_wide_shown;                  /* a wide frame was shown since the play screen went up */

/* the Immersive HUD bar; with a play area shorter than the picture
 * (Immersive Alt) the band below it is black (palette slot 0, as the
 * original's own black - it takes part in fades and flashes the same way) */
static void hud_band(uint8_t *out, int w, const uint8_t *base)
{
    if (cfg.hud == HUD_OFF) return;
    if (dl_view_h < 200) memset(out + dl_view_h * w, 0, (size_t)((200 - dl_view_h) * w));
    hud_draw(out, w, base);
}

int dl_compose_wide(double alpha, int overlay, uint8_t *out, int w)
{
    static uint8_t base[320 * 200];
    int wp = dl_immersive ? w : w - 112, x, y, r, intro = 0;
    int px0 = dl_play_x0(), py0 = dl_play_y0(), ph = dl_view_h;
    if (wp != dl_view_w) return 0;
    if (dl_frame_ready()) port_level_intro = 0;          /* the level is drawn: the usual way */
    else if (port_level_intro && dl_active() && dl_wide) intro = 1;
    else return 0;
    ega_present_indexed(base, ega_display_page);
    if (!dl_immersive)                  /* immersive: no border, no status panel */
    for (y = 0; y < 200; y++) {
        uint8_t *o = out + y * w, *b = base + y * 320;
        memcpy(o, b, 16);
        memcpy(o + w - 96, b + 224, 96);
        if (y < 16) {
            for (x = 16; x < 16 + wp; x++) o[x] = b[192 + ((x - 16) & 15)];
            memcpy(o + 16, b + 16, 64);
            memcpy(o + 16 + wp - 16, b + 208, 16);
        } else if (y >= 176) {
            for (x = 16; x < 16 + wp; x++) o[x] = b[32 + ((x - 16) & 15)];
            memcpy(o + 16, b + 16, 16);
            memcpy(o + 16 + wp - 16, b + 208, 16);
            memcpy(o + 16 + wp / 2 - 32, b + 88, 64);
        }
    }
    /* the original window centred in the play area: where screen-fixed things
     * (dialog boxes, their text) keep their exact place relative to it */
    {
        int ocx = px0 + (wp - WIN_W) / 2, ocy = py0 + (dl_immersive ? view_above() : (ph - WIN_H) / 2);
    if (intro) {
        /* before the level's first frame: the play area is still empty
         * (black, as in the original) apart from what the game drew in its
         * window - the "Get ready" box - which goes in the middle */
        for (y = 0; y < ph; y++) memset(out + (py0 + y) * w + px0, 0, (size_t)wp);
        for (y = 0; y < WIN_H; y++)
            memcpy(out + (ocy + y) * w + ocx, base + (WIN_Y0 + y) * 320 + WIN_X0, WIN_W);
        if (dl_immersive) hud_band(out, w, base);     /* (the panel shows during "Get ready" too) */
        return 1;
    }
    r = dl_render_wide(alpha, out + py0 * w + px0, w);
    if (!r) return 0;
    dl_wide_shown = 1;
    /* lay over only what the game drew itself: dialog boxes and text (this
     * frame's, the finished frame's) and direct drawing since the frame -
     * not the whole original view, whose backdrop is unstretched */
    {
        /* Two places: what belongs to the level (sprites the game drew
         * directly) goes where the original view sits over the level; dialog
         * boxes and their text - screen-fixed in the original, which draws them
         * across its play window - go in the original window centred in the
         * play area, so they keep their exact place relative to it. */
        int ow = px0 + dl_view_offset(), owy = py0 + dl_view_offset_y();
        /* (a finished frame's popups are gone from the screen by now: only a
         * popup being shown right now, or direct drawing, is laid over) */
        if (direct) {                                    /* the level's own drawing first */
            for (y = ddy0; y < ddy1; y++)
                memcpy(out + (y + owy) * w + ow + ddx0, base + (y + WIN_Y0) * 320 + WIN_X0 + ddx0, (size_t)(ddx1 - ddx0));
        }
        if (overlay && dl_capturing && rec->dirty) {
            for (y = rec->dy0; y < rec->dy1; y++)
                memcpy(out + (y + ocy) * w + ocx + rec->dx0, base + (y + WIN_Y0) * 320 + WIN_X0 + rec->dx0, (size_t)(rec->dx1 - rec->dx0));
        }
        if (dlgd) {
            for (y = dgy0; y < dgy1; y++)
                memcpy(out + (y + ocy) * w + ocx + dgx0, base + (y + WIN_Y0) * 320 + WIN_X0 + dgx0, (size_t)(dgx1 - dgx0));
        }
    }
    }
    if (dl_immersive) hud_band(out, w, base);         /* the Immersive HUD bar (port_hud.c) */
    return 1;
}
