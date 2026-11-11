/*
 * pf_sdl_embed.c - Techbot's own SDL platform layer (src/pf_sdl.c), built into
 * Shrapnel: play-testing runs with exactly the game's rendering, sound,
 * input, timing and display settings, in the editor's window.  See the
 * PF_SDL_EMBED parts of pf_sdl.c.
 */
#define PF_SDL_EMBED 1
#include "../../src/pf_sdl.c"
