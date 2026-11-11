/*
 * port_pack.c - Techbot plays levels and episodes made in Shrapnel (port
 * addition).
 *
 * A pack is a zip (.LEV: one level, .EPI: an episode) holding PACK.INI and
 * the maps (see BUILDING.md, "Projects and saving").  Opened, it stands in
 * for the episode's level table: level_index counts its entries in order
 * (level, hallway, level... - levels even, hallways odd, as in the game), the
 * maps, backdrops and note text come from it, and the last level's exit ends
 * the episode, with the ending PACK.INI names.  The art, sounds and
 * everything else stay the episode's own (PACK.INI's "episode" says which);
 * saves and high scores are kept apart from the stock episode's.
 *
 * Packs are Techbot-only: nothing here runs unless a pack has been opened.
 */
#include "dn1.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

Pack pack;

/* a copy that fits (cut short if need be) */
static void scopy(char *d, size_t n, const char *s)
{
    size_t k = strlen(s);
    if (k >= n) k = n - 1;
    memcpy(d, s, k);
    d[k] = 0;
}

static char *dupmem(const void *p, size_t n)
{
    char *d = (char *)malloc(n + 1);
    if (d) { memcpy(d, p, n); d[n] = 0; }
    return d;
}

void pack_close(void)
{
    int i, k;
    for (i = 0; i < pack.n; i++) {
        free(pack.e[i].map);
        for (k = 0; k < 2; k++) free(pack.e[i].drop_data[k]);
    }
    free(pack.e);
    memset(&pack, 0, sizeof pack);
}

/* a file in the zip, case-insensitively: malloc'd (NUL after), or NULL */
static char *zip_file(mz_zip_archive *z, const char *name, size_t *len)
{
    int i, n = (int)mz_zip_reader_get_num_files(z);
    for (i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(z, (mz_uint)i, &st)) continue;
        if (strcasecmp_port(st.m_filename, name) == 0) {
            size_t sz = 0;
            void *p = mz_zip_reader_extract_to_heap(z, (mz_uint)i, &sz, 0);
            char *d;
            if (!p) return NULL;
            d = dupmem(p, sz);
            mz_free(p);
            if (len) *len = sz;
            return d;
        }
    }
    return NULL;
}

int strcasecmp_port(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void trim(char *s)
{
    char *a = s, *e;
    while (*a == ' ' || *a == '\t') a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
}

/* "a|b|c": the parts into lines (at most n, each at most w chars) */
static int split_lines(const char *s, char (*lines)[PACK_NOTE_W + 1], int n)
{
    int k = 0, c = 0;
    if (!*s) return 0;
    memset(lines, 0, sizeof *lines * (size_t)n);
    for (; *s && k < n; s++) {
        if (*s == '|') { k++; c = 0; continue; }
        if (c < PACK_NOTE_W) lines[k][c++] = *s;
    }
    return k + 1 > n ? n : k + 1;
}

int pack_open(const char *path, char *err, int errlen)
{
    mz_zip_archive z;
    char *ini, *line, *next;
    size_t len;
    int cap = 0, cur = -1, top = 1;
    pack_close();
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, path, 0)) {
        snprintf(err, (size_t)errlen, "Can't open %s (not a .LEV or .EPI file?)", path);
        return 0;
    }
    ini = zip_file(&z, "PACK.INI", &len);
    if (!ini) {
        mz_zip_reader_end(&z);
        snprintf(err, (size_t)errlen, "%s has no PACK.INI: not made by Shrapnel?", path);
        return 0;
    }
    scopy(pack.path, sizeof pack.path, path);
    {   /* the file's name without folder or extension: names its saves */
        const char *b = path + strlen(path);
        char *dot;
        while (b > path && b[-1] != '/' && b[-1] != '\\') b--;
        scopy(pack.stem, sizeof pack.stem, b);
        dot = strrchr(pack.stem, '.');
        if (dot) *dot = 0;
    }
    pack.episode = 1;
    pack.ending = 0;
    for (line = ini; line && *line; line = next) {
        char *eq, key[64], val[1100];
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        trim(line);
        if (!*line || *line == ';' || *line == '#') continue;
        if (*line == '[') {               /* [level N] / [hallway N]: the next entry */
            if (cur + 1 >= cap) {
                int nc = cap ? cap * 2 : 32;
                PackEntry *ne = (PackEntry *)realloc(pack.e, sizeof *ne * (size_t)nc);
                if (!ne) break;
                memset(ne + cap, 0, sizeof *ne * (size_t)(nc - cap));
                pack.e = ne; cap = nc;
            }
            cur++;
            pack.e[cur].hallway = strncasecmp_port(line + 1, "hallway", 7) == 0;
            pack.e[cur].backdrop = 1;
            top = 0;
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        scopy(key, sizeof key, line);
        scopy(val, sizeof val, eq + 1);
        trim(key); trim(val);
        for (char *p = key; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (top) {
            if (!strcmp(key, "name")) scopy(pack.name, sizeof pack.name, val);
            else if (!strcmp(key, "author")) scopy(pack.author, sizeof pack.author, val);
            else if (!strcmp(key, "episode")) pack.episode = atoi(val);
            else if (!strcmp(key, "target")) pack.classic = !strcasecmp_port(val, "classic");
            else if (!strcmp(key, "ending")) pack.ending = isdigit((unsigned char)val[0]) ? atoi(val) : 0;
            else if (!strcmp(key, "ending_text")) {
                char *p;
                scopy(pack.ending_text, sizeof pack.ending_text, val);
                for (p = pack.ending_text; *p; p++) if (*p == '|') *p = '\n';
            }
        } else if (cur >= 0) {
            PackEntry *e = &pack.e[cur];
            if (!strcmp(key, "map")) {
                char *m = zip_file(&z, val, &len);
                if (m && len >= 0x5A00) e->map = (uint8_t *)m;
                else free(m);
                scopy(e->map_name, sizeof e->map_name, val);
            } else if (!strcmp(key, "backdrop")) e->backdrop = atoi(val);
            else if (!strcmp(key, "backdrop_a") || !strcmp(key, "backdrop_b")) {
                int k = key[9] == 'b';
                /* a picture in the pack, else a backdrop file of the game's ("DROP5.DN2") */
                size_t dl = 0;
                char *d = zip_file(&z, val, &dl);
                scopy(e->drop[k], sizeof e->drop[k], val);
                if (d) { e->drop_data[k] = (uint8_t *)d; e->drop_len[k] = (long)dl; }
            } else if (!strcmp(key, "note")) e->has_note = split_lines(val, e->note, 4) > 0;
        }
    }
    free(ini);
    mz_zip_reader_end(&z);
    pack.n = cur + 1;
    if (pack.episode < 1 || pack.episode > 3) pack.episode = 1;
    {   /* every entry needs its map; levels and hallways take turns */
        int i;
        for (i = 0; i < pack.n; i++) {
            if (!pack.e[i].map) {
                snprintf(err, (size_t)errlen, "%s: the map of entry %d (%s) is missing", path, i + 1, pack.e[i].map_name);
                pack_close();
                return 0;
            }
            if (pack.e[i].hallway != (i & 1)) {
                snprintf(err, (size_t)errlen, "%s: levels and hallways must take turns (entry %d)", path, i + 1);
                pack_close();
                return 0;
            }
        }
    }
    if (pack.n == 0 || pack.e[pack.n - 1].hallway) {
        snprintf(err, (size_t)errlen, "%s holds no levels, or ends with a hallway", path);
        pack_close();
        return 0;
    }
    if (!pack.name[0]) scopy(pack.name, sizeof pack.name, pack.stem);
    pack.on = 1;
    port_log("pack: %s (%s), episode %d art, %d entries", pack.name, pack.path, pack.episode, pack.n);
    return 1;
}

int strncasecmp_port(const char *a, const char *b, int n)
{
    for (; n > 0; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}

const uint8_t *pack_map(int index)
{
    return pack.on && index >= 0 && index < pack.n ? pack.e[index].map : NULL;
}

int pack_is_last(int index) { return pack.on && index >= pack.n - 1; }

/* a level's backdrop: which = 0 (A) or 1 (B).  The file name (for the
 * replacement-art lookup too), and the pack's own picture when it has one. */
const char *pack_backdrop(int index, int which, const uint8_t **data, long *len)
{
    PackEntry *e;
    if (data) *data = NULL;
    if (len) *len = 0;
    if (!pack.on || index < 0 || index >= pack.n) return NULL;
    index &= ~1;                          /* (a hallway keeps its level's) */
    e = &pack.e[index];
    if (e->drop_data[which]) {
        if (data) *data = e->drop_data[which];
        if (len) *len = e->drop_len[which];
        return e->drop[which];
    }
    if (e->drop[which][0]) return e->drop[which];
    {   /* a stock level's pair: that level's entries in the episode's table */
        int b = e->backdrop >= 1 && e->backdrop <= 10 ? e->backdrop : 1;
        return ds_farstr((uint16_t)(0x0B47 + ((b - 1) * 2 + which) * 4));
    }
}

const char (*pack_note(int index))[PACK_NOTE_W + 1]
{
    if (!pack.on || index < 0 || index >= pack.n || !pack.e[index].has_note) return NULL;
    return pack.e[index].note;
}

/* a save or high-score file of the stock episode's, renamed for the pack:
 * "SAVED1.DN1" -> "MYPACK.SV1" style names would clash between packs with
 * long names, so: "<stem>-SAVED1.DN1" */
const char *pack_file_name(const char *stock)
{
    static char b[2][160];
    static int t;
    if (!pack.on || !stock) return stock;
    t = !t;
    snprintf(b[t], sizeof b[t], "%s-%s", pack.stem, stock);
    return b[t];
}

/* ---------------------------------------------------------------- the list
 * The packs in the game folder's LEVELS folder, for the CUSTOM LEVEL menu:
 * just their PACK.INI's name, author, episode and how many levels. */
int port_list_files(const char *dir, char (*names)[128], int max);   /* port_dir.c */

static int ends_with(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a > b && strcasecmp_port(s + a - b, ext) == 0;
}

int pack_list(PackInfo *out, int max)
{
    static char names[256][128];
    char dir[1100];
    int n, i, k = 0;
    scopy(dir, sizeof dir, game_path(PACK_FOLDER));
    n = port_list_files(dir, names, 256);
    for (i = 0; i < n && k < max; i++) {
        mz_zip_archive z;
        char *ini, *line, *next;
        PackInfo *p = &out[k];
        if (!ends_with(names[i], ".lev") && !ends_with(names[i], ".epi")) continue;
        memset(p, 0, sizeof *p);
        snprintf(p->path, sizeof p->path, "%s/%s", dir, names[i]);
        memset(&z, 0, sizeof z);
        if (!mz_zip_reader_init_file(&z, p->path, 0)) continue;
        ini = zip_file(&z, "PACK.INI", NULL);
        mz_zip_reader_end(&z);
        if (!ini) continue;
        p->episode = 1;
        scopy(p->name, sizeof p->name, names[i]);
        for (line = ini; line && *line; line = next) {
            char *eq;
            next = strchr(line, '\n');
            if (next) *next++ = 0;
            trim(line);
            if (*line == '[') { if (strncasecmp_port(line + 1, "level", 5) == 0) p->levels++; continue; }
            if (!(eq = strchr(line, '='))) continue;
            *eq = 0;
            trim(line); trim(eq + 1);
            if (!strcasecmp_port(line, "name") && eq[1]) scopy(p->name, sizeof p->name, eq + 1);
            else if (!strcasecmp_port(line, "author")) scopy(p->author, sizeof p->author, eq + 1);
            else if (!strcasecmp_port(line, "episode")) p->episode = atoi(eq + 1);
        }
        free(ini);
        if (p->episode < 1 || p->episode > 3) p->episode = 1;
        k++;
    }
    /* by name */
    for (i = 1; i < k; i++) {
        PackInfo t = out[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp_port(out[j].name, t.name) > 0) { out[j + 1] = out[j]; j--; }
        out[j + 1] = t;
    }
    return k;
}
