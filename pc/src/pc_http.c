/* pc_http.c - HTTP(S) requests for the updater and save sync (WinHTTP, which Wine and Proton have too;
 * redirects, like GitHub's to its download host, are followed), and SHA-256. */
#include "pc_http.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

void pc_http_free(pc_http_resp* resp) {
    free(resp->body);
    resp->body = NULL;
    resp->len = 0;
}

#ifdef _WIN32
int pc_http_request(const char* method, const char* url, const char* headers, const void* body, size_t body_len,
                    pc_http_resp* resp, int timeout_ms, pc_http_progress progress) {
    wchar_t wurl[1024], host[256], path[1024], wmethod[8], wheaders[1024];
    URL_COMPONENTS uc;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    DWORD status = 0, len = sizeof(status), total = 0, tlen = sizeof(total);
    size_t cap = 0;
    int ok = FALSE;

    memset(resp, 0, sizeof(*resp));
    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024) == 0 ||
        MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 8) == 0) {
        return 0;
    }
    wheaders[0] = 0;
    if (headers != NULL && MultiByteToWideChar(CP_UTF8, 0, headers, -1, wheaders, 1024) == 0) {
        return 0;
    }
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host, uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path, uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        return 0;
    }
    ses = WinHttpOpen(L"ac-plus", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                      0);
    if (ses == NULL) {
        return 0;
    }
    WinHttpSetTimeouts(ses, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (con != NULL) {
        req = WinHttpOpenRequest(con, wmethod, path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (req != NULL &&
        WinHttpSendRequest(req, wheaders[0] ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS, wheaders[0] ? (DWORD)-1L : 0,
                           (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0) &&
        WinHttpReceiveResponse(req, NULL) &&
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX)) {
        wchar_t wrev[24];
        DWORD rlen = sizeof(wrev);

        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"X-Rev", wrev, &rlen, WINHTTP_NO_HEADER_INDEX)) {
            WideCharToMultiByte(CP_UTF8, 0, wrev, -1, resp->rev, sizeof(resp->rev), NULL, NULL);
        }
        WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &total, &tlen, WINHTTP_NO_HEADER_INDEX);
        ok = TRUE;
        for (;;) {
            DWORD avail = 0, got = 0;

            if (!WinHttpQueryDataAvailable(req, &avail)) {
                ok = FALSE;
                break;
            }
            if (avail == 0) {
                break;
            }
            if (resp->len + avail + 1 > cap) {
                size_t ncap = cap ? cap : 65536;
                char* p;

                while (ncap < resp->len + avail + 1) {
                    ncap *= 2;
                }
                p = realloc(resp->body, ncap);
                if (p == NULL) {
                    ok = FALSE;
                    break;
                }
                resp->body = p, cap = ncap;
            }
            if (!WinHttpReadData(req, resp->body + resp->len, avail, &got)) {
                ok = FALSE;
                break;
            }
            resp->len += got;
            if (progress != NULL) {
                progress(resp->len, total);
            }
        }
    }
    if (req != NULL) {
        WinHttpCloseHandle(req);
    }
    if (con != NULL) {
        WinHttpCloseHandle(con);
    }
    WinHttpCloseHandle(ses);
    if (!ok) {
        pc_http_free(resp);
        resp->status = 0;
        return 0;
    }
    if (resp->body == NULL) {
        resp->body = calloc(1, 1);
    } else {
        resp->body[resp->len] = '\0';
    }
    resp->status = (int)status;
    return resp->status;
}
#else
int pc_http_request(const char* method, const char* url, const char* headers, const void* body, size_t body_len,
                    pc_http_resp* resp, int timeout_ms, pc_http_progress progress) {
    memset(resp, 0, sizeof(*resp));
    return 0;
}
#endif

/* --- SHA-256 (FIPS 180-4) --- */

static const uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const unsigned char* p) {
    uint32_t w[64], a, b, c, d, e, f, g, hh;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (i = 0; i < 64; i++) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + k256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));

        hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}

void pc_sha256_hex(const void* data, size_t len, char hex[65]) {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const unsigned char* p = (const unsigned char*)data;
    unsigned char tail[128];
    size_t full = len / 64 * 64, rest = len - full, t;
    uint64_t bits = (uint64_t)len * 8;
    int i;

    for (t = 0; t < full; t += 64) {
        sha256_block(h, p + t);
    }
    memset(tail, 0, sizeof(tail));
    memcpy(tail, p + full, rest);
    tail[rest] = 0x80;
    t = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; i++) {
        tail[t - 1 - i] = (unsigned char)(bits >> (i * 8));
    }
    sha256_block(h, tail);
    if (t == 128) {
        sha256_block(h, tail + 64);
    }
    for (i = 0; i < 8; i++) {
        snprintf(hex + i * 8, 9, "%08x", (unsigned)h[i]);
    }
}
