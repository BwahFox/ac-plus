/* pc_memtrack.c - dev build only (cmake -DPC_MEMTRACK=ON): host heap tracking by call stack.
 *
 * malloc/calloc/realloc/free of the exe are linked through the __wrap_ functions below
 * (-Wl,--wrap). Every live block is in a pointer table with the call stack that made it.
 * With PC_MEMTRACK=N in the environment, a thread writes memtrack_NNNN.txt every N seconds:
 * the live bytes and blocks of each stack, biggest first. Diff two dumps to see what grows:
 *   pc/tools/memtrack_diff.py first.txt last.txt AnimalCrossing.exe
 * Blocks made outside the exe (SDL2.dll, msvcrt's own strdup, the GL driver) are not seen.
 * Only for measuring: this build draws the town wrongly (actors missing, wrong ground textures) and
 * the house walls too. Frame pointers alone don't do that; the wrapped allocator does, cause unknown. */
#ifdef PC_MEMTRACK
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void* __real_malloc(size_t n);
void* __real_calloc(size_t n, size_t m);
void* __real_realloc(void* p, size_t n);
void __real_free(void* p);

#define MT_DEPTH 6
#define MT_SITES 16384           /* power of two */
#define MT_PTRS  (1u << 21)      /* power of two */

typedef struct {
    void* pc[MT_DEPTH];
    unsigned int hash;
    long long live_bytes;
    long live_blocks;
    long total_blocks;
} MtSite;

typedef struct {
    void* ptr;                   /* NULL empty, (void*)1 deleted */
    unsigned int size;
    unsigned short site;
} MtPtr;

static MtSite* s_sites;
static MtPtr* s_ptrs;
static CRITICAL_SECTION s_lock;
static volatile LONG s_ready;
static __thread int s_inside;
static int s_sites_full;

static unsigned int mt_hash_ptr(void* p) {
    unsigned int x = (unsigned int)(uintptr_t)p;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    return x;
}

/* The frame pointer chain (this build has -fno-omit-frame-pointer), checked against the stack bounds */
static void mt_backtrace(void** out) {
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    void** fp = (void**)__builtin_frame_address(0);
    int i;

    for (i = 0; i < MT_DEPTH; i++) {
        out[i] = NULL;
    }
    for (i = 0; i < MT_DEPTH + 1; i++) {
        if ((void*)fp < tib->StackLimit || (void*)(fp + 2) > tib->StackBase || ((uintptr_t)fp & 3)) {
            break;
        }
        if (i > 0) {             /* frame 0 is the __wrap_ function itself */
            out[i - 1] = fp[1];
        }
        if ((void**)fp[0] <= fp) {
            break;
        }
        fp = (void**)fp[0];
    }
}

static int mt_site(void** pc) {
    unsigned int h = 2166136261u;
    unsigned int i;

    for (i = 0; i < MT_DEPTH; i++) {
        h = (h ^ (unsigned int)(uintptr_t)pc[i]) * 16777619u;
    }
    for (i = 0; i < MT_SITES; i++) {
        MtSite* s = &s_sites[(h + i) & (MT_SITES - 1)];

        if (s->total_blocks == 0) {
            if (i > MT_SITES / 2) {
                break;
            }
            memcpy(s->pc, pc, sizeof(s->pc));
            s->hash = h;
            return (int)((h + i) & (MT_SITES - 1));
        }
        if (s->hash == h && memcmp(s->pc, pc, sizeof(s->pc)) == 0) {
            return (int)((h + i) & (MT_SITES - 1));
        }
    }
    s_sites_full = 1;
    return 0;                    /* site 0 doubles as "everything else" */
}

static void mt_add(void* p, size_t n, void** pc) {
    unsigned int i, h;
    int site;

    if (p == NULL) {
        return;
    }
    site = mt_site(pc);
    h = mt_hash_ptr(p);
    for (i = 0; i < MT_PTRS; i++) {
        MtPtr* e = &s_ptrs[(h + i) & (MT_PTRS - 1)];

        if (e->ptr == NULL || e->ptr == (void*)1) {
            e->ptr = p;
            e->size = (unsigned int)n;
            e->site = (unsigned short)site;
            s_sites[site].live_bytes += n;
            s_sites[site].live_blocks++;
            s_sites[site].total_blocks++;
            return;
        }
    }
}

static void mt_remove(void* p) {
    unsigned int i, h;

    if (p == NULL) {
        return;
    }
    h = mt_hash_ptr(p);
    for (i = 0; i < MT_PTRS; i++) {
        MtPtr* e = &s_ptrs[(h + i) & (MT_PTRS - 1)];

        if (e->ptr == NULL) {
            return;              /* not ours: made outside the exe */
        }
        if (e->ptr == p) {
            s_sites[e->site].live_bytes -= e->size;
            s_sites[e->site].live_blocks--;
            e->ptr = (void*)1;
            return;
        }
    }
}

static int mt_cmp(const void* a, const void* b) {
    const MtSite* x = *(const MtSite* const*)a;
    const MtSite* y = *(const MtSite* const*)b;
    return x->live_bytes < y->live_bytes ? 1 : x->live_bytes > y->live_bytes ? -1 : 0;
}

static void mt_dump(int n) {
    static MtSite copy[MT_SITES];
    static MtSite* order[MT_SITES];
    char path[32];
    long long total = 0;
    long blocks = 0;
    int i, count = 0;
    FILE* f;

    EnterCriticalSection(&s_lock);
    memcpy(copy, s_sites, sizeof(copy));
    LeaveCriticalSection(&s_lock);
    for (i = 0; i < MT_SITES; i++) {
        if (copy[i].total_blocks != 0) {
            order[count++] = &copy[i];
            total += copy[i].live_bytes;
            blocks += copy[i].live_blocks;
        }
    }
    qsort(order, count, sizeof(order[0]), mt_cmp);
    snprintf(path, sizeof(path), "memtrack_%04d.txt", n);
    s_inside = 1;
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "# tick %lu base %p live %lld bytes %ld blocks, %d stacks%s\n", (unsigned long)GetTickCount(),
                (void*)GetModuleHandleA(NULL), total, blocks, count, s_sites_full ? " (stack table full)" : "");
        for (i = 0; i < count; i++) {
            int d;

            fprintf(f, "%lld %ld %ld", order[i]->live_bytes, order[i]->live_blocks, order[i]->total_blocks);
            for (d = 0; d < MT_DEPTH; d++) {
                fprintf(f, " %p", order[i]->pc[d]);
            }
            fputc('\n', f);
        }
        fclose(f);
    }
    s_inside = 0;
}

static DWORD WINAPI mt_thread(LPVOID arg) {
    int secs = (int)(intptr_t)arg;
    int n = 0;

    for (;;) {
        mt_dump(n++);
        Sleep((DWORD)secs * 1000);
    }
    return 0;
}

static void mt_init(void) {
    static volatile LONG once;
    const char* env;

    if (InterlockedExchange(&once, 1) != 0) {
        while (!s_ready) {
            Sleep(0);
        }
        return;
    }
    InitializeCriticalSection(&s_lock);
    s_sites = (MtSite*)VirtualAlloc(NULL, sizeof(MtSite) * MT_SITES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    s_ptrs = (MtPtr*)VirtualAlloc(NULL, sizeof(MtPtr) * MT_PTRS, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    env = getenv("PC_MEMTRACK");
    if (env != NULL && atoi(env) > 0) {
        CreateThread(NULL, 0, mt_thread, (LPVOID)(intptr_t)atoi(env), 0, NULL);
    }
    s_ready = 1;
}

#define MT_ENTER(pc)                                                                                               \
    if (!s_ready) mt_init();                                                                                       \
    void* pc[MT_DEPTH];                                                                                            \
    int mine = !s_inside;                                                                                          \
    if (mine) {                                                                                                    \
        mt_backtrace(pc);                                                                                          \
        EnterCriticalSection(&s_lock);                                                                             \
    }

#define MT_LEAVE()                                                                                                 \
    if (mine) LeaveCriticalSection(&s_lock)

void* __wrap_malloc(size_t n) {
    void* p;
    MT_ENTER(pc);
    p = __real_malloc(n);
    if (mine) mt_add(p, n, pc);
    MT_LEAVE();
    return p;
}

void* __wrap_calloc(size_t n, size_t m) {
    void* p;
    MT_ENTER(pc);
    p = __real_calloc(n, m);
    if (mine) mt_add(p, n * m, pc);
    MT_LEAVE();
    return p;
}

void* __wrap_realloc(void* old, size_t n) {
    void* p;
    MT_ENTER(pc);
    p = __real_realloc(old, n);
    if (mine && (p != NULL || n == 0)) {
        mt_remove(old);
        mt_add(p, n, pc);
    }
    MT_LEAVE();
    return p;
}

void __wrap_free(void* p) {
    MT_ENTER(pc);
    if (mine) mt_remove(p);
    __real_free(p);
    MT_LEAVE();
}
#endif
