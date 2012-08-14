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

#include "avfilter.h"
#include "internal.h"

#define HIST_SIZE (3*256)

struct thumb_frame {
    AVFilterBufferRef *buf;     ///< cached frame
    int histogram[HIST_SIZE];   ///< RGB color distribution histogram of the frame
};

typedef struct {
    int n;                      ///< current frame
    int n_frames;               ///< number of frames for analysis
    struct thumb_frame *frames; ///< the n_frames frames
} ThumbContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ThumbContext *thumb = ctx->priv;

    if (!args) {
        thumb->n_frames = 100;
    } else {
        int n = sscanf(args, "%d", &thumb->n_frames);
        if (n != 1 || thumb->n_frames < 2) {
            thumb->n_frames = 0;
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid number of frames specified (minimum is 2).\n");
            return AVERROR(EINVAL);
        }
    }
    thumb->frames = av_calloc(thumb->n_frames, sizeof(*thumb->frames));
    if (!thumb->frames) {
        av_log(ctx, AV_LOG_ERROR,
               "Allocation failure, try to lower the number of frames\n");
        return AVERROR(ENOMEM);
    }
    av_log(ctx, AV_LOG_VERBOSE, "batch size: %d frames\n", thumb->n_frames);
    return 0;
}

static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    int i, j;
    AVFilterContext *ctx = inlink->dst;
    ThumbContext *thumb = ctx->priv;
    int *hist = thumb->frames[thumb->n].histogram;
    AVFilterBufferRef *picref = inlink->cur_buf;
    const uint8_t *p = picref->data[0] + y * picref->linesize[0];

    // update current frame RGB histogram
    for (j = 0; j < h; j++) {
        for (i = 0; i < inlink->w; i++) {
            hist[0*256 + p[i*3    ]]++;
            hist[1*256 + p[i*3 + 1]]++;
            hist[2*256 + p[i*3 + 2]]++;
        }
        p += picref->linesize[0];
    }
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

static int  end_frame(AVFilterLink *inlink)
{
    int i, j, best_frame_idx = 0;
    double avg_hist[HIST_SIZE] = {0}, sq_err, min_sq_err = -1;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    ThumbContext *thumb   = inlink->dst->priv;
    AVFilterContext *ctx  = inlink->dst;
    AVFilterBufferRef *picref;

    // keep a reference of each frame
    thumb->frames[thumb->n].buf = inlink->cur_buf;
    inlink->cur_buf = NULL;

    // no selection until the buffer of N frames is filled up
    if (thumb->n < thumb->n_frames - 1) {
        thumb->n++;
        return 0;
    }

    // average histogram of the N frames
    for (j = 0; j < FF_ARRAY_ELEMS(avg_hist); j++) {
        for (i = 0; i < thumb->n_frames; i++)
            avg_hist[j] += (double)thumb->frames[i].histogram[j];
        avg_hist[j] /= thumb->n_frames;
    }

    // find the frame closer to the average using the sum of squared errors
    for (i = 0; i < thumb->n_frames; i++) {
        sq_err = frame_sum_square_err(thumb->frames[i].histogram, avg_hist);
        if (i == 0 || sq_err < min_sq_err)
            best_frame_idx = i, min_sq_err = sq_err;
    }

    // free and reset everything (except the best frame buffer)
    for (i = 0; i < thumb->n_frames; i++) {
        memset(thumb->frames[i].histogram, 0, sizeof(thumb->frames[i].histogram));
        if (i == best_frame_idx)
            continue;
        avfilter_unref_buffer(thumb->frames[i].buf);
        thumb->frames[i].buf = NULL;
    }
    thumb->n = 0;

    // raise the chosen one
    picref = thumb->frames[best_frame_idx].buf;
    av_log(ctx, AV_LOG_INFO, "frame id #%d (pts_time=%f) selected\n",
           best_frame_idx, picref->pts * av_q2d(inlink->time_base));
    ff_start_frame(outlink, picref);
    thumb->frames[best_frame_idx].buf = NULL;
    ff_draw_slice(outlink, 0, inlink->h, 1);
    return ff_end_frame(outlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    ThumbContext *thumb = ctx->priv;
    for (i = 0; i < thumb->n_frames && thumb->frames[i].buf; i++) {
        avfilter_unref_buffer(thumb->frames[i].buf);
        thumb->frames[i].buf = NULL;
    }
    av_freep(&thumb->frames);
}

static int null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref) { return 0; }

static int request_frame(AVFilterLink *link)
{
    ThumbContext *thumb = link->src->priv;

    /* loop until a frame thumbnail is available (when a frame is queued,
     * thumb->n is reset to zero) */
    do {
        int ret = ff_request_frame(link->src->inputs[0]);
        if (ret < 0)
            return ret;
    } while (thumb->n);
    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    ThumbContext *thumb  = link->src->priv;
    AVFilterLink *inlink = link->src->inputs[0];
    int ret, available_frames = ff_poll_frame(inlink);

    /* If the input link is not able to provide any frame, we can't do anything
     * at the moment and thus have zero thumbnail available. */
    if (!available_frames)
        return 0;

    /* Since at least one frame is available and the next frame will allow us
     * to compute a thumbnail, we can return 1 frame. */
    if (thumb->n == thumb->n_frames - 1)
        return 1;

    /* we have some frame(s) available in the input link, but not yet enough to
     * output a thumbnail, so we request more */
    ret = ff_request_frame(inlink);
    return ret < 0 ? ret : 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGB24, PIX_FMT_BGR24,
        PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

AVFilter avfilter_vf_thumbnail = {
    .name          = "thumbnail",
    .description   = NULL_IF_CONFIG_SMALL("Select the most representative frame in a given sequence of consecutive frames."),
    .priv_size     = sizeof(ThumbContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = (const AVFilterPad[]) {
        {   .name             = "default",
            .type             = AVMEDIA_TYPE_VIDEO,
            .get_video_buffer = ff_null_get_video_buffer,
            .min_perms        = AV_PERM_PRESERVE,
            .start_frame      = null_start_frame,
            .draw_slice       = draw_slice,
            .end_frame        = end_frame,
        },{ .name = NULL }
    },
    .outputs       = (const AVFilterPad[]) {
        {   .name             = "default",
            .type             = AVMEDIA_TYPE_VIDEO,
            .request_frame    = request_frame,
            .poll_frame       = poll_frame,
        },{ .name = NULL }
    },
};
