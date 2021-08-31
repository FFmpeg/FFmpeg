/*
 * COpyright (c) 2002 Daniel Pouzzner
 * Copyright (c) 1999 Chris Bagwell
 * Copyright (c) 1999 Nick Bailey
 * Copyright (c) 2007 Rob Sykes <robs@users.sourceforge.net>
 * Copyright (c) 2013 Paul B Mahol
 * Copyright (c) 2014 Andrew Kelley
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
 * audio multiband compand filter
 */

#include "libavutil/avstring.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct CompandSegment {
    double x, y;
    double a, b;
} CompandSegment;

typedef struct CompandT {
    CompandSegment *segments;
    int nb_segments;
    double in_min_lin;
    double out_min_lin;
    double curve_dB;
    double gain_dB;
} CompandT;

#define N 4

typedef struct PrevCrossover {
    double in;
    double out_low;
    double out_high;
} PrevCrossover[N * 2];

typedef struct Crossover {
  PrevCrossover *previous;
  size_t         pos;
  double         coefs[3 *(N+1)];
} Crossover;

typedef struct CompBand {
    CompandT transfer_fn;
    double *attack_rate;
    double *decay_rate;
    double *volume;
    double delay;
    double topfreq;
    Crossover filter;
    AVFrame *delay_buf;
    size_t delay_size;
    ptrdiff_t delay_buf_ptr;
    size_t delay_buf_cnt;
} CompBand;

typedef struct MCompandContext {
    const AVClass *class;

    char *args;

    int nb_bands;
    CompBand *bands;
    AVFrame *band_buf1, *band_buf2, *band_buf3;
    int band_samples;
    size_t delay_buf_size;
} MCompandContext;

#define OFFSET(x) offsetof(MCompandContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption mcompand_options[] = {
    { "args", "set parameters for each band", OFFSET(args), AV_OPT_TYPE_STRING, { .str = "0.005,0.1 6 -47/-40,-34/-34,-17/-33 100 | 0.003,0.05 6 -47/-40,-34/-34,-17/-33 400 | 0.000625,0.0125 6 -47/-40,-34/-34,-15/-33 1600 | 0.0001,0.025 6 -47/-40,-34/-34,-31/-31,-0/-30 6400 | 0,0.025 6 -38/-31,-28/-28,-0/-25 22000" }, 0, 0, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(mcompand);

static av_cold void uninit(AVFilterContext *ctx)
{
    MCompandContext *s = ctx->priv;
    int i;

    av_frame_free(&s->band_buf1);
    av_frame_free(&s->band_buf2);
    av_frame_free(&s->band_buf3);

    if (s->bands) {
        for (i = 0; i < s->nb_bands; i++) {
            av_freep(&s->bands[i].attack_rate);
            av_freep(&s->bands[i].decay_rate);
            av_freep(&s->bands[i].volume);
            av_freep(&s->bands[i].transfer_fn.segments);
            av_freep(&s->bands[i].filter.previous);
            av_frame_free(&s->bands[i].delay_buf);
        }
    }
    av_freep(&s->bands);
}

static void count_items(char *item_str, int *nb_items, char delimiter)
{
    char *p;

    *nb_items = 1;
    for (p = item_str; *p; p++) {
        if (*p == delimiter)
            (*nb_items)++;
    }
}

static void update_volume(CompBand *cb, double in, int ch)
{
    double delta = in - cb->volume[ch];

    if (delta > 0.0)
        cb->volume[ch] += delta * cb->attack_rate[ch];
    else
        cb->volume[ch] += delta * cb->decay_rate[ch];
}

static double get_volume(CompandT *s, double in_lin)
{
    CompandSegment *cs;
    double in_log, out_log;
    int i;

    if (in_lin <= s->in_min_lin)
        return s->out_min_lin;

    in_log = log(in_lin);

    for (i = 1; i < s->nb_segments; i++)
        if (in_log <= s->segments[i].x)
            break;
    cs = &s->segments[i - 1];
    in_log -= cs->x;
    out_log = cs->y + in_log * (cs->a * in_log + cs->b);

    return exp(out_log);
}

static int parse_points(char *points, int nb_points, double radius,
                        CompandT *s, AVFilterContext *ctx)
{
    int new_nb_items, num;
    char *saveptr = NULL;
    char *p = points;
    int i;

#define S(x) s->segments[2 * ((x) + 1)]
    for (i = 0, new_nb_items = 0; i < nb_points; i++) {
        char *tstr = av_strtok(p, ",", &saveptr);
        p = NULL;
        if (!tstr || sscanf(tstr, "%lf/%lf", &S(i).x, &S(i).y) != 2) {
            av_log(ctx, AV_LOG_ERROR,
                    "Invalid and/or missing input/output value.\n");
            return AVERROR(EINVAL);
        }
        if (i && S(i - 1).x > S(i).x) {
            av_log(ctx, AV_LOG_ERROR,
                    "Transfer function input values must be increasing.\n");
            return AVERROR(EINVAL);
        }
        S(i).y -= S(i).x;
        av_log(ctx, AV_LOG_DEBUG, "%d: x=%f y=%f\n", i, S(i).x, S(i).y);
        new_nb_items++;
    }
    num = new_nb_items;

    /* Add 0,0 if necessary */
    if (num == 0 || S(num - 1).x)
        num++;

#undef S
#define S(x) s->segments[2 * (x)]
    /* Add a tail off segment at the start */
    S(0).x = S(1).x - 2 * s->curve_dB;
    S(0).y = S(1).y;
    num++;

    /* Join adjacent colinear segments */
    for (i = 2; i < num; i++) {
        double g1 = (S(i - 1).y - S(i - 2).y) * (S(i - 0).x - S(i - 1).x);
        double g2 = (S(i - 0).y - S(i - 1).y) * (S(i - 1).x - S(i - 2).x);
        int j;

        if (fabs(g1 - g2))
            continue;
        num--;
        for (j = --i; j < num; j++)
            S(j) = S(j + 1);
    }

    for (i = 0; i < s->nb_segments; i += 2) {
        s->segments[i].y += s->gain_dB;
        s->segments[i].x *= M_LN10 / 20;
        s->segments[i].y *= M_LN10 / 20;
    }

#define L(x) s->segments[i - (x)]
    for (i = 4; i < s->nb_segments; i += 2) {
        double x, y, cx, cy, in1, in2, out1, out2, theta, len, r;

        L(4).a = 0;
        L(4).b = (L(2).y - L(4).y) / (L(2).x - L(4).x);

        L(2).a = 0;
        L(2).b = (L(0).y - L(2).y) / (L(0).x - L(2).x);

        theta = atan2(L(2).y - L(4).y, L(2).x - L(4).x);
        len = hypot(L(2).x - L(4).x, L(2).y - L(4).y);
        r = FFMIN(radius, len);
        L(3).x = L(2).x - r * cos(theta);
        L(3).y = L(2).y - r * sin(theta);

        theta = atan2(L(0).y - L(2).y, L(0).x - L(2).x);
        len = hypot(L(0).x - L(2).x, L(0).y - L(2).y);
        r = FFMIN(radius, len / 2);
        x = L(2).x + r * cos(theta);
        y = L(2).y + r * sin(theta);

        cx = (L(3).x + L(2).x + x) / 3;
        cy = (L(3).y + L(2).y + y) / 3;

        L(2).x = x;
        L(2).y = y;

        in1  = cx - L(3).x;
        out1 = cy - L(3).y;
        in2  = L(2).x - L(3).x;
        out2 = L(2).y - L(3).y;
        L(3).a = (out2 / in2 - out1 / in1) / (in2 - in1);
        L(3).b = out1 / in1 - L(3).a * in1;
    }
    L(3).x = 0;
    L(3).y = L(2).y;

    s->in_min_lin  = exp(s->segments[1].x);
    s->out_min_lin = exp(s->segments[1].y);

    return 0;
}

static void square_quadratic(double const *x, double *y)
{
    y[0] = x[0] * x[0];
    y[1] = 2 * x[0] * x[1];
    y[2] = 2 * x[0] * x[2] + x[1] * x[1];
    y[3] = 2 * x[1] * x[2];
    y[4] = x[2] * x[2];
}

static int crossover_setup(AVFilterLink *outlink, Crossover *p, double frequency)
{
    double w0 = 2 * M_PI * frequency / outlink->sample_rate;
    double Q = sqrt(.5), alpha = sin(w0) / (2*Q);
    double x[9], norm;
    int i;

    if (w0 > M_PI)
        return AVERROR(EINVAL);

    x[0] =  (1 - cos(w0))/2;           /* Cf. filter_LPF in biquads.c */
    x[1] =   1 - cos(w0);
    x[2] =  (1 - cos(w0))/2;
    x[3] =  (1 + cos(w0))/2;           /* Cf. filter_HPF in biquads.c */
    x[4] = -(1 + cos(w0));
    x[5] =  (1 + cos(w0))/2;
    x[6] =   1 + alpha;
    x[7] =  -2*cos(w0);
    x[8] =   1 - alpha;

    for (norm = x[6], i = 0; i < 9; ++i)
        x[i] /= norm;

    square_quadratic(x    , p->coefs);
    square_quadratic(x + 3, p->coefs + 5);
    square_quadratic(x + 6, p->coefs + 10);

    p->previous = av_calloc(outlink->ch_layout.nb_channels, sizeof(*p->previous));
    if (!p->previous)
        return AVERROR(ENOMEM);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx  = outlink->src;
    MCompandContext *s    = ctx->priv;
    int ret, ch, i, k, new_nb_items, nb_bands;
    char *p = s->args, *saveptr = NULL;
    int max_delay_size = 0;

    count_items(s->args, &nb_bands, '|');
    s->nb_bands = FFMAX(1, nb_bands);

    s->bands = av_calloc(nb_bands, sizeof(*s->bands));
    if (!s->bands)
        return AVERROR(ENOMEM);

    for (i = 0, new_nb_items = 0; i < nb_bands; i++) {
        int nb_points, nb_attacks, nb_items = 0;
        char *tstr2, *tstr = av_strtok(p, "|", &saveptr);
        char *p2, *p3, *saveptr2 = NULL, *saveptr3 = NULL;
        double radius;

        if (!tstr)
            return AVERROR(EINVAL);
        p = NULL;

        p2 = tstr;
        count_items(tstr, &nb_items, ' ');
        tstr2 = av_strtok(p2, " ", &saveptr2);
        if (!tstr2) {
            av_log(ctx, AV_LOG_ERROR, "at least one attacks/decays rate is mandatory\n");
            return AVERROR(EINVAL);
        }
        p2 = NULL;
        p3 = tstr2;

        count_items(tstr2, &nb_attacks, ',');
        if (!nb_attacks || nb_attacks & 1) {
            av_log(ctx, AV_LOG_ERROR, "number of attacks rate plus decays rate must be even\n");
            return AVERROR(EINVAL);
        }

        s->bands[i].attack_rate = av_calloc(outlink->ch_layout.nb_channels, sizeof(double));
        s->bands[i].decay_rate = av_calloc(outlink->ch_layout.nb_channels, sizeof(double));
        s->bands[i].volume = av_calloc(outlink->ch_layout.nb_channels, sizeof(double));
        if (!s->bands[i].attack_rate || !s->bands[i].decay_rate || !s->bands[i].volume)
            return AVERROR(ENOMEM);

        for (k = 0; k < FFMIN(nb_attacks / 2, outlink->ch_layout.nb_channels); k++) {
            char *tstr3 = av_strtok(p3, ",", &saveptr3);

            p3 = NULL;
            sscanf(tstr3, "%lf", &s->bands[i].attack_rate[k]);
            tstr3 = av_strtok(p3, ",", &saveptr3);
            sscanf(tstr3, "%lf", &s->bands[i].decay_rate[k]);

            if (s->bands[i].attack_rate[k] > 1.0 / outlink->sample_rate) {
                s->bands[i].attack_rate[k] = 1.0 - exp(-1.0 / (outlink->sample_rate * s->bands[i].attack_rate[k]));
            } else {
                s->bands[i].attack_rate[k] = 1.0;
            }

            if (s->bands[i].decay_rate[k] > 1.0 / outlink->sample_rate) {
                s->bands[i].decay_rate[k] = 1.0 - exp(-1.0 / (outlink->sample_rate * s->bands[i].decay_rate[k]));
            } else {
                s->bands[i].decay_rate[k] = 1.0;
            }
        }

        for (ch = k; ch < outlink->ch_layout.nb_channels; ch++) {
            s->bands[i].attack_rate[ch] = s->bands[i].attack_rate[k - 1];
            s->bands[i].decay_rate[ch]  = s->bands[i].decay_rate[k - 1];
        }

        tstr2 = av_strtok(p2, " ", &saveptr2);
        if (!tstr2) {
            av_log(ctx, AV_LOG_ERROR, "transfer function curve in dB must be set\n");
            return AVERROR(EINVAL);
        }
        sscanf(tstr2, "%lf", &s->bands[i].transfer_fn.curve_dB);

        radius = s->bands[i].transfer_fn.curve_dB * M_LN10 / 20.0;

        tstr2 = av_strtok(p2, " ", &saveptr2);
        if (!tstr2) {
            av_log(ctx, AV_LOG_ERROR, "transfer points missing\n");
            return AVERROR(EINVAL);
        }

        count_items(tstr2, &nb_points, ',');
        s->bands[i].transfer_fn.nb_segments = (nb_points + 4) * 2;
        s->bands[i].transfer_fn.segments = av_calloc(s->bands[i].transfer_fn.nb_segments,
                                                     sizeof(CompandSegment));
        if (!s->bands[i].transfer_fn.segments)
            return AVERROR(ENOMEM);

        ret = parse_points(tstr2, nb_points, radius, &s->bands[i].transfer_fn, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "transfer points parsing failed\n");
            return ret;
        }

        tstr2 = av_strtok(p2, " ", &saveptr2);
        if (!tstr2) {
            av_log(ctx, AV_LOG_ERROR, "crossover_frequency is missing\n");
            return AVERROR(EINVAL);
        }

        new_nb_items += sscanf(tstr2, "%lf", &s->bands[i].topfreq) == 1;
        if (s->bands[i].topfreq < 0 || s->bands[i].topfreq >= outlink->sample_rate / 2) {
            av_log(ctx, AV_LOG_ERROR, "crossover_frequency: %f, should be >=0 and lower than half of sample rate: %d.\n", s->bands[i].topfreq, outlink->sample_rate / 2);
            return AVERROR(EINVAL);
        }

        if (s->bands[i].topfreq != 0) {
            ret = crossover_setup(outlink, &s->bands[i].filter, s->bands[i].topfreq);
            if (ret < 0)
                return ret;
        }

        tstr2 = av_strtok(p2, " ", &saveptr2);
        if (tstr2) {
            sscanf(tstr2, "%lf", &s->bands[i].delay);
            max_delay_size = FFMAX(max_delay_size, s->bands[i].delay * outlink->sample_rate);

            tstr2 = av_strtok(p2, " ", &saveptr2);
            if (tstr2) {
                double initial_volume;

                sscanf(tstr2, "%lf", &initial_volume);
                initial_volume = pow(10.0, initial_volume / 20);

                for (k = 0; k < outlink->ch_layout.nb_channels; k++) {
                    s->bands[i].volume[k] = initial_volume;
                }

                tstr2 = av_strtok(p2, " ", &saveptr2);
                if (tstr2) {
                    sscanf(tstr2, "%lf", &s->bands[i].transfer_fn.gain_dB);
                }
            }
        }
    }
    s->nb_bands = new_nb_items;

    for (i = 0; max_delay_size > 0 && i < s->nb_bands; i++) {
        s->bands[i].delay_buf = ff_get_audio_buffer(outlink, max_delay_size);
        if (!s->bands[i].delay_buf)
            return AVERROR(ENOMEM);
    }
    s->delay_buf_size = max_delay_size;

    return 0;
}

#define CONVOLVE _ _ _ _

static void crossover(int ch, Crossover *p,
                      double *ibuf, double *obuf_low,
                      double *obuf_high, size_t len)
{
    double out_low, out_high;

    while (len--) {
        p->pos = p->pos ? p->pos - 1 : N - 1;
#define _ out_low += p->coefs[j] * p->previous[ch][p->pos + j].in \
            - p->coefs[2*N+2 + j] * p->previous[ch][p->pos + j].out_low, j++;
        {
            int j = 1;
            out_low = p->coefs[0] * *ibuf;
            CONVOLVE
            *obuf_low++ = out_low;
        }
#undef _
#define _ out_high += p->coefs[j+N+1] * p->previous[ch][p->pos + j].in \
            - p->coefs[2*N+2 + j] * p->previous[ch][p->pos + j].out_high, j++;
        {
            int j = 1;
            out_high = p->coefs[N+1] * *ibuf;
            CONVOLVE
            *obuf_high++ = out_high;
        }
        p->previous[ch][p->pos + N].in = p->previous[ch][p->pos].in = *ibuf++;
        p->previous[ch][p->pos + N].out_low = p->previous[ch][p->pos].out_low = out_low;
        p->previous[ch][p->pos + N].out_high = p->previous[ch][p->pos].out_high = out_high;
    }
}

static int mcompand_channel(MCompandContext *c, CompBand *l, double *ibuf, double *obuf, int len, int ch)
{
    int i;

    for (i = 0; i < len; i++) {
        double level_in_lin, level_out_lin, checkbuf;
        /* Maintain the volume fields by simulating a leaky pump circuit */
        update_volume(l, fabs(ibuf[i]), ch);

        /* Volume memory is updated: perform compand */
        level_in_lin = l->volume[ch];
        level_out_lin = get_volume(&l->transfer_fn, level_in_lin);

        if (c->delay_buf_size <= 0) {
            checkbuf = ibuf[i] * level_out_lin;
            obuf[i] = checkbuf;
        } else {
            double *delay_buf = (double *)l->delay_buf->extended_data[ch];

            /* FIXME: note that this lookahead algorithm is really lame:
               the response to a peak is released before the peak
               arrives. */

            /* because volume application delays differ band to band, but
               total delay doesn't, the volume is applied in an iteration
               preceding that in which the sample goes to obuf, except in
               the band(s) with the longest vol app delay.

               the offset between delay_buf_ptr and the sample to apply
               vol to, is a constant equal to the difference between this
               band's delay and the longest delay of all the bands. */

            if (l->delay_buf_cnt >= l->delay_size) {
                checkbuf =
                    delay_buf[(l->delay_buf_ptr +
                               c->delay_buf_size -
                               l->delay_size) % c->delay_buf_size] * level_out_lin;
                delay_buf[(l->delay_buf_ptr + c->delay_buf_size -
                           l->delay_size) % c->delay_buf_size] = checkbuf;
            }
            if (l->delay_buf_cnt >= c->delay_buf_size) {
                obuf[i] = delay_buf[l->delay_buf_ptr];
            } else {
                l->delay_buf_cnt++;
            }
            delay_buf[l->delay_buf_ptr++] = ibuf[i];
            l->delay_buf_ptr %= c->delay_buf_size;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext  *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    MCompandContext *s    = ctx->priv;
    AVFrame *out, *abuf, *bbuf, *cbuf;
    int ch, band, i;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    if (s->band_samples < in->nb_samples) {
        av_frame_free(&s->band_buf1);
        av_frame_free(&s->band_buf2);
        av_frame_free(&s->band_buf3);

        s->band_buf1 = ff_get_audio_buffer(outlink, in->nb_samples);
        s->band_buf2 = ff_get_audio_buffer(outlink, in->nb_samples);
        s->band_buf3 = ff_get_audio_buffer(outlink, in->nb_samples);
        s->band_samples = in->nb_samples;
    }

    for (ch = 0; ch < outlink->ch_layout.nb_channels; ch++) {
        double *a, *dst = (double *)out->extended_data[ch];

        for (band = 0, abuf = in, bbuf = s->band_buf2, cbuf = s->band_buf1; band < s->nb_bands; band++) {
            CompBand *b = &s->bands[band];

            if (b->topfreq) {
                crossover(ch, &b->filter, (double *)abuf->extended_data[ch],
                          (double *)bbuf->extended_data[ch], (double *)cbuf->extended_data[ch], in->nb_samples);
            } else {
                bbuf = abuf;
                abuf = cbuf;
            }

            if (abuf == in)
                abuf = s->band_buf3;
            mcompand_channel(s, b, (double *)bbuf->extended_data[ch], (double *)abuf->extended_data[ch], out->nb_samples, ch);
            a = (double *)abuf->extended_data[ch];
            for (i = 0; i < out->nb_samples; i++) {
                dst[i] += a[i];
            }

            FFSWAP(AVFrame *, abuf, cbuf);
        }
    }

    out->pts = in->pts;
    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    return ret;
}

static const AVFilterPad mcompand_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },
};

static const AVFilterPad mcompand_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
        .config_props  = config_output,
    },
};


const AVFilter ff_af_mcompand = {
    .name           = "mcompand",
    .description    = NULL_IF_CONFIG_SMALL(
            "Multiband Compress or expand audio dynamic range."),
    .priv_size      = sizeof(MCompandContext),
    .priv_class     = &mcompand_class,
    .uninit         = uninit,
    FILTER_INPUTS(mcompand_inputs),
    FILTER_OUTPUTS(mcompand_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
};
