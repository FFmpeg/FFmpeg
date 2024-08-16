/*
 * Copyright (c) 2010 Mark Heath mjpeg0 @ silicontrip dot org
 * Copyright (c) 2014 Clément Bœsch
 * Copyright (c) 2014 Dave Rice @dericed
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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "filters.h"

enum FilterMode {
    FILTER_NONE = -1,
    FILTER_TOUT,
    FILTER_VREP,
    FILTER_BRNG,
    FILT_NUMB
};

typedef struct SignalstatsContext {
    const AVClass *class;
    int chromah;    // height of chroma plane
    int chromaw;    // width of chroma plane
    int hsub;       // horizontal subsampling
    int vsub;       // vertical subsampling
    int depth;      // pixel depth
    int fs;         // pixel count per frame
    int cfs;        // pixel count per frame of chroma planes
    int outfilter;  // FilterMode
    int filters;
    AVFrame *frame_prev;
    uint8_t rgba_color[4];
    int yuv_color[3];
    int nb_jobs;
    int *jobs_rets;

    int maxsize;    // history stats array size
    int *histy, *histu, *histv, *histsat;

    AVFrame *frame_sat;
    AVFrame *frame_hue;
} SignalstatsContext;

typedef struct ThreadData {
    const AVFrame *in;
    AVFrame *out;
} ThreadData;

typedef struct ThreadDataHueSatMetrics {
    const AVFrame *src;
    AVFrame *dst_sat, *dst_hue;
} ThreadDataHueSatMetrics;

#define OFFSET(x) offsetof(SignalstatsContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption signalstats_options[] = {
    {"stat", "set statistics filters", OFFSET(filters), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, INT_MAX, FLAGS, .unit = "filters"},
        {"tout", "analyze pixels for temporal outliers",                0, AV_OPT_TYPE_CONST, {.i64=1<<FILTER_TOUT}, 0, 0, FLAGS, .unit = "filters"},
        {"vrep", "analyze video lines for vertical line repetition",    0, AV_OPT_TYPE_CONST, {.i64=1<<FILTER_VREP}, 0, 0, FLAGS, .unit = "filters"},
        {"brng", "analyze for pixels outside of broadcast range",       0, AV_OPT_TYPE_CONST, {.i64=1<<FILTER_BRNG}, 0, 0, FLAGS, .unit = "filters"},
    {"out", "set video filter", OFFSET(outfilter), AV_OPT_TYPE_INT, {.i64=FILTER_NONE}, -1, FILT_NUMB-1, FLAGS, .unit = "out"},
        {"tout", "highlight pixels that depict temporal outliers",              0, AV_OPT_TYPE_CONST, {.i64=FILTER_TOUT}, 0, 0, FLAGS, .unit = "out"},
        {"vrep", "highlight video lines that depict vertical line repetition",  0, AV_OPT_TYPE_CONST, {.i64=FILTER_VREP}, 0, 0, FLAGS, .unit = "out"},
        {"brng", "highlight pixels that are outside of broadcast range",        0, AV_OPT_TYPE_CONST, {.i64=FILTER_BRNG}, 0, 0, FLAGS, .unit = "out"},
    {"c",     "set highlight color", OFFSET(rgba_color), AV_OPT_TYPE_COLOR, {.str="yellow"}, .flags=FLAGS},
    {"color", "set highlight color", OFFSET(rgba_color), AV_OPT_TYPE_COLOR, {.str="yellow"}, .flags=FLAGS},
    {NULL}
};

AVFILTER_DEFINE_CLASS(signalstats);

static av_cold int init(AVFilterContext *ctx)
{
    uint8_t r, g, b;
    SignalstatsContext *s = ctx->priv;

    if (s->outfilter != FILTER_NONE)
        s->filters |= 1 << s->outfilter;

    r = s->rgba_color[0];
    g = s->rgba_color[1];
    b = s->rgba_color[2];
    s->yuv_color[0] = (( 66*r + 129*g +  25*b + (1<<7)) >> 8) +  16;
    s->yuv_color[1] = ((-38*r + -74*g + 112*b + (1<<7)) >> 8) + 128;
    s->yuv_color[2] = ((112*r + -94*g + -18*b + (1<<7)) >> 8) + 128;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SignalstatsContext *s = ctx->priv;
    av_frame_free(&s->frame_prev);
    av_frame_free(&s->frame_sat);
    av_frame_free(&s->frame_hue);
    av_freep(&s->jobs_rets);
    av_freep(&s->histy);
    av_freep(&s->histu);
    av_freep(&s->histv);
    av_freep(&s->histsat);
}

// TODO: add more
static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV420P16,
    AV_PIX_FMT_NONE
};

static AVFrame *alloc_frame(enum AVPixelFormat pixfmt, int w, int h)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return NULL;

    frame->format = pixfmt;
    frame->width  = w;
    frame->height = h;

    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return NULL;
    }

    return frame;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SignalstatsContext *s = ctx->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    s->depth = desc->comp[0].depth;
    s->maxsize = 1 << s->depth;
    s->histy = av_malloc_array(s->maxsize, sizeof(*s->histy));
    s->histu = av_malloc_array(s->maxsize, sizeof(*s->histu));
    s->histv = av_malloc_array(s->maxsize, sizeof(*s->histv));
    s->histsat = av_malloc_array(s->maxsize, sizeof(*s->histsat));

    if (!s->histy || !s->histu || !s->histv || !s->histsat)
        return AVERROR(ENOMEM);

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    s->chromaw = AV_CEIL_RSHIFT(inlink->w, s->hsub);
    s->chromah = AV_CEIL_RSHIFT(inlink->h, s->vsub);

    s->fs = inlink->w * inlink->h;
    s->cfs = s->chromaw * s->chromah;

    s->nb_jobs   = FFMAX(1, FFMIN(inlink->h, ff_filter_get_nb_threads(ctx)));
    s->jobs_rets = av_malloc_array(s->nb_jobs, sizeof(*s->jobs_rets));
    if (!s->jobs_rets)
        return AVERROR(ENOMEM);

    s->frame_sat = alloc_frame(s->depth > 8 ? AV_PIX_FMT_GRAY16 : AV_PIX_FMT_GRAY8,  inlink->w, inlink->h);
    s->frame_hue = alloc_frame(AV_PIX_FMT_GRAY16, inlink->w, inlink->h);
    if (!s->frame_sat || !s->frame_hue)
        return AVERROR(ENOMEM);

    return 0;
}

static void burn_frame8(const SignalstatsContext *s, AVFrame *f, int x, int y)
{
    const int chromax = x >> s->hsub;
    const int chromay = y >> s->vsub;
    f->data[0][y       * f->linesize[0] +       x] = s->yuv_color[0];
    f->data[1][chromay * f->linesize[1] + chromax] = s->yuv_color[1];
    f->data[2][chromay * f->linesize[2] + chromax] = s->yuv_color[2];
}

static void burn_frame16(const SignalstatsContext *s, AVFrame *f, int x, int y)
{
    const int chromax = x >> s->hsub;
    const int chromay = y >> s->vsub;
    const int mult = 1 << (s->depth - 8);
    AV_WN16(f->data[0] + y       * f->linesize[0] +       x * 2, s->yuv_color[0] * mult);
    AV_WN16(f->data[1] + chromay * f->linesize[1] + chromax * 2, s->yuv_color[1] * mult);
    AV_WN16(f->data[2] + chromay * f->linesize[2] + chromax * 2, s->yuv_color[2] * mult);
}

static int filter8_brng(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int w = in->width;
    const int h = in->height;
    const int slice_start = (h *  jobnr   ) / nb_jobs;
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;
    int x, y, score = 0;

    for (y = slice_start; y < slice_end; y++) {
        const int yc = y >> s->vsub;
        const uint8_t *pluma    = &in->data[0][y  * in->linesize[0]];
        const uint8_t *pchromau = &in->data[1][yc * in->linesize[1]];
        const uint8_t *pchromav = &in->data[2][yc * in->linesize[2]];

        for (x = 0; x < w; x++) {
            const int xc = x >> s->hsub;
            const int luma    = pluma[x];
            const int chromau = pchromau[xc];
            const int chromav = pchromav[xc];
            const int filt = luma    < 16 || luma    > 235 ||
                chromau < 16 || chromau > 240 ||
                chromav < 16 || chromav > 240;
            score += filt;
            if (out && filt)
                burn_frame8(s, out, x, y);
        }
    }
    return score;
}

static int filter16_brng(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int mult = 1 << (s->depth - 8);
    const int w = in->width;
    const int h = in->height;
    const int slice_start = (h *  jobnr   ) / nb_jobs;
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;
    int x, y, score = 0;

    for (y = slice_start; y < slice_end; y++) {
        const int yc = y >> s->vsub;
        const uint16_t *pluma    = (uint16_t *)&in->data[0][y  * in->linesize[0]];
        const uint16_t *pchromau = (uint16_t *)&in->data[1][yc * in->linesize[1]];
        const uint16_t *pchromav = (uint16_t *)&in->data[2][yc * in->linesize[2]];

        for (x = 0; x < w; x++) {
            const int xc = x >> s->hsub;
            const int luma    = pluma[x];
            const int chromau = pchromau[xc];
            const int chromav = pchromav[xc];
            const int filt = luma    < 16 * mult || luma    > 235 * mult ||
                chromau < 16 * mult || chromau > 240 * mult ||
                chromav < 16 * mult || chromav > 240 * mult;
            score += filt;
            if (out && filt)
                burn_frame16(s, out, x, y);
        }
    }
    return score;
}

static int filter_tout_outlier(uint8_t x, uint8_t y, uint8_t z)
{
    return ((abs(x - y) + abs (z - y)) / 2) - abs(z - x) > 4; // make 4 configurable?
}

static int filter8_tout(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int w = in->width;
    const int h = in->height;
    const int slice_start = (h *  jobnr   ) / nb_jobs;
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;
    const uint8_t *p = in->data[0];
    int lw = in->linesize[0];
    int x, y, score = 0, filt;

    for (y = slice_start; y < slice_end; y++) {

        if (y - 1 < 0 || y + 1 >= h)
            continue;

        // detect two pixels above and below (to eliminate interlace artefacts)
        // should check that video format is infact interlaced.

#define FILTER(i, j) \
        filter_tout_outlier(p[(y-j) * lw + x + i], \
                            p[    y * lw + x + i], \
                            p[(y+j) * lw + x + i])

#define FILTER3(j) (FILTER(-1, j) && FILTER(0, j) && FILTER(1, j))

        if (y - 2 >= 0 && y + 2 < h) {
            for (x = 1; x < w - 1; x++) {
                filt = FILTER3(2) && FILTER3(1);
                score += filt;
                if (filt && out)
                    burn_frame8(s, out, x, y);
            }
        } else {
            for (x = 1; x < w - 1; x++) {
                filt = FILTER3(1);
                score += filt;
                if (filt && out)
                    burn_frame8(s, out, x, y);
            }
        }
    }
    return score;
}

static int filter16_tout(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int w = in->width;
    const int h = in->height;
    const int slice_start = (h *  jobnr   ) / nb_jobs;
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;
    const uint16_t *p = (uint16_t *)in->data[0];
    int lw = in->linesize[0] / 2;
    int x, y, score = 0, filt;

    for (y = slice_start; y < slice_end; y++) {

        if (y - 1 < 0 || y + 1 >= h)
            continue;

        // detect two pixels above and below (to eliminate interlace artefacts)
        // should check that video format is infact interlaced.

        if (y - 2 >= 0 && y + 2 < h) {
            for (x = 1; x < w - 1; x++) {
                filt = FILTER3(2) && FILTER3(1);
                score += filt;
                if (filt && out)
                    burn_frame16(s, out, x, y);
            }
        } else {
            for (x = 1; x < w - 1; x++) {
                filt = FILTER3(1);
                score += filt;
                if (filt && out)
                    burn_frame16(s, out, x, y);
            }
        }
    }
    return score;
}

#define VREP_START 4

static int filter8_vrep(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int w = in->width;
    const int h = in->height;
    const int slice_start = (h *  jobnr   ) / nb_jobs;
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;
    const uint8_t *p = in->data[0];
    const int lw = in->linesize[0];
    int x, y, score = 0;

    for (y = slice_start; y < slice_end; y++) {
        const int y2lw = (y - VREP_START) * lw;
        const int ylw  =  y               * lw;
        int filt, totdiff = 0;

        if (y < VREP_START)
            continue;

        for (x = 0; x < w; x++)
            totdiff += abs(p[y2lw + x] - p[ylw + x]);
        filt = totdiff < w;

        score += filt;
        if (filt && out)
            for (x = 0; x < w; x++)
                burn_frame8(s, out, x, y);
    }
    return score * w;
}

static int filter16_vrep(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int w = in->width;
    const int h = in->height;
    const int slice_start = (h *  jobnr   ) / nb_jobs;
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;
    const uint16_t *p = (uint16_t *)in->data[0];
    const int lw = in->linesize[0] / 2;
    int x, y, score = 0;

    for (y = slice_start; y < slice_end; y++) {
        const int y2lw = (y - VREP_START) * lw;
        const int ylw  =  y               * lw;
        int64_t totdiff = 0;
        int filt;

        if (y < VREP_START)
            continue;

        for (x = 0; x < w; x++)
            totdiff += abs(p[y2lw + x] - p[ylw + x]);
        filt = totdiff < w;

        score += filt;
        if (filt && out)
            for (x = 0; x < w; x++)
                burn_frame16(s, out, x, y);
    }
    return score * w;
}

static const struct {
    const char *name;
    int (*process8)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
    int (*process16)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} filters_def[] = {
    {"TOUT", filter8_tout, filter16_tout},
    {"VREP", filter8_vrep, filter16_vrep},
    {"BRNG", filter8_brng, filter16_brng},
    {NULL}
};

static int compute_sat_hue_metrics8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    int i, j;
    ThreadDataHueSatMetrics *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *src = td->src;
    AVFrame *dst_sat = td->dst_sat;
    AVFrame *dst_hue = td->dst_hue;

    const int slice_start = (s->chromah *  jobnr   ) / nb_jobs;
    const int slice_end   = (s->chromah * (jobnr+1)) / nb_jobs;

    const int lsz_u = src->linesize[1];
    const int lsz_v = src->linesize[2];
    const uint8_t *p_u = src->data[1] + slice_start * lsz_u;
    const uint8_t *p_v = src->data[2] + slice_start * lsz_v;

    const int lsz_sat = dst_sat->linesize[0];
    const int lsz_hue = dst_hue->linesize[0];
    uint8_t *p_sat = dst_sat->data[0] + slice_start * lsz_sat;
    uint8_t *p_hue = dst_hue->data[0] + slice_start * lsz_hue;

    for (j = slice_start; j < slice_end; j++) {
        for (i = 0; i < s->chromaw; i++) {
            const int yuvu = p_u[i];
            const int yuvv = p_v[i];
            p_sat[i] = hypotf(yuvu - 128, yuvv - 128); // int or round?
            ((int16_t*)p_hue)[i] = fmodf(floorf((180.f / M_PI) * atan2f(yuvu-128, yuvv-128) + 180.f), 360.f);
        }
        p_u   += lsz_u;
        p_v   += lsz_v;
        p_sat += lsz_sat;
        p_hue += lsz_hue;
    }

    return 0;
}

static int compute_sat_hue_metrics16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    int i, j;
    ThreadDataHueSatMetrics *td = arg;
    const SignalstatsContext *s = ctx->priv;
    const AVFrame *src = td->src;
    AVFrame *dst_sat = td->dst_sat;
    AVFrame *dst_hue = td->dst_hue;
    const int mid = 1 << (s->depth - 1);

    const int slice_start = (s->chromah *  jobnr   ) / nb_jobs;
    const int slice_end   = (s->chromah * (jobnr+1)) / nb_jobs;

    const int lsz_u = src->linesize[1] / 2;
    const int lsz_v = src->linesize[2] / 2;
    const uint16_t *p_u = (uint16_t*)src->data[1] + slice_start * lsz_u;
    const uint16_t *p_v = (uint16_t*)src->data[2] + slice_start * lsz_v;

    const int lsz_sat = dst_sat->linesize[0] / 2;
    const int lsz_hue = dst_hue->linesize[0] / 2;
    uint16_t *p_sat = (uint16_t*)dst_sat->data[0] + slice_start * lsz_sat;
    uint16_t *p_hue = (uint16_t*)dst_hue->data[0] + slice_start * lsz_hue;

    for (j = slice_start; j < slice_end; j++) {
        for (i = 0; i < s->chromaw; i++) {
            const int yuvu = p_u[i];
            const int yuvv = p_v[i];
            p_sat[i] = hypotf(yuvu - mid, yuvv - mid); // int or round?
            ((int16_t*)p_hue)[i] = fmodf(floorf((180.f / M_PI) * atan2f(yuvu-mid, yuvv-mid) + 180.f), 360.f);
        }
        p_u   += lsz_u;
        p_v   += lsz_v;
        p_sat += lsz_sat;
        p_hue += lsz_hue;
    }

    return 0;
}

static unsigned compute_bit_depth(uint16_t mask)
{
    return av_popcount(mask);
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *ctx = link->dst;
    SignalstatsContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = in;
    int  w = 0,  cw = 0, // in
        pw = 0, cpw = 0; // prev
    int fil;
    char metabuf[128];
    unsigned int *histy = s->histy,
                 *histu = s->histu,
                 *histv = s->histv,
                 histhue[360] = {0},
                 *histsat = s->histsat;
    int miny  = -1, minu  = -1, minv  = -1;
    int maxy  = -1, maxu  = -1, maxv  = -1;
    int lowy  = -1, lowu  = -1, lowv  = -1;
    int highy = -1, highu = -1, highv = -1;
    int minsat = -1, maxsat = -1, lowsat = -1, highsat = -1;
    int lowp, highp, clowp, chighp;
    int accy, accu, accv;
    int accsat, acchue = 0;
    int medhue, maxhue;
    int64_t toty = 0, totu = 0, totv = 0, totsat=0;
    int64_t tothue = 0;
    int64_t dify = 0, difu = 0, difv = 0;
    uint16_t masky = 0, masku = 0, maskv = 0;

    int filtot[FILT_NUMB] = {0};
    AVFrame *prev;
    int ret;
    AVFrame *sat = s->frame_sat;
    AVFrame *hue = s->frame_hue;
    const int hbd = s->depth > 8;
    ThreadDataHueSatMetrics td_huesat = {
        .src     = in,
        .dst_sat = sat,
        .dst_hue = hue,
    };

    if (!s->frame_prev)
        s->frame_prev = av_frame_clone(in);

    prev = s->frame_prev;

    if (s->outfilter != FILTER_NONE) {
        out = av_frame_clone(in);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        ret = ff_inlink_make_frame_writable(link, &out);
        if (ret < 0) {
            av_frame_free(&out);
            av_frame_free(&in);
            return ret;
        }
    }

    ff_filter_execute(ctx, hbd ? compute_sat_hue_metrics16
                               : compute_sat_hue_metrics8, &td_huesat,
                      NULL, FFMIN(s->chromah, ff_filter_get_nb_threads(ctx)));

    memset(s->histy, 0, s->maxsize * sizeof(*s->histy));
    memset(s->histu, 0, s->maxsize * sizeof(*s->histu));
    memset(s->histv, 0, s->maxsize * sizeof(*s->histv));
    memset(s->histsat, 0, s->maxsize * sizeof(*s->histsat));

    if (hbd) {
        const uint16_t *p_sat = (uint16_t *)sat->data[0];
        const uint16_t *p_hue = (uint16_t *)hue->data[0];
        const int lsz_sat = sat->linesize[0] / 2;
        const int lsz_hue = hue->linesize[0] / 2;
        // Calculate luma histogram and difference with previous frame or field.
        for (int j = 0; j < link->h; j++) {
            for (int i = 0; i < link->w; i++) {
                const int yuv = AV_RN16(in->data[0] + w + i * 2);

                masky |= yuv;
                histy[yuv]++;
                dify += abs(yuv - (int)AV_RN16(prev->data[0] + pw + i * 2));
            }
            w  += in->linesize[0];
            pw += prev->linesize[0];
        }

        // Calculate chroma histogram and difference with previous frame or field.
        for (int j = 0; j < s->chromah; j++) {
            for (int i = 0; i < s->chromaw; i++) {
                const int yuvu = AV_RN16(in->data[1] + cw + i * 2);
                const int yuvv = AV_RN16(in->data[2] + cw + i * 2);

                masku |= yuvu;
                maskv |= yuvv;
                histu[yuvu]++;
                difu += abs(yuvu - (int)AV_RN16(prev->data[1] + cpw + i * 2));
                histv[yuvv]++;
                difv += abs(yuvv - (int)AV_RN16(prev->data[2] + cpw + i * 2));

                histsat[p_sat[i]]++;
                histhue[((int16_t*)p_hue)[i]]++;
            }
            cw  += in->linesize[1];
            cpw += prev->linesize[1];
            p_sat += lsz_sat;
            p_hue += lsz_hue;
        }
    } else {
        const uint8_t *p_sat = sat->data[0];
        const uint8_t *p_hue = hue->data[0];
        const int lsz_sat = sat->linesize[0];
        const int lsz_hue = hue->linesize[0];
        // Calculate luma histogram and difference with previous frame or field.
        for (int j = 0; j < link->h; j++) {
            for (int i = 0; i < link->w; i++) {
                const int yuv = in->data[0][w + i];

                masky |= yuv;
                histy[yuv]++;
                dify += abs(yuv - prev->data[0][pw + i]);
            }
            w  += in->linesize[0];
            pw += prev->linesize[0];
        }

        // Calculate chroma histogram and difference with previous frame or field.
        for (int j = 0; j < s->chromah; j++) {
            for (int i = 0; i < s->chromaw; i++) {
                const int yuvu = in->data[1][cw+i];
                const int yuvv = in->data[2][cw+i];

                masku |= yuvu;
                maskv |= yuvv;
                histu[yuvu]++;
                difu += abs(yuvu - prev->data[1][cpw+i]);
                histv[yuvv]++;
                difv += abs(yuvv - prev->data[2][cpw+i]);

                histsat[p_sat[i]]++;
                histhue[((int16_t*)p_hue)[i]]++;
            }
            cw  += in->linesize[1];
            cpw += prev->linesize[1];
            p_sat += lsz_sat;
            p_hue += lsz_hue;
        }
    }

    for (fil = 0; fil < FILT_NUMB; fil ++) {
        if (s->filters & 1<<fil) {
            ThreadData td = {
                .in = in,
                .out = out != in && s->outfilter == fil ? out : NULL,
            };
            memset(s->jobs_rets, 0, s->nb_jobs * sizeof(*s->jobs_rets));
            ff_filter_execute(ctx, hbd ? filters_def[fil].process16 : filters_def[fil].process8,
                              &td, s->jobs_rets, s->nb_jobs);
            for (int i = 0; i < s->nb_jobs; i++)
                filtot[fil] += s->jobs_rets[i];
        }
    }

    // find low / high based on histogram percentile
    // these only need to be calculated once.

    lowp   = lrint(s->fs  * 10 / 100.);
    highp  = lrint(s->fs  * 90 / 100.);
    clowp  = lrint(s->cfs * 10 / 100.);
    chighp = lrint(s->cfs * 90 / 100.);

    accy = accu = accv = accsat = 0;
    for (fil = 0; fil < s->maxsize; fil++) {
        if (miny   < 0 && histy[fil])   miny = fil;
        if (minu   < 0 && histu[fil])   minu = fil;
        if (minv   < 0 && histv[fil])   minv = fil;
        if (minsat < 0 && histsat[fil]) minsat = fil;

        if (histy[fil])   maxy   = fil;
        if (histu[fil])   maxu   = fil;
        if (histv[fil])   maxv   = fil;
        if (histsat[fil]) maxsat = fil;

        toty   += (uint64_t)histy[fil]   * fil;
        totu   += (uint64_t)histu[fil]   * fil;
        totv   += (uint64_t)histv[fil]   * fil;
        totsat += (uint64_t)histsat[fil] * fil;

        accy   += histy[fil];
        accu   += histu[fil];
        accv   += histv[fil];
        accsat += histsat[fil];

        if (lowy   == -1 && accy   >=  lowp) lowy   = fil;
        if (lowu   == -1 && accu   >= clowp) lowu   = fil;
        if (lowv   == -1 && accv   >= clowp) lowv   = fil;
        if (lowsat == -1 && accsat >= clowp) lowsat = fil;

        if (highy   == -1 && accy   >=  highp) highy   = fil;
        if (highu   == -1 && accu   >= chighp) highu   = fil;
        if (highv   == -1 && accv   >= chighp) highv   = fil;
        if (highsat == -1 && accsat >= chighp) highsat = fil;
    }

    maxhue = histhue[0];
    medhue = -1;
    for (fil = 0; fil < 360; fil++) {
        tothue += (uint64_t)histhue[fil] * fil;
        acchue += histhue[fil];

        if (medhue == -1 && acchue > s->cfs / 2)
            medhue = fil;
        if (histhue[fil] > maxhue) {
            maxhue = histhue[fil];
        }
    }

    av_frame_free(&s->frame_prev);
    s->frame_prev = av_frame_clone(in);

#define SET_META(key, fmt, val) do {                                \
    snprintf(metabuf, sizeof(metabuf), fmt, val);                   \
    av_dict_set(&out->metadata, "lavfi.signalstats." key, metabuf, 0);   \
} while (0)

    av_dict_set_int(&out->metadata, "lavfi.signalstats.YMIN", miny, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.YLOW", lowy, 0);
    SET_META("YAVG",    "%g", 1.0 * toty / s->fs);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.YHIGH", highy, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.YMAX", maxy, 0);

    av_dict_set_int(&out->metadata, "lavfi.signalstats.UMIN", minu, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.ULOW", lowu, 0);
    SET_META("UAVG",    "%g", 1.0 * totu / s->cfs);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.UHIGH", highu, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.UMAX", maxu, 0);

    av_dict_set_int(&out->metadata, "lavfi.signalstats.VMIN", minv, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.VLOW", lowv, 0);
    SET_META("VAVG",    "%g", 1.0 * totv / s->cfs);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.VHIGH", highv, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.VMAX", maxv, 0);

    av_dict_set_int(&out->metadata, "lavfi.signalstats.SATMIN", minsat, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.SATLOW", lowsat, 0);
    SET_META("SATAVG",  "%g", 1.0 * totsat / s->cfs);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.SATHIGH", highsat, 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.SATMAX", maxsat, 0);

    av_dict_set_int(&out->metadata, "lavfi.signalstats.HUEMED", medhue, 0);
    SET_META("HUEAVG",  "%g", 1.0 * tothue / s->cfs);

    SET_META("YDIF",    "%g", 1.0 * dify / s->fs);
    SET_META("UDIF",    "%g", 1.0 * difu / s->cfs);
    SET_META("VDIF",    "%g", 1.0 * difv / s->cfs);

    av_dict_set_int(&out->metadata, "lavfi.signalstats.YBITDEPTH", compute_bit_depth(masky), 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.UBITDEPTH", compute_bit_depth(masku), 0);
    av_dict_set_int(&out->metadata, "lavfi.signalstats.VBITDEPTH", compute_bit_depth(maskv), 0);

    for (fil = 0; fil < FILT_NUMB; fil ++) {
        if (s->filters & 1<<fil) {
            char metaname[128];
            snprintf(metabuf,  sizeof(metabuf),  "%g", 1.0 * filtot[fil] / s->fs);
            snprintf(metaname, sizeof(metaname), "lavfi.signalstats.%s", filters_def[fil].name);
            av_dict_set(&out->metadata, metaname, metabuf, 0);
        }
    }

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad signalstats_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
    },
};

static const AVFilterPad signalstats_outputs[] = {
    {
        .name           = "default",
        .config_props   = config_output,
        .type           = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_signalstats = {
    .name          = "signalstats",
    .description   = "Generate statistics from video analysis.",
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(SignalstatsContext),
    FILTER_INPUTS(signalstats_inputs),
    FILTER_OUTPUTS(signalstats_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &signalstats_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
