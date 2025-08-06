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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

#define HIST_SIZE (3*256)

struct thumb_frame {
    AVFrame *buf;               ///< cached frame
    int histogram[HIST_SIZE];   ///< RGB color distribution histogram of the frame
};

typedef struct ThumbContext {
    const AVClass *class;
    int n;                      ///< current frame
    int loglevel;
    int n_frames;               ///< number of frames for analysis
    struct thumb_frame *frames; ///< the n_frames frames
    AVRational tb;              ///< copy of the input timebase to ease access

    int nb_threads;
    int *thread_histogram;

    int planewidth[4];
    int planeheight[4];
    int planes;
    int bitdepth;
} ThumbContext;

#define OFFSET(x) offsetof(ThumbContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption thumbnail_options[] = {
    { "n", "set the frames batch size", OFFSET(n_frames), AV_OPT_TYPE_INT, {.i64=100}, 2, INT_MAX, FLAGS },
    { "log", "force stats logging level", OFFSET(loglevel), AV_OPT_TYPE_INT, {.i64 = AV_LOG_INFO}, INT_MIN, INT_MAX, FLAGS, .unit = "level" },
        { "quiet",   "logging disabled",          0, AV_OPT_TYPE_CONST, {.i64 = AV_LOG_QUIET},   0, 0, FLAGS, .unit = "level" },
        { "info",    "information logging level", 0, AV_OPT_TYPE_CONST, {.i64 = AV_LOG_INFO},    0, 0, FLAGS, .unit = "level" },
        { "verbose", "verbose logging level",     0, AV_OPT_TYPE_CONST, {.i64 = AV_LOG_VERBOSE}, 0, 0, FLAGS, .unit = "level" },
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
    if (s->loglevel != AV_LOG_QUIET)
        av_log(ctx, s->loglevel, "frame id #%d (pts_time=%f) selected "
               "from a set of %d images\n", best_frame_idx,
               picref->pts * av_q2d(s->tb), nb_frames);
    s->frames[best_frame_idx].buf = NULL;

    return picref;
}

static void get_hist8(int *hist, const uint8_t *p, ptrdiff_t stride,
                      ptrdiff_t width, ptrdiff_t height)
{
    int shist[4][256] = {0};

    const int width4 = width & ~3;
    while (height--) {
        for (int x = 0; x < width4; x += 4) {
            const uint32_t v = AV_RN32(&p[x]);
            shist[0][(uint8_t) (v >>  0)]++;
            shist[1][(uint8_t) (v >>  8)]++;
            shist[2][(uint8_t) (v >> 16)]++;
            shist[3][(uint8_t) (v >> 24)]++;
        }
        /* handle tail */
        for (int x = width4; x < width; x++)
            hist[p[x]]++;
        p += stride;
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 256; j++)
            hist[j] += shist[i][j];
    }
}

static void get_hist16(int *hist, const uint8_t *p, ptrdiff_t stride,
                       ptrdiff_t width, ptrdiff_t height, int shift)
{
    int shist[4][256] = {0};

    const int width4 = width & ~3;
    while (height--) {
        const uint16_t *p16 = (const uint16_t *) p;
        for (int x = 0; x < width4; x += 4) {
            const uint64_t v = AV_RN64(&p16[x]);
            shist[0][(uint8_t) (v >> (shift +  0))]++;
            shist[1][(uint8_t) (v >> (shift + 16))]++;
            shist[2][(uint8_t) (v >> (shift + 32))]++;
            shist[3][(uint8_t) (v >> (shift + 48))]++;
        }
        /* handle tail */
        for (int x = width4; x < width; x++)
            hist[(uint8_t) (p16[x] >> shift)]++;
        p += stride;
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 256; j++)
            hist[j] += shist[i][j];
    }
}

static int do_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThumbContext *s = ctx->priv;
    AVFrame *frame = arg;
    int *hist = s->thread_histogram + HIST_SIZE * jobnr;
    const int h = frame->height;
    const int w = frame->width;
    const int slice_start = (h * jobnr) / nb_jobs;
    const int slice_end = (h * (jobnr+1)) / nb_jobs;
    const uint8_t *p = frame->data[0] + slice_start * frame->linesize[0];

    memset(hist, 0, sizeof(*hist) * HIST_SIZE);

    switch (frame->format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        for (int j = slice_start; j < slice_end; j++) {
            for (int i = 0; i < w; i++) {
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
        for (int j = slice_start; j < slice_end; j++) {
            for (int i = 0; i < w; i++) {
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
        for (int j = slice_start; j < slice_end; j++) {
            for (int i = 0; i < w; i++) {
                hist[0*256 + p[i*4 + 1]]++;
                hist[1*256 + p[i*4 + 2]]++;
                hist[2*256 + p[i*4 + 3]]++;
            }
            p += frame->linesize[0];
        }
        break;
    default:
        for (int plane = 0; plane < s->planes; plane++) {
            const int slice_start = (s->planeheight[plane] * jobnr) / nb_jobs;
            const int slice_end = (s->planeheight[plane] * (jobnr+1)) / nb_jobs;
            const uint8_t *p = frame->data[plane] + slice_start * frame->linesize[plane];
            const ptrdiff_t linesize = frame->linesize[plane];
            const int planewidth = s->planewidth[plane];
            int *hhist = hist + 256 * plane;
            if (s->bitdepth > 8) {
                get_hist16(hhist, p, linesize, planewidth, slice_end - slice_start,
                           s->bitdepth - 8);
            } else {
                get_hist8(hhist, p, linesize, planewidth, slice_end - slice_start);
            }
        }
        break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx  = inlink->dst;
    ThumbContext *s   = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int *hist = s->frames[s->n].histogram;

    // keep a reference of each frame
    s->frames[s->n].buf = frame;

    ff_filter_execute(ctx, do_slice, frame, NULL,
                      FFMIN(frame->height, s->nb_threads));

    // update current frame histogram
    for (int j = 0; j < FFMIN(frame->height, s->nb_threads); j++) {
        int *thread_histogram = s->thread_histogram + HIST_SIZE * j;

        for (int i = 0; i < HIST_SIZE; i++)
            hist[i] += thread_histogram[i];
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
    av_freep(&s->thread_histogram);
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

    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->thread_histogram = av_calloc(HIST_SIZE, s->nb_threads * sizeof(*s->thread_histogram));
    if (!s->thread_histogram)
        return AVERROR(ENOMEM);

    s->tb = inlink->time_base;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planes         = av_pix_fmt_count_planes(inlink->format) - !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    s->bitdepth       = desc->comp[0].depth;

    return 0;
}

static const enum AVPixelFormat packed_rgb_fmts[] = {
    AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
    AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
    AV_PIX_FMT_ABGR,  AV_PIX_FMT_ARGB,
    AV_PIX_FMT_0BGR,  AV_PIX_FMT_0RGB,
    AV_PIX_FMT_NONE
};

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const AVPixFmtDescriptor *desc = NULL;
    AVFilterFormats *formats;

    formats = ff_make_format_list(packed_rgb_fmts);
    if (!formats)
        return AVERROR(ENOMEM);


    while ((desc = av_pix_fmt_desc_next(desc))) {
        int color_comps = desc->nb_components - !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);
        if ((color_comps == 1 || (desc->flags & AV_PIX_FMT_FLAG_PLANAR)) &&
            !(desc->flags & (AV_PIX_FMT_FLAG_FLOAT | AV_PIX_FMT_FLAG_BITSTREAM)) &&
            (desc->comp[0].depth <= 8 || HAVE_BIGENDIAN == !!(desc->flags & AV_PIX_FMT_FLAG_BE)) &&
            (desc->nb_components < 3 || desc->comp[1].plane != desc->comp[2].plane) &&
            desc->comp[0].depth <= 16)
        {
            int ret = ff_add_format(&formats, av_pix_fmt_desc_get_id(desc));
            if (ret < 0)
                return ret;
        }
    }

    return ff_set_common_formats2(ctx, cfg_in, cfg_out, formats);
}

static const AVFilterPad thumbnail_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad thumbnail_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const FFFilter ff_vf_thumbnail = {
    .p.name        = "thumbnail",
    .p.description = NULL_IF_CONFIG_SMALL("Select the most representative frame in a given sequence of consecutive frames."),
    .p.priv_class  = &thumbnail_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                     AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(ThumbContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(thumbnail_inputs),
    FILTER_OUTPUTS(thumbnail_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
