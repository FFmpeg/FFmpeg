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

#include "avfilter.h"
#include "internal.h"
#include "video.h"

#define LAG 25
#define CLOCK_BITSIZE_MIN 0.2f
#define CLOCK_BITSIZE_MAX 1.5f
#define SYNC_BITSIZE_MIN 12.f
#define SYNC_BITSIZE_MAX 15.f

typedef struct LineItem {
    int   input;
    int   output;

    float unfiltered;
    float filtered;
    float average;
    float deviation;
} LineItem;

typedef struct CodeItem {
    uint8_t bit;
    int size;
} CodeItem;

typedef struct ScanItem {
    int nb_line;
    int found;
    int white;
    int black;
    uint64_t *histogram;
    uint8_t byte[2];

    CodeItem *code;
    LineItem *line;
} ScanItem;

typedef struct ReadEIA608Context {
    const AVClass *class;

    int start, end;
    float spw;
    int chp;
    int lp;

    int depth;
    int max;
    int nb_allocated;
    ScanItem *scan;

    void (*read_line[2])(AVFrame *in, int nb_line,
                         LineItem *line, int lp, int w);
} ReadEIA608Context;

#define OFFSET(x) offsetof(ReadEIA608Context, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption readeia608_options[] = {
    { "scan_min", "set from which line to scan for codes",               OFFSET(start), AV_OPT_TYPE_INT,   {.i64=0},     0, INT_MAX, FLAGS },
    { "scan_max", "set to which line to scan for codes",                 OFFSET(end),   AV_OPT_TYPE_INT,   {.i64=29},    0, INT_MAX, FLAGS },
    { "spw",      "set ratio of width reserved for sync code detection", OFFSET(spw),   AV_OPT_TYPE_FLOAT, {.dbl=.27}, 0.1,     0.7, FLAGS },
    { "chp",      "check and apply parity bit",                          OFFSET(chp),   AV_OPT_TYPE_BOOL,  {.i64= 0},    0,       1, FLAGS },
    { "lp",       "lowpass line prior to processing",                    OFFSET(lp),    AV_OPT_TYPE_BOOL,  {.i64= 1},    0,       1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(readeia608);

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14,
    AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_NONE
};

static int config_filter(AVFilterContext *ctx, int start, int end)
{
    ReadEIA608Context *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int size = inlink->w + LAG;

    if (end >= inlink->h) {
        av_log(ctx, AV_LOG_WARNING, "Last line to scan too large, clipping.\n");
        end = inlink->h - 1;
    }

    if (start > end) {
        av_log(ctx, AV_LOG_ERROR, "Invalid range.\n");
        return AVERROR(EINVAL);
    }

    if (s->nb_allocated < end - start + 1) {
        const int diff = end - start + 1 - s->nb_allocated;

        s->scan = av_realloc_f(s->scan, end - start + 1, sizeof(*s->scan));
        if (!s->scan)
            return AVERROR(ENOMEM);
        memset(&s->scan[s->nb_allocated], 0, diff * sizeof(*s->scan));
        s->nb_allocated = end - start + 1;
    }

    for (int i = 0; i < s->nb_allocated; i++) {
        ScanItem *scan = &s->scan[i];

        if (!scan->histogram)
            scan->histogram = av_calloc(s->max + 1, sizeof(*scan->histogram));
        if (!scan->line)
            scan->line = av_calloc(size, sizeof(*scan->line));
        if (!scan->code)
            scan->code = av_calloc(size, sizeof(*scan->code));
        if (!scan->line || !scan->code || !scan->histogram)
            return AVERROR(ENOMEM);
    }

    s->start = start;
    s->end = end;

    return 0;
}

static void build_histogram(ReadEIA608Context *s, ScanItem *scan, const LineItem *line, int len)
{
    memset(scan->histogram, 0, (s->max + 1) * sizeof(*scan->histogram));

    for (int i = LAG; i < len + LAG; i++)
        scan->histogram[line[i].input]++;
}

static void find_black_and_white(ReadEIA608Context *s, ScanItem *scan)
{
    const int max = s->max;
    int start = 0, end = 0, middle;
    int black = 0, white = 0;
    int cnt;

    for (int i = 0; i <= max; i++) {
        if (scan->histogram[i]) {
            start = i;
            break;
        }
    }

    for (int i = max; i >= 0; i--) {
        if (scan->histogram[i]) {
            end = i;
            break;
        }
    }

    middle = start + (end - start) / 2;

    cnt = 0;
    for (int i = start; i <= middle; i++) {
        if (scan->histogram[i] > cnt) {
            cnt = scan->histogram[i];
            black = i;
        }
    }

    cnt = 0;
    for (int i = end; i >= middle; i--) {
        if (scan->histogram[i] > cnt) {
            cnt = scan->histogram[i];
            white = i;
        }
    }

    scan->black = black;
    scan->white = white;
}

static float meanf(const LineItem *line, int len)
{
    float sum = 0.0, mean = 0.0;

    for (int i = 0; i < len; i++)
        sum += line[i].filtered;

    mean = sum / len;

    return mean;
}

static float stddevf(const LineItem *line, int len)
{
    float m = meanf(line, len);
    float standard_deviation = 0.f;

    for (int i = 0; i < len; i++)
        standard_deviation += (line[i].filtered - m) * (line[i].filtered - m);

    return sqrtf(standard_deviation / (len - 1));
}

static void thresholding(ReadEIA608Context *s, ScanItem *scan, LineItem *line,
                         int lag, float threshold, float influence, int len)
{
    for (int i = lag; i < len + lag; i++) {
        line[i].unfiltered = line[i].input / 255.f;
        line[i].filtered = line[i].unfiltered;
    }

    for (int i = 0; i < lag; i++) {
        line[i].unfiltered = meanf(line, len * s->spw);
        line[i].filtered = line[i].unfiltered;
    }

    line[lag - 1].average   = meanf(line, lag);
    line[lag - 1].deviation = stddevf(line, lag);

    for (int i = lag; i < len + lag; i++) {
        if (fabsf(line[i].unfiltered - line[i-1].average) > threshold * line[i-1].deviation) {
            if (line[i].unfiltered > line[i-1].average) {
                line[i].output = 255;
            } else {
                line[i].output = 0;
            }

            line[i].filtered = influence * line[i].unfiltered + (1.f - influence) * line[i-1].filtered;
        } else {
            int distance_from_black, distance_from_white;

            distance_from_black = FFABS(line[i].input - scan->black);
            distance_from_white = FFABS(line[i].input - scan->white);

            line[i].output = distance_from_black <= distance_from_white ? 0 : 255;
        }

        line[i].average   = meanf(line + i - lag, lag);
        line[i].deviation = stddevf(line + i - lag, lag);
    }
}

static int periods(const LineItem *line, CodeItem *code, int len)
{
    int hold = line[LAG].output, cnt = 0;
    int last = LAG;

    memset(code, 0, len * sizeof(*code));

    for (int i = LAG + 1; i < len + LAG; i++) {
        if (line[i].output != hold) {
            code[cnt].size = i - last;
            code[cnt].bit = hold;
            hold = line[i].output;
            last = i;
            cnt++;
        }
    }

    code[cnt].size = LAG + len - last;
    code[cnt].bit = hold;

    return cnt + 1;
}

static void dump_code(AVFilterContext *ctx, ScanItem *scan, int len, int item)
{
    av_log(ctx, AV_LOG_DEBUG, "%d:", item);
    for (int i = 0; i < len; i++) {
        av_log(ctx, AV_LOG_DEBUG, " %03d", scan->code[i].size);
    }
    av_log(ctx, AV_LOG_DEBUG, "\n");
}

#define READ_LINE(type, name)                                                 \
static void read_##name(AVFrame *in, int nb_line, LineItem *line, int lp, int w) \
{                                                                             \
    const type *src = (const type *)(&in->data[0][nb_line * in->linesize[0]]);\
                                                                              \
    if (lp) {                                                                 \
        for (int i = 0; i < w; i++) {                                         \
            int a = FFMAX(i - 3, 0);                                          \
            int b = FFMAX(i - 2, 0);                                          \
            int c = FFMAX(i - 1, 0);                                          \
            int d = FFMIN(i + 3, w-1);                                        \
            int e = FFMIN(i + 2, w-1);                                        \
            int f = FFMIN(i + 1, w-1);                                        \
                                                                              \
            line[LAG + i].input = (src[a] + src[b] + src[c] + src[i] +        \
                                   src[d] + src[e] + src[f] + 6) / 7;         \
        }                                                                     \
    } else {                                                                  \
        for (int i = 0; i < w; i++) {                                         \
            line[LAG + i].input = src[i];                                     \
        }                                                                     \
    }                                                                         \
}

READ_LINE(uint8_t, byte)
READ_LINE(uint16_t, word)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ReadEIA608Context *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    if (!desc)
        return AVERROR_BUG;
    s->depth = desc->comp[0].depth;
    s->max = (1 << desc->comp[0].depth) - 1;
    s->read_line[0] = read_byte;
    s->read_line[1] = read_word;

    return config_filter(ctx, s->start, s->end);
}

static void extract_line(AVFilterContext *ctx, AVFrame *in, ScanItem *scan, int w, int nb_line)
{
    ReadEIA608Context *s = ctx->priv;
    LineItem *line = scan->line;
    int i, j, ch, len;
    uint8_t codes[19] = { 0 };
    float bit_size = 0.f;
    int parity;

    memset(line, 0, (w + LAG) * sizeof(*line));
    scan->byte[0] = scan->byte[1] = 0;
    scan->found = 0;

    s->read_line[s->depth > 8](in, nb_line, line, s->lp, w);

    build_histogram(s, scan, line, w);
    find_black_and_white(s, scan);
    if (scan->white - scan->black < 5)
        return;

    thresholding(s, scan, line, LAG, 1, 0, w);
    len = periods(line, scan->code, w);
    dump_code(ctx, scan, len, nb_line);
    if (len < 15 ||
        scan->code[14].bit != 0 ||
        w / (float)scan->code[14].size < SYNC_BITSIZE_MIN ||
        w / (float)scan->code[14].size > SYNC_BITSIZE_MAX) {
        return;
    }

    for (i = 14; i < len; i++) {
        bit_size += scan->code[i].size;
    }

    bit_size /= 19.f;
    for (i = 1; i < 14; i++) {
        if (scan->code[i].size / bit_size > CLOCK_BITSIZE_MAX ||
            scan->code[i].size / bit_size < CLOCK_BITSIZE_MIN) {
            return;
        }
    }

    if (scan->code[15].size / bit_size < 0.45f) {
        return;
    }

    for (j = 0, i = 14; i < len; i++) {
        int run, bit;

        run = lrintf(scan->code[i].size / bit_size);
        bit = scan->code[i].bit;

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
            scan->byte[ch] |= b << i;
        }

        if (s->chp) {
            if (!(parity & 1)) {
                scan->byte[ch] = 0x7F;
            }
        }
    }

    scan->nb_line = nb_line;
    scan->found = 1;
}

static int extract_lines(AVFilterContext *ctx, void *arg,
                         int job, int nb_jobs)
{
    ReadEIA608Context *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const int h = s->end - s->start + 1;
    const int start = (h * job) / nb_jobs;
    const int end   = (h * (job+1)) / nb_jobs;
    AVFrame *in = arg;

    for (int i = start; i < end; i++) {
        ScanItem *scan = &s->scan[i];

        extract_line(ctx, in, scan, inlink->w, s->start + i);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ReadEIA608Context *s = ctx->priv;
    int nb_found;

    ff_filter_execute(ctx, extract_lines, in, NULL,
                      FFMIN(FFMAX(s->end - s->start + 1, 1), ff_filter_get_nb_threads(ctx)));

    nb_found = 0;
    for (int i = 0; i < s->end - s->start + 1; i++) {
        ScanItem *scan = &s->scan[i];
        uint8_t key[128], value[128];

        if (!scan->found)
            continue;

        //snprintf(key, sizeof(key), "lavfi.readeia608.%d.bits", nb_found);
        //snprintf(value, sizeof(value), "0b%d%d%d%d%d%d%d%d 0b%d%d%d%d%d%d%d%d", codes[3]==255,codes[4]==255,codes[5]==255,codes[6]==255,codes[7]==255,codes[8]==255,codes[9]==255,codes[10]==255,codes[11]==255,codes[12]==255,codes[13]==255,codes[14]==255,codes[15]==255,codes[16]==255,codes[17]==255,codes[18]==255);
        //av_dict_set(&in->metadata, key, value, 0);

        snprintf(key, sizeof(key), "lavfi.readeia608.%d.cc", nb_found);
        snprintf(value, sizeof(value), "0x%02X%02X", scan->byte[0], scan->byte[1]);
        av_dict_set(&in->metadata, key, value, 0);

        snprintf(key, sizeof(key), "lavfi.readeia608.%d.line", nb_found);
        av_dict_set_int(&in->metadata, key, scan->nb_line, 0);

        nb_found++;
    }

    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ReadEIA608Context *s = ctx->priv;

    for (int i = 0; i < s->nb_allocated; i++) {
        ScanItem *scan = &s->scan[i];

        av_freep(&scan->histogram);
        av_freep(&scan->code);
        av_freep(&scan->line);
    }

    s->nb_allocated = 0;
    av_freep(&s->scan);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    ReadEIA608Context *s = ctx->priv;
    int ret, start = s->start, end = s->end;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    ret = config_filter(ctx, s->start, s->end);
    if (ret < 0) {
        s->start = start;
        s->end = end;
    }

    return 0;
}

static const AVFilterPad readeia608_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_readeia608 = {
    .name          = "readeia608",
    .description   = NULL_IF_CONFIG_SMALL("Read EIA-608 Closed Caption codes from input video and write them to frame metadata."),
    .priv_size     = sizeof(ReadEIA608Context),
    .priv_class    = &readeia608_class,
    FILTER_INPUTS(readeia608_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .uninit        = uninit,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                     AVFILTER_FLAG_SLICE_THREADS            |
                     AVFILTER_FLAG_METADATA_ONLY,
    .process_command = process_command,
};
