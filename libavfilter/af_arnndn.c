/*
 * Copyright (c) 2018 Gregor Richards
 * Copyright (c) 2017 Mozilla
 * Copyright (c) 2005-2009 Xiph.Org Foundation
 * Copyright (c) 2007-2008 CSIRO
 * Copyright (c) 2008-2011 Octasic Inc.
 * Copyright (c) Jean-Marc Valin
 * Copyright (c) 2019 Paul B Mahol
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/tx.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"
#include "formats.h"

#define FRAME_SIZE_SHIFT 2
#define FRAME_SIZE (120<<FRAME_SIZE_SHIFT)
#define WINDOW_SIZE (2*FRAME_SIZE)
#define FREQ_SIZE (FRAME_SIZE + 1)

#define PITCH_MIN_PERIOD 60
#define PITCH_MAX_PERIOD 768
#define PITCH_FRAME_SIZE 960
#define PITCH_BUF_SIZE (PITCH_MAX_PERIOD+PITCH_FRAME_SIZE)

#define SQUARE(x) ((x)*(x))

#define NB_BANDS 22

#define CEPS_MEM 8
#define NB_DELTA_CEPS 6

#define NB_FEATURES (NB_BANDS+3*NB_DELTA_CEPS+2)

#define WEIGHTS_SCALE (1.f/256)

#define MAX_NEURONS 128

#define ACTIVATION_TANH    0
#define ACTIVATION_SIGMOID 1
#define ACTIVATION_RELU    2

#define Q15ONE 1.0f

typedef struct DenseLayer {
    const float *bias;
    const float *input_weights;
    int nb_inputs;
    int nb_neurons;
    int activation;
} DenseLayer;

typedef struct GRULayer {
    const float *bias;
    const float *input_weights;
    const float *recurrent_weights;
    int nb_inputs;
    int nb_neurons;
    int activation;
} GRULayer;

typedef struct RNNModel {
    int input_dense_size;
    const DenseLayer *input_dense;

    int vad_gru_size;
    const GRULayer *vad_gru;

    int noise_gru_size;
    const GRULayer *noise_gru;

    int denoise_gru_size;
    const GRULayer *denoise_gru;

    int denoise_output_size;
    const DenseLayer *denoise_output;

    int vad_output_size;
    const DenseLayer *vad_output;
} RNNModel;

typedef struct RNNState {
    float *vad_gru_state;
    float *noise_gru_state;
    float *denoise_gru_state;
    RNNModel *model;
} RNNState;

typedef struct DenoiseState {
    float analysis_mem[FRAME_SIZE];
    float cepstral_mem[CEPS_MEM][NB_BANDS];
    int memid;
    DECLARE_ALIGNED(32, float, synthesis_mem)[FRAME_SIZE];
    float pitch_buf[PITCH_BUF_SIZE];
    float pitch_enh_buf[PITCH_BUF_SIZE];
    float last_gain;
    int last_period;
    float mem_hp_x[2];
    float lastg[NB_BANDS];
    float history[FRAME_SIZE];
    RNNState rnn[2];
    AVTXContext *tx, *txi;
    av_tx_fn tx_fn, txi_fn;
} DenoiseState;

typedef struct AudioRNNContext {
    const AVClass *class;

    char *model_name;
    float mix;

    int channels;
    DenoiseState *st;

    DECLARE_ALIGNED(32, float, window)[WINDOW_SIZE];
    DECLARE_ALIGNED(32, float, dct_table)[FFALIGN(NB_BANDS, 4)][FFALIGN(NB_BANDS, 4)];

    RNNModel *model[2];

    AVFloatDSPContext *fdsp;
} AudioRNNContext;

#define F_ACTIVATION_TANH       0
#define F_ACTIVATION_SIGMOID    1
#define F_ACTIVATION_RELU       2

static void rnnoise_model_free(RNNModel *model)
{
#define FREE_MAYBE(ptr) do { if (ptr) free(ptr); } while (0)
#define FREE_DENSE(name) do { \
    if (model->name) { \
        av_free((void *) model->name->input_weights); \
        av_free((void *) model->name->bias); \
        av_free((void *) model->name); \
    } \
    } while (0)
#define FREE_GRU(name) do { \
    if (model->name) { \
        av_free((void *) model->name->input_weights); \
        av_free((void *) model->name->recurrent_weights); \
        av_free((void *) model->name->bias); \
        av_free((void *) model->name); \
    } \
    } while (0)

    if (!model)
        return;
    FREE_DENSE(input_dense);
    FREE_GRU(vad_gru);
    FREE_GRU(noise_gru);
    FREE_GRU(denoise_gru);
    FREE_DENSE(denoise_output);
    FREE_DENSE(vad_output);
    av_free(model);
}

static int rnnoise_model_from_file(FILE *f, RNNModel **rnn)
{
    RNNModel *ret = NULL;
    DenseLayer *input_dense;
    GRULayer *vad_gru;
    GRULayer *noise_gru;
    GRULayer *denoise_gru;
    DenseLayer *denoise_output;
    DenseLayer *vad_output;
    int in;

    if (fscanf(f, "rnnoise-nu model file version %d\n", &in) != 1 || in != 1)
        return AVERROR_INVALIDDATA;

    ret = av_calloc(1, sizeof(RNNModel));
    if (!ret)
        return AVERROR(ENOMEM);

#define ALLOC_LAYER(type, name) \
    name = av_calloc(1, sizeof(type)); \
    if (!name) { \
        rnnoise_model_free(ret); \
        return AVERROR(ENOMEM); \
    } \
    ret->name = name

    ALLOC_LAYER(DenseLayer, input_dense);
    ALLOC_LAYER(GRULayer, vad_gru);
    ALLOC_LAYER(GRULayer, noise_gru);
    ALLOC_LAYER(GRULayer, denoise_gru);
    ALLOC_LAYER(DenseLayer, denoise_output);
    ALLOC_LAYER(DenseLayer, vad_output);

#define INPUT_VAL(name) do { \
    if (fscanf(f, "%d", &in) != 1 || in < 0 || in > 128) { \
        rnnoise_model_free(ret); \
        return AVERROR(EINVAL); \
    } \
    name = in; \
    } while (0)

#define INPUT_ACTIVATION(name) do { \
    int activation; \
    INPUT_VAL(activation); \
    switch (activation) { \
    case F_ACTIVATION_SIGMOID: \
        name = ACTIVATION_SIGMOID; \
        break; \
    case F_ACTIVATION_RELU: \
        name = ACTIVATION_RELU; \
        break; \
    default: \
        name = ACTIVATION_TANH; \
    } \
    } while (0)

#define INPUT_ARRAY(name, len) do { \
    float *values = av_calloc((len), sizeof(float)); \
    if (!values) { \
        rnnoise_model_free(ret); \
        return AVERROR(ENOMEM); \
    } \
    name = values; \
    for (int i = 0; i < (len); i++) { \
        if (fscanf(f, "%d", &in) != 1) { \
            rnnoise_model_free(ret); \
            return AVERROR(EINVAL); \
        } \
        values[i] = in; \
    } \
    } while (0)

#define INPUT_ARRAY3(name, len0, len1, len2) do { \
    float *values = av_calloc(FFALIGN((len0), 4) * FFALIGN((len1), 4) * (len2), sizeof(float)); \
    if (!values) { \
        rnnoise_model_free(ret); \
        return AVERROR(ENOMEM); \
    } \
    name = values; \
    for (int k = 0; k < (len0); k++) { \
        for (int i = 0; i < (len2); i++) { \
            for (int j = 0; j < (len1); j++) { \
                if (fscanf(f, "%d", &in) != 1) { \
                    rnnoise_model_free(ret); \
                    return AVERROR(EINVAL); \
                } \
                values[j * (len2) * FFALIGN((len0), 4) + i * FFALIGN((len0), 4) + k] = in; \
            } \
        } \
    } \
    } while (0)

#define NEW_LINE() do { \
    int c; \
    while ((c = fgetc(f)) != EOF) { \
        if (c == '\n') \
        break; \
    } \
    } while (0)

#define INPUT_DENSE(name) do { \
    INPUT_VAL(name->nb_inputs); \
    INPUT_VAL(name->nb_neurons); \
    ret->name ## _size = name->nb_neurons; \
    INPUT_ACTIVATION(name->activation); \
    NEW_LINE(); \
    INPUT_ARRAY(name->input_weights, name->nb_inputs * name->nb_neurons); \
    NEW_LINE(); \
    INPUT_ARRAY(name->bias, name->nb_neurons); \
    NEW_LINE(); \
    } while (0)

#define INPUT_GRU(name) do { \
    INPUT_VAL(name->nb_inputs); \
    INPUT_VAL(name->nb_neurons); \
    ret->name ## _size = name->nb_neurons; \
    INPUT_ACTIVATION(name->activation); \
    NEW_LINE(); \
    INPUT_ARRAY3(name->input_weights, name->nb_inputs, name->nb_neurons, 3); \
    NEW_LINE(); \
    INPUT_ARRAY3(name->recurrent_weights, name->nb_neurons, name->nb_neurons, 3); \
    NEW_LINE(); \
    INPUT_ARRAY(name->bias, name->nb_neurons * 3); \
    NEW_LINE(); \
    } while (0)

    INPUT_DENSE(input_dense);
    INPUT_GRU(vad_gru);
    INPUT_GRU(noise_gru);
    INPUT_GRU(denoise_gru);
    INPUT_DENSE(denoise_output);
    INPUT_DENSE(vad_output);

    if (vad_output->nb_neurons != 1) {
        rnnoise_model_free(ret);
        return AVERROR(EINVAL);
    }

    *rnn = ret;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret, sample_rates[] = { 48000, -1 };

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_rates);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioRNNContext *s = ctx->priv;
    int ret;

    s->channels = inlink->channels;

    if (!s->st)
        s->st = av_calloc(s->channels, sizeof(DenoiseState));
    if (!s->st)
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->channels; i++) {
        DenoiseState *st = &s->st[i];

        st->rnn[0].model = s->model[0];
        st->rnn[0].vad_gru_state = av_calloc(sizeof(float), FFALIGN(s->model[0]->vad_gru_size, 16));
        st->rnn[0].noise_gru_state = av_calloc(sizeof(float), FFALIGN(s->model[0]->noise_gru_size, 16));
        st->rnn[0].denoise_gru_state = av_calloc(sizeof(float), FFALIGN(s->model[0]->denoise_gru_size, 16));
        if (!st->rnn[0].vad_gru_state ||
            !st->rnn[0].noise_gru_state ||
            !st->rnn[0].denoise_gru_state)
            return AVERROR(ENOMEM);
    }

    for (int i = 0; i < s->channels; i++) {
        DenoiseState *st = &s->st[i];

        if (!st->tx)
            ret = av_tx_init(&st->tx, &st->tx_fn, AV_TX_FLOAT_FFT, 0, WINDOW_SIZE, NULL, 0);
        if (ret < 0)
            return ret;

        if (!st->txi)
            ret = av_tx_init(&st->txi, &st->txi_fn, AV_TX_FLOAT_FFT, 1, WINDOW_SIZE, NULL, 0);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void biquad(float *y, float mem[2], const float *x,
                   const float *b, const float *a, int N)
{
    for (int i = 0; i < N; i++) {
        float xi, yi;

        xi = x[i];
        yi = x[i] + mem[0];
        mem[0] = mem[1] + (b[0]*xi - a[0]*yi);
        mem[1] = (b[1]*xi - a[1]*yi);
        y[i] = yi;
    }
}

#define RNN_MOVE(dst, src, n) (memmove((dst), (src), (n)*sizeof(*(dst)) + 0*((dst)-(src)) ))
#define RNN_CLEAR(dst, n) (memset((dst), 0, (n)*sizeof(*(dst))))
#define RNN_COPY(dst, src, n) (memcpy((dst), (src), (n)*sizeof(*(dst)) + 0*((dst)-(src)) ))

static void forward_transform(DenoiseState *st, AVComplexFloat *out, const float *in)
{
    AVComplexFloat x[WINDOW_SIZE];
    AVComplexFloat y[WINDOW_SIZE];

    for (int i = 0; i < WINDOW_SIZE; i++) {
        x[i].re = in[i];
        x[i].im = 0;
    }

    st->tx_fn(st->tx, y, x, sizeof(float));

    RNN_COPY(out, y, FREQ_SIZE);
}

static void inverse_transform(DenoiseState *st, float *out, const AVComplexFloat *in)
{
    AVComplexFloat x[WINDOW_SIZE];
    AVComplexFloat y[WINDOW_SIZE];

    RNN_COPY(x, in, FREQ_SIZE);

    for (int i = FREQ_SIZE; i < WINDOW_SIZE; i++) {
        x[i].re =  x[WINDOW_SIZE - i].re;
        x[i].im = -x[WINDOW_SIZE - i].im;
    }

    st->txi_fn(st->txi, y, x, sizeof(float));

    for (int i = 0; i < WINDOW_SIZE; i++)
        out[i] = y[i].re / WINDOW_SIZE;
}

static const uint8_t eband5ms[] = {
/*0  200 400 600 800  1k 1.2 1.4 1.6  2k 2.4 2.8 3.2  4k 4.8 5.6 6.8  8k 9.6 12k 15.6 20k*/
  0,  1,  2,  3,  4,   5, 6,  7,  8,  10, 12, 14, 16, 20, 24, 28, 34, 40, 48, 60, 78, 100
};

static void compute_band_energy(float *bandE, const AVComplexFloat *X)
{
    float sum[NB_BANDS] = {0};

    for (int i = 0; i < NB_BANDS - 1; i++) {
        int band_size;

        band_size = (eband5ms[i + 1] - eband5ms[i]) << FRAME_SIZE_SHIFT;
        for (int j = 0; j < band_size; j++) {
            float tmp, frac = (float)j / band_size;

            tmp         = SQUARE(X[(eband5ms[i] << FRAME_SIZE_SHIFT) + j].re);
            tmp        += SQUARE(X[(eband5ms[i] << FRAME_SIZE_SHIFT) + j].im);
            sum[i]     += (1.f - frac) * tmp;
            sum[i + 1] +=        frac  * tmp;
        }
    }

    sum[0] *= 2;
    sum[NB_BANDS - 1] *= 2;

    for (int i = 0; i < NB_BANDS; i++)
        bandE[i] = sum[i];
}

static void compute_band_corr(float *bandE, const AVComplexFloat *X, const AVComplexFloat *P)
{
    float sum[NB_BANDS] = { 0 };

    for (int i = 0; i < NB_BANDS - 1; i++) {
        int band_size;

        band_size = (eband5ms[i + 1] - eband5ms[i]) << FRAME_SIZE_SHIFT;
        for (int j = 0; j < band_size; j++) {
            float tmp, frac = (float)j / band_size;

            tmp  = X[(eband5ms[i]<<FRAME_SIZE_SHIFT) + j].re * P[(eband5ms[i]<<FRAME_SIZE_SHIFT) + j].re;
            tmp += X[(eband5ms[i]<<FRAME_SIZE_SHIFT) + j].im * P[(eband5ms[i]<<FRAME_SIZE_SHIFT) + j].im;
            sum[i]     += (1 - frac) * tmp;
            sum[i + 1] +=      frac  * tmp;
        }
    }

    sum[0] *= 2;
    sum[NB_BANDS-1] *= 2;

    for (int i = 0; i < NB_BANDS; i++)
        bandE[i] = sum[i];
}

static void frame_analysis(AudioRNNContext *s, DenoiseState *st, AVComplexFloat *X, float *Ex, const float *in)
{
    LOCAL_ALIGNED_32(float, x, [WINDOW_SIZE]);

    RNN_COPY(x, st->analysis_mem, FRAME_SIZE);
    RNN_COPY(x + FRAME_SIZE, in, FRAME_SIZE);
    RNN_COPY(st->analysis_mem, in, FRAME_SIZE);
    s->fdsp->vector_fmul(x, x, s->window, WINDOW_SIZE);
    forward_transform(st, X, x);
    compute_band_energy(Ex, X);
}

static void frame_synthesis(AudioRNNContext *s, DenoiseState *st, float *out, const AVComplexFloat *y)
{
    LOCAL_ALIGNED_32(float, x, [WINDOW_SIZE]);
    const float *src = st->history;
    const float mix = s->mix;
    const float imix = 1.f - FFMAX(mix, 0.f);

    inverse_transform(st, x, y);
    s->fdsp->vector_fmul(x, x, s->window, WINDOW_SIZE);
    s->fdsp->vector_fmac_scalar(x, st->synthesis_mem, 1.f, FRAME_SIZE);
    RNN_COPY(out, x, FRAME_SIZE);
    RNN_COPY(st->synthesis_mem, &x[FRAME_SIZE], FRAME_SIZE);

    for (int n = 0; n < FRAME_SIZE; n++)
        out[n] = out[n] * mix + src[n] * imix;
}

static inline void xcorr_kernel(const float *x, const float *y, float sum[4], int len)
{
    float y_0, y_1, y_2, y_3 = 0;
    int j;

    y_0 = *y++;
    y_1 = *y++;
    y_2 = *y++;

    for (j = 0; j < len - 3; j += 4) {
        float tmp;

        tmp = *x++;
        y_3 = *y++;
        sum[0] += tmp * y_0;
        sum[1] += tmp * y_1;
        sum[2] += tmp * y_2;
        sum[3] += tmp * y_3;
        tmp = *x++;
        y_0 = *y++;
        sum[0] += tmp * y_1;
        sum[1] += tmp * y_2;
        sum[2] += tmp * y_3;
        sum[3] += tmp * y_0;
        tmp = *x++;
        y_1 = *y++;
        sum[0] += tmp * y_2;
        sum[1] += tmp * y_3;
        sum[2] += tmp * y_0;
        sum[3] += tmp * y_1;
        tmp = *x++;
        y_2 = *y++;
        sum[0] += tmp * y_3;
        sum[1] += tmp * y_0;
        sum[2] += tmp * y_1;
        sum[3] += tmp * y_2;
    }

    if (j++ < len) {
        float tmp = *x++;

        y_3 = *y++;
        sum[0] += tmp * y_0;
        sum[1] += tmp * y_1;
        sum[2] += tmp * y_2;
        sum[3] += tmp * y_3;
    }

    if (j++ < len) {
        float tmp=*x++;

        y_0 = *y++;
        sum[0] += tmp * y_1;
        sum[1] += tmp * y_2;
        sum[2] += tmp * y_3;
        sum[3] += tmp * y_0;
    }

    if (j < len) {
        float tmp=*x++;

        y_1 = *y++;
        sum[0] += tmp * y_2;
        sum[1] += tmp * y_3;
        sum[2] += tmp * y_0;
        sum[3] += tmp * y_1;
    }
}

static inline float celt_inner_prod(const float *x,
                                    const float *y, int N)
{
    float xy = 0.f;

    for (int i = 0; i < N; i++)
        xy += x[i] * y[i];

    return xy;
}

static void celt_pitch_xcorr(const float *x, const float *y,
                             float *xcorr, int len, int max_pitch)
{
    int i;

    for (i = 0; i < max_pitch - 3; i += 4) {
        float sum[4] = { 0, 0, 0, 0};

        xcorr_kernel(x, y + i, sum, len);

        xcorr[i]     = sum[0];
        xcorr[i + 1] = sum[1];
        xcorr[i + 2] = sum[2];
        xcorr[i + 3] = sum[3];
    }
    /* In case max_pitch isn't a multiple of 4, do non-unrolled version. */
    for (; i < max_pitch; i++) {
        xcorr[i] = celt_inner_prod(x, y + i, len);
    }
}

static int celt_autocorr(const float *x,   /*  in: [0...n-1] samples x   */
                         float       *ac,  /* out: [0...lag-1] ac values */
                         const float *window,
                         int          overlap,
                         int          lag,
                         int          n)
{
    int fastN = n - lag;
    int shift;
    const float *xptr;
    float xx[PITCH_BUF_SIZE>>1];

    if (overlap == 0) {
        xptr = x;
    } else {
        for (int i = 0; i < n; i++)
            xx[i] = x[i];
        for (int i = 0; i < overlap; i++) {
            xx[i] = x[i] * window[i];
            xx[n-i-1] = x[n-i-1] * window[i];
        }
        xptr = xx;
    }

    shift = 0;
    celt_pitch_xcorr(xptr, xptr, ac, fastN, lag+1);

    for (int k = 0; k <= lag; k++) {
        float d = 0.f;

        for (int i = k + fastN; i < n; i++)
            d += xptr[i] * xptr[i-k];
        ac[k] += d;
    }

    return shift;
}

static void celt_lpc(float *lpc, /* out: [0...p-1] LPC coefficients      */
                const float *ac,   /* in:  [0...p] autocorrelation values  */
                          int p)
{
    float r, error = ac[0];

    RNN_CLEAR(lpc, p);
    if (ac[0] != 0) {
        for (int i = 0; i < p; i++) {
            /* Sum up this iteration's reflection coefficient */
            float rr = 0;
            for (int j = 0; j < i; j++)
                rr += (lpc[j] * ac[i - j]);
            rr += ac[i + 1];
            r = -rr/error;
            /*  Update LPC coefficients and total error */
            lpc[i] = r;
            for (int j = 0; j < (i + 1) >> 1; j++) {
                float tmp1, tmp2;
                tmp1 = lpc[j];
                tmp2 = lpc[i-1-j];
                lpc[j]     = tmp1 + (r*tmp2);
                lpc[i-1-j] = tmp2 + (r*tmp1);
            }

            error = error - (r * r *error);
            /* Bail out once we get 30 dB gain */
            if (error < .001f * ac[0])
                break;
        }
    }
}

static void celt_fir5(const float *x,
                      const float *num,
                      float *y,
                      int N,
                      float *mem)
{
    float num0, num1, num2, num3, num4;
    float mem0, mem1, mem2, mem3, mem4;

    num0 = num[0];
    num1 = num[1];
    num2 = num[2];
    num3 = num[3];
    num4 = num[4];
    mem0 = mem[0];
    mem1 = mem[1];
    mem2 = mem[2];
    mem3 = mem[3];
    mem4 = mem[4];

    for (int i = 0; i < N; i++) {
        float sum = x[i];

        sum += (num0*mem0);
        sum += (num1*mem1);
        sum += (num2*mem2);
        sum += (num3*mem3);
        sum += (num4*mem4);
        mem4 = mem3;
        mem3 = mem2;
        mem2 = mem1;
        mem1 = mem0;
        mem0 = x[i];
        y[i] = sum;
    }

    mem[0] = mem0;
    mem[1] = mem1;
    mem[2] = mem2;
    mem[3] = mem3;
    mem[4] = mem4;
}

static void pitch_downsample(float *x[], float *x_lp,
                             int len, int C)
{
    float ac[5];
    float tmp=Q15ONE;
    float lpc[4], mem[5]={0,0,0,0,0};
    float lpc2[5];
    float c1 = .8f;

    for (int i = 1; i < len >> 1; i++)
        x_lp[i] = .5f * (.5f * (x[0][(2*i-1)]+x[0][(2*i+1)])+x[0][2*i]);
    x_lp[0] = .5f * (.5f * (x[0][1])+x[0][0]);
    if (C==2) {
        for (int i = 1; i < len >> 1; i++)
            x_lp[i] += (.5f * (.5f * (x[1][(2*i-1)]+x[1][(2*i+1)])+x[1][2*i]));
        x_lp[0] += .5f * (.5f * (x[1][1])+x[1][0]);
    }

    celt_autocorr(x_lp, ac, NULL, 0, 4, len>>1);

    /* Noise floor -40 dB */
    ac[0] *= 1.0001f;
    /* Lag windowing */
    for (int i = 1; i <= 4; i++) {
        /*ac[i] *= exp(-.5*(2*M_PI*.002*i)*(2*M_PI*.002*i));*/
        ac[i] -= ac[i]*(.008f*i)*(.008f*i);
    }

    celt_lpc(lpc, ac, 4);
    for (int i = 0; i < 4; i++) {
        tmp = .9f * tmp;
        lpc[i] = (lpc[i] * tmp);
    }
    /* Add a zero */
    lpc2[0] = lpc[0] + .8f;
    lpc2[1] = lpc[1] + (c1 * lpc[0]);
    lpc2[2] = lpc[2] + (c1 * lpc[1]);
    lpc2[3] = lpc[3] + (c1 * lpc[2]);
    lpc2[4] = (c1 * lpc[3]);
    celt_fir5(x_lp, lpc2, x_lp, len>>1, mem);
}

static inline void dual_inner_prod(const float *x, const float *y01, const float *y02,
                                   int N, float *xy1, float *xy2)
{
    float xy01 = 0, xy02 = 0;

    for (int i = 0; i < N; i++) {
        xy01 += (x[i] * y01[i]);
        xy02 += (x[i] * y02[i]);
    }

    *xy1 = xy01;
    *xy2 = xy02;
}

static float compute_pitch_gain(float xy, float xx, float yy)
{
    return xy / sqrtf(1.f + xx * yy);
}

static const uint8_t second_check[16] = {0, 0, 3, 2, 3, 2, 5, 2, 3, 2, 3, 2, 5, 2, 3, 2};
static float remove_doubling(float *x, int maxperiod, int minperiod, int N,
                             int *T0_, int prev_period, float prev_gain)
{
    int k, i, T, T0;
    float g, g0;
    float pg;
    float xy,xx,yy,xy2;
    float xcorr[3];
    float best_xy, best_yy;
    int offset;
    int minperiod0;
    float yy_lookup[PITCH_MAX_PERIOD+1];

    minperiod0 = minperiod;
    maxperiod /= 2;
    minperiod /= 2;
    *T0_ /= 2;
    prev_period /= 2;
    N /= 2;
    x += maxperiod;
    if (*T0_>=maxperiod)
        *T0_=maxperiod-1;

    T = T0 = *T0_;
    dual_inner_prod(x, x, x-T0, N, &xx, &xy);
    yy_lookup[0] = xx;
    yy=xx;
    for (i = 1; i <= maxperiod; i++) {
        yy = yy+(x[-i] * x[-i])-(x[N-i] * x[N-i]);
        yy_lookup[i] = FFMAX(0, yy);
    }
    yy = yy_lookup[T0];
    best_xy = xy;
    best_yy = yy;
    g = g0 = compute_pitch_gain(xy, xx, yy);
    /* Look for any pitch at T/k */
    for (k = 2; k <= 15; k++) {
        int T1, T1b;
        float g1;
        float cont=0;
        float thresh;
        T1 = (2*T0+k)/(2*k);
        if (T1 < minperiod)
            break;
        /* Look for another strong correlation at T1b */
        if (k==2)
        {
            if (T1+T0>maxperiod)
                T1b = T0;
            else
                T1b = T0+T1;
        } else
        {
            T1b = (2*second_check[k]*T0+k)/(2*k);
        }
        dual_inner_prod(x, &x[-T1], &x[-T1b], N, &xy, &xy2);
        xy = .5f * (xy + xy2);
        yy = .5f * (yy_lookup[T1] + yy_lookup[T1b]);
        g1 = compute_pitch_gain(xy, xx, yy);
        if (FFABS(T1-prev_period)<=1)
            cont = prev_gain;
        else if (FFABS(T1-prev_period)<=2 && 5 * k * k < T0)
            cont = prev_gain * .5f;
        else
            cont = 0;
        thresh = FFMAX(.3f, (.7f * g0) - cont);
        /* Bias against very high pitch (very short period) to avoid false-positives
           due to short-term correlation */
        if (T1<3*minperiod)
            thresh = FFMAX(.4f, (.85f * g0) - cont);
        else if (T1<2*minperiod)
            thresh = FFMAX(.5f, (.9f * g0) - cont);
        if (g1 > thresh)
        {
            best_xy = xy;
            best_yy = yy;
            T = T1;
            g = g1;
        }
    }
    best_xy = FFMAX(0, best_xy);
    if (best_yy <= best_xy)
        pg = Q15ONE;
    else
        pg = best_xy/(best_yy + 1);

    for (k = 0; k < 3; k++)
        xcorr[k] = celt_inner_prod(x, x-(T+k-1), N);
    if ((xcorr[2]-xcorr[0]) > .7f * (xcorr[1]-xcorr[0]))
        offset = 1;
    else if ((xcorr[0]-xcorr[2]) > (.7f * (xcorr[1] - xcorr[2])))
        offset = -1;
    else
        offset = 0;
    if (pg > g)
        pg = g;
    *T0_ = 2*T+offset;

    if (*T0_<minperiod0)
        *T0_=minperiod0;
    return pg;
}

static void find_best_pitch(float *xcorr, float *y, int len,
                            int max_pitch, int *best_pitch)
{
    float best_num[2];
    float best_den[2];
    float Syy = 1.f;

    best_num[0] = -1;
    best_num[1] = -1;
    best_den[0] = 0;
    best_den[1] = 0;
    best_pitch[0] = 0;
    best_pitch[1] = 1;

    for (int j = 0; j < len; j++)
        Syy += y[j] * y[j];

    for (int i = 0; i < max_pitch; i++) {
        if (xcorr[i]>0) {
            float num;
            float xcorr16;

            xcorr16 = xcorr[i];
            /* Considering the range of xcorr16, this should avoid both underflows
               and overflows (inf) when squaring xcorr16 */
            xcorr16 *= 1e-12f;
            num = xcorr16 * xcorr16;
            if ((num * best_den[1]) > (best_num[1] * Syy)) {
                if ((num * best_den[0]) > (best_num[0] * Syy)) {
                    best_num[1] = best_num[0];
                    best_den[1] = best_den[0];
                    best_pitch[1] = best_pitch[0];
                    best_num[0] = num;
                    best_den[0] = Syy;
                    best_pitch[0] = i;
                } else {
                    best_num[1] = num;
                    best_den[1] = Syy;
                    best_pitch[1] = i;
                }
            }
        }
        Syy += y[i+len]*y[i+len] - y[i] * y[i];
        Syy = FFMAX(1, Syy);
    }
}

static void pitch_search(const float *x_lp, float *y,
                         int len, int max_pitch, int *pitch)
{
    int lag;
    int best_pitch[2]={0,0};
    int offset;

    float x_lp4[WINDOW_SIZE];
    float y_lp4[WINDOW_SIZE];
    float xcorr[WINDOW_SIZE];

    lag = len+max_pitch;

    /* Downsample by 2 again */
    for (int j = 0; j < len >> 2; j++)
        x_lp4[j] = x_lp[2*j];
    for (int j = 0; j < lag >> 2; j++)
        y_lp4[j] = y[2*j];

    /* Coarse search with 4x decimation */

    celt_pitch_xcorr(x_lp4, y_lp4, xcorr, len>>2, max_pitch>>2);

    find_best_pitch(xcorr, y_lp4, len>>2, max_pitch>>2, best_pitch);

    /* Finer search with 2x decimation */
    for (int i = 0; i < max_pitch >> 1; i++) {
        float sum;
        xcorr[i] = 0;
        if (FFABS(i-2*best_pitch[0])>2 && FFABS(i-2*best_pitch[1])>2)
            continue;
        sum = celt_inner_prod(x_lp, y+i, len>>1);
        xcorr[i] = FFMAX(-1, sum);
    }

    find_best_pitch(xcorr, y, len>>1, max_pitch>>1, best_pitch);

    /* Refine by pseudo-interpolation */
    if (best_pitch[0] > 0 && best_pitch[0] < (max_pitch >> 1) - 1) {
        float a, b, c;

        a = xcorr[best_pitch[0] - 1];
        b = xcorr[best_pitch[0]];
        c = xcorr[best_pitch[0] + 1];
        if (c - a > .7f * (b - a))
            offset = 1;
        else if (a - c > .7f * (b-c))
            offset = -1;
        else
            offset = 0;
    } else {
        offset = 0;
    }

    *pitch = 2 * best_pitch[0] - offset;
}

static void dct(AudioRNNContext *s, float *out, const float *in)
{
    for (int i = 0; i < NB_BANDS; i++) {
        float sum;

        sum = s->fdsp->scalarproduct_float(in, s->dct_table[i], FFALIGN(NB_BANDS, 4));
        out[i] = sum * sqrtf(2.f / 22);
    }
}

static int compute_frame_features(AudioRNNContext *s, DenoiseState *st, AVComplexFloat *X, AVComplexFloat *P,
                                  float *Ex, float *Ep, float *Exp, float *features, const float *in)
{
    float E = 0;
    float *ceps_0, *ceps_1, *ceps_2;
    float spec_variability = 0;
    LOCAL_ALIGNED_32(float, Ly, [NB_BANDS]);
    LOCAL_ALIGNED_32(float, p, [WINDOW_SIZE]);
    float pitch_buf[PITCH_BUF_SIZE>>1];
    int pitch_index;
    float gain;
    float *(pre[1]);
    float tmp[NB_BANDS];
    float follow, logMax;

    frame_analysis(s, st, X, Ex, in);
    RNN_MOVE(st->pitch_buf, &st->pitch_buf[FRAME_SIZE], PITCH_BUF_SIZE-FRAME_SIZE);
    RNN_COPY(&st->pitch_buf[PITCH_BUF_SIZE-FRAME_SIZE], in, FRAME_SIZE);
    pre[0] = &st->pitch_buf[0];
    pitch_downsample(pre, pitch_buf, PITCH_BUF_SIZE, 1);
    pitch_search(pitch_buf+(PITCH_MAX_PERIOD>>1), pitch_buf, PITCH_FRAME_SIZE,
            PITCH_MAX_PERIOD-3*PITCH_MIN_PERIOD, &pitch_index);
    pitch_index = PITCH_MAX_PERIOD-pitch_index;

    gain = remove_doubling(pitch_buf, PITCH_MAX_PERIOD, PITCH_MIN_PERIOD,
            PITCH_FRAME_SIZE, &pitch_index, st->last_period, st->last_gain);
    st->last_period = pitch_index;
    st->last_gain = gain;

    for (int i = 0; i < WINDOW_SIZE; i++)
        p[i] = st->pitch_buf[PITCH_BUF_SIZE-WINDOW_SIZE-pitch_index+i];

    s->fdsp->vector_fmul(p, p, s->window, WINDOW_SIZE);
    forward_transform(st, P, p);
    compute_band_energy(Ep, P);
    compute_band_corr(Exp, X, P);

    for (int i = 0; i < NB_BANDS; i++)
        Exp[i] = Exp[i] / sqrtf(.001f+Ex[i]*Ep[i]);

    dct(s, tmp, Exp);

    for (int i = 0; i < NB_DELTA_CEPS; i++)
        features[NB_BANDS+2*NB_DELTA_CEPS+i] = tmp[i];

    features[NB_BANDS+2*NB_DELTA_CEPS] -= 1.3;
    features[NB_BANDS+2*NB_DELTA_CEPS+1] -= 0.9;
    features[NB_BANDS+3*NB_DELTA_CEPS] = .01*(pitch_index-300);
    logMax = -2;
    follow = -2;

    for (int i = 0; i < NB_BANDS; i++) {
        Ly[i] = log10f(1e-2f + Ex[i]);
        Ly[i] = FFMAX(logMax-7, FFMAX(follow-1.5, Ly[i]));
        logMax = FFMAX(logMax, Ly[i]);
        follow = FFMAX(follow-1.5, Ly[i]);
        E += Ex[i];
    }

    if (E < 0.04f) {
        /* If there's no audio, avoid messing up the state. */
        RNN_CLEAR(features, NB_FEATURES);
        return 1;
    }

    dct(s, features, Ly);
    features[0] -= 12;
    features[1] -= 4;
    ceps_0 = st->cepstral_mem[st->memid];
    ceps_1 = (st->memid < 1) ? st->cepstral_mem[CEPS_MEM+st->memid-1] : st->cepstral_mem[st->memid-1];
    ceps_2 = (st->memid < 2) ? st->cepstral_mem[CEPS_MEM+st->memid-2] : st->cepstral_mem[st->memid-2];

    for (int i = 0; i < NB_BANDS; i++)
        ceps_0[i] = features[i];

    st->memid++;
    for (int i = 0; i < NB_DELTA_CEPS; i++) {
        features[i] = ceps_0[i] + ceps_1[i] + ceps_2[i];
        features[NB_BANDS+i] = ceps_0[i] - ceps_2[i];
        features[NB_BANDS+NB_DELTA_CEPS+i] =  ceps_0[i] - 2*ceps_1[i] + ceps_2[i];
    }
    /* Spectral variability features. */
    if (st->memid == CEPS_MEM)
        st->memid = 0;

    for (int i = 0; i < CEPS_MEM; i++) {
        float mindist = 1e15f;
        for (int j = 0; j < CEPS_MEM; j++) {
            float dist = 0.f;
            for (int k = 0; k < NB_BANDS; k++) {
                float tmp;

                tmp = st->cepstral_mem[i][k] - st->cepstral_mem[j][k];
                dist += tmp*tmp;
            }

            if (j != i)
                mindist = FFMIN(mindist, dist);
        }

        spec_variability += mindist;
    }

    features[NB_BANDS+3*NB_DELTA_CEPS+1] = spec_variability/CEPS_MEM-2.1;

    return 0;
}

static void interp_band_gain(float *g, const float *bandE)
{
    memset(g, 0, sizeof(*g) * FREQ_SIZE);

    for (int i = 0; i < NB_BANDS - 1; i++) {
        const int band_size = (eband5ms[i + 1] - eband5ms[i]) << FRAME_SIZE_SHIFT;

        for (int j = 0; j < band_size; j++) {
            float frac = (float)j / band_size;

            g[(eband5ms[i] << FRAME_SIZE_SHIFT) + j] = (1.f - frac) * bandE[i] + frac * bandE[i + 1];
        }
    }
}

static void pitch_filter(AVComplexFloat *X, const AVComplexFloat *P, const float *Ex, const float *Ep,
                         const float *Exp, const float *g)
{
    float newE[NB_BANDS];
    float r[NB_BANDS];
    float norm[NB_BANDS];
    float rf[FREQ_SIZE] = {0};
    float normf[FREQ_SIZE]={0};

    for (int i = 0; i < NB_BANDS; i++) {
        if (Exp[i]>g[i]) r[i] = 1;
        else r[i] = SQUARE(Exp[i])*(1-SQUARE(g[i]))/(.001 + SQUARE(g[i])*(1-SQUARE(Exp[i])));
        r[i]  = sqrtf(av_clipf(r[i], 0, 1));
        r[i] *= sqrtf(Ex[i]/(1e-8+Ep[i]));
    }
    interp_band_gain(rf, r);
    for (int i = 0; i < FREQ_SIZE; i++) {
        X[i].re += rf[i]*P[i].re;
        X[i].im += rf[i]*P[i].im;
    }
    compute_band_energy(newE, X);
    for (int i = 0; i < NB_BANDS; i++) {
        norm[i] = sqrtf(Ex[i] / (1e-8+newE[i]));
    }
    interp_band_gain(normf, norm);
    for (int i = 0; i < FREQ_SIZE; i++) {
        X[i].re *= normf[i];
        X[i].im *= normf[i];
    }
}

static const float tansig_table[201] = {
    0.000000f, 0.039979f, 0.079830f, 0.119427f, 0.158649f,
    0.197375f, 0.235496f, 0.272905f, 0.309507f, 0.345214f,
    0.379949f, 0.413644f, 0.446244f, 0.477700f, 0.507977f,
    0.537050f, 0.564900f, 0.591519f, 0.616909f, 0.641077f,
    0.664037f, 0.685809f, 0.706419f, 0.725897f, 0.744277f,
    0.761594f, 0.777888f, 0.793199f, 0.807569f, 0.821040f,
    0.833655f, 0.845456f, 0.856485f, 0.866784f, 0.876393f,
    0.885352f, 0.893698f, 0.901468f, 0.908698f, 0.915420f,
    0.921669f, 0.927473f, 0.932862f, 0.937863f, 0.942503f,
    0.946806f, 0.950795f, 0.954492f, 0.957917f, 0.961090f,
    0.964028f, 0.966747f, 0.969265f, 0.971594f, 0.973749f,
    0.975743f, 0.977587f, 0.979293f, 0.980869f, 0.982327f,
    0.983675f, 0.984921f, 0.986072f, 0.987136f, 0.988119f,
    0.989027f, 0.989867f, 0.990642f, 0.991359f, 0.992020f,
    0.992631f, 0.993196f, 0.993718f, 0.994199f, 0.994644f,
    0.995055f, 0.995434f, 0.995784f, 0.996108f, 0.996407f,
    0.996682f, 0.996937f, 0.997172f, 0.997389f, 0.997590f,
    0.997775f, 0.997946f, 0.998104f, 0.998249f, 0.998384f,
    0.998508f, 0.998623f, 0.998728f, 0.998826f, 0.998916f,
    0.999000f, 0.999076f, 0.999147f, 0.999213f, 0.999273f,
    0.999329f, 0.999381f, 0.999428f, 0.999472f, 0.999513f,
    0.999550f, 0.999585f, 0.999617f, 0.999646f, 0.999673f,
    0.999699f, 0.999722f, 0.999743f, 0.999763f, 0.999781f,
    0.999798f, 0.999813f, 0.999828f, 0.999841f, 0.999853f,
    0.999865f, 0.999875f, 0.999885f, 0.999893f, 0.999902f,
    0.999909f, 0.999916f, 0.999923f, 0.999929f, 0.999934f,
    0.999939f, 0.999944f, 0.999948f, 0.999952f, 0.999956f,
    0.999959f, 0.999962f, 0.999965f, 0.999968f, 0.999970f,
    0.999973f, 0.999975f, 0.999977f, 0.999978f, 0.999980f,
    0.999982f, 0.999983f, 0.999984f, 0.999986f, 0.999987f,
    0.999988f, 0.999989f, 0.999990f, 0.999990f, 0.999991f,
    0.999992f, 0.999992f, 0.999993f, 0.999994f, 0.999994f,
    0.999994f, 0.999995f, 0.999995f, 0.999996f, 0.999996f,
    0.999996f, 0.999997f, 0.999997f, 0.999997f, 0.999997f,
    0.999997f, 0.999998f, 0.999998f, 0.999998f, 0.999998f,
    0.999998f, 0.999998f, 0.999999f, 0.999999f, 0.999999f,
    0.999999f, 0.999999f, 0.999999f, 0.999999f, 0.999999f,
    0.999999f, 0.999999f, 0.999999f, 0.999999f, 0.999999f,
    1.000000f, 1.000000f, 1.000000f, 1.000000f, 1.000000f,
    1.000000f, 1.000000f, 1.000000f, 1.000000f, 1.000000f,
    1.000000f,
};

static inline float tansig_approx(float x)
{
    float y, dy;
    float sign=1;
    int i;

    /* Tests are reversed to catch NaNs */
    if (!(x<8))
        return 1;
    if (!(x>-8))
        return -1;
    /* Another check in case of -ffast-math */

    if (isnan(x))
       return 0;

    if (x < 0) {
       x=-x;
       sign=-1;
    }
    i = (int)floor(.5f+25*x);
    x -= .04f*i;
    y = tansig_table[i];
    dy = 1-y*y;
    y = y + x*dy*(1 - y*x);
    return sign*y;
}

static inline float sigmoid_approx(float x)
{
    return .5f + .5f*tansig_approx(.5f*x);
}

static void compute_dense(const DenseLayer *layer, float *output, const float *input)
{
    const int N = layer->nb_neurons, M = layer->nb_inputs, stride = N;

    for (int i = 0; i < N; i++) {
        /* Compute update gate. */
        float sum = layer->bias[i];

        for (int j = 0; j < M; j++)
            sum += layer->input_weights[j * stride + i] * input[j];

        output[i] = WEIGHTS_SCALE * sum;
    }

    if (layer->activation == ACTIVATION_SIGMOID) {
        for (int i = 0; i < N; i++)
            output[i] = sigmoid_approx(output[i]);
    } else if (layer->activation == ACTIVATION_TANH) {
        for (int i = 0; i < N; i++)
            output[i] = tansig_approx(output[i]);
    } else if (layer->activation == ACTIVATION_RELU) {
        for (int i = 0; i < N; i++)
            output[i] = FFMAX(0, output[i]);
    } else {
        av_assert0(0);
    }
}

static void compute_gru(AudioRNNContext *s, const GRULayer *gru, float *state, const float *input)
{
    LOCAL_ALIGNED_32(float, z, [MAX_NEURONS]);
    LOCAL_ALIGNED_32(float, r, [MAX_NEURONS]);
    LOCAL_ALIGNED_32(float, h, [MAX_NEURONS]);
    const int M = gru->nb_inputs;
    const int N = gru->nb_neurons;
    const int AN = FFALIGN(N, 4);
    const int AM = FFALIGN(M, 4);
    const int stride = 3 * AN, istride = 3 * AM;

    for (int i = 0; i < N; i++) {
        /* Compute update gate. */
        float sum = gru->bias[i];

        sum += s->fdsp->scalarproduct_float(gru->input_weights + i * istride, input, AM);
        sum += s->fdsp->scalarproduct_float(gru->recurrent_weights + i * stride, state, AN);
        z[i] = sigmoid_approx(WEIGHTS_SCALE * sum);
    }

    for (int i = 0; i < N; i++) {
        /* Compute reset gate. */
        float sum = gru->bias[N + i];

        sum += s->fdsp->scalarproduct_float(gru->input_weights + AM + i * istride, input, AM);
        sum += s->fdsp->scalarproduct_float(gru->recurrent_weights + AN + i * stride, state, AN);
        r[i] = sigmoid_approx(WEIGHTS_SCALE * sum);
    }

    for (int i = 0; i < N; i++) {
        /* Compute output. */
        float sum = gru->bias[2 * N + i];

        sum += s->fdsp->scalarproduct_float(gru->input_weights + 2 * AM + i * istride, input, AM);
        for (int j = 0; j < N; j++)
            sum += gru->recurrent_weights[2 * AN + i * stride + j] * state[j] * r[j];

        if (gru->activation == ACTIVATION_SIGMOID)
            sum = sigmoid_approx(WEIGHTS_SCALE * sum);
        else if (gru->activation == ACTIVATION_TANH)
            sum = tansig_approx(WEIGHTS_SCALE * sum);
        else if (gru->activation == ACTIVATION_RELU)
            sum = FFMAX(0, WEIGHTS_SCALE * sum);
        else
            av_assert0(0);
        h[i] = z[i] * state[i] + (1.f - z[i]) * sum;
    }

    RNN_COPY(state, h, N);
}

#define INPUT_SIZE 42

static void compute_rnn(AudioRNNContext *s, RNNState *rnn, float *gains, float *vad, const float *input)
{
    LOCAL_ALIGNED_32(float, dense_out,     [MAX_NEURONS]);
    LOCAL_ALIGNED_32(float, noise_input,   [MAX_NEURONS * 3]);
    LOCAL_ALIGNED_32(float, denoise_input, [MAX_NEURONS * 3]);

    compute_dense(rnn->model->input_dense, dense_out, input);
    compute_gru(s, rnn->model->vad_gru, rnn->vad_gru_state, dense_out);
    compute_dense(rnn->model->vad_output, vad, rnn->vad_gru_state);

    memcpy(noise_input, dense_out, rnn->model->input_dense_size * sizeof(float));
    memcpy(noise_input + rnn->model->input_dense_size,
           rnn->vad_gru_state, rnn->model->vad_gru_size * sizeof(float));
    memcpy(noise_input + rnn->model->input_dense_size + rnn->model->vad_gru_size,
           input, INPUT_SIZE * sizeof(float));

    compute_gru(s, rnn->model->noise_gru, rnn->noise_gru_state, noise_input);

    memcpy(denoise_input, rnn->vad_gru_state, rnn->model->vad_gru_size * sizeof(float));
    memcpy(denoise_input + rnn->model->vad_gru_size,
           rnn->noise_gru_state, rnn->model->noise_gru_size * sizeof(float));
    memcpy(denoise_input + rnn->model->vad_gru_size + rnn->model->noise_gru_size,
           input, INPUT_SIZE * sizeof(float));

    compute_gru(s, rnn->model->denoise_gru, rnn->denoise_gru_state, denoise_input);
    compute_dense(rnn->model->denoise_output, gains, rnn->denoise_gru_state);
}

static float rnnoise_channel(AudioRNNContext *s, DenoiseState *st, float *out, const float *in,
                             int disabled)
{
    AVComplexFloat X[FREQ_SIZE];
    AVComplexFloat P[WINDOW_SIZE];
    float x[FRAME_SIZE];
    float Ex[NB_BANDS], Ep[NB_BANDS];
    LOCAL_ALIGNED_32(float, Exp, [NB_BANDS]);
    float features[NB_FEATURES];
    float g[NB_BANDS];
    float gf[FREQ_SIZE];
    float vad_prob = 0;
    float *history = st->history;
    static const float a_hp[2] = {-1.99599, 0.99600};
    static const float b_hp[2] = {-2, 1};
    int silence;

    biquad(x, st->mem_hp_x, in, b_hp, a_hp, FRAME_SIZE);
    silence = compute_frame_features(s, st, X, P, Ex, Ep, Exp, features, x);

    if (!silence && !disabled) {
        compute_rnn(s, &st->rnn[0], g, &vad_prob, features);
        pitch_filter(X, P, Ex, Ep, Exp, g);
        for (int i = 0; i < NB_BANDS; i++) {
            float alpha = .6f;

            g[i] = FFMAX(g[i], alpha * st->lastg[i]);
            st->lastg[i] = g[i];
        }

        interp_band_gain(gf, g);

        for (int i = 0; i < FREQ_SIZE; i++) {
            X[i].re *= gf[i];
            X[i].im *= gf[i];
        }
    }

    frame_synthesis(s, st, out, X);
    memcpy(history, in, FRAME_SIZE * sizeof(*history));

    return vad_prob;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int rnnoise_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioRNNContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int start = (out->channels * jobnr) / nb_jobs;
    const int end = (out->channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++) {
        rnnoise_channel(s, &s->st[ch],
                        (float *)out->extended_data[ch],
                        (const float *)in->extended_data[ch],
                        ctx->is_disabled);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = NULL;
    ThreadData td;

    out = ff_get_audio_buffer(outlink, FRAME_SIZE);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;

    td.in = in; td.out = out;
    ctx->internal->execute(ctx, rnnoise_channels, &td, NULL, FFMIN(outlink->channels,
                                                                   ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in = NULL;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, FRAME_SIZE, FRAME_SIZE, &in);
    if (ret < 0)
        return ret;

    if (ret > 0)
        return filter_frame(inlink, in);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int open_model(AVFilterContext *ctx, RNNModel **model)
{
    AudioRNNContext *s = ctx->priv;
    int ret;
    FILE *f;

    if (!s->model_name)
        return AVERROR(EINVAL);
    f = av_fopen_utf8(s->model_name, "r");
    if (!f) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open model file: %s\n", s->model_name);
        return AVERROR(EINVAL);
    }

    ret = rnnoise_model_from_file(f, model);
    fclose(f);
    if (!*model || ret < 0)
        return ret;

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioRNNContext *s = ctx->priv;
    int ret;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    ret = open_model(ctx, &s->model[0]);
    if (ret < 0)
        return ret;

    for (int i = 0; i < FRAME_SIZE; i++) {
        s->window[i] = sin(.5*M_PI*sin(.5*M_PI*(i+.5)/FRAME_SIZE) * sin(.5*M_PI*(i+.5)/FRAME_SIZE));
        s->window[WINDOW_SIZE - 1 - i] = s->window[i];
    }

    for (int i = 0; i < NB_BANDS; i++) {
        for (int j = 0; j < NB_BANDS; j++) {
            s->dct_table[j][i] = cosf((i + .5f) * j * M_PI / NB_BANDS);
            if (j == 0)
                s->dct_table[j][i] *= sqrtf(.5);
        }
    }

    return 0;
}

static void free_model(AVFilterContext *ctx, int n)
{
    AudioRNNContext *s = ctx->priv;

    rnnoise_model_free(s->model[n]);
    s->model[n] = NULL;

    for (int ch = 0; ch < s->channels && s->st; ch++) {
        av_freep(&s->st[ch].rnn[n].vad_gru_state);
        av_freep(&s->st[ch].rnn[n].noise_gru_state);
        av_freep(&s->st[ch].rnn[n].denoise_gru_state);
    }
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AudioRNNContext *s = ctx->priv;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    ret = open_model(ctx, &s->model[1]);
    if (ret < 0)
        return ret;

    FFSWAP(RNNModel *, s->model[0], s->model[1]);
    for (int ch = 0; ch < s->channels; ch++)
        FFSWAP(RNNState, s->st[ch].rnn[0], s->st[ch].rnn[1]);

    ret = config_input(ctx->inputs[0]);
    if (ret < 0) {
        for (int ch = 0; ch < s->channels; ch++)
            FFSWAP(RNNState, s->st[ch].rnn[0], s->st[ch].rnn[1]);
        FFSWAP(RNNModel *, s->model[0], s->model[1]);
        return ret;
    }

    free_model(ctx, 1);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioRNNContext *s = ctx->priv;

    av_freep(&s->fdsp);
    free_model(ctx, 0);
    for (int ch = 0; ch < s->channels && s->st; ch++) {
        av_tx_uninit(&s->st[ch].tx);
        av_tx_uninit(&s->st[ch].txi);
    }
    av_freep(&s->st);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(AudioRNNContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption arnndn_options[] = {
    { "model", "set model name", OFFSET(model_name), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, AF },
    { "m",     "set model name", OFFSET(model_name), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, AF },
    { "mix",   "set output vs input mix", OFFSET(mix), AV_OPT_TYPE_FLOAT, {.dbl=1.0},-1, 1, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(arnndn);

AVFilter ff_af_arnndn = {
    .name          = "arnndn",
    .description   = NULL_IF_CONFIG_SMALL("Reduce noise from speech using Recurrent Neural Networks."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioRNNContext),
    .priv_class    = &arnndn_class,
    .activate      = activate,
    .init          = init,
    .uninit        = uninit,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
