/*
 * Copyright (C) 2024      Nikles Haas
 * Copyright (C) 2003-2011 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>
#include <signal.h>

#undef HAVE_AV_CONFIG_H
#include "libavutil/cpu.h"
#include "libavutil/pixdesc.h"
#include "libavutil/lfg.h"
#include "libavutil/sfc64.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/pixfmt.h"
#include "libavutil/avassert.h"
#include "libavutil/macros.h"

#include "libswscale/swscale.h"

struct options {
    enum AVPixelFormat src_fmt;
    enum AVPixelFormat dst_fmt;
    double prob;
    int w, h;
    int threads;
    int iters;
    int bench;
    int flags;
    int dither;
    int unscaled;
};

struct mode {
    SwsFlags flags;
    SwsDither dither;
};

const SwsFlags flags[] = {
    0, // test defaults
    SWS_FAST_BILINEAR,
    SWS_BILINEAR,
    SWS_BICUBIC,
    SWS_X | SWS_BITEXACT,
    SWS_POINT,
    SWS_AREA | SWS_ACCURATE_RND,
    SWS_BICUBIC | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP,
};

static FFSFC64 prng_state;
static SwsContext *sws[3]; /* reused between tests for efficiency */

static double speedup_logavg;
static double speedup_min = 1e10;
static double speedup_max = 0;
static int speedup_count;

static const char *speedup_color(double ratio)
{
    return ratio > 10.00 ? "\033[1;94m" : /* bold blue */
           ratio >  2.00 ? "\033[1;32m" : /* bold green */
           ratio >  1.02 ? "\033[32m"   : /* green */
           ratio >  0.98 ? ""           : /* default */
           ratio >  0.90 ? "\033[33m"   : /* yellow */
           ratio >  0.75 ? "\033[31m"   : /* red */
            "\033[1;31m";  /* bold red */
}

static void exit_handler(int sig)
{
    if (speedup_count) {
        double ratio = exp(speedup_logavg / speedup_count);
        printf("Overall speedup=%.3fx %s%s\033[0m, min=%.3fx max=%.3fx\n", ratio,
               speedup_color(ratio), ratio >= 1.0 ? "faster" : "slower",
               speedup_min, speedup_max);
    }

    exit(sig);
}

/* Estimate luma variance assuming uniform dither noise distribution */
static float estimate_quantization_noise(enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    float variance = 1.0 / 12;
    if (desc->comp[0].depth < 8) {
        /* Extra headroom for very low bit depth output */
        variance *= (8 - desc->comp[0].depth);
    }

    if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        return 0.0;
    } else if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
        const float r = 0.299 / (1 << desc->comp[0].depth);
        const float g = 0.587 / (1 << desc->comp[1].depth);
        const float b = 0.114 / (1 << desc->comp[2].depth);
        return (r * r + g * g + b * b) * variance;
    } else {
        const float y = 1.0 / (1 << desc->comp[0].depth);
        return y * y * variance;
    }
}

static int fmt_comps(enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    int comps = desc->nb_components >= 3 ? 0b111 : 0b1;
    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA)
        comps |= 0b1000;
    return comps;
}

static void get_ssim(float ssim[4], const AVFrame *out, const AVFrame *ref, int comps)
{
    av_assert1(out->format == AV_PIX_FMT_YUVA444P);
    av_assert1(ref->format == out->format);
    av_assert1(ref->width == out->width && ref->height == out->height);

    for (int p = 0; p < 4; p++) {
        const int stride_a = out->linesize[p];
        const int stride_b = ref->linesize[p];
        const int w = out->width;
        const int h = out->height;

        const int is_chroma = p == 1 || p == 2;
        const uint8_t def = is_chroma ? 128 : 0xFF;
        const int has_ref = comps & (1 << p);
        double sum = 0;
        int count = 0;

        /* 4x4 SSIM */
        for (int y = 0; y < (h & ~3); y += 4) {
            for (int x = 0; x < (w & ~3); x += 4) {
                const float c1 = .01 * .01 * 255 * 255 * 64;
                const float c2 = .03 * .03 * 255 * 255 * 64 * 63;
                int s1 = 0, s2 = 0, ss = 0, s12 = 0, var, covar;

                for (int yy = 0; yy < 4; yy++) {
                    for (int xx = 0; xx < 4; xx++) {
                        int a = out->data[p][(y + yy) * stride_a + x + xx];
                        int b = has_ref ? ref->data[p][(y + yy) * stride_b + x + xx] : def;
                        s1  += a;
                        s2  += b;
                        ss  += a * a + b * b;
                        s12 += a * b;
                    }
                }

                var = ss * 64 - s1 * s1 - s2 * s2;
                covar = s12 * 64 - s1 * s2;
                sum += (2 * s1 * s2 + c1) * (2 * covar + c2) /
                       ((s1 * s1 + s2 * s2 + c1) * (var + c2));
                count++;
            }
        }

        ssim[p] = count ? sum / count : 0.0;
    }
}

static float get_loss(const float ssim[4])
{
    const float weights[3] = { 0.8, 0.1, 0.1 }; /* tuned for Y'CrCr */

    float sum = 0;
    for (int i = 0; i < 3; i++)
        sum += weights[i] * ssim[i];
    sum *= ssim[3]; /* ensure alpha errors get caught */

    return 1.0 - sum;
}

static int scale_legacy(AVFrame *dst, const AVFrame *src, struct mode mode,
                        struct options opts)
{
    SwsContext *sws_legacy;
    int ret;

    sws_legacy = sws_alloc_context();
    if (!sws_legacy)
        return -1;

    sws_legacy->src_w      = src->width;
    sws_legacy->src_h      = src->height;
    sws_legacy->src_format = src->format;
    sws_legacy->dst_w      = dst->width;
    sws_legacy->dst_h      = dst->height;
    sws_legacy->dst_format = dst->format;
    sws_legacy->flags      = mode.flags;
    sws_legacy->dither     = mode.dither;
    sws_legacy->threads    = opts.threads;

    if ((ret = sws_init_context(sws_legacy, NULL, NULL)) < 0)
        goto error;

    for (int i = 0; ret >= 0 && i < opts.iters; i++)
        ret = sws_scale_frame(sws_legacy, dst, src);

error:
    sws_freeContext(sws_legacy);
    return ret;
}

/* Runs a series of ref -> src -> dst -> out, and compares out vs ref */
static int run_test(enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                    int dst_w, int dst_h, struct mode mode, struct options opts,
                    const AVFrame *ref, const float ssim_ref[4])
{
    AVFrame *src = NULL, *dst = NULL, *out = NULL;
    float ssim[4], ssim_sws[4];
    const int comps = fmt_comps(src_fmt) & fmt_comps(dst_fmt);
    int64_t time, time_ref = 0;
    int ret = -1;

    /* Estimate the expected amount of loss from bit depth reduction */
    const float c1 = 0.01 * 0.01; /* stabilization constant */
    const float ref_var = 1.0 / 12.0; /* uniformly distributed signal */
    const float src_var = estimate_quantization_noise(src_fmt);
    const float dst_var = estimate_quantization_noise(dst_fmt);
    const float out_var = estimate_quantization_noise(ref->format);
    const float total_var = src_var + dst_var + out_var;
    const float ssim_luma = (2 * ref_var + c1) / (2 * ref_var + total_var + c1);
    const float ssim_expected[4] = { ssim_luma, 1, 1, 1 }; /* for simplicity */
    const float expected_loss = get_loss(ssim_expected);
    float loss;

    src = av_frame_alloc();
    dst = av_frame_alloc();
    out = av_frame_alloc();
    if (!src || !dst || !out)
        goto error;

    av_frame_copy_props(src, ref);
    av_frame_copy_props(dst, ref);
    av_frame_copy_props(out, ref);
    src->width  = out->width  = ref->width;
    src->height = out->height = ref->height;
    out->format = ref->format;
    src->format = src_fmt;
    dst->format = dst_fmt;
    dst->width  = dst_w;
    dst->height = dst_h;

    if (sws_scale_frame(sws[0], src, ref) < 0) {
        fprintf(stderr, "Failed %s ---> %s\n", av_get_pix_fmt_name(ref->format),
                av_get_pix_fmt_name(src->format));
        goto error;
    }

    sws[1]->flags  = mode.flags;
    sws[1]->dither = mode.dither;
    sws[1]->threads = opts.threads;

    time = av_gettime_relative();

    for (int i = 0; i < opts.iters; i++) {
        if (sws_scale_frame(sws[1], dst, src) < 0) {
            fprintf(stderr, "Failed %s ---> %s\n", av_get_pix_fmt_name(src->format),
                    av_get_pix_fmt_name(dst->format));
            goto error;
        }
    }

    time = av_gettime_relative() - time;

    if (sws_scale_frame(sws[2], out, dst) < 0) {
        fprintf(stderr, "Failed %s ---> %s\n", av_get_pix_fmt_name(dst->format),
                av_get_pix_fmt_name(out->format));
        goto error;
    }

    get_ssim(ssim, out, ref, comps);
    printf("%s %dx%d -> %s %3dx%3d, flags=0x%x dither=%u, "
           "SSIM {Y=%f U=%f V=%f A=%f}\n",
           av_get_pix_fmt_name(src->format), src->width, src->height,
           av_get_pix_fmt_name(dst->format), dst->width, dst->height,
           mode.flags, mode.dither,
           ssim[0], ssim[1], ssim[2], ssim[3]);

    loss = get_loss(ssim);
    if (loss - expected_loss > 1e-4 && dst_w >= ref->width && dst_h >= ref->height) {
        int bad = loss - expected_loss > 1e-2;
        printf("\033[1;31m  loss %g is %s by %g, expected loss %g\033[0m\n",
               loss, bad ? "WORSE" : "worse", loss - expected_loss, expected_loss);
        if (bad)
            goto error;
    }

    if (!ssim_ref && sws_isSupportedInput(src->format) && sws_isSupportedOutput(dst->format)) {
        /* Compare against the legacy swscale API as a reference */
        time_ref = av_gettime_relative();
        if (scale_legacy(dst, src, mode, opts) < 0) {
            fprintf(stderr, "Failed ref %s ---> %s\n", av_get_pix_fmt_name(src->format),
                    av_get_pix_fmt_name(dst->format));
            goto error;
        }
        time_ref = av_gettime_relative() - time_ref;

        if (sws_scale_frame(sws[2], out, dst) < 0)
            goto error;

        get_ssim(ssim_sws, out, ref, comps);

        /* Legacy swscale does not perform bit accurate upconversions of low
         * bit depth RGB. This artificially improves the SSIM score because the
         * resulting error deletes some of the input dither noise. This gives
         * it an unfair advantage when compared against a bit exact reference.
         * Work around this by ensuring that the reference SSIM score is not
         * higher than it theoretically "should" be. */
        if (src_var > dst_var) {
            const float src_loss = (2 * ref_var + c1) / (2 * ref_var + src_var + c1);
            ssim_sws[0] = FFMIN(ssim_sws[0], src_loss);
        }

        ssim_ref = ssim_sws;
    }

    if (ssim_ref) {
        const float loss_ref = get_loss(ssim_ref);
        if (loss - loss_ref > 1e-4) {
            int bad = loss - loss_ref > 1e-2;
            printf("\033[1;31m  loss %g is %s by %g, ref loss %g, "
                   "SSIM {Y=%f U=%f V=%f A=%f}\033[0m\n",
                   loss, bad ? "WORSE" : "worse", loss - loss_ref, loss_ref,
                   ssim_ref[0], ssim_ref[1], ssim_ref[2], ssim_ref[3]);
            if (bad)
                goto error;
        }
    }

    if (opts.bench && time_ref) {
        double ratio = (double) time_ref / time;
        if (FFMIN(time, time_ref) > 100 /* don't pollute stats with low precision */) {
            speedup_min = FFMIN(speedup_min, ratio);
            speedup_max = FFMAX(speedup_max, ratio);
            speedup_logavg += log(ratio);
            speedup_count++;
        }

        printf("  time=%"PRId64" us, ref=%"PRId64" us, speedup=%.3fx %s%s\033[0m\n",
               time / opts.iters, time_ref / opts.iters, ratio,
               speedup_color(ratio), ratio >= 1.0 ? "faster" : "slower");
    } else if (opts.bench) {
        printf("  time=%"PRId64" us\n", time / opts.iters);
    }

    fflush(stdout);
    ret = 0; /* fall through */
 error:
    av_frame_free(&src);
    av_frame_free(&dst);
    av_frame_free(&out);
    return ret;
}

static inline int fmt_is_subsampled(enum AVPixelFormat fmt)
{
    return av_pix_fmt_desc_get(fmt)->log2_chroma_w != 0 ||
           av_pix_fmt_desc_get(fmt)->log2_chroma_h != 0;
}

static int run_self_tests(const AVFrame *ref, struct options opts)
{
    const int dst_w[] = { opts.w, opts.w - opts.w / 3, opts.w + opts.w / 3 };
    const int dst_h[] = { opts.h, opts.h - opts.h / 3, opts.h + opts.h / 3 };

    enum AVPixelFormat src_fmt, dst_fmt,
                       src_fmt_min = 0,
                       dst_fmt_min = 0,
                       src_fmt_max = AV_PIX_FMT_NB - 1,
                       dst_fmt_max = AV_PIX_FMT_NB - 1;

    if (opts.src_fmt != AV_PIX_FMT_NONE)
        src_fmt_min = src_fmt_max = opts.src_fmt;
    if (opts.dst_fmt != AV_PIX_FMT_NONE)
        dst_fmt_min = dst_fmt_max = opts.dst_fmt;

    for (src_fmt = src_fmt_min; src_fmt <= src_fmt_max; src_fmt++) {
        if (opts.unscaled && fmt_is_subsampled(src_fmt))
            continue;
        if (!sws_test_format(src_fmt, 0) || !sws_test_format(src_fmt, 1))
            continue;
        for (dst_fmt = dst_fmt_min; dst_fmt <= dst_fmt_max; dst_fmt++) {
            if (opts.unscaled && fmt_is_subsampled(dst_fmt))
                continue;
            if (!sws_test_format(dst_fmt, 0) || !sws_test_format(dst_fmt, 1))
                continue;
            for (int h = 0; h < FF_ARRAY_ELEMS(dst_h); h++) {
                for (int w = 0; w < FF_ARRAY_ELEMS(dst_w); w++) {
                    for (int f = 0; f < FF_ARRAY_ELEMS(flags); f++) {
                        struct mode mode = {
                            .flags  = opts.flags  >= 0 ? opts.flags  : flags[f],
                            .dither = opts.dither >= 0 ? opts.dither : SWS_DITHER_AUTO,
                        };

                        if (ff_sfc64_get(&prng_state) > UINT64_MAX * opts.prob)
                            continue;

                        if (run_test(src_fmt, dst_fmt, dst_w[w], dst_h[h],
                                     mode, opts, ref, NULL) < 0)
                            return -1;

                        if (opts.flags >= 0 || opts.unscaled)
                            break;
                    }
                    if (opts.unscaled)
                        break;
                }
                if (opts.unscaled)
                    break;
            }
        }
    }

    return 0;
}

static int run_file_tests(const AVFrame *ref, FILE *fp, struct options opts)
{
    char buf[256];
    int ret;

    while (fgets(buf, sizeof(buf), fp)) {
        char src_fmt_str[21], dst_fmt_str[21];
        enum AVPixelFormat src_fmt;
        enum AVPixelFormat dst_fmt;
        int sw, sh, dw, dh;
        float ssim[4];
        struct mode mode;

        ret = sscanf(buf,
                     "%20s %dx%d -> %20s %dx%d, flags=0x%x dither=%u, "
                     "SSIM {Y=%f U=%f V=%f A=%f}\n",
                     src_fmt_str, &sw, &sh, dst_fmt_str, &dw, &dh,
                     &mode.flags, &mode.dither,
                     &ssim[0], &ssim[1], &ssim[2], &ssim[3]);
        if (ret != 12) {
            printf("%s", buf);
            continue;
        }

        src_fmt = av_get_pix_fmt(src_fmt_str);
        dst_fmt = av_get_pix_fmt(dst_fmt_str);
        if (src_fmt == AV_PIX_FMT_NONE || dst_fmt == AV_PIX_FMT_NONE ||
            sw != ref->width || sh != ref->height || dw > 8192 || dh > 8192 ||
            mode.dither >= SWS_DITHER_NB) {
            fprintf(stderr, "malformed input file\n");
            return -1;
        }

        if (opts.src_fmt != AV_PIX_FMT_NONE && src_fmt != opts.src_fmt ||
            opts.dst_fmt != AV_PIX_FMT_NONE && dst_fmt != opts.dst_fmt)
            continue;

        if (run_test(src_fmt, dst_fmt, dw, dh, mode, opts, ref, ssim) < 0)
            return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct options opts = {
        .src_fmt = AV_PIX_FMT_NONE,
        .dst_fmt = AV_PIX_FMT_NONE,
        .w       = 96,
        .h       = 96,
        .threads = 1,
        .iters   = 1,
        .prob    = 1.0,
        .flags   = -1,
        .dither  = -1,
    };

    AVFrame *rgb = NULL, *ref = NULL;
    FILE *fp = NULL;
    AVLFG rand;
    int ret = -1;

    for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
            fprintf(stderr,
                    "swscale [options...]\n"
                    "   -help\n"
                    "       This text\n"
                    "   -ref <file>\n"
                    "       Uses file as reference to compare tests againsts. Tests that have become worse will contain the string worse or WORSE\n"
                    "   -p <number between 0.0 and 1.0>\n"
                    "       The percentage of tests or comparisons to perform. Doing all tests will take long and generate over a hundred MB text output\n"
                    "       It is often convenient to perform a random subset\n"
                    "   -dst <pixfmt>\n"
                    "       Only test the specified destination pixel format\n"
                    "   -src <pixfmt>\n"
                    "       Only test the specified source pixel format\n"
                    "   -bench <iters>\n"
                    "       Run benchmarks with the specified number of iterations. This mode also increases the size of the test images\n"
                    "   -flags <flags>\n"
                    "       Test with a specific combination of flags\n"
                    "   -dither <mode>\n"
                    "       Test with a specific dither mode\n"
                    "   -unscaled <1 or 0>\n"
                    "       If 1, test only conversions that do not involve scaling\n"
                    "   -threads <threads>\n"
                    "       Use the specified number of threads\n"
                    "   -cpuflags <cpuflags>\n"
                    "       Uses the specified cpuflags in the tests\n"
                    "   -v <level>\n"
                    "       Enable log verbosity at given level\n"
            );
            return 0;
        }
        if (argv[i][0] != '-' || i + 1 == argc)
            goto bad_option;
        if (!strcmp(argv[i], "-ref")) {
            fp = fopen(argv[i + 1], "r");
            if (!fp) {
                fprintf(stderr, "could not open '%s'\n", argv[i + 1]);
                goto error;
            }
        } else if (!strcmp(argv[i], "-cpuflags")) {
            unsigned flags = av_get_cpu_flags();
            int res = av_parse_cpu_caps(&flags, argv[i + 1]);
            if (res < 0) {
                fprintf(stderr, "invalid cpu flags %s\n", argv[i + 1]);
                goto error;
            }
            av_force_cpu_flags(flags);
        } else if (!strcmp(argv[i], "-src")) {
            opts.src_fmt = av_get_pix_fmt(argv[i + 1]);
            if (opts.src_fmt == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                goto error;
            }
        } else if (!strcmp(argv[i], "-dst")) {
            opts.dst_fmt = av_get_pix_fmt(argv[i + 1]);
            if (opts.dst_fmt == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                goto error;
            }
        } else if (!strcmp(argv[i], "-bench")) {
            opts.bench = 1;
            opts.iters = atoi(argv[i + 1]);
            opts.iters = FFMAX(opts.iters, 1);
            opts.w = 1920;
            opts.h = 1080;
        } else if (!strcmp(argv[i], "-flags")) {
            opts.flags = strtol(argv[i + 1], NULL, 0);
        } else if (!strcmp(argv[i], "-dither")) {
            opts.dither = atoi(argv[i + 1]);
        } else if (!strcmp(argv[i], "-unscaled")) {
            opts.unscaled = atoi(argv[i + 1]);
        } else if (!strcmp(argv[i], "-threads")) {
            opts.threads = atoi(argv[i + 1]);
        } else if (!strcmp(argv[i], "-p")) {
            opts.prob = atof(argv[i + 1]);
        } else if (!strcmp(argv[i], "-v")) {
            av_log_set_level(atoi(argv[i + 1]));
        } else {
bad_option:
            fprintf(stderr, "bad option or argument missing (%s) see -help\n", argv[i]);
            goto error;
        }
    }

    ff_sfc64_init(&prng_state, 0, 0, 0, 12);
    av_lfg_init(&rand, 1);
    signal(SIGINT, exit_handler);

    for (int i = 0; i < 3; i++) {
        sws[i] = sws_alloc_context();
        if (!sws[i])
            goto error;
        sws[i]->flags = SWS_BILINEAR;
    }

    rgb = av_frame_alloc();
    if (!rgb)
        goto error;
    rgb->width  = opts.w / 12;
    rgb->height = opts.h / 12;
    rgb->format = AV_PIX_FMT_RGBA;
    if (av_frame_get_buffer(rgb, 32) < 0)
        goto error;

    for (int y = 0; y < rgb->height; y++) {
        for (int x = 0; x < rgb->width; x++) {
            for (int c = 0; c < 4; c++)
                rgb->data[0][y * rgb->linesize[0] + x * 4 + c] = av_lfg_get(&rand);
        }
    }

    ref = av_frame_alloc();
    if (!ref)
        goto error;
    ref->width  = opts.w;
    ref->height = opts.h;
    ref->format = AV_PIX_FMT_YUVA444P;

    if (sws_scale_frame(sws[0], ref, rgb) < 0)
        goto error;

    ret = fp ? run_file_tests(ref, fp, opts)
             : run_self_tests(ref, opts);

    /* fall through */
error:
    for (int i = 0; i < 3; i++)
        sws_free_context(&sws[i]);
    av_frame_free(&rgb);
    av_frame_free(&ref);
    if (fp)
        fclose(fp);
    exit_handler(ret);
}
