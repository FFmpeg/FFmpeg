/*
 * Copyright (c) 2017-2022 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/channel_layout.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "libavutil/timestamp.h"
#include "libavutil/xga_font_data.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"
#include "audio.h"
#include "video.h"

typedef struct AVSyncTestContext {
    const AVClass *class;

    int w, h;
    AVRational frame_rate;
    int sample_rate;
    int64_t duration;
    int64_t apts, vpts;
    float amplitude;
    int period;
    int delay;
    int cycle;

    int beep;
    int beep_duration;
    int flash;
    int dir;
    AVRational vdelay, delay_max, delay_min;
    AVRational delay_range;
    int64_t prev_intpart;

    uint8_t rgba[3][4];
    FFDrawContext draw;
    FFDrawColor fg;
    FFDrawColor bg;
    FFDrawColor ag;
} AVSyncTestContext;

#define OFFSET(x) offsetof(AVSyncTestContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption avsynctest_options[] = {
    {"size",       "set frame size",  OFFSET(w),            AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"},   0,   0, V },
    {"s",          "set frame size",  OFFSET(w),            AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"},   0,   0, V },
    {"framerate",  "set frame rate",  OFFSET(frame_rate),   AV_OPT_TYPE_VIDEO_RATE, {.str="30"},   0,INT_MAX, V },
    {"fr",         "set frame rate",  OFFSET(frame_rate),   AV_OPT_TYPE_VIDEO_RATE, {.str="30"},   0,INT_MAX, V },
    {"samplerate", "set sample rate", OFFSET(sample_rate),  AV_OPT_TYPE_INT,        {.i64=44100},8000,384000, A },
    {"sr",         "set sample rate", OFFSET(sample_rate),  AV_OPT_TYPE_INT,        {.i64=44100},8000,384000, A },
    {"amplitude",  "set beep amplitude", OFFSET(amplitude), AV_OPT_TYPE_FLOAT,      {.dbl=.7},       0.,  1., A },
    {"a",          "set beep amplitude", OFFSET(amplitude), AV_OPT_TYPE_FLOAT,      {.dbl=.7},       0.,  1., A },
    {"period",     "set beep period", OFFSET(period),       AV_OPT_TYPE_INT,        {.i64=3},         1, 99., A },
    {"p",          "set beep period", OFFSET(period),       AV_OPT_TYPE_INT,        {.i64=3},         1, 99., A },
    {"delay",      "set flash delay", OFFSET(delay),        AV_OPT_TYPE_INT,        {.i64=0},       -30,  30, V },
    {"dl",         "set flash delay", OFFSET(delay),        AV_OPT_TYPE_INT,        {.i64=0},       -30,  30, V },
    {"cycle",      "set delay cycle", OFFSET(cycle),        AV_OPT_TYPE_BOOL,       {.i64=0},         0,   1, V },
    {"c",          "set delay cycle", OFFSET(cycle),        AV_OPT_TYPE_BOOL,       {.i64=0},         0,   1, V },
    {"duration",   "set duration",    OFFSET(duration),     AV_OPT_TYPE_DURATION,   {.i64=0},         0, INT64_MAX, V|A },
    {"d",          "set duration",    OFFSET(duration),     AV_OPT_TYPE_DURATION,   {.i64=0},         0, INT64_MAX, V|A },
    {"fg",         "set foreground color", OFFSET(rgba[0]), AV_OPT_TYPE_COLOR,      {.str="white"},   0,   0, V },
    {"bg",         "set background color", OFFSET(rgba[1]), AV_OPT_TYPE_COLOR,      {.str="black"},   0,   0, V },
    {"ag",         "set additional color", OFFSET(rgba[2]), AV_OPT_TYPE_COLOR,      {.str="gray"},    0,   0, V },
    {NULL},
};

AVFILTER_DEFINE_CLASS(avsynctest);

static av_cold int query_formats(AVFilterContext *ctx)
{
    AVSyncTestContext *s = ctx->priv;
    AVFilterChannelLayouts *chlayout = NULL;
    int sample_rates[] = { s->sample_rate, -1 };
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_NONE
    };
    AVFilterFormats *formats;
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    if ((ret = ff_formats_ref(formats, &ctx->outputs[0]->incfg.formats)) < 0)
        return ret;

    formats = ff_draw_supported_pixel_formats(0);
    if (!formats)
        return AVERROR(ENOMEM);
    if ((ret = ff_formats_ref(formats, &ctx->outputs[1]->incfg.formats)) < 0)
        return ret;

    if ((ret = ff_add_channel_layout(&chlayout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO)) < 0)
        return ret;
    ret = ff_set_common_channel_layouts(ctx, chlayout);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_rates);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static av_cold int aconfig_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVSyncTestContext *s = ctx->priv;

    outlink->sample_rate = s->sample_rate;
    outlink->time_base = (AVRational){1, s->sample_rate};

    s->beep_duration = av_rescale(s->sample_rate, s->frame_rate.den, s->frame_rate.num);
    s->duration = av_rescale(s->duration, s->sample_rate, AV_TIME_BASE);

    return 0;
}

static av_cold int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVSyncTestContext *s = ctx->priv;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->time_base = av_inv_q(s->frame_rate);
    outlink->frame_rate = s->frame_rate;
    outlink->sample_aspect_ratio = (AVRational) {1, 1};
    s->delay_min = av_mul_q(s->frame_rate, av_make_q(-1, 2));
    s->delay_max = av_mul_q(s->delay_min, av_make_q(-1, 1));
    s->delay_range = av_sub_q(s->delay_max, s->delay_min);
    s->vdelay = av_make_q(s->delay, 1);
    s->dir = 1;
    s->prev_intpart = INT64_MIN;

    ff_draw_init(&s->draw, outlink->format, 0);

    ff_draw_color(&s->draw, &s->fg, s->rgba[0]);
    ff_draw_color(&s->draw, &s->bg, s->rgba[1]);
    ff_draw_color(&s->draw, &s->ag, s->rgba[2] );

    return 0;
}

#define FPI 0x8000

static int32_t sin32(int32_t x, int shift)
{
    const double pi = M_PI;
    const int32_t a = ((2.0 * pi) * (1 << 24));
    const int32_t b = (1 << 7) * (12.0 / pi - 1.0 - pi) * (1 << 24);
    const int32_t c = (1 << 9) * 3.0 * (2.0 + pi - 16.0 / pi) * (1 << 24);
    int64_t x2, result;
    int32_t t1, t2;

    x &= 2 * FPI - 1;

    if (x >= (3 * FPI / 2))
        x = x - 2 * FPI;
    else if (x > FPI / 2)
        x = FPI - x;

    x2 = x * x;
    t1 = (x2 * c) >> 32;
    t2 = ((b + t1) * x2) >> 32;
    x = x << 8;

    result = a + t2;
    result *= x;
    result += (1U << 31);
    result >>= (32 - shift);

    return result;
}

static int audio_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVSyncTestContext *s = ctx->priv;
    const int a = lrintf(s->amplitude * 15);
    int64_t duration[2];
    int64_t delta;
    AVFrame *out;
    int32_t *dst;

    delta = av_rescale_q(s->vpts, av_make_q(s->sample_rate, 1), s->frame_rate) - s->apts;
    if (delta < 0)
        return 1;

    duration[0] = av_rescale_rnd(s->sample_rate, s->frame_rate.den, s->frame_rate.num, AV_ROUND_DOWN);
    duration[1] = av_rescale_rnd(s->sample_rate, s->frame_rate.den, s->frame_rate.num, AV_ROUND_UP);

    delta = duration[delta > 0];
    out = ff_get_audio_buffer(outlink, delta);
    if (!out)
        return AVERROR(ENOMEM);

    out->pts = s->apts;
    dst = (int32_t *)out->data[0];

    for (int i = 0; i < delta; i++) {
        if (((s->apts + i) % (s->period * s->sample_rate)) == 0)
            s->beep = 1;
        if (s->beep) {
            dst[i] = sin32(av_rescale_q(800LL * 2LL * FPI, outlink->time_base, av_make_q(1, s->apts + i)), a);
            s->beep++;
        } else {
            dst[i] = 0;
        }
        if (s->beep >= s->beep_duration) {
            s->beep = 0;
        }
    }
    s->apts += out->nb_samples;

    return ff_filter_frame(outlink, out);
}

static void draw_text(FFDrawContext *draw, AVFrame *out, FFDrawColor *color,
                      int x0, int y0, const uint8_t *text)
{
    int x = x0;

    for (; *text; text++) {
        if (*text == '\n') {
            x = x0;
            y0 += 8;
            continue;
        }
        ff_blend_mask(draw, color, out->data, out->linesize,
                      out->width, out->height,
                      avpriv_cga_font + *text * 8, 1, 8, 8, 0, 0, x, y0);
        x += 8;
    }
}

static int offset(int x, int num, int den)
{
    return av_rescale_rnd(x, num, den, AV_ROUND_UP);
}

static int video_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVSyncTestContext *s = ctx->priv;
    const int w = outlink->w;
    const int h = outlink->h;
    const int step = av_rescale_rnd(w, s->delay_range.den, s->delay_range.num, AV_ROUND_DOWN);
    char text[128];
    int new_offset;
    int64_t delta, temp, intpart;
    AVFrame *out;

    delta = av_rescale_q(s->apts, s->frame_rate, av_make_q(s->sample_rate, 1)) - s->vpts;
    if (delta < 0)
        return 1;

    out = ff_get_video_buffer(outlink, w, h);
    if (!out)
        return AVERROR(ENOMEM);

    ff_fill_rectangle(&s->draw, &s->bg, out->data, out->linesize, 0, 0, w, h);

    snprintf(text, sizeof(text), "FRN: %"PRId64"", s->vpts);
    draw_text(&s->draw, out, &s->fg, offset(w, 1, 10), offset(h, 1, 10), text);

    snprintf(text, sizeof(text), "SEC: %s", av_ts2timestr(s->vpts, &outlink->time_base));
    draw_text(&s->draw, out, &s->fg, offset(w, 1, 10), offset(h, 9, 10), text);

    snprintf(text, sizeof(text), "DLY: %d", s->vdelay.num);
    draw_text(&s->draw, out, &s->fg, offset(w, 9, 10) - strlen(text) * 8, offset(h, 9, 10), text);

    snprintf(text, sizeof(text), "FPS: %d/%d", s->frame_rate.num, s->frame_rate.den);
    draw_text(&s->draw, out, &s->fg, offset(w, 9, 10) - strlen(text) * 8, offset(h, 1, 10), text);

    snprintf(text, sizeof(text), "P: %d", s->period);
    draw_text(&s->draw, out, &s->ag, offset(w, 1, 2) - strlen(text) * 4, offset(h, 9, 10), text);

    snprintf(text, sizeof(text), "SR: %d", s->sample_rate);
    draw_text(&s->draw, out, &s->ag, offset(w, 1, 2) - strlen(text) * 4, offset(h, 1, 10), text);

    snprintf(text, sizeof(text), "A: %1.2f", s->amplitude);
    draw_text(&s->draw, out, &s->ag, offset(w, 1, 10), offset(h, 1, 2), text);

    snprintf(text, sizeof(text), "WxH: %dx%d", w, h);
    draw_text(&s->draw, out, &s->ag, offset(w, 9, 10) - strlen(text) * 8, offset(h, 1, 2), text);

    temp    = s->vpts + s->vdelay.num;
    intpart = av_rescale_rnd(temp, outlink->time_base.num, outlink->time_base.den, AV_ROUND_NEAR_INF);
    intpart = temp - av_rescale_rnd(intpart, outlink->time_base.den, outlink->time_base.num, AV_ROUND_NEAR_INF);

    new_offset = offset(w, 1, 2);
    ff_fill_rectangle(&s->draw, &s->fg, out->data, out->linesize,
                      av_clip(new_offset + step * intpart, 0, w - 2),
                      offset(h, 141, 200), offset(step, 2, 3), offset(h, 1, 25));

    if (intpart == 0 && s->prev_intpart != intpart) {
        if (s->flash >= s->period) {
            int result;

            if (s->cycle)
                s->vdelay = av_add_q(s->vdelay, av_make_q(s->dir, 1));
            result = av_cmp_q(s->vdelay, s->delay_max);
            if (result >= 0)
                s->dir = -1;
            result = av_cmp_q(s->vdelay, s->delay_min);
            if (result <= 0)
                s->dir = 1;
            ff_fill_rectangle(&s->draw, &s->fg, out->data, out->linesize,
                              offset(w, 1, 3), offset(h, 1, 3), offset(w, 1, 3), offset(h, 1, 4));
            s->flash = 0;
        }
        s->flash++;
    }
    s->prev_intpart = intpart;

    for (int i = av_rescale(s->delay_min.num, 1, s->delay_min.den);
             i < av_rescale(s->delay_max.num, 1, s->delay_max.den); i++) {
        ff_fill_rectangle(&s->draw, &s->fg, out->data, out->linesize,
                          av_clip(new_offset + step * i, 0, w - 2),
                          offset(h, 7, 10), 1, offset(h, 1, 20));
    }

    out->pts = s->vpts++;

    return ff_filter_frame(outlink, out);
}

static int activate(AVFilterContext *ctx)
{
    AVSyncTestContext *s = ctx->priv;
    AVFilterLink *aoutlink = ctx->outputs[0];
    AVFilterLink *voutlink = ctx->outputs[1];
    int ret = FFERROR_NOT_READY;

    if (!ff_outlink_frame_wanted(aoutlink) &&
        !ff_outlink_frame_wanted(voutlink))
        return ret;

    if (s->duration > 0 && s->apts >= s->duration) {
        ff_outlink_set_status(aoutlink, AVERROR_EOF, s->apts);
        ff_outlink_set_status(voutlink, AVERROR_EOF, s->vpts);
        return 0;
    }

    ret = audio_frame(aoutlink);
    if (ret < 0)
        return ret;
    ret = video_frame(voutlink);

    return ret;
}

static const AVFilterPad avsynctest_outputs[] = {
    {
        .name          = "audio",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = aconfig_props,
    },
    {
        .name          = "video",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props,
    },
};

const AVFilter ff_avsrc_avsynctest = {
    .name          = "avsynctest",
    .description   = NULL_IF_CONFIG_SMALL("Generate an Audio Video Sync Test."),
    .priv_size     = sizeof(AVSyncTestContext),
    .priv_class    = &avsynctest_class,
    .inputs        = NULL,
    .activate      = activate,
    FILTER_OUTPUTS(avsynctest_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
