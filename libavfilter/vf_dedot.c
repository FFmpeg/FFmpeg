/*
 * Copyright (c) 2018 Paul B Mahol
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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct DedotContext {
    const AVClass *class;
    int m;
    float lt;
    float tl;
    float tc;
    float ct;

    const AVPixFmtDescriptor *desc;
    int depth;
    int max;
    int luma2d;
    int lumaT;
    int chromaT1;
    int chromaT2;

    int eof;
    int eof_frames;
    int nb_planes;
    int planewidth[4];
    int planeheight[4];

    AVFrame *frames[5];

    int (*dedotcrawl)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
    int (*derainbow)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} DedotContext;

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

#define DEFINE_DEDOTCRAWL(name, type, div)                       \
static int dedotcrawl##name(AVFilterContext *ctx, void *arg,     \
                            int jobnr, int nb_jobs)              \
{                                                                \
    DedotContext *s = ctx->priv;                                 \
    AVFrame *out = arg;                                          \
    int src_linesize = s->frames[2]->linesize[0] / div;          \
    int dst_linesize = out->linesize[0] / div;                   \
    int p0_linesize = s->frames[0]->linesize[0] / div;           \
    int p1_linesize = s->frames[1]->linesize[0] / div;           \
    int p3_linesize = s->frames[3]->linesize[0] / div;           \
    int p4_linesize = s->frames[4]->linesize[0] / div;           \
    const int h = s->planeheight[0];                             \
    int slice_start = (h * jobnr) / nb_jobs;                     \
    int slice_end = (h * (jobnr+1)) / nb_jobs;                   \
    type *p0 = (type *)s->frames[0]->data[0];                    \
    type *p1 = (type *)s->frames[1]->data[0];                    \
    type *p3 = (type *)s->frames[3]->data[0];                    \
    type *p4 = (type *)s->frames[4]->data[0];                    \
    type *src = (type *)s->frames[2]->data[0];                   \
    type *dst = (type *)out->data[0];                            \
    const int luma2d = s->luma2d;                                \
    const int lumaT = s->lumaT;                                  \
                                                                 \
    if (!slice_start) {                                          \
        slice_start++;                                           \
    }                                                            \
    p0 += p0_linesize * slice_start;                             \
    p1 += p1_linesize * slice_start;                             \
    p3 += p3_linesize * slice_start;                             \
    p4 += p4_linesize * slice_start;                             \
    src += src_linesize * slice_start;                           \
    dst += dst_linesize * slice_start;                           \
    if (slice_end == h) {                                        \
        slice_end--;                                             \
    }                                                            \
    for (int y = slice_start; y < slice_end; y++) {              \
        for (int x = 1; x < s->planewidth[0] - 1; x++) {         \
            int above = src[x - src_linesize];                   \
            int below = src[x + src_linesize];                   \
            int cur = src[x];                                    \
            int left = src[x - 1];                               \
            int right = src[x + 1];                              \
                                                                 \
            if (FFABS(above + below - 2 * cur) <= luma2d &&      \
                FFABS(left + right - 2 * cur) <= luma2d)         \
                continue;                                        \
                                                                 \
            if (FFABS(cur - p0[x]) <= lumaT &&                   \
                FFABS(cur - p4[x]) <= lumaT &&                   \
                FFABS(p1[x] - p3[x]) <= lumaT) {                 \
                int diff1 = FFABS(cur - p1[x]);                  \
                int diff2 = FFABS(cur - p3[x]);                  \
                                                                 \
                if (diff1 < diff2)                               \
                    dst[x] = (src[x] + p1[x] + 1) >> 1;          \
                else                                             \
                    dst[x] = (src[x] + p3[x] + 1) >> 1;          \
            }                                                    \
        }                                                        \
                                                                 \
        dst += dst_linesize;                                     \
        src += src_linesize;                                     \
        p0 += p0_linesize;                                       \
        p1 += p1_linesize;                                       \
        p3 += p3_linesize;                                       \
        p4 += p4_linesize;                                       \
    }                                                            \
    return 0;                                                    \
}

DEFINE_DEDOTCRAWL(8, uint8_t, 1)
DEFINE_DEDOTCRAWL(16, uint16_t, 2)

typedef struct ThreadData {
    AVFrame *out;
    int plane;
} ThreadData;

#define DEFINE_DERAINBOW(name, type, div)                    \
static int derainbow##name(AVFilterContext *ctx, void *arg,  \
                           int jobnr, int nb_jobs)           \
{                                                            \
    DedotContext *s = ctx->priv;                             \
    ThreadData *td = arg;                                    \
    AVFrame *out = td->out;                                  \
    const int plane = td->plane;                             \
    const int h = s->planeheight[plane];                     \
    int slice_start = (h * jobnr) / nb_jobs;                 \
    int slice_end = (h * (jobnr+1)) / nb_jobs;               \
    int src_linesize = s->frames[2]->linesize[plane] / div;  \
    int dst_linesize = out->linesize[plane] / div;           \
    int p0_linesize = s->frames[0]->linesize[plane] / div;   \
    int p1_linesize = s->frames[1]->linesize[plane] / div;   \
    int p3_linesize = s->frames[3]->linesize[plane] / div;   \
    int p4_linesize = s->frames[4]->linesize[plane] / div;   \
    type *p0 = (type *)s->frames[0]->data[plane];            \
    type *p1 = (type *)s->frames[1]->data[plane];            \
    type *p3 = (type *)s->frames[3]->data[plane];            \
    type *p4 = (type *)s->frames[4]->data[plane];            \
    type *src = (type *)s->frames[2]->data[plane];           \
    type *dst = (type *)out->data[plane];                    \
    const int chromaT1 = s->chromaT1;                        \
    const int chromaT2 = s->chromaT2;                        \
                                                             \
    p0 += slice_start * p0_linesize;                         \
    p1 += slice_start * p1_linesize;                         \
    p3 += slice_start * p3_linesize;                         \
    p4 += slice_start * p4_linesize;                         \
    src += slice_start * src_linesize;                       \
    dst += slice_start * dst_linesize;                       \
    for (int y = slice_start; y < slice_end; y++) {          \
        for (int x = 0; x < s->planewidth[plane]; x++) {     \
            int cur = src[x];                                \
                                                             \
            if (FFABS(cur - p0[x]) <= chromaT1 &&            \
                FFABS(cur - p4[x]) <= chromaT1 &&            \
                FFABS(p1[x] - p3[x]) <= chromaT1 &&          \
                FFABS(cur - p1[x]) > chromaT2 &&             \
                FFABS(cur - p3[x]) > chromaT2) {             \
                int diff1 = FFABS(cur - p1[x]);              \
                int diff2 = FFABS(cur - p3[x]);              \
                                                             \
                if (diff1 < diff2)                           \
                    dst[x] = (src[x] + p1[x] + 1) >> 1;      \
                else                                         \
                    dst[x] = (src[x] + p3[x] + 1) >> 1;      \
            }                                                \
        }                                                    \
                                                             \
        dst += dst_linesize;                                 \
        src += src_linesize;                                 \
        p0 += p0_linesize;                                   \
        p1 += p1_linesize;                                   \
        p3 += p3_linesize;                                   \
        p4 += p4_linesize;                                   \
    }                                                        \
    return 0;                                                \
}

DEFINE_DERAINBOW(8, uint8_t, 1)
DEFINE_DERAINBOW(16, uint16_t, 2)

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DedotContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = s->desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->luma2d = s->lt * s->max;
    s->lumaT = s->tl * s->max;
    s->chromaT1 = s->tc * s->max;
    s->chromaT2 = s->ct * s->max;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, s->desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    if (s->depth <= 8) {
        s->dedotcrawl = dedotcrawl8;
        s->derainbow = derainbow8;
    } else {
        s->dedotcrawl = dedotcrawl16;
        s->derainbow = derainbow16;
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    DedotContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int64_t pts;
    int status;
    int ret = 0;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (s->eof == 0) {
        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
    }
    if (frame || s->eof_frames > 0) {
        AVFrame *out = NULL;

        if (frame) {
            for (int i = 2; i < 5; i++) {
                if (!s->frames[i])
                    s->frames[i] = av_frame_clone(frame);
            }
            av_frame_free(&frame);
        } else if (s->frames[3]) {
            s->eof_frames--;
            s->frames[4] = av_frame_clone(s->frames[3]);
        }

        if (s->frames[0] &&
            s->frames[1] &&
            s->frames[2] &&
            s->frames[3] &&
            s->frames[4]) {
            out = av_frame_clone(s->frames[2]);
            if (out && !ctx->is_disabled) {
                ret = ff_inlink_make_frame_writable(inlink, &out);
                if (ret >= 0) {
                    if (s->m & 1)
                        ff_filter_execute(ctx, s->dedotcrawl, out, NULL,
                                          FFMIN(ff_filter_get_nb_threads(ctx),
                                                s->planeheight[0]));
                    if (s->m & 2) {
                        ThreadData td;
                        td.out = out; td.plane = 1;
                        ff_filter_execute(ctx, s->derainbow, &td, NULL,
                                          FFMIN(ff_filter_get_nb_threads(ctx),
                                                s->planeheight[1]));
                        td.plane = 2;
                        ff_filter_execute(ctx, s->derainbow, &td, NULL,
                                          FFMIN(ff_filter_get_nb_threads(ctx),
                                                s->planeheight[2]));
                    }
                } else
                    av_frame_free(&out);
            } else if (!out) {
                ret = AVERROR(ENOMEM);
            }
        }

        av_frame_free(&s->frames[0]);
        s->frames[0] = s->frames[1];
        s->frames[1] = s->frames[2];
        s->frames[2] = s->frames[3];
        s->frames[3] = s->frames[4];
        s->frames[4] = NULL;

        if (ret < 0)
            return ret;
        if (out)
            return ff_filter_frame(outlink, out);
    }

    if (s->eof) {
        if (s->eof_frames <= 0) {
            ff_outlink_set_status(outlink, AVERROR_EOF, s->frames[2]->pts);
        } else {
            ff_filter_set_ready(ctx, 10);
        }
        return 0;
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF) {
            s->eof = 1;
            s->eof_frames = !!s->frames[0] + !!s->frames[1];
            if (s->eof_frames <= 0) {
                ff_outlink_set_status(outlink, AVERROR_EOF, pts);
                return 0;
            }
            ff_filter_set_ready(ctx, 10);
            return 0;
        }
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DedotContext *s = ctx->priv;

    for (int i = 0; i < 5; i++)
        av_frame_free(&s->frames[i]);
}

#define OFFSET(x) offsetof(DedotContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption dedot_options[] = {
    { "m",   "set filtering mode",                          OFFSET( m), AV_OPT_TYPE_FLAGS, {.i64=3},    0, 3, FLAGS, .unit = "m" },
    { "dotcrawl",                                           0,       0, AV_OPT_TYPE_CONST, {.i64=1},    0, 0, FLAGS, .unit = "m" },
    { "rainbows",                                           0,       0, AV_OPT_TYPE_CONST, {.i64=2},    0, 0, FLAGS, .unit = "m" },
    { "lt",  "set spatial luma threshold",                  OFFSET(lt), AV_OPT_TYPE_FLOAT, {.dbl=.079}, 0, 1, FLAGS },
    { "tl",  "set tolerance for temporal luma",             OFFSET(tl), AV_OPT_TYPE_FLOAT, {.dbl=.079}, 0, 1, FLAGS },
    { "tc",  "set tolerance for chroma temporal variation", OFFSET(tc), AV_OPT_TYPE_FLOAT, {.dbl=.058}, 0, 1, FLAGS },
    { "ct",  "set temporal chroma threshold",               OFFSET(ct), AV_OPT_TYPE_FLOAT, {.dbl=.019}, 0, 1, FLAGS },
    { NULL },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

AVFILTER_DEFINE_CLASS(dedot);

const FFFilter ff_vf_dedot = {
    .p.name        = "dedot",
    .p.description = NULL_IF_CONFIG_SMALL("Reduce cross-luminance and cross-color."),
    .p.priv_class  = &dedot_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(DedotContext),
    .activate      = activate,
    .uninit        = uninit,
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
};
