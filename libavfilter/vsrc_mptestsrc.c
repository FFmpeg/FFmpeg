/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * MP test source, ported from MPlayer libmpcodecs/vf_test.c
 */

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"
#include "video.h"

#define WIDTH 512
#define HEIGHT 512

enum test_type {
    TEST_DC_LUMA,
    TEST_DC_CHROMA,
    TEST_FREQ_LUMA,
    TEST_FREQ_CHROMA,
    TEST_AMP_LUMA,
    TEST_AMP_CHROMA,
    TEST_CBP,
    TEST_MV,
    TEST_RING1,
    TEST_RING2,
    TEST_ALL,
    TEST_NB
};

typedef struct MPTestContext {
    const AVClass *class;
    unsigned int frame_nb;
    AVRational time_base;
    int64_t pts, max_pts;
    int hsub, vsub;
    char *size, *rate, *duration;
    enum test_type test;
} MPTestContext;

#define OFFSET(x) offsetof(MPTestContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption mptestsrc_options[]= {
    { "rate",     "set video rate",     OFFSET(rate),     AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "r",        "set video rate",     OFFSET(rate),     AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "duration", "set video duration", OFFSET(duration), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "d",        "set video duration", OFFSET(duration), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },

    { "test", "set test to perform", OFFSET(test),  AV_OPT_TYPE_INT,   {.i64=TEST_ALL}, 0, INT_MAX, FLAGS, "test" },
    { "t",    "set test to perform", OFFSET(test),  AV_OPT_TYPE_INT,   {.i64=TEST_ALL}, 0, INT_MAX, FLAGS, "test" },
    { "dc_luma",     "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_DC_LUMA},     INT_MIN, INT_MAX, FLAGS, "test" },
    { "dc_chroma",   "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_DC_CHROMA},   INT_MIN, INT_MAX, FLAGS, "test" },
    { "freq_luma",   "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_FREQ_LUMA},   INT_MIN, INT_MAX, FLAGS, "test" },
    { "freq_chroma", "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_FREQ_CHROMA}, INT_MIN, INT_MAX, FLAGS, "test" },
    { "amp_luma",    "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_AMP_LUMA},    INT_MIN, INT_MAX, FLAGS, "test" },
    { "amp_chroma",  "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_AMP_CHROMA},  INT_MIN, INT_MAX, FLAGS, "test" },
    { "cbp",         "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_CBP},         INT_MIN, INT_MAX, FLAGS, "test" },
    { "mv",          "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_MV},          INT_MIN, INT_MAX, FLAGS, "test" },
    { "ring1",       "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_RING1},       INT_MIN, INT_MAX, FLAGS, "test" },
    { "ring2",       "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_RING2},       INT_MIN, INT_MAX, FLAGS, "test" },
    { "all",         "", 0, AV_OPT_TYPE_CONST, {.i64=TEST_ALL},         INT_MIN, INT_MAX, FLAGS, "test" },

    { NULL },
};

AVFILTER_DEFINE_CLASS(mptestsrc);

static double c[64];

static void init_idct(void)
{
    int i, j;

    for (i = 0; i < 8; i++) {
        double s = i == 0 ? sqrt(0.125) : 0.5;

        for (j = 0; j < 8; j++)
            c[i*8+j] = s*cos((M_PI/8.0)*i*(j+0.5));
    }
}

static void idct(uint8_t *dst, int dst_linesize, int src[64])
{
    int i, j, k;
    double tmp[64];

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            double sum = 0.0;

            for (k = 0; k < 8; k++)
                sum += c[k*8+j] * src[8*i+k];

            tmp[8*i+j] = sum;
        }
    }

    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            double sum = 0.0;

            for (k = 0; k < 8; k++)
                sum += c[k*8+i]*tmp[8*k+j];

            dst[dst_linesize*i + j] = av_clip((int)floor(sum+0.5), 0, 255);
        }
    }
}

static void draw_dc(uint8_t *dst, int dst_linesize, int color, int w, int h)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            dst[x + y*dst_linesize] = color;
}

static void draw_basis(uint8_t *dst, int dst_linesize, int amp, int freq, int dc)
{
    int src[64];

    memset(src, 0, 64*sizeof(int));
    src[0] = dc;
    if (amp)
        src[freq] = amp;
    idct(dst, dst_linesize, src);
}

static void draw_cbp(uint8_t *dst[3], int dst_linesize[3], int cbp, int amp, int dc)
{
    if (cbp&1)  draw_basis(dst[0]                    , dst_linesize[0], amp, 1, dc);
    if (cbp&2)  draw_basis(dst[0]+8                  , dst_linesize[0], amp, 1, dc);
    if (cbp&4)  draw_basis(dst[0]+  8*dst_linesize[0], dst_linesize[0], amp, 1, dc);
    if (cbp&8)  draw_basis(dst[0]+8+8*dst_linesize[0], dst_linesize[0], amp, 1, dc);
    if (cbp&16) draw_basis(dst[1]                    , dst_linesize[1], amp, 1, dc);
    if (cbp&32) draw_basis(dst[2]                    , dst_linesize[2], amp, 1, dc);
}

static void dc_test(uint8_t *dst, int dst_linesize, int w, int h, int off)
{
    const int step = FFMAX(256/(w*h/256), 1);
    int x, y, color = off;

    for (y = 0; y < h; y += 16) {
        for (x = 0; x < w; x += 16) {
            draw_dc(dst + x + y*dst_linesize, dst_linesize, color, 8, 8);
            color += step;
        }
    }
}

static void freq_test(uint8_t *dst, int dst_linesize, int off)
{
    int x, y, freq = 0;

    for (y = 0; y < 8*16; y += 16) {
        for (x = 0; x < 8*16; x += 16) {
            draw_basis(dst + x + y*dst_linesize, dst_linesize, 4*(96+off), freq, 128*8);
            freq++;
        }
    }
}

static void amp_test(uint8_t *dst, int dst_linesize, int off)
{
    int x, y, amp = off;

    for (y = 0; y < 16*16; y += 16) {
        for (x = 0; x < 16*16; x += 16) {
            draw_basis(dst + x + y*dst_linesize, dst_linesize, 4*amp, 1, 128*8);
            amp++;
        }
    }
}

static void cbp_test(uint8_t *dst[3], int dst_linesize[3], int off)
{
    int x, y, cbp = 0;

    for (y = 0; y < 16*8; y += 16) {
        for (x = 0; x < 16*8; x += 16) {
            uint8_t *dst1[3];
            dst1[0] = dst[0] + x*2 + y*2*dst_linesize[0];
            dst1[1] = dst[1] + x   + y*  dst_linesize[1];
            dst1[2] = dst[2] + x   + y*  dst_linesize[2];

            draw_cbp(dst1, dst_linesize, cbp, (64+off)*4, 128*8);
            cbp++;
        }
    }
}

static void mv_test(uint8_t *dst, int dst_linesize, int off)
{
    int x, y;

    for (y = 0; y < 16*16; y++) {
        if (y&16)
            continue;
        for (x = 0; x < 16*16; x++)
            dst[x + y*dst_linesize] = x + off*8/(y/32+1);
    }
}

static void ring1_test(uint8_t *dst, int dst_linesize, int off)
{
    int x, y, color = 0;

    for (y = off; y < 16*16; y += 16) {
        for (x = off; x < 16*16; x += 16) {
            draw_dc(dst + x + y*dst_linesize, dst_linesize, ((x+y)&16) ? color : -color, 16, 16);
            color++;
        }
    }
}

static void ring2_test(uint8_t *dst, int dst_linesize, int off)
{
    int x, y;

    for (y = 0; y < 16*16; y++) {
        for (x = 0; x < 16*16; x++) {
            double d = sqrt((x-8*16)*(x-8*16) + (y-8*16)*(y-8*16));
            double r = d/20 - (int)(d/20);
            if (r < off/30.0) {
                dst[x + y*dst_linesize]     = 255;
                dst[x + y*dst_linesize+256] = 0;
            } else {
                dst[x + y*dst_linesize]     = x;
                dst[x + y*dst_linesize+256] = x;
            }
        }
    }
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    MPTestContext *test = ctx->priv;
    AVRational frame_rate_q;
    int64_t duration = -1;
    int ret;

    test->class = &mptestsrc_class;
    av_opt_set_defaults(test);

    if ((ret = (av_set_options_string(test, args, "=", ":"))) < 0)
        return ret;

    if ((ret = av_parse_video_rate(&frame_rate_q, test->rate)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: '%s'\n", test->rate);
        return ret;
    }

    if ((test->duration) && (ret = av_parse_time(&duration, test->duration, 1)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid duration: '%s'\n", test->duration);
        return ret;
    }

    test->time_base.num = frame_rate_q.den;
    test->time_base.den = frame_rate_q.num;
    test->max_pts = duration >= 0 ?
        av_rescale_q(duration, AV_TIME_BASE_Q, test->time_base) : -1;
    test->frame_nb = 0;
    test->pts = 0;

    av_log(ctx, AV_LOG_VERBOSE, "rate:%d/%d duration:%f\n",
           frame_rate_q.num, frame_rate_q.den,
           duration < 0 ? -1 : test->max_pts * av_q2d(test->time_base));
    init_idct();

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MPTestContext *test = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(outlink->format);

    test->hsub = pix_desc->log2_chroma_w;
    test->vsub = pix_desc->log2_chroma_h;

    outlink->w = WIDTH;
    outlink->h = HEIGHT;
    outlink->time_base = test->time_base;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    MPTestContext *test = outlink->src->priv;
    AVFilterBufferRef *picref;
    int w = WIDTH, h = HEIGHT, ch = h>>test->vsub;
    unsigned int frame = test->frame_nb;
    enum test_type tt = test->test;

    if (test->max_pts >= 0 && test->pts > test->max_pts)
        return AVERROR_EOF;
    picref = ff_get_video_buffer(outlink, AV_PERM_WRITE, w, h);
    picref->pts = test->pts++;

    // clean image
    memset(picref->data[0], 0,   picref->linesize[0] * h);
    memset(picref->data[1], 128, picref->linesize[1] * ch);
    memset(picref->data[2], 128, picref->linesize[2] * ch);

    if (tt == TEST_ALL && frame%30) /* draw a black frame at the beginning of each test */
        tt = (frame/30)%(TEST_NB-1);

    switch (tt) {
    case TEST_DC_LUMA:       dc_test(picref->data[0], picref->linesize[0], 256, 256, frame%30); break;
    case TEST_DC_CHROMA:     dc_test(picref->data[1], picref->linesize[1], 256, 256, frame%30); break;
    case TEST_FREQ_LUMA:   freq_test(picref->data[0], picref->linesize[0], frame%30); break;
    case TEST_FREQ_CHROMA: freq_test(picref->data[1], picref->linesize[1], frame%30); break;
    case TEST_AMP_LUMA:     amp_test(picref->data[0], picref->linesize[0], frame%30); break;
    case TEST_AMP_CHROMA:   amp_test(picref->data[1], picref->linesize[1], frame%30); break;
    case TEST_CBP:          cbp_test(picref->data   , picref->linesize   , frame%30); break;
    case TEST_MV:            mv_test(picref->data[0], picref->linesize[0], frame%30); break;
    case TEST_RING1:      ring1_test(picref->data[0], picref->linesize[0], frame%30); break;
    case TEST_RING2:      ring2_test(picref->data[0], picref->linesize[0], frame%30); break;
    }

    test->frame_nb++;
    ff_filter_frame(outlink, picref);

    return 0;
}

static const AVFilterPad mptestsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter avfilter_vsrc_mptestsrc = {
    .name      = "mptestsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate various test pattern."),
    .priv_size = sizeof(MPTestContext),
    .init      = init,

    .query_formats   = query_formats,

    .inputs         = NULL,
    .outputs        = mptestsrc_outputs,
    .priv_class     = &mptestsrc_class,
};
