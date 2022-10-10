/*
 * Copyright (c) 2021 Paul B Mahol
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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ESTDIFContext {
    const AVClass *class;

    int mode;             ///< 0 is frame, 1 is field
    int parity;           ///< frame field parity
    int deint;            ///< which frames to deinterlace
    int rslope;           ///< best edge slope search radius
    int redge;            ///< best edge match search radius
    float ecost;          ///< edge cost for edge matching
    float mcost;          ///< middle cost for edge matching
    float dcost;          ///< distance cost for edge matching
    int interp;           ///< type of interpolation
    int linesize[4];      ///< bytes of pixel data per line for each plane
    int planewidth[4];    ///< width of each plane
    int planeheight[4];   ///< height of each plane
    int field;            ///< which field are we on, 0 or 1
    int eof;
    int depth;
    int max;
    int nb_planes;
    int nb_threads;
    AVFrame *prev;

    void (*interpolate)(struct ESTDIFContext *s, uint8_t *dst,
                        const uint8_t *prev_line,  const uint8_t *next_line,
                        const uint8_t *prev2_line, const uint8_t *next2_line,
                        const uint8_t *prev3_line, const uint8_t *next3_line,
                        int x, int width, int rslope, int redge,
                        int depth, int *K);

    unsigned (*mid_8[3])(const uint8_t *const prev,
                         const uint8_t *const next,
                         const uint8_t *const prev2,
                         const uint8_t *const next2,
                         const uint8_t *const prev3,
                         const uint8_t *const next3,
                         int end, int x, int k, int depth);

    unsigned (*mid_16[3])(const uint16_t *const prev,
                          const uint16_t *const next,
                          const uint16_t *const prev2,
                          const uint16_t *const next2,
                          const uint16_t *const prev3,
                          const uint16_t *const next3,
                          int end, int x, int k, int depth);
} ESTDIFContext;

#define MAX_R 15
#define S     (MAX_R * 2 + 1)

#define OFFSET(x) offsetof(ESTDIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, 0, 0, FLAGS, unit }

static const AVOption estdif_options[] = {
    { "mode", "specify the mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "mode" },
    CONST("frame", "send one frame for each frame", 0, "mode"),
    CONST("field", "send one frame for each field", 1, "mode"),
    { "parity", "specify the assumed picture field parity", OFFSET(parity), AV_OPT_TYPE_INT, {.i64=-1}, -1, 1, FLAGS, "parity" },
    CONST("tff",  "assume top field first",    0, "parity"),
    CONST("bff",  "assume bottom field first", 1, "parity"),
    CONST("auto", "auto detect parity",       -1, "parity"),
    { "deint",  "specify which frames to deinterlace", OFFSET(deint), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "deint" },
    CONST("all",        "deinterlace all frames",                       0, "deint"),
    CONST("interlaced", "only deinterlace frames marked as interlaced", 1, "deint"),
    { "rslope", "specify the search radius for edge slope tracing", OFFSET(rslope), AV_OPT_TYPE_INT, {.i64=1}, 1, MAX_R, FLAGS, },
    { "redge",  "specify the search radius for best edge matching", OFFSET(redge),  AV_OPT_TYPE_INT, {.i64=2}, 0, MAX_R, FLAGS, },
    { "ecost",  "specify the edge cost for edge matching",          OFFSET(ecost),  AV_OPT_TYPE_FLOAT,{.dbl=1},0,9,FLAGS, },
    { "mcost",  "specify the middle cost for edge matching",        OFFSET(mcost),  AV_OPT_TYPE_FLOAT,{.dbl=0.5}, 0, 1,  FLAGS, },
    { "dcost",  "specify the distance cost for edge matching",      OFFSET(dcost),  AV_OPT_TYPE_FLOAT,{.dbl=0.5}, 0, 1,  FLAGS, },
    { "interp", "specify the type of interpolation",                OFFSET(interp), AV_OPT_TYPE_INT, {.i64=1}, 0, 2,     FLAGS, "interp" },
    CONST("2p", "two-point interpolation",  0, "interp"),
    CONST("4p", "four-point interpolation", 1, "interp"),
    CONST("6p", "six-point interpolation",  2, "interp"),
    { NULL }
};

AVFILTER_DEFINE_CLASS(estdif);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_GBRAP10,   AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ESTDIFContext *s = ctx->priv;

    outlink->time_base = av_mul_q(inlink->time_base, (AVRational){1, 2});
    if (s->mode)
        outlink->frame_rate = av_mul_q(inlink->frame_rate, (AVRational){2, 1});

    return 0;
}

typedef struct ThreadData {
    AVFrame *out, *in;
} ThreadData;

#define MIDL(type, ss)                                         \
static unsigned midl_##ss(const type *const prev,              \
                          const type *const next,              \
                          int end, int x, int k)               \
{                                                              \
    return (prev[av_clip(x + k, 0, end)] +                     \
            next[av_clip(x - k, 0, end)] + 1) >> 1;            \
}

MIDL(uint8_t, 8)
MIDL(uint16_t, 16)

#define MID2(type, ss)                                         \
static unsigned mid2_##ss(const type *const prev,              \
                          const type *const next,              \
                          const type *const prev2,             \
                          const type *const next2,             \
                          const type *const prev3,             \
                          const type *const next3,             \
                          int end, int x, int k, int depth)    \
{                                                              \
    return (prev[av_clip(x + k, 0, end)] +                     \
            next[av_clip(x - k, 0, end)] + 1) >> 1;            \
}

MID2(uint8_t, 8)
MID2(uint16_t, 16)

#define MID4(type, ss)                                         \
static unsigned mid4_##ss(const type *const prev,              \
                          const type *const next,              \
                          const type *const prev2,             \
                          const type *const next2,             \
                          const type *const prev3,             \
                          const type *const next3,             \
                          int end, int x, int k, int depth)    \
{                                                              \
    return av_clip_uintp2_c((                                  \
            9 * (prev[av_clip(x + k, 0, end)] +                \
                 next[av_clip(x - k, 0, end)]) -               \
            1 * (prev2[av_clip(x + k*3, 0, end)] +             \
                 next2[av_clip(x - k*3, 0, end)]) + 8) >> 4,   \
                 depth);                                       \
}

MID4(uint8_t, 8)
MID4(uint16_t, 16)

#define MID6(type, ss)                                         \
static unsigned mid6_##ss(const type *const prev,              \
                          const type *const next,              \
                          const type *const prev2,             \
                          const type *const next2,             \
                          const type *const prev3,             \
                          const type *const next3,             \
                          int end, int x, int k, int depth)    \
{                                                              \
    return av_clip_uintp2_c((                                  \
           20 * (prev[av_clip(x + k, 0, end)] +                \
                 next[av_clip(x - k, 0, end)]) -               \
            5 * (prev2[av_clip(x + k*3, 0, end)] +             \
                 next2[av_clip(x - k*3, 0, end)]) +            \
            1 * (prev3[av_clip(x + k*5, 0, end)] +             \
                 next3[av_clip(x - k*5, 0, end)]) + 16) >> 5,  \
                 depth);                                       \
}

MID6(uint8_t, 8)
MID6(uint16_t, 16)

#define DIFF(type, ss)                                         \
static unsigned diff_##ss(const type *const prev,              \
                          const type *const next,              \
                          int x, int y)                        \
{                                                              \
    return FFABS(prev[x] -  next[y]);                          \
}

DIFF(uint8_t, 8)
DIFF(uint16_t, 16)

#define COST(type, ss)                                         \
static unsigned cost_##ss(const type *const prev,              \
                          const type *const next,              \
                          int end, int x, int k)               \
{                                                              \
    const int m = midl_##ss(prev, next, end, x, k);            \
    const int p = prev[x];                                     \
    const int n = next[x];                                     \
                                                               \
    return FFABS(p - m) + FFABS(n - m);                        \
}

COST(uint8_t, 8)
COST(uint16_t, 16)

#define INTERPOLATE(type, atype, amax, ss)                                     \
static void interpolate_##ss(ESTDIFContext *s, uint8_t *ddst,                  \
                             const uint8_t *const pprev_line,                  \
                             const uint8_t *const nnext_line,                  \
                             const uint8_t *const pprev2_line,                 \
                             const uint8_t *const nnext2_line,                 \
                             const uint8_t *const pprev3_line,                 \
                             const uint8_t *const nnext3_line,                 \
                             int x, int width, int rslope,                     \
                             int redge, int depth,                             \
                             int *K)                                           \
{                                                                              \
    type *dst = (type *)ddst;                                                  \
    const type *const prev_line = (const type *const)pprev_line;               \
    const type *const prev2_line = (const type *const)pprev2_line;             \
    const type *const prev3_line = (const type *const)pprev3_line;             \
    const type *const next_line = (const type *const)nnext_line;               \
    const type *const next2_line = (const type *const)nnext2_line;             \
    const type *const next3_line = (const type *const)nnext3_line;             \
    const int interp = s->interp;                                              \
    const int ecost = s->ecost * 32.f;                                         \
    const int dcost = s->dcost * s->max;                                       \
    const int end = width - 1;                                                 \
    const atype mcost = s->mcost * s->redge * 4.f;                             \
    atype sd[S], sD[S], di = 0;                                                \
    atype dmin = amax;                                                         \
    int k = *K;                                                                \
                                                                               \
    for (int i = -rslope; i <= rslope && abs(k) > rslope; i++) {               \
        atype sum = 0;                                                         \
                                                                               \
        for (int j = -redge; j <= redge; j++) {                                \
            const int xx = av_clip(x + i + j, 0, end);                         \
            const int yy = av_clip(x - i + j, 0, end);                         \
            sum += diff_##ss(prev_line,  next_line,  xx, yy);                  \
            sum += diff_##ss(prev2_line, prev_line,  xx, yy);                  \
            sum += diff_##ss(next_line,  next2_line, xx, yy);                  \
        }                                                                      \
                                                                               \
        sD[i + rslope]  = ecost * sum;                                         \
        sD[i + rslope] += mcost * cost_##ss(prev_line,  next_line,  end, x, i);\
        sD[i + rslope] += dcost * abs(i);                                      \
                                                                               \
        dmin = FFMIN(sD[i + rslope], dmin);                                    \
    }                                                                          \
                                                                               \
    for (int i = -rslope; i <= rslope; i++) {                                  \
        atype sum = 0;                                                         \
                                                                               \
        for (int j = -redge; j <= redge; j++) {                                \
            const int xx = av_clip(x + k + i + j, 0, end);                     \
            const int yy = av_clip(x - k - i + j, 0, end);                     \
            sum += diff_##ss(prev_line,  next_line,  xx, yy);                  \
            sum += diff_##ss(prev2_line, prev_line,  xx, yy);                  \
            sum += diff_##ss(next_line,  next2_line, xx, yy);                  \
        }                                                                      \
                                                                               \
        sd[i + rslope]  = ecost * sum;                                         \
        sd[i + rslope] += mcost * cost_##ss(prev_line, next_line, end, x, k+i);\
        sd[i + rslope] += dcost * abs(k + i);                                  \
                                                                               \
        dmin = FFMIN(sd[i + rslope], dmin);                                    \
    }                                                                          \
                                                                               \
    for (int i = -rslope; i <= rslope && abs(k) > rslope; i++) {               \
        if (dmin == sD[i + rslope]) {                                          \
            di = 1;                                                            \
            k = i;                                                             \
            break;                                                             \
        }                                                                      \
    }                                                                          \
                                                                               \
    for (int i = -rslope; i <= rslope && !di; i++) {                           \
        if (dmin == sd[i + rslope]) {                                          \
            k += i;                                                            \
            break;                                                             \
        }                                                                      \
    }                                                                          \
                                                                               \
    dst[x] = s->mid_##ss[interp](prev_line, next_line,                         \
                                 prev2_line, next2_line,                       \
                                 prev3_line, next3_line,                       \
                                 end, x, k, depth);                            \
                                                                               \
    *K = k;                                                                    \
}

INTERPOLATE(uint8_t,  unsigned, UINT_MAX, 8)
INTERPOLATE(uint16_t, uint64_t, UINT64_MAX, 16)

static int deinterlace_slice(AVFilterContext *ctx, void *arg,
                             int jobnr, int nb_jobs)
{
    ESTDIFContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    const int rslope = s->rslope;
    const int redge = s->redge;
    const int depth = s->depth;
    const int interlaced = in->interlaced_frame;
    const int tff = (s->field == (s->parity == -1 ? interlaced ? in->top_field_first : 1 :
                                  s->parity ^ 1));

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const uint8_t *src_data = in->data[plane];
        uint8_t *dst_data = out->data[plane];
        const int linesize = s->linesize[plane];
        const int width = s->planewidth[plane];
        const int height = s->planeheight[plane];
        const int src_linesize = in->linesize[plane];
        const int dst_linesize = out->linesize[plane];
        const int start = (height * jobnr) / nb_jobs;
        const int end = (height * (jobnr+1)) / nb_jobs;
        const uint8_t *prev_line, *prev2_line, *next_line, *next2_line, *in_line;
        const uint8_t *prev3_line, *next3_line;
        uint8_t *out_line;
        int y_out;

        y_out = start + (tff ^ (start & 1));

        in_line  = src_data + (y_out * src_linesize);
        out_line = dst_data + (y_out * dst_linesize);

        while (y_out < end) {
            memcpy(out_line, in_line, linesize);
            y_out += 2;
            in_line  += src_linesize * 2;
            out_line += dst_linesize * 2;
        }

        y_out = start + ((!tff) ^ (start & 1));
        out_line = dst_data + (y_out * dst_linesize);

        for (int y = y_out; y < end; y += 2) {
            int y_prev3_in = y - 5;
            int y_next3_in = y + 5;
            int y_prev2_in = y - 3;
            int y_next2_in = y + 3;
            int y_prev_in = y - 1;
            int y_next_in = y + 1;
            int k;

            while (y_prev3_in < 0)
                y_prev3_in += 2;

            while (y_next3_in >= height)
                y_next3_in -= 2;

            while (y_prev2_in < 0)
                y_prev2_in += 2;

            while (y_next2_in >= height)
                y_next2_in -= 2;

            while (y_prev_in < 0)
                y_prev_in += 2;

            while (y_next_in >= height)
                y_next_in -= 2;

            prev3_line = src_data + (y_prev3_in * src_linesize);
            next3_line = src_data + (y_next3_in * src_linesize);

            prev2_line = src_data + (y_prev2_in * src_linesize);
            next2_line = src_data + (y_next2_in * src_linesize);

            prev_line = src_data + (y_prev_in * src_linesize);
            next_line = src_data + (y_next_in * src_linesize);

            k = 0;

            for (int x = 0; x < width; x++) {
                s->interpolate(s, out_line,
                               prev_line, next_line,
                               prev2_line, next2_line,
                               prev3_line, next3_line,
                               x, width, rslope, redge, depth, &k);
            }

            out_line += 2 * dst_linesize;
        }
    }

    return 0;
}

static int filter(AVFilterContext *ctx, AVFrame *in, int64_t pts, int64_t duration)
{
    ESTDIFContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    ThreadData td;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    av_frame_copy_props(out, in);
    out->interlaced_frame = 0;
    out->pts = pts;
    out->duration = duration;

    td.out = out; td.in = in;
    ff_filter_execute(ctx, deinterlace_slice, &td, NULL,
                      FFMIN(s->planeheight[1] / 2, s->nb_threads));

    if (s->mode)
        s->field = !s->field;

    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ESTDIFContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;

    if (inlink->h < 3) {
        av_log(ctx, AV_LOG_ERROR, "Video of less than 3 lines is not supported\n");
        return AVERROR(EINVAL);
    }

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->depth = desc->comp[0].depth;
    s->interpolate = s->depth <= 8 ? interpolate_8 : interpolate_16;
    s->mid_8[0] = mid2_8;
    s->mid_8[1] = mid4_8;
    s->mid_8[2] = mid6_8;
    s->mid_16[0] = mid2_16;
    s->mid_16[1] = mid4_16;
    s->mid_16[2] = mid6_16;
    s->max = (1 << (s->depth)) - 1;

    return 0;
}
 static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ESTDIFContext *s = ctx->priv;
    int ret;

    if (!s->prev) {
        s->prev = in;
        return 0;
    }

    if ((s->deint && !s->prev->interlaced_frame) || ctx->is_disabled) {
        s->prev->pts *= 2;
        s->prev->duration *= 2;
        ret = ff_filter_frame(ctx->outputs[0], s->prev);
        s->prev = in;
        return ret;
    }

    ret = filter(ctx, s->prev, s->prev->pts * 2,
                 s->prev->duration * (s->mode ? 1 : 2));
    if (ret < 0 || s->mode == 0) {
        av_frame_free(&s->prev);
        s->prev = in;
        return ret;
    }

    ret = filter(ctx, s->prev, s->prev->pts + in->pts, in->duration);
    av_frame_free(&s->prev);
    s->prev = in;
    return ret;
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    ESTDIFContext *s = ctx->priv;
    int ret;

    if (s->eof)
        return AVERROR_EOF;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->prev) {
        AVFrame *next = av_frame_clone(s->prev);

        if (!next)
            return AVERROR(ENOMEM);

        next->pts = s->prev->pts + av_rescale_q(1, av_inv_q(ctx->outputs[0]->frame_rate),
                                                ctx->outputs[0]->time_base);
        s->eof = 1;
        ret = filter_frame(ctx->inputs[0], next);
    } else if (ret < 0) {
        return ret;
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ESTDIFContext *s = ctx->priv;

    av_frame_free(&s->prev);
}

static const AVFilterPad estdif_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
};

static const AVFilterPad estdif_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_estdif = {
    .name          = "estdif",
    .description   = NULL_IF_CONFIG_SMALL("Apply Edge Slope Tracing deinterlace."),
    .priv_size     = sizeof(ESTDIFContext),
    .priv_class    = &estdif_class,
    .uninit        = uninit,
    FILTER_INPUTS(estdif_inputs),
    FILTER_OUTPUTS(estdif_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
