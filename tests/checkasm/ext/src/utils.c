/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "checkasm_config.h"

#ifdef _WIN32
  #include <windows.h>
  #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x04
  #endif
#else
  #if HAVE_ISATTY
    #include <unistd.h>
  #endif
  #if HAVE_IOCTL
    #include <sys/ioctl.h>
  #endif
#endif

#if defined(__APPLE__) && defined(__MACH__)
  #include <mach/mach_time.h>
#endif

#include "checkasm/test.h"
#include "checkasm/utils.h"
#include "internal.h"

NOINLINE void checkasm_noop(void *ptr)
{
    (void) ptr;
}

static ALWAYS_INLINE uint64_t gettime_nsec(int is_seed)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER        ts;
    if (!freq.QuadPart) {
        if (!QueryPerformanceFrequency(&freq))
            return -1;
    }
    if (!QueryPerformanceCounter(&ts))
        return -1;
    return UINT64_C(1000000000) * ts.QuadPart / freq.QuadPart;
#elif defined(__APPLE__) && defined(__MACH__)
    static mach_timebase_info_data_t tb_info;
    if (!tb_info.denom) {
        if (mach_timebase_info(&tb_info) != KERN_SUCCESS)
            return -1;
    }
    return mach_absolute_time() * tb_info.numer / tb_info.denom;
#elif HAVE_CLOCK_GETTIME
    struct timespec ts;
    clockid_t id;
    if (!is_seed) {
  #ifdef CLOCK_MONOTONIC_RAW
      id = CLOCK_MONOTONIC_RAW;
  #else
      id = CLOCK_MONOTONIC;
  #endif
    } else {
      id = CLOCK_REALTIME;
    }
    if (clock_gettime(id, &ts) < 0)
        return -1;
    return UINT64_C(1000000000) * ts.tv_sec + ts.tv_nsec;
#else
    return -1;
#endif
}

uint64_t checkasm_gettime_nsec(void)
{
    return gettime_nsec(0);
}

uint64_t checkasm_gettime_nsec_diff(uint64_t t)
{
    return gettime_nsec(0) - t;
}

unsigned checkasm_seed(void)
{
    return (unsigned) gettime_nsec(1);
}

// (parallel) xoshiro128++ from https://prng.di.unimi.it/
typedef struct CheckasmRand {
#define CHECKASM_PRNG_NUM 4
    uint32_t s0[CHECKASM_PRNG_NUM];
    uint32_t s1[CHECKASM_PRNG_NUM];
    uint32_t s2[CHECKASM_PRNG_NUM];
    uint32_t s3[CHECKASM_PRNG_NUM];
} CheckasmRand;

static CheckasmRand checkasm_prng;

static ALWAYS_INLINE uint32_t rotl(const uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

/* Single round of a parallel xoshiro128++, generates a full block */
static ALWAYS_INLINE void xoshiro128pp(CheckasmRand *restrict xs, uint32_t *restrict buf)
{
    for (int i = 0; i < CHECKASM_PRNG_NUM; i++) {
        buf[i] = rotl(xs->s0[i] + xs->s3[i], 7) + xs->s0[i];

        const uint32_t t = xs->s1[i] << 9;
        xs->s2[i] ^= xs->s0[i];
        xs->s3[i] ^= xs->s1[i];
        xs->s1[i] ^= xs->s2[i];
        xs->s0[i] ^= xs->s3[i];
        xs->s2[i] ^= t;
        xs->s3[i] = rotl(xs->s3[i], 11);
    }
}

static void prng(CheckasmRand *restrict xs, uint8_t *restrict buf, size_t size)
{
    uint32_t     tmp[CHECKASM_PRNG_NUM];
    const size_t block_size = sizeof(tmp);
    CheckasmRand xs_copy    = *xs;

    while (size >= block_size) {
        xoshiro128pp(&xs_copy, tmp);
        memcpy(buf, tmp, block_size);
        buf += block_size;
        size -= block_size;
    }

    if (size) {
        xoshiro128pp(&xs_copy, tmp);
        memcpy(buf, tmp, size);
    }

    *xs = xs_copy;
}

/* Efficient wrapper for generating individual random integers, by caching
 * the result of a single call to the underlying generator() */
static struct {
    #define PRNG_CACHE_SIZE 64
    uint8_t  buf8 [PRNG_CACHE_SIZE];
    uint16_t buf16[PRNG_CACHE_SIZE >> 1];
    uint32_t buf32[PRNG_CACHE_SIZE >> 2];
    uint64_t buf64[PRNG_CACHE_SIZE >> 3];
    int num8;
    int num16;
    int num32;
    int num64;
} prng_cache;

static_assert(PRNG_CACHE_SIZE % sizeof(uint32_t[CHECKASM_PRNG_NUM]) == 0,
              "PRNG_CACHE_SIZE should be a multiple of uint32_t[CHECKASM_PRNG_NUM]");

#define DEF_CHECKASM_RAND(BITS, TYPE, NAME)                                              \
    TYPE checkasm_rand_##NAME(void)                                                      \
    {                                                                                    \
        if (!prng_cache.num##BITS) {                                                     \
            prng(&checkasm_prng, (uint8_t *) prng_cache.buf##BITS,                       \
                 sizeof(prng_cache.buf##BITS));                                          \
            prng_cache.num##BITS = ARRAY_SIZE(prng_cache.buf##BITS);                     \
        }                                                                                \
                                                                                         \
        union {                                                                          \
            TYPE           type;                                                         \
            uint##BITS##_t raw;                                                          \
        } val;                                                                           \
        val.raw = prng_cache.buf##BITS[--prng_cache.num##BITS];                          \
        return val.type;                                                                 \
    }

DEF_CHECKASM_RAND(8,  int8_t,   int8)
DEF_CHECKASM_RAND(8,  uint8_t,  uint8)
DEF_CHECKASM_RAND(16, int16_t,  int16)
DEF_CHECKASM_RAND(16, uint16_t, uint16)
DEF_CHECKASM_RAND(32, int32_t,  int32)
DEF_CHECKASM_RAND(32, uint32_t, uint32)
DEF_CHECKASM_RAND(32, float,    float32)
DEF_CHECKASM_RAND(64, int64_t,  int64)
DEF_CHECKASM_RAND(64, uint64_t, uint64)
DEF_CHECKASM_RAND(64, double,   float64)

static inline uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9e3779b97f4a7c15);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

void checkasm_srand(unsigned seed)
{
    /* Seed using splitmix64() as recommended by xoroshiro128 authors */
    uint64_t s = seed;

    for (int i = 0; i < CHECKASM_PRNG_NUM; i++) {
        const uint64_t a = splitmix64(&s);
        const uint64_t b = splitmix64(&s);

        checkasm_prng.s0[i] = (uint32_t) a;
        checkasm_prng.s1[i] = (uint32_t) b;
        checkasm_prng.s2[i] = a >> 32;
        checkasm_prng.s3[i] = b >> 32;
    }

    /* discard cached random bytes */
    prng_cache.num8 = prng_cache.num16 = prng_cache.num32 = prng_cache.num64 = 0;
}

int checkasm_rand(void)
{
    static_assert(sizeof(int) <= sizeof(uint32_t), "int larger than 32 bits");
    return checkasm_rand_uint32() & INT_MAX;
}

double checkasm_randf(void)
{
    return checkasm_rand_uint32() / (UINT32_MAX + 1.0);
}

/* Marsaglia polar method */
static inline double marsaglia(double *z2)
{
    double u1, u2, w;
    do {
        u1 = 2.0 / UINT32_MAX * checkasm_rand_uint32() - 1.0;
        u2 = 2.0 / UINT32_MAX * checkasm_rand_uint32() - 1.0;
        w  = u1 * u1 + u2 * u2;
    } while (w >= 1.0);

    w   = sqrt((-2.0 * log(w)) / w);
    *z2 = u2 * w;
    return u1 * w;
}

double checkasm_rand_norm(void)
{
    static int    cached;
    static double cache;
    if ((cached = !cached)) {
        return marsaglia(&cache);
    } else {
        return cache;
    }
}

double checkasm_rand_dist(CheckasmDist dist)
{
    return dist.mean + dist.stddev * checkasm_rand_norm();
}

void checkasm_randomize(void *buf, size_t bytes)
{
    prng(&checkasm_prng, buf, bytes);
}

void checkasm_randomize_mask8(uint8_t *buf, int width, uint8_t mask)
{
    prng(&checkasm_prng, (uint8_t *) buf, width * sizeof(*buf));
    for (int i = 0; i < width; i++)
        buf[i] &= mask;
}

void checkasm_randomize_mask16(uint16_t *buf, int width, uint16_t mask)
{
    prng(&checkasm_prng, (uint8_t *) buf, width * sizeof(*buf));
    for (int i = 0; i < width; i++)
        buf[i] &= mask;
}

void checkasm_randomize_range(double *buf, int width, double range)
{
    const double scale = range / (UINT32_MAX + 1.0);
    while (width--)
        *buf++ = scale * checkasm_rand_uint32();
}

void checkasm_randomize_rangef(float *buf, int width, float range)
{
    const float scale = (float) (range / (UINT32_MAX + 1.0));
    while (width--)
        *buf++ = scale * checkasm_rand_uint32();
}

void checkasm_randomize_interval(double *buf, int width, double low, double high)
{
    const double scale = (high - low) / (double) UINT32_MAX;
    while (width--)
        *buf++ = scale * checkasm_rand_uint32() + low;
}

void checkasm_randomize_intervalf(float *buf, int width, float low, float high)
{
    const float scale = (high - low) / (float) UINT32_MAX;
    while (width--)
        *buf++ = scale * checkasm_rand_uint32() + low;
}

#define RANDOMIZE_DIST(buf, ftype, width, mean, stddev)                                  \
    do {                                                                                 \
        if ((width) & 1) {                                                               \
            *(buf)++ = (ftype) ((mean) + (stddev) * checkasm_rand_norm());               \
            (width) ^= 1;                                                                \
        }                                                                                \
                                                                                         \
        for (; width; width -= 2) {                                                      \
            double z1, z2;                                                               \
            z1       = marsaglia(&z2);                                                   \
            *(buf)++ = (ftype) ((mean) + (stddev) * z1);                                 \
            *(buf)++ = (ftype) ((mean) + (stddev) * z2);                                 \
        }                                                                                \
    } while (0)

void checkasm_randomize_dist(double *buf, int width, CheckasmDist dist)
{
    RANDOMIZE_DIST(buf, double, width, dist.mean, dist.stddev);
}

void checkasm_randomize_distf(float *buf, int width, CheckasmDist dist)
{
    RANDOMIZE_DIST(buf, float, width, dist.mean, dist.stddev);
}

void checkasm_randomize_norm(double *buf, int width)
{
    RANDOMIZE_DIST(buf, double, width, 0.0, 1.0);
}

void checkasm_randomize_normf(float *buf, int width)
{
    RANDOMIZE_DIST(buf, float, width, 0.0, 1.0);
}

void checkasm_clear(void *buf, size_t bytes)
{
    memset(buf, 0xAA, bytes);
}

void checkasm_clear8(uint8_t *buf, int width, uint8_t val)
{
    memset(buf, val, width);
}

void checkasm_clear16(uint16_t *buf, int width, uint16_t val)
{
    while (width--)
        *buf++ = val;
}

#if HAVE_STDBIT_H
  #include <stdbit.h>

static inline int clz(const unsigned int mask)
{
    return stdc_leading_zeros_ui(mask);
}

#elif defined(_MSC_VER) && !defined(__clang__)
  #include <intrin.h>

static inline int clz(const unsigned int mask)
{
    unsigned long leading_zero = 0;
    _BitScanReverse(&leading_zero, mask);
    return (31 - leading_zero);
}

#else  /* !_MSC_VER */
static inline int clz(const unsigned int mask)
{
    return __builtin_clz(mask);
}
#endif /* !_MSC_VER */

/* Randomly downshift an integer */
static int shift_rand(int x)
{
    const int bits = 8 * sizeof(x) - clz(x);
    return x ? (x >> (checkasm_rand() % bits)) : 0;
}

enum {
    PAT_ZERO,  // all zero
    PAT_ONE,   // all one
    PAT_RAND,  // random data
    PAT_LOW,   // all low
    PAT_HIGH,  // all high
    PAT_ALTLO, // alternating low and high
    PAT_ALTHI, // alternating high and low
    PAT_MIX,   // random mix of low and high
};

void checkasm_init(void *buf, size_t bytes)
{
    checkasm_init_mask8(buf, (int) bytes, 0xFF);
}

#define DEF_CHECKASM_INIT_MASK(BITS, PIXEL)                                              \
    void checkasm_init_mask##BITS(PIXEL *buf, const int width, const PIXEL mask_pixel)   \
    {                                                                                    \
        if (!width)                                                                      \
            return;                                                                      \
                                                                                         \
        int step = 0, mode = 0, mask = mask_pixel;                                       \
        for (int i = 0; i < width; i++, step--) {                                        \
            if (!step) {                                                                 \
                step = imax(shift_rand(width), 1);                                       \
                mode = checkasm_rand_uint8() & 7;                                        \
                mask = shift_rand(mask_pixel);                                           \
            }                                                                            \
                                                                                         \
            const PIXEL low  = checkasm_rand_uint##BITS() & mask;                        \
            const PIXEL high = mask_pixel - low;                                         \
            switch (mode) {                                                              \
            case PAT_ZERO:  buf[i] = 0; break;                                           \
            case PAT_ONE:   buf[i] = mask_pixel; break;                                  \
            case PAT_RAND:  buf[i] = checkasm_rand_uint##BITS() & mask_pixel; break;     \
            case PAT_LOW:   buf[i] = low; break;                                         \
            case PAT_HIGH:  buf[i] = high; break;                                        \
            case PAT_ALTLO: buf[i] = (i & 1) ? high : low; break;                        \
            case PAT_ALTHI: buf[i] = (i & 1) ? low : high; break;                        \
            case PAT_MIX:   buf[i] = (checkasm_rand_uint8() & 1) ? low : high; break;    \
            }                                                                            \
        }                                                                                \
    }

DEF_CHECKASM_INIT_MASK(8, uint8_t)
DEF_CHECKASM_INIT_MASK(16, uint16_t)

static int use_printf_color[2];
static char statusline[256];
static int statusline_visible;

/* Print colored text to stderr if the terminal supports it */
int checkasm_vfprintf(FILE *const f, const int color, const char *const fmt, va_list arg)
{
    size_t fmt_len = strlen(fmt);
    int use_color = use_printf_color[f == stderr];
    if (!use_color || !fmt_len)
        return vfprintf(f, fmt, arg);

    if (f == stderr && statusline_visible) {
        fprintf(f, "\r\033[K"); /* clear line */
        statusline_visible = 0;
    }

    if (color >= 0)
        fprintf(f, "\x1b[0;%dm", color);

    int ret = vfprintf(f, fmt, arg);

    if (color >= 0)
        fprintf(f, "\x1b[0m");

    if (f == stderr && statusline[0] && fmt[fmt_len - 1] == '\n') {
        fprintf(f, "%s", statusline);
        statusline_visible = 1;
    }

    return ret;
}

void checkasm_statusline(const char *status)
{
    if (!status)
        status = "";

    if (!use_printf_color[1] || !strcmp(statusline, status))
        return; /* don't re-paint unchanged status */

    snprintf(statusline, sizeof(statusline), "%s", status);

    if (statusline_visible) {
        fprintf(stderr, "\r\033[K");
        statusline_visible = 0;
    }

    if (statusline[0]) {
        fprintf(stderr, "%s", statusline);
        statusline_visible = 1;
    }
}

static COLD int should_use_color(FILE *const f)
{
#ifdef _WIN32
  #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    HANDLE con       = GetStdHandle(f == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD  con_mode  = 0;
    return con && con != INVALID_HANDLE_VALUE && GetConsoleMode(con, &con_mode)
        && SetConsoleMode(con, con_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  #else
    return 0;
  #endif
#elif HAVE_ISATTY
    if (isatty(f == stderr ? 2 : 1)) {
        const char *const term = getenv("TERM");
        return term && strcmp(term, "dumb");
    }
    return 0;
#else
    return 0;
#endif
}

COLD void checkasm_setup_fprintf(void)
{
    use_printf_color[0] = should_use_color(stdout);
    use_printf_color[1] = should_use_color(stderr);
}

static int get_terminal_width(void)
{
#ifdef _WIN32
  #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
  #endif
#elif defined(__OS2__)
    int dst[2];
    _scrsize(dst);
    return dst[0];
#elif HAVE_IOCTL && defined(TIOCGWINSZ)
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1)
        return w.ws_col;
#endif
    return 80;
}

void checkasm_json(CheckasmJson *json, const char *key, const char *const fmt, ...)
{
    assert(json->level > 0);
    fputs(json->nonempty ? ",\n" : "\n", json->file);
    for (int i = 0; i < json->level; i++)
        fputc(' ', json->file);

    va_list ap;
    va_start(ap, fmt);
    if (key)
        fprintf(json->file, "\"%s\": ", key);
    vfprintf(json->file, fmt, ap);
    va_end(ap);
    json->nonempty = 1;
}

void checkasm_json_str(CheckasmJson *json, const char *key, const char *str)
{
    assert(json->level > 0);
    fputs(json->nonempty ? ",\n" : "\n", json->file);
    for (int i = 0; i < json->level; i++)
        fputc(' ', json->file);

    if (key)
        fprintf(json->file, "\"%s\": \"", key);
    else
        fputc('"', json->file);

    while (*str) {
        switch (*str) {
        case '\\': fputs("\\\\", json->file); break;
        case '"':  fputs("\\\"", json->file); break;
        case '\n': fputs("\\n", json->file); break;
        default:   fputc(*str, json->file); break;
        }
        str++;
    }
    fputc('"', json->file);
    json->nonempty = 1;
}

void checkasm_json_push(CheckasmJson *json, const char *const key, const char type)
{
    fputs(json->nonempty ? ",\n" : "\n", json->file);
    for (int i = 0; i < json->level; i++)
        fputc(' ', json->file);

    if (key) {
        fprintf(json->file, "\"%s\": %c", key, type);
    } else {
        fputc(type, json->file);
    }

    json->level += 2;
    json->nonempty = 0;
}

void checkasm_json_pop(CheckasmJson *json, char type)
{
    assert(json->level >= 2);
    json->level -= 2;
    if (json->nonempty) {
        fputc('\n', json->file);
        for (int i = 0; i < json->level; i++)
            fputc(' ', json->file);
    }
    fputc(type, json->file);
    json->nonempty = 1;
}

/* float compare support code */
typedef union {
    float    f;
    uint32_t i;
} intfloat;

static int is_negative(const intfloat u)
{
    return u.i >> 31;
}

int checkasm_float_near_ulp(const float a, const float b, const unsigned max_ulp)
{
    intfloat x, y;

    x.f = a;
    y.f = b;

    if (is_negative(x) != is_negative(y)) {
        // handle -0.0 == +0.0
        return a == b;
    }

    if (llabs((int64_t) x.i - y.i) <= max_ulp)
        return 1;

    return 0;
}

int checkasm_float_near_ulp_array(const float *const a, const float *const b,
                                  const unsigned max_ulp, const int len)
{
    for (int i = 0; i < len; i++)
        if (!float_near_ulp(a[i], b[i], max_ulp))
            return 0;

    return 1;
}

int checkasm_float_near_abs_eps(const float a, const float b, const float eps)
{
    return fabsf(a - b) < eps;
}

int checkasm_float_near_abs_eps_array(const float *const a, const float *const b,
                                      const float eps, const int len)
{
    for (int i = 0; i < len; i++)
        if (!float_near_abs_eps(a[i], b[i], eps))
            return 0;

    return 1;
}

int checkasm_float_near_abs_eps_ulp(const float a, const float b, const float eps,
                                    const unsigned max_ulp)
{
    return float_near_ulp(a, b, max_ulp) || float_near_abs_eps(a, b, eps);
}

int checkasm_float_near_abs_eps_array_ulp(const float *const a, const float *const b,
                                          const float eps, const unsigned max_ulp,
                                          const int len)
{
    for (int i = 0; i < len; i++)
        if (!float_near_abs_eps_ulp(a[i], b[i], eps, max_ulp))
            return 0;

    return 1;
}

int checkasm_double_near_abs_eps(const double a, const double b, const double eps)
{
    return fabs(a - b) < eps;
}

int checkasm_double_near_abs_eps_array(const double *const a, const double *const b,
                                       const double eps, const unsigned len)
{
    for (unsigned i = 0; i < len; i++)
        if (!double_near_abs_eps(a[i], b[i], eps))
            return 0;

    return 1;
}

static int check_err(const char *const file, const int line, const char *const name,
                     const int w, const int h, int *const err)
{
    if (*err)
        return 0;
    if (!checkasm_fail_func("%s:%d", file, line))
        return 1;
    *err = 1;
    fprintf(stderr, "%s (%dx%d):\n", name, w, h);
    return 0;
}

#define PRINT_LINE(buf1, buf2, xstart, xend, xpad, fmt, fmtw)                            \
    do {                                                                                 \
        for (int x = xstart; x < xend; x++) {                                            \
            if (buf1[x] != buf2[x])                                                      \
                checkasm_fprintf(stderr, COLOR_RED, " " fmt, buf1[x]);                   \
            else                                                                         \
                fprintf(stderr, " " fmt, buf1[x]);                                       \
        }                                                                                \
        for (int pad = xend; pad < xstart + xpad; pad++)                                 \
            fprintf(stderr, &"          "[9 - fmtw]);                                    \
    } while (0)

#define PRINT_RECT(type, buf1, buf2, ystart, yend, xstart, xend, fmt, fmtw)              \
    do {                                                                                 \
        const type *ptr1          = (buf1) + ystart * stride1;                           \
        const type *ptr2          = (buf2) + ystart * stride1;                           \
        const int   elem_size     = 2 * (fmtw + 1) + 1;                                  \
        const int   display_elems = imin(term_width / elem_size, xend - xstart);         \
        for (int y = ystart; y < yend; y++) {                                            \
            for (int xpos = xstart; xpos < xend; xpos += display_elems) {                \
                const int xstep = imin(xpos + display_elems, xend);                      \
                if (xpos == xstart) /* line change */                                    \
                    checkasm_fprintf(stderr, COLOR_BLUE, "%3d: ", y);                    \
                else                                                                     \
                    fprintf(stderr, "     ");                                            \
                PRINT_LINE(ptr1, ptr2, xpos, xstep, display_elems, fmt, fmtw);           \
                fprintf(stderr, "    ");                                                 \
                PRINT_LINE(ptr2, ptr1, xpos, xstep, display_elems, fmt, fmtw);           \
                fprintf(stderr, "    ");                                                 \
                for (int x = xpos; x < xstep; x++) {                                     \
                    if (ptr1[x] != ptr2[x])                                              \
                        checkasm_fprintf(stderr, COLOR_RED, "x");                        \
                    else                                                                 \
                        fprintf(stderr, ".");                                            \
                }                                                                        \
                fprintf(stderr, "\n");                                                   \
            }                                                                            \
            ptr1 += stride1;                                                             \
            ptr2 += stride2;                                                             \
        }                                                                                \
    } while (0)

#define CHECK_RECT(buf1, buf2, ystart, yend, xstart, xend, msg, compare, type, fmt,      \
                   fmtw)                                                                 \
    do {                                                                                 \
        const int xw = xend - xstart;                                                    \
        for (int y = ystart; y < yend; y++) {                                            \
            if (compare(&buf1[y * stride1 + xstart], &buf2[y * stride2 + xstart], xw))   \
                continue;                                                                \
            if (check_err(file, line, name, w, h, &err))                                 \
                return 1;                                                                \
            /* Exclude unneeded lines on overwrite above */                              \
            int yprint = y < 0 ? y : ystart;                                             \
            if (msg[0])                                                                  \
                fprintf(stderr, " %s (%dx%d, from idx [%d]):\n", msg, xend - xstart,     \
                        yend - yprint, xstart);                                          \
            PRINT_RECT(type, buf1, buf2, yprint, yend, xstart, xend, fmt, fmtw);         \
            break;                                                                       \
        }                                                                                \
    } while (0)

#define DEF_CHECKASM_CHECK_BODY(compare, type, fmt, fmtw)                                \
    do {                                                                                 \
        const int overhead   = 5 + 3 + 3;                                                \
        const int term_width = get_terminal_width() - overhead;                          \
        const int aligned_w  = (w + align_w - 1) & ~(align_w - 1);                       \
        stride1 /= sizeof(type);                                                         \
        stride2 /= sizeof(type);                                                         \
                                                                                         \
        int err = 0;                                                                     \
        CHECK_RECT(buf1, buf2, 0, h, 0, w, "", compare, type, fmt, fmtw);                \
        if (align_h >= 1) {                                                              \
            const int aligned_h = (h + align_h - 1) & ~(align_h - 1);                    \
            CHECK_RECT(buf1, buf2, -padding, 0, -padding, w + padding, "overwrite top",  \
                       compare, type, fmt, fmtw);                                        \
            CHECK_RECT(buf1, buf2, aligned_h, aligned_h + padding, -padding,             \
                       w + padding, "overwrite bottom", compare, type, fmt, fmtw);       \
        }                                                                                \
        CHECK_RECT(buf1, buf2, 0, h, -padding, 0, "overwrite left", compare, type, fmt,  \
                   fmtw);                                                                \
        CHECK_RECT(buf1, buf2, 0, h, aligned_w, aligned_w + padding, "overwrite right",  \
                   compare, type, fmt, fmtw);                                            \
        return err;                                                                      \
    } while (0)

#define cmp_int(a, b, len) (!memcmp(a, b, (len) * sizeof(*(a))))
#define DEF_CHECKASM_CHECK_FUNC(type, fmt, fmtw)                                         \
    int checkasm_check_impl_##type(const char *file, int line, const type *buf1,         \
                                   ptrdiff_t stride1, const type *buf2,                  \
                                   ptrdiff_t stride2, int w, int h, const char *name,    \
                                   int align_w, int align_h, int padding)                \
    {                                                                                    \
        DEF_CHECKASM_CHECK_BODY(cmp_int, type, fmt, fmtw);                               \
    }

DEF_CHECKASM_CHECK_FUNC(int,     "%9d",       9)
DEF_CHECKASM_CHECK_FUNC(int8_t,  "%4" PRId8,  4)
DEF_CHECKASM_CHECK_FUNC(int16_t, "%6" PRId16, 6)
DEF_CHECKASM_CHECK_FUNC(int32_t, "%9" PRId32, 9)

DEF_CHECKASM_CHECK_FUNC(unsigned, "%08x",       8)
DEF_CHECKASM_CHECK_FUNC(uint8_t,  "%02" PRIx8,  2)
DEF_CHECKASM_CHECK_FUNC(uint16_t, "%04" PRIx16, 4)
DEF_CHECKASM_CHECK_FUNC(uint32_t, "%08" PRIx32, 8)

int checkasm_check_impl_float_ulp(const char *file, int line, const float *buf1,
                                  ptrdiff_t stride1, const float *buf2, ptrdiff_t stride2,
                                  int w, int h, const char *name, unsigned max_ulp,
                                  int align_w, int align_h, int padding)
{
#define cmp_float(a, b, len) float_near_ulp_array(a, b, max_ulp, len)
    DEF_CHECKASM_CHECK_BODY(cmp_float, float, "%7g", 7);
#undef cmp_float
}

char *checkasm_vasprintf(const char *fmt, va_list arg)
{
    va_list arg2;
    va_copy(arg2, arg);
    int len = vsnprintf(NULL, 0, fmt, arg2);
    va_end(arg2);
    if (len < 0)
        return NULL;

    char *buf = checkasm_mallocz(len + 1);
    vsnprintf(buf, len + 1, fmt, arg);
    return buf;
}
