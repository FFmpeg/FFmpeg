/*
 * Copyright (c) 2012 Jeremy Tran
 * Copyright (c) 2004 Tobias Diedrich
 * Copyright (c) 2003 Donald A. Graft
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
 * Kernel Deinterlacer
 * Ported from MPlayer libmpcodecs/vf_kerndeint.c.
 */

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int           frame; ///< frame count, starting from 0
    int           thresh, map, order, sharp, twoway;
    int           vsub;
    int           is_packed_rgb;
    uint8_t       *tmp_data    [4];  ///< temporary plane data buffer
    int            tmp_linesize[4];  ///< temporary plane byte linesize
    int            tmp_bwidth  [4];  ///< temporary plane byte width
} KerndeintContext;

#define OFFSET(x) offsetof(KerndeintContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption kerndeint_options[] = {
    { "thresh", "set the threshold", OFFSET(thresh), AV_OPT_TYPE_INT, {.i64=10}, 0, 255, FLAGS },
    { "map",    "set the map", OFFSET(map), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "order",  "set the order", OFFSET(order), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "sharp",  "enable sharpening", OFFSET(sharp), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "twoway", "enable twoway", OFFSET(twoway), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(kerndeint);

static av_cold void uninit(AVFilterContext *ctx)
{
    KerndeintContext *kerndeint = ctx->priv;

    av_free(kerndeint->tmp_data[0]);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_ARGB, AV_PIX_FMT_0RGB,
        AV_PIX_FMT_ABGR, AV_PIX_FMT_0BGR,
        AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB0,
        AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    KerndeintContext *kerndeint = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    kerndeint->is_packed_rgb = av_pix_fmt_desc_get(inlink->format)->flags & AV_PIX_FMT_FLAG_RGB;
    kerndeint->vsub = desc->log2_chroma_h;

    ret = av_image_alloc(kerndeint->tmp_data, kerndeint->tmp_linesize,
                         inlink->w, inlink->h, inlink->format, 16);
    if (ret < 0)
        return ret;
    memset(kerndeint->tmp_data[0], 0, ret);

    if ((ret = av_image_fill_linesizes(kerndeint->tmp_bwidth, inlink->format, inlink->w)) < 0)
        return ret;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpic)
{
    KerndeintContext *kerndeint = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *outpic;
    const uint8_t *prvp;   ///< Previous field's pixel line number n
    const uint8_t *prvpp;  ///< Previous field's pixel line number (n - 1)
    const uint8_t *prvpn;  ///< Previous field's pixel line number (n + 1)
    const uint8_t *prvppp; ///< Previous field's pixel line number (n - 2)
    const uint8_t *prvpnn; ///< Previous field's pixel line number (n + 2)
    const uint8_t *prvp4p; ///< Previous field's pixel line number (n - 4)
    const uint8_t *prvp4n; ///< Previous field's pixel line number (n + 4)

    const uint8_t *srcp;   ///< Current field's pixel line number n
    const uint8_t *srcpp;  ///< Current field's pixel line number (n - 1)
    const uint8_t *srcpn;  ///< Current field's pixel line number (n + 1)
    const uint8_t *srcppp; ///< Current field's pixel line number (n - 2)
    const uint8_t *srcpnn; ///< Current field's pixel line number (n + 2)
    const uint8_t *srcp3p; ///< Current field's pixel line number (n - 3)
    const uint8_t *srcp3n; ///< Current field's pixel line number (n + 3)
    const uint8_t *srcp4p; ///< Current field's pixel line number (n - 4)
    const uint8_t *srcp4n; ///< Current field's pixel line number (n + 4)

    uint8_t *dstp, *dstp_saved;
    const uint8_t *srcp_saved;

    int src_linesize, psrc_linesize, dst_linesize, bwidth;
    int x, y, plane, val, hi, lo, g, h, n = kerndeint->frame++;
    double valf;

    const int thresh = kerndeint->thresh;
    const int order  = kerndeint->order;
    const int map    = kerndeint->map;
    const int sharp  = kerndeint->sharp;
    const int twoway = kerndeint->twoway;

    const int is_packed_rgb = kerndeint->is_packed_rgb;

    outpic = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpic) {
        av_frame_free(&inpic);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(outpic, inpic);
    outpic->interlaced_frame = 0;

    for (plane = 0; plane < 4 && inpic->data[plane] && inpic->linesize[plane]; plane++) {
        h = plane == 0 ? inlink->h : FF_CEIL_RSHIFT(inlink->h, kerndeint->vsub);
        bwidth = kerndeint->tmp_bwidth[plane];

        srcp = srcp_saved = inpic->data[plane];
        src_linesize      = inpic->linesize[plane];
        psrc_linesize     = kerndeint->tmp_linesize[plane];
        dstp = dstp_saved = outpic->data[plane];
        dst_linesize      = outpic->linesize[plane];
        srcp              = srcp_saved + (1 - order) * src_linesize;
        dstp              = dstp_saved + (1 - order) * dst_linesize;

        for (y = 0; y < h; y += 2) {
            memcpy(dstp, srcp, bwidth);
            srcp += 2 * src_linesize;
            dstp += 2 * dst_linesize;
        }

        // Copy through the lines that will be missed below.
        memcpy(dstp_saved + order            * dst_linesize, srcp_saved + (1 -     order) * src_linesize, bwidth);
        memcpy(dstp_saved + (2 + order    )  * dst_linesize, srcp_saved + (3 -     order) * src_linesize, bwidth);
        memcpy(dstp_saved + (h - 2 + order)  * dst_linesize, srcp_saved + (h - 1 - order) * src_linesize, bwidth);
        memcpy(dstp_saved + (h - 4 + order)  * dst_linesize, srcp_saved + (h - 3 - order) * src_linesize, bwidth);

        /* For the other field choose adaptively between using the previous field
           or the interpolant from the current field. */
        prvp   = kerndeint->tmp_data[plane] + 5 * psrc_linesize - (1 - order) * psrc_linesize;
        prvpp  = prvp - psrc_linesize;
        prvppp = prvp - 2 * psrc_linesize;
        prvp4p = prvp - 4 * psrc_linesize;
        prvpn  = prvp + psrc_linesize;
        prvpnn = prvp + 2 * psrc_linesize;
        prvp4n = prvp + 4 * psrc_linesize;

        srcp   = srcp_saved + 5 * src_linesize - (1 - order) * src_linesize;
        srcpp  = srcp - src_linesize;
        srcppp = srcp - 2 * src_linesize;
        srcp3p = srcp - 3 * src_linesize;
        srcp4p = srcp - 4 * src_linesize;

        srcpn  = srcp + src_linesize;
        srcpnn = srcp + 2 * src_linesize;
        srcp3n = srcp + 3 * src_linesize;
        srcp4n = srcp + 4 * src_linesize;

        dstp   = dstp_saved + 5 * dst_linesize - (1 - order) * dst_linesize;

        for (y = 5 - (1 - order); y <= h - 5 - (1 - order); y += 2) {
            for (x = 0; x < bwidth; x++) {
                if (thresh == 0 || n == 0 ||
                    (abs((int)prvp[x]  - (int)srcp[x])  > thresh) ||
                    (abs((int)prvpp[x] - (int)srcpp[x]) > thresh) ||
                    (abs((int)prvpn[x] - (int)srcpn[x]) > thresh)) {
                    if (map) {
                        g = x & ~3;

                        if (is_packed_rgb) {
                            AV_WB32(dstp + g, 0xffffffff);
                            x = g + 3;
                        } else if (inlink->format == AV_PIX_FMT_YUYV422) {
                            // y <- 235, u <- 128, y <- 235, v <- 128
                            AV_WB32(dstp + g, 0xeb80eb80);
                            x = g + 3;
                        } else {
                            dstp[x] = plane == 0 ? 235 : 128;
                        }
                    } else {
                        if (is_packed_rgb) {
                            hi = 255;
                            lo = 0;
                        } else if (inlink->format == AV_PIX_FMT_YUYV422) {
                            hi = x & 1 ? 240 : 235;
                            lo = 16;
                        } else {
                            hi = plane == 0 ? 235 : 240;
                            lo = 16;
                        }

                        if (sharp) {
                            if (twoway) {
                                valf = + 0.526 * ((int)srcpp[x] + (int)srcpn[x])
                                    + 0.170 * ((int)srcp[x] + (int)prvp[x])
                                    - 0.116 * ((int)srcppp[x] + (int)srcpnn[x] + (int)prvppp[x] + (int)prvpnn[x])
                                    - 0.026 * ((int)srcp3p[x] + (int)srcp3n[x])
                                    + 0.031 * ((int)srcp4p[x] + (int)srcp4n[x] + (int)prvp4p[x] + (int)prvp4n[x]);
                            } else {
                                valf = + 0.526 * ((int)srcpp[x] + (int)srcpn[x])
                                    + 0.170 * ((int)prvp[x])
                                    - 0.116 * ((int)prvppp[x] + (int)prvpnn[x])
                                    - 0.026 * ((int)srcp3p[x] + (int)srcp3n[x])
                                    + 0.031 * ((int)prvp4p[x] + (int)prvp4p[x]);
                            }
                            dstp[x] = av_clip(valf, lo, hi);
                        } else {
                            if (twoway) {
                                val = (8 * ((int)srcpp[x] + (int)srcpn[x]) + 2 * ((int)srcp[x] + (int)prvp[x])
                                       - (int)(srcppp[x]) - (int)(srcpnn[x])
                                       - (int)(prvppp[x]) - (int)(prvpnn[x])) >> 4;
                            } else {
                                val = (8 * ((int)srcpp[x] + (int)srcpn[x]) + 2 * ((int)prvp[x])
                                       - (int)(prvppp[x]) - (int)(prvpnn[x])) >> 4;
                            }
                            dstp[x] = av_clip(val, lo, hi);
                        }
                    }
                } else {
                    dstp[x] = srcp[x];
                }
            }
            prvp   += 2 * psrc_linesize;
            prvpp  += 2 * psrc_linesize;
            prvppp += 2 * psrc_linesize;
            prvpn  += 2 * psrc_linesize;
            prvpnn += 2 * psrc_linesize;
            prvp4p += 2 * psrc_linesize;
            prvp4n += 2 * psrc_linesize;
            srcp   += 2 * src_linesize;
            srcpp  += 2 * src_linesize;
            srcppp += 2 * src_linesize;
            srcp3p += 2 * src_linesize;
            srcp4p += 2 * src_linesize;
            srcpn  += 2 * src_linesize;
            srcpnn += 2 * src_linesize;
            srcp3n += 2 * src_linesize;
            srcp4n += 2 * src_linesize;
            dstp   += 2 * dst_linesize;
        }

        srcp = inpic->data[plane];
        dstp = kerndeint->tmp_data[plane];
        av_image_copy_plane(dstp, psrc_linesize, srcp, src_linesize, bwidth, h);
    }

    av_frame_free(&inpic);
    return ff_filter_frame(outlink, outpic);
}

static const AVFilterPad kerndeint_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad kerndeint_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};


AVFilter avfilter_vf_kerndeint = {
    .name          = "kerndeint",
    .description   = NULL_IF_CONFIG_SMALL("Apply kernel deinterlacing to the input."),
    .priv_size     = sizeof(KerndeintContext),
    .priv_class    = &kerndeint_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = kerndeint_inputs,
    .outputs       = kerndeint_outputs,
};
