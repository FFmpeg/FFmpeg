/*
 * Copyright (c) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen and others
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

#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"

#define FILTER_ORDER 4

enum FilterType {
    BUTTERWORTH,
    CHEBYSHEV1,
    CHEBYSHEV2,
    NB_TYPES
};

typedef struct FoSection {
    double a0, a1, a2, a3, a4;
    double b0, b1, b2, b3, b4;

    double num[4];
    double denum[4];
} FoSection;

typedef struct EqualizatorFilter {
    int ignore;
    int channel;
    int type;

    double freq;
    double gain;
    double width;

    FoSection section[2];
} EqualizatorFilter;

typedef struct AudioNEqualizerContext {
    const AVClass *class;
    char *args;
    char *colors;
    int draw_curves;
    int w, h;

    double mag;
    int fscale;
    int nb_filters;
    int nb_allocated;
    EqualizatorFilter *filters;
    AVFrame *video;
} AudioNEqualizerContext;

#define OFFSET(x) offsetof(AudioNEqualizerContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM

static const AVOption anequalizer_options[] = {
    { "params", NULL,                             OFFSET(args),        AV_OPT_TYPE_STRING,     {.str=""}, 0, 0, A|F },
    { "curves", "draw frequency response curves", OFFSET(draw_curves), AV_OPT_TYPE_BOOL,       {.i64=0}, 0, 1, V|F },
    { "size",   "set video size",                 OFFSET(w),           AV_OPT_TYPE_IMAGE_SIZE, {.str = "hd720"}, 0, 0, V|F },
    { "mgain",  "set max gain",                   OFFSET(mag),         AV_OPT_TYPE_DOUBLE,     {.dbl=60}, -900, 900, V|F },
    { "fscale", "set frequency scale",            OFFSET(fscale),      AV_OPT_TYPE_INT,        {.i64=1}, 0, 1, V|F, "fscale" },
        { "lin",  "linear",                       0,                   AV_OPT_TYPE_CONST,      {.i64=0}, 0, 0, V|F, "fscale" },
        { "log",  "logarithmic",                  0,                   AV_OPT_TYPE_CONST,      {.i64=1}, 0, 0, V|F, "fscale" },
    { "colors", "set channels curves colors",     OFFSET(colors),      AV_OPT_TYPE_STRING,     {.str = "red|green|blue|yellow|orange|lime|pink|magenta|brown" }, 0, 0, V|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(anequalizer);

static void draw_curves(AVFilterContext *ctx, AVFilterLink *inlink, AVFrame *out)
{
    AudioNEqualizerContext *s = ctx->priv;
    char *colors, *color, *saveptr = NULL;
    int ch, i, n;

    colors = av_strdup(s->colors);
    if (!colors)
        return;

    memset(out->data[0], 0, s->h * out->linesize[0]);

    for (ch = 0; ch < inlink->channels; ch++) {
        uint8_t fg[4] = { 0xff, 0xff, 0xff, 0xff };
        int prev_v = -1;
        double f;

        color = av_strtok(ch == 0 ? colors : NULL, " |", &saveptr);
        if (color)
            av_parse_color(fg, color, -1, ctx);

        for (f = 0; f < s->w; f++) {
            double zr, zi, zr2, zi2;
            double Hr, Hi;
            double Hmag = 1;
            double w;
            int v, y, x;

            w = M_PI * (s->fscale ? pow(s->w - 1, f / s->w) : f) / (s->w - 1);
            zr = cos(w);
            zr2 = zr * zr;
            zi = -sin(w);
            zi2 = zi * zi;

            for (n = 0; n < s->nb_filters; n++) {
                if (s->filters[n].channel != ch ||
                    s->filters[n].ignore)
                    continue;

                for (i = 0; i < FILTER_ORDER / 2; i++) {
                    FoSection *S = &s->filters[n].section[i];

                    /* H *= (((((S->b4 * z + S->b3) * z + S->b2) * z + S->b1) * z + S->b0) /
                          ((((S->a4 * z + S->a3) * z + S->a2) * z + S->a1) * z + S->a0)); */

                    Hr = S->b4*(1-8*zr2*zi2) + S->b2*(zr2-zi2) + zr*(S->b1+S->b3*(zr2-3*zi2))+ S->b0;
                    Hi = zi*(S->b3*(3*zr2-zi2) + S->b1 + 2*zr*(2*S->b4*(zr2-zi2) + S->b2));
                    Hmag *= hypot(Hr, Hi);
                    Hr = S->a4*(1-8*zr2*zi2) + S->a2*(zr2-zi2) + zr*(S->a1+S->a3*(zr2-3*zi2))+ S->a0;
                    Hi = zi*(S->a3*(3*zr2-zi2) + S->a1 + 2*zr*(2*S->a4*(zr2-zi2) + S->a2));
                    Hmag /= hypot(Hr, Hi);
                }
            }

            v = av_clip((1. + -20 * log10(Hmag) / s->mag) * s->h / 2, 0, s->h - 1);
            x = lrint(f);
            if (prev_v == -1)
                prev_v = v;
            if (v <= prev_v) {
                for (y = v; y <= prev_v; y++)
                    AV_WL32(out->data[0] + y * out->linesize[0] + x * 4, AV_RL32(fg));
            } else {
                for (y = prev_v; y <= v; y++)
                    AV_WL32(out->data[0] + y * out->linesize[0] + x * 4, AV_RL32(fg));
            }

            prev_v = v;
        }
    }

    av_free(colors);
}

static int config_video(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioNEqualizerContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFrame *out;

    outlink->w = s->w;
    outlink->h = s->h;

    av_frame_free(&s->video);
    s->video = out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    outlink->sample_aspect_ratio = (AVRational){1,1};

    draw_curves(ctx, inlink, out);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioNEqualizerContext *s = ctx->priv;
    AVFilterPad pad, vpad;
    int ret;

    pad = (AVFilterPad){
        .name         = "out0",
        .type         = AVMEDIA_TYPE_AUDIO,
    };

    ret = ff_append_outpad(ctx, &pad);
    if (ret < 0)
        return ret;

    if (s->draw_curves) {
        vpad = (AVFilterPad){
            .name         = "out1",
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video,
        };
        ret = ff_append_outpad(ctx, &vpad);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioNEqualizerContext *s = ctx->priv;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    if (s->draw_curves) {
        AVFilterLink *videolink = ctx->outputs[1];
        formats = ff_make_format_list(pix_fmts);
        if ((ret = ff_formats_ref(formats, &videolink->incfg.formats)) < 0)
            return ret;
    }

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.formats)) < 0 ||
        (ret = ff_formats_ref(formats, &outlink->incfg.formats)) < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0 ||
        (ret = ff_channel_layouts_ref(layouts, &outlink->incfg.channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.samplerates)) < 0 ||
        (ret = ff_formats_ref(formats, &outlink->incfg.samplerates)) < 0)
        return ret;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioNEqualizerContext *s = ctx->priv;

    av_frame_free(&s->video);
    av_freep(&s->filters);
    s->nb_filters = 0;
    s->nb_allocated = 0;
}

static void butterworth_fo_section(FoSection *S, double beta,
                                   double si, double g, double g0,
                                   double D, double c0)
{
    if (c0 == 1 || c0 == -1) {
        S->b0 = (g*g*beta*beta + 2*g*g0*si*beta + g0*g0)/D;
        S->b1 = 2*c0*(g*g*beta*beta - g0*g0)/D;
        S->b2 = (g*g*beta*beta - 2*g0*g*beta*si + g0*g0)/D;
        S->b3 = 0;
        S->b4 = 0;

        S->a0 = 1;
        S->a1 = 2*c0*(beta*beta - 1)/D;
        S->a2 = (beta*beta - 2*beta*si + 1)/D;
        S->a3 = 0;
        S->a4 = 0;
    } else {
        S->b0 = (g*g*beta*beta + 2*g*g0*si*beta + g0*g0)/D;
        S->b1 = -4*c0*(g0*g0 + g*g0*si*beta)/D;
        S->b2 = 2*(g0*g0*(1 + 2*c0*c0) - g*g*beta*beta)/D;
        S->b3 = -4*c0*(g0*g0 - g*g0*si*beta)/D;
        S->b4 = (g*g*beta*beta - 2*g*g0*si*beta + g0*g0)/D;

        S->a0 = 1;
        S->a1 = -4*c0*(1 + si*beta)/D;
        S->a2 = 2*(1 + 2*c0*c0 - beta*beta)/D;
        S->a3 = -4*c0*(1 - si*beta)/D;
        S->a4 = (beta*beta - 2*si*beta + 1)/D;
    }
}

static void butterworth_bp_filter(EqualizatorFilter *f,
                                  int N, double w0, double wb,
                                  double G, double Gb, double G0)
{
    double g, c0, g0, beta;
    double epsilon;
    int r =  N % 2;
    int L = (N - r) / 2;
    int i;

    if (G == 0 && G0 == 0) {
        f->section[0].a0 = 1;
        f->section[0].b0 = 1;
        f->section[1].a0 = 1;
        f->section[1].b0 = 1;
        return;
    }

    G  = ff_exp10(G/20);
    Gb = ff_exp10(Gb/20);
    G0 = ff_exp10(G0/20);

    epsilon = sqrt((G * G - Gb * Gb) / (Gb * Gb - G0 * G0));
    g  = pow(G,  1.0 / N);
    g0 = pow(G0, 1.0 / N);
    beta = pow(epsilon, -1.0 / N) * tan(wb/2);
    c0 = cos(w0);

    for (i = 1; i <= L; i++) {
        double ui = (2.0 * i - 1) / N;
        double si = sin(M_PI * ui / 2.0);
        double Di = beta * beta + 2 * si * beta + 1;

        butterworth_fo_section(&f->section[i - 1], beta, si, g, g0, Di, c0);
    }
}

static void chebyshev1_fo_section(FoSection *S, double a,
                                  double c, double tetta_b,
                                  double g0, double si, double b,
                                  double D, double c0)
{
    if (c0 == 1 || c0 == -1) {
        S->b0 = (tetta_b*tetta_b*(b*b+g0*g0*c*c) + 2*g0*b*si*tetta_b*tetta_b + g0*g0)/D;
        S->b1 = 2*c0*(tetta_b*tetta_b*(b*b+g0*g0*c*c) - g0*g0)/D;
        S->b2 = (tetta_b*tetta_b*(b*b+g0*g0*c*c) - 2*g0*b*si*tetta_b + g0*g0)/D;
        S->b3 = 0;
        S->b4 = 0;

        S->a0 = 1;
        S->a1 = 2*c0*(tetta_b*tetta_b*(a*a+c*c) - 1)/D;
        S->a2 = (tetta_b*tetta_b*(a*a+c*c) - 2*a*si*tetta_b + 1)/D;
        S->a3 = 0;
        S->a4 = 0;
    } else {
        S->b0 = ((b*b + g0*g0*c*c)*tetta_b*tetta_b + 2*g0*b*si*tetta_b + g0*g0)/D;
        S->b1 = -4*c0*(g0*g0 + g0*b*si*tetta_b)/D;
        S->b2 = 2*(g0*g0*(1 + 2*c0*c0) - (b*b + g0*g0*c*c)*tetta_b*tetta_b)/D;
        S->b3 = -4*c0*(g0*g0 - g0*b*si*tetta_b)/D;
        S->b4 = ((b*b + g0*g0*c*c)*tetta_b*tetta_b - 2*g0*b*si*tetta_b + g0*g0)/D;

        S->a0 = 1;
        S->a1 = -4*c0*(1 + a*si*tetta_b)/D;
        S->a2 = 2*(1 + 2*c0*c0 - (a*a + c*c)*tetta_b*tetta_b)/D;
        S->a3 = -4*c0*(1 - a*si*tetta_b)/D;
        S->a4 = ((a*a + c*c)*tetta_b*tetta_b - 2*a*si*tetta_b + 1)/D;
    }
}

static void chebyshev1_bp_filter(EqualizatorFilter *f,
                                 int N, double w0, double wb,
                                 double G, double Gb, double G0)
{
    double a, b, c0, g0, alfa, beta, tetta_b;
    double epsilon;
    int r =  N % 2;
    int L = (N - r) / 2;
    int i;

    if (G == 0 && G0 == 0) {
        f->section[0].a0 = 1;
        f->section[0].b0 = 1;
        f->section[1].a0 = 1;
        f->section[1].b0 = 1;
        return;
    }

    G  = ff_exp10(G/20);
    Gb = ff_exp10(Gb/20);
    G0 = ff_exp10(G0/20);

    epsilon = sqrt((G*G - Gb*Gb) / (Gb*Gb - G0*G0));
    g0 = pow(G0,1.0/N);
    alfa = pow(1.0/epsilon    + sqrt(1 + 1/(epsilon*epsilon)), 1.0/N);
    beta = pow(G/epsilon + Gb * sqrt(1 + 1/(epsilon*epsilon)), 1.0/N);
    a = 0.5 * (alfa - 1.0/alfa);
    b = 0.5 * (beta - g0*g0*(1/beta));
    tetta_b = tan(wb/2);
    c0 = cos(w0);

    for (i = 1; i <= L; i++) {
        double ui = (2.0*i-1.0)/N;
        double ci = cos(M_PI*ui/2.0);
        double si = sin(M_PI*ui/2.0);
        double Di = (a*a + ci*ci)*tetta_b*tetta_b + 2.0*a*si*tetta_b + 1;

        chebyshev1_fo_section(&f->section[i - 1], a, ci, tetta_b, g0, si, b, Di, c0);
    }
}

static void chebyshev2_fo_section(FoSection *S, double a,
                                  double c, double tetta_b,
                                  double g, double si, double b,
                                  double D, double c0)
{
    if (c0 == 1 || c0 == -1) {
        S->b0 = (g*g*tetta_b*tetta_b + 2*tetta_b*g*b*si + b*b + g*g*c*c)/D;
        S->b1 = 2*c0*(g*g*tetta_b*tetta_b - b*b - g*g*c*c)/D;
        S->b2 = (g*g*tetta_b*tetta_b - 2*tetta_b*g*b*si + b*b + g*g*c*c)/D;
        S->b3 = 0;
        S->b4 = 0;

        S->a0 = 1;
        S->a1 = 2*c0*(tetta_b*tetta_b - a*a - c*c)/D;
        S->a2 = (tetta_b*tetta_b - 2*tetta_b*a*si + a*a + c*c)/D;
        S->a3 = 0;
        S->a4 = 0;
    } else {
        S->b0 = (g*g*tetta_b*tetta_b + 2*g*b*si*tetta_b + b*b + g*g*c*c)/D;
        S->b1 = -4*c0*(b*b + g*g*c*c + g*b*si*tetta_b)/D;
        S->b2 = 2*((b*b + g*g*c*c)*(1 + 2*c0*c0) - g*g*tetta_b*tetta_b)/D;
        S->b3 = -4*c0*(b*b + g*g*c*c - g*b*si*tetta_b)/D;
        S->b4 = (g*g*tetta_b*tetta_b - 2*g*b*si*tetta_b + b*b + g*g*c*c)/D;

        S->a0 = 1;
        S->a1 = -4*c0*(a*a + c*c + a*si*tetta_b)/D;
        S->a2 = 2*((a*a + c*c)*(1 + 2*c0*c0) - tetta_b*tetta_b)/D;
        S->a3 = -4*c0*(a*a + c*c - a*si*tetta_b)/D;
        S->a4 = (tetta_b*tetta_b - 2*a*si*tetta_b + a*a + c*c)/D;
    }
}

static void chebyshev2_bp_filter(EqualizatorFilter *f,
                                 int N, double w0, double wb,
                                 double G, double Gb, double G0)
{
    double a, b, c0, tetta_b;
    double epsilon, g, eu, ew;
    int r =  N % 2;
    int L = (N - r) / 2;
    int i;

    if (G == 0 && G0 == 0) {
        f->section[0].a0 = 1;
        f->section[0].b0 = 1;
        f->section[1].a0 = 1;
        f->section[1].b0 = 1;
        return;
    }

    G  = ff_exp10(G/20);
    Gb = ff_exp10(Gb/20);
    G0 = ff_exp10(G0/20);

    epsilon = sqrt((G*G - Gb*Gb) / (Gb*Gb - G0*G0));
    g  = pow(G, 1.0 / N);
    eu = pow(epsilon + sqrt(1 + epsilon*epsilon), 1.0/N);
    ew = pow(G0*epsilon + Gb*sqrt(1 + epsilon*epsilon), 1.0/N);
    a = (eu - 1.0/eu)/2.0;
    b = (ew - g*g/ew)/2.0;
    tetta_b = tan(wb/2);
    c0 = cos(w0);

    for (i = 1; i <= L; i++) {
        double ui = (2.0 * i - 1.0)/N;
        double ci = cos(M_PI * ui / 2.0);
        double si = sin(M_PI * ui / 2.0);
        double Di = tetta_b*tetta_b + 2*a*si*tetta_b + a*a + ci*ci;

        chebyshev2_fo_section(&f->section[i - 1], a, ci, tetta_b, g, si, b, Di, c0);
    }
}

static double butterworth_compute_bw_gain_db(double gain)
{
    double bw_gain = 0;

    if (gain <= -6)
        bw_gain = gain + 3;
    else if(gain > -6 && gain < 6)
        bw_gain = gain * 0.5;
    else if(gain >= 6)
        bw_gain = gain - 3;

    return bw_gain;
}

static double chebyshev1_compute_bw_gain_db(double gain)
{
    double bw_gain = 0;

    if (gain <= -6)
        bw_gain = gain + 1;
    else if(gain > -6 && gain < 6)
        bw_gain = gain * 0.9;
    else if(gain >= 6)
        bw_gain = gain - 1;

    return bw_gain;
}

static double chebyshev2_compute_bw_gain_db(double gain)
{
    double bw_gain = 0;

    if (gain <= -6)
        bw_gain = -3;
    else if(gain > -6 && gain < 6)
        bw_gain = gain * 0.3;
    else if(gain >= 6)
        bw_gain = 3;

    return bw_gain;
}

static inline double hz_2_rad(double x, double fs)
{
    return 2 * M_PI * x / fs;
}

static void equalizer(EqualizatorFilter *f, double sample_rate)
{
    double w0 = hz_2_rad(f->freq,  sample_rate);
    double wb = hz_2_rad(f->width, sample_rate);
    double bw_gain;

    switch (f->type) {
    case BUTTERWORTH:
        bw_gain = butterworth_compute_bw_gain_db(f->gain);
        butterworth_bp_filter(f, FILTER_ORDER, w0, wb, f->gain, bw_gain, 0);
        break;
    case CHEBYSHEV1:
        bw_gain = chebyshev1_compute_bw_gain_db(f->gain);
        chebyshev1_bp_filter(f, FILTER_ORDER, w0, wb, f->gain, bw_gain, 0);
        break;
    case CHEBYSHEV2:
        bw_gain = chebyshev2_compute_bw_gain_db(f->gain);
        chebyshev2_bp_filter(f, FILTER_ORDER, w0, wb, f->gain, bw_gain, 0);
        break;
    }

}

static int add_filter(AudioNEqualizerContext *s, AVFilterLink *inlink)
{
    equalizer(&s->filters[s->nb_filters], inlink->sample_rate);
    if (s->nb_filters >= s->nb_allocated - 1) {
        EqualizatorFilter *filters;

        filters = av_calloc(s->nb_allocated, 2 * sizeof(*s->filters));
        if (!filters)
            return AVERROR(ENOMEM);
        memcpy(filters, s->filters, sizeof(*s->filters) * s->nb_allocated);
        av_free(s->filters);
        s->filters = filters;
        s->nb_allocated *= 2;
    }
    s->nb_filters++;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioNEqualizerContext *s = ctx->priv;
    char *args = av_strdup(s->args);
    char *saveptr = NULL;
    int ret = 0;

    if (!args)
        return AVERROR(ENOMEM);

    s->nb_allocated = 32 * inlink->channels;
    s->filters = av_calloc(inlink->channels, 32 * sizeof(*s->filters));
    if (!s->filters) {
        s->nb_allocated = 0;
        av_free(args);
        return AVERROR(ENOMEM);
    }

    while (1) {
        char *arg = av_strtok(s->nb_filters == 0 ? args : NULL, "|", &saveptr);

        if (!arg)
            break;

        s->filters[s->nb_filters].type = 0;
        if (sscanf(arg, "c%d f=%lf w=%lf g=%lf t=%d", &s->filters[s->nb_filters].channel,
                                                     &s->filters[s->nb_filters].freq,
                                                     &s->filters[s->nb_filters].width,
                                                     &s->filters[s->nb_filters].gain,
                                                     &s->filters[s->nb_filters].type) != 5 &&
            sscanf(arg, "c%d f=%lf w=%lf g=%lf", &s->filters[s->nb_filters].channel,
                                                &s->filters[s->nb_filters].freq,
                                                &s->filters[s->nb_filters].width,
                                                &s->filters[s->nb_filters].gain) != 4 ) {
            av_free(args);
            return AVERROR(EINVAL);
        }

        if (s->filters[s->nb_filters].freq < 0 ||
            s->filters[s->nb_filters].freq > inlink->sample_rate / 2.0)
            s->filters[s->nb_filters].ignore = 1;

        if (s->filters[s->nb_filters].channel < 0 ||
            s->filters[s->nb_filters].channel >= inlink->channels)
            s->filters[s->nb_filters].ignore = 1;

        s->filters[s->nb_filters].type = av_clip(s->filters[s->nb_filters].type, 0, NB_TYPES - 1);
        ret = add_filter(s, inlink);
        if (ret < 0)
            break;
    }

    av_free(args);

    return ret;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AudioNEqualizerContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret = AVERROR(ENOSYS);

    if (!strcmp(cmd, "change")) {
        double freq, width, gain;
        int filter;

        if (sscanf(args, "%d|f=%lf|w=%lf|g=%lf", &filter, &freq, &width, &gain) != 4)
            return AVERROR(EINVAL);

        if (filter < 0 || filter >= s->nb_filters)
            return AVERROR(EINVAL);

        if (freq < 0 || freq > inlink->sample_rate / 2.0)
            return AVERROR(EINVAL);

        s->filters[filter].freq  = freq;
        s->filters[filter].width = width;
        s->filters[filter].gain  = gain;
        equalizer(&s->filters[filter], inlink->sample_rate);
        if (s->draw_curves)
            draw_curves(ctx, inlink, s->video);

        ret = 0;
    }

    return ret;
}

static inline double section_process(FoSection *S, double in)
{
    double out;

    out = S->b0 * in;
    out+= S->b1 * S->num[0] - S->denum[0] * S->a1;
    out+= S->b2 * S->num[1] - S->denum[1] * S->a2;
    out+= S->b3 * S->num[2] - S->denum[2] * S->a3;
    out+= S->b4 * S->num[3] - S->denum[3] * S->a4;

    S->num[3] = S->num[2];
    S->num[2] = S->num[1];
    S->num[1] = S->num[0];
    S->num[0] = in;

    S->denum[3] = S->denum[2];
    S->denum[2] = S->denum[1];
    S->denum[1] = S->denum[0];
    S->denum[0] = out;

    return out;
}

static double process_sample(FoSection *s1, double in)
{
    double p0 = in, p1;
    int i;

    for (i = 0; i < FILTER_ORDER / 2; i++) {
        p1 = section_process(&s1[i], p0);
        p0 = p1;
    }

    return p1;
}

static int filter_channels(AVFilterContext *ctx, void *arg,
                           int jobnr, int nb_jobs)
{
    AudioNEqualizerContext *s = ctx->priv;
    AVFrame *buf = arg;
    const int start = (buf->channels * jobnr) / nb_jobs;
    const int end = (buf->channels * (jobnr+1)) / nb_jobs;

    for (int i = 0; i < s->nb_filters; i++) {
        EqualizatorFilter *f = &s->filters[i];
        double *bptr;

        if (f->gain == 0. || f->ignore)
            continue;
        if (f->channel < start ||
            f->channel >= end)
            continue;

        bptr = (double *)buf->extended_data[f->channel];
        for (int n = 0; n < buf->nb_samples; n++) {
            double sample = bptr[n];

            sample  = process_sample(f->section, sample);
            bptr[n] = sample;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    AudioNEqualizerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (!ctx->is_disabled)
        ff_filter_execute(ctx, filter_channels, buf, NULL,
                          FFMIN(inlink->channels, ff_filter_get_nb_threads(ctx)));

    if (s->draw_curves) {
        AVFrame *clone;

        const int64_t pts = buf->pts +
            av_rescale_q(buf->nb_samples, (AVRational){ 1, inlink->sample_rate },
                         outlink->time_base);
        int ret;

        s->video->pts = pts;
        clone = av_frame_clone(s->video);
        if (!clone)
            return AVERROR(ENOMEM);
        ret = ff_filter_frame(ctx->outputs[1], clone);
        if (ret < 0)
            return ret;
    }

    return ff_filter_frame(outlink, buf);
}

static const AVFilterPad inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
    },
};

const AVFilter ff_af_anequalizer = {
    .name          = "anequalizer",
    .description   = NULL_IF_CONFIG_SMALL("Apply high-order audio parametric multi band equalizer."),
    .priv_size     = sizeof(AudioNEqualizerContext),
    .priv_class    = &anequalizer_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    .outputs       = NULL,
    FILTER_QUERY_FUNC(query_formats),
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_DYNAMIC_OUTPUTS |
                     AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
};
