/*
 * Copyright (c) 2011 Stefano Sabatini
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

/**
 * @file
 * filter for selecting which frame passes in the filterchain
 */

#include "libavutil/eval.h"
#include "libavutil/fifo.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#if CONFIG_AVCODEC
#include "libavcodec/dsputil.h"
#endif

static const char *const var_names[] = {
    "TB",                ///< timebase

    "pts",               ///< original pts in the file of the frame
    "start_pts",         ///< first PTS in the stream, expressed in TB units
    "prev_pts",          ///< previous frame PTS
    "prev_selected_pts", ///< previous selected frame PTS

    "t",                 ///< first PTS in seconds
    "start_t",           ///< first PTS in the stream, expressed in seconds
    "prev_t",            ///< previous frame time
    "prev_selected_t",   ///< previously selected time

    "pict_type",         ///< the type of picture in the movie
    "I",
    "P",
    "B",
    "S",
    "SI",
    "SP",
    "BI",

    "interlace_type",    ///< the frame interlace type
    "PROGRESSIVE",
    "TOPFIRST",
    "BOTTOMFIRST",

    "consumed_samples_n",///< number of samples consumed by the filter (only audio)
    "samples_n",         ///< number of samples in the current frame (only audio)
    "sample_rate",       ///< sample rate (only audio)

    "n",                 ///< frame number (starting from zero)
    "selected_n",        ///< selected frame number (starting from zero)
    "prev_selected_n",   ///< number of the last selected frame

    "key",               ///< tell if the frame is a key frame
    "pos",               ///< original position in the file of the frame

    "scene",

    NULL
};

enum var_name {
    VAR_TB,

    VAR_PTS,
    VAR_START_PTS,
    VAR_PREV_PTS,
    VAR_PREV_SELECTED_PTS,

    VAR_T,
    VAR_START_T,
    VAR_PREV_T,
    VAR_PREV_SELECTED_T,

    VAR_PICT_TYPE,
    VAR_PICT_TYPE_I,
    VAR_PICT_TYPE_P,
    VAR_PICT_TYPE_B,
    VAR_PICT_TYPE_S,
    VAR_PICT_TYPE_SI,
    VAR_PICT_TYPE_SP,
    VAR_PICT_TYPE_BI,

    VAR_INTERLACE_TYPE,
    VAR_INTERLACE_TYPE_P,
    VAR_INTERLACE_TYPE_T,
    VAR_INTERLACE_TYPE_B,

    VAR_CONSUMED_SAMPLES_N,
    VAR_SAMPLES_N,
    VAR_SAMPLE_RATE,

    VAR_N,
    VAR_SELECTED_N,
    VAR_PREV_SELECTED_N,

    VAR_KEY,
    VAR_POS,

    VAR_SCENE,

    VAR_VARS_NB
};

typedef struct {
    const AVClass *class;
    AVExpr *expr;
    char *expr_str;
    double var_values[VAR_VARS_NB];
    int do_scene_detect;            ///< 1 if the expression requires scene detection variables, 0 otherwise
#if CONFIG_AVCODEC
    AVCodecContext *avctx;          ///< codec context required for the DSPContext (scene detect only)
    DSPContext c;                   ///< context providing optimized SAD methods   (scene detect only)
    double prev_mafd;               ///< previous MAFD                             (scene detect only)
#endif
    AVFilterBufferRef *prev_picref; ///< previous frame                            (scene detect only)
    double select;
} SelectContext;

#define OFFSET(x) offsetof(SelectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption options[] = {
    { "expr", "set selection expression", OFFSET(expr_str), AV_OPT_TYPE_STRING, {.str = "1"}, 0, 0, FLAGS },
    { "e",    "set selection expression", OFFSET(expr_str), AV_OPT_TYPE_STRING, {.str = "1"}, 0, 0, FLAGS },
    {NULL},
};

static av_cold int init(AVFilterContext *ctx, const char *args, const AVClass *class)
{
    SelectContext *select = ctx->priv;
    const char *shorthand[] = { "expr", NULL };
    int ret;

    select->class = class;
    av_opt_set_defaults(select);

    if ((ret = av_opt_set_from_string(select, args, shorthand, "=", ":")) < 0)
        return ret;

    if ((ret = av_expr_parse(&select->expr, select->expr_str,
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing expression '%s'\n", select->expr_str);
        return ret;
    }
    select->do_scene_detect = !!strstr(select->expr_str, "scene");

    return 0;
}

#define INTERLACE_TYPE_P 0
#define INTERLACE_TYPE_T 1
#define INTERLACE_TYPE_B 2

static int config_input(AVFilterLink *inlink)
{
    SelectContext *select = inlink->dst->priv;

    select->var_values[VAR_N]          = 0.0;
    select->var_values[VAR_SELECTED_N] = 0.0;

    select->var_values[VAR_TB] = av_q2d(inlink->time_base);

    select->var_values[VAR_PREV_PTS]          = NAN;
    select->var_values[VAR_PREV_SELECTED_PTS] = NAN;
    select->var_values[VAR_PREV_SELECTED_T]   = NAN;
    select->var_values[VAR_PREV_T]            = NAN;
    select->var_values[VAR_START_PTS]         = NAN;
    select->var_values[VAR_START_T]           = NAN;

    select->var_values[VAR_PICT_TYPE_I]  = AV_PICTURE_TYPE_I;
    select->var_values[VAR_PICT_TYPE_P]  = AV_PICTURE_TYPE_P;
    select->var_values[VAR_PICT_TYPE_B]  = AV_PICTURE_TYPE_B;
    select->var_values[VAR_PICT_TYPE_SI] = AV_PICTURE_TYPE_SI;
    select->var_values[VAR_PICT_TYPE_SP] = AV_PICTURE_TYPE_SP;

    select->var_values[VAR_INTERLACE_TYPE_P] = INTERLACE_TYPE_P;
    select->var_values[VAR_INTERLACE_TYPE_T] = INTERLACE_TYPE_T;
    select->var_values[VAR_INTERLACE_TYPE_B] = INTERLACE_TYPE_B;

    select->var_values[VAR_PICT_TYPE]         = NAN;
    select->var_values[VAR_INTERLACE_TYPE]    = NAN;
    select->var_values[VAR_SCENE]             = NAN;
    select->var_values[VAR_CONSUMED_SAMPLES_N] = NAN;
    select->var_values[VAR_SAMPLES_N]          = NAN;

    select->var_values[VAR_SAMPLE_RATE] =
        inlink->type == AVMEDIA_TYPE_AUDIO ? inlink->sample_rate : NAN;

#if CONFIG_AVCODEC
    if (select->do_scene_detect) {
        select->avctx = avcodec_alloc_context3(NULL);
        if (!select->avctx)
            return AVERROR(ENOMEM);
        dsputil_init(&select->c, select->avctx);
    }
#endif
    return 0;
}

#if CONFIG_AVCODEC
static double get_scene_score(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    double ret = 0;
    SelectContext *select = ctx->priv;
    AVFilterBufferRef *prev_picref = select->prev_picref;

    if (prev_picref &&
        picref->video->h    == prev_picref->video->h &&
        picref->video->w    == prev_picref->video->w &&
        picref->linesize[0] == prev_picref->linesize[0]) {
        int x, y, nb_sad = 0;
        int64_t sad = 0;
        double mafd, diff;
        uint8_t *p1 =      picref->data[0];
        uint8_t *p2 = prev_picref->data[0];
        const int linesize = picref->linesize[0];

        for (y = 0; y < picref->video->h - 8; y += 8) {
            for (x = 0; x < picref->video->w*3 - 8; x += 8) {
                sad += select->c.sad[1](select, p1 + x, p2 + x,
                                        linesize, 8);
                nb_sad += 8 * 8;
            }
            p1 += 8 * linesize;
            p2 += 8 * linesize;
        }
        emms_c();
        mafd = nb_sad ? sad / nb_sad : 0;
        diff = fabs(mafd - select->prev_mafd);
        ret  = av_clipf(FFMIN(mafd, diff) / 100., 0, 1);
        select->prev_mafd = mafd;
        avfilter_unref_buffer(prev_picref);
    }
    select->prev_picref = avfilter_ref_buffer(picref, ~0);
    return ret;
}
#endif

#define D2TS(d)  (isnan(d) ? AV_NOPTS_VALUE : (int64_t)(d))
#define TS2D(ts) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts))

static int select_frame(AVFilterContext *ctx, AVFilterBufferRef *ref)
{
    SelectContext *select = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    double res;

    if (isnan(select->var_values[VAR_START_PTS]))
        select->var_values[VAR_START_PTS] = TS2D(ref->pts);
    if (isnan(select->var_values[VAR_START_T]))
        select->var_values[VAR_START_T] = TS2D(ref->pts) * av_q2d(inlink->time_base);

    select->var_values[VAR_PTS] = TS2D(ref->pts);
    select->var_values[VAR_T  ] = TS2D(ref->pts) * av_q2d(inlink->time_base);
    select->var_values[VAR_POS] = ref->pos == -1 ? NAN : ref->pos;

    switch (inlink->type) {
    case AVMEDIA_TYPE_AUDIO:
        select->var_values[VAR_SAMPLES_N] = ref->audio->nb_samples;
        break;

    case AVMEDIA_TYPE_VIDEO:
        select->var_values[VAR_INTERLACE_TYPE] =
            !ref->video->interlaced ? INTERLACE_TYPE_P :
        ref->video->top_field_first ? INTERLACE_TYPE_T : INTERLACE_TYPE_B;
        select->var_values[VAR_PICT_TYPE] = ref->video->pict_type;
#if CONFIG_AVCODEC
        if (select->do_scene_detect) {
            char buf[32];
            select->var_values[VAR_SCENE] = get_scene_score(ctx, ref);
            // TODO: document metadata
            snprintf(buf, sizeof(buf), "%f", select->var_values[VAR_SCENE]);
            av_dict_set(&ref->metadata, "lavfi.scene_score", buf, 0);
        }
#endif
        break;
    }

    res = av_expr_eval(select->expr, select->var_values, NULL);
    av_log(inlink->dst, AV_LOG_DEBUG,
           "n:%f pts:%f t:%f pos:%f key:%d",
           select->var_values[VAR_N],
           select->var_values[VAR_PTS],
           select->var_values[VAR_T],
           select->var_values[VAR_POS],
           (int)select->var_values[VAR_KEY]);

    switch (inlink->type) {
    case AVMEDIA_TYPE_VIDEO:
        av_log(inlink->dst, AV_LOG_DEBUG, " interlace_type:%c pict_type:%c scene:%f",
               select->var_values[VAR_INTERLACE_TYPE] == INTERLACE_TYPE_P ? 'P' :
               select->var_values[VAR_INTERLACE_TYPE] == INTERLACE_TYPE_T ? 'T' :
               select->var_values[VAR_INTERLACE_TYPE] == INTERLACE_TYPE_B ? 'B' : '?',
               av_get_picture_type_char(select->var_values[VAR_PICT_TYPE]),
               select->var_values[VAR_SCENE]);
        break;
    case AVMEDIA_TYPE_AUDIO:
        av_log(inlink->dst, AV_LOG_DEBUG, " samples_n:%d consumed_samples_n:%d",
               (int)select->var_values[VAR_SAMPLES_N],
               (int)select->var_values[VAR_CONSUMED_SAMPLES_N]);
        break;
    }

    av_log(inlink->dst, AV_LOG_DEBUG, " -> select:%f\n", res);

    if (res) {
        select->var_values[VAR_PREV_SELECTED_N]   = select->var_values[VAR_N];
        select->var_values[VAR_PREV_SELECTED_PTS] = select->var_values[VAR_PTS];
        select->var_values[VAR_PREV_SELECTED_T]   = select->var_values[VAR_T];
        select->var_values[VAR_SELECTED_N] += 1.0;
        if (inlink->type == AVMEDIA_TYPE_AUDIO)
            select->var_values[VAR_CONSUMED_SAMPLES_N] += ref->audio->nb_samples;
    }

    select->var_values[VAR_N] += 1.0;
    select->var_values[VAR_PREV_PTS] = select->var_values[VAR_PTS];
    select->var_values[VAR_PREV_T]   = select->var_values[VAR_T];

    return res;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *frame)
{
    SelectContext *select = inlink->dst->priv;

    select->select = select_frame(inlink->dst, frame);
    if (select->select)
        return ff_filter_frame(inlink->dst->outputs[0], frame);

    avfilter_unref_bufferp(&frame);
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SelectContext *select = ctx->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    select->select = 0;

    do {
        int ret = ff_request_frame(inlink);
        if (ret < 0)
            return ret;
    } while (!select->select);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SelectContext *select = ctx->priv;

    av_expr_free(select->expr);
    select->expr = NULL;
    av_opt_free(select);

#if CONFIG_AVCODEC
    if (select->do_scene_detect) {
        avfilter_unref_bufferp(&select->prev_picref);
        if (select->avctx) {
            avcodec_close(select->avctx);
            av_freep(&select->avctx);
        }
    }
#endif
}

static int query_formats(AVFilterContext *ctx)
{
    SelectContext *select = ctx->priv;

    if (!select->do_scene_detect) {
        return ff_default_query_formats(ctx);
    } else {
        static const enum AVPixelFormat pix_fmts[] = {
            AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
            AV_PIX_FMT_NONE
        };
        ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    }
    return 0;
}

#if CONFIG_ASELECT_FILTER

#define aselect_options options
AVFILTER_DEFINE_CLASS(aselect);

static av_cold int aselect_init(AVFilterContext *ctx, const char *args)
{
    SelectContext *select = ctx->priv;
    int ret;

    if ((ret = init(ctx, args, &aselect_class)) < 0)
        return ret;

    if (select->do_scene_detect) {
        av_log(ctx, AV_LOG_ERROR, "Scene detection is ignored in aselect filter\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static const AVFilterPad avfilter_af_aselect_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_AUDIO,
        .get_audio_buffer = ff_null_get_audio_buffer,
        .config_props     = config_input,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_aselect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter avfilter_af_aselect = {
    .name      = "aselect",
    .description = NULL_IF_CONFIG_SMALL("Select audio frames to pass in output."),
    .init      = aselect_init,
    .uninit    = uninit,
    .priv_size = sizeof(SelectContext),
    .inputs    = avfilter_af_aselect_inputs,
    .outputs   = avfilter_af_aselect_outputs,
    .priv_class = &aselect_class,
};
#endif /* CONFIG_ASELECT_FILTER */

#if CONFIG_SELECT_FILTER

#define select_options options
AVFILTER_DEFINE_CLASS(select);

static av_cold int select_init(AVFilterContext *ctx, const char *args)
{
    SelectContext *select = ctx->priv;
    int ret;

    if ((ret = init(ctx, args, &select_class)) < 0)
        return ret;

    if (select->do_scene_detect && !CONFIG_AVCODEC) {
        av_log(ctx, AV_LOG_ERROR, "Scene detection is not available without libavcodec.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static const AVFilterPad avfilter_vf_select_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .min_perms        = AV_PERM_PRESERVE,
        .config_props     = config_input,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_select_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_select = {
    .name      = "select",
    .description = NULL_IF_CONFIG_SMALL("Select video frames to pass in output."),
    .init      = select_init,
    .uninit    = uninit,
    .query_formats = query_formats,

    .priv_size = sizeof(SelectContext),

    .inputs    = avfilter_vf_select_inputs,
    .outputs   = avfilter_vf_select_outputs,
    .priv_class = &select_class,
};
#endif /* CONFIG_SELECT_FILTER */
