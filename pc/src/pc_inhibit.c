/* pc_inhibit.c - keep a Linux desktop from dimming, locking or sleeping while the game runs.
 *
 * Under Wine, SDL's screensaver handling only reaches Windows (SetThreadExecutionState), which
 * Wine doesn't pass on to the desktop, and Wine's sockets can't reach D-Bus (no AF_UNIX). So when
 * we find Wine, we start ac-plus-inhibit next to the exe: a small Linux program (built from
 * pc/tools/ac-plus-inhibit.c) that asks the desktop not to sleep until our process is gone. We pass
 * it our Linux pid and start time, read from /proc/self/stat (Wine opens files in our own process).
 * On real Windows, or if the helper is missing, this does nothing. */
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_pc_verbose;

typedef WCHAR* (CDECL* wine_get_dos_file_name_fn)(const char*);

/* Our Linux pid and start time (fields 1 and 22 of /proc/self/stat) */
static int pc_inhibit_self(wine_get_dos_file_name_fn to_dos, unsigned long* pid, unsigned long long* start) {
    WCHAR* path = to_dos("/proc/self/stat");
    char buf[1024];
    const char* p;
    HANDLE f;
    DWORD n = 0;
    int field;

    if (path == NULL) {
        return 0;
    }
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HeapFree(GetProcessHeap(), 0, path);
    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ReadFile(f, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(f);
    buf[n] = '\0';
    *pid = strtoul(buf, NULL, 10);
    if ((p = strrchr(buf, ')')) == NULL) { /* the name (field 2) may hold spaces */
        return 0;
    }
    for (field = 2; field < 22 && (p = strchr(p + 1, ' ')) != NULL; field++) {
    }
    if (p == NULL) {
        return 0;
    }
    *start = strtoull(p + 1, NULL, 10);
    return *pid != 0 && *start != 0;
}

void pc_inhibit_start(void) {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    wine_get_dos_file_name_fn to_dos;
    WCHAR helper[MAX_PATH];
    WCHAR cmd[MAX_PATH + 64];
    WCHAR* slash;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    unsigned long pid;
    unsigned long long start;

    if (k32 == NULL || getenv("PC_NO_INHIBIT") != NULL) {
        return;
    }
    to_dos = (wine_get_dos_file_name_fn)GetProcAddress(k32, "wine_get_dos_file_name");
    if (to_dos == NULL) {
        return; /* not Wine */
    }

    GetModuleFileNameW(NULL, helper, MAX_PATH);
    slash = wcsrchr(helper, L'\\');
    if (slash == NULL || (size_t)(slash + 1 - helper) + 16 >= MAX_PATH) {
        return;
    }
    wcscpy(slash + 1, L"ac-plus-inhibit");
    if (GetFileAttributesW(helper) == INVALID_FILE_ATTRIBUTES || !pc_inhibit_self(to_dos, &pid, &start)) {
        if (g_pc_verbose) {
            printf("[inhibit] no ac-plus-inhibit next to the exe, or no /proc; the desktop may sleep\n");
        }
        return;
    }

    /* Wine runs a Linux program given to CreateProcess, splitting the command line as Windows does */
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%ls\" %lu %llu", helper, pid, start);
    cmd[sizeof(cmd) / sizeof(cmd[0]) - 1] = L'\0';
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (CreateProcessW(helper, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (g_pc_verbose) {
            printf("[inhibit] asked the desktop not to sleep (pid %lu)\n", pid);
        }
    } else if (g_pc_verbose) {
        printf("[inhibit] couldn't start ac-plus-inhibit (%lu)\n", GetLastError());
    }
}
#else
/* Native builds: SDL disables the screensaver itself (over D-Bus on Linux) */
void pc_inhibit_start(void) {
}
#endif
