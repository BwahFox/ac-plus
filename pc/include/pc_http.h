#ifndef PC_HTTP_H
#define PC_HTTP_H

/* HTTP(S) for the updater and save sync (pc_http.c: WinHTTP; nothing elsewhere), and SHA-256 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int status;           /* the HTTP status, 0 if there was no answer (offline, timed out, bad address) */
    char* body;           /* malloc'd, NUL-terminated (free with pc_http_free) */
    size_t len;
    char rev[24];         /* the X-Rev header, if any */
} pc_http_resp;

typedef void (*pc_http_progress)(size_t got, size_t total);

/* method "GET"/"PUT"; headers "Name: value\r\n..." or NULL; body for PUT. Returns resp->status. */
int pc_http_request(const char* method, const char* url, const char* headers, const void* body, size_t body_len,
                    pc_http_resp* resp, int timeout_ms, pc_http_progress progress);
void pc_http_free(pc_http_resp* resp);

/* hex: 65 chars */
void pc_sha256_hex(const void* data, size_t len, char hex[65]);

#ifdef __cplusplus
}
#endif

#endif /* PC_HTTP_H */
