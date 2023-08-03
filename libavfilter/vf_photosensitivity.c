/*
 * Copyright (c) 2019 Vladimir Panteleev
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

#include <float.h>

#include "libavutil/opt.h"
#include "avfilter.h"

#include "filters.h"
#include "internal.h"
#include "video.h"

#define MAX_FRAMES 240
#define GRID_SIZE 8
#define NUM_CHANNELS 3

typedef struct PhotosensitivityFrame {
    uint8_t grid[GRID_SIZE][GRID_SIZE][4];
} PhotosensitivityFrame;

typedef struct PhotosensitivityContext {
    const AVClass *class;

    int nb_frames;
    int skip;
    float threshold_multiplier;
    int bypass;

    int badness_threshold;

    /* Circular buffer */
    int history[MAX_FRAMES];
    int history_pos;

    PhotosensitivityFrame last_frame_e;
    AVFrame *last_frame_av;
} PhotosensitivityContext;

#define OFFSET(x) offsetof(PhotosensitivityContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption photosensitivity_options[] = {
    { "frames",    "set how many frames to use",                          OFFSET(nb_frames),            AV_OPT_TYPE_INT,   {.i64=30}, 2, MAX_FRAMES, FLAGS },
    { "f",         "set how many frames to use",                          OFFSET(nb_frames),            AV_OPT_TYPE_INT,   {.i64=30}, 2, MAX_FRAMES, FLAGS },
    { "threshold", "set detection threshold factor (lower is stricter)",  OFFSET(threshold_multiplier), AV_OPT_TYPE_FLOAT, {.dbl=1},  0.1, FLT_MAX,  FLAGS },
    { "t",         "set detection threshold factor (lower is stricter)",  OFFSET(threshold_multiplier), AV_OPT_TYPE_FLOAT, {.dbl=1},  0.1, FLT_MAX,  FLAGS },
    { "skip",      "set pixels to skip when sampling frames",             OFFSET(skip),                 AV_OPT_TYPE_INT,   {.i64=1},  1, 1024,       FLAGS },
    { "bypass",    "leave frames unchanged",                              OFFSET(bypass),               AV_OPT_TYPE_BOOL,  {.i64=0},  0, 1,          FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(photosensitivity);

typedef struct ThreadData_convert_frame
{
    AVFrame *in;
    PhotosensitivityFrame *out;
    int skip;
} ThreadData_convert_frame;

#define NUM_CELLS (GRID_SIZE * GRID_SIZE)

static int convert_frame_partial(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    int cell, gx, gy, x0, x1, y0, y1, x, y, c, area;
    int sum[NUM_CHANNELS];
    const uint8_t *p;

    ThreadData_convert_frame *td = arg;

    const int slice_start = (NUM_CELLS * jobnr) / nb_jobs;
    const int slice_end = (NUM_CELLS * (jobnr+1)) / nb_jobs;

    int width = td->in->width, height = td->in->height, linesize = td->in->linesize[0], skip = td->skip;
    const uint8_t *data = td->in->data[0];

    for (cell = slice_start; cell < slice_end; cell++) {
        gx = cell % GRID_SIZE;
        gy = cell / GRID_SIZE;

        x0 = width  *  gx    / GRID_SIZE;
        x1 = width  * (gx+1) / GRID_SIZE;
        y0 = height *  gy    / GRID_SIZE;
        y1 = height * (gy+1) / GRID_SIZE;

        for (c = 0; c < NUM_CHANNELS; c++) {
            sum[c] = 0;
        }
        for (y = y0; y < y1; y += skip) {
            p = data + y * linesize + x0 * NUM_CHANNELS;
            for (x = x0; x < x1; x += skip) {
                //av_log(NULL, AV_LOG_VERBOSE, "%d %d %d : (%d,%d) (%d,%d) -> %d,%d | *%d\n", c, gx, gy, x0, y0, x1, y1, x, y, (int)row);
                sum[0] += p[0];
                sum[1] += p[1];
                sum[2] += p[2];
                p += NUM_CHANNELS * skip;
                // TODO: variable size
            }
        }

        area = ((x1 - x0 + skip - 1) / skip) * ((y1 - y0 + skip - 1) / skip);
        for (c = 0; c < NUM_CHANNELS; c++) {
            if (area)
                sum[c] /= area;
            td->out->grid[gy][gx][c] = sum[c];
        }
    }
    return 0;
}

static void convert_frame(AVFilterContext *ctx, AVFrame *in, PhotosensitivityFrame *out, int skip)
{
    ThreadData_convert_frame td;
    td.in = in;
    td.out = out;
    td.skip = skip;
    ff_filter_execute(ctx, convert_frame_partial, &td, NULL,
                      FFMIN(NUM_CELLS, ff_filter_get_nb_threads(ctx)));
}

typedef struct ThreadData_blend_frame
{
    AVFrame *target;
    AVFrame *source;
    uint16_t s_mul;
} ThreadData_blend_frame;

static int blend_frame_partial(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    int x, y;
    uint8_t *t, *s;

    ThreadData_blend_frame *td = arg;
    const uint16_t s_mul = td->s_mul;
    const uint16_t t_mul = 0x100 - s_mul;
    const int slice_start = (td->target->height * jobnr) / nb_jobs;
    const int slice_end = (td->target->height * (jobnr+1)) / nb_jobs;
    const int linesize = td->target->linesize[0];

    for (y = slice_start; y < slice_end; y++) {
        t = td->target->data[0] + y * td->target->linesize[0];
        s = td->source->data[0] + y * td->source->linesize[0];
        for (x = 0; x < linesize; x++) {
            *t = (*t * t_mul + *s * s_mul) >> 8;
            t++; s++;
        }
    }
    return 0;
}

static void blend_frame(AVFilterContext *ctx, AVFrame *target, AVFrame *source, float factor)
{
    ThreadData_blend_frame td;
    td.target = target;
    td.source = source;
    td.s_mul = (uint16_t)(factor * 0x100);
    ff_filter_execute(ctx, blend_frame_partial, &td, NULL,
                      FFMIN(ctx->outputs[0]->h, ff_filter_get_nb_threads(ctx)));
}

static int get_badness(PhotosensitivityFrame *a, PhotosensitivityFrame *b)
{
    int badness, x, y, c;
    badness = 0;
    for (c = 0; c < NUM_CHANNELS; c++) {
        for (y = 0; y < GRID_SIZE; y++) {
            for (x = 0; x < GRID_SIZE; x++) {
                badness += abs((int)a->grid[y][x][c] - (int)b->grid[y][x][c]);
                //av_log(NULL, AV_LOG_VERBOSE, "%d - %d -> %d \n", a->grid[y][x], b->grid[y][x], badness);
                //av_log(NULL, AV_LOG_VERBOSE, "%d -> %d \n", abs((int)a->grid[y][x] - (int)b->grid[y][x]), badness);
            }
        }
    }
    return badness;
}

static int config_input(AVFilterLink *inlink)
{
    /* const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format); */
    AVFilterContext *ctx = inlink->dst;
    PhotosensitivityContext *s = ctx->priv;

    s->badness_threshold = (int)(GRID_SIZE * GRID_SIZE * 4 * 256 * s->nb_frames * s->threshold_multiplier / 128);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int this_badness, current_badness, fixed_badness, new_badness, i, res;
    PhotosensitivityFrame ef;
    AVFrame *src, *out;
    int free_in = 0;
    float factor;
    AVDictionary **metadata;

    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    PhotosensitivityContext *s = ctx->priv;

    /* weighted moving average */
    current_badness = 0;
    for (i = 1; i < s->nb_frames; i++)
        current_badness += i * s->history[(s->history_pos + i) % s->nb_frames];
    current_badness /= s->nb_frames;

    convert_frame(ctx, in, &ef, s->skip);
    this_badness = get_badness(&ef, &s->last_frame_e);
    new_badness = current_badness + this_badness;
    av_log(s, AV_LOG_VERBOSE, "badness: %6d -> %6d / %6d (%3d%% - %s)\n",
        current_badness, new_badness, s->badness_threshold,
        100 * new_badness / s->badness_threshold, new_badness < s->badness_threshold ? "OK" : "EXCEEDED");

    fixed_badness = new_badness;
    if (new_badness < s->badness_threshold || !s->last_frame_av || s->bypass) {
        factor = 1; /* for metadata */
        av_frame_free(&s->last_frame_av);
        s->last_frame_av = src = in;
        s->last_frame_e = ef;
        s->history[s->history_pos] = this_badness;
    } else {
        factor = (float)(s->badness_threshold - current_badness) / (new_badness - current_badness);
        if (factor <= 0) {
            /* just duplicate the frame */
            s->history[s->history_pos] = 0; /* frame was duplicated, thus, delta is zero */
        } else {
            res = ff_inlink_make_frame_writable(inlink, &s->last_frame_av);
            if (res) {
                av_frame_free(&in);
                return res;
            }
            blend_frame(ctx, s->last_frame_av, in, factor);

            convert_frame(ctx, s->last_frame_av, &ef, s->skip);
            this_badness = get_badness(&ef, &s->last_frame_e);
            fixed_badness = current_badness + this_badness;
            av_log(s, AV_LOG_VERBOSE, "  fixed: %6d -> %6d / %6d (%3d%%) factor=%5.3f\n",
                current_badness, fixed_badness, s->badness_threshold,
                100 * new_badness / s->badness_threshold, factor);
            s->last_frame_e = ef;
            s->history[s->history_pos] = this_badness;
        }
        src = s->last_frame_av;
        free_in = 1;
    }
    s->history_pos = (s->history_pos + 1) % s->nb_frames;

    out = ff_get_video_buffer(outlink, in->width, in->height);
    if (!out) {
        if (free_in == 1)
            av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    metadata = &out->metadata;
    if (metadata) {
        char value[128];

        snprintf(value, sizeof(value), "%f", (float)new_badness / s->badness_threshold);
        av_dict_set(metadata, "lavfi.photosensitivity.badness", value, 0);

        snprintf(value, sizeof(value), "%f", (float)fixed_badness / s->badness_threshold);
        av_dict_set(metadata, "lavfi.photosensitivity.fixed-badness", value, 0);

        snprintf(value, sizeof(value), "%f", (float)this_badness / s->badness_threshold);
        av_dict_set(metadata, "lavfi.photosensitivity.frame-badness", value, 0);

        snprintf(value, sizeof(value), "%f", factor);
        av_dict_set(metadata, "lavfi.photosensitivity.factor", value, 0);
    }
    av_frame_copy(out, src);
    if (free_in == 1)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PhotosensitivityContext *s = ctx->priv;

    av_frame_free(&s->last_frame_av);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_photosensitivity = {
    .name          = "photosensitivity",
    .description   = NULL_IF_CONFIG_SMALL("Filter out photosensitive epilepsy seizure-inducing flashes."),
    .priv_size     = sizeof(PhotosensitivityContext),
    .priv_class    = &photosensitivity_class,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS(AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24),
};
