/*
 * Copyright (c) 2014 Muhammad Faiz <mfcc64@gmail.com>
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

#include "config.h"
#include "libavcodec/avfft.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/xga_font_data.h"
#include "libavutil/eval.h"
#include "avfilter.h"
#include "internal.h"

#include <math.h>
#include <stdlib.h>

#if CONFIG_LIBFREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

/* this filter is designed to do 16 bins/semitones constant Q transform with Brown-Puckette algorithm
 * start from E0 to D#10 (10 octaves)
 * so there are 16 bins/semitones * 12 semitones/octaves * 10 octaves = 1920 bins
 * match with full HD resolution */

#define VIDEO_WIDTH 1920
#define VIDEO_HEIGHT 1080
#define FONT_HEIGHT 32
#define SPECTOGRAM_HEIGHT ((VIDEO_HEIGHT-FONT_HEIGHT)/2)
#define SPECTOGRAM_START (VIDEO_HEIGHT-SPECTOGRAM_HEIGHT)
#define BASE_FREQ 20.051392800492
#define TLENGTH_MIN 0.001
#define TLENGTH_DEFAULT "384/f*tc/(384/f+tc)"
#define VOLUME_MIN 1e-10
#define VOLUME_MAX 100.0
#define FONTCOLOR_DEFAULT "st(0, (midi(f)-59.5)/12);" \
    "st(1, if(between(ld(0),0,1), 0.5-0.5*cos(2*PI*ld(0)), 0));" \
    "r(1-ld(1)) + b(ld(1))"

typedef struct {
    FFTSample *values;
    int start, len;
} Coeffs;

typedef struct {
    const AVClass *class;
    AVFrame *outpicref;
    FFTContext *fft_context;
    FFTComplex *fft_data;
    FFTComplex *fft_result;
    uint8_t *spectogram;
    Coeffs coeffs[VIDEO_WIDTH];
    uint8_t *font_alpha;
    char *fontfile;     /* using freetype */
    uint8_t fontcolor_value[VIDEO_WIDTH*3];  /* result of fontcolor option */
    int64_t frame_count;
    int spectogram_count;
    int spectogram_index;
    int fft_bits;
    int req_fullfilled;
    int remaining_fill;
    char *tlength;
    char *volume;
    char *fontcolor;
    double timeclamp;   /* lower timeclamp, time-accurate, higher timeclamp, freq-accurate (at low freq)*/
    float coeffclamp;   /* lower coeffclamp, more precise, higher coeffclamp, faster */
    int fullhd;         /* if true, output video is at full HD resolution, otherwise it will be halved */
    float gamma;        /* lower gamma, more contrast, higher gamma, more range */
    float gamma2;       /* gamma of bargraph */
    int fps;            /* the required fps is so strict, so it's enough to be int, but 24000/1001 etc cannot be encoded */
    int count;          /* fps * count = transform rate */
    int draw_text;
} ShowCQTContext;

#define OFFSET(x) offsetof(ShowCQTContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showcqt_options[] = {
    { "volume", "set volume", OFFSET(volume), AV_OPT_TYPE_STRING, { .str = "16" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "tlength", "set transform length", OFFSET(tlength), AV_OPT_TYPE_STRING, { .str = TLENGTH_DEFAULT }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "timeclamp", "set timeclamp", OFFSET(timeclamp), AV_OPT_TYPE_DOUBLE, { .dbl = 0.17 }, 0.1, 1.0, FLAGS },
    { "coeffclamp", "set coeffclamp", OFFSET(coeffclamp), AV_OPT_TYPE_FLOAT, { .dbl = 1 }, 0.1, 10, FLAGS },
    { "gamma", "set gamma", OFFSET(gamma), AV_OPT_TYPE_FLOAT, { .dbl = 3 }, 1, 7, FLAGS },
    { "gamma2", "set gamma of bargraph", OFFSET(gamma2), AV_OPT_TYPE_FLOAT, { .dbl = 1 }, 1, 7, FLAGS },
    { "fullhd", "set full HD resolution", OFFSET(fullhd), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { "fps", "set video fps", OFFSET(fps), AV_OPT_TYPE_INT, { .i64 = 25 }, 10, 100, FLAGS },
    { "count", "set number of transform per frame", OFFSET(count), AV_OPT_TYPE_INT, { .i64 = 6 }, 1, 30, FLAGS },
    { "fontfile", "set font file", OFFSET(fontfile), AV_OPT_TYPE_STRING, { .str = NULL }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "fontcolor", "set font color", OFFSET(fontcolor), AV_OPT_TYPE_STRING, { .str = FONTCOLOR_DEFAULT }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "text", "draw text", OFFSET(draw_text), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showcqt);

static av_cold void uninit(AVFilterContext *ctx)
{
    int k;

    ShowCQTContext *s = ctx->priv;
    av_fft_end(s->fft_context);
    s->fft_context = NULL;
    for (k = 0; k < VIDEO_WIDTH; k++)
        av_freep(&s->coeffs[k].values);
    av_freep(&s->fft_data);
    av_freep(&s->fft_result);
    av_freep(&s->spectogram);
    av_freep(&s->font_alpha);
    av_frame_free(&s->outpicref);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE };
    static const int64_t channel_layouts[] = { AV_CH_LAYOUT_STEREO, AV_CH_LAYOUT_STEREO_DOWNMIX, -1 };
    static const int samplerates[] = { 44100, 48000, -1 };

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_formats);

    layouts = avfilter_make_format64_list(channel_layouts);
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts);

    formats = ff_make_format_list(samplerates);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_samplerates);

    /* set output video format */
    formats = ff_make_format_list(pix_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &outlink->in_formats);

    return 0;
}

#if CONFIG_LIBFREETYPE
static void load_freetype_font(AVFilterContext *ctx)
{
    static const char str[] = "EF G A BC D ";
    ShowCQTContext *s = ctx->priv;
    FT_Library lib = NULL;
    FT_Face face = NULL;
    int video_scale = s->fullhd ? 2 : 1;
    int video_width = (VIDEO_WIDTH/2) * video_scale;
    int font_height = (FONT_HEIGHT/2) * video_scale;
    int font_width = 8 * video_scale;
    int font_repeat = font_width * 12;
    int linear_hori_advance = font_width * 65536;
    int non_monospace_warning = 0;
    int x;

    s->font_alpha = NULL;

    if (!s->fontfile)
        return;

    if (FT_Init_FreeType(&lib))
        goto fail;

    if (FT_New_Face(lib, s->fontfile, 0, &face))
        goto fail;

    if (FT_Set_Char_Size(face, 16*64, 0, 0, 0))
        goto fail;

    if (FT_Load_Char(face, 'A', FT_LOAD_RENDER))
        goto fail;

    if (FT_Set_Char_Size(face, 16*64 * linear_hori_advance / face->glyph->linearHoriAdvance, 0, 0, 0))
        goto fail;

    s->font_alpha = av_malloc_array(font_height, video_width);
    if (!s->font_alpha)
        goto fail;

    memset(s->font_alpha, 0, font_height * video_width);

    for (x = 0; x < 12; x++) {
        int sx, sy, rx, bx, by, dx, dy;

        if (str[x] == ' ')
            continue;

        if (FT_Load_Char(face, str[x], FT_LOAD_RENDER))
            goto fail;

        if (face->glyph->advance.x != font_width*64 && !non_monospace_warning) {
            av_log(ctx, AV_LOG_WARNING, "Font is not monospace\n");
            non_monospace_warning = 1;
        }

        sy = font_height - 4*video_scale - face->glyph->bitmap_top;
        for (rx = 0; rx < 10; rx++) {
            sx = rx * font_repeat + x * font_width + face->glyph->bitmap_left;
            for (by = 0; by < face->glyph->bitmap.rows; by++) {
                dy = by + sy;
                if (dy < 0)
                    continue;
                if (dy >= font_height)
                    break;

                for (bx = 0; bx < face->glyph->bitmap.width; bx++) {
                    dx = bx + sx;
                    if (dx < 0)
                        continue;
                    if (dx >= video_width)
                        break;
                    s->font_alpha[dy*video_width+dx] = face->glyph->bitmap.buffer[by*face->glyph->bitmap.width+bx];
                }
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return;

    fail:
    av_log(ctx, AV_LOG_WARNING, "Error while loading freetype font, using default font instead\n");
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    av_freep(&s->font_alpha);
    return;
}
#endif

static double a_weighting(void *p, double f)
{
    double ret = 12200.0*12200.0 * (f*f*f*f);
    ret /= (f*f + 20.6*20.6) * (f*f + 12200.0*12200.0) *
           sqrt((f*f + 107.7*107.7) * (f*f + 737.9*737.9));
    return ret;
}

static double b_weighting(void *p, double f)
{
    double ret = 12200.0*12200.0 * (f*f*f);
    ret /= (f*f + 20.6*20.6) * (f*f + 12200.0*12200.0) * sqrt(f*f + 158.5*158.5);
    return ret;
}

static double c_weighting(void *p, double f)
{
    double ret = 12200.0*12200.0 * (f*f);
    ret /= (f*f + 20.6*20.6) * (f*f + 12200.0*12200.0);
    return ret;
}

static double midi(void *p, double f)
{
    return log2(f/440.0) * 12.0 + 69.0;
}

static double r_func(void *p, double x)
{
    x = av_clipd(x, 0.0, 1.0);
    return (int)(x*255.0+0.5) << 16;
}

static double g_func(void *p, double x)
{
    x = av_clipd(x, 0.0, 1.0);
    return (int)(x*255.0+0.5) << 8;
}

static double b_func(void *p, double x)
{
    x = av_clipd(x, 0.0, 1.0);
    return (int)(x*255.0+0.5);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowCQTContext *s = ctx->priv;
    AVExpr *tlength_expr = NULL, *volume_expr = NULL, *fontcolor_expr = NULL;
    uint8_t *fontcolor_value = s->fontcolor_value;
    static const char * const expr_vars[] = { "timeclamp", "tc", "frequency", "freq", "f", NULL };
    static const char * const expr_func_names[] = { "a_weighting", "b_weighting", "c_weighting", NULL };
    static const char * const expr_fontcolor_func_names[] = { "midi", "r", "g", "b", NULL };
    static double (* const expr_funcs[])(void *, double) = { a_weighting, b_weighting, c_weighting, NULL };
    static double (* const expr_fontcolor_funcs[])(void *, double) = { midi, r_func, g_func, b_func, NULL };
    int fft_len, k, x, ret;
    int num_coeffs = 0;
    int rate = inlink->sample_rate;
    double max_len = rate * (double) s->timeclamp;
    int video_scale = s->fullhd ? 2 : 1;
    int video_width = (VIDEO_WIDTH/2) * video_scale;
    int video_height = (VIDEO_HEIGHT/2) * video_scale;
    int spectogram_height = (SPECTOGRAM_HEIGHT/2) * video_scale;

    s->fft_bits = ceil(log2(max_len));
    fft_len = 1 << s->fft_bits;

    if (rate % (s->fps * s->count)) {
        av_log(ctx, AV_LOG_ERROR, "Rate (%u) is not divisible by fps*count (%u*%u)\n", rate, s->fps, s->count);
        return AVERROR(EINVAL);
    }

    s->fft_data         = av_malloc_array(fft_len, sizeof(*s->fft_data));
    s->fft_result       = av_malloc_array(fft_len + 1, sizeof(*s->fft_result));
    s->fft_context      = av_fft_init(s->fft_bits, 0);

    if (!s->fft_data || !s->fft_result || !s->fft_context)
        return AVERROR(ENOMEM);

#if CONFIG_LIBFREETYPE
    load_freetype_font(ctx);
#else
    if (s->fontfile)
        av_log(ctx, AV_LOG_WARNING, "Freetype is not available, ignoring fontfile option\n");
    s->font_alpha = NULL;
#endif

    ret = av_expr_parse(&tlength_expr, s->tlength, expr_vars, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto eval_error;

    ret = av_expr_parse(&volume_expr, s->volume, expr_vars, expr_func_names,
                        expr_funcs, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto eval_error;

    ret = av_expr_parse(&fontcolor_expr, s->fontcolor, expr_vars, expr_fontcolor_func_names,
                        expr_fontcolor_funcs, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto eval_error;

    for (k = 0; k < VIDEO_WIDTH; k++) {
        double freq = BASE_FREQ * exp2(k * (1.0/192.0));
        double flen, center, tlength, volume;
        int start, end;
        double expr_vars_val[] = { s->timeclamp, s->timeclamp, freq, freq, freq, 0 };

        tlength = av_expr_eval(tlength_expr, expr_vars_val, NULL);
        if (isnan(tlength)) {
            av_log(ctx, AV_LOG_WARNING, "at freq %g: tlength is nan, setting it to %g\n", freq, s->timeclamp);
            tlength = s->timeclamp;
        } else if (tlength < TLENGTH_MIN) {
            av_log(ctx, AV_LOG_WARNING, "at freq %g: tlength is %g, setting it to %g\n", freq, tlength, TLENGTH_MIN);
            tlength = TLENGTH_MIN;
        } else if (tlength > s->timeclamp) {
            av_log(ctx, AV_LOG_WARNING, "at freq %g: tlength is %g, setting it to %g\n", freq, tlength, s->timeclamp);
            tlength = s->timeclamp;
        }

        volume = FFABS(av_expr_eval(volume_expr, expr_vars_val, NULL));
        if (isnan(volume)) {
            av_log(ctx, AV_LOG_WARNING, "at freq %g: volume is nan, setting it to 0\n", freq);
            volume = VOLUME_MIN;
        } else if (volume < VOLUME_MIN) {
            volume = VOLUME_MIN;
        } else if (volume > VOLUME_MAX) {
            av_log(ctx, AV_LOG_WARNING, "at freq %g: volume is %g, setting it to %g\n", freq, volume, VOLUME_MAX);
            volume = VOLUME_MAX;
        }

        if (s->fullhd || !(k & 1)) {
            int fontcolor = av_expr_eval(fontcolor_expr, expr_vars_val, NULL);
            fontcolor_value[0] = (fontcolor >> 16) & 0xFF;
            fontcolor_value[1] = (fontcolor >> 8) & 0xFF;
            fontcolor_value[2] = fontcolor & 0xFF;
            fontcolor_value += 3;
        }

        /* direct frequency domain windowing */
        flen = 8.0 * fft_len / (tlength * rate);
        center = freq * fft_len / rate;
        start = FFMAX(0, ceil(center - 0.5 * flen));
        end = FFMIN(fft_len, floor(center + 0.5 * flen));
        s->coeffs[k].len = end - start + 1;
        s->coeffs[k].start = start;
        num_coeffs += s->coeffs[k].len;
        s->coeffs[k].values = av_malloc_array(s->coeffs[k].len, sizeof(*s->coeffs[k].values));
        if (!s->coeffs[k].values) {
            ret = AVERROR(ENOMEM);
            goto eval_error;
        }
        for (x = start; x <= end; x++) {
            int sign = (x & 1) ? (-1) : 1;
            double u = 2.0 * M_PI * (x - center) * (1.0/flen);
            /* nuttall window */
            double w = 0.355768 + 0.487396 * cos(u) + 0.144232 * cos(2*u) + 0.012604 * cos(3*u);
            s->coeffs[k].values[x-start] = sign * volume * (1.0/fft_len) * w;
        }
    }
    av_expr_free(fontcolor_expr);
    av_expr_free(volume_expr);
    av_expr_free(tlength_expr);
    av_log(ctx, AV_LOG_INFO, "fft_len=%u, num_coeffs=%u\n", fft_len, num_coeffs);

    outlink->w = video_width;
    outlink->h = video_height;

    s->req_fullfilled = 0;
    s->spectogram_index = 0;
    s->frame_count = 0;
    s->spectogram_count = 0;
    s->remaining_fill = fft_len >> 1;
    memset(s->fft_data, 0, fft_len * sizeof(*s->fft_data));

    s->outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!s->outpicref)
        return AVERROR(ENOMEM);

    s->spectogram = av_calloc(spectogram_height, s->outpicref->linesize[0]);
    if (!s->spectogram)
        return AVERROR(ENOMEM);

    outlink->sample_aspect_ratio = av_make_q(1, 1);
    outlink->time_base = av_make_q(1, s->fps);
    outlink->frame_rate = av_make_q(s->fps, 1);
    return 0;

eval_error:
    av_expr_free(fontcolor_expr);
    av_expr_free(volume_expr);
    av_expr_free(tlength_expr);
    return ret;
}

static int plot_cqt(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ShowCQTContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int fft_len = 1 << s->fft_bits;
    FFTSample result[VIDEO_WIDTH][4];
    int x, y, ret = 0;
    int linesize = s->outpicref->linesize[0];
    int video_scale = s->fullhd ? 2 : 1;
    int video_width = (VIDEO_WIDTH/2) * video_scale;
    int spectogram_height = (SPECTOGRAM_HEIGHT/2) * video_scale;
    int spectogram_start = (SPECTOGRAM_START/2) * video_scale;
    int font_height = (FONT_HEIGHT/2) * video_scale;

    /* real part contains left samples, imaginary part contains right samples */
    memcpy(s->fft_result, s->fft_data, fft_len * sizeof(*s->fft_data));
    av_fft_permute(s->fft_context, s->fft_result);
    av_fft_calc(s->fft_context, s->fft_result);
    s->fft_result[fft_len] = s->fft_result[0];

    /* calculating cqt */
    for (x = 0; x < VIDEO_WIDTH; x++) {
        int u;
        FFTComplex v = {0,0};
        FFTComplex w = {0,0};
        FFTComplex l, r;

        for (u = 0; u < s->coeffs[x].len; u++) {
            FFTSample value = s->coeffs[x].values[u];
            int index = s->coeffs[x].start + u;
            v.re += value * s->fft_result[index].re;
            v.im += value * s->fft_result[index].im;
            w.re += value * s->fft_result[fft_len - index].re;
            w.im += value * s->fft_result[fft_len - index].im;
        }

        /* separate left and right, (and multiply by 2.0) */
        l.re = v.re + w.re;
        l.im = v.im - w.im;
        r.re = w.im + v.im;
        r.im = w.re - v.re;
        /* result is power, not amplitude */
        result[x][0] = l.re * l.re + l.im * l.im;
        result[x][2] = r.re * r.re + r.im * r.im;
        result[x][1] = 0.5f * (result[x][0] + result[x][2]);

        if (s->gamma2 == 1.0f)
            result[x][3] = result[x][1];
        else if (s->gamma2 == 2.0f)
            result[x][3] = sqrtf(result[x][1]);
        else if (s->gamma2 == 3.0f)
            result[x][3] = cbrtf(result[x][1]);
        else if (s->gamma2 == 4.0f)
            result[x][3] = sqrtf(sqrtf(result[x][1]));
        else
            result[x][3] = expf(logf(result[x][1]) * (1.0f / s->gamma2));

        result[x][0] = FFMIN(1.0f, result[x][0]);
        result[x][1] = FFMIN(1.0f, result[x][1]);
        result[x][2] = FFMIN(1.0f, result[x][2]);
        if (s->gamma == 1.0f) {
            result[x][0] = 255.0f * result[x][0];
            result[x][1] = 255.0f * result[x][1];
            result[x][2] = 255.0f * result[x][2];
        } else if (s->gamma == 2.0f) {
            result[x][0] = 255.0f * sqrtf(result[x][0]);
            result[x][1] = 255.0f * sqrtf(result[x][1]);
            result[x][2] = 255.0f * sqrtf(result[x][2]);
        } else if (s->gamma == 3.0f) {
            result[x][0] = 255.0f * cbrtf(result[x][0]);
            result[x][1] = 255.0f * cbrtf(result[x][1]);
            result[x][2] = 255.0f * cbrtf(result[x][2]);
        } else if (s->gamma == 4.0f) {
            result[x][0] = 255.0f * sqrtf(sqrtf(result[x][0]));
            result[x][1] = 255.0f * sqrtf(sqrtf(result[x][1]));
            result[x][2] = 255.0f * sqrtf(sqrtf(result[x][2]));
        } else {
            result[x][0] = 255.0f * expf(logf(result[x][0]) * (1.0f / s->gamma));
            result[x][1] = 255.0f * expf(logf(result[x][1]) * (1.0f / s->gamma));
            result[x][2] = 255.0f * expf(logf(result[x][2]) * (1.0f / s->gamma));
        }
    }

    if (!s->fullhd) {
        for (x = 0; x < video_width; x++) {
            result[x][0] = 0.5f * (result[2*x][0] + result[2*x+1][0]);
            result[x][1] = 0.5f * (result[2*x][1] + result[2*x+1][1]);
            result[x][2] = 0.5f * (result[2*x][2] + result[2*x+1][2]);
            result[x][3] = 0.5f * (result[2*x][3] + result[2*x+1][3]);
        }
    }

    for (x = 0; x < video_width; x++) {
        s->spectogram[s->spectogram_index*linesize + 3*x] = result[x][0] + 0.5f;
        s->spectogram[s->spectogram_index*linesize + 3*x + 1] = result[x][1] + 0.5f;
        s->spectogram[s->spectogram_index*linesize + 3*x + 2] = result[x][2] + 0.5f;
    }

    /* drawing */
    if (!s->spectogram_count) {
        uint8_t *data = (uint8_t*) s->outpicref->data[0];
        float rcp_result[VIDEO_WIDTH];
        int total_length = linesize * spectogram_height;
        int back_length = linesize * s->spectogram_index;

        for (x = 0; x < video_width; x++)
            rcp_result[x] = 1.0f / (result[x][3]+0.0001f);

        /* drawing bar */
        for (y = 0; y < spectogram_height; y++) {
            float height = (spectogram_height - y) * (1.0f/spectogram_height);
            uint8_t *lineptr = data + y * linesize;
            for (x = 0; x < video_width; x++) {
                float mul;
                if (result[x][3] <= height) {
                    *lineptr++ = 0;
                    *lineptr++ = 0;
                    *lineptr++ = 0;
                } else {
                    mul = (result[x][3] - height) * rcp_result[x];
                    *lineptr++ = mul * result[x][0] + 0.5f;
                    *lineptr++ = mul * result[x][1] + 0.5f;
                    *lineptr++ = mul * result[x][2] + 0.5f;
                }
            }
        }

        /* drawing font */
        if (s->font_alpha && s->draw_text) {
            for (y = 0; y < font_height; y++) {
                uint8_t *lineptr = data + (spectogram_height + y) * linesize;
                uint8_t *spectogram_src = s->spectogram + s->spectogram_index * linesize;
                uint8_t *fontcolor_value = s->fontcolor_value;
                for (x = 0; x < video_width; x++) {
                    uint8_t alpha = s->font_alpha[y*video_width+x];
                    lineptr[3*x] = (spectogram_src[3*x] * (255-alpha) + fontcolor_value[0] * alpha + 255) >> 8;
                    lineptr[3*x+1] = (spectogram_src[3*x+1] * (255-alpha) + fontcolor_value[1] * alpha + 255) >> 8;
                    lineptr[3*x+2] = (spectogram_src[3*x+2] * (255-alpha) + fontcolor_value[2] * alpha + 255) >> 8;
                    fontcolor_value += 3;
                }
            }
        } else if (s->draw_text) {
            for (y = 0; y < font_height; y++) {
                uint8_t *lineptr = data + (spectogram_height + y) * linesize;
                memcpy(lineptr, s->spectogram + s->spectogram_index * linesize, video_width*3);
            }
            for (x = 0; x < video_width; x += video_width/10) {
                int u;
                static const char str[] = "EF G A BC D ";
                uint8_t *startptr = data + spectogram_height * linesize + x * 3;
                for (u = 0; str[u]; u++) {
                    int v;
                    for (v = 0; v < 16; v++) {
                        uint8_t *p = startptr + v * linesize * video_scale + 8 * 3 * u * video_scale;
                        int ux = x + 8 * u * video_scale;
                        int mask;
                        for (mask = 0x80; mask; mask >>= 1) {
                            if (mask & avpriv_vga16_font[str[u] * 16 + v]) {
                                p[0] = s->fontcolor_value[3*ux];
                                p[1] = s->fontcolor_value[3*ux+1];
                                p[2] = s->fontcolor_value[3*ux+2];
                                if (video_scale == 2) {
                                    p[linesize] = p[0];
                                    p[linesize+1] = p[1];
                                    p[linesize+2] = p[2];
                                    p[3] = p[linesize+3] = s->fontcolor_value[3*ux+3];
                                    p[4] = p[linesize+4] = s->fontcolor_value[3*ux+4];
                                    p[5] = p[linesize+5] = s->fontcolor_value[3*ux+5];
                                }
                            }
                            p  += 3 * video_scale;
                            ux += video_scale;
                        }
                    }
                }
            }
        } else {
            for (y = 0; y < font_height; y++) {
                uint8_t *lineptr = data + (spectogram_height + y) * linesize;
                uint8_t *spectogram_src = s->spectogram + s->spectogram_index * linesize;
                for (x = 0; x < video_width; x++) {
                    lineptr[3*x] = spectogram_src[3*x];
                    lineptr[3*x+1] = spectogram_src[3*x+1];
                    lineptr[3*x+2] = spectogram_src[3*x+2];
                }
            }
        }

        /* drawing spectogram/sonogram */
        data += spectogram_start * linesize;
        memcpy(data, s->spectogram + s->spectogram_index*linesize, total_length - back_length);

        data += total_length - back_length;
        if (back_length)
            memcpy(data, s->spectogram, back_length);

        s->outpicref->pts = s->frame_count;
        ret = ff_filter_frame(outlink, av_frame_clone(s->outpicref));
        s->req_fullfilled = 1;
        s->frame_count++;
    }
    s->spectogram_count = (s->spectogram_count + 1) % s->count;
    s->spectogram_index = (s->spectogram_index + spectogram_height - 1) % spectogram_height;
    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    ShowCQTContext *s = ctx->priv;
    int step = inlink->sample_rate / (s->fps * s->count);
    int fft_len = 1 << s->fft_bits;
    int remaining;
    float *audio_data;

    if (!insamples) {
        while (s->remaining_fill < (fft_len >> 1)) {
            int ret, x;
            memset(&s->fft_data[fft_len - s->remaining_fill], 0, sizeof(*s->fft_data) * s->remaining_fill);
            ret = plot_cqt(inlink);
            if (ret < 0)
                return ret;
            for (x = 0; x < (fft_len-step); x++)
                s->fft_data[x] = s->fft_data[x+step];
            s->remaining_fill += step;
        }
        return AVERROR_EOF;
    }

    remaining = insamples->nb_samples;
    audio_data = (float*) insamples->data[0];

    while (remaining) {
        if (remaining >= s->remaining_fill) {
            int i = insamples->nb_samples - remaining;
            int j = fft_len - s->remaining_fill;
            int m, ret;
            for (m = 0; m < s->remaining_fill; m++) {
                s->fft_data[j+m].re = audio_data[2*(i+m)];
                s->fft_data[j+m].im = audio_data[2*(i+m)+1];
            }
            ret = plot_cqt(inlink);
            if (ret < 0) {
                av_frame_free(&insamples);
                return ret;
            }
            remaining -= s->remaining_fill;
            for (m = 0; m < fft_len-step; m++)
                s->fft_data[m] = s->fft_data[m+step];
            s->remaining_fill = step;
        } else {
            int i = insamples->nb_samples - remaining;
            int j = fft_len - s->remaining_fill;
            int m;
            for (m = 0; m < remaining; m++) {
                s->fft_data[m+j].re = audio_data[2*(i+m)];
                s->fft_data[m+j].im = audio_data[2*(i+m)+1];
            }
            s->remaining_fill -= remaining;
            remaining = 0;
        }
    }
    av_frame_free(&insamples);
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    ShowCQTContext *s = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    s->req_fullfilled = 0;
    do {
        ret = ff_request_frame(inlink);
    } while (!s->req_fullfilled && ret >= 0);

    if (ret == AVERROR_EOF && s->outpicref)
        filter_frame(inlink, NULL);
    return ret;
}

static const AVFilterPad showcqt_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad showcqt_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_avf_showcqt = {
    .name          = "showcqt",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a CQT (Constant Q Transform) spectrum video output."),
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(ShowCQTContext),
    .inputs        = showcqt_inputs,
    .outputs       = showcqt_outputs,
    .priv_class    = &showcqt_class,
};
