/*
 * port_log.c - TECHBOT.LOG in the game folder: what happened at startup
 * (versions, display, audio, controller, episode, settings), any errors, and -
 * if the program crashes - what and where (see pf_sdl.c).  Each launch writes
 * a fresh log; the previous one is kept as TECHBOT-PREVIOUS.LOG, so a crash on
 * launch is still there after starting again.
 */
#include "dn1.h"
#include "port.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static FILE *log_fp;
static char log_name[1024];
const char *volatile port_step = "starting";     /* last thing begun (crash reports) */

void port_log_open(void)
{
    char prev[1024];
    time_t t = time(NULL);
    snprintf(log_name, sizeof log_name, "%s", game_path("TECHBOT.LOG"));
    snprintf(prev, sizeof prev, "%s", game_path("TECHBOT-PREVIOUS.LOG"));
    remove(prev);
    rename(log_name, prev);
    log_fp = fopen(log_name, "w");
    if (!log_fp) return;
    fprintf(log_fp, "%s\n", PORT_FULL_NAME);
    fprintf(log_fp, "build %s %s, started %s", __DATE__, __TIME__, ctime(&t));
    fflush(log_fp);
}

void port_log(const char *fmt, ...)
{
    va_list ap;
    if (!log_fp) return;
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);
    va_end(ap);
    fputc('\n', log_fp);
    fflush(log_fp);                    /* every line reaches the disk: a crash loses nothing */
}

void port_log_step(const char *what)
{
    port_step = what;
    port_log("- %s", what);
}

const char *port_log_path(void) { return log_name; }
FILE *port_log_file(void) { return log_fp; }
