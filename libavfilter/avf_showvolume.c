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
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"

static const char *const var_names[] = {   "VOLUME",   "CHANNEL",        NULL };
enum                                   { VAR_VOLUME, VAR_CHANNEL, VAR_VARS_NB };

typedef struct ShowVolumeContext {
    const AVClass *class;
    int w, h;
    int b;
    double f;
    AVRational frame_rate;
    char *color;

    AVFrame *out;
    AVExpr *c_expr;
    int draw_text;
    int draw_volume;
    double *values;
} ShowVolumeContext;

#define OFFSET(x) offsetof(ShowVolumeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showvolume_options[] = {
    { "rate", "set video rate",  OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, 0, FLAGS },
    { "r",    "set video rate",  OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, 0, FLAGS },
    { "b", "set border width",   OFFSET(b), AV_OPT_TYPE_INT, {.i64=1}, 0, 5, FLAGS },
    { "w", "set channel width",  OFFSET(w), AV_OPT_TYPE_INT, {.i64=400}, 80, 1080, FLAGS },
    { "h", "set channel height", OFFSET(h), AV_OPT_TYPE_INT, {.i64=20}, 1, 100, FLAGS },
    { "f", "set fade",           OFFSET(f), AV_OPT_TYPE_DOUBLE, {.dbl=0.95}, 0.001, 1, FLAGS },
    { "c", "set volume color expression", OFFSET(color), AV_OPT_TYPE_STRING, {.str="if(gte(VOLUME,-6), if(gte(VOLUME,-2), if(gte(VOLUME,-1), if(gt(VOLUME,0), 0xff0000ff, 0xff0066ff), 0xff00ffff),0xff00ff00),0xffff0000)"}, 0, 0, FLAGS },
    { "t", "display channel names", OFFSET(draw_text), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "v", "display volume value", OFFSET(draw_volume), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
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
    if ((ret = ff_formats_ref(formats, &inlink->out_formats)) < 0)
        return ret;

    layouts = ff_all_channel_layouts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->out_samplerates)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->in_formats)) < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ShowVolumeContext *s = ctx->priv;
    int nb_samples;

    nb_samples = FFMAX(1024, ((double)inlink->sample_rate / av_q2d(s->frame_rate)) + 0.5);
    inlink->partial_buf_size =
    inlink->min_samples =
    inlink->max_samples = nb_samples;
    s->values = av_calloc(inlink->channels * VAR_VARS_NB, sizeof(double));
    if (!s->values)
        return AVERROR(ENOMEM);
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    ShowVolumeContext *s = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];

    outlink->w = s->w;
    outlink->h = s->h * inlink->channels + (inlink->channels - 1) * s->b;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->frame_rate = s->frame_rate;

    return 0;
}

static void drawtext(AVFrame *pic, int x, int y, const char *txt)
{
    const uint8_t *font;
    int font_height;
    int i;

    font = avpriv_cga_font,   font_height =  8;

    for (i = 0; txt[i]; i++) {
        int char_y, mask;
        uint8_t *p = pic->data[0] + y*pic->linesize[0] + (x + i*8)*4;

        for (char_y = 0; char_y < font_height; char_y++) {
            for (mask = 0x80; mask; mask >>= 1) {
                if (font[txt[i] * font_height + char_y] & mask)
                    AV_WN32(p, ~AV_RN32(p));
                p += 4;
            }
            p += pic->linesize[0] - 8*4;
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowVolumeContext *s = ctx->priv;
    int c, i, j, k;
    AVFrame *out;

    if (!s->out || s->out->width  != outlink->w ||
                   s->out->height != outlink->h) {
        av_frame_free(&s->out);
        s->out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->out) {
            av_frame_free(&insamples);
            return AVERROR(ENOMEM);
        }

        for (i = 0; i < outlink->h; i++)
            memset(s->out->data[0] + i * s->out->linesize[0], 0, outlink->w * 4);
    }
    s->out->pts = insamples->pts;

    for (j = 0; j < outlink->h; j++) {
        uint8_t *dst = s->out->data[0] + j * s->out->linesize[0];
        for (k = 0; k < s->w; k++) {
            dst[k * 4 + 0] = FFMAX(dst[k * 4 + 0] * s->f, 0);
            dst[k * 4 + 1] = FFMAX(dst[k * 4 + 1] * s->f, 0);
            dst[k * 4 + 2] = FFMAX(dst[k * 4 + 2] * s->f, 0);
            dst[k * 4 + 3] = FFMAX(dst[k * 4 + 3] * s->f, 0);
        }
    }

    for (c = 0; c < inlink->channels; c++) {
        float *src = (float *)insamples->extended_data[c];
        float max = 0;
        uint32_t color;

        for (i = 0; i < insamples->nb_samples; i++)
            max = FFMAX(max, src[i]);

        s->values[c * VAR_VARS_NB + VAR_VOLUME] = 20.0 * log10(max);
        max = av_clipf(max, 0, 1);
        s->values[c * VAR_VARS_NB + VAR_CHANNEL] = c;
        color = av_expr_eval(s->c_expr, &s->values[c * VAR_VARS_NB], NULL);

        for (j = 0; j < s->h; j++) {
            uint8_t *dst = s->out->data[0] + (c * s->h + c * s->b + j) * s->out->linesize[0];

            for (k = 0; k < s->w * max; k++)
                AV_WN32A(dst + k * 4, color);
        }

        if (s->h >= 8 && s->draw_text)
            drawtext(s->out, 2, c * (s->h + s->b) + (s->h - 8) / 2,
                     av_get_channel_name(av_channel_layout_extract_channel(insamples->channel_layout, c)));
    }

    av_frame_free(&insamples);
    out = av_frame_clone(s->out);
    if (!out)
        return AVERROR(ENOMEM);
    av_frame_make_writable(out);

    for (c = 0; c < inlink->channels && s->draw_volume; c++) {
        if (s->h >= 8) {
            char buf[16];

            snprintf(buf, sizeof(buf), "%.2f", s->values[c * VAR_VARS_NB + VAR_VOLUME]);
            drawtext(out, FFMAX(0, s->w - 8 * (int)strlen(buf)), c * (s->h + s->b) + (s->h - 8) / 2, buf);
        }
    }

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowVolumeContext *s = ctx->priv;

    av_frame_free(&s->out);
    av_expr_free(s->c_expr);
    av_freep(&s->values);
}

static const AVFilterPad showvolume_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad showvolume_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_avf_showvolume = {
    .name          = "showvolume",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio volume to video output."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(ShowVolumeContext),
    .inputs        = showvolume_inputs,
    .outputs       = showvolume_outputs,
    .priv_class    = &showvolume_class,
};
