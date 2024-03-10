/*
 * Copyright (c) 2016 Muhammad Faiz <mfcc64@gmail.com>
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

#include "libavutil/channel_layout.h"
#include "libavutil/file_open.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/avassert.h"
#include "libavutil/tx.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"

#define RDFT_BITS_MIN 4
#define RDFT_BITS_MAX 16

enum WindowFunc {
    WFUNC_RECTANGULAR,
    WFUNC_HANN,
    WFUNC_HAMMING,
    WFUNC_BLACKMAN,
    WFUNC_NUTTALL3,
    WFUNC_MNUTTALL3,
    WFUNC_NUTTALL,
    WFUNC_BNUTTALL,
    WFUNC_BHARRIS,
    WFUNC_TUKEY,
    NB_WFUNC
};

enum Scale {
    SCALE_LINLIN,
    SCALE_LINLOG,
    SCALE_LOGLIN,
    SCALE_LOGLOG,
    NB_SCALE
};

#define NB_GAIN_ENTRY_MAX 4096
typedef struct GainEntry {
    double  freq;
    double  gain;
} GainEntry;

typedef struct OverlapIndex {
    int buf_idx;
    int overlap_idx;
} OverlapIndex;

typedef struct FIREqualizerContext {
    const AVClass *class;

    AVTXContext   *analysis_rdft;
    av_tx_fn      analysis_rdft_fn;
    AVTXContext   *analysis_irdft;
    av_tx_fn      analysis_irdft_fn;
    AVTXContext   *rdft;
    av_tx_fn      rdft_fn;
    AVTXContext   *irdft;
    av_tx_fn      irdft_fn;
    AVTXContext   *fft_ctx;
    av_tx_fn      fft_fn;
    AVTXContext   *cepstrum_rdft;
    av_tx_fn      cepstrum_rdft_fn;
    AVTXContext   *cepstrum_irdft;
    av_tx_fn      cepstrum_irdft_fn;
    int           analysis_rdft_len;
    int           rdft_len;
    int           cepstrum_len;

    float         *analysis_buf;
    float         *analysis_tbuf;
    float         *dump_buf;
    float         *kernel_tmp_buf;
    float         *kernel_tmp_tbuf;
    float         *kernel_buf;
    float         *tx_buf;
    float         *cepstrum_buf;
    float         *cepstrum_tbuf;
    float         *conv_buf;
    OverlapIndex  *conv_idx;
    int           fir_len;
    int           nsamples_max;
    int64_t       next_pts;
    int           frame_nsamples_max;
    int           remaining;

    char          *gain_cmd;
    char          *gain_entry_cmd;
    const char    *gain;
    const char    *gain_entry;
    double        delay;
    double        accuracy;
    int           wfunc;
    int           fixed;
    int           multi;
    int           zero_phase;
    int           scale;
    char          *dumpfile;
    int           dumpscale;
    int           fft2;
    int           min_phase;

    int           nb_gain_entry;
    int           gain_entry_err;
    GainEntry     gain_entry_tbl[NB_GAIN_ENTRY_MAX];
} FIREqualizerContext;

#define OFFSET(x) offsetof(FIREqualizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define TFLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption firequalizer_options[] = {
    { "gain", "set gain curve", OFFSET(gain), AV_OPT_TYPE_STRING, { .str = "gain_interpolate(f)" }, 0, 0, TFLAGS },
    { "gain_entry", "set gain entry", OFFSET(gain_entry), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, TFLAGS },
    { "delay", "set delay", OFFSET(delay), AV_OPT_TYPE_DOUBLE, { .dbl = 0.01 }, 0.0, 1e10, FLAGS },
    { "accuracy", "set accuracy", OFFSET(accuracy), AV_OPT_TYPE_DOUBLE, { .dbl = 5.0 }, 0.0, 1e10, FLAGS },
    { "wfunc", "set window function", OFFSET(wfunc), AV_OPT_TYPE_INT, { .i64 = WFUNC_HANN }, 0, NB_WFUNC-1, FLAGS, .unit = "wfunc" },
        { "rectangular", "rectangular window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_RECTANGULAR }, 0, 0, FLAGS, .unit = "wfunc" },
        { "hann", "hann window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_HANN }, 0, 0, FLAGS, .unit = "wfunc" },
        { "hamming", "hamming window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_HAMMING }, 0, 0, FLAGS, .unit = "wfunc" },
        { "blackman", "blackman window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_BLACKMAN }, 0, 0, FLAGS, .unit = "wfunc" },
        { "nuttall3", "3-term nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_NUTTALL3 }, 0, 0, FLAGS, .unit = "wfunc" },
        { "mnuttall3", "minimum 3-term nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_MNUTTALL3 }, 0, 0, FLAGS, .unit = "wfunc" },
        { "nuttall", "nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_NUTTALL }, 0, 0, FLAGS, .unit = "wfunc" },
        { "bnuttall", "blackman-nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_BNUTTALL }, 0, 0, FLAGS, .unit = "wfunc" },
        { "bharris", "blackman-harris window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_BHARRIS }, 0, 0, FLAGS, .unit = "wfunc" },
        { "tukey", "tukey window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_TUKEY }, 0, 0, FLAGS, .unit = "wfunc" },
    { "fixed", "set fixed frame samples", OFFSET(fixed), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "multi", "set multi channels mode", OFFSET(multi), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "zero_phase", "set zero phase mode", OFFSET(zero_phase), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "scale", "set gain scale", OFFSET(scale), AV_OPT_TYPE_INT, { .i64 = SCALE_LINLOG }, 0, NB_SCALE-1, FLAGS, .unit = "scale" },
        { "linlin", "linear-freq linear-gain", 0, AV_OPT_TYPE_CONST, { .i64 = SCALE_LINLIN }, 0, 0, FLAGS, .unit = "scale" },
        { "linlog", "linear-freq logarithmic-gain", 0, AV_OPT_TYPE_CONST, { .i64 = SCALE_LINLOG }, 0, 0, FLAGS, .unit = "scale" },
        { "loglin", "logarithmic-freq linear-gain", 0, AV_OPT_TYPE_CONST, { .i64 = SCALE_LOGLIN }, 0, 0, FLAGS, .unit = "scale" },
        { "loglog", "logarithmic-freq logarithmic-gain", 0, AV_OPT_TYPE_CONST, { .i64 = SCALE_LOGLOG }, 0, 0, FLAGS, .unit = "scale" },
    { "dumpfile", "set dump file", OFFSET(dumpfile), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { "dumpscale", "set dump scale", OFFSET(dumpscale), AV_OPT_TYPE_INT, { .i64 = SCALE_LINLOG }, 0, NB_SCALE-1, FLAGS, .unit = "scale" },
    { "fft2", "set 2-channels fft", OFFSET(fft2), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "min_phase", "set minimum phase mode", OFFSET(min_phase), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(firequalizer);

static void common_uninit(FIREqualizerContext *s)
{
    av_tx_uninit(&s->analysis_rdft);
    av_tx_uninit(&s->analysis_irdft);
    av_tx_uninit(&s->rdft);
    av_tx_uninit(&s->irdft);
    av_tx_uninit(&s->fft_ctx);
    av_tx_uninit(&s->cepstrum_rdft);
    av_tx_uninit(&s->cepstrum_irdft);
    s->analysis_rdft = s->analysis_irdft = s->rdft = s->irdft = NULL;
    s->fft_ctx = NULL;
    s->cepstrum_rdft = NULL;
    s->cepstrum_irdft = NULL;

    av_freep(&s->analysis_buf);
    av_freep(&s->analysis_tbuf);
    av_freep(&s->dump_buf);
    av_freep(&s->kernel_tmp_buf);
    av_freep(&s->kernel_tmp_tbuf);
    av_freep(&s->kernel_buf);
    av_freep(&s->tx_buf);
    av_freep(&s->cepstrum_buf);
    av_freep(&s->cepstrum_tbuf);
    av_freep(&s->conv_buf);
    av_freep(&s->conv_idx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FIREqualizerContext *s = ctx->priv;

    common_uninit(s);
    av_freep(&s->gain_cmd);
    av_freep(&s->gain_entry_cmd);
}

static void fast_convolute(FIREqualizerContext *restrict s, const float *restrict kernel_buf, float *restrict conv_buf,
                           OverlapIndex *restrict idx, float *restrict data, int nsamples)
{
    if (nsamples <= s->nsamples_max) {
        float *buf = conv_buf + idx->buf_idx * s->rdft_len;
        float *obuf = conv_buf + !idx->buf_idx * s->rdft_len + idx->overlap_idx;
        float *tbuf = s->tx_buf;
        int center = s->fir_len/2;
        int k;

        memset(buf, 0, center * sizeof(*data));
        memcpy(buf + center, data, nsamples * sizeof(*data));
        memset(buf + center + nsamples, 0, (s->rdft_len - nsamples - center) * sizeof(*data));
        s->rdft_fn(s->rdft, tbuf, buf, sizeof(float));

        for (k = 0; k <= s->rdft_len/2; k++) {
            tbuf[2*k] *= kernel_buf[k];
            tbuf[2*k+1] *= kernel_buf[k];
        }

        s->irdft_fn(s->irdft, buf, tbuf, sizeof(AVComplexFloat));
        for (k = 0; k < s->rdft_len - idx->overlap_idx; k++)
            buf[k] += obuf[k];
        memcpy(data, buf, nsamples * sizeof(*data));
        idx->buf_idx = !idx->buf_idx;
        idx->overlap_idx = nsamples;
    } else {
        while (nsamples > s->nsamples_max * 2) {
            fast_convolute(s, kernel_buf, conv_buf, idx, data, s->nsamples_max);
            data += s->nsamples_max;
            nsamples -= s->nsamples_max;
        }
        fast_convolute(s, kernel_buf, conv_buf, idx, data, nsamples/2);
        fast_convolute(s, kernel_buf, conv_buf, idx, data + nsamples/2, nsamples - nsamples/2);
    }
}

static void fast_convolute_nonlinear(FIREqualizerContext *restrict s, const float *restrict kernel_buf,
                                     float *restrict conv_buf, OverlapIndex *restrict idx,
                                     float *restrict data, int nsamples)
{
    if (nsamples <= s->nsamples_max) {
        float *buf = conv_buf + idx->buf_idx * s->rdft_len;
        float *obuf = conv_buf + !idx->buf_idx * s->rdft_len + idx->overlap_idx;
        float *tbuf = s->tx_buf;
        int k;

        memcpy(buf, data, nsamples * sizeof(*data));
        memset(buf + nsamples, 0, (s->rdft_len - nsamples) * sizeof(*data));
        s->rdft_fn(s->rdft, tbuf, buf, sizeof(float));

        for (k = 0; k < s->rdft_len + 2; k += 2) {
            float re, im;
            re = tbuf[k] * kernel_buf[k] - tbuf[k+1] * kernel_buf[k+1];
            im = tbuf[k] * kernel_buf[k+1] + tbuf[k+1] * kernel_buf[k];
            tbuf[k] = re;
            tbuf[k+1] = im;
        }

        s->irdft_fn(s->irdft, buf, tbuf, sizeof(AVComplexFloat));
        for (k = 0; k < s->rdft_len - idx->overlap_idx; k++)
            buf[k] += obuf[k];
        memcpy(data, buf, nsamples * sizeof(*data));
        idx->buf_idx = !idx->buf_idx;
        idx->overlap_idx = nsamples;
    } else {
        while (nsamples > s->nsamples_max * 2) {
            fast_convolute_nonlinear(s, kernel_buf, conv_buf, idx, data, s->nsamples_max);
            data += s->nsamples_max;
            nsamples -= s->nsamples_max;
        }
        fast_convolute_nonlinear(s, kernel_buf, conv_buf, idx, data, nsamples/2);
        fast_convolute_nonlinear(s, kernel_buf, conv_buf, idx, data + nsamples/2, nsamples - nsamples/2);
    }
}

static void fast_convolute2(FIREqualizerContext *restrict s, const float *restrict kernel_buf, AVComplexFloat *restrict conv_buf,
                            OverlapIndex *restrict idx, float *restrict data0, float *restrict data1, int nsamples)
{
    if (nsamples <= s->nsamples_max) {
        AVComplexFloat *buf = conv_buf + idx->buf_idx * s->rdft_len;
        AVComplexFloat *obuf = conv_buf + !idx->buf_idx * s->rdft_len + idx->overlap_idx;
        AVComplexFloat *tbuf = (AVComplexFloat *)s->tx_buf;
        int center = s->fir_len/2;
        int k;
        float tmp;

        memset(buf, 0, center * sizeof(*buf));
        for (k = 0; k < nsamples; k++) {
            buf[center+k].re = data0[k];
            buf[center+k].im = data1[k];
        }
        memset(buf + center + nsamples, 0, (s->rdft_len - nsamples - center) * sizeof(*buf));
        s->fft_fn(s->fft_ctx, tbuf, buf, sizeof(AVComplexFloat));

        /* swap re <-> im, do backward fft using forward fft_ctx */
        /* normalize with 0.5f */
        tmp = tbuf[0].re;
        tbuf[0].re = 0.5f * kernel_buf[0] * tbuf[0].im;
        tbuf[0].im = 0.5f * kernel_buf[0] * tmp;
        for (k = 1; k < s->rdft_len/2; k++) {
            int m = s->rdft_len - k;
            tmp = tbuf[k].re;
            tbuf[k].re = 0.5f * kernel_buf[k] * tbuf[k].im;
            tbuf[k].im = 0.5f * kernel_buf[k] * tmp;
            tmp = tbuf[m].re;
            tbuf[m].re = 0.5f * kernel_buf[k] * tbuf[m].im;
            tbuf[m].im = 0.5f * kernel_buf[k] * tmp;
        }
        tmp = tbuf[k].re;
        tbuf[k].re = 0.5f * kernel_buf[k] * tbuf[k].im;
        tbuf[k].im = 0.5f * kernel_buf[k] * tmp;

        s->fft_fn(s->fft_ctx, buf, tbuf, sizeof(AVComplexFloat));

        for (k = 0; k < s->rdft_len - idx->overlap_idx; k++) {
            buf[k].re += obuf[k].re;
            buf[k].im += obuf[k].im;
        }

        /* swapped re <-> im */
        for (k = 0; k < nsamples; k++) {
            data0[k] = buf[k].im;
            data1[k] = buf[k].re;
        }
        idx->buf_idx = !idx->buf_idx;
        idx->overlap_idx = nsamples;
    } else {
        while (nsamples > s->nsamples_max * 2) {
            fast_convolute2(s, kernel_buf, conv_buf, idx, data0, data1, s->nsamples_max);
            data0 += s->nsamples_max;
            data1 += s->nsamples_max;
            nsamples -= s->nsamples_max;
        }
        fast_convolute2(s, kernel_buf, conv_buf, idx, data0, data1, nsamples/2);
        fast_convolute2(s, kernel_buf, conv_buf, idx, data0 + nsamples/2, data1 + nsamples/2, nsamples - nsamples/2);
    }
}

static void dump_fir(AVFilterContext *ctx, FILE *fp, int ch)
{
    FIREqualizerContext *s = ctx->priv;
    int rate = ctx->inputs[0]->sample_rate;
    int xlog = s->dumpscale == SCALE_LOGLIN || s->dumpscale == SCALE_LOGLOG;
    int ylog = s->dumpscale == SCALE_LINLOG || s->dumpscale == SCALE_LOGLOG;
    int x;
    int center = s->fir_len / 2;
    double delay = s->zero_phase ? 0.0 : (double) center / rate;
    double vx, ya, yb;

    if (!s->min_phase) {
        s->analysis_buf[0] *= s->rdft_len/2;
        for (x = 1; x <= center; x++) {
            s->analysis_buf[x] *= s->rdft_len/2;
            s->analysis_buf[s->analysis_rdft_len - x] *= s->rdft_len/2;
        }
    } else {
        for (x = 0; x < s->fir_len; x++)
            s->analysis_buf[x] *= s->rdft_len/2;
    }

    if (ch)
        fprintf(fp, "\n\n");

    fprintf(fp, "# time[%d] (time amplitude)\n", ch);

    if (!s->min_phase) {
    for (x = center; x > 0; x--)
        fprintf(fp, "%15.10f %15.10f\n", delay - (double) x / rate, (double) s->analysis_buf[s->analysis_rdft_len - x]);

    for (x = 0; x <= center; x++)
        fprintf(fp, "%15.10f %15.10f\n", delay + (double)x / rate , (double) s->analysis_buf[x]);
    } else {
        for (x = 0; x < s->fir_len; x++)
            fprintf(fp, "%15.10f %15.10f\n", (double)x / rate, (double) s->analysis_buf[x]);
    }

    s->analysis_rdft_fn(s->analysis_rdft, s->analysis_tbuf, s->analysis_buf, sizeof(float));

    fprintf(fp, "\n\n# freq[%d] (frequency desired_gain actual_gain)\n", ch);

    for (x = 0; x <= s->analysis_rdft_len/2; x++) {
        int i = 2 * x;
        vx = (double)x * rate / s->analysis_rdft_len;
        if (xlog)
            vx = log2(0.05*vx);
        ya = s->dump_buf[i];
        yb = s->min_phase ? hypotf(s->analysis_tbuf[i], s->analysis_tbuf[i+1]) : s->analysis_tbuf[i];
        if (s->min_phase)
            yb = fabs(yb);
        if (ylog) {
            ya = 20.0 * log10(fabs(ya));
            yb = 20.0 * log10(fabs(yb));
        }
        fprintf(fp, "%17.10f %17.10f %17.10f\n", vx, ya, yb);
    }
}

static double entry_func(void *p, double freq, double gain)
{
    AVFilterContext *ctx = p;
    FIREqualizerContext *s = ctx->priv;

    if (s->nb_gain_entry >= NB_GAIN_ENTRY_MAX) {
        av_log(ctx, AV_LOG_ERROR, "entry table overflow.\n");
        s->gain_entry_err = AVERROR(EINVAL);
        return 0;
    }

    if (isnan(freq)) {
        av_log(ctx, AV_LOG_ERROR, "nan frequency (%g, %g).\n", freq, gain);
        s->gain_entry_err = AVERROR(EINVAL);
        return 0;
    }

    if (s->nb_gain_entry > 0 && freq <= s->gain_entry_tbl[s->nb_gain_entry - 1].freq) {
        av_log(ctx, AV_LOG_ERROR, "unsorted frequency (%g, %g).\n", freq, gain);
        s->gain_entry_err = AVERROR(EINVAL);
        return 0;
    }

    s->gain_entry_tbl[s->nb_gain_entry].freq = freq;
    s->gain_entry_tbl[s->nb_gain_entry].gain = gain;
    s->nb_gain_entry++;
    return 0;
}

static int gain_entry_compare(const void *key, const void *memb)
{
    const double *freq = key;
    const GainEntry *entry = memb;

    if (*freq < entry[0].freq)
        return -1;
    if (*freq > entry[1].freq)
        return 1;
    return 0;
}

static double gain_interpolate_func(void *p, double freq)
{
    AVFilterContext *ctx = p;
    FIREqualizerContext *s = ctx->priv;
    GainEntry *res;
    double d0, d1, d;

    if (isnan(freq))
        return freq;

    if (!s->nb_gain_entry)
        return 0;

    if (freq <= s->gain_entry_tbl[0].freq)
        return s->gain_entry_tbl[0].gain;

    if (freq >= s->gain_entry_tbl[s->nb_gain_entry-1].freq)
        return s->gain_entry_tbl[s->nb_gain_entry-1].gain;

    res = bsearch(&freq, &s->gain_entry_tbl, s->nb_gain_entry - 1, sizeof(*res), gain_entry_compare);
    av_assert0(res);

    d  = res[1].freq - res[0].freq;
    d0 = freq - res[0].freq;
    d1 = res[1].freq - freq;

    if (d0 && d1)
        return (d0 * res[1].gain + d1 * res[0].gain) / d;

    if (d0)
        return res[1].gain;

    return res[0].gain;
}

static double cubic_interpolate_func(void *p, double freq)
{
    AVFilterContext *ctx = p;
    FIREqualizerContext *s = ctx->priv;
    GainEntry *res;
    double x, x2, x3;
    double a, b, c, d;
    double m0, m1, m2, msum, unit;

    if (!s->nb_gain_entry)
        return 0;

    if (freq <= s->gain_entry_tbl[0].freq)
        return s->gain_entry_tbl[0].gain;

    if (freq >= s->gain_entry_tbl[s->nb_gain_entry-1].freq)
        return s->gain_entry_tbl[s->nb_gain_entry-1].gain;

    res = bsearch(&freq, &s->gain_entry_tbl, s->nb_gain_entry - 1, sizeof(*res), gain_entry_compare);
    av_assert0(res);

    unit = res[1].freq - res[0].freq;
    m0 = res != s->gain_entry_tbl ?
         unit * (res[0].gain - res[-1].gain) / (res[0].freq - res[-1].freq) : 0;
    m1 = res[1].gain - res[0].gain;
    m2 = res != s->gain_entry_tbl + s->nb_gain_entry - 2 ?
         unit * (res[2].gain - res[1].gain) / (res[2].freq - res[1].freq) : 0;

    msum = fabs(m0) + fabs(m1);
    m0 = msum > 0 ? (fabs(m0) * m1 + fabs(m1) * m0) / msum : 0;
    msum = fabs(m1) + fabs(m2);
    m1 = msum > 0 ? (fabs(m1) * m2 + fabs(m2) * m1) / msum : 0;

    d = res[0].gain;
    c = m0;
    b = 3 * res[1].gain - m1 - 2 * c - 3 * d;
    a = res[1].gain - b - c - d;

    x = (freq - res[0].freq) / unit;
    x2 = x * x;
    x3 = x2 * x;

    return a * x3 + b * x2 + c * x + d;
}

static const char *const var_names[] = {
    "f",
    "sr",
    "ch",
    "chid",
    "chs",
    "chlayout",
    NULL
};

enum VarOffset {
    VAR_F,
    VAR_SR,
    VAR_CH,
    VAR_CHID,
    VAR_CHS,
    VAR_CHLAYOUT,
    VAR_NB
};

static void generate_min_phase_kernel(FIREqualizerContext *s, float *rdft_buf)
{
    int k, cepstrum_len = s->cepstrum_len, rdft_len = s->rdft_len;
    double norm = 2.0 / cepstrum_len;
    double minval = 1e-7 / rdft_len;

    memset(s->cepstrum_buf, 0, cepstrum_len * sizeof(*s->cepstrum_buf));
    memset(s->cepstrum_tbuf, 0, (cepstrum_len + 2) * sizeof(*s->cepstrum_tbuf));
    memcpy(s->cepstrum_buf, rdft_buf, rdft_len/2 * sizeof(*rdft_buf));
    memcpy(s->cepstrum_buf + cepstrum_len - rdft_len/2, rdft_buf + rdft_len/2, rdft_len/2  * sizeof(*rdft_buf));

    s->cepstrum_rdft_fn(s->cepstrum_rdft, s->cepstrum_tbuf, s->cepstrum_buf, sizeof(float));

    for (k = 0; k < cepstrum_len + 2; k += 2) {
        s->cepstrum_tbuf[k] = log(FFMAX(s->cepstrum_tbuf[k], minval));
        s->cepstrum_tbuf[k+1] = 0;
    }

    s->cepstrum_irdft_fn(s->cepstrum_irdft, s->cepstrum_buf, s->cepstrum_tbuf, sizeof(AVComplexFloat));

    memset(s->cepstrum_buf + cepstrum_len/2 + 1, 0, (cepstrum_len/2 - 1) * sizeof(*s->cepstrum_buf));
    for (k = 1; k <= cepstrum_len/2; k++)
        s->cepstrum_buf[k] *= 2;

    s->cepstrum_rdft_fn(s->cepstrum_rdft, s->cepstrum_tbuf, s->cepstrum_buf, sizeof(float));

    for (k = 0; k < cepstrum_len + 2; k += 2) {
        double mag = exp(s->cepstrum_tbuf[k] * norm) * norm;
        double ph = s->cepstrum_tbuf[k+1] * norm;
        s->cepstrum_tbuf[k] = mag * cos(ph);
        s->cepstrum_tbuf[k+1] = mag * sin(ph);
    }

    s->cepstrum_irdft_fn(s->cepstrum_irdft, s->cepstrum_buf, s->cepstrum_tbuf, sizeof(AVComplexFloat));
    memset(rdft_buf, 0, s->rdft_len * sizeof(*rdft_buf));
    memcpy(rdft_buf, s->cepstrum_buf, s->fir_len * sizeof(*rdft_buf));

    if (s->dumpfile) {
        memset(s->analysis_buf, 0, (s->analysis_rdft_len + 2) * sizeof(*s->analysis_buf));
        memcpy(s->analysis_buf, s->cepstrum_buf, s->fir_len * sizeof(*s->analysis_buf));
    }
}

static int generate_kernel(AVFilterContext *ctx, const char *gain, const char *gain_entry)
{
    FIREqualizerContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const char *gain_entry_func_names[] = { "entry", NULL };
    const char *gain_func_names[] = { "gain_interpolate", "cubic_interpolate", NULL };
    double (*gain_entry_funcs[])(void *, double, double) = { entry_func, NULL };
    double (*gain_funcs[])(void *, double) = { gain_interpolate_func, cubic_interpolate_func, NULL };
    double vars[VAR_NB];
    AVExpr *gain_expr;
    int ret, k, center, ch;
    int xlog = s->scale == SCALE_LOGLIN || s->scale == SCALE_LOGLOG;
    int ylog = s->scale == SCALE_LINLOG || s->scale == SCALE_LOGLOG;
    FILE *dump_fp = NULL;

    s->nb_gain_entry = 0;
    s->gain_entry_err = 0;
    if (gain_entry) {
        double result = 0.0;
        ret = av_expr_parse_and_eval(&result, gain_entry, NULL, NULL, NULL, NULL,
                                     gain_entry_func_names, gain_entry_funcs, ctx, 0, ctx);
        if (ret < 0)
            return ret;
        if (s->gain_entry_err < 0)
            return s->gain_entry_err;
    }

    av_log(ctx, AV_LOG_DEBUG, "nb_gain_entry = %d.\n", s->nb_gain_entry);

    ret = av_expr_parse(&gain_expr, gain, var_names,
                        gain_func_names, gain_funcs, NULL, NULL, 0, ctx);
    if (ret < 0)
        return ret;

    if (s->dumpfile && (!s->dump_buf || !s->analysis_rdft || !(dump_fp = avpriv_fopen_utf8(s->dumpfile, "w"))))
        av_log(ctx, AV_LOG_WARNING, "dumping failed.\n");

    vars[VAR_CHS] = inlink->ch_layout.nb_channels;
    vars[VAR_CHLAYOUT] = inlink->ch_layout.order == AV_CHANNEL_ORDER_NATIVE ?
                         inlink->ch_layout.u.mask : 0;
    vars[VAR_SR] = inlink->sample_rate;
    for (ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
        float *rdft_buf = s->kernel_tmp_buf + ch * (s->rdft_len * 2);
        float *rdft_tbuf = s->kernel_tmp_tbuf;
        double result;
        vars[VAR_CH] = ch;
        vars[VAR_CHID] = av_channel_layout_channel_from_index(&inlink->ch_layout, ch);

        for (k = 0; k <= s->analysis_rdft_len/2; k++) {
            vars[VAR_F] = k * ((double)inlink->sample_rate /(double)s->analysis_rdft_len);
            if (xlog)
                vars[VAR_F] = log2(0.05 * vars[VAR_F]);
            result = av_expr_eval(gain_expr, vars, ctx);
            s->analysis_tbuf[2*k] = ylog ? pow(10.0, 0.05 * result) : s->min_phase ? fabs(result) : result;
            s->analysis_tbuf[2*k+1] = 0.0;
        }

        if (s->dump_buf)
            memcpy(s->dump_buf, s->analysis_tbuf, (s->analysis_rdft_len + 2) * sizeof(*s->analysis_tbuf));

        s->analysis_irdft_fn(s->analysis_irdft, s->analysis_buf, s->analysis_tbuf, sizeof(AVComplexFloat));
        center = s->fir_len / 2;

        for (k = 0; k <= center; k++) {
            double u = k * (M_PI/center);
            double win;
            switch (s->wfunc) {
            case WFUNC_RECTANGULAR:
                win = 1.0;
                break;
            case WFUNC_HANN:
                win = 0.5 + 0.5 * cos(u);
                break;
            case WFUNC_HAMMING:
                win = 0.53836 + 0.46164 * cos(u);
                break;
            case WFUNC_BLACKMAN:
                win = 0.42 + 0.5 * cos(u) + 0.08 * cos(2*u);
                break;
            case WFUNC_NUTTALL3:
                win = 0.40897 + 0.5 * cos(u) + 0.09103 * cos(2*u);
                break;
            case WFUNC_MNUTTALL3:
                win = 0.4243801 + 0.4973406 * cos(u) + 0.0782793 * cos(2*u);
                break;
            case WFUNC_NUTTALL:
                win = 0.355768 + 0.487396 * cos(u) + 0.144232 * cos(2*u) + 0.012604 * cos(3*u);
                break;
            case WFUNC_BNUTTALL:
                win = 0.3635819 + 0.4891775 * cos(u) + 0.1365995 * cos(2*u) + 0.0106411 * cos(3*u);
                break;
            case WFUNC_BHARRIS:
                win = 0.35875 + 0.48829 * cos(u) + 0.14128 * cos(2*u) + 0.01168 * cos(3*u);
                break;
            case WFUNC_TUKEY:
                win = (u <= 0.5 * M_PI) ? 1.0 : (0.5 + 0.5 * cos(2*u - M_PI));
                break;
            default:
                av_assert0(0);
            }
            s->analysis_buf[k] *= (2.0/s->analysis_rdft_len) * (2.0/s->rdft_len) * win;
            if (k)
                s->analysis_buf[s->analysis_rdft_len - k] = s->analysis_buf[k];
        }

        memset(s->analysis_buf + center + 1, 0, (s->analysis_rdft_len - s->fir_len) * sizeof(*s->analysis_buf));
        memcpy(rdft_tbuf, s->analysis_buf, s->rdft_len/2 * sizeof(*s->analysis_buf));
        memcpy(rdft_tbuf + s->rdft_len/2, s->analysis_buf + s->analysis_rdft_len - s->rdft_len/2, s->rdft_len/2 * sizeof(*s->analysis_buf));
        if (s->min_phase)
            generate_min_phase_kernel(s, rdft_tbuf);
        s->rdft_fn(s->rdft, rdft_buf, rdft_tbuf, sizeof(float));

        for (k = 0; k < s->rdft_len + 2; k++) {
            if (isnan(rdft_buf[k]) || isinf(rdft_buf[k])) {
                av_log(ctx, AV_LOG_ERROR, "filter kernel contains nan or infinity.\n");
                av_expr_free(gain_expr);
                if (dump_fp)
                    fclose(dump_fp);
                return AVERROR(EINVAL);
            }
        }

        if (!s->min_phase) {
            for (k = 0; k <= s->rdft_len/2; k++)
                rdft_buf[k] = rdft_buf[2*k];
        }

        if (dump_fp)
            dump_fir(ctx, dump_fp, ch);

        if (!s->multi)
            break;
    }

    memcpy(s->kernel_buf, s->kernel_tmp_buf, (s->multi ? inlink->ch_layout.nb_channels : 1) * (s->rdft_len * 2) * sizeof(*s->kernel_buf));
    av_expr_free(gain_expr);
    if (dump_fp)
        fclose(dump_fp);
    return 0;
}

#define SELECT_GAIN(s) (s->gain_cmd ? s->gain_cmd : s->gain)
#define SELECT_GAIN_ENTRY(s) (s->gain_entry_cmd ? s->gain_entry_cmd : s->gain_entry)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FIREqualizerContext *s = ctx->priv;
    float iscale, scale = 1.f;
    int rdft_bits, ret;

    common_uninit(s);

    s->next_pts = 0;
    s->frame_nsamples_max = 0;

    s->fir_len = FFMAX(2 * (int)(inlink->sample_rate * s->delay) + 1, 3);
    s->remaining = s->fir_len - 1;

    for (rdft_bits = RDFT_BITS_MIN; rdft_bits <= RDFT_BITS_MAX; rdft_bits++) {
        s->rdft_len = 1 << rdft_bits;
        s->nsamples_max = s->rdft_len - s->fir_len + 1;
        if (s->nsamples_max * 2 >= s->fir_len)
            break;
    }

    if (rdft_bits > RDFT_BITS_MAX) {
        av_log(ctx, AV_LOG_ERROR, "too large delay, please decrease it.\n");
        return AVERROR(EINVAL);
    }

    iscale = 0.5f;
    if (((ret = av_tx_init(&s->rdft,  &s->rdft_fn,  AV_TX_FLOAT_RDFT, 0, 1 << rdft_bits, &scale,  0)) < 0) ||
        ((ret = av_tx_init(&s->irdft, &s->irdft_fn, AV_TX_FLOAT_RDFT, 1, 1 << rdft_bits, &iscale, 0)) < 0))
        return ret;

    scale = 1.f;
    if (s->fft2 && !s->multi && inlink->ch_layout.nb_channels > 1 &&
        ((ret = av_tx_init(&s->fft_ctx, &s->fft_fn, AV_TX_FLOAT_FFT, 0, 1 << rdft_bits, &scale, 0)) < 0))
        return ret;

    if (s->min_phase) {
        int cepstrum_bits = rdft_bits + 2;
        if (cepstrum_bits > RDFT_BITS_MAX) {
            av_log(ctx, AV_LOG_ERROR, "too large delay, please decrease it.\n");
            return AVERROR(EINVAL);
        }

        cepstrum_bits = FFMIN(RDFT_BITS_MAX, cepstrum_bits + 1);
        scale = 1.f;
        ret = av_tx_init(&s->cepstrum_rdft,  &s->cepstrum_rdft_fn,  AV_TX_FLOAT_RDFT, 0, 1 << cepstrum_bits, &scale, 0);
        if (ret < 0)
            return ret;

        iscale = 0.5f;
        ret = av_tx_init(&s->cepstrum_irdft, &s->cepstrum_irdft_fn, AV_TX_FLOAT_RDFT, 1, 1 << cepstrum_bits, &iscale, 0);
        if (ret < 0)
            return ret;

        s->cepstrum_len = 1 << cepstrum_bits;
        s->cepstrum_buf = av_malloc_array(s->cepstrum_len, sizeof(*s->cepstrum_buf));
        if (!s->cepstrum_buf)
            return AVERROR(ENOMEM);
        s->cepstrum_tbuf = av_malloc_array(s->cepstrum_len + 2, sizeof(*s->cepstrum_tbuf));
        if (!s->cepstrum_tbuf)
            return AVERROR(ENOMEM);
    }

    for ( ; rdft_bits <= RDFT_BITS_MAX; rdft_bits++) {
        s->analysis_rdft_len = 1 << rdft_bits;
        if (inlink->sample_rate <= s->accuracy * s->analysis_rdft_len)
            break;
    }

    if (rdft_bits > RDFT_BITS_MAX) {
        av_log(ctx, AV_LOG_ERROR, "too small accuracy, please increase it.\n");
        return AVERROR(EINVAL);
    }

    iscale = 0.5f;
    if ((ret = av_tx_init(&s->analysis_irdft, &s->analysis_irdft_fn, AV_TX_FLOAT_RDFT, 1, 1 << rdft_bits, &iscale, 0)) < 0)
        return ret;

    if (s->dumpfile) {
        scale = 1.f;
        if ((ret = av_tx_init(&s->analysis_rdft, &s->analysis_rdft_fn, AV_TX_FLOAT_RDFT, 0, 1 << rdft_bits, &scale, 0)) < 0)
            return ret;
        s->dump_buf = av_malloc_array(s->analysis_rdft_len + 2, sizeof(*s->dump_buf));
    }

    s->analysis_buf = av_malloc_array((s->analysis_rdft_len + 2), sizeof(*s->analysis_buf));
    s->analysis_tbuf = av_malloc_array(s->analysis_rdft_len + 2, sizeof(*s->analysis_tbuf));
    s->kernel_tmp_buf = av_malloc_array((s->rdft_len * 2) * (s->multi ? inlink->ch_layout.nb_channels : 1), sizeof(*s->kernel_tmp_buf));
    s->kernel_tmp_tbuf = av_malloc_array(s->rdft_len, sizeof(*s->kernel_tmp_tbuf));
    s->kernel_buf = av_malloc_array((s->rdft_len * 2) * (s->multi ? inlink->ch_layout.nb_channels : 1), sizeof(*s->kernel_buf));
    s->tx_buf = av_malloc_array(2 * (s->rdft_len + 2), sizeof(*s->kernel_buf));
    s->conv_buf   = av_calloc(2 * s->rdft_len * inlink->ch_layout.nb_channels, sizeof(*s->conv_buf));
    s->conv_idx   = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->conv_idx));
    if (!s->analysis_buf || !s->analysis_tbuf || !s->kernel_tmp_buf || !s->kernel_buf || !s->conv_buf || !s->conv_idx || !s->kernel_tmp_tbuf || !s->tx_buf)
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_DEBUG, "sample_rate = %d, channels = %d, analysis_rdft_len = %d, rdft_len = %d, fir_len = %d, nsamples_max = %d.\n",
           inlink->sample_rate, inlink->ch_layout.nb_channels, s->analysis_rdft_len, s->rdft_len, s->fir_len, s->nsamples_max);

    if (s->fixed)
        inlink->min_samples = inlink->max_samples = s->nsamples_max;

    return generate_kernel(ctx, SELECT_GAIN(s), SELECT_GAIN_ENTRY(s));
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    FIREqualizerContext *s = ctx->priv;
    int ch;

    if (!s->min_phase) {
        for (ch = 0; ch + 1 < inlink->ch_layout.nb_channels && s->fft_ctx; ch += 2) {
            fast_convolute2(s, s->kernel_buf, (AVComplexFloat *)(s->conv_buf + 2 * ch * s->rdft_len),
                            s->conv_idx + ch, (float *) frame->extended_data[ch],
                            (float *) frame->extended_data[ch+1], frame->nb_samples);
        }

        for ( ; ch < inlink->ch_layout.nb_channels; ch++) {
            fast_convolute(s, s->kernel_buf + (s->multi ? ch * (s->rdft_len * 2) : 0),
                        s->conv_buf + 2 * ch * s->rdft_len, s->conv_idx + ch,
                        (float *) frame->extended_data[ch], frame->nb_samples);
        }
    } else {
        for (ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
            fast_convolute_nonlinear(s, s->kernel_buf + (s->multi ? ch * (s->rdft_len * 2) : 0),
                                     s->conv_buf + 2 * ch * s->rdft_len, s->conv_idx + ch,
                                     (float *) frame->extended_data[ch], frame->nb_samples);
        }
    }

    s->next_pts = AV_NOPTS_VALUE;
    if (frame->pts != AV_NOPTS_VALUE) {
        s->next_pts = frame->pts + av_rescale_q(frame->nb_samples, av_make_q(1, inlink->sample_rate), inlink->time_base);
        if (s->zero_phase && !s->min_phase)
            frame->pts -= av_rescale_q(s->fir_len/2, av_make_q(1, inlink->sample_rate), inlink->time_base);
    }
    s->frame_nsamples_max = FFMAX(s->frame_nsamples_max, frame->nb_samples);
    return ff_filter_frame(ctx->outputs[0], frame);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FIREqualizerContext *s= ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->remaining > 0 && s->frame_nsamples_max > 0) {
        AVFrame *frame = ff_get_audio_buffer(outlink, FFMIN(s->remaining, s->frame_nsamples_max));

        if (!frame)
            return AVERROR(ENOMEM);

        av_samples_set_silence(frame->extended_data, 0, frame->nb_samples, outlink->ch_layout.nb_channels, frame->format);
        frame->pts = s->next_pts;
        s->remaining -= frame->nb_samples;
        ret = filter_frame(ctx->inputs[0], frame);
    }

    return ret;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    FIREqualizerContext *s = ctx->priv;
    int ret = AVERROR(ENOSYS);

    if (!strcmp(cmd, "gain")) {
        char *gain_cmd;

        if (SELECT_GAIN(s) && !strcmp(SELECT_GAIN(s), args)) {
            av_log(ctx, AV_LOG_DEBUG, "equal gain, do not rebuild.\n");
            return 0;
        }

        gain_cmd = av_strdup(args);
        if (!gain_cmd)
            return AVERROR(ENOMEM);

        ret = generate_kernel(ctx, gain_cmd, SELECT_GAIN_ENTRY(s));
        if (ret >= 0) {
            av_freep(&s->gain_cmd);
            s->gain_cmd = gain_cmd;
        } else {
            av_freep(&gain_cmd);
        }
    } else if (!strcmp(cmd, "gain_entry")) {
        char *gain_entry_cmd;

        if (SELECT_GAIN_ENTRY(s) && !strcmp(SELECT_GAIN_ENTRY(s), args)) {
            av_log(ctx, AV_LOG_DEBUG, "equal gain_entry, do not rebuild.\n");
            return 0;
        }

        gain_entry_cmd = av_strdup(args);
        if (!gain_entry_cmd)
            return AVERROR(ENOMEM);

        ret = generate_kernel(ctx, SELECT_GAIN(s), gain_entry_cmd);
        if (ret >= 0) {
            av_freep(&s->gain_entry_cmd);
            s->gain_entry_cmd = gain_entry_cmd;
        } else {
            av_freep(&gain_entry_cmd);
        }
    }

    return ret;
}

static const AVFilterPad firequalizer_inputs[] = {
    {
        .name           = "default",
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
        .type           = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad firequalizer_outputs[] = {
    {
        .name           = "default",
        .request_frame  = request_frame,
        .type           = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_firequalizer = {
    .name               = "firequalizer",
    .description        = NULL_IF_CONFIG_SMALL("Finite Impulse Response Equalizer."),
    .uninit             = uninit,
    .process_command    = process_command,
    .priv_size          = sizeof(FIREqualizerContext),
    FILTER_INPUTS(firequalizer_inputs),
    FILTER_OUTPUTS(firequalizer_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
    .priv_class         = &firequalizer_class,
};
