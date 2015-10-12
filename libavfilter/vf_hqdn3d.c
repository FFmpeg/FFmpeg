/*
 * Copyright (c) 2003 Daniel Moreno <comac AT comac DOT darktech DOT org>
 * Copyright (c) 2010 Baptiste Coudurier
 * Copyright (c) 2012 Loren Merritt
 *
 * This file is part of FFmpeg, ported from MPlayer.
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
 * high quality 3d video denoiser, ported from MPlayer
 * libmpcodecs/vf_hqdn3d.c.
 */

#include <float.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "vf_hqdn3d.h"

#define LUT_BITS (depth==16 ? 8 : 4)
#define LOAD(x) (((depth == 8 ? src[x] : AV_RN16A(src + (x) * 2)) << (16 - depth))\
                 + (((1 << (16 - depth)) - 1) >> 1))
#define STORE(x,val) (depth == 8 ? dst[x] = (val) >> (16 - depth) : \
                                   AV_WN16A(dst + (x) * 2, (val) >> (16 - depth)))

av_always_inline
static uint32_t lowpass(int prev, int cur, int16_t *coef, int depth)
{
    int d = (prev - cur) >> (8 - LUT_BITS);
    return cur + coef[d];
}

av_always_inline
static void denoise_temporal(uint8_t *src, uint8_t *dst,
                             uint16_t *frame_ant,
                             int w, int h, int sstride, int dstride,
                             int16_t *temporal, int depth)
{
    long x, y;
    uint32_t tmp;

    temporal += 256 << LUT_BITS;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            frame_ant[x] = tmp = lowpass(frame_ant[x], LOAD(x), temporal, depth);
            STORE(x, tmp);
        }
        src += sstride;
        dst += dstride;
        frame_ant += w;
    }
}

av_always_inline
static void denoise_spatial(HQDN3DContext *s,
                            uint8_t *src, uint8_t *dst,
                            uint16_t *line_ant, uint16_t *frame_ant,
                            int w, int h, int sstride, int dstride,
                            int16_t *spatial, int16_t *temporal, int depth)
{
    long x, y;
    uint32_t pixel_ant;
    uint32_t tmp;

    spatial  += 256 << LUT_BITS;
    temporal += 256 << LUT_BITS;

    /* First line has no top neighbor. Only left one for each tmp and
     * last frame */
    pixel_ant = LOAD(0);
    for (x = 0; x < w; x++) {
        line_ant[x] = tmp = pixel_ant = lowpass(pixel_ant, LOAD(x), spatial, depth);
        frame_ant[x] = tmp = lowpass(frame_ant[x], tmp, temporal, depth);
        STORE(x, tmp);
    }

    for (y = 1; y < h; y++) {
        src += sstride;
        dst += dstride;
        frame_ant += w;
        if (s->denoise_row[depth]) {
            s->denoise_row[depth](src, dst, line_ant, frame_ant, w, spatial, temporal);
            continue;
        }
        pixel_ant = LOAD(0);
        for (x = 0; x < w-1; x++) {
            line_ant[x] = tmp = lowpass(line_ant[x], pixel_ant, spatial, depth);
            pixel_ant = lowpass(pixel_ant, LOAD(x+1), spatial, depth);
            frame_ant[x] = tmp = lowpass(frame_ant[x], tmp, temporal, depth);
            STORE(x, tmp);
        }
        line_ant[x] = tmp = lowpass(line_ant[x], pixel_ant, spatial, depth);
        frame_ant[x] = tmp = lowpass(frame_ant[x], tmp, temporal, depth);
        STORE(x, tmp);
    }
}

av_always_inline
static int denoise_depth(HQDN3DContext *s,
                         uint8_t *src, uint8_t *dst,
                         uint16_t *line_ant, uint16_t **frame_ant_ptr,
                         int w, int h, int sstride, int dstride,
                         int16_t *spatial, int16_t *temporal, int depth)
{
    // FIXME: For 16bit depth, frame_ant could be a pointer to the previous
    // filtered frame rather than a separate buffer.
    long x, y;
    uint16_t *frame_ant = *frame_ant_ptr;
    if (!frame_ant) {
        uint8_t *frame_src = src;
        *frame_ant_ptr = frame_ant = av_malloc_array(w, h*sizeof(uint16_t));
        if (!frame_ant)
            return AVERROR(ENOMEM);
        for (y = 0; y < h; y++, src += sstride, frame_ant += w)
            for (x = 0; x < w; x++)
                frame_ant[x] = LOAD(x);
        src = frame_src;
        frame_ant = *frame_ant_ptr;
    }

    if (spatial[0])
        denoise_spatial(s, src, dst, line_ant, frame_ant,
                        w, h, sstride, dstride, spatial, temporal, depth);
    else
        denoise_temporal(src, dst, frame_ant,
                         w, h, sstride, dstride, temporal, depth);
    emms_c();
    return 0;
}

#define denoise(...)                                                          \
    do {                                                                      \
        int ret = AVERROR_BUG;                                                \
        switch (s->depth) {                                                   \
            case  8: ret = denoise_depth(__VA_ARGS__,  8); break;             \
            case  9: ret = denoise_depth(__VA_ARGS__,  9); break;             \
            case 10: ret = denoise_depth(__VA_ARGS__, 10); break;             \
            case 16: ret = denoise_depth(__VA_ARGS__, 16); break;             \
        }                                                                     \
        if (ret < 0) {                                                        \
            av_frame_free(&out);                                              \
            if (!direct)                                                      \
                av_frame_free(&in);                                           \
            return ret;                                                       \
        }                                                                     \
    } while (0)

static int16_t *precalc_coefs(double dist25, int depth)
{
    int i;
    double gamma, simil, C;
    int16_t *ct = av_malloc((512<<LUT_BITS)*sizeof(int16_t));
    if (!ct)
        return NULL;

    gamma = log(0.25) / log(1.0 - FFMIN(dist25,252.0)/255.0 - 0.00001);

    for (i = -256<<LUT_BITS; i < 256<<LUT_BITS; i++) {
        double f = ((i<<(9-LUT_BITS)) + (1<<(8-LUT_BITS)) - 1) / 512.0; // midpoint of the bin
        simil = FFMAX(0, 1.0 - fabs(f) / 255.0);
        C = pow(simil, gamma) * 256.0 * f;
        ct[(256<<LUT_BITS)+i] = lrint(C);
    }

    ct[0] = !!dist25;
    return ct;
}

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0

static av_cold int init(AVFilterContext *ctx)
{
    HQDN3DContext *s = ctx->priv;

    if (!s->strength[LUMA_SPATIAL])
        s->strength[LUMA_SPATIAL] = PARAM1_DEFAULT;
    if (!s->strength[CHROMA_SPATIAL])
        s->strength[CHROMA_SPATIAL] = PARAM2_DEFAULT * s->strength[LUMA_SPATIAL] / PARAM1_DEFAULT;
    if (!s->strength[LUMA_TMP])
        s->strength[LUMA_TMP]   = PARAM3_DEFAULT * s->strength[LUMA_SPATIAL] / PARAM1_DEFAULT;
    if (!s->strength[CHROMA_TMP])
        s->strength[CHROMA_TMP] = s->strength[LUMA_TMP] * s->strength[CHROMA_SPATIAL] / s->strength[LUMA_SPATIAL];

    av_log(ctx, AV_LOG_VERBOSE, "ls:%f cs:%f lt:%f ct:%f\n",
           s->strength[LUMA_SPATIAL], s->strength[CHROMA_SPATIAL],
           s->strength[LUMA_TMP], s->strength[CHROMA_TMP]);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HQDN3DContext *s = ctx->priv;

    av_freep(&s->coefs[0]);
    av_freep(&s->coefs[1]);
    av_freep(&s->coefs[2]);
    av_freep(&s->coefs[3]);
    av_freep(&s->line);
    av_freep(&s->frame_prev[0]);
    av_freep(&s->frame_prev[1]);
    av_freep(&s->frame_prev[2]);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUV420P9,
        AV_PIX_FMT_YUV422P9,
        AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10,
        AV_PIX_FMT_YUV422P10,
        AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P16,
        AV_PIX_FMT_YUV422P16,
        AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    HQDN3DContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int i;

    uninit(inlink->dst);

    s->hsub  = desc->log2_chroma_w;
    s->vsub  = desc->log2_chroma_h;
    s->depth = desc->comp[0].depth;

    s->line = av_malloc_array(inlink->w, sizeof(*s->line));
    if (!s->line)
        return AVERROR(ENOMEM);

    for (i = 0; i < 4; i++) {
        s->coefs[i] = precalc_coefs(s->strength[i], s->depth);
        if (!s->coefs[i])
            return AVERROR(ENOMEM);
    }

    if (ARCH_X86)
        ff_hqdn3d_init_x86(s);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    HQDN3DContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out;
    int c, direct = av_frame_is_writable(in) && !ctx->is_disabled;

    if (direct) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(out, in);
    }

    for (c = 0; c < 3; c++) {
        denoise(s, in->data[c], out->data[c],
                s->line, &s->frame_prev[c],
                FF_CEIL_RSHIFT(in->width,  (!!c * s->hsub)),
                FF_CEIL_RSHIFT(in->height, (!!c * s->vsub)),
                in->linesize[c], out->linesize[c],
                s->coefs[c ? CHROMA_SPATIAL : LUMA_SPATIAL],
                s->coefs[c ? CHROMA_TMP     : LUMA_TMP]);
    }

    if (ctx->is_disabled) {
        av_frame_free(&out);
        return ff_filter_frame(outlink, in);
    }

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(HQDN3DContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption hqdn3d_options[] = {
    { "luma_spatial",   "spatial luma strength",    OFFSET(strength[LUMA_SPATIAL]),   AV_OPT_TYPE_DOUBLE, { .dbl = 0.0 }, 0, DBL_MAX, FLAGS },
    { "chroma_spatial", "spatial chroma strength",  OFFSET(strength[CHROMA_SPATIAL]), AV_OPT_TYPE_DOUBLE, { .dbl = 0.0 }, 0, DBL_MAX, FLAGS },
    { "luma_tmp",       "temporal luma strength",   OFFSET(strength[LUMA_TMP]),       AV_OPT_TYPE_DOUBLE, { .dbl = 0.0 }, 0, DBL_MAX, FLAGS },
    { "chroma_tmp",     "temporal chroma strength", OFFSET(strength[CHROMA_TMP]),     AV_OPT_TYPE_DOUBLE, { .dbl = 0.0 }, 0, DBL_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(hqdn3d);

static const AVFilterPad avfilter_vf_hqdn3d_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};


static const AVFilterPad avfilter_vf_hqdn3d_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO
    },
    { NULL }
};

AVFilter ff_vf_hqdn3d = {
    .name          = "hqdn3d",
    .description   = NULL_IF_CONFIG_SMALL("Apply a High Quality 3D Denoiser."),
    .priv_size     = sizeof(HQDN3DContext),
    .priv_class    = &hqdn3d_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_hqdn3d_inputs,
    .outputs       = avfilter_vf_hqdn3d_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
