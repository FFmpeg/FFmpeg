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

#include "config.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "vf_hqdn3d.h"

#define LUT_BITS (depth==16 ? 8 : 4)
#define LOAD(x) (((depth==8 ? src[x] : AV_RN16A(src+(x)*2)) << (16-depth)) + (((1<<(16-depth))-1)>>1))
#define STORE(x,val) (depth==8 ? dst[x] = (val) >> (16-depth)\
                    : AV_WN16A(dst+(x)*2, (val) >> (16-depth)))

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
static void denoise_spatial(HQDN3DContext *hqdn3d,
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
        if (hqdn3d->denoise_row[depth]) {
            hqdn3d->denoise_row[depth](src, dst, line_ant, frame_ant, w, spatial, temporal);
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
static void denoise_depth(HQDN3DContext *hqdn3d,
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
        *frame_ant_ptr = frame_ant = av_malloc(w*h*sizeof(uint16_t));
        for (y = 0; y < h; y++, src += sstride, frame_ant += w)
            for (x = 0; x < w; x++)
                frame_ant[x] = LOAD(x);
        src = frame_src;
        frame_ant = *frame_ant_ptr;
    }

    if (spatial[0])
        denoise_spatial(hqdn3d, src, dst, line_ant, frame_ant,
                        w, h, sstride, dstride, spatial, temporal, depth);
    else
        denoise_temporal(src, dst, frame_ant,
                         w, h, sstride, dstride, temporal, depth);
}

#define denoise(...) \
    switch (hqdn3d->depth) {\
        case  8: denoise_depth(__VA_ARGS__,  8); break;\
        case  9: denoise_depth(__VA_ARGS__,  9); break;\
        case 10: denoise_depth(__VA_ARGS__, 10); break;\
        case 16: denoise_depth(__VA_ARGS__, 16); break;\
    }

static int16_t *precalc_coefs(double dist25, int depth)
{
    int i;
    double gamma, simil, C;
    int16_t *ct = av_malloc((512<<LUT_BITS)*sizeof(int16_t));
    if (!ct)
        return NULL;

    gamma = log(0.25) / log(1.0 - FFMIN(dist25,252.0)/255.0 - 0.00001);

    for (i = -255<<LUT_BITS; i <= 255<<LUT_BITS; i++) {
        double f = ((i<<(9-LUT_BITS)) + (1<<(8-LUT_BITS)) - 1) / 512.0; // midpoint of the bin
        simil = 1.0 - FFABS(f) / 255.0;
        C = pow(simil, gamma) * 256.0 * f;
        ct[(256<<LUT_BITS)+i] = lrint(C);
    }

    ct[0] = !!dist25;
    return ct;
}

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0

static int init(AVFilterContext *ctx, const char *args)
{
    HQDN3DContext *hqdn3d = ctx->priv;
    double lum_spac, lum_tmp, chrom_spac, chrom_tmp;
    double param1, param2, param3, param4;

    lum_spac   = PARAM1_DEFAULT;
    chrom_spac = PARAM2_DEFAULT;
    lum_tmp    = PARAM3_DEFAULT;
    chrom_tmp  = lum_tmp * chrom_spac / lum_spac;

    if (args) {
        switch (sscanf(args, "%lf:%lf:%lf:%lf",
                       &param1, &param2, &param3, &param4)) {
        case 1:
            lum_spac   = param1;
            chrom_spac = PARAM2_DEFAULT * param1 / PARAM1_DEFAULT;
            lum_tmp    = PARAM3_DEFAULT * param1 / PARAM1_DEFAULT;
            chrom_tmp  = lum_tmp * chrom_spac / lum_spac;
            break;
        case 2:
            lum_spac   = param1;
            chrom_spac = param2;
            lum_tmp    = PARAM3_DEFAULT * param1 / PARAM1_DEFAULT;
            chrom_tmp  = lum_tmp * chrom_spac / lum_spac;
            break;
        case 3:
            lum_spac   = param1;
            chrom_spac = param2;
            lum_tmp    = param3;
            chrom_tmp  = lum_tmp * chrom_spac / lum_spac;
            break;
        case 4:
            lum_spac   = param1;
            chrom_spac = param2;
            lum_tmp    = param3;
            chrom_tmp  = param4;
            break;
        }
    }

    hqdn3d->strength[0] = lum_spac;
    hqdn3d->strength[1] = lum_tmp;
    hqdn3d->strength[2] = chrom_spac;
    hqdn3d->strength[3] = chrom_tmp;

    av_log(ctx, AV_LOG_VERBOSE, "ls:%f cs:%f lt:%f ct:%f\n",
           lum_spac, chrom_spac, lum_tmp, chrom_tmp);
    if (lum_spac < 0 || chrom_spac < 0 || isnan(chrom_tmp)) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid negative value for luma or chroma spatial strength, "
               "or resulting value for chroma temporal strength is nan.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    HQDN3DContext *hqdn3d = ctx->priv;

    av_freep(&hqdn3d->coefs[0]);
    av_freep(&hqdn3d->coefs[1]);
    av_freep(&hqdn3d->coefs[2]);
    av_freep(&hqdn3d->coefs[3]);
    av_freep(&hqdn3d->line);
    av_freep(&hqdn3d->frame_prev[0]);
    av_freep(&hqdn3d->frame_prev[1]);
    av_freep(&hqdn3d->frame_prev[2]);
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
        AV_NE( AV_PIX_FMT_YUV420P9BE, AV_PIX_FMT_YUV420P9LE ),
        AV_NE( AV_PIX_FMT_YUV422P9BE, AV_PIX_FMT_YUV422P9LE ),
        AV_NE( AV_PIX_FMT_YUV444P9BE, AV_PIX_FMT_YUV444P9LE ),
        AV_NE( AV_PIX_FMT_YUV420P10BE, AV_PIX_FMT_YUV420P10LE ),
        AV_NE( AV_PIX_FMT_YUV422P10BE, AV_PIX_FMT_YUV422P10LE ),
        AV_NE( AV_PIX_FMT_YUV444P10BE, AV_PIX_FMT_YUV444P10LE ),
        AV_NE( AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_YUV420P16LE ),
        AV_NE( AV_PIX_FMT_YUV422P16BE, AV_PIX_FMT_YUV422P16LE ),
        AV_NE( AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUV444P16LE ),
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    HQDN3DContext *hqdn3d = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int i;

    hqdn3d->hsub  = desc->log2_chroma_w;
    hqdn3d->vsub  = desc->log2_chroma_h;
    hqdn3d->depth = desc->comp[0].depth_minus1+1;

    hqdn3d->line = av_malloc(inlink->w * sizeof(*hqdn3d->line));
    if (!hqdn3d->line)
        return AVERROR(ENOMEM);

    for (i = 0; i < 4; i++) {
        hqdn3d->coefs[i] = precalc_coefs(hqdn3d->strength[i], hqdn3d->depth);
        if (!hqdn3d->coefs[i])
            return AVERROR(ENOMEM);
    }

    if (ARCH_X86)
        ff_hqdn3d_init_x86(hqdn3d);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{
    HQDN3DContext *hqdn3d = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];

    AVFilterBufferRef *out;
    int direct = 0, c;

    if (in->perms & AV_PERM_WRITE) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        if (!out) {
            avfilter_unref_bufferp(&in);
            return AVERROR(ENOMEM);
        }
        avfilter_copy_buffer_ref_props(out, in);
    }

    for (c = 0; c < 3; c++) {
        denoise(hqdn3d, in->data[c], out->data[c],
                hqdn3d->line, &hqdn3d->frame_prev[c],
                in->video->w >> (!!c * hqdn3d->hsub),
                in->video->h >> (!!c * hqdn3d->vsub),
                in->linesize[c], out->linesize[c],
                hqdn3d->coefs[c?2:0], hqdn3d->coefs[c?3:1]);
    }

    if (!direct)
        avfilter_unref_bufferp(&in);

    return ff_filter_frame(outlink, out);
}

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

AVFilter avfilter_vf_hqdn3d = {
    .name          = "hqdn3d",
    .description   = NULL_IF_CONFIG_SMALL("Apply a High Quality 3D Denoiser."),

    .priv_size     = sizeof(HQDN3DContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_hqdn3d_inputs,

    .outputs   = avfilter_vf_hqdn3d_outputs,
};
