/* pc_cf_setup.c - the City Folk files: checked at startup, made on the first start. See pc_cf_data.h.
 *
 * Release builds carry the converter in cf-tools/ (pc/tools/*.py and a Windows Python, from
 * ./ac-plus.sh package). When cf_sky/ is missing or from an older version, the game runs
 * cf-tools/extract_cf_sky.py --auto on the City Folk disc the player put in rom/, with a progress bar,
 * then starts. Without cf-tools/ (a build from source) it says how to run the conversion instead. */
#include "pc_platform.h"
#include "pc_cf_data.h"

#ifdef _WIN32
#include <windows.h>
#endif

extern SDL_Window* g_pc_window;

#define PC_CF_SETUP_PYTHON "cf-tools\\python\\python.exe"
#define PC_CF_SETUP_SCRIPT "cf-tools\\extract_cf_sky.py"

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

/* A bar across the middle of the window, pct of it filled */
static void pc_cf_setup_draw(int pct) {
    int w = 0, h = 0, bw, bh, x, y;
    char title[96];

    SDL_GL_GetDrawableSize(g_pc_window, &w, &h);
    bw = w * 3 / 5, bh = h / 30 > 6 ? h / 30 : 6;
    x = (w - bw) / 2, y = (h - bh) / 2;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.13f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x - 2, y - 2, bw + 4, bh + 4);
    glClearColor(0.85f, 0.80f, 0.62f, 1.0f); /* the frame */
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(x, y, bw, bh);
    glClearColor(0.18f, 0.22f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(x, y, bw * pct / 100, bh);
    glClearColor(0.45f, 0.75f, 0.35f, 1.0f); /* leaf green */
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(g_pc_window);
    snprintf(title, sizeof(title), "ac-plus - converting your City Folk disc: %d%%", pct);
    SDL_SetWindowTitle(g_pc_window, title);
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
        SDL_Event ev;
        char buf[256];

        while (SDL_PollEvent(&ev)) {
            /* keep the window answering; closing it waits for the converter */
        }
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
    SDL_SetWindowTitle(g_pc_window, "Animal Crossing Plus");
    return code == 0;
}
#endif

int pc_cf_setup(void) {
    int version = pc_cf_setup_version();
    char err[512];

    if (version == PC_CF_DATA_VERSION) {
        return TRUE;
    }
#ifdef _WIN32
    if (pc_cf_setup_file_exists(PC_CF_SETUP_PYTHON) && pc_cf_setup_file_exists(PC_CF_SETUP_SCRIPT)) {
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
#endif
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
