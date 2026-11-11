/*
 * port_dir.c - the files in a folder (port addition: the CUSTOM LEVEL list).
 * Kept apart so <windows.h> stays out of the rest of the game.
 */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* up to max file names (not folders) in dir; the number found */
int port_list_files(const char *dir, char (*names)[128], int max)
{
    int n = 0;
#ifdef _WIN32
    char pat[1100];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (strlen(fd.cFileName) >= 128) continue;
        strcpy(names[n++], fd.cFileName);
    } while (n < max && FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return 0;
    while (n < max && (e = readdir(d)) != NULL) {
        char full[1400];
        struct stat st;
        if (e->d_name[0] == '.' || strlen(e->d_name) >= 128) continue;
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        strcpy(names[n++], e->d_name);
    }
    closedir(d);
#endif
    return n;
}
