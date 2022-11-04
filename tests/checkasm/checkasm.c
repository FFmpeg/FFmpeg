/*
 * Assembly testing and benchmarking tool
 * Copyright (c) 2015 Henrik Gramner
 * Copyright (c) 2008 Loren Merritt
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "config_components.h"

#if CONFIG_LINUX_PERF
# ifndef _GNU_SOURCE
#  define _GNU_SOURCE // for syscall (performance monitoring API)
# endif
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "checkasm.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/intfloat.h"
#include "libavutil/random_seed.h"

#if HAVE_IO_H
#include <io.h>
#endif

#if HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
#include <windows.h>
#define COLOR_RED    FOREGROUND_RED
#define COLOR_GREEN  FOREGROUND_GREEN
#define COLOR_YELLOW (FOREGROUND_RED|FOREGROUND_GREEN)
#else
#define COLOR_RED    1
#define COLOR_GREEN  2
#define COLOR_YELLOW 3
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if !HAVE_ISATTY
#define isatty(fd) 1
#endif

#if ARCH_ARM && HAVE_ARMV5TE_EXTERNAL
#include "libavutil/arm/cpu.h"

void (*checkasm_checked_call)(void *func, int dummy, ...) = checkasm_checked_call_novfp;
#endif

/* List of tests to invoke */
static const struct {
    const char *name;
    void (*func)(void);
} tests[] = {
#if CONFIG_AVCODEC
    #if CONFIG_AAC_DECODER
        { "aacpsdsp", checkasm_check_aacpsdsp },
        { "sbrdsp",   checkasm_check_sbrdsp },
    #endif
    #if CONFIG_ALAC_DECODER
        { "alacdsp", checkasm_check_alacdsp },
    #endif
    #if CONFIG_AUDIODSP
        { "audiodsp", checkasm_check_audiodsp },
    #endif
    #if CONFIG_BLOCKDSP
        { "blockdsp", checkasm_check_blockdsp },
    #endif
    #if CONFIG_BSWAPDSP
        { "bswapdsp", checkasm_check_bswapdsp },
    #endif
    #if CONFIG_DCA_DECODER
        { "synth_filter", checkasm_check_synth_filter },
    #endif
    #if CONFIG_EXR_DECODER
        { "exrdsp", checkasm_check_exrdsp },
    #endif
    #if CONFIG_FLAC_DECODER
        { "flacdsp", checkasm_check_flacdsp },
    #endif
    #if CONFIG_FMTCONVERT
        { "fmtconvert", checkasm_check_fmtconvert },
    #endif
    #if CONFIG_G722DSP
        { "g722dsp", checkasm_check_g722dsp },
    #endif
    #if CONFIG_H264DSP
        { "h264dsp", checkasm_check_h264dsp },
    #endif
    #if CONFIG_H264PRED
        { "h264pred", checkasm_check_h264pred },
    #endif
    #if CONFIG_H264QPEL
        { "h264qpel", checkasm_check_h264qpel },
    #endif
    #if CONFIG_HEVC_DECODER
        { "hevc_add_res", checkasm_check_hevc_add_res },
        { "hevc_idct", checkasm_check_hevc_idct },
        { "hevc_pel", checkasm_check_hevc_pel },
        { "hevc_sao", checkasm_check_hevc_sao },
    #endif
    #if CONFIG_HUFFYUV_DECODER
        { "huffyuvdsp", checkasm_check_huffyuvdsp },
    #endif
    #if CONFIG_IDCTDSP
        { "idctdsp", checkasm_check_idctdsp },
    #endif
    #if CONFIG_JPEG2000_DECODER
        { "jpeg2000dsp", checkasm_check_jpeg2000dsp },
    #endif
    #if CONFIG_HUFFYUVDSP
        { "llviddsp", checkasm_check_llviddsp },
    #endif
    #if CONFIG_LLVIDENCDSP
        { "llviddspenc", checkasm_check_llviddspenc },
    #endif
    #if CONFIG_LPC
        { "lpc", checkasm_check_lpc },
    #endif
    #if CONFIG_ME_CMP
        { "motion", checkasm_check_motion },
    #endif
    #if CONFIG_OPUS_DECODER
        { "opusdsp", checkasm_check_opusdsp },
    #endif
    #if CONFIG_PIXBLOCKDSP
        { "pixblockdsp", checkasm_check_pixblockdsp },
    #endif
    #if CONFIG_UTVIDEO_DECODER
        { "utvideodsp", checkasm_check_utvideodsp },
    #endif
    #if CONFIG_V210_DECODER
        { "v210dec", checkasm_check_v210dec },
    #endif
    #if CONFIG_V210_ENCODER
        { "v210enc", checkasm_check_v210enc },
    #endif
    #if CONFIG_VC1DSP
        { "vc1dsp", checkasm_check_vc1dsp },
    #endif
    #if CONFIG_VP8DSP
        { "vp8dsp", checkasm_check_vp8dsp },
    #endif
    #if CONFIG_VP9_DECODER
        { "vp9dsp", checkasm_check_vp9dsp },
    #endif
    #if CONFIG_VIDEODSP
        { "videodsp", checkasm_check_videodsp },
    #endif
    #if CONFIG_VORBIS_DECODER
        { "vorbisdsp", checkasm_check_vorbisdsp },
    #endif
#endif
#if CONFIG_AVFILTER
    #if CONFIG_AFIR_FILTER
        { "af_afir", checkasm_check_afir },
    #endif
    #if CONFIG_BLEND_FILTER
        { "vf_blend", checkasm_check_blend },
    #endif
    #if CONFIG_COLORSPACE_FILTER
        { "vf_colorspace", checkasm_check_colorspace },
    #endif
    #if CONFIG_EQ_FILTER
        { "vf_eq", checkasm_check_vf_eq },
    #endif
    #if CONFIG_GBLUR_FILTER
        { "vf_gblur", checkasm_check_vf_gblur },
    #endif
    #if CONFIG_HFLIP_FILTER
        { "vf_hflip", checkasm_check_vf_hflip },
    #endif
    #if CONFIG_NLMEANS_FILTER
        { "vf_nlmeans", checkasm_check_nlmeans },
    #endif
    #if CONFIG_THRESHOLD_FILTER
        { "vf_threshold", checkasm_check_vf_threshold },
    #endif
    #if CONFIG_SOBEL_FILTER
        { "vf_sobel", checkasm_check_vf_sobel },
    #endif
#endif
#if CONFIG_SWSCALE
    { "sw_gbrp", checkasm_check_sw_gbrp },
    { "sw_rgb", checkasm_check_sw_rgb },
    { "sw_scale", checkasm_check_sw_scale },
#endif
#if CONFIG_AVUTIL
        { "fixed_dsp", checkasm_check_fixed_dsp },
        { "float_dsp", checkasm_check_float_dsp },
        { "av_tx",     checkasm_check_av_tx },
#endif
    { NULL }
};

/* List of cpu flags to check */
static const struct {
    const char *name;
    const char *suffix;
    int flag;
} cpus[] = {
#if   ARCH_AARCH64
    { "ARMV8",    "armv8",    AV_CPU_FLAG_ARMV8 },
    { "NEON",     "neon",     AV_CPU_FLAG_NEON },
#elif ARCH_ARM
    { "ARMV5TE",  "armv5te",  AV_CPU_FLAG_ARMV5TE },
    { "ARMV6",    "armv6",    AV_CPU_FLAG_ARMV6 },
    { "ARMV6T2",  "armv6t2",  AV_CPU_FLAG_ARMV6T2 },
    { "VFP",      "vfp",      AV_CPU_FLAG_VFP },
    { "VFP_VM",   "vfp_vm",   AV_CPU_FLAG_VFP_VM },
    { "VFPV3",    "vfp3",     AV_CPU_FLAG_VFPV3 },
    { "NEON",     "neon",     AV_CPU_FLAG_NEON },
#elif ARCH_PPC
    { "ALTIVEC",  "altivec",  AV_CPU_FLAG_ALTIVEC },
    { "VSX",      "vsx",      AV_CPU_FLAG_VSX },
    { "POWER8",   "power8",   AV_CPU_FLAG_POWER8 },
#elif ARCH_RISCV
    { "RVI",      "rvi",      AV_CPU_FLAG_RVI },
    { "RVF",      "rvf",      AV_CPU_FLAG_RVF },
    { "RVD",      "rvd",      AV_CPU_FLAG_RVD },
    { "RVVi32",   "rvv_i32",  AV_CPU_FLAG_RVV_I32 },
    { "RVVf32",   "rvv_f32",  AV_CPU_FLAG_RVV_F32 },
    { "RVVi64",   "rvv_i64",  AV_CPU_FLAG_RVV_I64 },
    { "RVVf64",   "rvv_f64",  AV_CPU_FLAG_RVV_F64 },
    { "RVBbasic", "rvb_b",    AV_CPU_FLAG_RVB_BASIC },
#elif ARCH_MIPS
    { "MMI",      "mmi",      AV_CPU_FLAG_MMI },
    { "MSA",      "msa",      AV_CPU_FLAG_MSA },
#elif ARCH_X86
    { "MMX",        "mmx",       AV_CPU_FLAG_MMX|AV_CPU_FLAG_CMOV },
    { "MMXEXT",     "mmxext",    AV_CPU_FLAG_MMXEXT },
    { "3DNOW",      "3dnow",     AV_CPU_FLAG_3DNOW },
    { "3DNOWEXT",   "3dnowext",  AV_CPU_FLAG_3DNOWEXT },
    { "SSE",        "sse",       AV_CPU_FLAG_SSE },
    { "SSE2",       "sse2",      AV_CPU_FLAG_SSE2|AV_CPU_FLAG_SSE2SLOW },
    { "SSE3",       "sse3",      AV_CPU_FLAG_SSE3|AV_CPU_FLAG_SSE3SLOW },
    { "SSSE3",      "ssse3",     AV_CPU_FLAG_SSSE3|AV_CPU_FLAG_ATOM },
    { "SSE4.1",     "sse4",      AV_CPU_FLAG_SSE4 },
    { "SSE4.2",     "sse42",     AV_CPU_FLAG_SSE42 },
    { "AES-NI",     "aesni",     AV_CPU_FLAG_AESNI },
    { "AVX",        "avx",       AV_CPU_FLAG_AVX },
    { "XOP",        "xop",       AV_CPU_FLAG_XOP },
    { "FMA3",       "fma3",      AV_CPU_FLAG_FMA3 },
    { "FMA4",       "fma4",      AV_CPU_FLAG_FMA4 },
    { "AVX2",       "avx2",      AV_CPU_FLAG_AVX2 },
    { "AVX-512",    "avx512",    AV_CPU_FLAG_AVX512 },
    { "AVX-512ICL", "avx512icl", AV_CPU_FLAG_AVX512ICL },
#elif ARCH_LOONGARCH
    { "LSX",      "lsx",      AV_CPU_FLAG_LSX },
    { "LASX",     "lasx",     AV_CPU_FLAG_LASX },
#endif
    { NULL }
};

typedef struct CheckasmFuncVersion {
    struct CheckasmFuncVersion *next;
    void *func;
    int ok;
    int cpu;
    CheckasmPerf perf;
} CheckasmFuncVersion;

/* Binary search tree node */
typedef struct CheckasmFunc {
    struct CheckasmFunc *child[2];
    CheckasmFuncVersion versions;
    uint8_t color; /* 0 = red, 1 = black */
    char name[1];
} CheckasmFunc;

/* Internal state */
static struct {
    CheckasmFunc *funcs;
    CheckasmFunc *current_func;
    CheckasmFuncVersion *current_func_ver;
    const char *current_test_name;
    const char *bench_pattern;
    int bench_pattern_len;
    int num_checked;
    int num_failed;

    /* perf */
    int nop_time;
    int sysfd;

    int cpu_flag;
    const char *cpu_flag_name;
    const char *test_name;
    int verbose;
} state;

/* PRNG state */
AVLFG checkasm_lfg;

/* float compare support code */
static int is_negative(union av_intfloat32 u)
{
    return u.i >> 31;
}

int float_near_ulp(float a, float b, unsigned max_ulp)
{
    union av_intfloat32 x, y;

    x.f = a;
    y.f = b;

    if (is_negative(x) != is_negative(y)) {
        // handle -0.0 == +0.0
        return a == b;
    }

    if (llabs((int64_t)x.i - y.i) <= max_ulp)
        return 1;

    return 0;
}

int float_near_ulp_array(const float *a, const float *b, unsigned max_ulp,
                         unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (!float_near_ulp(a[i], b[i], max_ulp))
            return 0;
    }
    return 1;
}

int float_near_abs_eps(float a, float b, float eps)
{
    float abs_diff = fabsf(a - b);
    if (abs_diff < eps)
        return 1;

    fprintf(stderr, "test failed comparing %g with %g (abs diff=%g with EPS=%g)\n", a, b, abs_diff, eps);

    return 0;
}

int float_near_abs_eps_array(const float *a, const float *b, float eps,
                         unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (!float_near_abs_eps(a[i], b[i], eps))
            return 0;
    }
    return 1;
}

int float_near_abs_eps_ulp(float a, float b, float eps, unsigned max_ulp)
{
    return float_near_ulp(a, b, max_ulp) || float_near_abs_eps(a, b, eps);
}

int float_near_abs_eps_array_ulp(const float *a, const float *b, float eps,
                         unsigned max_ulp, unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (!float_near_abs_eps_ulp(a[i], b[i], eps, max_ulp))
            return 0;
    }
    return 1;
}

int double_near_abs_eps(double a, double b, double eps)
{
    double abs_diff = fabs(a - b);

    return abs_diff < eps;
}

int double_near_abs_eps_array(const double *a, const double *b, double eps,
                              unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (!double_near_abs_eps(a[i], b[i], eps))
            return 0;
    }
    return 1;
}

/* Print colored text to stderr if the terminal supports it */
static void color_printf(int color, const char *fmt, ...)
{
    static int use_color = -1;
    va_list arg;

#if HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
    static HANDLE con;
    static WORD org_attributes;

    if (use_color < 0) {
        CONSOLE_SCREEN_BUFFER_INFO con_info;
        con = GetStdHandle(STD_ERROR_HANDLE);
        if (con && con != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(con, &con_info)) {
            org_attributes = con_info.wAttributes;
            use_color = 1;
        } else
            use_color = 0;
    }
    if (use_color)
        SetConsoleTextAttribute(con, (org_attributes & 0xfff0) | (color & 0x0f));
#else
    if (use_color < 0) {
        const char *term = getenv("TERM");
        use_color = term && strcmp(term, "dumb") && isatty(2);
    }
    if (use_color)
        fprintf(stderr, "\x1b[%d;3%dm", (color & 0x08) >> 3, color & 0x07);
#endif

    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    if (use_color) {
#if HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
        SetConsoleTextAttribute(con, org_attributes);
#else
        fprintf(stderr, "\x1b[0m");
#endif
    }
}

/* Deallocate a tree */
static void destroy_func_tree(CheckasmFunc *f)
{
    if (f) {
        CheckasmFuncVersion *v = f->versions.next;
        while (v) {
            CheckasmFuncVersion *next = v->next;
            free(v);
            v = next;
        }

        destroy_func_tree(f->child[0]);
        destroy_func_tree(f->child[1]);
        free(f);
    }
}

/* Allocate a zero-initialized block, clean up and exit on failure */
static void *checkasm_malloc(size_t size)
{
    void *ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "checkasm: malloc failed\n");
        destroy_func_tree(state.funcs);
        exit(1);
    }
    return ptr;
}

/* Get the suffix of the specified cpu flag */
static const char *cpu_suffix(int cpu)
{
    int i = FF_ARRAY_ELEMS(cpus);

    while (--i >= 0)
        if (cpu & cpus[i].flag)
            return cpus[i].suffix;

    return "c";
}

static int cmp_nop(const void *a, const void *b)
{
    return *(const uint16_t*)a - *(const uint16_t*)b;
}

/* Measure the overhead of the timing code (in decicycles) */
static int measure_nop_time(void)
{
    uint16_t nops[10000];
    int i, nop_sum = 0;
    av_unused const int sysfd = state.sysfd;

    uint64_t t = 0;
    for (i = 0; i < 10000; i++) {
        PERF_START(t);
        PERF_STOP(t);
        nops[i] = t;
    }

    qsort(nops, 10000, sizeof(uint16_t), cmp_nop);
    for (i = 2500; i < 7500; i++)
        nop_sum += nops[i];

    return nop_sum / 500;
}

/* Print benchmark results */
static void print_benchs(CheckasmFunc *f)
{
    if (f) {
        print_benchs(f->child[0]);

        /* Only print functions with at least one assembly version */
        if (f->versions.cpu || f->versions.next) {
            CheckasmFuncVersion *v = &f->versions;
            do {
                CheckasmPerf *p = &v->perf;
                if (p->iterations) {
                    int decicycles = (10*p->cycles/p->iterations - state.nop_time) / 4;
                    printf("%s_%s: %d.%d\n", f->name, cpu_suffix(v->cpu), decicycles/10, decicycles%10);
                }
            } while ((v = v->next));
        }

        print_benchs(f->child[1]);
    }
}

/* ASCIIbetical sort except preserving natural order for numbers */
static int cmp_func_names(const char *a, const char *b)
{
    const char *start = a;
    int ascii_diff, digit_diff;

    for (; !(ascii_diff = *(const unsigned char*)a - *(const unsigned char*)b) && *a; a++, b++);
    for (; av_isdigit(*a) && av_isdigit(*b); a++, b++);

    if (a > start && av_isdigit(a[-1]) && (digit_diff = av_isdigit(*a) - av_isdigit(*b)))
        return digit_diff;

    return ascii_diff;
}

/* Perform a tree rotation in the specified direction and return the new root */
static CheckasmFunc *rotate_tree(CheckasmFunc *f, int dir)
{
    CheckasmFunc *r = f->child[dir^1];
    f->child[dir^1] = r->child[dir];
    r->child[dir] = f;
    r->color = f->color;
    f->color = 0;
    return r;
}

#define is_red(f) ((f) && !(f)->color)

/* Balance a left-leaning red-black tree at the specified node */
static void balance_tree(CheckasmFunc **root)
{
    CheckasmFunc *f = *root;

    if (is_red(f->child[0]) && is_red(f->child[1])) {
        f->color ^= 1;
        f->child[0]->color = f->child[1]->color = 1;
    }

    if (!is_red(f->child[0]) && is_red(f->child[1]))
        *root = rotate_tree(f, 0); /* Rotate left */
    else if (is_red(f->child[0]) && is_red(f->child[0]->child[0]))
        *root = rotate_tree(f, 1); /* Rotate right */
}

/* Get a node with the specified name, creating it if it doesn't exist */
static CheckasmFunc *get_func(CheckasmFunc **root, const char *name)
{
    CheckasmFunc *f = *root;

    if (f) {
        /* Search the tree for a matching node */
        int cmp = cmp_func_names(name, f->name);
        if (cmp) {
            f = get_func(&f->child[cmp > 0], name);

            /* Rebalance the tree on the way up if a new node was inserted */
            if (!f->versions.func)
                balance_tree(root);
        }
    } else {
        /* Allocate and insert a new node into the tree */
        int name_length = strlen(name);
        f = *root = checkasm_malloc(sizeof(CheckasmFunc) + name_length);
        memcpy(f->name, name, name_length + 1);
    }

    return f;
}

/* Perform tests and benchmarks for the specified cpu flag if supported by the host */
static void check_cpu_flag(const char *name, int flag)
{
    int old_cpu_flag = state.cpu_flag;

    flag |= old_cpu_flag;
    av_force_cpu_flags(-1);
    state.cpu_flag = flag & av_get_cpu_flags();
    av_force_cpu_flags(state.cpu_flag);

    if (!flag || state.cpu_flag != old_cpu_flag) {
        int i;

        state.cpu_flag_name = name;
        for (i = 0; tests[i].func; i++) {
            if (state.test_name && strcmp(tests[i].name, state.test_name))
                continue;
            state.current_test_name = tests[i].name;
            tests[i].func();
        }
    }
}

/* Print the name of the current CPU flag, but only do it once */
static void print_cpu_name(void)
{
    if (state.cpu_flag_name) {
        color_printf(COLOR_YELLOW, "%s:\n", state.cpu_flag_name);
        state.cpu_flag_name = NULL;
    }
}

#if CONFIG_LINUX_PERF
static int bench_init_linux(void)
{
    struct perf_event_attr attr = {
        .type           = PERF_TYPE_HARDWARE,
        .size           = sizeof(struct perf_event_attr),
        .config         = PERF_COUNT_HW_CPU_CYCLES,
        .disabled       = 1, // start counting only on demand
        .exclude_kernel = 1,
        .exclude_hv     = 1,
    };

    printf("benchmarking with Linux Perf Monitoring API\n");

    state.sysfd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if (state.sysfd == -1) {
        perror("syscall");
        return -1;
    }
    return 0;
}
#elif CONFIG_MACOS_KPERF
static int bench_init_kperf(void)
{
    ff_kperf_init();
    return 0;
}
#else
static int bench_init_ffmpeg(void)
{
#ifdef AV_READ_TIME
    printf("benchmarking with native FFmpeg timers\n");
    return 0;
#else
    fprintf(stderr, "checkasm: --bench is not supported on your system\n");
    return -1;
#endif
}
#endif

static int bench_init(void)
{
#if CONFIG_LINUX_PERF
    int ret = bench_init_linux();
#elif CONFIG_MACOS_KPERF
    int ret = bench_init_kperf();
#else
    int ret = bench_init_ffmpeg();
#endif
    if (ret < 0)
        return ret;

    state.nop_time = measure_nop_time();
    printf("nop: %d.%d\n", state.nop_time/10, state.nop_time%10);
    return 0;
}

static void bench_uninit(void)
{
#if CONFIG_LINUX_PERF
    if (state.sysfd > 0)
        close(state.sysfd);
#endif
}

int main(int argc, char *argv[])
{
    unsigned int seed = av_get_random_seed();
    int i, ret = 0;

#if ARCH_ARM && HAVE_ARMV5TE_EXTERNAL
    if (have_vfp(av_get_cpu_flags()) || have_neon(av_get_cpu_flags()))
        checkasm_checked_call = checkasm_checked_call_vfp;
#endif

    if (!tests[0].func || !cpus[0].flag) {
        fprintf(stderr, "checkasm: no tests to perform\n");
        return 0;
    }

    while (argc > 1) {
        if (!strncmp(argv[1], "--bench", 7)) {
            if (bench_init() < 0)
                return 1;
            if (argv[1][7] == '=') {
                state.bench_pattern = argv[1] + 8;
                state.bench_pattern_len = strlen(state.bench_pattern);
            } else
                state.bench_pattern = "";
        } else if (!strncmp(argv[1], "--test=", 7)) {
            state.test_name = argv[1] + 7;
        } else if (!strcmp(argv[1], "--verbose") || !strcmp(argv[1], "-v")) {
            state.verbose = 1;
        } else {
            seed = strtoul(argv[1], NULL, 10);
        }

        argc--;
        argv++;
    }

    fprintf(stderr, "checkasm: using random seed %u\n", seed);
    av_lfg_init(&checkasm_lfg, seed);

    check_cpu_flag(NULL, 0);
    for (i = 0; cpus[i].flag; i++)
        check_cpu_flag(cpus[i].name, cpus[i].flag);

    if (state.num_failed) {
        fprintf(stderr, "checkasm: %d of %d tests have failed\n", state.num_failed, state.num_checked);
        ret = 1;
    } else {
        fprintf(stderr, "checkasm: all %d tests passed\n", state.num_checked);
        if (state.bench_pattern) {
            print_benchs(state.funcs);
        }
    }

    destroy_func_tree(state.funcs);
    bench_uninit();
    return ret;
}

/* Decide whether or not the specified function needs to be tested and
 * allocate/initialize data structures if needed. Returns a pointer to a
 * reference function if the function should be tested, otherwise NULL */
void *checkasm_check_func(void *func, const char *name, ...)
{
    char name_buf[256];
    void *ref = func;
    CheckasmFuncVersion *v;
    int name_length;
    va_list arg;

    va_start(arg, name);
    name_length = vsnprintf(name_buf, sizeof(name_buf), name, arg);
    va_end(arg);

    if (!func || name_length <= 0 || name_length >= sizeof(name_buf))
        return NULL;

    state.current_func = get_func(&state.funcs, name_buf);
    state.funcs->color = 1;
    v = &state.current_func->versions;

    if (v->func) {
        CheckasmFuncVersion *prev;
        do {
            /* Only test functions that haven't already been tested */
            if (v->func == func)
                return NULL;

            if (v->ok)
                ref = v->func;

            prev = v;
        } while ((v = v->next));

        v = prev->next = checkasm_malloc(sizeof(CheckasmFuncVersion));
    }

    v->func = func;
    v->ok = 1;
    v->cpu = state.cpu_flag;
    state.current_func_ver = v;

    if (state.cpu_flag)
        state.num_checked++;

    return ref;
}

/* Decide whether or not the current function needs to be benchmarked */
int checkasm_bench_func(void)
{
    return !state.num_failed && state.bench_pattern &&
           !strncmp(state.current_func->name, state.bench_pattern, state.bench_pattern_len);
}

/* Indicate that the current test has failed */
void checkasm_fail_func(const char *msg, ...)
{
    if (state.current_func_ver->cpu && state.current_func_ver->ok) {
        va_list arg;

        print_cpu_name();
        fprintf(stderr, "   %s_%s (", state.current_func->name, cpu_suffix(state.current_func_ver->cpu));
        va_start(arg, msg);
        vfprintf(stderr, msg, arg);
        va_end(arg);
        fprintf(stderr, ")\n");

        state.current_func_ver->ok = 0;
        state.num_failed++;
    }
}

/* Get the benchmark context of the current function */
CheckasmPerf *checkasm_get_perf_context(void)
{
    CheckasmPerf *perf = &state.current_func_ver->perf;
    memset(perf, 0, sizeof(*perf));
    perf->sysfd = state.sysfd;
    return perf;
}

/* Print the outcome of all tests performed since the last time this function was called */
void checkasm_report(const char *name, ...)
{
    static int prev_checked, prev_failed, max_length;

    if (state.num_checked > prev_checked) {
        int pad_length = max_length + 4;
        va_list arg;

        print_cpu_name();
        pad_length -= fprintf(stderr, " - %s.", state.current_test_name);
        va_start(arg, name);
        pad_length -= vfprintf(stderr, name, arg);
        va_end(arg);
        fprintf(stderr, "%*c", FFMAX(pad_length, 0) + 2, '[');

        if (state.num_failed == prev_failed)
            color_printf(COLOR_GREEN, "OK");
        else
            color_printf(COLOR_RED, "FAILED");
        fprintf(stderr, "]\n");

        prev_checked = state.num_checked;
        prev_failed  = state.num_failed;
    } else if (!state.cpu_flag) {
        /* Calculate the amount of padding required to make the output vertically aligned */
        int length = strlen(state.current_test_name);
        va_list arg;

        va_start(arg, name);
        length += vsnprintf(NULL, 0, name, arg);
        va_end(arg);

        if (length > max_length)
            max_length = length;
    }
}

#define DEF_CHECKASM_CHECK_FUNC(type, fmt) \
int checkasm_check_##type(const char *const file, const int line, \
                          const type *buf1, ptrdiff_t stride1, \
                          const type *buf2, ptrdiff_t stride2, \
                          const int w, int h, const char *const name) \
{ \
    int y = 0; \
    stride1 /= sizeof(*buf1); \
    stride2 /= sizeof(*buf2); \
    for (y = 0; y < h; y++) \
        if (memcmp(&buf1[y*stride1], &buf2[y*stride2], w*sizeof(*buf1))) \
            break; \
    if (y == h) \
        return 0; \
    checkasm_fail_func("%s:%d", file, line); \
    if (!state.verbose) \
        return 1; \
    fprintf(stderr, "%s:\n", name); \
    while (h--) { \
        for (int x = 0; x < w; x++) \
            fprintf(stderr, " " fmt, buf1[x]); \
        fprintf(stderr, "    "); \
        for (int x = 0; x < w; x++) \
            fprintf(stderr, " " fmt, buf2[x]); \
        fprintf(stderr, "    "); \
        for (int x = 0; x < w; x++) \
            fprintf(stderr, "%c", buf1[x] != buf2[x] ? 'x' : '.'); \
        buf1 += stride1; \
        buf2 += stride2; \
        fprintf(stderr, "\n"); \
    } \
    return 1; \
}

DEF_CHECKASM_CHECK_FUNC(uint8_t,  "%02x")
DEF_CHECKASM_CHECK_FUNC(uint16_t, "%04x")
DEF_CHECKASM_CHECK_FUNC(uint32_t, "%08x")
DEF_CHECKASM_CHECK_FUNC(int16_t,  "%6d")
DEF_CHECKASM_CHECK_FUNC(int32_t,  "%9d")
