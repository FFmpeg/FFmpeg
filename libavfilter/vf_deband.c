/*
 * Copyright (c) 2015 Niklas Haas
 * Copyright (c) 2015 Paul B Mahol
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct DebandContext {
    const AVClass *class;

    int coupling;
    float threshold[4];
    int range;
    int blur;
    float direction;

    int nb_components;
    int planewidth[4];
    int planeheight[4];
    int shift[2];
    int thr[4];

    int *x_pos;
    int *y_pos;

    int (*deband)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} DebandContext;

#define OFFSET(x) offsetof(DebandContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption deband_options[] = {
    { "1thr",      "set 1st plane threshold", OFFSET(threshold[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.02},  0.00003,     0.5, FLAGS },
    { "2thr",      "set 2nd plane threshold", OFFSET(threshold[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.02},  0.00003,     0.5, FLAGS },
    { "3thr",      "set 3rd plane threshold", OFFSET(threshold[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.02},  0.00003,     0.5, FLAGS },
    { "4thr",      "set 4th plane threshold", OFFSET(threshold[3]), AV_OPT_TYPE_FLOAT, {.dbl=0.02},  0.00003,     0.5, FLAGS },
    { "range",     "set range",               OFFSET(range),        AV_OPT_TYPE_INT,   {.i64=16},    INT_MIN, INT_MAX, FLAGS },
    { "r",         "set range",               OFFSET(range),        AV_OPT_TYPE_INT,   {.i64=16},    INT_MIN, INT_MAX, FLAGS },
    { "direction", "set direction",           OFFSET(direction),    AV_OPT_TYPE_FLOAT, {.dbl=2*M_PI},-2*M_PI,  2*M_PI, FLAGS },
    { "d",         "set direction",           OFFSET(direction),    AV_OPT_TYPE_FLOAT, {.dbl=2*M_PI},-2*M_PI,  2*M_PI, FLAGS },
    { "blur",      "set blur",                OFFSET(blur),         AV_OPT_TYPE_BOOL,  {.i64=1},           0,       1, FLAGS },
    { "b",         "set blur",                OFFSET(blur),         AV_OPT_TYPE_BOOL,  {.i64=1},           0,       1, FLAGS },
    { "coupling",  "set plane coupling",      OFFSET(coupling),     AV_OPT_TYPE_BOOL,  {.i64=0},           0,       1, FLAGS },
    { "c",         "set plane coupling",      OFFSET(coupling),     AV_OPT_TYPE_BOOL,  {.i64=0},           0,       1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(deband);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const DebandContext *s = ctx->priv;

    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
        AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat cpix_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out,
                                            s->coupling ? cpix_fmts : pix_fmts);
}

static float frand(int x, int y)
{
    const float r = sinf(x * 12.9898f + y * 78.233f) * 43758.545f;

    return r - floorf(r);
}

static int inline get_avg(int ref0, int ref1, int ref2, int ref3)
{
    return (ref0 + ref1 + ref2 + ref3) / 4;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int deband_8_c(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DebandContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    int x, y, p;

    for (p = 0; p < s->nb_components; p++) {
        const uint8_t *src_ptr = (const uint8_t *)in->data[p];
        uint8_t *dst_ptr = (uint8_t *)out->data[p];
        const int dst_linesize = out->linesize[p];
        const int src_linesize = in->linesize[p];
        const int thr = s->thr[p];
        const int start = (s->planeheight[p] *  jobnr   ) / nb_jobs;
        const int end   = (s->planeheight[p] * (jobnr+1)) / nb_jobs;
        const int w = s->planewidth[p] - 1;
        const int h = s->planeheight[p] - 1;

        for (y = start; y < end; y++) {
            const int pos = y * s->planewidth[0];

            for (x = 0; x < s->planewidth[p]; x++) {
                const int x_pos = s->x_pos[pos + x];
                const int y_pos = s->y_pos[pos + x];
                const int ref0 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref1 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref2 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int ref3 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int src0 = src_ptr[y * src_linesize + x];

                if (s->blur) {
                    const int avg = get_avg(ref0, ref1, ref2, ref3);
                    const int diff = FFABS(src0 - avg);

                    dst_ptr[y * dst_linesize + x] = diff < thr ? avg : src0;
                } else {
                    dst_ptr[y * dst_linesize + x] = (FFABS(src0 - ref0) < thr) &&
                                                    (FFABS(src0 - ref1) < thr) &&
                                                    (FFABS(src0 - ref2) < thr) &&
                                                    (FFABS(src0 - ref3) < thr) ? get_avg(ref0, ref1, ref2, ref3) : src0;
                }
            }
        }
    }

    return 0;
}

static int deband_8_coupling_c(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DebandContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int start = (s->planeheight[0] *  jobnr   ) / nb_jobs;
    const int end   = (s->planeheight[0] * (jobnr+1)) / nb_jobs;
    int x, y, p;

    for (y = start; y < end; y++) {
        const int pos = y * s->planewidth[0];

        for (x = 0; x < s->planewidth[0]; x++) {
            const int x_pos = s->x_pos[pos + x];
            const int y_pos = s->y_pos[pos + x];
            int avg[4], cmp[4] = { 0 }, src[4];

            for (p = 0; p < s->nb_components; p++) {
                const uint8_t *src_ptr = (const uint8_t *)in->data[p];
                const int src_linesize = in->linesize[p];
                const int thr = s->thr[p];
                const int w = s->planewidth[p] - 1;
                const int h = s->planeheight[p] - 1;
                const int ref0 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref1 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref2 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int ref3 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int src0 = src_ptr[y * src_linesize + x];

                src[p] = src0;
                avg[p] = get_avg(ref0, ref1, ref2, ref3);

                if (s->blur) {
                    cmp[p] = FFABS(src0 - avg[p]) < thr;
                } else {
                    cmp[p] = (FFABS(src0 - ref0) < thr) &&
                             (FFABS(src0 - ref1) < thr) &&
                             (FFABS(src0 - ref2) < thr) &&
                             (FFABS(src0 - ref3) < thr);
                }
            }

            for (p = 0; p < s->nb_components; p++)
                if (!cmp[p])
                    break;
            if (p == s->nb_components) {
                for (p = 0; p < s->nb_components; p++) {
                    const int dst_linesize = out->linesize[p];

                    out->data[p][y * dst_linesize + x] = avg[p];
                }
            } else {
                for (p = 0; p < s->nb_components; p++) {
                    const int dst_linesize = out->linesize[p];

                    out->data[p][y * dst_linesize + x] = src[p];
                }
            }
        }
    }

    return 0;
}

static int deband_16_coupling_c(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DebandContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int start = (s->planeheight[0] *  jobnr   ) / nb_jobs;
    const int end   = (s->planeheight[0] * (jobnr+1)) / nb_jobs;
    int x, y, p, z;

    for (y = start; y < end; y++) {
        const int pos = y * s->planewidth[0];

        for (x = 0; x < s->planewidth[0]; x++) {
            const int x_pos = s->x_pos[pos + x];
            const int y_pos = s->y_pos[pos + x];
            int avg[4], cmp[4] = { 0 }, src[4];

            for (p = 0; p < s->nb_components; p++) {
                const uint16_t *src_ptr = (const uint16_t *)in->data[p];
                const int src_linesize = in->linesize[p] / 2;
                const int thr = s->thr[p];
                const int w = s->planewidth[p] - 1;
                const int h = s->planeheight[p] - 1;
                const int ref0 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref1 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref2 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int ref3 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int src0 = src_ptr[y * src_linesize + x];

                src[p] = src0;
                avg[p] = get_avg(ref0, ref1, ref2, ref3);

                if (s->blur) {
                    cmp[p] = FFABS(src0 - avg[p]) < thr;
                } else {
                    cmp[p] = (FFABS(src0 - ref0) < thr) &&
                             (FFABS(src0 - ref1) < thr) &&
                             (FFABS(src0 - ref2) < thr) &&
                             (FFABS(src0 - ref3) < thr);
                }
            }

            for (z = 0; z < s->nb_components; z++)
                if (!cmp[z])
                    break;
            if (z == s->nb_components) {
                for (p = 0; p < s->nb_components; p++) {
                    const int dst_linesize = out->linesize[p] / 2;
                    uint16_t *dst = (uint16_t *)out->data[p] + y * dst_linesize + x;

                    dst[0] = avg[p];
                }
            } else {
                for (p = 0; p < s->nb_components; p++) {
                    const int dst_linesize = out->linesize[p] / 2;
                    uint16_t *dst = (uint16_t *)out->data[p] + y * dst_linesize + x;

                    dst[0] = src[p];
                }
            }
        }
    }

    return 0;
}

static int deband_16_c(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DebandContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    int x, y, p;

    for (p = 0; p < s->nb_components; p++) {
        const uint16_t *src_ptr = (const uint16_t *)in->data[p];
        uint16_t *dst_ptr = (uint16_t *)out->data[p];
        const int dst_linesize = out->linesize[p] / 2;
        const int src_linesize = in->linesize[p] / 2;
        const int thr = s->thr[p];
        const int start = (s->planeheight[p] *  jobnr   ) / nb_jobs;
        const int end   = (s->planeheight[p] * (jobnr+1)) / nb_jobs;
        const int w = s->planewidth[p] - 1;
        const int h = s->planeheight[p] - 1;

        for (y = start; y < end; y++) {
            const int pos = y * s->planewidth[0];

            for (x = 0; x < s->planewidth[p]; x++) {
                const int x_pos = s->x_pos[pos + x];
                const int y_pos = s->y_pos[pos + x];
                const int ref0 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref1 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x +  x_pos, 0, w)];
                const int ref2 = src_ptr[av_clip(y + -y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int ref3 = src_ptr[av_clip(y +  y_pos, 0, h) * src_linesize + av_clip(x + -x_pos, 0, w)];
                const int src0 = src_ptr[y * src_linesize + x];

                if (s->blur) {
                    const int avg = get_avg(ref0, ref1, ref2, ref3);
                    const int diff = FFABS(src0 - avg);

                    dst_ptr[y * dst_linesize + x] = diff < thr ? avg : src0;
                } else {
                    dst_ptr[y * dst_linesize + x] = (FFABS(src0 - ref0) < thr) &&
                                                    (FFABS(src0 - ref1) < thr) &&
                                                    (FFABS(src0 - ref2) < thr) &&
                                                    (FFABS(src0 - ref3) < thr) ? get_avg(ref0, ref1, ref2, ref3) : src0;
                }
            }
        }
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    DebandContext *s = ctx->priv;
    const float direction = s->direction;
    const int range = s->range;
    int x, y;

    s->nb_components = desc->nb_components;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    s->shift[0] = desc->log2_chroma_w;
    s->shift[1] = desc->log2_chroma_h;

    if (s->coupling)
        s->deband = desc->comp[0].depth > 8 ? deband_16_coupling_c : deband_8_coupling_c;
    else
        s->deband = desc->comp[0].depth > 8 ? deband_16_c : deband_8_c;

    s->thr[0] = ((1 << desc->comp[0].depth) - 1) * s->threshold[0];
    s->thr[1] = ((1 << desc->comp[1].depth) - 1) * s->threshold[1];
    s->thr[2] = ((1 << desc->comp[2].depth) - 1) * s->threshold[2];
    s->thr[3] = ((1 << desc->comp[3].depth) - 1) * s->threshold[3];

    if (!s->x_pos)
        s->x_pos = av_malloc(s->planewidth[0] * s->planeheight[0] * sizeof(*s->x_pos));
    if (!s->y_pos)
        s->y_pos = av_malloc(s->planewidth[0] * s->planeheight[0] * sizeof(*s->y_pos));
    if (!s->x_pos || !s->y_pos)
        return AVERROR(ENOMEM);

    for (y = 0; y < s->planeheight[0]; y++) {
        for (x = 0; x < s->planewidth[0]; x++) {
            const float r = frand(x, y);
            const float dir = direction < 0 ? -direction : r * direction;
            const int dist = range < 0 ? -range : r * range;

            s->x_pos[y * s->planewidth[0] + x] = cosf(dir) * dist;
            s->y_pos[y * s->planewidth[0] + x] = sinf(dir) * dist;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DebandContext *s = ctx->priv;
    AVFrame *out;
    ThreadData td;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    td.in = in; td.out = out;
    ff_filter_execute(ctx, s->deband, &td, NULL,
                      FFMIN3(s->planeheight[1], s->planeheight[2],
                             ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);

    if (ret < 0)
        return ret;

    return config_input(ctx->inputs[0]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DebandContext *s = ctx->priv;

    av_freep(&s->x_pos);
    av_freep(&s->y_pos);
}

static const AVFilterPad avfilter_vf_deband_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_deband = {
    .name          = "deband",
    .description   = NULL_IF_CONFIG_SMALL("Debands video."),
    .priv_size     = sizeof(DebandContext),
    .priv_class    = &deband_class,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_vf_deband_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
