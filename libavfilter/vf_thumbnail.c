/*
 * Copyright (c) 2011 Smartjog S.A.S, Clément Bœsch <clement.boesch@smartjog.com>
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
 * Potential thumbnail lookup filter to reduce the risk of an inappropriate
 * selection (such as a black frame) we could get with an absolute seek.
 *
 * Simplified version of algorithm by Vadim Zaliva <lord@crocodile.org>.
 * @see http://notbrainsurgery.livejournal.com/29773.html
 */

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

#define HIST_SIZE (3*256)

struct thumb_frame {
    AVFrame *buf;               ///< cached frame
    int histogram[HIST_SIZE];   ///< RGB color distribution histogram of the frame
};

typedef struct ThumbContext {
    const AVClass *class;
    int n;                      ///< current frame
    int n_frames;               ///< number of frames for analysis
    struct thumb_frame *frames; ///< the n_frames frames
    AVRational tb;              ///< copy of the input timebase to ease access

    int planewidth[4];
    int planeheight[4];
} ThumbContext;

#define OFFSET(x) offsetof(ThumbContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption thumbnail_options[] = {
    { "n", "set the frames batch size", OFFSET(n_frames), AV_OPT_TYPE_INT, {.i64=100}, 2, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(thumbnail);

static av_cold int init(AVFilterContext *ctx)
{
    ThumbContext *s = ctx->priv;

    s->frames = av_calloc(s->n_frames, sizeof(*s->frames));
    if (!s->frames) {
        av_log(ctx, AV_LOG_ERROR,
               "Allocation failure, try to lower the number of frames\n");
        return AVERROR(ENOMEM);
    }
    av_log(ctx, AV_LOG_VERBOSE, "batch size: %d frames\n", s->n_frames);
    return 0;
}

/**
 * @brief        Compute Sum-square deviation to estimate "closeness".
 * @param hist   color distribution histogram
 * @param median average color distribution histogram
 * @return       sum of squared errors
 */
static double frame_sum_square_err(const int *hist, const double *median)
{
    int i;
    double err, sum_sq_err = 0;

    for (i = 0; i < HIST_SIZE; i++) {
        err = median[i] - (double)hist[i];
        sum_sq_err += err*err;
    }
    return sum_sq_err;
}

static AVFrame *get_best_frame(AVFilterContext *ctx)
{
    AVFrame *picref;
    ThumbContext *s = ctx->priv;
    int i, j, best_frame_idx = 0;
    int nb_frames = s->n;
    double avg_hist[HIST_SIZE] = {0}, sq_err, min_sq_err = -1;

    // average histogram of the N frames
    for (j = 0; j < FF_ARRAY_ELEMS(avg_hist); j++) {
        for (i = 0; i < nb_frames; i++)
            avg_hist[j] += (double)s->frames[i].histogram[j];
        avg_hist[j] /= nb_frames;
    }

    // find the frame closer to the average using the sum of squared errors
    for (i = 0; i < nb_frames; i++) {
        sq_err = frame_sum_square_err(s->frames[i].histogram, avg_hist);
        if (i == 0 || sq_err < min_sq_err)
            best_frame_idx = i, min_sq_err = sq_err;
    }

    // free and reset everything (except the best frame buffer)
    for (i = 0; i < nb_frames; i++) {
        memset(s->frames[i].histogram, 0, sizeof(s->frames[i].histogram));
        if (i != best_frame_idx)
            av_frame_free(&s->frames[i].buf);
    }
    s->n = 0;

    // raise the chosen one
    picref = s->frames[best_frame_idx].buf;
    av_log(ctx, AV_LOG_INFO, "frame id #%d (pts_time=%f) selected "
           "from a set of %d images\n", best_frame_idx,
           picref->pts * av_q2d(s->tb), nb_frames);
    s->frames[best_frame_idx].buf = NULL;

    return picref;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    int i, j;
    AVFilterContext *ctx  = inlink->dst;
    ThumbContext *s   = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int *hist = s->frames[s->n].histogram;
    const uint8_t *p = frame->data[0];

    // keep a reference of each frame
    s->frames[s->n].buf = frame;

    // update current frame histogram
    switch (inlink->format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        for (j = 0; j < inlink->h; j++) {
            for (i = 0; i < inlink->w; i++) {
                hist[0*256 + p[i*3    ]]++;
                hist[1*256 + p[i*3 + 1]]++;
                hist[2*256 + p[i*3 + 2]]++;
            }
            p += frame->linesize[0];
        }
        break;
    case AV_PIX_FMT_RGB0:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
        for (j = 0; j < inlink->h; j++) {
            for (i = 0; i < inlink->w; i++) {
                hist[0*256 + p[i*4    ]]++;
                hist[1*256 + p[i*4 + 1]]++;
                hist[2*256 + p[i*4 + 2]]++;
            }
            p += frame->linesize[0];
        }
        break;
    case AV_PIX_FMT_0RGB:
    case AV_PIX_FMT_0BGR:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_ABGR:
        for (j = 0; j < inlink->h; j++) {
            for (i = 0; i < inlink->w; i++) {
                hist[0*256 + p[i*4 + 1]]++;
                hist[1*256 + p[i*4 + 2]]++;
                hist[2*256 + p[i*4 + 3]]++;
            }
            p += frame->linesize[0];
        }
        break;
    default:
        for (int plane = 0; plane < 3; plane++) {
            const uint8_t *p = frame->data[plane];
            for (j = 0; j < s->planeheight[plane]; j++) {
                for (i = 0; i < s->planewidth[plane]; i++)
                    hist[256*plane + p[i]]++;
                p += frame->linesize[plane];
            }
        }
        break;
    }

    // no selection until the buffer of N frames is filled up
    s->n++;
    if (s->n < s->n_frames)
        return 0;

    return ff_filter_frame(outlink, get_best_frame(ctx));
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    ThumbContext *s = ctx->priv;
    for (i = 0; i < s->n_frames && s->frames && s->frames[i].buf; i++)
        av_frame_free(&s->frames[i].buf);
    av_freep(&s->frames);
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    ThumbContext *s = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->n) {
        ret = ff_filter_frame(link, get_best_frame(ctx));
        if (ret < 0)
            return ret;
        ret = AVERROR_EOF;
    }
    if (ret < 0)
        return ret;
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ThumbContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->tb = inlink->time_base;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
        AV_PIX_FMT_ABGR,  AV_PIX_FMT_ARGB,
        AV_PIX_FMT_0BGR,  AV_PIX_FMT_0RGB,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static const AVFilterPad thumbnail_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad thumbnail_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_thumbnail = {
    .name          = "thumbnail",
    .description   = NULL_IF_CONFIG_SMALL("Select the most representative frame in a given sequence of consecutive frames."),
    .priv_size     = sizeof(ThumbContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = thumbnail_inputs,
    .outputs       = thumbnail_outputs,
    .priv_class    = &thumbnail_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
