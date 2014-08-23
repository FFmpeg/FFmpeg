/*
 * Copyright (c) 2012 Pavel Koshevoy <pkoshevoy at gmail dot com>
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
 * tempo scaling audio filter -- an implementation of WSOLA algorithm
 *
 * Based on MIT licensed yaeAudioTempoFilter.h and yaeAudioFragment.h
 * from Apprentice Video player by Pavel Koshevoy.
 * https://sourceforge.net/projects/apprenticevideo/
 *
 * An explanation of SOLA algorithm is available at
 * http://www.surina.net/article/time-and-pitch-scaling.html
 *
 * WSOLA is very similar to SOLA, only one major difference exists between
 * these algorithms.  SOLA shifts audio fragments along the output stream,
 * where as WSOLA shifts audio fragments along the input stream.
 *
 * The advantage of WSOLA algorithm is that the overlap region size is
 * always the same, therefore the blending function is constant and
 * can be precomputed.
 */

#include <float.h>
#include "libavcodec/avfft.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

/**
 * A fragment of audio waveform
 */
typedef struct {
    // index of the first sample of this fragment in the overall waveform;
    // 0: input sample position
    // 1: output sample position
    int64_t position[2];

    // original packed multi-channel samples:
    uint8_t *data;

    // number of samples in this fragment:
    int nsamples;

    // rDFT transform of the down-mixed mono fragment, used for
    // fast waveform alignment via correlation in frequency domain:
    FFTSample *xdat;
} AudioFragment;

/**
 * Filter state machine states
 */
typedef enum {
    YAE_LOAD_FRAGMENT,
    YAE_ADJUST_POSITION,
    YAE_RELOAD_FRAGMENT,
    YAE_OUTPUT_OVERLAP_ADD,
    YAE_FLUSH_OUTPUT,
} FilterState;

/**
 * Filter state machine
 */
typedef struct {
    const AVClass *class;

    // ring-buffer of input samples, necessary because some times
    // input fragment position may be adjusted backwards:
    uint8_t *buffer;

    // ring-buffer maximum capacity, expressed in sample rate time base:
    int ring;

    // ring-buffer house keeping:
    int size;
    int head;
    int tail;

    // 0: input sample position corresponding to the ring buffer tail
    // 1: output sample position
    int64_t position[2];

    // sample format:
    enum AVSampleFormat format;

    // number of channels:
    int channels;

    // row of bytes to skip from one sample to next, across multple channels;
    // stride = (number-of-channels * bits-per-sample-per-channel) / 8
    int stride;

    // fragment window size, power-of-two integer:
    int window;

    // Hann window coefficients, for feathering
    // (blending) the overlapping fragment region:
    float *hann;

    // tempo scaling factor:
    double tempo;

    // a snapshot of previous fragment input and output position values
    // captured when the tempo scale factor was set most recently:
    int64_t origin[2];

    // current/previous fragment ring-buffer:
    AudioFragment frag[2];

    // current fragment index:
    uint64_t nfrag;

    // current state:
    FilterState state;

    // for fast correlation calculation in frequency domain:
    RDFTContext *real_to_complex;
    RDFTContext *complex_to_real;
    FFTSample *correlation;

    // for managing AVFilterPad.request_frame and AVFilterPad.filter_frame
    AVFrame *dst_buffer;
    uint8_t *dst;
    uint8_t *dst_end;
    uint64_t nsamples_in;
    uint64_t nsamples_out;
} ATempoContext;

#define OFFSET(x) offsetof(ATempoContext, x)

static const AVOption atempo_options[] = {
    { "tempo", "set tempo scale factor",
      OFFSET(tempo), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 0.5, 2.0,
      AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { NULL }
};

AVFILTER_DEFINE_CLASS(atempo);

inline static AudioFragment *yae_curr_frag(ATempoContext *atempo)
{
    return &atempo->frag[atempo->nfrag % 2];
}

inline static AudioFragment *yae_prev_frag(ATempoContext *atempo)
{
    return &atempo->frag[(atempo->nfrag + 1) % 2];
}

/**
 * Reset filter to initial state, do not deallocate existing local buffers.
 */
static void yae_clear(ATempoContext *atempo)
{
    atempo->size = 0;
    atempo->head = 0;
    atempo->tail = 0;

    atempo->nfrag = 0;
    atempo->state = YAE_LOAD_FRAGMENT;

    atempo->position[0] = 0;
    atempo->position[1] = 0;

    atempo->origin[0] = 0;
    atempo->origin[1] = 0;

    atempo->frag[0].position[0] = 0;
    atempo->frag[0].position[1] = 0;
    atempo->frag[0].nsamples    = 0;

    atempo->frag[1].position[0] = 0;
    atempo->frag[1].position[1] = 0;
    atempo->frag[1].nsamples    = 0;

    // shift left position of 1st fragment by half a window
    // so that no re-normalization would be required for
    // the left half of the 1st fragment:
    atempo->frag[0].position[0] = -(int64_t)(atempo->window / 2);
    atempo->frag[0].position[1] = -(int64_t)(atempo->window / 2);

    av_frame_free(&atempo->dst_buffer);
    atempo->dst     = NULL;
    atempo->dst_end = NULL;

    atempo->nsamples_in       = 0;
    atempo->nsamples_out      = 0;
}

/**
 * Reset filter to initial state and deallocate all buffers.
 */
static void yae_release_buffers(ATempoContext *atempo)
{
    yae_clear(atempo);

    av_freep(&atempo->frag[0].data);
    av_freep(&atempo->frag[1].data);
    av_freep(&atempo->frag[0].xdat);
    av_freep(&atempo->frag[1].xdat);

    av_freep(&atempo->buffer);
    av_freep(&atempo->hann);
    av_freep(&atempo->correlation);

    av_rdft_end(atempo->real_to_complex);
    atempo->real_to_complex = NULL;

    av_rdft_end(atempo->complex_to_real);
    atempo->complex_to_real = NULL;
}

/* av_realloc is not aligned enough; fortunately, the data does not need to
 * be preserved */
#define RE_MALLOC_OR_FAIL(field, field_size)                    \
    do {                                                        \
        av_freep(&field);                                       \
        field = av_malloc(field_size);                          \
        if (!field) {                                           \
            yae_release_buffers(atempo);                        \
            return AVERROR(ENOMEM);                             \
        }                                                       \
    } while (0)

/**
 * Prepare filter for processing audio data of given format,
 * sample rate and number of channels.
 */
static int yae_reset(ATempoContext *atempo,
                     enum AVSampleFormat format,
                     int sample_rate,
                     int channels)
{
    const int sample_size = av_get_bytes_per_sample(format);
    uint32_t nlevels  = 0;
    uint32_t pot;
    int i;

    atempo->format   = format;
    atempo->channels = channels;
    atempo->stride   = sample_size * channels;

    // pick a segment window size:
    atempo->window = sample_rate / 24;

    // adjust window size to be a power-of-two integer:
    nlevels = av_log2(atempo->window);
    pot = 1 << nlevels;
    av_assert0(pot <= atempo->window);

    if (pot < atempo->window) {
        atempo->window = pot * 2;
        nlevels++;
    }

    // initialize audio fragment buffers:
    RE_MALLOC_OR_FAIL(atempo->frag[0].data, atempo->window * atempo->stride);
    RE_MALLOC_OR_FAIL(atempo->frag[1].data, atempo->window * atempo->stride);
    RE_MALLOC_OR_FAIL(atempo->frag[0].xdat, atempo->window * sizeof(FFTComplex));
    RE_MALLOC_OR_FAIL(atempo->frag[1].xdat, atempo->window * sizeof(FFTComplex));

    // initialize rDFT contexts:
    av_rdft_end(atempo->real_to_complex);
    atempo->real_to_complex = NULL;

    av_rdft_end(atempo->complex_to_real);
    atempo->complex_to_real = NULL;

    atempo->real_to_complex = av_rdft_init(nlevels + 1, DFT_R2C);
    if (!atempo->real_to_complex) {
        yae_release_buffers(atempo);
        return AVERROR(ENOMEM);
    }

    atempo->complex_to_real = av_rdft_init(nlevels + 1, IDFT_C2R);
    if (!atempo->complex_to_real) {
        yae_release_buffers(atempo);
        return AVERROR(ENOMEM);
    }

    RE_MALLOC_OR_FAIL(atempo->correlation, atempo->window * sizeof(FFTComplex));

    atempo->ring = atempo->window * 3;
    RE_MALLOC_OR_FAIL(atempo->buffer, atempo->ring * atempo->stride);

    // initialize the Hann window function:
    RE_MALLOC_OR_FAIL(atempo->hann, atempo->window * sizeof(float));

    for (i = 0; i < atempo->window; i++) {
        double t = (double)i / (double)(atempo->window - 1);
        double h = 0.5 * (1.0 - cos(2.0 * M_PI * t));
        atempo->hann[i] = (float)h;
    }

    yae_clear(atempo);
    return 0;
}

static int yae_set_tempo(AVFilterContext *ctx, const char *arg_tempo)
{
    const AudioFragment *prev;
    ATempoContext *atempo = ctx->priv;
    char   *tail = NULL;
    double tempo = av_strtod(arg_tempo, &tail);

    if (tail && *tail) {
        av_log(ctx, AV_LOG_ERROR, "Invalid tempo value '%s'\n", arg_tempo);
        return AVERROR(EINVAL);
    }

    if (tempo < 0.5 || tempo > 2.0) {
        av_log(ctx, AV_LOG_ERROR, "Tempo value %f exceeds [0.5, 2.0] range\n",
               tempo);
        return AVERROR(EINVAL);
    }

    prev = yae_prev_frag(atempo);
    atempo->origin[0] = prev->position[0] + atempo->window / 2;
    atempo->origin[1] = prev->position[1] + atempo->window / 2;
    atempo->tempo = tempo;
    return 0;
}

/**
 * A helper macro for initializing complex data buffer with scalar data
 * of a given type.
 */
#define yae_init_xdat(scalar_type, scalar_max)                          \
    do {                                                                \
        const uint8_t *src_end = src +                                  \
            frag->nsamples * atempo->channels * sizeof(scalar_type);    \
                                                                        \
        FFTSample *xdat = frag->xdat;                                   \
        scalar_type tmp;                                                \
                                                                        \
        if (atempo->channels == 1) {                                    \
            for (; src < src_end; xdat++) {                             \
                tmp = *(const scalar_type *)src;                        \
                src += sizeof(scalar_type);                             \
                                                                        \
                *xdat = (FFTSample)tmp;                                 \
            }                                                           \
        } else {                                                        \
            FFTSample s, max, ti, si;                                   \
            int i;                                                      \
                                                                        \
            for (; src < src_end; xdat++) {                             \
                tmp = *(const scalar_type *)src;                        \
                src += sizeof(scalar_type);                             \
                                                                        \
                max = (FFTSample)tmp;                                   \
                s = FFMIN((FFTSample)scalar_max,                        \
                          (FFTSample)fabsf(max));                       \
                                                                        \
                for (i = 1; i < atempo->channels; i++) {                \
                    tmp = *(const scalar_type *)src;                    \
                    src += sizeof(scalar_type);                         \
                                                                        \
                    ti = (FFTSample)tmp;                                \
                    si = FFMIN((FFTSample)scalar_max,                   \
                               (FFTSample)fabsf(ti));                   \
                                                                        \
                    if (s < si) {                                       \
                        s   = si;                                       \
                        max = ti;                                       \
                    }                                                   \
                }                                                       \
                                                                        \
                *xdat = max;                                            \
            }                                                           \
        }                                                               \
    } while (0)

/**
 * Initialize complex data buffer of a given audio fragment
 * with down-mixed mono data of appropriate scalar type.
 */
static void yae_downmix(ATempoContext *atempo, AudioFragment *frag)
{
    // shortcuts:
    const uint8_t *src = frag->data;

    // init complex data buffer used for FFT and Correlation:
    memset(frag->xdat, 0, sizeof(FFTComplex) * atempo->window);

    if (atempo->format == AV_SAMPLE_FMT_U8) {
        yae_init_xdat(uint8_t, 127);
    } else if (atempo->format == AV_SAMPLE_FMT_S16) {
        yae_init_xdat(int16_t, 32767);
    } else if (atempo->format == AV_SAMPLE_FMT_S32) {
        yae_init_xdat(int, 2147483647);
    } else if (atempo->format == AV_SAMPLE_FMT_FLT) {
        yae_init_xdat(float, 1);
    } else if (atempo->format == AV_SAMPLE_FMT_DBL) {
        yae_init_xdat(double, 1);
    }
}

/**
 * Populate the internal data buffer on as-needed basis.
 *
 * @return
 *   0 if requested data was already available or was successfully loaded,
 *   AVERROR(EAGAIN) if more input data is required.
 */
static int yae_load_data(ATempoContext *atempo,
                         const uint8_t **src_ref,
                         const uint8_t *src_end,
                         int64_t stop_here)
{
    // shortcut:
    const uint8_t *src = *src_ref;
    const int read_size = stop_here - atempo->position[0];

    if (stop_here <= atempo->position[0]) {
        return 0;
    }

    // samples are not expected to be skipped:
    av_assert0(read_size <= atempo->ring);

    while (atempo->position[0] < stop_here && src < src_end) {
        int src_samples = (src_end - src) / atempo->stride;

        // load data piece-wise, in order to avoid complicating the logic:
        int nsamples = FFMIN(read_size, src_samples);
        int na;
        int nb;

        nsamples = FFMIN(nsamples, atempo->ring);
        na = FFMIN(nsamples, atempo->ring - atempo->tail);
        nb = FFMIN(nsamples - na, atempo->ring);

        if (na) {
            uint8_t *a = atempo->buffer + atempo->tail * atempo->stride;
            memcpy(a, src, na * atempo->stride);

            src += na * atempo->stride;
            atempo->position[0] += na;

            atempo->size = FFMIN(atempo->size + na, atempo->ring);
            atempo->tail = (atempo->tail + na) % atempo->ring;
            atempo->head =
                atempo->size < atempo->ring ?
                atempo->tail - atempo->size :
                atempo->tail;
        }

        if (nb) {
            uint8_t *b = atempo->buffer;
            memcpy(b, src, nb * atempo->stride);

            src += nb * atempo->stride;
            atempo->position[0] += nb;

            atempo->size = FFMIN(atempo->size + nb, atempo->ring);
            atempo->tail = (atempo->tail + nb) % atempo->ring;
            atempo->head =
                atempo->size < atempo->ring ?
                atempo->tail - atempo->size :
                atempo->tail;
        }
    }

    // pass back the updated source buffer pointer:
    *src_ref = src;

    // sanity check:
    av_assert0(atempo->position[0] <= stop_here);

    return atempo->position[0] == stop_here ? 0 : AVERROR(EAGAIN);
}

/**
 * Populate current audio fragment data buffer.
 *
 * @return
 *   0 when the fragment is ready,
 *   AVERROR(EAGAIN) if more input data is required.
 */
static int yae_load_frag(ATempoContext *atempo,
                         const uint8_t **src_ref,
                         const uint8_t *src_end)
{
    // shortcuts:
    AudioFragment *frag = yae_curr_frag(atempo);
    uint8_t *dst;
    int64_t missing, start, zeros;
    uint32_t nsamples;
    const uint8_t *a, *b;
    int i0, i1, n0, n1, na, nb;

    int64_t stop_here = frag->position[0] + atempo->window;
    if (src_ref && yae_load_data(atempo, src_ref, src_end, stop_here) != 0) {
        return AVERROR(EAGAIN);
    }

    // calculate the number of samples we don't have:
    missing =
        stop_here > atempo->position[0] ?
        stop_here - atempo->position[0] : 0;

    nsamples =
        missing < (int64_t)atempo->window ?
        (uint32_t)(atempo->window - missing) : 0;

    // setup the output buffer:
    frag->nsamples = nsamples;
    dst = frag->data;

    start = atempo->position[0] - atempo->size;
    zeros = 0;

    if (frag->position[0] < start) {
        // what we don't have we substitute with zeros:
        zeros = FFMIN(start - frag->position[0], (int64_t)nsamples);
        av_assert0(zeros != nsamples);

        memset(dst, 0, zeros * atempo->stride);
        dst += zeros * atempo->stride;
    }

    if (zeros == nsamples) {
        return 0;
    }

    // get the remaining data from the ring buffer:
    na = (atempo->head < atempo->tail ?
          atempo->tail - atempo->head :
          atempo->ring - atempo->head);

    nb = atempo->head < atempo->tail ? 0 : atempo->tail;

    // sanity check:
    av_assert0(nsamples <= zeros + na + nb);

    a = atempo->buffer + atempo->head * atempo->stride;
    b = atempo->buffer;

    i0 = frag->position[0] + zeros - start;
    i1 = i0 < na ? 0 : i0 - na;

    n0 = i0 < na ? FFMIN(na - i0, (int)(nsamples - zeros)) : 0;
    n1 = nsamples - zeros - n0;

    if (n0) {
        memcpy(dst, a + i0 * atempo->stride, n0 * atempo->stride);
        dst += n0 * atempo->stride;
    }

    if (n1) {
        memcpy(dst, b + i1 * atempo->stride, n1 * atempo->stride);
    }

    return 0;
}

/**
 * Prepare for loading next audio fragment.
 */
static void yae_advance_to_next_frag(ATempoContext *atempo)
{
    const double fragment_step = atempo->tempo * (double)(atempo->window / 2);

    const AudioFragment *prev;
    AudioFragment       *frag;

    atempo->nfrag++;
    prev = yae_prev_frag(atempo);
    frag = yae_curr_frag(atempo);

    frag->position[0] = prev->position[0] + (int64_t)fragment_step;
    frag->position[1] = prev->position[1] + atempo->window / 2;
    frag->nsamples    = 0;
}

/**
 * Calculate cross-correlation via rDFT.
 *
 * Multiply two vectors of complex numbers (result of real_to_complex rDFT)
 * and transform back via complex_to_real rDFT.
 */
static void yae_xcorr_via_rdft(FFTSample *xcorr,
                               RDFTContext *complex_to_real,
                               const FFTComplex *xa,
                               const FFTComplex *xb,
                               const int window)
{
    FFTComplex *xc = (FFTComplex *)xcorr;
    int i;

    // NOTE: first element requires special care -- Given Y = rDFT(X),
    // Im(Y[0]) and Im(Y[N/2]) are always zero, therefore av_rdft_calc
    // stores Re(Y[N/2]) in place of Im(Y[0]).

    xc->re = xa->re * xb->re;
    xc->im = xa->im * xb->im;
    xa++;
    xb++;
    xc++;

    for (i = 1; i < window; i++, xa++, xb++, xc++) {
        xc->re = (xa->re * xb->re + xa->im * xb->im);
        xc->im = (xa->im * xb->re - xa->re * xb->im);
    }

    // apply inverse rDFT:
    av_rdft_calc(complex_to_real, xcorr);
}

/**
 * Calculate alignment offset for given fragment
 * relative to the previous fragment.
 *
 * @return alignment offset of current fragment relative to previous.
 */
static int yae_align(AudioFragment *frag,
                     const AudioFragment *prev,
                     const int window,
                     const int delta_max,
                     const int drift,
                     FFTSample *correlation,
                     RDFTContext *complex_to_real)
{
    int       best_offset = -drift;
    FFTSample best_metric = -FLT_MAX;
    FFTSample *xcorr;

    int i0;
    int i1;
    int i;

    yae_xcorr_via_rdft(correlation,
                       complex_to_real,
                       (const FFTComplex *)prev->xdat,
                       (const FFTComplex *)frag->xdat,
                       window);

    // identify search window boundaries:
    i0 = FFMAX(window / 2 - delta_max - drift, 0);
    i0 = FFMIN(i0, window);

    i1 = FFMIN(window / 2 + delta_max - drift, window - window / 16);
    i1 = FFMAX(i1, 0);

    // identify cross-correlation peaks within search window:
    xcorr = correlation + i0;

    for (i = i0; i < i1; i++, xcorr++) {
        FFTSample metric = *xcorr;

        // normalize:
        FFTSample drifti = (FFTSample)(drift + i);
        metric *= drifti * (FFTSample)(i - i0) * (FFTSample)(i1 - i);

        if (metric > best_metric) {
            best_metric = metric;
            best_offset = i - window / 2;
        }
    }

    return best_offset;
}

/**
 * Adjust current fragment position for better alignment
 * with previous fragment.
 *
 * @return alignment correction.
 */
static int yae_adjust_position(ATempoContext *atempo)
{
    const AudioFragment *prev = yae_prev_frag(atempo);
    AudioFragment       *frag = yae_curr_frag(atempo);

    const double prev_output_position =
        (double)(prev->position[1] - atempo->origin[1] + atempo->window / 2);

    const double ideal_output_position =
        (double)(prev->position[0] - atempo->origin[0] + atempo->window / 2) /
        atempo->tempo;

    const int drift = (int)(prev_output_position - ideal_output_position);

    const int delta_max  = atempo->window / 2;
    const int correction = yae_align(frag,
                                     prev,
                                     atempo->window,
                                     delta_max,
                                     drift,
                                     atempo->correlation,
                                     atempo->complex_to_real);

    if (correction) {
        // adjust fragment position:
        frag->position[0] -= correction;

        // clear so that the fragment can be reloaded:
        frag->nsamples = 0;
    }

    return correction;
}

/**
 * A helper macro for blending the overlap region of previous
 * and current audio fragment.
 */
#define yae_blend(scalar_type)                                          \
    do {                                                                \
        const scalar_type *aaa = (const scalar_type *)a;                \
        const scalar_type *bbb = (const scalar_type *)b;                \
                                                                        \
        scalar_type *out     = (scalar_type *)dst;                      \
        scalar_type *out_end = (scalar_type *)dst_end;                  \
        int64_t i;                                                      \
                                                                        \
        for (i = 0; i < overlap && out < out_end;                       \
             i++, atempo->position[1]++, wa++, wb++) {                  \
            float w0 = *wa;                                             \
            float w1 = *wb;                                             \
            int j;                                                      \
                                                                        \
            for (j = 0; j < atempo->channels;                           \
                 j++, aaa++, bbb++, out++) {                            \
                float t0 = (float)*aaa;                                 \
                float t1 = (float)*bbb;                                 \
                                                                        \
                *out =                                                  \
                    frag->position[0] + i < 0 ?                         \
                    *aaa :                                              \
                    (scalar_type)(t0 * w0 + t1 * w1);                   \
            }                                                           \
        }                                                               \
        dst = (uint8_t *)out;                                           \
    } while (0)

/**
 * Blend the overlap region of previous and current audio fragment
 * and output the results to the given destination buffer.
 *
 * @return
 *   0 if the overlap region was completely stored in the dst buffer,
 *   AVERROR(EAGAIN) if more destination buffer space is required.
 */
static int yae_overlap_add(ATempoContext *atempo,
                           uint8_t **dst_ref,
                           uint8_t *dst_end)
{
    // shortcuts:
    const AudioFragment *prev = yae_prev_frag(atempo);
    const AudioFragment *frag = yae_curr_frag(atempo);

    const int64_t start_here = FFMAX(atempo->position[1],
                                     frag->position[1]);

    const int64_t stop_here = FFMIN(prev->position[1] + prev->nsamples,
                                    frag->position[1] + frag->nsamples);

    const int64_t overlap = stop_here - start_here;

    const int64_t ia = start_here - prev->position[1];
    const int64_t ib = start_here - frag->position[1];

    const float *wa = atempo->hann + ia;
    const float *wb = atempo->hann + ib;

    const uint8_t *a = prev->data + ia * atempo->stride;
    const uint8_t *b = frag->data + ib * atempo->stride;

    uint8_t *dst = *dst_ref;

    av_assert0(start_here <= stop_here &&
               frag->position[1] <= start_here &&
               overlap <= frag->nsamples);

    if (atempo->format == AV_SAMPLE_FMT_U8) {
        yae_blend(uint8_t);
    } else if (atempo->format == AV_SAMPLE_FMT_S16) {
        yae_blend(int16_t);
    } else if (atempo->format == AV_SAMPLE_FMT_S32) {
        yae_blend(int);
    } else if (atempo->format == AV_SAMPLE_FMT_FLT) {
        yae_blend(float);
    } else if (atempo->format == AV_SAMPLE_FMT_DBL) {
        yae_blend(double);
    }

    // pass-back the updated destination buffer pointer:
    *dst_ref = dst;

    return atempo->position[1] == stop_here ? 0 : AVERROR(EAGAIN);
}

/**
 * Feed as much data to the filter as it is able to consume
 * and receive as much processed data in the destination buffer
 * as it is able to produce or store.
 */
static void
yae_apply(ATempoContext *atempo,
          const uint8_t **src_ref,
          const uint8_t *src_end,
          uint8_t **dst_ref,
          uint8_t *dst_end)
{
    while (1) {
        if (atempo->state == YAE_LOAD_FRAGMENT) {
            // load additional data for the current fragment:
            if (yae_load_frag(atempo, src_ref, src_end) != 0) {
                break;
            }

            // down-mix to mono:
            yae_downmix(atempo, yae_curr_frag(atempo));

            // apply rDFT:
            av_rdft_calc(atempo->real_to_complex, yae_curr_frag(atempo)->xdat);

            // must load the second fragment before alignment can start:
            if (!atempo->nfrag) {
                yae_advance_to_next_frag(atempo);
                continue;
            }

            atempo->state = YAE_ADJUST_POSITION;
        }

        if (atempo->state == YAE_ADJUST_POSITION) {
            // adjust position for better alignment:
            if (yae_adjust_position(atempo)) {
                // reload the fragment at the corrected position, so that the
                // Hann window blending would not require normalization:
                atempo->state = YAE_RELOAD_FRAGMENT;
            } else {
                atempo->state = YAE_OUTPUT_OVERLAP_ADD;
            }
        }

        if (atempo->state == YAE_RELOAD_FRAGMENT) {
            // load additional data if necessary due to position adjustment:
            if (yae_load_frag(atempo, src_ref, src_end) != 0) {
                break;
            }

            // down-mix to mono:
            yae_downmix(atempo, yae_curr_frag(atempo));

            // apply rDFT:
            av_rdft_calc(atempo->real_to_complex, yae_curr_frag(atempo)->xdat);

            atempo->state = YAE_OUTPUT_OVERLAP_ADD;
        }

        if (atempo->state == YAE_OUTPUT_OVERLAP_ADD) {
            // overlap-add and output the result:
            if (yae_overlap_add(atempo, dst_ref, dst_end) != 0) {
                break;
            }

            // advance to the next fragment, repeat:
            yae_advance_to_next_frag(atempo);
            atempo->state = YAE_LOAD_FRAGMENT;
        }
    }
}

/**
 * Flush any buffered data from the filter.
 *
 * @return
 *   0 if all data was completely stored in the dst buffer,
 *   AVERROR(EAGAIN) if more destination buffer space is required.
 */
static int yae_flush(ATempoContext *atempo,
                     uint8_t **dst_ref,
                     uint8_t *dst_end)
{
    AudioFragment *frag = yae_curr_frag(atempo);
    int64_t overlap_end;
    int64_t start_here;
    int64_t stop_here;
    int64_t offset;

    const uint8_t *src;
    uint8_t *dst;

    int src_size;
    int dst_size;
    int nbytes;

    atempo->state = YAE_FLUSH_OUTPUT;

    if (atempo->position[0] == frag->position[0] + frag->nsamples &&
        atempo->position[1] == frag->position[1] + frag->nsamples) {
        // the current fragment is already flushed:
        return 0;
    }

    if (frag->position[0] + frag->nsamples < atempo->position[0]) {
        // finish loading the current (possibly partial) fragment:
        yae_load_frag(atempo, NULL, NULL);

        if (atempo->nfrag) {
            // down-mix to mono:
            yae_downmix(atempo, frag);

            // apply rDFT:
            av_rdft_calc(atempo->real_to_complex, frag->xdat);

            // align current fragment to previous fragment:
            if (yae_adjust_position(atempo)) {
                // reload the current fragment due to adjusted position:
                yae_load_frag(atempo, NULL, NULL);
            }
        }
    }

    // flush the overlap region:
    overlap_end = frag->position[1] + FFMIN(atempo->window / 2,
                                            frag->nsamples);

    while (atempo->position[1] < overlap_end) {
        if (yae_overlap_add(atempo, dst_ref, dst_end) != 0) {
            return AVERROR(EAGAIN);
        }
    }

    // check whether all of the input samples have been consumed:
    if (frag->position[0] + frag->nsamples < atempo->position[0]) {
        yae_advance_to_next_frag(atempo);
        return AVERROR(EAGAIN);
    }

    // flush the remainder of the current fragment:
    start_here = FFMAX(atempo->position[1], overlap_end);
    stop_here  = frag->position[1] + frag->nsamples;
    offset     = start_here - frag->position[1];
    av_assert0(start_here <= stop_here && frag->position[1] <= start_here);

    src = frag->data + offset * atempo->stride;
    dst = (uint8_t *)*dst_ref;

    src_size = (int)(stop_here - start_here) * atempo->stride;
    dst_size = dst_end - dst;
    nbytes = FFMIN(src_size, dst_size);

    memcpy(dst, src, nbytes);
    dst += nbytes;

    atempo->position[1] += (nbytes / atempo->stride);

    // pass-back the updated destination buffer pointer:
    *dst_ref = (uint8_t *)dst;

    return atempo->position[1] == stop_here ? 0 : AVERROR(EAGAIN);
}

static av_cold int init(AVFilterContext *ctx)
{
    ATempoContext *atempo = ctx->priv;
    atempo->format = AV_SAMPLE_FMT_NONE;
    atempo->state  = YAE_LOAD_FRAGMENT;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ATempoContext *atempo = ctx->priv;
    yae_release_buffers(atempo);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterFormats        *formats = NULL;

    // WSOLA necessitates an internal sliding window ring buffer
    // for incoming audio stream.
    //
    // Planar sample formats are too cumbersome to store in a ring buffer,
    // therefore planar sample formats are not supported.
    //
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_U8,
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S32,
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };

    layouts = ff_all_channel_layouts();
    if (!layouts) {
        return AVERROR(ENOMEM);
    }
    ff_set_common_channel_layouts(ctx, layouts);

    formats = ff_make_format_list(sample_fmts);
    if (!formats) {
        return AVERROR(ENOMEM);
    }
    ff_set_common_formats(ctx, formats);

    formats = ff_all_samplerates();
    if (!formats) {
        return AVERROR(ENOMEM);
    }
    ff_set_common_samplerates(ctx, formats);

    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext  *ctx = inlink->dst;
    ATempoContext *atempo = ctx->priv;

    enum AVSampleFormat format = inlink->format;
    int sample_rate = (int)inlink->sample_rate;
    int channels = av_get_channel_layout_nb_channels(inlink->channel_layout);

    ctx->outputs[0]->flags |= FF_LINK_FLAG_REQUEST_LOOP;

    return yae_reset(atempo, format, sample_rate, channels);
}

static int push_samples(ATempoContext *atempo,
                        AVFilterLink *outlink,
                        int n_out)
{
    int ret;

    atempo->dst_buffer->sample_rate = outlink->sample_rate;
    atempo->dst_buffer->nb_samples  = n_out;

    // adjust the PTS:
    atempo->dst_buffer->pts =
        av_rescale_q(atempo->nsamples_out,
                     (AVRational){ 1, outlink->sample_rate },
                     outlink->time_base);

    ret = ff_filter_frame(outlink, atempo->dst_buffer);
    atempo->dst_buffer = NULL;
    atempo->dst        = NULL;
    atempo->dst_end    = NULL;
    if (ret < 0)
        return ret;

    atempo->nsamples_out += n_out;
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *src_buffer)
{
    AVFilterContext  *ctx = inlink->dst;
    ATempoContext *atempo = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    int ret = 0;
    int n_in = src_buffer->nb_samples;
    int n_out = (int)(0.5 + ((double)n_in) / atempo->tempo);

    const uint8_t *src = src_buffer->data[0];
    const uint8_t *src_end = src + n_in * atempo->stride;

    while (src < src_end) {
        if (!atempo->dst_buffer) {
            atempo->dst_buffer = ff_get_audio_buffer(outlink, n_out);
            if (!atempo->dst_buffer)
                return AVERROR(ENOMEM);
            av_frame_copy_props(atempo->dst_buffer, src_buffer);

            atempo->dst = atempo->dst_buffer->data[0];
            atempo->dst_end = atempo->dst + n_out * atempo->stride;
        }

        yae_apply(atempo, &src, src_end, &atempo->dst, atempo->dst_end);

        if (atempo->dst == atempo->dst_end) {
            int n_samples = ((atempo->dst - atempo->dst_buffer->data[0]) /
                             atempo->stride);
            ret = push_samples(atempo, outlink, n_samples);
            if (ret < 0)
                goto end;
        }
    }

    atempo->nsamples_in += n_in;
end:
    av_frame_free(&src_buffer);
    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext  *ctx = outlink->src;
    ATempoContext *atempo = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF) {
        // flush the filter:
        int n_max = atempo->ring;
        int n_out;
        int err = AVERROR(EAGAIN);

        while (err == AVERROR(EAGAIN)) {
            if (!atempo->dst_buffer) {
                atempo->dst_buffer = ff_get_audio_buffer(outlink, n_max);
                if (!atempo->dst_buffer)
                    return AVERROR(ENOMEM);

                atempo->dst = atempo->dst_buffer->data[0];
                atempo->dst_end = atempo->dst + n_max * atempo->stride;
            }

            err = yae_flush(atempo, &atempo->dst, atempo->dst_end);

            n_out = ((atempo->dst - atempo->dst_buffer->data[0]) /
                     atempo->stride);

            if (n_out) {
                ret = push_samples(atempo, outlink, n_out);
            }
        }

        av_frame_free(&atempo->dst_buffer);
        atempo->dst     = NULL;
        atempo->dst_end = NULL;

        return AVERROR_EOF;
    }

    return ret;
}

static int process_command(AVFilterContext *ctx,
                           const char *cmd,
                           const char *arg,
                           char *res,
                           int res_len,
                           int flags)
{
    return !strcmp(cmd, "tempo") ? yae_set_tempo(ctx, arg) : AVERROR(ENOSYS);
}

static const AVFilterPad atempo_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad atempo_outputs[] = {
    {
        .name          = "default",
        .request_frame = request_frame,
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_atempo = {
    .name            = "atempo",
    .description     = NULL_IF_CONFIG_SMALL("Adjust audio tempo."),
    .init            = init,
    .uninit          = uninit,
    .query_formats   = query_formats,
    .process_command = process_command,
    .priv_size       = sizeof(ATempoContext),
    .priv_class      = &atempo_class,
    .inputs          = atempo_inputs,
    .outputs         = atempo_outputs,
};
