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
#include "avfilter.h"
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

    VAR_N,
    VAR_SELECTED_N,
    VAR_PREV_SELECTED_N,

    VAR_KEY,
    VAR_POS,

    VAR_SCENE,

    VAR_VARS_NB
};

#define FIFO_SIZE 8

typedef struct {
    AVExpr *expr;
    double var_values[VAR_VARS_NB];
    int do_scene_detect;            ///< 1 if the expression requires scene detection variables, 0 otherwise
#if CONFIG_AVCODEC
    AVCodecContext *avctx;          ///< codec context required for the DSPContext (scene detect only)
    DSPContext c;                   ///< context providing optimized SAD methods   (scene detect only)
    double prev_mafd;               ///< previous MAFD                             (scene detect only)
#endif
    AVFilterBufferRef *prev_picref; ///< previous frame                            (scene detect only)
    double select;
    int cache_frames;
    AVFifoBuffer *pending_frames; ///< FIFO buffer of video frames
} SelectContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    SelectContext *select = ctx->priv;
    int ret;

    if ((ret = av_expr_parse(&select->expr, args ? args : "1",
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing expression '%s'\n", args);
        return ret;
    }

    select->pending_frames = av_fifo_alloc(FIFO_SIZE*sizeof(AVFilterBufferRef*));
    if (!select->pending_frames) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate pending frames buffer.\n");
        return AVERROR(ENOMEM);
    }

    select->do_scene_detect = args && strstr(args, "scene");
    if (select->do_scene_detect && !CONFIG_AVCODEC) {
        av_log(ctx, AV_LOG_ERROR, "Scene detection is not available without libavcodec.\n");
        return AVERROR(EINVAL);
    }
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

    if (CONFIG_AVCODEC && select->do_scene_detect) {
        select->avctx = avcodec_alloc_context3(NULL);
        if (!select->avctx)
            return AVERROR(ENOMEM);
        dsputil_init(&select->c, select->avctx);
    }
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
        int x, y;
        int64_t sad;
        double mafd, diff;
        uint8_t *p1 =      picref->data[0];
        uint8_t *p2 = prev_picref->data[0];
        const int linesize = picref->linesize[0];

        for (sad = y = 0; y < picref->video->h; y += 8)
            for (x = 0; x < linesize; x += 8)
                sad += select->c.sad[1](select,
                                        p1 + y * linesize + x,
                                        p2 + y * linesize + x,
                                        linesize, 8);
        emms_c();
        mafd = sad / (picref->video->h * picref->video->w * 3);
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

static int select_frame(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    SelectContext *select = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    double res;

    if (CONFIG_AVCODEC && select->do_scene_detect)
        select->var_values[VAR_SCENE] = get_scene_score(ctx, picref);
    if (isnan(select->var_values[VAR_START_PTS]))
        select->var_values[VAR_START_PTS] = TS2D(picref->pts);
    if (isnan(select->var_values[VAR_START_T]))
        select->var_values[VAR_START_T] = TS2D(picref->pts) * av_q2d(inlink->time_base);

    select->var_values[VAR_PTS] = TS2D(picref->pts);
    select->var_values[VAR_T  ] = TS2D(picref->pts) * av_q2d(inlink->time_base);
    select->var_values[VAR_POS] = picref->pos == -1 ? NAN : picref->pos;
    select->var_values[VAR_PREV_PTS] = TS2D(picref ->pts);

    select->var_values[VAR_INTERLACE_TYPE] =
        !picref->video->interlaced     ? INTERLACE_TYPE_P :
        picref->video->top_field_first ? INTERLACE_TYPE_T : INTERLACE_TYPE_B;
    select->var_values[VAR_PICT_TYPE] = picref->video->pict_type;

    res = av_expr_eval(select->expr, select->var_values, NULL);
    av_log(inlink->dst, AV_LOG_DEBUG,
           "n:%d pts:%d t:%f pos:%d interlace_type:%c key:%d pict_type:%c "
           "-> select:%f\n",
           (int)select->var_values[VAR_N],
           (int)select->var_values[VAR_PTS],
           select->var_values[VAR_T],
           (int)select->var_values[VAR_POS],
           select->var_values[VAR_INTERLACE_TYPE] == INTERLACE_TYPE_P ? 'P' :
           select->var_values[VAR_INTERLACE_TYPE] == INTERLACE_TYPE_T ? 'T' :
           select->var_values[VAR_INTERLACE_TYPE] == INTERLACE_TYPE_B ? 'B' : '?',
           (int)select->var_values[VAR_KEY],
           av_get_picture_type_char(select->var_values[VAR_PICT_TYPE]),
           res);

    select->var_values[VAR_N] += 1.0;

    if (res) {
        select->var_values[VAR_PREV_SELECTED_N]   = select->var_values[VAR_N];
        select->var_values[VAR_PREV_SELECTED_PTS] = select->var_values[VAR_PTS];
        select->var_values[VAR_PREV_SELECTED_T]   = select->var_values[VAR_T];
        select->var_values[VAR_SELECTED_N] += 1.0;
    }
    return res;
}

static int start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    SelectContext *select = inlink->dst->priv;

    select->select = select_frame(inlink->dst, picref);
    if (select->select) {
        AVFilterBufferRef *buf_out;
        /* frame was requested through poll_frame */
        if (select->cache_frames) {
            if (!av_fifo_space(select->pending_frames))
                av_log(inlink->dst, AV_LOG_ERROR,
                       "Buffering limit reached, cannot cache more frames\n");
            else
                av_fifo_generic_write(select->pending_frames, &picref,
                                      sizeof(picref), NULL);
            return 0;
        }
        buf_out = avfilter_ref_buffer(picref, ~0);
        if (!buf_out)
            return AVERROR(ENOMEM);
        return ff_start_frame(inlink->dst->outputs[0], buf_out);
    }

    return 0;
}

static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    SelectContext *select = inlink->dst->priv;

    if (select->select && !select->cache_frames)
        return ff_draw_slice(inlink->dst->outputs[0], y, h, slice_dir);
    return 0;
}

static int end_frame(AVFilterLink *inlink)
{
    SelectContext *select = inlink->dst->priv;

    if (select->select) {
        if (select->cache_frames)
            return 0;
        return ff_end_frame(inlink->dst->outputs[0]);
    }
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SelectContext *select = ctx->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    select->select = 0;

    if (av_fifo_size(select->pending_frames)) {
        AVFilterBufferRef *picref;
        int ret;

        av_fifo_generic_read(select->pending_frames, &picref, sizeof(picref), NULL);
        if ((ret = ff_start_frame(outlink, picref)) < 0 ||
            (ret = ff_draw_slice(outlink, 0, outlink->h, 1)) < 0 ||
            (ret = ff_end_frame(outlink)) < 0);

        return ret;
    }

    while (!select->select) {
        int ret = ff_request_frame(inlink);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int poll_frame(AVFilterLink *outlink)
{
    SelectContext *select = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int count, ret;

    if (!av_fifo_size(select->pending_frames)) {
        if ((count = ff_poll_frame(inlink)) <= 0)
            return count;
        /* request frame from input, and apply select condition to it */
        select->cache_frames = 1;
        while (count-- && av_fifo_space(select->pending_frames)) {
            ret = ff_request_frame(inlink);
            if (ret < 0)
                break;
        }
        select->cache_frames = 0;
    }

    return av_fifo_size(select->pending_frames)/sizeof(AVFilterBufferRef *);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SelectContext *select = ctx->priv;
    AVFilterBufferRef *picref;

    av_expr_free(select->expr);
    select->expr = NULL;

    while (select->pending_frames &&
           av_fifo_generic_read(select->pending_frames, &picref, sizeof(picref), NULL) == sizeof(picref))
        avfilter_unref_buffer(picref);
    av_fifo_free(select->pending_frames);
    select->pending_frames = NULL;

    if (select->do_scene_detect) {
        avfilter_unref_bufferp(&select->prev_picref);
        if (select->avctx) {
            avcodec_close(select->avctx);
            av_freep(&select->avctx);
        }
    }
}

static int query_formats(AVFilterContext *ctx)
{
    SelectContext *select = ctx->priv;

    if (!select->do_scene_detect) {
        return ff_default_query_formats(ctx);
    } else {
        static const enum PixelFormat pix_fmts[] = {
            PIX_FMT_RGB24, PIX_FMT_BGR24,
            PIX_FMT_NONE
        };
        ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    }
    return 0;
}

AVFilter avfilter_vf_select = {
    .name      = "select",
    .description = NULL_IF_CONFIG_SMALL("Select frames to pass in output."),
    .init      = init,
    .uninit    = uninit,
    .query_formats = query_formats,

    .priv_size = sizeof(SelectContext),

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .get_video_buffer = ff_null_get_video_buffer,
                                          .min_perms        = AV_PERM_PRESERVE,
                                          .config_props     = config_input,
                                          .start_frame      = start_frame,
                                          .draw_slice       = draw_slice,
                                          .end_frame        = end_frame },
                                        { .name = NULL }},
    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .poll_frame       = poll_frame,
                                          .request_frame    = request_frame, },
                                        { .name = NULL}},
};
