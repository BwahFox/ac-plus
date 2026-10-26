/* pc_wine_egl.c - under Wine with the NVIDIA driver, have Wine present through GLX instead of EGL.
 *
 * Wine 11's X11 driver presents OpenGL through EGL by default. With the NVIDIA driver (615.78,
 * egl-x11 1.0.6) every eglSwapBuffers leaves memory behind in the driver: the game grew 15-40 MB an
 * hour while standing still. Through GLX it doesn't grow at all. Wine reads its choice when the
 * process starts (HKCU\Software\Wine\AppDefaults\<exe>\X11 Driver, "UseEGL"), so this sets it for
 * our exe only, for the next start. A value that is already there, Y or N, is left alone.
 * If the game then can't open its window or GL (GLX broken for that machine), pc_wine_egl_undo takes our
 * value back out, so the start after works as before. */
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <glad/gl.h>

void pc_wine_egl_check(void) {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    WCHAR exe[MAX_PATH], path[MAX_PATH + 64];
    WCHAR* name;
    HKEY key;

    if (ntdll == NULL || GetProcAddress(ntdll, "wine_get_version") == NULL) {
        return; /* not Wine */
    }
    if (vendor == NULL || strstr(vendor, "NVIDIA") == NULL) {
        return;
    }
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    name = wcsrchr(exe, L'\\') != NULL ? wcsrchr(exe, L'\\') + 1 : exe;
    swprintf(path, sizeof(path) / sizeof(path[0]), L"Software\\Wine\\AppDefaults\\%ls\\X11 Driver", name);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL) !=
        ERROR_SUCCESS) {
        return;
    }
    if (RegQueryValueExW(key, L"UseEGL", NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND &&
        RegSetValueExW(key, L"UseEGL", 0, REG_SZ, (const BYTE*)L"N", 2 * sizeof(WCHAR)) == ERROR_SUCCESS) {
        DWORD one = 1;

        RegSetValueExW(key, L"ACPlusSetUseEGL", 0, REG_DWORD, (const BYTE*)&one, sizeof(one)); /* ours: undo knows */
        printf("[wine] NVIDIA under Wine: presenting through GLX from the next start (UseEGL=N for %ls)\n", name);
    }
    RegCloseKey(key);
}

/* The window or GL couldn't be made: if that may be our UseEGL=N, take it out. TRUE if it did (restart to retry) */
int pc_wine_egl_undo(void) {
    WCHAR exe[MAX_PATH], path[MAX_PATH + 64];
    WCHAR* name;
    HKEY key;
    int undone = FALSE;

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    name = wcsrchr(exe, L'\\') != NULL ? wcsrchr(exe, L'\\') + 1 : exe;
    swprintf(path, sizeof(path) / sizeof(path[0]), L"Software\\Wine\\AppDefaults\\%ls\\X11 Driver", name);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return FALSE;
    }
    if (RegQueryValueExW(key, L"ACPlusSetUseEGL", NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        RegDeleteValueW(key, L"UseEGL");
        RegDeleteValueW(key, L"ACPlusSetUseEGL");
        undone = TRUE;
    }
    RegCloseKey(key);
    return undone;
}
#endif
