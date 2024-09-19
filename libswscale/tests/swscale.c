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

#undef HAVE_AV_CONFIG_H
#include "libavutil/cpu.h"
#include "libavutil/pixdesc.h"
#include "libavutil/lfg.h"
#include "libavutil/sfc64.h"
#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"
#include "libavutil/avassert.h"
#include "libavutil/macros.h"

#include "libswscale/swscale.h"

enum {
    WIDTH  = 96,
    HEIGHT = 96,
};

struct options {
    enum AVPixelFormat src_fmt;
    enum AVPixelFormat dst_fmt;
    double prob;
};

struct mode {
    SwsFlags flags;
    SwsDither dither;
};

const int dst_w[] = { WIDTH,  WIDTH  - WIDTH  / 3, WIDTH  + WIDTH  / 3 };
const int dst_h[] = { HEIGHT, HEIGHT - HEIGHT / 3, HEIGHT + HEIGHT / 3 };

const struct mode modes[] = {
    { SWS_FAST_BILINEAR },
    { SWS_BILINEAR },
    { SWS_BICUBIC },
    { SWS_X | SWS_BITEXACT },
    { SWS_POINT },
    { SWS_AREA | SWS_ACCURATE_RND },
    { SWS_BICUBIC | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP },
    {0}, // test defaults
};

static FFSFC64 prng_state;
static SwsContext *sws[3]; /* reused between tests for efficiency */

static int fmt_comps(enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    int comps = desc->nb_components >= 3 ? 0b111 : 0b1;
    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA)
        comps |= 0b1000;
    return comps;
}

static void get_mse(int mse[4], const AVFrame *a, const AVFrame *b, int comps)
{
    av_assert1(a->format == AV_PIX_FMT_YUVA420P);
    av_assert1(b->format == a->format);
    av_assert1(b->width == a->width && b->height == a->height);

    for (int p = 0; p < 4; p++) {
        const int is_chroma = p == 1 || p == 2;
        const int stride_a = a->linesize[p];
        const int stride_b = b->linesize[p];
        const int w = (a->width + is_chroma) >> is_chroma;
        const int h = (a->height + is_chroma) >> is_chroma;
        uint64_t sum = 0;

        if (comps & (1 << p)) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int d = a->data[p][y * stride_a + x] - b->data[p][y * stride_b + x];
                    sum += d * d;
                }
            }
        } else {
            const int ref = is_chroma ? 128 : 0xFF;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int d = a->data[p][y * stride_a + x] - ref;
                    sum += d * d;
                }
            }
        }

        mse[p] = sum / (w * h);
    }
}

static int scale_legacy(AVFrame *dst, const AVFrame *src, struct mode mode)
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

    ret = sws_init_context(sws_legacy, NULL, NULL);
    if (!ret)
        ret = sws_scale_frame(sws_legacy, dst, src);

    sws_freeContext(sws_legacy);
    return ret;
}

/* Runs a series of ref -> src -> dst -> out, and compares out vs ref */
static int run_test(enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                    int dst_w, int dst_h, struct mode mode, const AVFrame *ref,
                    const int mse_ref[4])
{
    AVFrame *src = NULL, *dst = NULL, *out = NULL;
    int mse[4], mse_sws[4], ret = -1;
    const int comps = fmt_comps(src_fmt) & fmt_comps(dst_fmt);

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
    if (sws_scale_frame(sws[1], dst, src) < 0) {
        fprintf(stderr, "Failed %s ---> %s\n", av_get_pix_fmt_name(src->format),
                av_get_pix_fmt_name(dst->format));
        goto error;
    }

    if (sws_scale_frame(sws[2], out, dst) < 0) {
        fprintf(stderr, "Failed %s ---> %s\n", av_get_pix_fmt_name(dst->format),
                av_get_pix_fmt_name(out->format));
        goto error;
    }

    get_mse(mse, out, ref, comps);
    printf("%s %dx%d -> %s %3dx%3d, flags=%u dither=%u, "
           "MSE={%5d %5d %5d %5d}\n",
           av_get_pix_fmt_name(src->format), src->width, src->height,
           av_get_pix_fmt_name(dst->format), dst->width, dst->height,
           mode.flags, mode.dither,
           mse[0], mse[1], mse[2], mse[3]);

    if (!mse_ref) {
        /* Compare against the legacy swscale API as a reference */
        if (scale_legacy(dst, src, mode) < 0) {
            fprintf(stderr, "Failed ref %s ---> %s\n", av_get_pix_fmt_name(src->format),
                    av_get_pix_fmt_name(dst->format));
            goto error;
        }

        if (sws_scale_frame(sws[2], out, dst) < 0)
            goto error;

        get_mse(mse_sws, out, ref, comps);
        mse_ref = mse_sws;
    }

    for (int i = 0; i < 4; i++) {
        if (mse[i] > mse_ref[i]) {
            int bad = mse[i] > mse_ref[i] * 1.02 + 1;
            printf("\033[1;31m  %s, ref MSE={%5d %5d %5d %5d}\033[0m\n",
                   bad ? "WORSE" : "worse",
                   mse_ref[0], mse_ref[1], mse_ref[2], mse_ref[3]);
            if (bad)
                goto error;
            break;
        }
    }

    fflush(stdout);
    ret = 0; /* fall through */
 error:
    av_frame_free(&src);
    av_frame_free(&dst);
    av_frame_free(&out);
    return ret;
}

static int run_self_tests(const AVFrame *ref, struct options opts)
{
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
        if (!sws_test_format(src_fmt, 0) || !sws_test_format(src_fmt, 1))
            continue;
        for (dst_fmt = dst_fmt_min; dst_fmt <= dst_fmt_max; dst_fmt++) {
            if (!sws_test_format(dst_fmt, 0) || !sws_test_format(dst_fmt, 1))
                continue;
            for (int h = 0; h < FF_ARRAY_ELEMS(dst_h); h++)
                for (int w = 0; w < FF_ARRAY_ELEMS(dst_w); w++)
                    for (int m = 0; m < FF_ARRAY_ELEMS(modes); m++) {
                        if (ff_sfc64_get(&prng_state) > UINT64_MAX * opts.prob)
                            continue;
                        if (run_test(src_fmt, dst_fmt, dst_w[w], dst_h[h],
                                     modes[m], ref, NULL) < 0)
                            return -1;
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
        char src_fmt_str[20], dst_fmt_str[20];
        enum AVPixelFormat src_fmt;
        enum AVPixelFormat dst_fmt;
        int sw, sh, dw, dh, mse[4];
        struct mode mode;

        ret = sscanf(buf,
                     " %20s %dx%d -> %20s %dx%d, flags=%u dither=%u, "
                     "MSE={%d %d %d %d}\n",
                     src_fmt_str, &sw, &sh, dst_fmt_str, &dw, &dh,
                     &mode.flags, &mode.dither,
                     &mse[0], &mse[1], &mse[2], &mse[3]);
        if (ret != 13) {
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

        if (run_test(src_fmt, dst_fmt, dw, dh, mode, ref, mse) < 0)
            return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct options opts = {
        .src_fmt = AV_PIX_FMT_NONE,
        .dst_fmt = AV_PIX_FMT_NONE,
        .prob = 1.0,
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
                    "   -cpuflags <cpuflags>\n"
                    "       Uses the specified cpuflags in the tests\n"
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
        } else if (!strcmp(argv[i], "-p")) {
            opts.prob = atof(argv[i + 1]);
        } else {
bad_option:
            fprintf(stderr, "bad option or argument missing (%s) see -help\n", argv[i]);
            goto error;
        }
    }

    ff_sfc64_init(&prng_state, 0, 0, 0, 12);
    av_lfg_init(&rand, 1);

    for (int i = 0; i < 3; i++) {
        sws[i] = sws_alloc_context();
        if (!sws[i])
            goto error;
        sws[i]->flags = SWS_BILINEAR;
    }

    rgb = av_frame_alloc();
    if (!rgb)
        goto error;
    rgb->width  = WIDTH  / 12;
    rgb->height = HEIGHT / 12;
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
    ref->width  = WIDTH;
    ref->height = HEIGHT;
    ref->format = AV_PIX_FMT_YUVA420P;

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
    return ret;
}
