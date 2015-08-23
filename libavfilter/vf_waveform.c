/*
 * Copyright (c) 2012-2015 Paul B Mahol
 * Copyright (c) 2013 Marton Balint
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

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct WaveformContext {
    const AVClass *class;
    int            mode;
    int            ncomp;
    int            pcomp;
    const uint8_t  *bg_color;
    int            intensity;
    int            mirror;
    int            display;
    int            envelope;
    int            estart[4];
    int            eend[4];
    int            *emax[4];
    int            *emin[4];
    const AVPixFmtDescriptor *desc;
} WaveformContext;

#define OFFSET(x) offsetof(WaveformContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption waveform_options[] = {
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "mode" },
    { "m",    "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "mode" },
        { "row",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "mode" },
        { "column", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "mode" },
    { "intensity", "set intensity", OFFSET(intensity), AV_OPT_TYPE_INT, {.i64=10}, 1, 255, FLAGS },
    { "i",         "set intensity", OFFSET(intensity), AV_OPT_TYPE_INT, {.i64=10}, 1, 255, FLAGS },
    { "mirror", "set mirroring", OFFSET(mirror), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS },
    { "r",      "set mirroring", OFFSET(mirror), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS },
    { "display", "set display mode", OFFSET(display), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "display" },
    { "d",       "set display mode", OFFSET(display), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "display" },
        { "overlay", NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "display" },
        { "parade",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "display" },
    { "components", "set components to display", OFFSET(pcomp), AV_OPT_TYPE_INT, {.i64=1}, 1, 15, FLAGS },
    { "c",          "set components to display", OFFSET(pcomp), AV_OPT_TYPE_INT, {.i64=1}, 1, 15, FLAGS },
    { "envelope", "set envelope to display", OFFSET(envelope), AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "envelope" },
    { "e",        "set envelope to display", OFFSET(envelope), AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "envelope" },
        { "none",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "envelope" },
        { "instant",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "envelope" },
        { "peak",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "envelope" },
        { "peak+instant", NULL, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, FLAGS, "envelope" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(waveform);

static const enum AVPixelFormat pix_fmts[] = {
     AV_PIX_FMT_GBRP,     AV_PIX_FMT_GBRAP,
     AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
     AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
     AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
     AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
     AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
     AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
     AV_PIX_FMT_GRAY8,
     AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *fmts_list;

    fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static const uint8_t black_yuva_color[4] = { 0, 127, 127, 255 };
static const uint8_t black_gbrp_color[4] = { 0, 0, 0, 255 };

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    WaveformContext *s = ctx->priv;

    s->desc  = av_pix_fmt_desc_get(inlink->format);
    s->ncomp = s->desc->nb_components;

    switch (inlink->format) {
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
        s->bg_color = black_gbrp_color;
        break;
    default:
        s->bg_color = black_yuva_color;
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    WaveformContext *s = ctx->priv;
    int comp = 0, i, j = 0, p, size, shift;

    for (i = 0; i < s->ncomp; i++) {
        if ((1 << i) & s->pcomp)
            comp++;
    }

    for (p = 0; p < 4; p++) {
        av_freep(&s->emax[p]);
        av_freep(&s->emin[p]);
    }

    if (s->mode) {
        outlink->h = 256 * FFMAX(comp * s->display, 1);
        size = inlink->w * sizeof(int);
    } else {
        outlink->w = 256 * FFMAX(comp * s->display, 1);
        size = inlink->h * sizeof(int);
    }

    for (p = 0; p < 4; p++) {
        const int is_chroma = (p == 1 || p == 2);
        const int shift_w = (is_chroma ? s->desc->log2_chroma_w : 0);
        const int shift_h = (is_chroma ? s->desc->log2_chroma_h : 0);
        const int plane = s->desc->comp[p].plane;
        int offset;

        if (!((1 << p) & s->pcomp))
            continue;

        shift = s->mode ? shift_h : shift_w;

        s->emax[plane] = av_malloc(size);
        s->emin[plane] = av_malloc(size);

        if (!s->emin[plane] || !s->emax[plane])
            return AVERROR(ENOMEM);

        offset = j++ * 256 * s->display;
        s->estart[plane] = offset >> shift;
        s->eend[plane]   = (offset + 255) >> shift;
        for (i = 0; i < size / sizeof(int); i++) {
            s->emax[plane][i] = s->estart[plane];
            s->emin[plane][i] = s->eend[plane];
        }
    }

    outlink->sample_aspect_ratio = (AVRational){1,1};

    return 0;
}

static void gen_waveform(WaveformContext *s, AVFrame *in, AVFrame *out,
                         int component, int intensity, int offset, int col_mode)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const int is_chroma = (component == 1 || component == 2);
    const int shift_w = (is_chroma ? s->desc->log2_chroma_w : 0);
    const int shift_h = (is_chroma ? s->desc->log2_chroma_h : 0);
    const int src_linesize = in->linesize[plane];
    const int dst_linesize = out->linesize[plane];
    const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);
    const int max = 255 - intensity;
    const int src_h = FF_CEIL_RSHIFT(in->height, shift_h);
    const int src_w = FF_CEIL_RSHIFT(in->width, shift_w);
    const uint8_t *src_data = in->data[plane];
    uint8_t *dst_data = out->data[plane] + (col_mode ? (offset >> shift_h) * dst_linesize : offset >> shift_w);
    uint8_t * const dst_bottom_line = dst_data + dst_linesize * ((256 >> shift_h) - 1);
    uint8_t * const dst_line = (mirror ? dst_bottom_line : dst_data);
    const uint8_t *p;
    uint8_t *dst;
    int y;

    if (!col_mode && mirror)
        dst_data += 256 >> shift_w;
    for (y = 0; y < src_h; y++) {
        const uint8_t *src_data_end = src_data + src_w;
        dst = dst_line;
        for (p = src_data; p < src_data_end; p++) {
            uint8_t *target;
            if (col_mode) {
                target = dst++ + dst_signed_linesize * (*p >> shift_h);
            } else {
                if (mirror)
                    target = dst_data - (*p >> shift_w) - 1;
                else
                    target = dst_data + (*p >> shift_w);
            }
            if (*target <= max)
                *target += intensity;
            else
                *target = 255;
        }
        src_data += src_linesize;
        dst_data += dst_linesize;
    }
}

static void gen_envelope_instant(WaveformContext *s, AVFrame *out, int component)
{
    const int plane = s->desc->comp[component].plane;
    const int dst_linesize = out->linesize[plane];
    const uint8_t bg = s->bg_color[plane];
    const int is_chroma = (component == 1 || component == 2);
    const int shift_w = (is_chroma ? s->desc->log2_chroma_w : 0);
    const int shift_h = (is_chroma ? s->desc->log2_chroma_h : 0);
    const int dst_h = FF_CEIL_RSHIFT(out->height, shift_h);
    const int dst_w = FF_CEIL_RSHIFT(out->width, shift_w);
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    uint8_t *dst;
    int x, y;

    if (!s->mode) {
        for (y = 0; y < dst_h; y++) {
            dst = out->data[plane] + y * dst_linesize;
            for (x = start; x < end; x++) {
                if (dst[x] != bg) {
                    dst[x] = 255;
                    break;
                }
            }
            for (x = end - 1; x >= start; x--) {
                if (dst[x] != bg) {
                    dst[x] = 255;
                    break;
                }
            }
        }
    } else {
        for (x = 0; x < dst_w; x++) {
            for (y = start; y < end; y++) {
                dst = out->data[plane] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    dst[0] = 255;
                    break;
                }
            }
            for (y = end - 1; y >= start; y--) {
                dst = out->data[plane] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    dst[0] = 255;
                    break;
                }
            }
        }
    }
}

static void gen_envelope_peak(WaveformContext *s, AVFrame *out, int component)
{
    const int plane = s->desc->comp[component].plane;
    const int dst_linesize = out->linesize[plane];
    const uint8_t bg = s->bg_color[plane];
    const int is_chroma = (component == 1 || component == 2);
    const int shift_w = (is_chroma ? s->desc->log2_chroma_w : 0);
    const int shift_h = (is_chroma ? s->desc->log2_chroma_h : 0);
    const int dst_h = FF_CEIL_RSHIFT(out->height, shift_h);
    const int dst_w = FF_CEIL_RSHIFT(out->width, shift_w);
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    int *emax = s->emax[plane];
    int *emin = s->emin[plane];
    uint8_t *dst;
    int x, y;

    if (!s->mode) {
        for (y = 0; y < dst_h; y++) {
            dst = out->data[plane] + y * dst_linesize;
            for (x = start; x < end && x < emin[y]; x++) {
                if (dst[x] != bg) {
                    emin[y] = x;
                    break;
                }
            }
            for (x = end - 1; x >= start && x >= emax[y]; x--) {
                if (dst[x] != bg) {
                    emax[y] = x;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            gen_envelope_instant(s, out, component);

        for (y = 0; y < dst_h; y++) {
            dst = out->data[plane] + y * dst_linesize + emin[y];
            dst[0] = 255;
            dst = out->data[plane] + y * dst_linesize + emax[y];
            dst[0] = 255;
        }
    } else {
        for (x = 0; x < dst_w; x++) {
            for (y = start; y < end && y < emin[x]; y++) {
                dst = out->data[plane] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emin[x] = y;
                    break;
                }
            }
            for (y = end - 1; y >= start && y >= emax[x]; y--) {
                dst = out->data[plane] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emax[x] = y;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            gen_envelope_instant(s, out, component);

        for (x = 0; x < dst_w; x++) {
            dst = out->data[plane] + emin[x] * dst_linesize + x;
            dst[0] = 255;
            dst = out->data[plane] + emax[x] * dst_linesize + x;
            dst[0] = 255;
        }
    }
}

static void gen_envelope(WaveformContext *s, AVFrame *out, int component)
{
    if (s->envelope == 0) {
        return;
    } else if (s->envelope == 1) {
        gen_envelope_instant(s, out, component);
    } else {
        gen_envelope_peak(s, out, component);
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    WaveformContext *s    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int i,  k;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;

    for (k = 0; k < s->ncomp; k++) {
        const int is_chroma = (k == 1 || k == 2);
        const int dst_h = FF_CEIL_RSHIFT(outlink->h, (is_chroma ? s->desc->log2_chroma_h : 0));
        const int dst_w = FF_CEIL_RSHIFT(outlink->w, (is_chroma ? s->desc->log2_chroma_w : 0));
        for (i = 0; i < dst_h ; i++)
            memset(out->data[s->desc->comp[k].plane] +
                   i * out->linesize[s->desc->comp[k].plane],
                   s->bg_color[k], dst_w);
    }

    for (k = 0, i = 0; k < s->ncomp; k++) {
        if ((1 << k) & s->pcomp) {
            const int offset = i++ * 256 * s->display;
            gen_waveform(s, in, out, k, s->intensity, offset, s->mode);
            gen_envelope(s, out, k);
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    WaveformContext *s = ctx->priv;
    int p;

    for (p = 0; p < 4; p++) {
        av_freep(&s->emax[p]);
        av_freep(&s->emin[p]);
    }
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_waveform = {
    .name          = "waveform",
    .description   = NULL_IF_CONFIG_SMALL("Video waveform monitor."),
    .priv_size     = sizeof(WaveformContext),
    .priv_class    = &waveform_class,
    .query_formats = query_formats,
    .uninit        = uninit,
    .inputs        = inputs,
    .outputs       = outputs,
};
