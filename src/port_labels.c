/*
 * port_labels.c - name the controller's buttons in on-screen prompts.
 *
 * The game's text is read from DN1.EXE at runtime.  When the last input came
 * from a controller, a few known lines are rewritten just before drawing:
 * the key word is swapped for the button that does the same job in menus
 * (south button = confirm/Enter/Y, east button = back/N), and a centred line
 * is re-centred within its original width so the dialog keeps its layout.
 * Only the drawn copy changes.  Lines are selected by their offset in the
 * data segment and only rewritten if the expected key word is really there.
 */
#include "dn1.h"
#include "port.h"
#include <string.h>
#include <ctype.h>

volatile int input_device = DEV_KEYBOARD;
int pad_style = PAD_STYLE_XBOX;
int ds_offset_of(const char *p);

enum { LBL_CONFIRM, LBL_YESNO };
/* "press ENTER" prompts: hints, saved / not saved / not available, the level
 * notes and "you died"; and the yes/no letters of the restart question.
 * (Not included: instructions and keys-help pages, which describe the
 * keyboard on purpose, and the high-score name entry, which needs typing.) */
static const struct { uint16_t off; uint8_t kind; } lines[] = {
    {0x2D25, LBL_CONFIRM}, {0x2D89, LBL_CONFIRM}, {0x2DED, LBL_CONFIRM}, {0x2E51, LBL_CONFIRM},
    {0x2EB5, LBL_CONFIRM}, {0x2F19, LBL_CONFIRM}, {0x31A0, LBL_CONFIRM}, {0x3204, LBL_CONFIRM},
    {0x3268, LBL_CONFIRM}, {0x32CB, LBL_CONFIRM}, {0x332E, LBL_CONFIRM}, {0x3392, LBL_CONFIRM},
    {0x33F6, LBL_CONFIRM}, {0x34E7, LBL_CONFIRM}, {0x354B, LBL_CONFIRM}, {0x4479, LBL_CONFIRM},
    {0x08AE, LBL_CONFIRM}, {0x313C, LBL_YESNO},
};

static const char *confirm_name(void)
{
    switch (pad_style) {
    case PAD_STYLE_PLAYSTATION: return "CROSS";
    case PAD_STYLE_NINTENDO:    return "B";       /* the south button is labelled B */
    default:                    return "A";
    }
}
static const char *yesno_name(void)
{
    switch (pad_style) {
    case PAD_STYLE_PLAYSTATION: return "X/O";
    case PAD_STYLE_NINTENDO:    return "B/A";
    default:                    return "A/B";
    }
}

/* case-insensitive search */
static const char *find(const char *s, const char *w)
{
    size_t n = strlen(w);
    for (; *s; s++) {
        size_t i;
        i = 0;
        while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)w[i])) i++;
        if (i == n) return s;
    }
    return NULL;
}

/* port wording that applies whatever the input device: there is no DOS to
 * quit to any more (main menu, quit option) */
static const struct { uint16_t off; const char *from, *to; } wording[] = {
    {0x42A9, "to DOS", "Game"},
};

static const char *swap_word(const char *s, const char *word, const char *repl, int keep_case);

const char *port_relabel(const char *s)
{
    const char *word, *repl;
    int off, i;
    if ((off = ds_offset_of(s)) < 0) return s;
    for (i = 0; i < (int)(sizeof wording / sizeof wording[0]); i++)
        if (wording[i].off == off) return swap_word(s, wording[i].from, wording[i].to, 1);
    if (input_device != DEV_PAD) return s;
    for (i = 0; i < (int)(sizeof lines / sizeof lines[0]); i++) if (lines[i].off == off) break;
    if (i == (int)(sizeof lines / sizeof lines[0])) return s;
    word = lines[i].kind == LBL_YESNO ? "Y/N" : "ENTER";
    repl = lines[i].kind == LBL_YESNO ? yesno_name() : confirm_name();
    return swap_word(s, word, repl, 0);
}

/* swap 'word' (found case-insensitively) for 'repl' in a drawn copy of s,
 * keeping the original width: a centred line is re-centred, otherwise the
 * indent is kept.  Button names follow the case of the word they replace;
 * with keep_case the replacement is used exactly as written. */
static const char *swap_word(const char *s, const char *word, const char *repl, int keep_case)
{
    static char out[96];
    char tmp[96];
    const char *at;
    int i, len, lead, trail, n, k;
    len = (int)strlen(s);
    if (len >= (int)sizeof tmp - 8 || !(at = find(s, word))) return s;
    /* swap the word (upper-case in the original lines; follow its case) */
    n = (int)(at - s);
    memcpy(tmp, s, (size_t)n);
    for (k = 0; repl[k]; k++)
        tmp[n + k] = keep_case || isupper((unsigned char)at[0]) ? repl[k] : (char)tolower((unsigned char)repl[k]);
    strcpy(tmp + n + k, at + strlen(word));
    /* keep the original width: re-centre a centred line, else keep its indent */
    lead = 0;
    while (s[lead] == ' ') lead++;
    trail = 0;
    while (trail < len && s[len - 1 - trail] == ' ') trail++;
    {
        const char *body = tmp;
        int blen;
        while (*body == ' ') body++;
        blen = (int)strlen(body);
        while (blen > 0 && body[blen - 1] == ' ') blen--;
        if (blen > len) return s;                       /* would not fit: leave it */
        if (lead > 0 && (lead - trail <= 1 && trail - lead <= 1)) lead = (len - blen) / 2;
        else if (lead + blen > len) lead = len - blen;
        memset(out, ' ', (size_t)len);
        memcpy(out + lead, body, (size_t)blen);
        out[len] = 0;
    }
    return out;
}
