/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
 *
 * Triangular with Noise Shaping is based on opusfile.
 * Copyright (c) 1994-2012 by the Xiph.Org Foundation and contributors
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
 * Dithered Audio Sample Quantization
 *
 * Converts from dbl, flt, or s32 to s16 using dithering.
 */

#include <math.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "libavutil/samplefmt.h"
#include "audio_convert.h"
#include "dither.h"
#include "internal.h"

typedef struct DitherState {
    int mute;
    unsigned int seed;
    AVLFG lfg;
    float *noise_buf;
    int noise_buf_size;
    int noise_buf_ptr;
    float dither_a[4];
    float dither_b[4];
} DitherState;

struct DitherContext {
    DitherDSPContext  ddsp;
    enum AVResampleDitherMethod method;
    int apply_map;
    ChannelMapInfo *ch_map_info;

    int mute_dither_threshold;  // threshold for disabling dither
    int mute_reset_threshold;   // threshold for resetting noise shaping
    const float *ns_coef_b;     // noise shaping coeffs
    const float *ns_coef_a;     // noise shaping coeffs

    int channels;
    DitherState *state;         // dither states for each channel

    AudioData *flt_data;        // input data in fltp
    AudioData *s16_data;        // dithered output in s16p
    AudioConvert *ac_in;        // converter for input to fltp
    AudioConvert *ac_out;       // converter for s16p to s16 (if needed)

    void (*quantize)(int16_t *dst, const float *src, float *dither, int len);
    int samples_align;
};

/* mute threshold, in seconds */
#define MUTE_THRESHOLD_SEC 0.000333

/* scale factor for 16-bit output.
   The signal is attenuated slightly to avoid clipping */
#define S16_SCALE 32753.0f

/* scale to convert lfg from INT_MIN/INT_MAX to -0.5/0.5 */
#define LFG_SCALE (1.0f / (2.0f * INT32_MAX))

/* noise shaping coefficients */

static const float ns_48_coef_b[4] = {
    2.2374f, -0.7339f, -0.1251f, -0.6033f
};

static const float ns_48_coef_a[4] = {
    0.9030f, 0.0116f, -0.5853f, -0.2571f
};

static const float ns_44_coef_b[4] = {
    2.2061f, -0.4707f, -0.2534f, -0.6213f
};

static const float ns_44_coef_a[4] = {
    1.0587f, 0.0676f, -0.6054f, -0.2738f
};

static void dither_int_to_float_rectangular_c(float *dst, int *src, int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src[i] * LFG_SCALE;
}

static void dither_int_to_float_triangular_c(float *dst, int *src0, int len)
{
    int i;
    int *src1  = src0 + len;

    for (i = 0; i < len; i++) {
        float r = src0[i] * LFG_SCALE;
        r      += src1[i] * LFG_SCALE;
        dst[i]  = r;
    }
}

static void quantize_c(int16_t *dst, const float *src, float *dither, int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = av_clip_int16(lrintf(src[i] * S16_SCALE + dither[i]));
}

#define SQRT_1_6 0.40824829046386301723f

static void dither_highpass_filter(float *src, int len)
{
    int i;

    /* filter is from libswresample in FFmpeg */
    for (i = 0; i < len - 2; i++)
        src[i] = (-src[i] + 2 * src[i + 1] - src[i + 2]) * SQRT_1_6;
}

static int generate_dither_noise(DitherContext *c, DitherState *state,
                                 int min_samples)
{
    int i;
    int nb_samples  = FFALIGN(min_samples, 16) + 16;
    int buf_samples = nb_samples *
                      (c->method == AV_RESAMPLE_DITHER_RECTANGULAR ? 1 : 2);
    unsigned int *noise_buf_ui;

    av_freep(&state->noise_buf);
    state->noise_buf_size = state->noise_buf_ptr = 0;

    state->noise_buf = av_malloc(buf_samples * sizeof(*state->noise_buf));
    if (!state->noise_buf)
        return AVERROR(ENOMEM);
    state->noise_buf_size = FFALIGN(min_samples, 16);
    noise_buf_ui          = (unsigned int *)state->noise_buf;

    av_lfg_init(&state->lfg, state->seed);
    for (i = 0; i < buf_samples; i++)
        noise_buf_ui[i] = av_lfg_get(&state->lfg);

    c->ddsp.dither_int_to_float(state->noise_buf, noise_buf_ui, nb_samples);

    if (c->method == AV_RESAMPLE_DITHER_TRIANGULAR_HP)
        dither_highpass_filter(state->noise_buf, nb_samples);

    return 0;
}

static void quantize_triangular_ns(DitherContext *c, DitherState *state,
                                   int16_t *dst, const float *src,
                                   int nb_samples)
{
    int i, j;
    float *dither = &state->noise_buf[state->noise_buf_ptr];

    if (state->mute > c->mute_reset_threshold)
        memset(state->dither_a, 0, sizeof(state->dither_a));

    for (i = 0; i < nb_samples; i++) {
        float err = 0;
        float sample = src[i] * S16_SCALE;

        for (j = 0; j < 4; j++) {
            err += c->ns_coef_b[j] * state->dither_b[j] -
                   c->ns_coef_a[j] * state->dither_a[j];
        }
        for (j = 3; j > 0; j--) {
            state->dither_a[j] = state->dither_a[j - 1];
            state->dither_b[j] = state->dither_b[j - 1];
        }
        state->dither_a[0] = err;
        sample -= err;

        if (state->mute > c->mute_dither_threshold) {
            dst[i]             = av_clip_int16(lrintf(sample));
            state->dither_b[0] = 0;
        } else {
            dst[i]             = av_clip_int16(lrintf(sample + dither[i]));
            state->dither_b[0] = av_clipf(dst[i] - sample, -1.5f, 1.5f);
        }

        state->mute++;
        if (src[i])
            state->mute = 0;
    }
}

static int convert_samples(DitherContext *c, int16_t **dst, float * const *src,
                           int channels, int nb_samples)
{
    int ch, ret;
    int aligned_samples = FFALIGN(nb_samples, 16);

    for (ch = 0; ch < channels; ch++) {
        DitherState *state = &c->state[ch];

        if (state->noise_buf_size < aligned_samples) {
            ret = generate_dither_noise(c, state, nb_samples);
            if (ret < 0)
                return ret;
        } else if (state->noise_buf_size - state->noise_buf_ptr < aligned_samples) {
            state->noise_buf_ptr = 0;
        }

        if (c->method == AV_RESAMPLE_DITHER_TRIANGULAR_NS) {
            quantize_triangular_ns(c, state, dst[ch], src[ch], nb_samples);
        } else {
            c->quantize(dst[ch], src[ch],
                        &state->noise_buf[state->noise_buf_ptr],
                        FFALIGN(nb_samples, c->samples_align));
        }

        state->noise_buf_ptr += aligned_samples;
    }

    return 0;
}

int ff_convert_dither(DitherContext *c, AudioData *dst, AudioData *src)
{
    int ret;
    AudioData *flt_data;

    /* output directly to dst if it is planar */
    if (dst->sample_fmt == AV_SAMPLE_FMT_S16P)
        c->s16_data = dst;
    else {
        /* make sure s16_data is large enough for the output */
        ret = ff_audio_data_realloc(c->s16_data, src->nb_samples);
        if (ret < 0)
            return ret;
    }

    if (src->sample_fmt != AV_SAMPLE_FMT_FLTP || c->apply_map) {
        /* make sure flt_data is large enough for the input */
        ret = ff_audio_data_realloc(c->flt_data, src->nb_samples);
        if (ret < 0)
            return ret;
        flt_data = c->flt_data;
    }

    if (src->sample_fmt != AV_SAMPLE_FMT_FLTP) {
        /* convert input samples to fltp and scale to s16 range */
        ret = ff_audio_convert(c->ac_in, flt_data, src);
        if (ret < 0)
            return ret;
    } else if (c->apply_map) {
        ret = ff_audio_data_copy(flt_data, src, c->ch_map_info);
        if (ret < 0)
            return ret;
    } else {
        flt_data = src;
    }

    /* check alignment and padding constraints */
    if (c->method != AV_RESAMPLE_DITHER_TRIANGULAR_NS) {
        int ptr_align     = FFMIN(flt_data->ptr_align,     c->s16_data->ptr_align);
        int samples_align = FFMIN(flt_data->samples_align, c->s16_data->samples_align);
        int aligned_len   = FFALIGN(src->nb_samples, c->ddsp.samples_align);

        if (!(ptr_align % c->ddsp.ptr_align) && samples_align >= aligned_len) {
            c->quantize      = c->ddsp.quantize;
            c->samples_align = c->ddsp.samples_align;
        } else {
            c->quantize      = quantize_c;
            c->samples_align = 1;
        }
    }

    ret = convert_samples(c, (int16_t **)c->s16_data->data,
                          (float * const *)flt_data->data, src->channels,
                          src->nb_samples);
    if (ret < 0)
        return ret;

    c->s16_data->nb_samples = src->nb_samples;

    /* interleave output to dst if needed */
    if (dst->sample_fmt == AV_SAMPLE_FMT_S16) {
        ret = ff_audio_convert(c->ac_out, dst, c->s16_data);
        if (ret < 0)
            return ret;
    } else
        c->s16_data = NULL;

    return 0;
}

void ff_dither_free(DitherContext **cp)
{
    DitherContext *c = *cp;
    int ch;

    if (!c)
        return;
    ff_audio_data_free(&c->flt_data);
    ff_audio_data_free(&c->s16_data);
    ff_audio_convert_free(&c->ac_in);
    ff_audio_convert_free(&c->ac_out);
    for (ch = 0; ch < c->channels; ch++)
        av_free(c->state[ch].noise_buf);
    av_free(c->state);
    av_freep(cp);
}

static av_cold void dither_init(DitherDSPContext *ddsp,
                                enum AVResampleDitherMethod method)
{
    ddsp->quantize      = quantize_c;
    ddsp->ptr_align     = 1;
    ddsp->samples_align = 1;

    if (method == AV_RESAMPLE_DITHER_RECTANGULAR)
        ddsp->dither_int_to_float = dither_int_to_float_rectangular_c;
    else
        ddsp->dither_int_to_float = dither_int_to_float_triangular_c;

    if (ARCH_X86)
        ff_dither_init_x86(ddsp, method);
}

DitherContext *ff_dither_alloc(AVAudioResampleContext *avr,
                               enum AVSampleFormat out_fmt,
                               enum AVSampleFormat in_fmt,
                               int channels, int sample_rate, int apply_map)
{
    AVLFG seed_gen;
    DitherContext *c;
    int ch;

    if (av_get_packed_sample_fmt(out_fmt) != AV_SAMPLE_FMT_S16 ||
        av_get_bytes_per_sample(in_fmt) <= 2) {
        av_log(avr, AV_LOG_ERROR, "dithering %s to %s is not supported\n",
               av_get_sample_fmt_name(in_fmt), av_get_sample_fmt_name(out_fmt));
        return NULL;
    }

    c = av_mallocz(sizeof(*c));
    if (!c)
        return NULL;

    c->apply_map = apply_map;
    if (apply_map)
        c->ch_map_info = &avr->ch_map_info;

    if (avr->dither_method == AV_RESAMPLE_DITHER_TRIANGULAR_NS &&
        sample_rate != 48000 && sample_rate != 44100) {
        av_log(avr, AV_LOG_WARNING, "sample rate must be 48000 or 44100 Hz "
               "for triangular_ns dither. using triangular_hp instead.\n");
        avr->dither_method = AV_RESAMPLE_DITHER_TRIANGULAR_HP;
    }
    c->method = avr->dither_method;
    dither_init(&c->ddsp, c->method);

    if (c->method == AV_RESAMPLE_DITHER_TRIANGULAR_NS) {
        if (sample_rate == 48000) {
            c->ns_coef_b = ns_48_coef_b;
            c->ns_coef_a = ns_48_coef_a;
        } else {
            c->ns_coef_b = ns_44_coef_b;
            c->ns_coef_a = ns_44_coef_a;
        }
    }

    /* Either s16 or s16p output format is allowed, but s16p is used
       internally, so we need to use a temp buffer and interleave if the output
       format is s16 */
    if (out_fmt != AV_SAMPLE_FMT_S16P) {
        c->s16_data = ff_audio_data_alloc(channels, 1024, AV_SAMPLE_FMT_S16P,
                                          "dither s16 buffer");
        if (!c->s16_data)
            goto fail;

        c->ac_out = ff_audio_convert_alloc(avr, out_fmt, AV_SAMPLE_FMT_S16P,
                                           channels, sample_rate, 0);
        if (!c->ac_out)
            goto fail;
    }

    if (in_fmt != AV_SAMPLE_FMT_FLTP || c->apply_map) {
        c->flt_data = ff_audio_data_alloc(channels, 1024, AV_SAMPLE_FMT_FLTP,
                                          "dither flt buffer");
        if (!c->flt_data)
            goto fail;
    }
    if (in_fmt != AV_SAMPLE_FMT_FLTP) {
        c->ac_in = ff_audio_convert_alloc(avr, AV_SAMPLE_FMT_FLTP, in_fmt,
                                          channels, sample_rate, c->apply_map);
        if (!c->ac_in)
            goto fail;
    }

    c->state = av_mallocz(channels * sizeof(*c->state));
    if (!c->state)
        goto fail;
    c->channels = channels;

    /* calculate thresholds for turning off dithering during periods of
       silence to avoid replacing digital silence with quiet dither noise */
    c->mute_dither_threshold = lrintf(sample_rate * MUTE_THRESHOLD_SEC);
    c->mute_reset_threshold  = c->mute_dither_threshold * 4;

    /* initialize dither states */
    av_lfg_init(&seed_gen, 0xC0FFEE);
    for (ch = 0; ch < channels; ch++) {
        DitherState *state = &c->state[ch];
        state->mute = c->mute_reset_threshold + 1;
        state->seed = av_lfg_get(&seed_gen);
        generate_dither_noise(c, state, FFMAX(32768, sample_rate / 2));
    }

    return c;

fail:
    ff_dither_free(&c);
    return NULL;
}
