/*
 * filedlg.cpp - native Open / Save As / folder dialogs.
 *
 * Windows: the system's own dialogs (comdlg32).  Elsewhere: zenity, if it is
 * installed (most Linux desktops have it); without it the dialogs return
 * nothing and Shrapnel says so.
 */
#include "shrapnel.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

static std::string narrow(const wchar_t *w)
{
    char b[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b, NULL, NULL);
    return n > 0 ? std::string(b) : std::string();
}
static std::wstring widen(const std::string &s)
{
    wchar_t b[4096];
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, b, 4096);
    return n > 0 ? std::wstring(b) : std::wstring();
}

static std::string run(bool save, const std::string &dir, const std::string &name,
                       const wchar_t *filter = NULL, const wchar_t *def_ext = NULL)
{
    wchar_t file[4096] = L"";
    std::wstring wdir = widen(dir), wname = widen(name);
    if (save) wcsncpy(file, wname.c_str(), 4095);
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.lpstrFilter = filter ? filter : L"Duke Nukem maps (*.dn1;*.dn2;*.dn3)\0*.dn1;*.dn2;*.dn3\0All files\0*.*\0";
    o.lpstrDefExt = def_ext;
    o.lpstrFile = file;
    o.nMaxFile = 4096;
    o.lpstrInitialDir = wdir.empty() ? NULL : wdir.c_str();
    o.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
    return ok ? narrow(file) : std::string();
}

std::string dialog_open_project(const std::string &dir, bool episodes_only)
{
    return run(false, dir, "", episodes_only ? L"Techbot episodes (*.epi)\0*.epi\0All files\0*.*\0"
                                             : L"Techbot levels and episodes (*.lev;*.epi)\0*.lev;*.epi\0All files\0*.*\0");
}
std::string dialog_save_project(const std::string &dir, const std::string &name, bool episode)
{
    return run(true, dir, name, episode ? L"Techbot episode (*.epi)\0*.epi\0" : L"Techbot level (*.lev)\0*.lev\0",
               episode ? L"epi" : L"lev");
}

std::string dialog_open_picture(const std::string &dir)
{
    return run(false, dir, "", L"PNG pictures (*.png)\0*.png\0All files\0*.*\0");
}

static int CALLBACK start_in(HWND h, UINT msg, LPARAM, LPARAM data)
{
    if (msg == BFFM_INITIALIZED && data) SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}
std::string dialog_folder(const std::string &dir, const char *title)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    std::wstring wdir = widen(dir), wtitle = widen(title);
    BROWSEINFOW b;
    memset(&b, 0, sizeof b);
    b.lpszTitle = wtitle.c_str();
    b.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    b.lpfn = start_in;
    b.lParam = (LPARAM)wdir.c_str();
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&b);
    if (!id) return "";
    wchar_t path[MAX_PATH] = L"";
    BOOL ok = SHGetPathFromIDListW(id, path);
    CoTaskMemFree(id);
    return ok ? narrow(path) : std::string();
}
#else
#include <strings.h>
static std::string run(bool save, const std::string &dir, const std::string &name,
                       const char *title = NULL, const char *filter = NULL, bool folder = false)
{
    std::string cmd = "zenity --file-selection --title=\"";
    cmd += title ? title : save ? "Save map as" : "Open map";
    cmd += "\"";
    if (folder) cmd += " --directory";
    else cmd += std::string(" --file-filter=\"") + (filter ? filter : "Duke Nukem maps | *.dn1 *.DN1 *.dn2 *.DN2 *.dn3 *.DN3") +
                "\" --file-filter=\"All files | *\"";
    if (save) cmd += " --save --confirm-overwrite";
    cmd += " --filename=\"" + dir + "/" + name + "\" 2>/dev/null";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char b[4096] = "";
    if (!fgets(b, sizeof b, p)) b[0] = 0;
    pclose(p);
    size_t n = strlen(b);
    while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0;
    return b;
}

std::string dialog_open_project(const std::string &dir, bool episodes_only)
{
    return run(false, dir, "", "Open a project", episodes_only ? "Techbot episodes | *.epi *.EPI" : "Techbot levels and episodes | *.lev *.LEV *.epi *.EPI");
}
std::string dialog_save_project(const std::string &dir, const std::string &name, bool episode)
{
    std::string p = run(true, dir, name, "Save the project as", episode ? "Techbot episode | *.epi *.EPI" : "Techbot level | *.lev *.LEV");
    std::string want = episode ? ".epi" : ".lev";
    if (!p.empty() && (p.size() < 4 || strcasecmp(p.c_str() + p.size() - 4, want.c_str()) != 0)) p += want;
    return p;
}
std::string dialog_folder(const std::string &dir, const char *title) { return run(false, dir, "", title, NULL, true); }
std::string dialog_open_picture(const std::string &dir)
{
    return run(false, dir, "", "Choose a picture", "PNG pictures | *.png *.PNG");
}
#endif

std::string dialog_open_map(const std::string &dir) { return run(false, dir, ""); }
std::string dialog_save_map(const std::string &dir, const std::string &name) { return run(true, dir, name); }

/* ---------------------------------------------------------------- another program
 * Starts a program (Techbot) on its own, not waiting for it. */
#ifdef _WIN32
bool launch_program(const std::vector<std::string> &args, std::string &err)
{
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); i++) {
        if (i) cmd += L' ';
        cmd += L'"' + widen(args[i]) + L'"';
    }
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        err = "Windows couldn't start " + args[0];
        return false;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#else
#include <unistd.h>
#include <sys/wait.h>
bool launch_program(const std::vector<std::string> &args, std::string &err)
{
    if (access(args[0].c_str(), X_OK) != 0) { err = "Can't run " + args[0]; return false; }
    pid_t p = fork();
    if (p < 0) { err = "Couldn't start " + args[0]; return false; }
    if (p == 0) {                       /* a grandchild, so nothing waits on it */
        if (fork() == 0) {
            std::vector<char *> a;
            for (const std::string &s : args) a.push_back(const_cast<char *>(s.c_str()));
            a.push_back(NULL);
            setsid();
            execv(a[0], a.data());
        }
        _exit(0);
    }
    waitpid(p, NULL, 0);
    return true;
}
#endif
