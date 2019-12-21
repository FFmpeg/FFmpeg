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

#define LAG 25
#define SYNC_MIN 12.f
#define SYNC_MAX 15.f

typedef struct CodeItem {
    uint8_t bit;
    int size;
} CodeItem;

typedef struct ReadEIA608Context {
    const AVClass *class;
    int start, end;
    int nb_found;
    int white;
    int black;
    float spw;
    int chp;
    int lp;

    uint64_t histogram[256];

    uint8_t *temp;
    uint8_t *signal;
    CodeItem *code;
    float *unfiltered;
    float *filtered;
    float *avg_filter;
    float *std_filter;
} ReadEIA608Context;

#define OFFSET(x) offsetof(ReadEIA608Context, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption readeia608_options[] = {
    { "scan_min", "set from which line to scan for codes",               OFFSET(start), AV_OPT_TYPE_INT,   {.i64=0},     0, INT_MAX, FLAGS },
    { "scan_max", "set to which line to scan for codes",                 OFFSET(end),   AV_OPT_TYPE_INT,   {.i64=29},    0, INT_MAX, FLAGS },
    { "spw",      "set ratio of width reserved for sync code detection", OFFSET(spw),   AV_OPT_TYPE_FLOAT, {.dbl=.27}, 0.1,     0.7, FLAGS },
    { "chp",      "check and apply parity bit",                          OFFSET(chp),   AV_OPT_TYPE_BOOL,  {.i64= 0},    0,       1, FLAGS },
    { "lp",       "lowpass line prior to processing",                    OFFSET(lp),    AV_OPT_TYPE_BOOL,  {.i64= 1},    0,       1, FLAGS },
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
    AVFilterContext *ctx = inlink->dst;
    ReadEIA608Context *s = ctx->priv;
    int size = inlink->w + LAG;

    if (s->end >= inlink->h) {
        av_log(ctx, AV_LOG_WARNING, "Last line to scan too large, clipping.\n");
        s->end = inlink->h - 1;
    }

    if (s->start > s->end) {
        av_log(ctx, AV_LOG_ERROR, "Invalid range.\n");
        return AVERROR(EINVAL);
    }

    s->unfiltered = av_calloc(size, sizeof(*s->unfiltered));
    s->filtered = av_calloc(size, sizeof(*s->filtered));
    s->avg_filter = av_calloc(size, sizeof(*s->avg_filter));
    s->std_filter = av_calloc(size, sizeof(*s->std_filter));
    s->signal = av_calloc(size, sizeof(*s->signal));
    s->code = av_calloc(size, sizeof(*s->code));
    s->temp = av_calloc(size, sizeof(*s->temp));
    if (!s->temp)
        return AVERROR(ENOMEM);

    return 0;
}

static void build_histogram(ReadEIA608Context *s, const uint8_t *src, int len)
{
    memset(s->histogram, 0, sizeof(s->histogram));

    for (int i = 0; i < len; i++)
        s->histogram[src[i]]++;
}

static void find_black_and_white(ReadEIA608Context *s)
{
    int start = 0, end = 0, middle;
    int black = 0, white = 0;
    int cnt;

    for (int i = 0; i < 256; i++) {
        if (s->histogram[i]) {
            start = i;
            break;
        }
    }

    for (int i = 255; i >= 0; i--) {
        if (s->histogram[i]) {
            end = i;
            break;
        }
    }

    middle = start + (end - start) / 2;

    cnt = 0;
    for (int i = start; i <= middle; i++) {
        if (s->histogram[i] > cnt) {
            cnt = s->histogram[i];
            black = i;
        }
    }

    cnt = 0;
    for (int i = end; i >= middle; i--) {
        if (s->histogram[i] > cnt) {
            cnt = s->histogram[i];
            white = i;
        }
    }

    s->black = black;
    s->white = white;
}

static float meanf(float *data, int len)
{
    float sum = 0.0, mean = 0.0;

    for (int i = 0; i < len; i++)
        sum += data[i];

    mean = sum / len;

    return mean;
}

static float stddevf(float *data, int len)
{
    float m = meanf(data, len);
    float standard_deviation = 0.f;

    for (int i = 0; i < len; i++)
        standard_deviation += (data[i] - m) * (data[i] - m);

    return sqrtf(standard_deviation / (len - 1));
}

static void thresholding(ReadEIA608Context *s, const uint8_t *y, uint8_t *signal,
                         float *unfiltered, float *filtered, float *avg_filter, float *std_filter,
                         int lag, float threshold, float influence, int len)
{
    for (int i = lag; i < len + lag; i++) {
        unfiltered[i] = y[i - lag] / 255.f;
        filtered[i] = unfiltered[i];
    }

    for (int i = 0; i < lag; i++) {
        unfiltered[i] = meanf(unfiltered, len * s->spw);
        filtered[i] = unfiltered[i];
    }

    memset(signal, 0, len);

    avg_filter[lag - 1] = meanf(unfiltered, lag);
    std_filter[lag - 1] = stddevf(unfiltered, lag);

    for (int i = lag; i < len + lag; i++) {
        if (fabsf(unfiltered[i] - avg_filter[i-1]) > threshold * std_filter[i-1]) {
            if (unfiltered[i] > avg_filter[i-1]) {
                signal[i - lag] = 255;
            } else {
                signal[i - lag] = 0;
            }
            filtered[i] = influence * unfiltered[i] + (1.f - influence) * filtered[i-1];
        } else {
            int distance_from_black, distance_from_white;

            distance_from_black = FFABS(y[i - lag] - s->black);
            distance_from_white = FFABS(y[i - lag] - s->white);

            signal[i - lag] = distance_from_black <= distance_from_white ? 0 : 255;
        }
        avg_filter[i] = meanf(filtered + i - lag, lag);
        std_filter[i] = stddevf(filtered + i - lag, lag);
    }
}

static int periods(const uint8_t *signal, CodeItem *code, int len)
{
    int hold = signal[0], cnt = 0;
    int last = 0;

    memset(code, 0, len * sizeof(*code));

    for (int i = 1; i < len; i++) {
        if (signal[i] != hold) {
            code[cnt].size = i - last;
            code[cnt].bit = hold;
            hold = signal[i];
            last = i;
            cnt++;
        }
    }

    code[cnt].size = len - last;
    code[cnt].bit = hold;

    return cnt + 1;
}

static void dump_code(AVFilterContext *ctx, int len, int item)
{
    ReadEIA608Context *s = ctx->priv;

    av_log(ctx, AV_LOG_DEBUG, "%d:", item);
    for (int i = 0; i < len; i++) {
        av_log(ctx, AV_LOG_DEBUG, " %03d", s->code[i].size);
    }
    av_log(ctx, AV_LOG_DEBUG, "\n");
}

static void extract_line(AVFilterContext *ctx, AVFilterLink *inlink, AVFrame *in, int line)
{
    ReadEIA608Context *s = ctx->priv;
    int i, j, ch, len;
    const uint8_t *src;
    uint8_t byte[2] = { 0 };
    uint8_t codes[19] = { 0 };
    float bit_size = 0.f;
    int parity;

    src = &in->data[0][line * in->linesize[0]];
    if (s->lp) {
        uint8_t *dst = s->temp;
        int w = inlink->w - 1;

        for (i = 0; i < inlink->w; i++) {
            int a = FFMAX(i - 3, 0);
            int b = FFMAX(i - 2, 0);
            int c = FFMAX(i - 1, 0);
            int d = FFMIN(i + 3, w);
            int e = FFMIN(i + 2, w);
            int f = FFMIN(i + 1, w);

            dst[i] = (src[a] + src[b] + src[c] + src[i] + src[d] + src[e] + src[f] + 6) / 7;
        }

        src = s->temp;
    }

    build_histogram(s, src, inlink->w);
    find_black_and_white(s);
    if (s->white - s->black < 5)
        return;

    thresholding(s, src, s->signal, s->unfiltered, s->filtered,
                 s->avg_filter, s->std_filter,
                 LAG, 1, 0, inlink->w);
    //memcpy(&in->data[0][line * in->linesize[0]], s->signal, inlink->w);
    len = periods(s->signal, s->code, inlink->w);
    dump_code(ctx, len, line);
    if (len < 15 ||
        s->code[14].bit != 0 ||
        inlink->w / (float)s->code[14].size < SYNC_MIN ||
        inlink->w / (float)s->code[14].size > SYNC_MAX) {
        return;
    }

    for (i = 14; i < len; i++) {
        bit_size += s->code[i].size;
    }

    bit_size /= 19.f;
    for (i = 1; i < 14; i++) {
        if (s->code[i].size > bit_size * 1.5f) {
            return;
        }
    }

    if (s->code[15].size / bit_size < 0.45f) {
        return;
    }

    for (j = 0, i = 14; i < len; i++) {
        int run, bit;

        run = lrintf(s->code[i].size / bit_size);
        bit = s->code[i].bit;

        for (int k = 0; j < 19 && k < run; k++) {
            codes[j++] = bit;
        }

        if (j >= 19)
            break;
    }

    for (ch = 0; ch < 2; ch++) {
        for (parity = 0, i = 0; i < 8; i++) {
            int b = codes[3 + ch * 8 + i];

            if (b == 255) {
                parity++;
                b = 1;
            } else {
                b = 0;
            }
            byte[ch] |= b << i;
        }

        if (s->chp) {
            if (!(parity & 1)) {
                byte[ch] = 0x7F;
            }
        }
    }

    {
        uint8_t key[128], value[128];

        //snprintf(key, sizeof(key), "lavfi.readeia608.%d.bits", s->nb_found);
        //snprintf(value, sizeof(value), "0b%d%d%d%d%d%d%d%d 0b%d%d%d%d%d%d%d%d", codes[3]==255,codes[4]==255,codes[5]==255,codes[6]==255,codes[7]==255,codes[8]==255,codes[9]==255,codes[10]==255,codes[11]==255,codes[12]==255,codes[13]==255,codes[14]==255,codes[15]==255,codes[16]==255,codes[17]==255,codes[18]==255);
        //av_dict_set(&in->metadata, key, value, 0);

        snprintf(key, sizeof(key), "lavfi.readeia608.%d.cc", s->nb_found);
        snprintf(value, sizeof(value), "0x%02X%02X", byte[0], byte[1]);
        av_dict_set(&in->metadata, key, value, 0);

        snprintf(key, sizeof(key), "lavfi.readeia608.%d.line", s->nb_found);
        snprintf(value, sizeof(value), "%d", line);
        av_dict_set(&in->metadata, key, value, 0);
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

static av_cold void uninit(AVFilterContext *ctx)
{
    ReadEIA608Context *s = ctx->priv;

    av_freep(&s->temp);
    av_freep(&s->code);
    av_freep(&s->signal);
    av_freep(&s->unfiltered);
    av_freep(&s->filtered);
    av_freep(&s->avg_filter);
    av_freep(&s->std_filter);
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
    .uninit        = uninit,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
