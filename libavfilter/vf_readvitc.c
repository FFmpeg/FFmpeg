/*
 * Copyright (c) 2016 Tobias Rapp
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
 * Filter for reading the vertical interval timecode (VITC).
 * See also https://en.wikipedia.org/wiki/Vertical_interval_timecode
 */

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timecode.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define LINE_DATA_SIZE 9

typedef struct ReadVitcContext {
    const AVClass *class;

    int scan_max;
    double thr_b;
    double thr_w;

    int threshold_black;
    int threshold_white;
    int threshold_gray;
    int grp_width;
    uint8_t line_data[LINE_DATA_SIZE];
    char tcbuf[AV_TIMECODE_STR_SIZE];
} ReadVitcContext;

#define OFFSET(x) offsetof(ReadVitcContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption readvitc_options[] = {
    { "scan_max", "maximum line numbers to scan for VITC data", OFFSET(scan_max), AV_OPT_TYPE_INT, {.i64 = 45 }, -1, INT_MAX, FLAGS },
    { "thr_b",    "black color threshold", OFFSET(thr_b), AV_OPT_TYPE_DOUBLE, {.dbl = 0.2 }, 0, 1.0, FLAGS },
    { "thr_w",    "white color threshold", OFFSET(thr_w), AV_OPT_TYPE_DOUBLE, {.dbl = 0.6 }, 0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(readvitc);

static uint8_t get_vitc_crc( uint8_t *line ) {
    uint8_t crc;

    crc = 0x01 | (line[0] << 2);
    crc ^= (line[0] >> 6) | 0x04 | (line[1] << 4);
    crc ^= (line[1] >> 4) | 0x10 | (line[2] << 6);
    crc ^= (line[2] >> 2) | 0x40;
    crc ^= line[3];
    crc ^= 0x01 | (line[4] << 2);
    crc ^= (line[4] >> 6) | 0x04 | (line[5] << 4);
    crc ^= (line[5] >> 4) | 0x10 | (line[6] << 6);
    crc ^= (line[6] >> 2) | 0x40;
    crc ^= line[7];
    crc ^= 0x01;
    crc = (crc >> 2) | (crc << 6);  // rotate byte right by two bits
    return crc;
}

static inline uint8_t get_pit_avg3( uint8_t *line, int i ) {
    return ((line[i-1] + line[i] + line[i+1]) / 3);
}

static int read_vitc_line( ReadVitcContext *ctx, uint8_t *src, int line_size, int width, int height )
{
    uint8_t *scan_line;
    int grp_index, pit_index;
    int grp_start_pos;
    uint8_t pit_value;
    int x, y, res = 0;

    if (ctx->scan_max >= 0)
        height = FFMIN(height, ctx->scan_max);

    // scan lines for VITC data, starting from the top
    for (y = 0; y < height; y++) {
        scan_line = src;
        memset(ctx->line_data, 0, LINE_DATA_SIZE);
        grp_index = 0;
        x = 0;
        while ((x < width) && (grp_index < 9)) {
            // search next sync pattern
            while ((x < width) && (scan_line[x] < ctx->threshold_white))
                x++;
            while ((x < width) && (scan_line[x] > ctx->threshold_black))
                x++;
            x = FFMAX(x - ((ctx->grp_width+10) / 20), 1);  // step back a half pit
            grp_start_pos = x;
            if ((grp_start_pos + ctx->grp_width) > width)
                break;  // not enough pixels for reading a whole pit group
            pit_value = get_pit_avg3(scan_line, x);
            if (pit_value < ctx->threshold_white)
               break;  // first sync bit mismatch
            x = grp_start_pos + ((ctx->grp_width) / 10);
            pit_value = get_pit_avg3(scan_line, x);
            if (pit_value > ctx->threshold_black )
                break;  // second sync bit mismatch
            for (pit_index = 0; pit_index <= 7; pit_index++) {
                x = grp_start_pos + (((pit_index+2)*ctx->grp_width) / 10);
                pit_value = get_pit_avg3(scan_line, x);
                if (pit_value > ctx->threshold_gray)
                    ctx->line_data[grp_index] |= (1 << pit_index);
            }
            grp_index++;
        }
        if ((grp_index == 9) && (get_vitc_crc(ctx->line_data) == ctx->line_data[8])) {
            res = 1;
            break;
        }
        src += line_size;
    }

    return res;
}

static unsigned bcd2uint(uint8_t high, uint8_t low)
{
   if (high > 9 || low > 9)
       return 0;
   return 10*high + low;
}

static char *make_vitc_tc_string(char *buf, uint8_t *line)
{
    unsigned hh   = bcd2uint(line[7] & 0x03, line[6] & 0x0f);  // 6-bit hours
    unsigned mm   = bcd2uint(line[5] & 0x07, line[4] & 0x0f);  // 7-bit minutes
    unsigned ss   = bcd2uint(line[3] & 0x07, line[2] & 0x0f);  // 7-bit seconds
    unsigned ff   = bcd2uint(line[1] & 0x03, line[0] & 0x0f);  // 6-bit frames
    unsigned drop = (line[1] & 0x04);                          // 1-bit drop flag
    snprintf(buf, AV_TIMECODE_STR_SIZE, "%02u:%02u:%02u%c%02u",
             hh, mm, ss, drop ? ';' : ':', ff);
    return buf;
}

static av_cold int init(AVFilterContext *ctx)
{
    ReadVitcContext *s = ctx->priv;

    s->threshold_black = s->thr_b * UINT8_MAX;
    s->threshold_white = s->thr_w * UINT8_MAX;
    if (s->threshold_black > s->threshold_white) {
        av_log(ctx, AV_LOG_WARNING, "Black color threshold is higher than white color threshold (%g > %g)\n",
                s->thr_b, s->thr_w);
        return AVERROR(EINVAL);
    }
    s->threshold_gray = s->threshold_white - ((s->threshold_white - s->threshold_black) / 2);
    av_log(ctx, AV_LOG_DEBUG, "threshold_black:%d threshold_white:%d threshold_gray:%d\n",
            s->threshold_black, s->threshold_white, s->threshold_gray);

    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ReadVitcContext *s = ctx->priv;

    s->grp_width = inlink->w * 5 / 48;
    av_log(ctx, AV_LOG_DEBUG, "w:%d h:%d grp_width:%d scan_max:%d\n",
            inlink->w, inlink->h, s->grp_width, s->scan_max);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_NV16,
        AV_PIX_FMT_NV21,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pixel_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ReadVitcContext *s = ctx->priv;
    int found;

    found = read_vitc_line(s, frame->data[0], frame->linesize[0], inlink->w, inlink->h);
    av_dict_set(avpriv_frame_get_metadatap(frame), "lavfi.readvitc.found", (found ? "1" : "0"), 0);
    if (found)
        av_dict_set(avpriv_frame_get_metadatap(frame), "lavfi.readvitc.tc_str", make_vitc_tc_string(s->tcbuf, s->line_data), 0);

    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_readvitc = {
    .name          = "readvitc",
    .description   = NULL_IF_CONFIG_SMALL("Read vertical interval timecode and write it to frame metadata."),
    .priv_size     = sizeof(ReadVitcContext),
    .priv_class    = &readvitc_class,
    .inputs        = inputs,
    .outputs       = outputs,
    .init          = init,
    .query_formats = query_formats,
};
