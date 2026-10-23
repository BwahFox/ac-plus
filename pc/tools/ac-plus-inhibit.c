/* ac-plus-inhibit.c - keep a Linux desktop from dimming, locking or sleeping while ac-plus runs.
 *
 *   ac-plus-inhibit <pid> <start time>
 *
 * The game (pc/src/pc_inhibit.c) starts this when it runs under Wine or Proton, since Wine doesn't
 * pass SDL's screensaver request on to the desktop, and its sockets can't reach D-Bus themselves.
 * It calls org.freedesktop.ScreenSaver.Inhibit on the session bus (what browsers do for video; KDE
 * and GNOME both honour it) and keeps the connection open until process <pid>, started at <start
 * time> (field 22 of /proc/<pid>/stat), is gone. The desktop drops the inhibit when we exit.
 *
 * Built as a static Linux program by ac-plus.sh / build_wine.sh: cc -O2 -static -o ac-plus-inhibit
 * ac-plus-inhibit.c. No libraries; the bit of D-Bus it needs is written out below. */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct {
    unsigned char data[512];
    unsigned int len;
} dbus_msg_c;

static void msg_pad(dbus_msg_c* m, unsigned int align) {
    while (m->len % align) {
        m->data[m->len++] = 0;
    }
}

static void msg_u32(dbus_msg_c* m, unsigned int v) {
    msg_pad(m, 4);
    m->data[m->len++] = v & 0xFF;
    m->data[m->len++] = (v >> 8) & 0xFF;
    m->data[m->len++] = (v >> 16) & 0xFF;
    m->data[m->len++] = v >> 24;
}

static void msg_str(dbus_msg_c* m, const char* s) {
    unsigned int n = (unsigned int)strlen(s);

    msg_u32(m, n);
    memcpy(&m->data[m->len], s, n + 1);
    m->len += n + 1;
}

static void msg_sig(dbus_msg_c* m, const char* s) {
    unsigned int n = (unsigned int)strlen(s);

    m->data[m->len++] = (unsigned char)n;
    memcpy(&m->data[m->len], s, n + 1);
    m->len += n + 1;
}

/* header field: (code, variant of type t holding s) */
static void msg_field(dbus_msg_c* m, unsigned char code, const char* t, const char* s) {
    msg_pad(m, 8);
    m->data[m->len++] = code;
    msg_sig(m, t);
    if (t[0] == 'g') {
        msg_sig(m, s);
    } else {
        msg_str(m, s);
    }
}

/* A little-endian METHOD_CALL; body is "ss" (a, b) when a != NULL, else empty */
static void msg_call(dbus_msg_c* m, unsigned int serial, const char* dest, const char* path, const char* iface,
                     const char* member, const char* a, const char* b) {
    dbus_msg_c body = { 0 };
    dbus_msg_c n = { 0 };
    unsigned int fields_at;

    if (a != NULL) {
        msg_str(&body, a);
        msg_str(&body, b);
    }

    m->len = 0;
    m->data[m->len++] = 'l';
    m->data[m->len++] = 1; /* METHOD_CALL */
    m->data[m->len++] = 0;
    m->data[m->len++] = 1; /* protocol version */
    msg_u32(m, body.len);
    msg_u32(m, serial);
    msg_u32(m, 0); /* fields array length, filled in below */
    fields_at = m->len;
    msg_field(m, 1, "o", path);
    msg_field(m, 6, "s", dest);
    msg_field(m, 2, "s", iface);
    msg_field(m, 3, "s", member);
    if (a != NULL) {
        msg_field(m, 8, "g", "ss");
    }
    msg_u32(&n, m->len - fields_at);
    memcpy(&m->data[fields_at - 4], n.data, 4);
    msg_pad(m, 8);
    memcpy(&m->data[m->len], body.data, body.len);
    m->len += body.len;
}

static int send_all(int fd, const void* p, size_t n) {
    const char* c = (const char*)p;

    while (n > 0) {
        ssize_t r = send(fd, c, n, MSG_NOSIGNAL);
        if (r <= 0) {
            return 0;
        }
        c += r;
        n -= (size_t)r;
    }
    return 1;
}

/* Read one "\r\n"-terminated auth line */
static int recv_line(int fd, char* buf, int size) {
    int n = 0;

    while (n < size - 1) {
        if (recv(fd, &buf[n], 1, 0) != 1) {
            return 0;
        }
        if (buf[n++] == '\n') {
            break;
        }
    }
    buf[n] = '\0';
    return 1;
}

/* Field 22 of /proc/<pid>/stat, or 0 if there's no such process */
static unsigned long long start_time(const char* pid) {
    char path[64];
    char buf[1024];
    unsigned long long t = 0;
    const char* p;
    size_t n;
    FILE* f;
    int field;

    snprintf(path, sizeof(path), "/proc/%s/stat", pid);
    if ((f = fopen(path, "r")) == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    if ((p = strrchr(buf, ')')) == NULL) { /* the name (field 2) may hold spaces */
        return 0;
    }
    for (field = 2; field < 22 && (p = strchr(p + 1, ' ')) != NULL; field++) {
    }
    if (p != NULL) {
        t = strtoull(p + 1, NULL, 10);
    }
    return t;
}

static int bus_connect(void) {
    const char* addr = getenv("DBUS_SESSION_BUS_ADDRESS");
    struct sockaddr_un sa;
    const char* p;
    size_t n;
    int fd;

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (addr != NULL && (p = strstr(addr, "unix:path=")) != NULL) {
        p += 10;
        n = strcspn(p, ",;");
        if (n == 0 || n >= sizeof(sa.sun_path)) {
            return -1;
        }
        memcpy(sa.sun_path, p, n);
    } else if ((p = getenv("XDG_RUNTIME_DIR")) != NULL) {
        snprintf(sa.sun_path, sizeof(sa.sun_path), "%s/bus", p);
    } else {
        snprintf(sa.sun_path, sizeof(sa.sun_path), "/run/user/%u/bus", (unsigned)getuid());
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        return -1;
    }
    return fd;
}

int main(int argc, char** argv) {
    unsigned long long game_start;
    char auth[64];
    char line[256];
    char uid[16];
    dbus_msg_c m;
    struct pollfd pfd;
    int i;
    int fd;

    if (argc != 3 || (game_start = strtoull(argv[2], NULL, 10)) == 0 || start_time(argv[1]) != game_start) {
        return 1;
    }
    if ((fd = bus_connect()) < 0) {
        return 1;
    }

    /* EXTERNAL: our uid, as hex digits of its decimal string */
    snprintf(uid, sizeof(uid), "%u", (unsigned)getuid());
    strcpy(auth, "AUTH EXTERNAL ");
    for (i = 0; uid[i] != '\0'; i++) {
        sprintf(&auth[strlen(auth)], "%02x", (unsigned char)uid[i]);
    }
    strcat(auth, "\r\n");
    if (!send_all(fd, "", 1) || !send_all(fd, auth, strlen(auth)) || !recv_line(fd, line, sizeof(line)) ||
        strncmp(line, "OK", 2) != 0 || !send_all(fd, "BEGIN\r\n", 7)) {
        return 1;
    }

    msg_call(&m, 1, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello", NULL, NULL);
    if (!send_all(fd, m.data, m.len)) {
        return 1;
    }
    msg_call(&m, 2, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver",
             "Inhibit", "ac-plus", "Playing ac-plus");
    if (!send_all(fd, m.data, m.len)) {
        return 1;
    }

    /* Hold the connection while the game runs; read and drop what the bus sends (replies, NameAcquired) */
    pfd.fd = fd;
    pfd.events = POLLIN;
    while (start_time(argv[1]) == game_start) {
        if (poll(&pfd, 1, 2000) > 0 && read(fd, line, sizeof(line)) <= 0) {
            break;
        }
    }
    close(fd);
    return 0;
}
