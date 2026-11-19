#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#include "usm_switch.h"
#include "switch_util.h"

#define CASE_CACHE_SIZE 256
typedef struct {
    char orig[256];
    char resolved[256];
} CaseCache;
static CaseCache s_case_cache[CASE_CACHE_SIZE];
static unsigned s_case_cache_count = 0;
static pthread_mutex_t s_case_lock = PTHREAD_MUTEX_INITIALIZER;

static void resolve_romfs_case(char* path) {
    if (strncmp(path, "romfs:/", 7) != 0) return;

    struct stat st;
    if (stat(path, &st) == 0) return;

    pthread_mutex_lock(&s_case_lock);
    for (unsigned i = 0; i < s_case_cache_count; i++) {
        if (!strcmp(s_case_cache[i].orig, path)) {
            strcpy(path, s_case_cache[i].resolved);
            pthread_mutex_unlock(&s_case_lock);
            return;
        }
    }
    pthread_mutex_unlock(&s_case_lock);

    char orig_path[256];
    strncpy(orig_path, path, sizeof(orig_path) - 1);
    orig_path[sizeof(orig_path) - 1] = '\0';

    char resolved[512] = "romfs:/";
    char* cur = path + 7;
    while (*cur == '/') cur++;

    while (*cur) {
        char seg[128];
        char* slash = strchr(cur, '/');
        size_t seg_len = slash ? (size_t)(slash - cur) : strlen(cur);
        if (seg_len >= sizeof(seg)) seg_len = sizeof(seg) - 1;
        memcpy(seg, cur, seg_len);
        seg[seg_len] = '\0';

        if (strchr(seg, '*') || strchr(seg, '?')) {
            size_t rlen = strlen(resolved);
            if (rlen > 0 && resolved[rlen - 1] != '/') strncat(resolved, "/", sizeof(resolved) - rlen - 1);
            strncat(resolved, cur, sizeof(resolved) - strlen(resolved) - 1);
            strcpy(path, resolved);
            return;
        }

        cur += seg_len;
        while (*cur == '/') cur++;

        DIR* d = opendir(resolved);
        if (!d) return;

        struct dirent* ent;
        int found = 0;
        char matched[128];
        while ((ent = readdir(d)) != NULL) {
            if (!strcasecmp(ent->d_name, seg)) {
                snprintf(matched, sizeof matched, "%s", ent->d_name);
                found = 1;
                break;
            }
        }
        closedir(d);

        if (!found) return;

        size_t rlen = strlen(resolved);
        if (rlen > 0 && resolved[rlen - 1] != '/') {
            strncat(resolved, "/", sizeof(resolved) - rlen - 1);
        }
        strncat(resolved, matched, sizeof(resolved) - strlen(resolved) - 1);
    }

    if (stat(resolved, &st) == 0) {
        pthread_mutex_lock(&s_case_lock);
        if (s_case_cache_count < CASE_CACHE_SIZE) {
            strncpy(s_case_cache[s_case_cache_count].orig, orig_path, sizeof(s_case_cache[0].orig) - 1);
            strncpy(s_case_cache[s_case_cache_count].resolved, resolved, sizeof(s_case_cache[0].resolved) - 1);
            s_case_cache_count++;
        }
        pthread_mutex_unlock(&s_case_lock);
        strcpy(path, resolved);
    }
}

char* gstrdup(const char* s) {
    char* d = guest_alloc((uint32_t)strlen(s) + 1);
    strcpy(d, s);
    return d;
}

int gformat(char* out, uint32_t n, const char* fmt, uint32_t ap) {
    uint32_t len = 0;
    char spec[32], tmp[512];
#define PUT(s, k) do { uint32_t k_ = (uint32_t)(k); if (len < n) memcpy(out + len, s, len + k_ < n ? k_ : n - len); len += k_; } while (0)
    while (*fmt) {
        if (*fmt != '%') { PUT(fmt, 1); fmt++; continue; }
        if (fmt[1] == '%') { PUT("%", 1); fmt += 2; continue; }
        unsigned k = 0;
        spec[k++] = *fmt++;
        int star[2], nstar = 0, wide64 = 0;
        while (*fmt && strchr("-+ #0123456789.*", *fmt) && k < 20) {
            if (*fmt == '*' && nstar < 2) star[nstar++] = (int)LD32(ap), ap += 4;
            spec[k++] = *fmt++;
        }
        if (fmt[0] == 'I' && fmt[1] == '6' && fmt[2] == '4') wide64 = 1, fmt += 3;
        else if (fmt[0] == 'l' && fmt[1] == 'l') wide64 = 1, fmt += 2;
        else while (*fmt == 'l' || *fmt == 'h' || *fmt == 'L') fmt++;
        char conv = *fmt ? *fmt++ : 'd';
        int w;
        if (strchr("diouxXc", conv)) {
            if (wide64) {
                spec[k++] = 'l', spec[k++] = 'l';
            }
            spec[k++] = conv, spec[k] = 0;
            if (wide64) {
                uint64_t v = LD32(ap) | (uint64_t)LD32(ap + 4) << 32;
                ap += 8;
                w = nstar == 2 ? snprintf(tmp, sizeof tmp, spec, star[0], star[1], v) : nstar ? snprintf(tmp, sizeof tmp, spec, star[0], v) : snprintf(tmp, sizeof tmp, spec, v);
            } else {
                uint32_t v = LD32(ap);
                ap += 4;
                w = nstar == 2 ? snprintf(tmp, sizeof tmp, spec, star[0], star[1], v) : nstar ? snprintf(tmp, sizeof tmp, spec, star[0], v) : snprintf(tmp, sizeof tmp, spec, v);
            }
        } else if (strchr("eEfgGaA", conv)) {
            spec[k++] = conv, spec[k] = 0;
            uint64_t b = LD32(ap) | (uint64_t)LD32(ap + 4) << 32;
            double d;
            memcpy(&d, &b, 8);
            ap += 8;
            w = nstar == 2 ? snprintf(tmp, sizeof tmp, spec, star[0], star[1], d) : nstar ? snprintf(tmp, sizeof tmp, spec, star[0], d) : snprintf(tmp, sizeof tmp, spec, d);
        } else if (conv == 's') {
            spec[k++] = 's', spec[k] = 0;
            uint32_t a = LD32(ap);
            ap += 4;
            const char* s = a ? (const char*)H(a) : "(null)";
            w = nstar == 2 ? snprintf(tmp, sizeof tmp, spec, star[0], star[1], s) : nstar ? snprintf(tmp, sizeof tmp, spec, star[0], s) : snprintf(tmp, sizeof tmp, spec, s);
        } else if (conv == 'p') {
            w = snprintf(tmp, sizeof tmp, "%08X", LD32(ap));
            ap += 4;
        } else if (conv == 'n') {
            ST32(LD32(ap), len);
            ap += 4;
            continue;
        } else {
            PUT(&conv, 1);
            continue;
        }
        if (w > 0) PUT(tmp, w < (int)sizeof tmp ? w : (int)sizeof tmp - 1);
    }
    if (n) out[len < n ? len : n - 1] = 0;
    return (int)len;
#undef PUT
}

void map_path(const char* game, char* out, unsigned n) {
    if (!game) {
        if (n) out[0] = 0;
        return;
    }
    char p[512];
    unsigned k = 0;
    for (; game[k] && k < sizeof p - 1; k++) p[k] = game[k] == '\\' ? '/' : game[k];
    p[k] = 0;

    if (!strncmp(p, "save:/", 6) || !strncmp(p, "romfs:/", 7)) {
        snprintf(out, n, "%s", p);
        if (!strncmp(out, "romfs:/", 7)) resolve_romfs_case(out);
        return;
    }

    const char* s = p;
    if (isalpha((unsigned char)s[0]) && s[1] == ':') s += 2;
    while (*s == '/') s++;
    if (!strcasecmp(s, "USM")) {
        s += 3;
    } else if (!strncasecmp(s, "USM/", 4)) {
        s += 4;
    }
    while (s[0] == '.' && s[1] == '/') s += 2;
    while (*s == '/') s++;

    if (*s == '\0') {
        snprintf(out, n, "save:/");
    } else if (!strncasecmp(s, "save", 4)) {
        /* User saves stay in emuNAND save:/ */
        if (!strncasecmp(s, "save/", 5)) {
            const char* sub = s + 5;
            char cand[512];
            snprintf(cand, sizeof cand, "save:/%s", sub);
            struct stat st;
            if (stat(cand, &st) == 0) {
                snprintf(out, n, "%s", cand);
            } else {
                /* Check if only the filename (e.g. Save0) exists at save:/Save0 */
                const char* slash = strrchr(sub, '/');
                const char* fname = slash ? slash + 1 : sub;
                char flat[512];
                snprintf(flat, sizeof flat, "save:/%s", fname);
                if (stat(flat, &st) == 0) {
                    snprintf(out, n, "%s", flat);
                } else {
                    /* Default to the full subpath */
                    snprintf(out, n, "%s", cand);
                }
            }
        } else {
            snprintf(out, n, "save:/");
        }
    } else if (!strcasecmp(s, "settings.ini") || !strcasecmp(s, "resolution.txt") || !strcasecmp(s, "registry.txt")) {
        char cand[512];
        snprintf(cand, sizeof cand, "save:/%s", s);
        struct stat st;
        if (stat(cand, &st) == 0) {
            snprintf(out, n, "%s", cand);
        } else {
            /* Fall back to embedded RomFS default */
            snprintf(out, n, "romfs:/%s", s);
        }
    } else {
        /* Check if file exists in RomFS (e.g. Data/...) */
        snprintf(out, n, "romfs:/%s", s);
    }

    size_t len = strlen(out);
    while (len > strlen("save:/") && out[len - 1] == '/') {
        out[--len] = '\0';
    }

    if (!strncmp(out, "romfs:/", 7)) {
        resolve_romfs_case(out);
        struct stat st;
        if (stat(out, &st) != 0) {
            /* If not found in RomFS (e.g. running as NRO from SD card), fall back to SD card */
            char sdmc_path[512];
            snprintf(sdmc_path, sizeof sdmc_path, "sdmc:/switch/usm/%s", s);
            if (stat(sdmc_path, &st) == 0) {
                snprintf(out, n, "%s", sdmc_path);
            }
        }
    } else if (!strncmp(out, "save:/", 6)) {
        struct stat st;
        if (stat("save:/", &st) != 0) {
            /* If save:/ partition is not mounted (e.g. running as NRO), fall back to sdmc */
            char sdmc_save[512];
            snprintf(sdmc_save, sizeof sdmc_save, "sdmc:/switch/usm/%s", s);
            snprintf(out, n, "%s", sdmc_save);
        }
    }
}

static pthread_mutex_t g_io_lock = PTHREAD_MUTEX_INITIALIZER;

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    pthread_mutex_lock(&g_io_lock);
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur == (off_t)-1) { pthread_mutex_unlock(&g_io_lock); return -1; }
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) { pthread_mutex_unlock(&g_io_lock); return -1; }
    ssize_t r = read(fd, buf, count);
    lseek(fd, cur, SEEK_SET);
    pthread_mutex_unlock(&g_io_lock);
    return r;
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    pthread_mutex_lock(&g_io_lock);
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur == (off_t)-1) { pthread_mutex_unlock(&g_io_lock); return -1; }
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) { pthread_mutex_unlock(&g_io_lock); return -1; }
    ssize_t r = write(fd, buf, count);
    lseek(fd, cur, SEEK_SET);
    pthread_mutex_unlock(&g_io_lock);
    return r;
}

int g_shim_verbose = 0;
int pipe_skip_unbind(void) { return 0; }
void pipe_fence_d3d(void) {}

