/*
 * Copyright (c) 2017 Paul B Mahol
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
 * Filter for reading closed captioning data (EIA-608).
 * See also https://en.wikipedia.org/wiki/EIA-608
 */

#include <string.h>

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define FALL 0
#define RISE 1

typedef struct ReadEIA608Context {
    const AVClass *class;
    int start, end;
    int min_range;
    int max_peak_diff;
    int max_period_diff;
    int max_start_diff;
    int nb_found;
    int white;
    int black;
    float mpd, mhd, msd, mac, spw, bhd, wth, bth;
    int chp;
} ReadEIA608Context;

#define OFFSET(x) offsetof(ReadEIA608Context, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption readeia608_options[] = {
    { "scan_min", "set from which line to scan for codes",                            OFFSET(start), AV_OPT_TYPE_INT,   {.i64=0},       0, INT_MAX, FLAGS },
    { "scan_max", "set to which line to scan for codes",                              OFFSET(end),   AV_OPT_TYPE_INT,   {.i64=29},      0, INT_MAX, FLAGS },
    { "mac",      "set minimal acceptable amplitude change for sync codes detection", OFFSET(mac),   AV_OPT_TYPE_FLOAT, {.dbl=.2},  0.001,       1, FLAGS },
    { "spw",      "set ratio of width reserved for sync code detection",              OFFSET(spw),   AV_OPT_TYPE_FLOAT, {.dbl=.27},   0.1,     0.7, FLAGS },
    { "mhd",      "set max peaks height difference for sync code detection",          OFFSET(mhd),   AV_OPT_TYPE_FLOAT, {.dbl=.1},      0,     0.5, FLAGS },
    { "mpd",      "set max peaks period difference for sync code detection",          OFFSET(mpd),   AV_OPT_TYPE_FLOAT, {.dbl=.1},      0,     0.5, FLAGS },
    { "msd",      "set first two max start code bits differences",                    OFFSET(msd),   AV_OPT_TYPE_FLOAT, {.dbl=.02},     0,     0.5, FLAGS },
    { "bhd",      "set min ratio of bits height compared to 3rd start code bit",      OFFSET(bhd),   AV_OPT_TYPE_FLOAT, {.dbl=.75},  0.01,       1, FLAGS },
    { "th_w",     "set white color threshold",                                        OFFSET(wth),   AV_OPT_TYPE_FLOAT, {.dbl=.35},   0.1,       1, FLAGS },
    { "th_b",     "set black color threshold",                                        OFFSET(bth),   AV_OPT_TYPE_FLOAT, {.dbl=.15},     0,     0.5, FLAGS },
    { "chp",      "check and apply parity bit",                                       OFFSET(chp),   AV_OPT_TYPE_BOOL,  {.i64= 0},      0,       1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(readeia608);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    ReadEIA608Context *s = ctx->priv;
    int depth = desc->comp[0].depth;

    if (s->end >= inlink->h) {
        av_log(ctx, AV_LOG_WARNING, "Last line to scan too large, clipping.\n");
        s->end = inlink->h - 1;
    }

    if (s->start > s->end) {
        av_log(ctx, AV_LOG_ERROR, "Invalid range.\n");
        return AVERROR(EINVAL);
    }

    s->min_range = s->mac * ((1 << depth) - 1);
    s->max_peak_diff = s->mhd * ((1 << depth) - 1);
    s->max_period_diff = s->mpd * ((1 << depth) - 1);
    s->max_start_diff = s->msd * ((1 << depth) - 1);
    s->white = s->wth * ((1 << depth) - 1);
    s->black = s->bth * ((1 << depth) - 1);

    return 0;
}

static void extract_line(AVFilterContext *ctx, AVFilterLink *inlink, AVFrame *in, int line)
{
    ReadEIA608Context *s = ctx->priv;
    int max = 0, min = INT_MAX;
    int i, ch, range = 0;
    const uint8_t *src;
    uint16_t clock[8][2] = { { 0 } };
    const int sync_width = s->spw * in->width;
    int last = 0, peaks = 0, max_peak_diff = 0, dir = RISE;
    const int width_per_bit = (in->width - sync_width) / 19;
    uint8_t byte[2] = { 0 };
    int s1, s2, s3, parity;

    src = &in->data[0][line * in->linesize[0]];
    for (i = 0; i < sync_width; i++) {
        max = FFMAX(max, src[i]);
        min = FFMIN(min, src[i]);
    }

    range = max - min;
    if (range < s->min_range)
        return;

    for (i = 0; i < sync_width; i++) {
        int Y = src[i];

        if (dir == RISE) {
            if (Y < last) {
                dir = FALL;
                if (last >= s->white) {
                    clock[peaks][0] = last;
                    clock[peaks][1] = i;
                    peaks++;
                    if (peaks > 7)
                        break;
                }
            }
        } else if (dir == FALL) {
            if (Y > last && last <= s->black) {
                dir = RISE;
            }
        }
        last = Y;
    }

    if (peaks != 7)
        return;

    for (i = 1; i < 7; i++)
        max_peak_diff = FFMAX(max_peak_diff, FFABS(clock[i][0] - clock[i-1][0]));

    if (max_peak_diff > s->max_peak_diff)
        return;

    max = 0; min = INT_MAX;
    for (i = 1; i < 7; i++) {
        max = FFMAX(max, FFABS(clock[i][1] - clock[i-1][1]));
        min = FFMIN(min, FFABS(clock[i][1] - clock[i-1][1]));
    }

    range = max - min;
    if (range > s->max_period_diff)
        return;

    s1 = src[sync_width + width_per_bit * 0 + width_per_bit / 2];
    s2 = src[sync_width + width_per_bit * 1 + width_per_bit / 2];
    s3 = src[sync_width + width_per_bit * 2 + width_per_bit / 2];

    if (FFABS(s1 - s2) > s->max_start_diff || s1 > s->black || s2 > s->black || s3 < s->white)
        return;

    for (ch = 0; ch < 2; ch++) {
        for (parity = 0, i = 0; i < 8; i++) {
            int b = src[sync_width + width_per_bit * (i + 3 + 8 * ch) + width_per_bit / 2];

            if (b - s1 > (s3 - s1) * s->bhd) {
                b = 1;
                parity++;
            } else {
                b = 0;
            }
            byte[ch] |= b << i;
        }

        if (s->chp) {
            if (!(parity & 1)) {
                byte[ch] = 0;
            }
        }
    }

    {
        uint8_t key[128], value[128];

        snprintf(key, sizeof(key), "lavfi.readeia608.%d.cc", s->nb_found);
        snprintf(value, sizeof(value), "0x%02X%02X", byte[0], byte[1]);
        av_dict_set(avpriv_frame_get_metadatap(in), key, value, 0);

        snprintf(key, sizeof(key), "lavfi.readeia608.%d.line", s->nb_found);
        snprintf(value, sizeof(value), "%d", line);
        av_dict_set(avpriv_frame_get_metadatap(in), key, value, 0);
    }

    s->nb_found++;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ReadEIA608Context *s = ctx->priv;
    int i;

    s->nb_found = 0;
    for (i = s->start; i <= s->end; i++)
        extract_line(ctx, inlink, in, i);

    return ff_filter_frame(outlink, in);
}

static const AVFilterPad readeia608_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad readeia608_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_readeia608 = {
    .name          = "readeia608",
    .description   = NULL_IF_CONFIG_SMALL("Read EIA-608 Closed Caption codes from input video and write them to frame metadata."),
    .priv_size     = sizeof(ReadEIA608Context),
    .priv_class    = &readeia608_class,
    .query_formats = query_formats,
    .inputs        = readeia608_inputs,
    .outputs       = readeia608_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
