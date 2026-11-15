/* pc_cf_setup.c - the City Folk files: checked at startup, made on the first start. See pc_cf_data.h.
 *
 * Release builds carry the converter in cf-tools/ (pc/tools/*.py and a Windows Python, from
 * ./ac-plus.sh package). When cf_sky/ is missing or from an older version, the game runs
 * cf-tools/extract_cf_sky.py --auto on the City Folk disc the player put in rom/, with a progress bar,
 * then starts. Without cf-tools/ (a build from source) it says how to run the conversion instead. */
#include <SDL3/SDL.h>
#include "pc_platform.h"
#include "pc_cf_data.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

extern SDL_Window* g_pc_window;

#ifdef _WIN32
#define PC_CF_SETUP_PYTHON "cf-tools\\python\\python.exe"
#define PC_CF_SETUP_SCRIPT "cf-tools\\extract_cf_sky.py"
#else
/* Linux and macOS: the system's Python 3 (with numpy and Pillow), or one shipped in cf-tools/python */
#define PC_CF_SETUP_PYTHON_SHIPPED "cf-tools/python/bin/python3"
#define PC_CF_SETUP_SCRIPT "cf-tools/extract_cf_sky.py"
#endif

static int pc_cf_setup_version(void) {
    FILE* f = fopen("cf_sky/version.txt", "r");
    int version = 0;

    if (f != NULL) {
        if (fscanf(f, "%d", &version) != 1) {
            version = 0;
        }
        fclose(f);
    }
    return version;
}

static int pc_cf_setup_file_exists(const char* path) {
    FILE* f = fopen(path, "rb");

    if (f == NULL) {
        return FALSE;
    }
    fclose(f);
    return TRUE;
}

static void pc_cf_setup_message(Uint32 flags, const char* title, const char* msg) {
    fprintf(stderr, "[CFSetup] %s\n", msg);
    SDL_ShowSimpleMessageBox(flags, title, msg, g_pc_window);
}

/* A bar across the middle of the window, pct of it filled (pc_acplus_host.cpp) */
int pc_acplus_progress(const char* what, int pct);
static void pc_cf_setup_draw(int pct) {
    pc_acplus_progress("Converting your City Folk disc (only the first time)", pct);
}

#ifdef _WIN32
/* Runs the converter; FALSE with its last words in err if it fails */
static int pc_cf_setup_run(char* err, size_t err_size) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL;
    char cmd[512], line[512];
    size_t line_len = 0;
    DWORD code = 1;
    int pct = 0;

    err[0] = '\0';
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        snprintf(err, err_size, "couldn't start the converter");
        return FALSE;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    snprintf(cmd, sizeof(cmd), "\"%s\" -u \"%s\" --auto .", PC_CF_SETUP_PYTHON, PC_CF_SETUP_SCRIPT);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        snprintf(err, err_size, "couldn't start the converter (%s)", PC_CF_SETUP_PYTHON);
        return FALSE;
    }
    CloseHandle(wr);
    pc_cf_setup_draw(0);
    for (;;) {
        DWORD avail = 0, got = 0, i;
        char buf[256];

        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) {
            break; /* the converter has finished and everything it said is read */
        }
        if (avail == 0) {
            WaitForSingleObject(pi.hProcess, 30);
            pc_cf_setup_draw(pct);
            continue;
        }
        if (!ReadFile(rd, buf, avail < sizeof(buf) ? avail : sizeof(buf), &got, NULL)) {
            break;
        }
        for (i = 0; i < got; i++) {
            char c = buf[i];

            if (c == '\r') {
                continue;
            }
            if (c != '\n' && line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
                continue;
            }
            line[line_len] = '\0';
            line_len = 0;
            if (strncmp(line, "PROGRESS ", 9) == 0) {
                pct = atoi(line + 9);
                pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
            } else if (line[0] != '\0' && strncmp(line, "wrote ", 6) != 0) {
                snprintf(err, err_size, "%s", line); /* the last thing it said that wasn't progress */
            }
        }
        pc_cf_setup_draw(pct);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    SDL_SetWindowTitle(g_pc_window, PC_WINDOW_TITLE);
    return code == 0;
}
#else
/* Runs the converter; FALSE with its last words in err if it fails */
static int pc_cf_setup_run(char* err, size_t err_size) {
    char cmd[512];
    char line[512];
    FILE* p;
    int pct = 0;

    err[0] = '\0';
    snprintf(cmd, sizeof(cmd), "\"%s\" -u \"%s\" --auto . 2>&1",
             pc_cf_setup_file_exists(PC_CF_SETUP_PYTHON_SHIPPED) ? PC_CF_SETUP_PYTHON_SHIPPED : "python3",
             PC_CF_SETUP_SCRIPT);
    p = popen(cmd, "r");
    if (p == NULL) {
        snprintf(err, err_size, "couldn't start the converter (python3)");
        return FALSE;
    }
    pc_cf_setup_draw(0);
    while (fgets(line, sizeof(line), p) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "PROGRESS ", 9) == 0) {
            pct = atoi(line + 9);
            pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        } else if (line[0] != '\0' && strncmp(line, "wrote ", 6) != 0) {
            snprintf(err, err_size, "%s", line); /* the last thing it said that wasn't progress */
        }
        pc_cf_setup_draw(pct);
    }
    SDL_SetWindowTitle(g_pc_window, PC_WINDOW_TITLE);
    return pclose(p) == 0;
}
#endif

int pc_cf_setup(void) {
    int version = pc_cf_setup_version();
    char err[512];

    if (version == PC_CF_DATA_VERSION) {
        return TRUE;
    }
#ifdef ACPLUS_DESKTOP /* (elsewhere: cf_sky/ converted on a computer and copied over; docs/PORTING.md) */
#ifdef _WIN32
    if (pc_cf_setup_file_exists(PC_CF_SETUP_PYTHON) && pc_cf_setup_file_exists(PC_CF_SETUP_SCRIPT)) {
#else
    if (pc_cf_setup_file_exists(PC_CF_SETUP_SCRIPT)) {
#endif
        if (getenv("PC_CF_SETUP_QUIET") == NULL) { /* dev: tests can't click OK */
            pc_cf_setup_message(SDL_MESSAGEBOX_INFORMATION, "ac-plus - first start",
                                version == 0
                                    ? "Welcome to ac-plus!\n\n"
                                      "Before the first start, ac-plus converts what it uses from your City Folk "
                                      "disc (it should be in the \"rom\" folder, next to your GameCube disc).\n\n"
                                      "This takes about a minute and only happens once."
                                    : "This version of ac-plus needs your City Folk disc converted again.\n\n"
                                      "This takes about a minute.");
        }
        if (pc_cf_setup_run(err, sizeof(err)) && pc_cf_setup_version() == PC_CF_DATA_VERSION) {
            return TRUE;
        }
        {
            char msg[768];

            snprintf(msg, sizeof(msg),
                     "Your City Folk disc couldn't be converted:\n\n%s\n\nSee \"Setting up\" in README.md.",
                     err[0] != '\0' ? err : "(no reason given)");
            pc_cf_setup_message(SDL_MESSAGEBOX_ERROR, "ac-plus - City Folk disc", msg);
        }
        return FALSE;
    }
#endif /* ACPLUS_DESKTOP */
    pc_cf_setup_message(SDL_MESSAGEBOX_ERROR, "ac-plus - City Folk files needed",
                        version == 0 ? "City Folk files not found.\n\n"
                                       "ac-plus also needs your own copy of Animal Crossing: City Folk (Wii, USA).\n"
                                       "Convert it once with:\n\n"
                                       "    ./ac-plus.sh extract-cf <your City Folk disc image>\n\n"
                                       "See \"Setting up\" in README.md."
                                     : "The City Folk files are from an older version of ac-plus.\n\n"
                                       "Convert your City Folk disc again with:\n\n"
                                       "    ./ac-plus.sh extract-cf <your City Folk disc image>\n\n"
                                       "See \"Setting up\" in README.md.");
    return FALSE;
}
