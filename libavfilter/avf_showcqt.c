/*
 * Copyright (c) 2014-2015 Muhammad Faiz <mfcc64@gmail.com>
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
#include "libavutil/opt.h"
#include "libavutil/xga_font_data.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "avfilter.h"
#include "internal.h"
#include "lavfutils.h"
#include "lswsutils.h"

#if CONFIG_LIBFREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#if CONFIG_LIBFONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include "avf_showcqt.h"

#define BASEFREQ        20.01523126408007475
#define ENDFREQ         20495.59681441799654
#define TLENGTH         "384*tc/(384+tc*f)"
#define TLENGTH_MIN     0.001
#define VOLUME_MAX      100.0
#define FONTCOLOR       "st(0, (midi(f)-59.5)/12);" \
    "st(1, if(between(ld(0),0,1), 0.5-0.5*cos(2*PI*ld(0)), 0));" \
    "r(1-ld(1)) + b(ld(1))"
#define CSCHEME         "1|0.5|0|0|0.5|1"
#define PTS_STEP 10
#define PTS_TOLERANCE 1

#define OFFSET(x) offsetof(ShowCQTContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption showcqt_options[] = {
    { "size",         "set video size", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, { .str = "1920x1080" },      0, 0,        FLAGS },
    { "s",            "set video size", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, { .str = "1920x1080" },      0, 0,        FLAGS },
    { "fps",          "set video rate", OFFSET(rate),  AV_OPT_TYPE_VIDEO_RATE, { .str = "25" },             1, 1000,     FLAGS },
    { "rate",         "set video rate", OFFSET(rate),  AV_OPT_TYPE_VIDEO_RATE, { .str = "25" },             1, 1000,     FLAGS },
    { "r",            "set video rate", OFFSET(rate),  AV_OPT_TYPE_VIDEO_RATE, { .str = "25" },             1, 1000,     FLAGS },
    { "bar_h",   "set bargraph height", OFFSET(bar_h),        AV_OPT_TYPE_INT, { .i64 = -1 },              -1, INT_MAX,  FLAGS },
    { "axis_h",      "set axis height", OFFSET(axis_h),       AV_OPT_TYPE_INT, { .i64 = -1 },              -1, INT_MAX,  FLAGS },
    { "sono_h",  "set sonogram height", OFFSET(sono_h),       AV_OPT_TYPE_INT, { .i64 = -1 },              -1, INT_MAX,  FLAGS },
    { "fullhd",      "set fullhd size", OFFSET(fullhd),      AV_OPT_TYPE_BOOL, { .i64 = 1 },                0, 1,        FLAGS },
    { "sono_v",  "set sonogram volume", OFFSET(sono_v),    AV_OPT_TYPE_STRING, { .str = "16" },      CHAR_MIN, CHAR_MAX, FLAGS },
    { "volume",  "set sonogram volume", OFFSET(sono_v),    AV_OPT_TYPE_STRING, { .str = "16" },      CHAR_MIN, CHAR_MAX, FLAGS },
    { "bar_v",   "set bargraph volume", OFFSET(bar_v),     AV_OPT_TYPE_STRING, { .str = "sono_v" },  CHAR_MIN, CHAR_MAX, FLAGS },
    { "volume2", "set bargraph volume", OFFSET(bar_v),     AV_OPT_TYPE_STRING, { .str = "sono_v" },  CHAR_MIN, CHAR_MAX, FLAGS },
    { "sono_g",   "set sonogram gamma", OFFSET(sono_g),     AV_OPT_TYPE_FLOAT, { .dbl = 3.0 },            1.0, 7.0,      FLAGS },
    { "gamma",    "set sonogram gamma", OFFSET(sono_g),     AV_OPT_TYPE_FLOAT, { .dbl = 3.0 },            1.0, 7.0,      FLAGS },
    { "bar_g",    "set bargraph gamma", OFFSET(bar_g),      AV_OPT_TYPE_FLOAT, { .dbl = 1.0 },            1.0, 7.0,      FLAGS },
    { "gamma2",   "set bargraph gamma", OFFSET(bar_g),      AV_OPT_TYPE_FLOAT, { .dbl = 1.0 },            1.0, 7.0,      FLAGS },
    { "bar_t",  "set bar transparency", OFFSET(bar_t),      AV_OPT_TYPE_FLOAT, { .dbl = 1.0 },            0.0, 1.0,      FLAGS },
    { "timeclamp",     "set timeclamp", OFFSET(timeclamp), AV_OPT_TYPE_DOUBLE, { .dbl = 0.17 },         0.002, 1.0,      FLAGS },
    { "tc",            "set timeclamp", OFFSET(timeclamp), AV_OPT_TYPE_DOUBLE, { .dbl = 0.17 },         0.002, 1.0,      FLAGS },
    { "basefreq", "set base frequency", OFFSET(basefreq),  AV_OPT_TYPE_DOUBLE, { .dbl = BASEFREQ },      10.0, 100000.0, FLAGS },
    { "endfreq",   "set end frequency", OFFSET(endfreq),   AV_OPT_TYPE_DOUBLE, { .dbl = ENDFREQ },       10.0, 100000.0, FLAGS },
    { "coeffclamp",   "set coeffclamp", OFFSET(coeffclamp), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 },            0.1, 10.0,     FLAGS },
    { "tlength",         "set tlength", OFFSET(tlength),   AV_OPT_TYPE_STRING, { .str = TLENGTH },   CHAR_MIN, CHAR_MAX, FLAGS },
    { "count",   "set transform count", OFFSET(count),        AV_OPT_TYPE_INT, { .i64 = 6 },                1, 30,       FLAGS },
    { "fcount",  "set frequency count", OFFSET(fcount),       AV_OPT_TYPE_INT, { .i64 = 0 },                0, 10,       FLAGS },
    { "fontfile", "set axis font file", OFFSET(fontfile),  AV_OPT_TYPE_STRING, { .str = NULL },      CHAR_MIN, CHAR_MAX, FLAGS },
    { "font",          "set axis font", OFFSET(font),      AV_OPT_TYPE_STRING, { .str = NULL },      CHAR_MIN, CHAR_MAX, FLAGS },
    { "fontcolor",    "set font color", OFFSET(fontcolor), AV_OPT_TYPE_STRING, { .str = FONTCOLOR }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "axisfile",     "set axis image", OFFSET(axisfile),  AV_OPT_TYPE_STRING, { .str = NULL },      CHAR_MIN, CHAR_MAX, FLAGS },
    { "axis",              "draw axis", OFFSET(axis),        AV_OPT_TYPE_BOOL, { .i64 = 1 },                0, 1,        FLAGS },
    { "text",              "draw axis", OFFSET(axis),        AV_OPT_TYPE_BOOL, { .i64 = 1 },                0, 1,        FLAGS },
    { "csp",         "set color space", OFFSET(csp),          AV_OPT_TYPE_INT, { .i64 = AVCOL_SPC_UNSPECIFIED }, 0, INT_MAX, FLAGS, "csp" },
        { "unspecified", "unspecified", 0,                  AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_UNSPECIFIED }, 0, 0, FLAGS, "csp" },
        { "bt709",             "bt709", 0,                  AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT709 },       0, 0, FLAGS, "csp" },
        { "fcc",                 "fcc", 0,                  AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_FCC },         0, 0, FLAGS, "csp" },
        { "bt470bg",         "bt470bg", 0,                  AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT470BG },     0, 0, FLAGS, "csp" },
        { "smpte170m",     "smpte170m", 0,                  AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_SMPTE170M },   0, 0, FLAGS, "csp" },
        { "smpte240m",     "smpte240m", 0,                  AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_SMPTE240M },   0, 0, FLAGS, "csp" },
        { "bt2020ncl",     "bt2020ncl", 0,                  AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT2020_NCL },  0, 0, FLAGS, "csp" },
    { "cscheme",    "set color scheme", OFFSET(cscheme),   AV_OPT_TYPE_STRING, { .str = CSCHEME },   CHAR_MIN, CHAR_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showcqt);

static void common_uninit(ShowCQTContext *s)
{
    int k;
    int level = AV_LOG_DEBUG;
    int64_t plot_time;

    if (s->fft_time)
        av_log(s->ctx, level, "fft_time         = %16.3f s.\n", s->fft_time * 1e-6);
    if (s->cqt_time)
        av_log(s->ctx, level, "cqt_time         = %16.3f s.\n", s->cqt_time * 1e-6);
    if (s->process_cqt_time)
        av_log(s->ctx, level, "process_cqt_time = %16.3f s.\n", s->process_cqt_time * 1e-6);
    if (s->update_sono_time)
        av_log(s->ctx, level, "update_sono_time = %16.3f s.\n", s->update_sono_time * 1e-6);
    if (s->alloc_time)
        av_log(s->ctx, level, "alloc_time       = %16.3f s.\n", s->alloc_time * 1e-6);
    if (s->bar_time)
        av_log(s->ctx, level, "bar_time         = %16.3f s.\n", s->bar_time * 1e-6);
    if (s->axis_time)
        av_log(s->ctx, level, "axis_time        = %16.3f s.\n", s->axis_time * 1e-6);
    if (s->sono_time)
        av_log(s->ctx, level, "sono_time        = %16.3f s.\n", s->sono_time * 1e-6);

    plot_time = s->fft_time + s->cqt_time + s->process_cqt_time + s->update_sono_time
              + s->alloc_time + s->bar_time + s->axis_time + s->sono_time;
    if (plot_time)
        av_log(s->ctx, level, "plot_time        = %16.3f s.\n", plot_time * 1e-6);

    s->fft_time = s->cqt_time = s->process_cqt_time = s->update_sono_time
                = s->alloc_time = s->bar_time = s->axis_time = s->sono_time = 0;
    /* axis_frame may be non reference counted frame */
    if (s->axis_frame && !s->axis_frame->buf[0]) {
        av_freep(s->axis_frame->data);
        for (k = 0; k < 4; k++)
            s->axis_frame->data[k] = NULL;
    }

    av_frame_free(&s->axis_frame);
    av_frame_free(&s->sono_frame);
    av_fft_end(s->fft_ctx);
    s->fft_ctx = NULL;
    if (s->coeffs)
        for (k = 0; k < s->cqt_len; k++)
            av_freep(&s->coeffs[k].val);
    av_freep(&s->coeffs);
    av_freep(&s->fft_data);
    av_freep(&s->fft_result);
    av_freep(&s->cqt_result);
    av_freep(&s->c_buf);
    av_freep(&s->h_buf);
    av_freep(&s->rcp_h_buf);
    av_freep(&s->freq);
    av_freep(&s->sono_v_buf);
    av_freep(&s->bar_v_buf);
}

static double *create_freq_table(double base, double end, int n)
{
    double log_base, log_end;
    double rcp_n = 1.0 / n;
    double *freq;
    int x;

    freq = av_malloc_array(n, sizeof(*freq));
    if (!freq)
        return NULL;

    log_base = log(base);
    log_end  = log(end);
    for (x = 0; x < n; x++) {
        double log_freq = log_base + (x + 0.5) * (log_end - log_base) * rcp_n;
        freq[x] = exp(log_freq);
    }
    return freq;
}

static double clip_with_log(void *log_ctx, const char *name,
                            double val, double min, double max,
                            double nan_replace, int idx)
{
    int level = AV_LOG_WARNING;
    if (isnan(val)) {
        av_log(log_ctx, level, "[%d] %s is nan, setting it to %g.\n",
               idx, name, nan_replace);
        val = nan_replace;
    } else if (val < min) {
        av_log(log_ctx, level, "[%d] %s is too low (%g), setting it to %g.\n",
               idx, name, val, min);
        val = min;
    } else if (val > max) {
        av_log(log_ctx, level, "[%d] %s it too high (%g), setting it to %g.\n",
               idx, name, val, max);
        val = max;
    }
    return val;
}

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

static int init_volume(ShowCQTContext *s)
{
    const char *func_names[] = { "a_weighting", "b_weighting", "c_weighting", NULL };
    const char *sono_names[] = { "timeclamp", "tc", "frequency", "freq", "f", "bar_v", NULL };
    const char *bar_names[] = { "timeclamp", "tc", "frequency", "freq", "f", "sono_v", NULL };
    double (*funcs[])(void *, double) = { a_weighting, b_weighting, c_weighting };
    AVExpr *sono = NULL, *bar = NULL;
    int x, ret = AVERROR(ENOMEM);

    s->sono_v_buf = av_malloc_array(s->cqt_len, sizeof(*s->sono_v_buf));
    s->bar_v_buf = av_malloc_array(s->cqt_len, sizeof(*s->bar_v_buf));
    if (!s->sono_v_buf || !s->bar_v_buf)
        goto error;

    if ((ret = av_expr_parse(&sono, s->sono_v, sono_names, func_names, funcs, NULL, NULL, 0, s->ctx)) < 0)
        goto error;

    if ((ret = av_expr_parse(&bar, s->bar_v, bar_names, func_names, funcs, NULL, NULL, 0, s->ctx)) < 0)
        goto error;

    for (x = 0; x < s->cqt_len; x++) {
        double vars[] = { s->timeclamp, s->timeclamp, s->freq[x], s->freq[x], s->freq[x], 0.0 };
        double vol = clip_with_log(s->ctx, "sono_v", av_expr_eval(sono, vars, NULL), 0.0, VOLUME_MAX, 0.0, x);
        vars[5] = vol;
        vol = clip_with_log(s->ctx, "bar_v", av_expr_eval(bar, vars, NULL), 0.0, VOLUME_MAX, 0.0, x);
        s->bar_v_buf[x] = vol * vol;
        vars[5] = vol;
        vol = clip_with_log(s->ctx, "sono_v", av_expr_eval(sono, vars, NULL), 0.0, VOLUME_MAX, 0.0, x);
        s->sono_v_buf[x] = vol * vol;
    }
    av_expr_free(sono);
    av_expr_free(bar);
    return 0;

error:
    av_freep(&s->sono_v_buf);
    av_freep(&s->bar_v_buf);
    av_expr_free(sono);
    av_expr_free(bar);
    return ret;
}

static void cqt_calc(FFTComplex *dst, const FFTComplex *src, const Coeffs *coeffs,
                     int len, int fft_len)
{
    int k, x, i, j;
    for (k = 0; k < len; k++) {
        FFTComplex l, r, a = {0,0}, b = {0,0};

        for (x = 0; x < coeffs[k].len; x++) {
            FFTSample u = coeffs[k].val[x];
            i = coeffs[k].start + x;
            j = fft_len - i;
            a.re += u * src[i].re;
            a.im += u * src[i].im;
            b.re += u * src[j].re;
            b.im += u * src[j].im;
        }

        /* separate left and right, (and multiply by 2.0) */
        l.re = a.re + b.re;
        l.im = a.im - b.im;
        r.re = b.im + a.im;
        r.im = b.re - a.re;
        dst[k].re = l.re * l.re + l.im * l.im;
        dst[k].im = r.re * r.re + r.im * r.im;
    }
}

static int init_cqt(ShowCQTContext *s)
{
    const char *var_names[] = { "timeclamp", "tc", "frequency", "freq", "f", NULL };
    AVExpr *expr = NULL;
    int rate = s->ctx->inputs[0]->sample_rate;
    int nb_cqt_coeffs = 0;
    int k, x, ret;

    if ((ret = av_expr_parse(&expr, s->tlength, var_names, NULL, NULL, NULL, NULL, 0, s->ctx)) < 0)
        goto error;

    ret = AVERROR(ENOMEM);
    if (!(s->coeffs = av_calloc(s->cqt_len, sizeof(*s->coeffs))))
        goto error;

    for (k = 0; k < s->cqt_len; k++) {
        double vars[] = { s->timeclamp, s->timeclamp, s->freq[k], s->freq[k], s->freq[k] };
        double flen, center, tlength;
        int start, end, m = k;

        if (s->freq[k] > 0.5 * rate)
            continue;
        tlength = clip_with_log(s->ctx, "tlength", av_expr_eval(expr, vars, NULL),
                                TLENGTH_MIN, s->timeclamp, s->timeclamp, k);

        flen = 8.0 * s->fft_len / (tlength * rate);
        center = s->freq[k] * s->fft_len / rate;
        start = FFMAX(0, ceil(center - 0.5 * flen));
        end = FFMIN(s->fft_len, floor(center + 0.5 * flen));

        s->coeffs[m].start = start & ~(s->cqt_align - 1);
        s->coeffs[m].len = (end | (s->cqt_align - 1)) + 1 - s->coeffs[m].start;
        nb_cqt_coeffs += s->coeffs[m].len;
        if (!(s->coeffs[m].val = av_calloc(s->coeffs[m].len, sizeof(*s->coeffs[m].val))))
            goto error;

        for (x = start; x <= end; x++) {
            int sign = (x & 1) ? (-1) : 1;
            double y = 2.0 * M_PI * (x - center) * (1.0 / flen);
            /* nuttall window */
            double w = 0.355768 + 0.487396 * cos(y) + 0.144232 * cos(2*y) + 0.012604 * cos(3*y);
            w *= sign * (1.0 / s->fft_len);
            s->coeffs[m].val[x - s->coeffs[m].start] = w;
        }

        if (s->permute_coeffs)
            s->permute_coeffs(s->coeffs[m].val, s->coeffs[m].len);
    }

    av_expr_free(expr);
    av_log(s->ctx, AV_LOG_INFO, "nb_cqt_coeffs = %d.\n", nb_cqt_coeffs);
    return 0;

error:
    av_expr_free(expr);
    if (s->coeffs)
        for (k = 0; k < s->cqt_len; k++)
            av_freep(&s->coeffs[k].val);
    av_freep(&s->coeffs);
    return ret;
}

static AVFrame *alloc_frame_empty(enum AVPixelFormat format, int w, int h)
{
    AVFrame *out;
    out = av_frame_alloc();
    if (!out)
        return NULL;
    out->format = format;
    out->width = w;
    out->height = h;
    if (av_frame_get_buffer(out, 32) < 0) {
        av_frame_free(&out);
        return NULL;
    }
    if (format == AV_PIX_FMT_RGB24 || format == AV_PIX_FMT_RGBA) {
        memset(out->data[0], 0, out->linesize[0] * h);
    } else {
        int hh = (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVA420P) ? h / 2 : h;
        memset(out->data[0], 16, out->linesize[0] * h);
        memset(out->data[1], 128, out->linesize[1] * hh);
        memset(out->data[2], 128, out->linesize[2] * hh);
        if (out->data[3])
            memset(out->data[3], 0, out->linesize[3] * h);
    }
    return out;
}

static enum AVPixelFormat convert_axis_pixel_format(enum AVPixelFormat format)
{
    switch (format) {
        case AV_PIX_FMT_RGB24:   format = AV_PIX_FMT_RGBA; break;
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUV420P: format = AV_PIX_FMT_YUVA444P; break;
    }
    return format;
}

static int init_axis_empty(ShowCQTContext *s)
{
    if (!(s->axis_frame = alloc_frame_empty(convert_axis_pixel_format(s->format), s->width, s->axis_h)))
        return AVERROR(ENOMEM);
    return 0;
}

static int init_axis_from_file(ShowCQTContext *s)
{
    uint8_t *tmp_data[4] = { NULL };
    int tmp_linesize[4];
    enum AVPixelFormat tmp_format;
    int tmp_w, tmp_h, ret;

    if ((ret = ff_load_image(tmp_data, tmp_linesize, &tmp_w, &tmp_h, &tmp_format,
                             s->axisfile, s->ctx)) < 0)
        goto error;

    ret = AVERROR(ENOMEM);
    if (!(s->axis_frame = av_frame_alloc()))
        goto error;

    if ((ret = ff_scale_image(s->axis_frame->data, s->axis_frame->linesize, s->width, s->axis_h,
                              convert_axis_pixel_format(s->format), tmp_data, tmp_linesize, tmp_w, tmp_h,
                              tmp_format, s->ctx)) < 0)
        goto error;

    s->axis_frame->width = s->width;
    s->axis_frame->height = s->axis_h;
    s->axis_frame->format = convert_axis_pixel_format(s->format);
    av_freep(tmp_data);
    return 0;

error:
    av_frame_free(&s->axis_frame);
    av_freep(tmp_data);
    return ret;
}

static double midi(void *p, double f)
{
    return log2(f/440.0) * 12.0 + 69.0;
}

static double r_func(void *p, double x)
{
    x = av_clipd(x, 0.0, 1.0);
    return lrint(x*255.0) << 16;
}

static double g_func(void *p, double x)
{
    x = av_clipd(x, 0.0, 1.0);
    return lrint(x*255.0) << 8;
}

static double b_func(void *p, double x)
{
    x = av_clipd(x, 0.0, 1.0);
    return lrint(x*255.0);
}

static int init_axis_color(ShowCQTContext *s, AVFrame *tmp, int half)
{
    const char *var_names[] = { "timeclamp", "tc", "frequency", "freq", "f", NULL };
    const char *func_names[] = { "midi", "r", "g", "b", NULL };
    double (*funcs[])(void *, double) = { midi, r_func, g_func, b_func };
    AVExpr *expr = NULL;
    double *freq = NULL;
    int x, xs, y, ret;
    int width = half ? 1920/2 : 1920, height = half ? 16 : 32;
    int step = half ? 2 : 1;

    if (s->basefreq != (double) BASEFREQ || s->endfreq != (double) ENDFREQ) {
        av_log(s->ctx, AV_LOG_WARNING, "font axis rendering is not implemented in non-default frequency range,"
               " please use axisfile option instead.\n");
        return AVERROR(EINVAL);
    }

    if (s->cqt_len == 1920)
        freq = s->freq;
    else if (!(freq = create_freq_table(s->basefreq, s->endfreq, 1920)))
        return AVERROR(ENOMEM);

    if ((ret = av_expr_parse(&expr, s->fontcolor, var_names, func_names, funcs, NULL, NULL, 0, s->ctx)) < 0) {
        if (freq != s->freq)
            av_freep(&freq);
        return ret;
    }

    for (x = 0, xs = 0; x < width; x++, xs += step) {
        double vars[] = { s->timeclamp, s->timeclamp, freq[xs], freq[xs], freq[xs] };
        int color = (int) av_expr_eval(expr, vars, NULL);
        uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
        uint8_t *data = tmp->data[0];
        int linesize = tmp->linesize[0];
        for (y = 0; y < height; y++) {
            data[linesize * y + 4 * x] = r;
            data[linesize * y + 4 * x + 1] = g;
            data[linesize * y + 4 * x + 2] = b;
        }
    }

    av_expr_free(expr);
    if (freq != s->freq)
        av_freep(&freq);
    return 0;
}

static int render_freetype(ShowCQTContext *s, AVFrame *tmp, char *fontfile)
{
#if CONFIG_LIBFREETYPE
    const char *str = "EF G A BC D ";
    uint8_t *data = tmp->data[0];
    int linesize = tmp->linesize[0];
    FT_Library lib = NULL;
    FT_Face face = NULL;
    int font_width = 16, font_height = 32;
    int font_repeat = font_width * 12;
    int linear_hori_advance = font_width * 65536;
    int non_monospace_warning = 0;
    int x;

    if (!fontfile)
        return AVERROR(EINVAL);

    if (FT_Init_FreeType(&lib))
        goto fail;

    if (FT_New_Face(lib, fontfile, 0, &face))
        goto fail;

    if (FT_Set_Char_Size(face, 16*64, 0, 0, 0))
        goto fail;

    if (FT_Load_Char(face, 'A', FT_LOAD_RENDER))
        goto fail;

    if (FT_Set_Char_Size(face, 16*64 * linear_hori_advance / face->glyph->linearHoriAdvance, 0, 0, 0))
        goto fail;

    for (x = 0; x < 12; x++) {
        int sx, sy, rx, bx, by, dx, dy;

        if (str[x] == ' ')
            continue;

        if (FT_Load_Char(face, str[x], FT_LOAD_RENDER))
            goto fail;

        if (face->glyph->advance.x != font_width*64 && !non_monospace_warning) {
            av_log(s->ctx, AV_LOG_WARNING, "font is not monospace.\n");
            non_monospace_warning = 1;
        }

        sy = font_height - 8 - face->glyph->bitmap_top;
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
                    if (dx >= 1920)
                        break;
                    data[dy*linesize+4*dx+3] = face->glyph->bitmap.buffer[by*face->glyph->bitmap.width+bx];
                }
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return 0;

fail:
    av_log(s->ctx, AV_LOG_WARNING, "error while loading freetype font.\n");
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return AVERROR(EINVAL);
#else
    if (fontfile)
        av_log(s->ctx, AV_LOG_WARNING, "freetype is not available, ignoring fontfile option.\n");
    return AVERROR(EINVAL);
#endif
}

static int render_fontconfig(ShowCQTContext *s, AVFrame *tmp, char* font)
{
#if CONFIG_LIBFONTCONFIG
    FcConfig *fontconfig;
    FcPattern *pat, *best;
    FcResult result = FcResultMatch;
    char *filename;
    int i, ret;

    if (!font)
        return AVERROR(EINVAL);

    for (i = 0; font[i]; i++) {
        if (font[i] == '|')
            font[i] = ':';
    }

    if (!(fontconfig = FcInitLoadConfigAndFonts())) {
        av_log(s->ctx, AV_LOG_ERROR, "impossible to init fontconfig.\n");
        return AVERROR_UNKNOWN;
    }

    if (!(pat = FcNameParse((uint8_t *)font))) {
        av_log(s->ctx, AV_LOG_ERROR, "could not parse fontconfig pat.\n");
        FcConfigDestroy(fontconfig);
        return AVERROR(EINVAL);
    }

    FcDefaultSubstitute(pat);

    if (!FcConfigSubstitute(fontconfig, pat, FcMatchPattern)) {
        av_log(s->ctx, AV_LOG_ERROR, "could not substitue fontconfig options.\n");
        FcPatternDestroy(pat);
        FcConfigDestroy(fontconfig);
        return AVERROR(ENOMEM);
    }

    best = FcFontMatch(fontconfig, pat, &result);
    FcPatternDestroy(pat);

    ret = AVERROR(EINVAL);
    if (!best || result != FcResultMatch) {
        av_log(s->ctx, AV_LOG_ERROR, "cannot find a valid font for %s.\n", font);
        goto fail;
    }

    if (FcPatternGetString(best, FC_FILE, 0, (FcChar8 **)&filename) != FcResultMatch) {
        av_log(s->ctx, AV_LOG_ERROR, "no file path for %s\n", font);
        goto fail;
    }

    ret = render_freetype(s, tmp, filename);

fail:
    FcPatternDestroy(best);
    FcConfigDestroy(fontconfig);
    return ret;
#else
    if (font)
        av_log(s->ctx, AV_LOG_WARNING, "fontconfig is not available, ignoring font option.\n");
    return AVERROR(EINVAL);
#endif
}

static int render_default_font(AVFrame *tmp)
{
    const char *str = "EF G A BC D ";
    int x, u, v, mask;
    uint8_t *data = tmp->data[0];
    int linesize = tmp->linesize[0];
    int width = 1920/2, height = 16;

    for (x = 0; x < width; x += width/10) {
        uint8_t *startptr = data + 4 * x;
        for (u = 0; u < 12; u++) {
            for (v = 0; v < height; v++) {
                uint8_t *p = startptr + v * linesize + height/2 * 4 * u;
                for (mask = 0x80; mask; mask >>= 1, p += 4) {
                    if (mask & avpriv_vga16_font[str[u] * 16 + v])
                        p[3] = 255;
                    else
                        p[3] = 0;
                }
            }
        }
    }

    return 0;
}

static int init_axis_from_font(ShowCQTContext *s)
{
    AVFrame *tmp = NULL;
    int ret = AVERROR(ENOMEM);
    int width = 1920, height = 32;
    int default_font = 0;

    if (!(tmp = alloc_frame_empty(AV_PIX_FMT_RGBA, width, height)))
        goto fail;

    if (!(s->axis_frame = av_frame_alloc()))
        goto fail;

    if (render_freetype(s, tmp, s->fontfile) < 0 &&
        render_fontconfig(s, tmp, s->font) < 0 &&
        (default_font = 1, ret = render_default_font(tmp)) < 0)
        goto fail;

    if (default_font)
        width /= 2, height /= 2;

    if ((ret = init_axis_color(s, tmp, default_font)) < 0)
        goto fail;

    if ((ret = ff_scale_image(s->axis_frame->data, s->axis_frame->linesize, s->width, s->axis_h,
                              convert_axis_pixel_format(s->format), tmp->data, tmp->linesize,
                              width, height, AV_PIX_FMT_RGBA, s->ctx)) < 0)
        goto fail;

    av_frame_free(&tmp);
    s->axis_frame->width = s->width;
    s->axis_frame->height = s->axis_h;
    s->axis_frame->format = convert_axis_pixel_format(s->format);
    return 0;

fail:
    av_frame_free(&tmp);
    av_frame_free(&s->axis_frame);
    return ret;
}

static float calculate_gamma(float v, float g)
{
    if (g == 1.0f)
        return v;
    if (g == 2.0f)
        return sqrtf(v);
    if (g == 3.0f)
        return cbrtf(v);
    if (g == 4.0f)
        return sqrtf(sqrtf(v));
    return expf(logf(v) / g);
}

static void rgb_from_cqt(ColorFloat *c, const FFTComplex *v, float g, int len, float cscheme[6])
{
    int x;
    for (x = 0; x < len; x++) {
        c[x].rgb.r = 255.0f * calculate_gamma(FFMIN(1.0f, cscheme[0] * v[x].re + cscheme[3] * v[x].im), g);
        c[x].rgb.g = 255.0f * calculate_gamma(FFMIN(1.0f, cscheme[1] * v[x].re + cscheme[4] * v[x].im), g);
        c[x].rgb.b = 255.0f * calculate_gamma(FFMIN(1.0f, cscheme[2] * v[x].re + cscheme[5] * v[x].im), g);
    }
}

static void yuv_from_cqt(ColorFloat *c, const FFTComplex *v, float gamma, int len, float cm[3][3], float cscheme[6])
{
    int x;
    for (x = 0; x < len; x++) {
        float r, g, b;
        r = calculate_gamma(FFMIN(1.0f, cscheme[0] * v[x].re + cscheme[3] * v[x].im), gamma);
        g = calculate_gamma(FFMIN(1.0f, cscheme[1] * v[x].re + cscheme[4] * v[x].im), gamma);
        b = calculate_gamma(FFMIN(1.0f, cscheme[2] * v[x].re + cscheme[5] * v[x].im), gamma);
        c[x].yuv.y = cm[0][0] * r + cm[0][1] * g + cm[0][2] * b;
        c[x].yuv.u = cm[1][0] * r + cm[1][1] * g + cm[1][2] * b;
        c[x].yuv.v = cm[2][0] * r + cm[2][1] * g + cm[2][2] * b;
    }
}

static void draw_bar_rgb(AVFrame *out, const float *h, const float *rcp_h,
                         const ColorFloat *c, int bar_h, float bar_t)
{
    int x, y, w = out->width;
    float mul, ht, rcp_bar_h = 1.0f / bar_h, rcp_bar_t = 1.0f / bar_t;
    uint8_t *v = out->data[0], *lp;
    int ls = out->linesize[0];

    for (y = 0; y < bar_h; y++) {
        ht = (bar_h - y) * rcp_bar_h;
        lp = v + y * ls;
        for (x = 0; x < w; x++) {
            if (h[x] <= ht) {
                *lp++ = 0;
                *lp++ = 0;
                *lp++ = 0;
            } else {
                mul = (h[x] - ht) * rcp_h[x];
                mul = (mul < bar_t) ? (mul * rcp_bar_t) : 1.0f;
                *lp++ = lrintf(mul * c[x].rgb.r);
                *lp++ = lrintf(mul * c[x].rgb.g);
                *lp++ = lrintf(mul * c[x].rgb.b);
            }
        }
    }
}

#define DRAW_BAR_WITH_CHROMA(x) \
do { \
    if (h[x] <= ht) { \
        *lpy++ = 16; \
        *lpu++ = 128; \
        *lpv++ = 128; \
    } else { \
        mul = (h[x] - ht) * rcp_h[x]; \
        mul = (mul < bar_t) ? (mul * rcp_bar_t) : 1.0f; \
        *lpy++ = lrintf(mul * c[x].yuv.y + 16.0f); \
        *lpu++ = lrintf(mul * c[x].yuv.u + 128.0f); \
        *lpv++ = lrintf(mul * c[x].yuv.v + 128.0f); \
    } \
} while (0)

#define DRAW_BAR_WITHOUT_CHROMA(x) \
do { \
    if (h[x] <= ht) { \
        *lpy++ = 16; \
    } else { \
        mul = (h[x] - ht) * rcp_h[x]; \
        mul = (mul < bar_t) ? (mul * rcp_bar_t) : 1.0f; \
        *lpy++ = lrintf(mul * c[x].yuv.y + 16.0f); \
    } \
} while (0)

static void draw_bar_yuv(AVFrame *out, const float *h, const float *rcp_h,
                         const ColorFloat *c, int bar_h, float bar_t)
{
    int x, y, yh, w = out->width;
    float mul, ht, rcp_bar_h = 1.0f / bar_h, rcp_bar_t = 1.0f / bar_t;
    uint8_t *vy = out->data[0], *vu = out->data[1], *vv = out->data[2];
    uint8_t *lpy, *lpu, *lpv;
    int lsy = out->linesize[0], lsu = out->linesize[1], lsv = out->linesize[2];
    int fmt = out->format;

    for (y = 0; y < bar_h; y += 2) {
        yh = (fmt == AV_PIX_FMT_YUV420P) ? y / 2 : y;
        ht = (bar_h - y) * rcp_bar_h;
        lpy = vy + y * lsy;
        lpu = vu + yh * lsu;
        lpv = vv + yh * lsv;
        if (fmt == AV_PIX_FMT_YUV444P) {
            for (x = 0; x < w; x += 2) {
                DRAW_BAR_WITH_CHROMA(x);
                DRAW_BAR_WITH_CHROMA(x+1);
            }
        } else {
            for (x = 0; x < w; x += 2) {
                DRAW_BAR_WITH_CHROMA(x);
                DRAW_BAR_WITHOUT_CHROMA(x+1);
            }
        }

        ht = (bar_h - (y+1)) * rcp_bar_h;
        lpy = vy + (y+1) * lsy;
        lpu = vu + (y+1) * lsu;
        lpv = vv + (y+1) * lsv;
        if (fmt == AV_PIX_FMT_YUV444P) {
            for (x = 0; x < w; x += 2) {
                DRAW_BAR_WITH_CHROMA(x);
                DRAW_BAR_WITH_CHROMA(x+1);
            }
        } else if (fmt == AV_PIX_FMT_YUV422P) {
            for (x = 0; x < w; x += 2) {
                DRAW_BAR_WITH_CHROMA(x);
                DRAW_BAR_WITHOUT_CHROMA(x+1);
            }
        } else {
            for (x = 0; x < w; x += 2) {
                DRAW_BAR_WITHOUT_CHROMA(x);
                DRAW_BAR_WITHOUT_CHROMA(x+1);
            }
        }
    }
}

static void draw_axis_rgb(AVFrame *out, AVFrame *axis, const ColorFloat *c, int off)
{
    int x, y, w = axis->width, h = axis->height;
    float a, rcp_255 = 1.0f / 255.0f;
    uint8_t *lp, *lpa;

    for (y = 0; y < h; y++) {
        lp = out->data[0] + (off + y) * out->linesize[0];
        lpa = axis->data[0] + y * axis->linesize[0];
        for (x = 0; x < w; x++) {
            if (!lpa[3]) {
                *lp++ = lrintf(c[x].rgb.r);
                *lp++ = lrintf(c[x].rgb.g);
                *lp++ = lrintf(c[x].rgb.b);
            } else if (lpa[3] == 255) {
                *lp++ = lpa[0];
                *lp++ = lpa[1];
                *lp++ = lpa[2];
            } else {
                a = rcp_255 * lpa[3];
                *lp++ = lrintf(a * lpa[0] + (1.0f - a) * c[x].rgb.r);
                *lp++ = lrintf(a * lpa[1] + (1.0f - a) * c[x].rgb.g);
                *lp++ = lrintf(a * lpa[2] + (1.0f - a) * c[x].rgb.b);
            }
            lpa += 4;
        }
    }
}

#define BLEND_WITH_CHROMA(c) \
do { \
    if (!*lpaa) { \
        *lpy = lrintf(c.yuv.y + 16.0f); \
        *lpu = lrintf(c.yuv.u + 128.0f); \
        *lpv = lrintf(c.yuv.v + 128.0f); \
    } else if (255 == *lpaa) { \
        *lpy = *lpay; \
        *lpu = *lpau; \
        *lpv = *lpav; \
    } else { \
        float a = (1.0f/255.0f) * (*lpaa); \
        *lpy = lrintf(a * (*lpay) + (1.0f - a) * (c.yuv.y + 16.0f)); \
        *lpu = lrintf(a * (*lpau) + (1.0f - a) * (c.yuv.u + 128.0f)); \
        *lpv = lrintf(a * (*lpav) + (1.0f - a) * (c.yuv.v + 128.0f)); \
    } \
    lpy++; lpu++; lpv++; \
    lpay++; lpau++; lpav++; lpaa++; \
} while (0)

#define BLEND_WITHOUT_CHROMA(c, alpha_inc) \
do { \
    if (!*lpaa) { \
        *lpy = lrintf(c.yuv.y + 16.0f); \
    } else if (255 == *lpaa) { \
        *lpy = *lpay; \
    } else { \
        float a = (1.0f/255.0f) * (*lpaa); \
        *lpy = lrintf(a * (*lpay) + (1.0f - a) * (c.yuv.y + 16.0f)); \
    } \
    lpy++; \
    lpay++; lpaa += alpha_inc; \
} while (0)

#define BLEND_CHROMA2(c) \
do { \
    if (!lpaa[0] && !lpaa[1]) { \
        *lpu = lrintf(c.yuv.u + 128.0f); \
        *lpv = lrintf(c.yuv.v + 128.0f); \
    } else if (255 == lpaa[0] && 255 == lpaa[1]) { \
        *lpu = *lpau; *lpv = *lpav; \
    } else { \
        float a0 = (0.5f/255.0f) * lpaa[0]; \
        float a1 = (0.5f/255.0f) * lpaa[1]; \
        float b = 1.0f - a0 - a1; \
        *lpu = lrintf(a0 * lpau[0] + a1 * lpau[1] + b * (c.yuv.u + 128.0f)); \
        *lpv = lrintf(a0 * lpav[0] + a1 * lpav[1] + b * (c.yuv.v + 128.0f)); \
    } \
    lpau += 2; lpav += 2; lpaa++; lpu++; lpv++; \
} while (0)

#define BLEND_CHROMA2x2(c) \
do { \
    if (!lpaa[0] && !lpaa[1] && !lpaa[lsaa] && !lpaa[lsaa+1]) { \
        *lpu = lrintf(c.yuv.u + 128.0f); \
        *lpv = lrintf(c.yuv.v + 128.0f); \
    } else if (255 == lpaa[0] && 255 == lpaa[1] && \
               255 == lpaa[lsaa] && 255 == lpaa[lsaa+1]) { \
        *lpu = *lpau; *lpv = *lpav; \
    } else { \
        float a0 = (0.25f/255.0f) * lpaa[0]; \
        float a1 = (0.25f/255.0f) * lpaa[1]; \
        float a2 = (0.25f/255.0f) * lpaa[lsaa]; \
        float a3 = (0.25f/255.0f) * lpaa[lsaa+1]; \
        float b = 1.0f - a0 - a1 - a2 - a3; \
        *lpu = lrintf(a0 * lpau[0] + a1 * lpau[1] + a2 * lpau[lsau] + a3 * lpau[lsau+1] \
                    + b * (c.yuv.u + 128.0f)); \
        *lpv = lrintf(a0 * lpav[0] + a1 * lpav[1] + a2 * lpav[lsav] + a3 * lpav[lsav+1] \
                    + b * (c.yuv.v + 128.0f)); \
    } \
    lpau += 2; lpav += 2; lpaa++; lpu++; lpv++; \
} while (0)

static void draw_axis_yuv(AVFrame *out, AVFrame *axis, const ColorFloat *c, int off)
{
    int fmt = out->format, x, y, yh, w = axis->width, h = axis->height;
    int offh = (fmt == AV_PIX_FMT_YUV420P) ? off / 2 : off;
    uint8_t *vy = out->data[0], *vu = out->data[1], *vv = out->data[2];
    uint8_t *vay = axis->data[0], *vau = axis->data[1], *vav = axis->data[2], *vaa = axis->data[3];
    int lsy = out->linesize[0], lsu = out->linesize[1], lsv = out->linesize[2];
    int lsay = axis->linesize[0], lsau = axis->linesize[1], lsav = axis->linesize[2], lsaa = axis->linesize[3];
    uint8_t *lpy, *lpu, *lpv, *lpay, *lpau, *lpav, *lpaa;

    for (y = 0; y < h; y += 2) {
        yh = (fmt == AV_PIX_FMT_YUV420P) ? y / 2 : y;
        lpy = vy + (off + y) * lsy;
        lpu = vu + (offh + yh) * lsu;
        lpv = vv + (offh + yh) * lsv;
        lpay = vay + y * lsay;
        lpau = vau + y * lsau;
        lpav = vav + y * lsav;
        lpaa = vaa + y * lsaa;
        if (fmt == AV_PIX_FMT_YUV444P) {
            for (x = 0; x < w; x += 2) {
                BLEND_WITH_CHROMA(c[x]);
                BLEND_WITH_CHROMA(c[x+1]);
            }
        } else if (fmt == AV_PIX_FMT_YUV422P) {
            for (x = 0; x < w; x += 2) {
                BLEND_WITHOUT_CHROMA(c[x], 0);
                BLEND_CHROMA2(c[x]);
                BLEND_WITHOUT_CHROMA(c[x+1], 1);
            }
        } else {
            for (x = 0; x < w; x += 2) {
                BLEND_WITHOUT_CHROMA(c[x], 0);
                BLEND_CHROMA2x2(c[x]);
                BLEND_WITHOUT_CHROMA(c[x+1], 1);
            }
        }

        lpy = vy + (off + y + 1) * lsy;
        lpu = vu + (off + y + 1) * lsu;
        lpv = vv + (off + y + 1) * lsv;
        lpay = vay + (y + 1) * lsay;
        lpau = vau + (y + 1) * lsau;
        lpav = vav + (y + 1) * lsav;
        lpaa = vaa + (y + 1) * lsaa;
        if (fmt == AV_PIX_FMT_YUV444P) {
            for (x = 0; x < w; x += 2) {
                BLEND_WITH_CHROMA(c[x]);
                BLEND_WITH_CHROMA(c[x+1]);
            }
        } else if (fmt == AV_PIX_FMT_YUV422P) {
            for (x = 0; x < w; x += 2) {
                BLEND_WITHOUT_CHROMA(c[x], 0);
                BLEND_CHROMA2(c[x]);
                BLEND_WITHOUT_CHROMA(c[x+1], 1);
            }
        } else {
            for (x = 0; x < w; x += 2) {
                BLEND_WITHOUT_CHROMA(c[x], 1);
                BLEND_WITHOUT_CHROMA(c[x+1], 1);
            }
        }
    }
}

static void draw_sono(AVFrame *out, AVFrame *sono, int off, int idx)
{
    int fmt = out->format, h = sono->height;
    int nb_planes = (fmt == AV_PIX_FMT_RGB24) ? 1 : 3;
    int offh = (fmt == AV_PIX_FMT_YUV420P) ? off / 2 : off;
    int inc = (fmt == AV_PIX_FMT_YUV420P) ? 2 : 1;
    int ls, i, y, yh;

    ls = FFMIN(out->linesize[0], sono->linesize[0]);
    for (y = 0; y < h; y++) {
        memcpy(out->data[0] + (off + y) * out->linesize[0],
               sono->data[0] + (idx + y) % h * sono->linesize[0], ls);
    }

    for (i = 1; i < nb_planes; i++) {
        ls = FFMIN(out->linesize[i], sono->linesize[i]);
        for (y = 0; y < h; y += inc) {
            yh = (fmt == AV_PIX_FMT_YUV420P) ? y / 2 : y;
            memcpy(out->data[i] + (offh + yh) * out->linesize[i],
                   sono->data[i] + (idx + y) % h * sono->linesize[i], ls);
        }
    }
}

static void update_sono_rgb(AVFrame *sono, const ColorFloat *c, int idx)
{
    int x, w = sono->width;
    uint8_t *lp = sono->data[0] + idx * sono->linesize[0];

    for (x = 0; x < w; x++) {
        *lp++ = lrintf(c[x].rgb.r);
        *lp++ = lrintf(c[x].rgb.g);
        *lp++ = lrintf(c[x].rgb.b);
    }
}

static void update_sono_yuv(AVFrame *sono, const ColorFloat *c, int idx)
{
    int x, fmt = sono->format, w = sono->width;
    uint8_t *lpy = sono->data[0] + idx * sono->linesize[0];
    uint8_t *lpu = sono->data[1] + idx * sono->linesize[1];
    uint8_t *lpv = sono->data[2] + idx * sono->linesize[2];

    for (x = 0; x < w; x += 2) {
        *lpy++ = lrintf(c[x].yuv.y + 16.0f);
        *lpu++ = lrintf(c[x].yuv.u + 128.0f);
        *lpv++ = lrintf(c[x].yuv.v + 128.0f);
        *lpy++ = lrintf(c[x+1].yuv.y + 16.0f);
        if (fmt == AV_PIX_FMT_YUV444P) {
            *lpu++ = lrintf(c[x+1].yuv.u + 128.0f);
            *lpv++ = lrintf(c[x+1].yuv.v + 128.0f);
        }
    }
}

static void process_cqt(ShowCQTContext *s)
{
    int x, i;
    if (!s->sono_count) {
        for (x = 0; x < s->cqt_len; x++) {
            s->h_buf[x] = s->bar_v_buf[x] * 0.5f * (s->cqt_result[x].re + s->cqt_result[x].im);
        }
        if (s->fcount > 1) {
            float rcp_fcount = 1.0f / s->fcount;
            for (x = 0; x < s->width; x++) {
                float h = 0.0f;
                for (i = 0; i < s->fcount; i++)
                    h += s->h_buf[s->fcount * x + i];
                s->h_buf[x] = rcp_fcount * h;
            }
        }
        for (x = 0; x < s->width; x++) {
            s->h_buf[x] = calculate_gamma(s->h_buf[x], s->bar_g);
            s->rcp_h_buf[x] = 1.0f / (s->h_buf[x] + 0.0001f);
        }
    }

    for (x = 0; x < s->cqt_len; x++) {
        s->cqt_result[x].re *= s->sono_v_buf[x];
        s->cqt_result[x].im *= s->sono_v_buf[x];
    }

    if (s->fcount > 1) {
        float rcp_fcount = 1.0f / s->fcount;
        for (x = 0; x < s->width; x++) {
            FFTComplex result = {0.0f, 0.0f};
            for (i = 0; i < s->fcount; i++) {
                result.re += s->cqt_result[s->fcount * x + i].re;
                result.im += s->cqt_result[s->fcount * x + i].im;
            }
            s->cqt_result[x].re = rcp_fcount * result.re;
            s->cqt_result[x].im = rcp_fcount * result.im;
        }
    }

    if (s->format == AV_PIX_FMT_RGB24)
        rgb_from_cqt(s->c_buf, s->cqt_result, s->sono_g, s->width, s->cscheme_v);
    else
        yuv_from_cqt(s->c_buf, s->cqt_result, s->sono_g, s->width, s->cmatrix, s->cscheme_v);
}

static int plot_cqt(AVFilterContext *ctx, AVFrame **frameout)
{
    AVFilterLink *outlink = ctx->outputs[0];
    ShowCQTContext *s = ctx->priv;
    int64_t last_time, cur_time;

#define UPDATE_TIME(t) \
    cur_time = av_gettime(); \
    t += cur_time - last_time; \
    last_time = cur_time

    last_time = av_gettime();

    memcpy(s->fft_result, s->fft_data, s->fft_len * sizeof(*s->fft_data));
    av_fft_permute(s->fft_ctx, s->fft_result);
    av_fft_calc(s->fft_ctx, s->fft_result);
    s->fft_result[s->fft_len] = s->fft_result[0];
    UPDATE_TIME(s->fft_time);

    s->cqt_calc(s->cqt_result, s->fft_result, s->coeffs, s->cqt_len, s->fft_len);
    UPDATE_TIME(s->cqt_time);

    process_cqt(s);
    UPDATE_TIME(s->process_cqt_time);

    if (s->sono_h) {
        s->update_sono(s->sono_frame, s->c_buf, s->sono_idx);
        UPDATE_TIME(s->update_sono_time);
    }

    if (!s->sono_count) {
        AVFrame *out = *frameout = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        out->sample_aspect_ratio = av_make_q(1, 1);
        av_frame_set_color_range(out, AVCOL_RANGE_MPEG);
        av_frame_set_colorspace(out, s->csp);
        UPDATE_TIME(s->alloc_time);

        if (s->bar_h) {
            s->draw_bar(out, s->h_buf, s->rcp_h_buf, s->c_buf, s->bar_h, s->bar_t);
            UPDATE_TIME(s->bar_time);
        }

        if (s->axis_h) {
            s->draw_axis(out, s->axis_frame, s->c_buf, s->bar_h);
            UPDATE_TIME(s->axis_time);
        }

        if (s->sono_h) {
            s->draw_sono(out, s->sono_frame, s->bar_h + s->axis_h, s->sono_idx);
            UPDATE_TIME(s->sono_time);
        }
        out->pts = s->next_pts;
        s->next_pts += PTS_STEP;
    }
    s->sono_count = (s->sono_count + 1) % s->count;
    if (s->sono_h)
        s->sono_idx = (s->sono_idx + s->sono_h - 1) % s->sono_h;
    return 0;
}

static void init_colormatrix(ShowCQTContext *s)
{
    double kr, kg, kb;

    /* from vf_colorspace.c */
    switch (s->csp) {
    default:
        av_log(s->ctx, AV_LOG_WARNING, "unsupported colorspace, setting it to unspecified.\n");
        s->csp = AVCOL_SPC_UNSPECIFIED;
    case AVCOL_SPC_UNSPECIFIED:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        kr = 0.299; kb = 0.114; break;
    case AVCOL_SPC_BT709:
        kr = 0.2126; kb = 0.0722; break;
    case AVCOL_SPC_FCC:
        kr = 0.30; kb = 0.11; break;
    case AVCOL_SPC_SMPTE240M:
        kr = 0.212; kb = 0.087; break;
    case AVCOL_SPC_BT2020_NCL:
        kr = 0.2627; kb = 0.0593; break;
    }

    kg = 1.0 - kr - kb;
    s->cmatrix[0][0] = 219.0 * kr;
    s->cmatrix[0][1] = 219.0 * kg;
    s->cmatrix[0][2] = 219.0 * kb;
    s->cmatrix[1][0] = -112.0 * kr / (1.0 - kb);
    s->cmatrix[1][1] = -112.0 * kg / (1.0 - kb);
    s->cmatrix[1][2] = 112.0;
    s->cmatrix[2][0] = 112.0;
    s->cmatrix[2][1] = -112.0 * kg / (1.0 - kr);
    s->cmatrix[2][2] = -112.0 * kb / (1.0 - kr);
}

static int init_cscheme(ShowCQTContext *s)
{
    char tail[2];
    int k;

    if (sscanf(s->cscheme, " %f | %f | %f | %f | %f | %f %1s", &s->cscheme_v[0],
        &s->cscheme_v[1], &s->cscheme_v[2], &s->cscheme_v[3], &s->cscheme_v[4],
        &s->cscheme_v[5], tail) != 6)
        goto fail;

    for (k = 0; k < 6; k++)
        if (isnan(s->cscheme_v[k]) || s->cscheme_v[k] < 0.0f || s->cscheme_v[k] > 1.0f)
            goto fail;

    return 0;

fail:
    av_log(s->ctx, AV_LOG_ERROR, "invalid cscheme.\n");
    return AVERROR(EINVAL);
}

/* main filter control */
static av_cold int init(AVFilterContext *ctx)
{
    ShowCQTContext *s = ctx->priv;
    s->ctx = ctx;

    if (!s->fullhd) {
        av_log(ctx, AV_LOG_WARNING, "fullhd option is deprecated, use size/s option instead.\n");
        if (s->width != 1920 || s->height != 1080) {
            av_log(ctx, AV_LOG_ERROR, "fullhd set to 0 but with custom dimension.\n");
            return AVERROR(EINVAL);
        }
        s->width /= 2;
        s->height /= 2;
        s->fullhd = 1;
    }

    if (s->axis_h < 0) {
        s->axis_h = s->width / 60;
        if (s->axis_h & 1)
            s->axis_h++;
        if (s->bar_h >= 0 && s->sono_h >= 0)
            s->axis_h = s->height - s->bar_h - s->sono_h;
        if (s->bar_h >= 0 && s->sono_h < 0)
            s->axis_h = FFMIN(s->axis_h, s->height - s->bar_h);
        if (s->bar_h < 0 && s->sono_h >= 0)
            s->axis_h = FFMIN(s->axis_h, s->height - s->sono_h);
    }

    if (s->bar_h < 0) {
        s->bar_h = (s->height - s->axis_h) / 2;
        if (s->bar_h & 1)
            s->bar_h--;
        if (s->sono_h >= 0)
            s->bar_h = s->height - s->sono_h - s->axis_h;
    }

    if (s->sono_h < 0)
        s->sono_h = s->height - s->axis_h - s->bar_h;

    if ((s->width & 1) || (s->height & 1) || (s->bar_h & 1) || (s->axis_h & 1) || (s->sono_h & 1) ||
        (s->bar_h < 0) || (s->axis_h < 0) || (s->sono_h < 0) || (s->bar_h > s->height) ||
        (s->axis_h > s->height) || (s->sono_h > s->height) || (s->bar_h + s->axis_h + s->sono_h != s->height)) {
        av_log(ctx, AV_LOG_ERROR, "invalid dimension.\n");
        return AVERROR(EINVAL);
    }

    if (!s->fcount) {
        do {
            s->fcount++;
        } while(s->fcount * s->width < 1920 && s->fcount < 10);
    }

    init_colormatrix(s);

    return init_cscheme(s);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    common_uninit(ctx->priv);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
    enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE
    };
    int64_t channel_layouts[] = { AV_CH_LAYOUT_STEREO, AV_CH_LAYOUT_STEREO_DOWNMIX, -1 };
    int ret;

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->out_formats)) < 0)
        return ret;

    layouts = avfilter_make_format64_list(channel_layouts);
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->out_samplerates)) < 0)
        return ret;

    /* set output video format */
    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->in_formats)) < 0)
        return ret;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowCQTContext *s = ctx->priv;
    int ret;

    common_uninit(s);

    outlink->w = s->width;
    outlink->h = s->height;
    s->format = outlink->format;
    outlink->sample_aspect_ratio = av_make_q(1, 1);
    outlink->frame_rate = s->rate;
    outlink->time_base = av_mul_q(av_inv_q(s->rate), av_make_q(1, PTS_STEP));
    av_log(ctx, AV_LOG_INFO, "video: %dx%d %s %d/%d fps, bar_h = %d, axis_h = %d, sono_h = %d.\n",
           s->width, s->height, av_get_pix_fmt_name(s->format), s->rate.num, s->rate.den,
           s->bar_h, s->axis_h, s->sono_h);

    s->cqt_len = s->width * s->fcount;
    if (!(s->freq = create_freq_table(s->basefreq, s->endfreq, s->cqt_len)))
        return AVERROR(ENOMEM);

    if ((ret = init_volume(s)) < 0)
        return ret;

    s->fft_bits = FFMAX(ceil(log2(inlink->sample_rate * s->timeclamp)), 4);
    s->fft_len = 1 << s->fft_bits;
    av_log(ctx, AV_LOG_INFO, "fft_len = %d, cqt_len = %d.\n", s->fft_len, s->cqt_len);

    s->fft_ctx = av_fft_init(s->fft_bits, 0);
    s->fft_data = av_calloc(s->fft_len, sizeof(*s->fft_data));
    s->fft_result = av_calloc(s->fft_len + 64, sizeof(*s->fft_result));
    s->cqt_result = av_malloc_array(s->cqt_len, sizeof(*s->cqt_result));
    if (!s->fft_ctx || !s->fft_data || !s->fft_result || !s->cqt_result)
        return AVERROR(ENOMEM);

    s->cqt_align = 1;
    s->cqt_calc = cqt_calc;
    s->permute_coeffs = NULL;
    s->draw_sono = draw_sono;
    if (s->format == AV_PIX_FMT_RGB24) {
        s->draw_bar = draw_bar_rgb;
        s->draw_axis = draw_axis_rgb;
        s->update_sono = update_sono_rgb;
    } else {
        s->draw_bar = draw_bar_yuv;
        s->draw_axis = draw_axis_yuv;
        s->update_sono = update_sono_yuv;
    }

    if (ARCH_X86)
        ff_showcqt_init_x86(s);

    if ((ret = init_cqt(s)) < 0)
        return ret;

    if (s->axis_h) {
        if (!s->axis) {
            if ((ret = init_axis_empty(s)) < 0)
                return ret;
        } else if (s->axisfile) {
            if (init_axis_from_file(s) < 0) {
                av_log(ctx, AV_LOG_WARNING, "loading axis image failed, fallback to font rendering.\n");
                if (init_axis_from_font(s) < 0) {
                    av_log(ctx, AV_LOG_WARNING, "loading axis font failed, disable text drawing.\n");
                    if ((ret = init_axis_empty(s)) < 0)
                        return ret;
                }
            }
        } else {
            if (init_axis_from_font(s) < 0) {
                av_log(ctx, AV_LOG_WARNING, "loading axis font failed, disable text drawing.\n");
                if ((ret = init_axis_empty(s)) < 0)
                    return ret;
            }
        }
    }

    if (s->sono_h) {
        s->sono_frame = alloc_frame_empty((outlink->format == AV_PIX_FMT_YUV420P) ?
                        AV_PIX_FMT_YUV422P : outlink->format, s->width, s->sono_h);
        if (!s->sono_frame)
            return AVERROR(ENOMEM);
    }

    s->h_buf = av_malloc_array(s->cqt_len, sizeof (*s->h_buf));
    s->rcp_h_buf = av_malloc_array(s->width, sizeof(*s->rcp_h_buf));
    s->c_buf = av_malloc_array(s->width, sizeof(*s->c_buf));
    if (!s->h_buf || !s->rcp_h_buf || !s->c_buf)
        return AVERROR(ENOMEM);

    s->sono_count = 0;
    s->next_pts = 0;
    s->sono_idx = 0;
    s->remaining_fill = s->fft_len / 2;
    s->remaining_frac = 0;
    s->step_frac = av_div_q(av_make_q(inlink->sample_rate, s->count) , s->rate);
    s->step = (int)(s->step_frac.num / s->step_frac.den);
    s->step_frac.num %= s->step_frac.den;
    if (s->step_frac.num) {
        av_log(ctx, AV_LOG_INFO, "audio: %d Hz, step = %d + %d/%d.\n",
               inlink->sample_rate, s->step, s->step_frac.num, s->step_frac.den);
        av_log(ctx, AV_LOG_WARNING, "fractional step.\n");
    } else {
        av_log(ctx, AV_LOG_INFO, "audio: %d Hz, step = %d.\n",
               inlink->sample_rate, s->step);
    }

    return 0;
}


static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowCQTContext *s = ctx->priv;
    int remaining, step, ret, x, i, j, m;
    float *audio_data;
    AVFrame *out = NULL;

    if (!insamples) {
        while (s->remaining_fill < s->fft_len / 2) {
            memset(&s->fft_data[s->fft_len - s->remaining_fill], 0, sizeof(*s->fft_data) * s->remaining_fill);
            ret = plot_cqt(ctx, &out);
            if (ret < 0)
                return ret;

            step = s->step + (s->step_frac.num + s->remaining_frac) / s->step_frac.den;
            s->remaining_frac = (s->step_frac.num + s->remaining_frac) % s->step_frac.den;
            for (x = 0; x < (s->fft_len-step); x++)
                s->fft_data[x] = s->fft_data[x+step];
            s->remaining_fill += step;

            if (out)
                return ff_filter_frame(outlink, out);
        }
        return AVERROR_EOF;
    }

    remaining = insamples->nb_samples;
    audio_data = (float*) insamples->data[0];

    while (remaining) {
        i = insamples->nb_samples - remaining;
        j = s->fft_len - s->remaining_fill;
        if (remaining >= s->remaining_fill) {
            for (m = 0; m < s->remaining_fill; m++) {
                s->fft_data[j+m].re = audio_data[2*(i+m)];
                s->fft_data[j+m].im = audio_data[2*(i+m)+1];
            }
            ret = plot_cqt(ctx, &out);
            if (ret < 0) {
                av_frame_free(&insamples);
                return ret;
            }
            remaining -= s->remaining_fill;
            if (out) {
                int64_t pts = av_rescale_q(insamples->pts, inlink->time_base, av_make_q(1, inlink->sample_rate));
                pts += insamples->nb_samples - remaining - s->fft_len/2;
                pts = av_rescale_q(pts, av_make_q(1, inlink->sample_rate), outlink->time_base);
                if (FFABS(pts - out->pts) > PTS_TOLERANCE) {
                    av_log(ctx, AV_LOG_DEBUG, "changing pts from %"PRId64" (%.3f) to %"PRId64" (%.3f).\n",
                           out->pts, out->pts * av_q2d(outlink->time_base),
                           pts, pts * av_q2d(outlink->time_base));
                    out->pts = pts;
                    s->next_pts = pts + PTS_STEP;
                }
                ret = ff_filter_frame(outlink, out);
                if (ret < 0) {
                    av_frame_free(&insamples);
                    return ret;
                }
                out = NULL;
            }
            step = s->step + (s->step_frac.num + s->remaining_frac) / s->step_frac.den;
            s->remaining_frac = (s->step_frac.num + s->remaining_frac) % s->step_frac.den;
            for (m = 0; m < s->fft_len-step; m++)
                s->fft_data[m] = s->fft_data[m+step];
            s->remaining_fill = step;
        } else {
            for (m = 0; m < remaining; m++) {
                s->fft_data[j+m].re = audio_data[2*(i+m)];
                s->fft_data[j+m].im = audio_data[2*(i+m)+1];
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
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    ret = ff_request_frame(inlink);
    if (ret == AVERROR_EOF)
        ret = filter_frame(inlink, NULL);
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
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a CQT (Constant/Clamped Q Transform) spectrum video output."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(ShowCQTContext),
    .inputs        = showcqt_inputs,
    .outputs       = showcqt_outputs,
    .priv_class    = &showcqt_class,
};
