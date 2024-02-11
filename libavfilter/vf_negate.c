/*
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
#include "drawutils.h"
#include "internal.h"
#include "video.h"

#define COMP_R 0x01
#define COMP_G 0x02
#define COMP_B 0x04
#define COMP_A 0x08
#define COMP_Y 0x10
#define COMP_U 0x20
#define COMP_V 0x40

typedef struct ThreadData {
    AVFrame *in;
    AVFrame *out;
} ThreadData;

typedef struct NegateContext {
    const AVClass *class;
    int negate_alpha;
    int max;
    int requested_components;
    int components;
    int planes;
    int step;
    int nb_planes;
    int linesize[4];
    int width[4];
    int height[4];
    uint8_t rgba_map[4];

    void (*negate)(const uint8_t *src, uint8_t *dst,
                   ptrdiff_t slinesize, ptrdiff_t dlinesize,
                   int w, int h, int max, int step,
                   int components);
} NegateContext;

#define OFFSET(x) offsetof(NegateContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption negate_options[] = {
    { "components", "set components to negate",  OFFSET(requested_components), AV_OPT_TYPE_FLAGS, {.i64=0x77}, 1, 0xff, FLAGS, .unit = "flags"},
    {      "y", "set luma component",  0, AV_OPT_TYPE_CONST, {.i64=COMP_Y}, 0, 0, FLAGS, .unit = "flags"},
    {      "u", "set u component",     0, AV_OPT_TYPE_CONST, {.i64=COMP_U}, 0, 0, FLAGS, .unit = "flags"},
    {      "v", "set v component",     0, AV_OPT_TYPE_CONST, {.i64=COMP_V}, 0, 0, FLAGS, .unit = "flags"},
    {      "r", "set red component",   0, AV_OPT_TYPE_CONST, {.i64=COMP_R}, 0, 0, FLAGS, .unit = "flags"},
    {      "g", "set green component", 0, AV_OPT_TYPE_CONST, {.i64=COMP_G}, 0, 0, FLAGS, .unit = "flags"},
    {      "b", "set blue component",  0, AV_OPT_TYPE_CONST, {.i64=COMP_B}, 0, 0, FLAGS, .unit = "flags"},
    {      "a", "set alpha component", 0, AV_OPT_TYPE_CONST, {.i64=COMP_A}, 0, 0, FLAGS, .unit = "flags"},
    { "negate_alpha",  NULL,    OFFSET(negate_alpha), AV_OPT_TYPE_BOOL, {.i64=0},    0,    1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(negate);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV420P16,
    AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
    AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA422P12,
    AV_PIX_FMT_YUVA444P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_ARGB, AV_PIX_FMT_RGBA,
    AV_PIX_FMT_ABGR, AV_PIX_FMT_BGRA,
    AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB48, AV_PIX_FMT_RGBA64,
    AV_PIX_FMT_BGR48, AV_PIX_FMT_BGRA64,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP14,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14,
    AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP12,
    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static void negate8(const uint8_t *src, uint8_t *dst,
                     ptrdiff_t slinesize, ptrdiff_t dlinesize,
                     int w, int h, int max, int step,
                     int components)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            dst[x] = 255 - src[x];

        dst += dlinesize;
        src += slinesize;
    }
}

static void negate_packed8(const uint8_t *ssrc, uint8_t *ddst,
                           ptrdiff_t slinesize, ptrdiff_t dlinesize,
                           int w, int h, int max, int step,
                           int components)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *src = ssrc + y * slinesize;
        uint8_t *dst = ddst + y * dlinesize;

        for (int x = 0; x < w; x++) {
            switch (step) {
            case 4:  dst[3] = components & 8 ? 255 - src[3] : src[3];
            case 3:  dst[2] = components & 4 ? 255 - src[2] : src[2];
            case 2:  dst[1] = components & 2 ? 255 - src[1] : src[1];
            default: dst[0] = components & 1 ? 255 - src[0] : src[0];
            }

            src += step;
            dst += step;
        }
    }
}

static void negate16(const uint8_t *ssrc, uint8_t *ddst,
                     ptrdiff_t slinesize, ptrdiff_t dlinesize,
                     int w, int h, int max, int step,
                     int components)
{
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;

    dlinesize /= 2;
    slinesize /= 2;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            dst[x] = max - src[x];

        dst += dlinesize;
        src += slinesize;
    }
}

static void negate_packed16(const uint8_t *ssrc, uint8_t *ddst,
                            ptrdiff_t slinesize, ptrdiff_t dlinesize,
                            int w, int h, int max, int step,
                            int components)
{
    for (int y = 0; y < h; y++) {
        const uint16_t *src = (const uint16_t *)(ssrc + y * slinesize);
        uint16_t *dst = (uint16_t *)(ddst + y * dlinesize);

        for (int x = 0; x < w; x++) {
            switch (step) {
            case 4:  dst[3] = components & 8 ? max - src[3] : src[3];
            case 3:  dst[2] = components & 4 ? max - src[2] : src[2];
            case 2:  dst[1] = components & 2 ? max - src[1] : src[1];
            default: dst[0] = components & 1 ? max - src[0] : src[0];
            }

            src += step;
            dst += step;
        }
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    NegateContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int depth, vsub, hsub, ret, is_packed;
    int comp_avail;

    s->planes = s->negate_alpha ? 0xF : 0x7;
    is_packed = !(desc->flags & AV_PIX_FMT_FLAG_PLANAR) &&
                 (desc->nb_components > 1);
    if (s->requested_components != 0x77) {
        comp_avail = ((desc->flags & AV_PIX_FMT_FLAG_RGB) ? COMP_R|COMP_G|COMP_B :
                                                     COMP_Y |
                                    ((desc->nb_components > 2) ? COMP_U|COMP_V : 0)) |
                      ((desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? COMP_A : 0);
        if (s->requested_components & ~comp_avail) {
            av_log(ctx, AV_LOG_ERROR, "Requested components not available.\n");
            return AVERROR(EINVAL);
        }

        s->planes = 0;
        if (!(desc->flags & AV_PIX_FMT_FLAG_RGB)) {
            if (s->requested_components & COMP_Y)
                s->planes |= 1;
            if (s->requested_components & COMP_U)
                s->planes |= 2;
            if (s->requested_components & COMP_V)
                s->planes |= 4;
            if (s->requested_components & COMP_A)
                s->planes |= 8;
        } else {
            if (s->requested_components & COMP_R)
                s->planes |= 4;
            if (s->requested_components & COMP_G)
                s->planes |= 1;
            if (s->requested_components & COMP_B)
                s->planes |= 2;
            if (s->requested_components & COMP_A)
                s->planes |= 8;
        }
    }
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->components = 0;
    if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
        ff_fill_rgba_map(s->rgba_map, inlink->format);

        if (s->requested_components & COMP_R)
            s->components |= 1 << s->rgba_map[0];
        if (s->requested_components & COMP_G)
            s->components |= 1 << s->rgba_map[1];
        if (s->requested_components & COMP_B)
            s->components |= 1 << s->rgba_map[2];
        if (s->requested_components & COMP_A)
            s->components |= 1 << s->rgba_map[3];
    }

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    depth = desc->comp[0].depth;
    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    s->negate = depth <= 8 ? negate8 : negate16;
    if (is_packed) {
        s->negate = depth <= 8 ? negate_packed8 : negate_packed16;
        s->planes = 1;
    }
    s->max = (1 << depth) - 1;
    s->step = av_get_bits_per_pixel(desc) >> 3;
    if (depth > 8)
        s->step = s->step >> 1;

    return 0;
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    NegateContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;

    for (int p = 0; p < s->nb_planes; p++) {
        const int h = s->height[p];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;

        if (!((1 << p) & s->planes)) {
            if (out != in)
                av_image_copy_plane(out->data[p] + slice_start * out->linesize[p],
                                    out->linesize[p],
                                    in->data[p] + slice_start * in->linesize[p],
                                    in->linesize[p],
                                    s->linesize[p], slice_end - slice_start);
            continue;
        }

        s->negate(in->data[p] + slice_start * in->linesize[p],
                  out->data[p] + slice_start * out->linesize[p],
                  in->linesize[p], out->linesize[p],
                  s->width[p], slice_end - slice_start,
                  s->max, s->step, s->components);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    NegateContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.out = out;
    td.in = in;
    ff_filter_execute(ctx, filter_slice, &td, NULL,
                      FFMIN(s->height[2], ff_filter_get_nb_threads(ctx)));
    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    NegateContext *s = ctx->priv;
    int old_planes = s->planes;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    ret = config_input(ctx->inputs[0]);
    if (ret < 0)
        s->planes = old_planes;
    return ret;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_negate = {
    .name          = "negate",
    .description   = NULL_IF_CONFIG_SMALL("Negate input video."),
    .priv_size     = sizeof(NegateContext),
    .priv_class    = &negate_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
