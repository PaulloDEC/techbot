/*
 * globals.c - definitions of all game globals + a table mapping each one to
 * its offset in the original data segment (used by the test harness to diff
 * the port against the original, frame by frame).
 */
#include "dn1.h"

#define G(type, name, off)      type name;
#define GA(type, name, off, n)  type name[n];
#include "globals.def"
#undef G
#undef GA

typedef struct { const char *name; void *addr; unsigned ds_off; unsigned size; } StateVar;

#define G(type, name, off)      { #name, (void *)&name, off, (unsigned)sizeof(type) },
#define GA(type, name, off, n)  { #name, (void *)name, off, (unsigned)(sizeof(type) * (n)) },
const StateVar state_map[] = {
#include "globals.def"
    { 0, 0, 0, 0 }
};
#undef G
#undef GA

/* initialised data: these globals start with values from the EXE's data
 * segment (the uninitialised BSS starts at DS:4ECA) */
void init_globals_from_exe(void)
{
    const StateVar *v;
    unsigned char *p;
    unsigned i;
    for (v = state_map; v->name; v++) {
        if (v->ds_off >= 0x4ECA) continue;
        p = (unsigned char *)v->addr;
        for (i = 0; i < v->size; i++) p[i] = ds_u8((uint16_t)(v->ds_off + i));
    }
}
