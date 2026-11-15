/* pc_dirent.h - opendir/readdir/closedir for ac-plus's folder scans: <dirent.h> where there is one, a small
 * FindFirstFile version on Windows with MSVC/clang-cl (MinGW and every other system have <dirent.h>). */
#ifndef PC_DIRENT_H
#define PC_DIRENT_H

#if defined(_WIN32) && !defined(__MINGW32__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE find;
    WIN32_FIND_DATAA data;
    int first; /* data holds the first entry, not read yet */
    struct dirent entry;
} DIR;

static inline DIR* opendir(const char* path) {
    char pattern[MAX_PATH + 3];
    DIR* d;

    if (strlen(path) + 3 > sizeof(pattern)) {
        return NULL;
    }
    strcpy(pattern, path);
    strcat(pattern, "\\*");
    d = (DIR*)calloc(1, sizeof(DIR));
    if (d == NULL) {
        return NULL;
    }
    d->find = FindFirstFileA(pattern, &d->data);
    if (d->find == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first = 1;
    return d;
}

static inline struct dirent* readdir(DIR* d) {
    if (d == NULL) {
        return NULL;
    }
    if (!d->first && !FindNextFileA(d->find, &d->data)) {
        return NULL;
    }
    d->first = 0;
    strncpy(d->entry.d_name, d->data.cFileName, sizeof(d->entry.d_name) - 1);
    d->entry.d_name[sizeof(d->entry.d_name) - 1] = '\0';
    return &d->entry;
}

static inline int closedir(DIR* d) {
    if (d != NULL) {
        FindClose(d->find);
        free(d);
    }
    return 0;
}
#else
#include <dirent.h>
#endif

#endif /* PC_DIRENT_H */
