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

/**
 * @file
 * video freeze detection filter
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"

#include "avfilter.h"
#include "filters.h"
#include "scene_sad.h"

typedef struct FreezeDetectContext {
    const AVClass *class;

    ptrdiff_t width[4];
    ptrdiff_t height[4];
    ff_scene_sad_fn sad;
    int bitdepth;
    AVFrame *reference_frame;
    int64_t n;
    int64_t reference_n;
    int frozen;

    double noise;
    int64_t duration;            ///< minimum duration of frozen frame until notification
} FreezeDetectContext;

#define OFFSET(x) offsetof(FreezeDetectContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM

static const AVOption freezedetect_options[] = {
    { "n",                   "set noise tolerance",                       OFFSET(noise),  AV_OPT_TYPE_DOUBLE,   {.dbl=0.001},     0,       1.0, V|F },
    { "noise",               "set noise tolerance",                       OFFSET(noise),  AV_OPT_TYPE_DOUBLE,   {.dbl=0.001},     0,       1.0, V|F },
    { "d",                   "set minimum duration in seconds",        OFFSET(duration),  AV_OPT_TYPE_DURATION, {.i64=2000000},   0, INT64_MAX, V|F },
    { "duration",            "set minimum duration in seconds",        OFFSET(duration),  AV_OPT_TYPE_DURATION, {.i64=2000000},   0, INT64_MAX, V|F },

    {NULL}
};

AVFILTER_DEFINE_CLASS(freezedetect);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUYV422, AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_UYVY422, AV_PIX_FMT_NV12, AV_PIX_FMT_NV21, AV_PIX_FMT_ARGB,
    AV_PIX_FMT_RGBA, AV_PIX_FMT_ABGR, AV_PIX_FMT_BGRA, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YA8, AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV422P9, AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP16, AV_PIX_FMT_YUVA422P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10,
    AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA444P16, AV_PIX_FMT_NV16, AV_PIX_FMT_YVYU422,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP16, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV422P14,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14, AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_GBRP14, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV440P12, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP10,
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_GRAY14,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FreezeDetectContext *s = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);

    s->bitdepth = pix_desc->comp[0].depth;

    for (int plane = 0; plane < 4; plane++) {
        ptrdiff_t line_size = av_image_get_linesize(inlink->format, inlink->w, plane);
        s->width[plane] = line_size >> (s->bitdepth > 8);
        s->height[plane] = inlink->h >> ((plane == 1 || plane == 2) ? pix_desc->log2_chroma_h : 0);
    }

    s->sad = ff_scene_sad_get_fn(s->bitdepth == 8 ? 8 : 16);
    if (!s->sad)
        return AVERROR(EINVAL);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FreezeDetectContext *s = ctx->priv;
    av_frame_free(&s->reference_frame);
}

static int is_frozen(FreezeDetectContext *s, AVFrame *reference, AVFrame *frame)
{
    uint64_t sad = 0;
    uint64_t count = 0;
    double mafd;
    for (int plane = 0; plane < 4; plane++) {
        if (s->width[plane]) {
            uint64_t plane_sad;
            s->sad(frame->data[plane], frame->linesize[plane],
                   reference->data[plane], reference->linesize[plane],
                   s->width[plane], s->height[plane], &plane_sad);
            sad += plane_sad;
            count += s->width[plane] * s->height[plane];
        }
    }
    emms_c();
    mafd = (double)sad / count / (1ULL << s->bitdepth);
    return (mafd <= s->noise);
}

static int set_meta(FreezeDetectContext *s, AVFrame *frame, const char *key, const char *value)
{
    av_log(s, AV_LOG_INFO, "%s: %s\n", key, value);
    return av_dict_set(&frame->metadata, key, value, 0);
}

static int activate(AVFilterContext *ctx)
{
    int ret;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    FreezeDetectContext *s = ctx->priv;
    AVFrame *frame;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_frame(inlink, &frame);
    if (ret < 0)
        return ret;

    if (frame) {
        int frozen = 0;
        s->n++;

        if (s->reference_frame) {
            int64_t duration;
            if (s->reference_frame->pts == AV_NOPTS_VALUE || frame->pts == AV_NOPTS_VALUE || frame->pts < s->reference_frame->pts)     // Discontinuity?
                duration = inlink->frame_rate.num > 0 ? av_rescale_q(s->n - s->reference_n, av_inv_q(inlink->frame_rate), AV_TIME_BASE_Q) : 0;
            else
                duration = av_rescale_q(frame->pts - s->reference_frame->pts, inlink->time_base, AV_TIME_BASE_Q);

            frozen = is_frozen(s, s->reference_frame, frame);
            if (duration >= s->duration) {
                if (!s->frozen)
                    set_meta(s, frame, "lavfi.freezedetect.freeze_start", av_ts2timestr(s->reference_frame->pts, &inlink->time_base));
                if (!frozen) {
                    set_meta(s, frame, "lavfi.freezedetect.freeze_duration", av_ts2timestr(duration, &AV_TIME_BASE_Q));
                    set_meta(s, frame, "lavfi.freezedetect.freeze_end", av_ts2timestr(frame->pts, &inlink->time_base));
                }
                s->frozen = frozen;
            }
        }

        if (!frozen) {
            av_frame_free(&s->reference_frame);
            s->reference_frame = av_frame_clone(frame);
            s->reference_n = s->n;
            if (!s->reference_frame) {
                av_frame_free(&frame);
                return AVERROR(ENOMEM);
            }
        }
        return ff_filter_frame(outlink, frame);
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static const AVFilterPad freezedetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

static const AVFilterPad freezedetect_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_freezedetect = {
    .name          = "freezedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detects frozen video input."),
    .priv_size     = sizeof(FreezeDetectContext),
    .priv_class    = &freezedetect_class,
    .uninit        = uninit,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(freezedetect_inputs),
    FILTER_OUTPUTS(freezedetect_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .activate      = activate,
};
