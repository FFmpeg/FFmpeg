/*
 * Copyright (c) 2015 Paul B Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/eval.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/xga_font_data.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"

static const char *const var_names[] = {   "VOLUME",   "CHANNEL",   "PEAK",        NULL };
enum                                   { VAR_VOLUME, VAR_CHANNEL, VAR_PEAK, VAR_VARS_NB };
enum DisplayScale   { LINEAR, LOG, NB_DISPLAY_SCALE };

typedef struct ShowVolumeContext {
    const AVClass *class;
    int w, h;
    int b;
    double f;
    AVRational frame_rate;
    char *color;
    int orientation;
    int step;
    float bgopacity;
    int mode;

    int nb_samples;
    AVFrame *out;
    AVExpr *c_expr;
    int draw_text;
    int draw_volume;
    double *values;
    uint32_t *color_lut;
    float *max;
    float rms_factor;
    int display_scale;

    double draw_persistent_duration; /* in second */
    uint8_t persistant_max_rgba[4];
    int persistent_max_frames; /* number of frames to check max value */
    float *max_persistent; /* max value for draw_persistent_max for each channel */
    int *nb_frames_max_display; /* number of frame for each channel, for displaying the max value */

    void (*meter)(float *src, int nb_samples, float *max, float factor);
} ShowVolumeContext;

#define OFFSET(x) offsetof(ShowVolumeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showvolume_options[] = {
    { "rate", "set video rate",  OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate",  OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "b", "set border width",   OFFSET(b), AV_OPT_TYPE_INT, {.i64=1}, 0, 5, FLAGS },
    { "w", "set channel width",  OFFSET(w), AV_OPT_TYPE_INT, {.i64=400}, 80, 8192, FLAGS },
    { "h", "set channel height", OFFSET(h), AV_OPT_TYPE_INT, {.i64=20}, 1, 900, FLAGS },
    { "f", "set fade",           OFFSET(f), AV_OPT_TYPE_DOUBLE, {.dbl=0.95}, 0, 1, FLAGS },
    { "c", "set volume color expression", OFFSET(color), AV_OPT_TYPE_STRING, {.str="PEAK*255+floor((1-PEAK)*255)*256+0xff000000"}, 0, 0, FLAGS },
    { "t", "display channel names", OFFSET(draw_text), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "v", "display volume value", OFFSET(draw_volume), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "dm", "duration for max value display", OFFSET(draw_persistent_duration), AV_OPT_TYPE_DOUBLE, {.dbl=0.}, 0, 9000, FLAGS},
    { "dmc","set color of the max value line", OFFSET(persistant_max_rgba), AV_OPT_TYPE_COLOR, {.str = "orange"}, 0, 0, FLAGS },
    { "o", "set orientation", OFFSET(orientation), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "orientation" },
    {   "h", "horizontal", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "orientation" },
    {   "v", "vertical",   0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "orientation" },
    { "s", "set step size", OFFSET(step), AV_OPT_TYPE_INT, {.i64=0}, 0, 5, FLAGS },
    { "p", "set background opacity", OFFSET(bgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0}, 0, 1, FLAGS },
    { "m", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "mode" },
    {   "p", "peak", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "mode" },
    {   "r", "rms",  0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "mode" },
    { "ds", "set display scale", OFFSET(display_scale), AV_OPT_TYPE_INT, {.i64=LINEAR}, LINEAR, NB_DISPLAY_SCALE - 1, FLAGS, "display_scale" },
    {   "lin", "linear", 0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, "display_scale" },
    {   "log", "log",  0, AV_OPT_TYPE_CONST, {.i64=LOG}, 0, 0, FLAGS, "display_scale" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showvolume);

static av_cold int init(AVFilterContext *ctx)
{
    ShowVolumeContext *s = ctx->priv;
    int ret;

    if (s->color) {
        ret = av_expr_parse(&s->c_expr, s->color, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.formats)) < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.samplerates)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->incfg.formats)) < 0)
        return ret;

    return 0;
}

static void find_peak(float *src, int nb_samples, float *peak, float factor)
{
    int i;

    *peak = 0;
    for (i = 0; i < nb_samples; i++)
        *peak = FFMAX(*peak, FFABS(src[i]));
}

static void find_rms(float *src, int nb_samples, float *rms, float factor)
{
    int i;

    for (i = 0; i < nb_samples; i++)
        *rms += factor * (src[i] * src[i] - *rms);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ShowVolumeContext *s = ctx->priv;

    s->nb_samples = FFMAX(1, av_rescale(inlink->sample_rate, s->frame_rate.den, s->frame_rate.num));
    s->values = av_calloc(inlink->channels * VAR_VARS_NB, sizeof(double));
    if (!s->values)
        return AVERROR(ENOMEM);

    s->color_lut = av_calloc(s->w, sizeof(*s->color_lut) * inlink->channels);
    if (!s->color_lut)
        return AVERROR(ENOMEM);

    s->max = av_calloc(inlink->channels, sizeof(*s->max));
    if (!s->max)
        return AVERROR(ENOMEM);

    s->rms_factor = 10000. / inlink->sample_rate;

    switch (s->mode) {
    case 0: s->meter = find_peak; break;
    case 1: s->meter = find_rms;  break;
    default: return AVERROR_BUG;
    }

    if (s->draw_persistent_duration > 0.) {
        s->persistent_max_frames = (int) FFMAX(av_q2d(s->frame_rate) * s->draw_persistent_duration, 1.);
        s->max_persistent = av_calloc(inlink->channels * s->persistent_max_frames, sizeof(*s->max_persistent));
        s->nb_frames_max_display = av_calloc(inlink->channels * s->persistent_max_frames, sizeof(*s->nb_frames_max_display));
    }
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    ShowVolumeContext *s = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ch;

    if (s->orientation) {
        outlink->h = s->w;
        outlink->w = s->h * inlink->channels + (inlink->channels - 1) * s->b;
    } else {
        outlink->w = s->w;
        outlink->h = s->h * inlink->channels + (inlink->channels - 1) * s->b;
    }

    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->frame_rate = s->frame_rate;

    for (ch = 0; ch < inlink->channels; ch++) {
        int i;

        for (i = 0; i < s->w; i++) {
            float max = i / (float)(s->w - 1);

            s->values[ch * VAR_VARS_NB + VAR_PEAK] = max;
            s->values[ch * VAR_VARS_NB + VAR_VOLUME] = 20.0 * log10(max);
            s->values[ch * VAR_VARS_NB + VAR_CHANNEL] = ch;
            s->color_lut[ch * s->w + i] = av_expr_eval(s->c_expr, &s->values[ch * VAR_VARS_NB], NULL);
        }
    }

    return 0;
}

static void drawtext(AVFrame *pic, int x, int y, const char *txt, int o)
{
    const uint8_t *font;
    int font_height;
    int i;

    font = avpriv_cga_font,   font_height =  8;

    for (i = 0; txt[i]; i++) {
        int char_y, mask;

        if (o) { /* vertical orientation */
            for (char_y = font_height - 1; char_y >= 0; char_y--) {
                uint8_t *p = pic->data[0] + (y + i * 10) * pic->linesize[0] + x * 4;
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + font_height - 1 - char_y] & mask)
                        AV_WN32(&p[char_y * 4], ~AV_RN32(&p[char_y * 4]));
                    p += pic->linesize[0];
                }
            }
        } else { /* horizontal orientation */
            uint8_t *p = pic->data[0] + y * pic->linesize[0] + (x + i * 8) * 4;
            for (char_y = 0; char_y < font_height; char_y++) {
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + char_y] & mask)
                        AV_WN32(p, ~AV_RN32(p));
                    p += 4;
                }
                p += pic->linesize[0] - 8 * 4;
            }
        }
    }
}

static void clear_picture(ShowVolumeContext *s, AVFilterLink *outlink)
{
    int i, j;
    const uint32_t bg = (uint32_t)(s->bgopacity * 255) << 24;

    for (i = 0; i < outlink->h; i++) {
        uint32_t *dst = (uint32_t *)(s->out->data[0] + i * s->out->linesize[0]);
        for (j = 0; j < outlink->w; j++)
            AV_WN32A(dst + j, bg);
    }
}

static inline int calc_max_draw(ShowVolumeContext *s, AVFilterLink *outlink, float max)
{
    float max_val;
    if (s->display_scale == LINEAR) {
        max_val = max;
    } else { /* log */
        max_val = av_clipf(0.21 * log10(max) + 1, 0, 1);
    }
    if (s->orientation) { /* vertical */
        return outlink->h - outlink->h * max_val;
    } else { /* horizontal */
        return s->w * max_val;
    }
}

static inline void calc_persistent_max(ShowVolumeContext *s, float max, int channel)
{
    /* update max value for persistent max display */
    if ((max >= s->max_persistent[channel]) || (s->nb_frames_max_display[channel] >= s->persistent_max_frames)) { /* update max value for display */
        s->max_persistent[channel] = max;
        s->nb_frames_max_display[channel] = 0;
    } else {
        s->nb_frames_max_display[channel] += 1; /* incremente display frame count */
    }
}

static inline void draw_max_line(ShowVolumeContext *s, int max_draw, int channel)
{
    int k;
    if (s->orientation) { /* vertical */
        uint8_t *dst = s->out->data[0] + max_draw * s->out->linesize[0] + channel * (s->b + s->h) * 4;
        for (k = 0; k < s->h; k++) {
            memcpy(dst + k * 4, s->persistant_max_rgba, sizeof(s->persistant_max_rgba));
        }
    } else { /* horizontal */
        for (k = 0; k < s->h; k++) {
            uint8_t *dst = s->out->data[0] + (channel * s->h + channel * s->b + k) * s->out->linesize[0];
            memcpy(dst + max_draw * 4, s->persistant_max_rgba, sizeof(s->persistant_max_rgba));
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowVolumeContext *s = ctx->priv;
    const int step = s->step;
    int c, j, k, max_draw;
    AVFrame *out;

    if (!s->out || s->out->width  != outlink->w ||
                   s->out->height != outlink->h) {
        av_frame_free(&s->out);
        s->out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->out) {
            av_frame_free(&insamples);
            return AVERROR(ENOMEM);
        }
        clear_picture(s, outlink);
    }
    s->out->pts = insamples->pts;

    if ((s->f < 1.) && (s->f > 0.)) {
        for (j = 0; j < outlink->h; j++) {
            uint8_t *dst = s->out->data[0] + j * s->out->linesize[0];
            const uint32_t alpha = s->bgopacity * 255;

            for (k = 0; k < outlink->w; k++) {
                dst[k * 4 + 0] = FFMAX(dst[k * 4 + 0] * s->f, 0);
                dst[k * 4 + 1] = FFMAX(dst[k * 4 + 1] * s->f, 0);
                dst[k * 4 + 2] = FFMAX(dst[k * 4 + 2] * s->f, 0);
                dst[k * 4 + 3] = FFMAX(dst[k * 4 + 3] * s->f, alpha);
            }
        }
    } else if (s->f == 0.) {
        clear_picture(s, outlink);
    }

    if (s->orientation) { /* vertical */
        for (c = 0; c < inlink->channels; c++) {
            float *src = (float *)insamples->extended_data[c];
            uint32_t *lut = s->color_lut + s->w * c;
            float max;

            s->meter(src, insamples->nb_samples, &s->max[c], s->rms_factor);
            max = s->max[c];

            s->values[c * VAR_VARS_NB + VAR_VOLUME] = 20.0 * log10(max);
            max = av_clipf(max, 0, 1);
            max_draw = calc_max_draw(s, outlink, max);

            for (j = max_draw; j < s->w; j++) {
                uint8_t *dst = s->out->data[0] + j * s->out->linesize[0] + c * (s->b + s->h) * 4;
                for (k = 0; k < s->h; k++) {
                    AV_WN32A(&dst[k * 4], lut[s->w - j - 1]);
                    if (j & step)
                        j += step;
                }
            }

            if (s->h >= 8 && s->draw_text) {
                const char *channel_name = av_get_channel_name(av_channel_layout_extract_channel(insamples->channel_layout, c));
                if (!channel_name)
                    continue;
                drawtext(s->out, c * (s->h + s->b) + (s->h - 10) / 2, outlink->h - 35, channel_name, 1);
            }

            if (s->draw_persistent_duration > 0.) {
                calc_persistent_max(s, max, c);
                max_draw = FFMAX(0, calc_max_draw(s, outlink, s->max_persistent[c]) - 1);
                draw_max_line(s, max_draw, c);
            }
        }
    } else { /* horizontal */
        for (c = 0; c < inlink->channels; c++) {
            float *src = (float *)insamples->extended_data[c];
            uint32_t *lut = s->color_lut + s->w * c;
            float max;

            s->meter(src, insamples->nb_samples, &s->max[c], s->rms_factor);
            max = s->max[c];

            s->values[c * VAR_VARS_NB + VAR_VOLUME] = 20.0 * log10(max);
            max = av_clipf(max, 0, 1);
            max_draw = calc_max_draw(s, outlink, max);

            for (j = 0; j < s->h; j++) {
                uint8_t *dst = s->out->data[0] + (c * s->h + c * s->b + j) * s->out->linesize[0];

                for (k = 0; k < max_draw; k++) {
                    AV_WN32A(dst + k * 4, lut[k]);
                    if (k & step)
                        k += step;
                }
            }

            if (s->h >= 8 && s->draw_text) {
                const char *channel_name = av_get_channel_name(av_channel_layout_extract_channel(insamples->channel_layout, c));
                if (!channel_name)
                    continue;
                drawtext(s->out, 2, c * (s->h + s->b) + (s->h - 8) / 2, channel_name, 0);
            }

            if (s->draw_persistent_duration > 0.) {
                calc_persistent_max(s, max, c);
                max_draw = FFMAX(0, calc_max_draw(s, outlink, s->max_persistent[c]) - 1);
                draw_max_line(s, max_draw, c);
            }
        }
    }

    av_frame_free(&insamples);
    out = av_frame_clone(s->out);
    if (!out)
        return AVERROR(ENOMEM);
    av_frame_make_writable(out);

    /* draw volume level */
    for (c = 0; c < inlink->channels && s->h >= 8 && s->draw_volume; c++) {
        char buf[16];

        if (s->orientation) { /* vertical */
            snprintf(buf, sizeof(buf), "%.2f", s->values[c * VAR_VARS_NB + VAR_VOLUME]);
            drawtext(out, c * (s->h + s->b) + (s->h - 8) / 2, 2, buf, 1);
        } else { /* horizontal */
            snprintf(buf, sizeof(buf), "%.2f", s->values[c * VAR_VARS_NB + VAR_VOLUME]);
            drawtext(out, FFMAX(0, s->w - 8 * (int)strlen(buf)), c * (s->h + s->b) + (s->h - 8) / 2, buf, 0);
        }
    }

    return ff_filter_frame(outlink, out);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    ShowVolumeContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->nb_samples, s->nb_samples, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return filter_frame(inlink, in);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowVolumeContext *s = ctx->priv;

    av_frame_free(&s->out);
    av_expr_free(s->c_expr);
    av_freep(&s->values);
    av_freep(&s->color_lut);
    av_freep(&s->max);
}

static const AVFilterPad showvolume_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

static const AVFilterPad showvolume_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_avf_showvolume = {
    .name          = "showvolume",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio volume to video output."),
    .init          = init,
    .activate      = activate,
    .uninit        = uninit,
    .priv_size     = sizeof(ShowVolumeContext),
    FILTER_INPUTS(showvolume_inputs),
    FILTER_OUTPUTS(showvolume_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &showvolume_class,
};
