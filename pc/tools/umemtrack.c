/* umemtrack.c - glibc heap tracking by call stack for the Linux side of a Wine process
 * (the GL driver, Wine's unix libraries). pc_memtrack.c is the same for the exe's own heap.
 *
 *   cc -O2 -shared -fPIC -o umemtrack.so pc/tools/umemtrack.c -ldl -lpthread
 *   UMT_SECS=30 UMT_MATCH=AnimalCrossing gamescope ... -- env LD_PRELOAD=$PWD/umemtrack.so wine AnimalCrossing.exe
 *
 * Every UMT_SECS seconds the process whose command line contains UMT_MATCH writes umt_PID_NNNN.txt in its
 * working directory: live bytes, live blocks and allocations so far of each stack, biggest first, each
 * frame as library+offset [symbol]. Diff two with memtrack_diff.py --unix A B. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

extern void* __libc_malloc(size_t);
extern void* __libc_calloc(size_t, size_t);
extern void* __libc_realloc(void*, size_t);
extern void* __libc_memalign(size_t, size_t);
extern void __libc_free(void*);

#define DEPTH 24
#define SKIP 2                    /* backtrace itself and the hook */
#define SITES (1u << 16)
#define PTRS (1u << 23)

typedef struct {
    void* pc[DEPTH];
    uint64_t hash;
    long long live_bytes;
    long live_blocks;
    long total;
} Site;

typedef struct {
    void* ptr;                    /* NULL empty, (void*)1 deleted */
    uint32_t size;
    uint32_t site;
} Ptr;

static Site* s_sites;
static Ptr* s_ptrs;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread int t_inside;
static int s_on;

static void* raw_alloc(size_t n) {
    void* p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static uint32_t site_of(void** pc) {
    uint64_t h = 1469598103934665603ull;
    uint32_t i;

    for (i = 0; i < DEPTH; i++) {
        h = (h ^ (uint64_t)(uintptr_t)pc[i]) * 1099511628211ull;
    }
    for (i = 0; i < SITES / 2; i++) {
        uint32_t k = (uint32_t)(h + i) & (SITES - 1);
        Site* s = &s_sites[k];

        if (s->total == 0) {
            memcpy(s->pc, pc, sizeof(s->pc));
            s->hash = h;
            return k;
        }
        if (s->hash == h && memcmp(s->pc, pc, sizeof(s->pc)) == 0) {
            return k;
        }
    }
    return 0;
}

static uint32_t ptr_hash(void* p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    return (uint32_t)x;
}

static void track_add(void* p, size_t n, void** pc) {
    uint32_t h = ptr_hash(p), i, site = site_of(pc);

    for (i = 0; i < PTRS; i++) {
        Ptr* e = &s_ptrs[(h + i) & (PTRS - 1)];

        if (e->ptr == NULL || e->ptr == (void*)1) {
            e->ptr = p;
            e->size = (uint32_t)n;
            e->site = site;
            s_sites[site].live_bytes += (long long)n;
            s_sites[site].live_blocks++;
            s_sites[site].total++;
            return;
        }
    }
}

static void track_remove(void* p) {
    uint32_t h = ptr_hash(p), i;

    for (i = 0; i < PTRS; i++) {
        Ptr* e = &s_ptrs[(h + i) & (PTRS - 1)];

        if (e->ptr == NULL) {
            return;
        }
        if (e->ptr == p) {
            s_sites[e->site].live_bytes -= e->size;
            s_sites[e->site].live_blocks--;
            e->ptr = (void*)1;
            return;
        }
    }
}

/* Takes the lock and the stack when this call should be tracked */
static int enter(void** pc) {
    void* bt[DEPTH + SKIP];
    int n, i;

    if (!s_on || t_inside) {
        return 0;
    }
    t_inside = 1;
    n = backtrace(bt, DEPTH + SKIP);
    for (i = 0; i < DEPTH; i++) {
        pc[i] = i + SKIP < n ? bt[i + SKIP] : NULL;
    }
    pthread_mutex_lock(&s_lock);
    return 1;
}

static void leave(int on) {
    if (on) {
        pthread_mutex_unlock(&s_lock);
        t_inside = 0;
    }
}

void* malloc(size_t n) {
    void* pc[DEPTH];
    int on = enter(pc);
    void* p = __libc_malloc(n);
    if (on && p) track_add(p, n, pc);
    leave(on);
    return p;
}

void* calloc(size_t n, size_t m) {
    void* pc[DEPTH];
    int on = enter(pc);
    void* p = __libc_calloc(n, m);
    if (on && p) track_add(p, n * m, pc);
    leave(on);
    return p;
}

void* realloc(void* old, size_t n) {
    void* pc[DEPTH];
    int on = enter(pc);
    void* p = __libc_realloc(old, n);
    if (on) {
        if (old && (p || n == 0)) track_remove(old);
        if (p) track_add(p, n, pc);
    }
    leave(on);
    return p;
}

void* memalign(size_t a, size_t n) {
    void* pc[DEPTH];
    int on = enter(pc);
    void* p = __libc_memalign(a, n);
    if (on && p) track_add(p, n, pc);
    leave(on);
    return p;
}

int posix_memalign(void** out, size_t a, size_t n) {
    void* p = memalign(a, n);
    if (!p) return 12;
    *out = p;
    return 0;
}

void* aligned_alloc(size_t a, size_t n) {
    return memalign(a, n);
}

void free(void* p) {
    void* pc[DEPTH];
    int on;

    if (!p) return;
    on = enter(pc);
    if (on) track_remove(p);
    __libc_free(p);
    leave(on);
}

static int by_live(const void* a, const void* b) {
    const Site* x = *(const Site* const*)a;
    const Site* y = *(const Site* const*)b;
    return x->live_bytes < y->live_bytes ? 1 : x->live_bytes > y->live_bytes ? -1 : 0;
}

static int matches(const char* want) {
    char cmd[4096];
    FILE* f = fopen("/proc/self/cmdline", "rb");
    size_t n, i;

    if (!f) return 0;
    n = fread(cmd, 1, sizeof(cmd) - 1, f);
    fclose(f);
    for (i = 0; i < n; i++) {
        if (cmd[i] == '\0') cmd[i] = ' ';
    }
    cmd[n] = '\0';
    return strstr(cmd, want) != NULL;
}

static void dump(int n) {
    static Site copy[SITES];
    static Site* order[SITES];
    char path[64];
    long long total = 0;
    int count = 0, i, d;
    FILE* f;

    pthread_mutex_lock(&s_lock);
    memcpy(copy, s_sites, sizeof(copy));
    pthread_mutex_unlock(&s_lock);
    for (i = 0; i < (int)SITES; i++) {
        if (copy[i].total) {
            order[count++] = &copy[i];
            total += copy[i].live_bytes;
        }
    }
    qsort(order, count, sizeof(order[0]), by_live);
    snprintf(path, sizeof(path), "umt_%d_%04d.txt", (int)getpid(), n);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# time %ld live %lld bytes, %d stacks\n", (long)time(NULL), total, count);
    for (i = 0; i < count && i < 400; i++) {
        fprintf(f, "%lld %ld %ld", order[i]->live_bytes, order[i]->live_blocks, order[i]->total);
        for (d = 0; d < DEPTH && order[i]->pc[d]; d++) {
            Dl_info di;
            const char* lib = "?";
            const char* sym = "";
            uintptr_t off = (uintptr_t)order[i]->pc[d];

            if (dladdr(order[i]->pc[d], &di) && di.dli_fname) {
                lib = strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname;
                off -= (uintptr_t)di.dli_fbase;
                sym = di.dli_sname ? di.dli_sname : "";
            }
            fprintf(f, " %s+0x%lx[%s]", lib, (unsigned long)off, sym);
        }
        fputc('\n', f);
    }
    fclose(f);
}

static void* dump_thread(void* arg) {
    int secs = (int)(intptr_t)arg, n = 0;
    const char* want = getenv("UMT_MATCH");

    t_inside = 1;                 /* this thread's own allocations are not counted */
    for (;;) {
        sleep((unsigned)secs);
        if (want == NULL || matches(want)) {
            dump(n++);
        }
    }
    return NULL;
}

__attribute__((constructor)) static void init(void) {
    const char* secs = getenv("UMT_SECS");
    pthread_t th;
    void* warm[4];

    if (secs == NULL || atoi(secs) <= 0) return;
    t_inside = 1;
    backtrace(warm, 4);           /* loads libgcc_s now, not from inside malloc */
    s_sites = raw_alloc(sizeof(Site) * SITES);
    s_ptrs = raw_alloc(sizeof(Ptr) * PTRS);
    t_inside = 0;
    if (!s_sites || !s_ptrs) return;
    s_on = 1;
    pthread_create(&th, NULL, dump_thread, (void*)(intptr_t)atoi(secs));
}
