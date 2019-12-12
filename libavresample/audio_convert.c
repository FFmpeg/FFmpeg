/*
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

#include <stdint.h>

#include "config.h"
#include "libavutil/common.h"
#include "libavutil/libm.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/samplefmt.h"
#include "audio_convert.h"
#include "audio_data.h"
#include "dither.h"

enum ConvFuncType {
    CONV_FUNC_TYPE_FLAT,
    CONV_FUNC_TYPE_INTERLEAVE,
    CONV_FUNC_TYPE_DEINTERLEAVE,
};

typedef void (conv_func_flat)(uint8_t *out, const uint8_t *in, int len);

typedef void (conv_func_interleave)(uint8_t *out, uint8_t *const *in,
                                    int len, int channels);

typedef void (conv_func_deinterleave)(uint8_t **out, const uint8_t *in, int len,
                                      int channels);

struct AudioConvert {
    AVAudioResampleContext *avr;
    DitherContext *dc;
    enum AVSampleFormat in_fmt;
    enum AVSampleFormat out_fmt;
    int apply_map;
    int channels;
    int planes;
    int ptr_align;
    int samples_align;
    int has_optimized_func;
    const char *func_descr;
    const char *func_descr_generic;
    enum ConvFuncType func_type;
    conv_func_flat         *conv_flat;
    conv_func_flat         *conv_flat_generic;
    conv_func_interleave   *conv_interleave;
    conv_func_interleave   *conv_interleave_generic;
    conv_func_deinterleave *conv_deinterleave;
    conv_func_deinterleave *conv_deinterleave_generic;
};

void ff_audio_convert_set_func(AudioConvert *ac, enum AVSampleFormat out_fmt,
                               enum AVSampleFormat in_fmt, int channels,
                               int ptr_align, int samples_align,
                               const char *descr, void *conv)
{
    int found = 0;

    switch (ac->func_type) {
    case CONV_FUNC_TYPE_FLAT:
        if (av_get_packed_sample_fmt(ac->in_fmt)  == in_fmt &&
            av_get_packed_sample_fmt(ac->out_fmt) == out_fmt) {
            ac->conv_flat     = conv;
            ac->func_descr    = descr;
            ac->ptr_align     = ptr_align;
            ac->samples_align = samples_align;
            if (ptr_align == 1 && samples_align == 1) {
                ac->conv_flat_generic  = conv;
                ac->func_descr_generic = descr;
            } else {
                ac->has_optimized_func = 1;
            }
            found = 1;
        }
        break;
    case CONV_FUNC_TYPE_INTERLEAVE:
        if (ac->in_fmt == in_fmt && ac->out_fmt == out_fmt &&
            (!channels || ac->channels == channels)) {
            ac->conv_interleave = conv;
            ac->func_descr      = descr;
            ac->ptr_align       = ptr_align;
            ac->samples_align   = samples_align;
            if (ptr_align == 1 && samples_align == 1) {
                ac->conv_interleave_generic = conv;
                ac->func_descr_generic      = descr;
            } else {
                ac->has_optimized_func = 1;
            }
            found = 1;
        }
        break;
    case CONV_FUNC_TYPE_DEINTERLEAVE:
        if (ac->in_fmt == in_fmt && ac->out_fmt == out_fmt &&
            (!channels || ac->channels == channels)) {
            ac->conv_deinterleave = conv;
            ac->func_descr        = descr;
            ac->ptr_align         = ptr_align;
            ac->samples_align     = samples_align;
            if (ptr_align == 1 && samples_align == 1) {
                ac->conv_deinterleave_generic = conv;
                ac->func_descr_generic        = descr;
            } else {
                ac->has_optimized_func = 1;
            }
            found = 1;
        }
        break;
    }
    if (found) {
        av_log(ac->avr, AV_LOG_DEBUG, "audio_convert: found function: %-4s "
               "to %-4s (%s)\n", av_get_sample_fmt_name(ac->in_fmt),
               av_get_sample_fmt_name(ac->out_fmt), descr);
    }
}

#define CONV_FUNC_NAME(dst_fmt, src_fmt) conv_ ## src_fmt ## _to_ ## dst_fmt

#define CONV_LOOP(otype, expr)                                              \
    do {                                                                    \
        *(otype *)po = expr;                                                \
        pi += is;                                                           \
        po += os;                                                           \
    } while (po < end);                                                     \

#define CONV_FUNC_FLAT(ofmt, otype, ifmt, itype, expr)                      \
static void CONV_FUNC_NAME(ofmt, ifmt)(uint8_t *out, const uint8_t *in,     \
                                       int len)                             \
{                                                                           \
    int is       = sizeof(itype);                                           \
    int os       = sizeof(otype);                                           \
    const uint8_t *pi = in;                                                 \
    uint8_t       *po = out;                                                \
    uint8_t *end = out + os * len;                                          \
    CONV_LOOP(otype, expr)                                                  \
}

#define CONV_FUNC_INTERLEAVE(ofmt, otype, ifmt, itype, expr)                \
static void CONV_FUNC_NAME(ofmt, ifmt)(uint8_t *out, const uint8_t **in,    \
                                       int len, int channels)               \
{                                                                           \
    int ch;                                                                 \
    int out_bps = sizeof(otype);                                            \
    int is      = sizeof(itype);                                            \
    int os      = channels * out_bps;                                       \
    for (ch = 0; ch < channels; ch++) {                                     \
        const uint8_t *pi = in[ch];                                         \
        uint8_t       *po = out + ch * out_bps;                             \
        uint8_t      *end = po + os * len;                                  \
        CONV_LOOP(otype, expr)                                              \
    }                                                                       \
}

#define CONV_FUNC_DEINTERLEAVE(ofmt, otype, ifmt, itype, expr)              \
static void CONV_FUNC_NAME(ofmt, ifmt)(uint8_t **out, const uint8_t *in,    \
                                       int len, int channels)               \
{                                                                           \
    int ch;                                                                 \
    int in_bps = sizeof(itype);                                             \
    int is     = channels * in_bps;                                         \
    int os     = sizeof(otype);                                             \
    for (ch = 0; ch < channels; ch++) {                                     \
        const uint8_t *pi = in  + ch * in_bps;                              \
        uint8_t       *po = out[ch];                                        \
        uint8_t      *end = po + os * len;                                  \
        CONV_LOOP(otype, expr)                                              \
    }                                                                       \
}

#define CONV_FUNC_GROUP(ofmt, otype, ifmt, itype, expr) \
CONV_FUNC_FLAT(        ofmt,      otype, ifmt,      itype, expr) \
CONV_FUNC_INTERLEAVE(  ofmt,      otype, ifmt ## P, itype, expr) \
CONV_FUNC_DEINTERLEAVE(ofmt ## P, otype, ifmt,      itype, expr)

CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  uint8_t, AV_SAMPLE_FMT_U8,  uint8_t,  *(const uint8_t *)pi)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_U8,  uint8_t, (*(const uint8_t *)pi - 0x80) <<  8)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_U8,  uint8_t, (*(const uint8_t *)pi - 0x80) << 24)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, float,   AV_SAMPLE_FMT_U8,  uint8_t, (*(const uint8_t *)pi - 0x80) * (1.0f / (1 << 7)))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, double,  AV_SAMPLE_FMT_U8,  uint8_t, (*(const uint8_t *)pi - 0x80) * (1.0  / (1 << 7)))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  uint8_t, AV_SAMPLE_FMT_S16, int16_t, (*(const int16_t *)pi >> 8) + 0x80)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_S16, int16_t,  *(const int16_t *)pi)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_S16, int16_t,  *(const int16_t *)pi << 16)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, float,   AV_SAMPLE_FMT_S16, int16_t,  *(const int16_t *)pi * (1.0f / (1 << 15)))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, double,  AV_SAMPLE_FMT_S16, int16_t,  *(const int16_t *)pi * (1.0  / (1 << 15)))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  uint8_t, AV_SAMPLE_FMT_S32, int32_t, (*(const int32_t *)pi >> 24) + 0x80)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_S32, int32_t,  *(const int32_t *)pi >> 16)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_S32, int32_t,  *(const int32_t *)pi)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, float,   AV_SAMPLE_FMT_S32, int32_t,  *(const int32_t *)pi * (1.0f / (1U << 31)))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, double,  AV_SAMPLE_FMT_S32, int32_t,  *(const int32_t *)pi * (1.0  / (1U << 31)))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  uint8_t, AV_SAMPLE_FMT_FLT, float,   av_clip_uint8(  lrintf(*(const float *)pi * (1  <<  7)) + 0x80))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_FLT, float,   av_clip_int16(  lrintf(*(const float *)pi * (1  << 15))))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_FLT, float,   av_clipl_int32(llrintf(*(const float *)pi * (1U << 31))))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, float,   AV_SAMPLE_FMT_FLT, float,   *(const float *)pi)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, double,  AV_SAMPLE_FMT_FLT, float,   *(const float *)pi)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  uint8_t, AV_SAMPLE_FMT_DBL, double,  av_clip_uint8(  lrint(*(const double *)pi * (1  <<  7)) + 0x80))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_DBL, double,  av_clip_int16(  lrint(*(const double *)pi * (1  << 15))))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_DBL, double,  av_clipl_int32(llrint(*(const double *)pi * (1U << 31))))
CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, float,   AV_SAMPLE_FMT_DBL, double,  *(const double *)pi)
CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, double,  AV_SAMPLE_FMT_DBL, double,  *(const double *)pi)

#define SET_CONV_FUNC_GROUP(ofmt, ifmt)                                                             \
ff_audio_convert_set_func(ac, ofmt,      ifmt,      0, 1, 1, "C", CONV_FUNC_NAME(ofmt,      ifmt)); \
ff_audio_convert_set_func(ac, ofmt ## P, ifmt,      0, 1, 1, "C", CONV_FUNC_NAME(ofmt ## P, ifmt)); \
ff_audio_convert_set_func(ac, ofmt,      ifmt ## P, 0, 1, 1, "C", CONV_FUNC_NAME(ofmt,      ifmt ## P));

static void set_generic_function(AudioConvert *ac)
{
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_U8)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_U8)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_U8)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_U8)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_U8)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_S16)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_S16)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_S32)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_S32)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_FLT)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_FLT)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLT)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_FLT)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_DBL)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_DBL)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_DBL)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_DBL)
    SET_CONV_FUNC_GROUP(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBL)
}

void ff_audio_convert_free(AudioConvert **ac)
{
    if (!*ac)
        return;
    ff_dither_free(&(*ac)->dc);
    av_freep(ac);
}

AudioConvert *ff_audio_convert_alloc(AVAudioResampleContext *avr,
                                     enum AVSampleFormat out_fmt,
                                     enum AVSampleFormat in_fmt,
                                     int channels, int sample_rate,
                                     int apply_map)
{
    AudioConvert *ac;
    int in_planar, out_planar;

    ac = av_mallocz(sizeof(*ac));
    if (!ac)
        return NULL;

    ac->avr      = avr;
    ac->out_fmt  = out_fmt;
    ac->in_fmt   = in_fmt;
    ac->channels = channels;
    ac->apply_map = apply_map;

    if (avr->dither_method != AV_RESAMPLE_DITHER_NONE          &&
        av_get_packed_sample_fmt(out_fmt) == AV_SAMPLE_FMT_S16 &&
        av_get_bytes_per_sample(in_fmt) > 2) {
        ac->dc = ff_dither_alloc(avr, out_fmt, in_fmt, channels, sample_rate,
                                 apply_map);
        if (!ac->dc) {
            av_free(ac);
            return NULL;
        }
        return ac;
    }

    in_planar  = ff_sample_fmt_is_planar(in_fmt, channels);
    out_planar = ff_sample_fmt_is_planar(out_fmt, channels);

    if (in_planar == out_planar) {
        ac->func_type = CONV_FUNC_TYPE_FLAT;
        ac->planes    = in_planar ? ac->channels : 1;
    } else if (in_planar)
        ac->func_type = CONV_FUNC_TYPE_INTERLEAVE;
    else
        ac->func_type = CONV_FUNC_TYPE_DEINTERLEAVE;

    set_generic_function(ac);

    if (ARCH_AARCH64)
        ff_audio_convert_init_aarch64(ac);
    if (ARCH_ARM)
        ff_audio_convert_init_arm(ac);
    if (ARCH_X86)
        ff_audio_convert_init_x86(ac);

    return ac;
}

int ff_audio_convert(AudioConvert *ac, AudioData *out, AudioData *in)
{
    int use_generic = 1;
    int len         = in->nb_samples;
    int p;

    if (ac->dc) {
        /* dithered conversion */
        av_log(ac->avr, AV_LOG_TRACE, "%d samples - audio_convert: %s to %s (dithered)\n",
                len, av_get_sample_fmt_name(ac->in_fmt),
                av_get_sample_fmt_name(ac->out_fmt));

        return ff_convert_dither(ac->dc, out, in);
    }

    /* determine whether to use the optimized function based on pointer and
       samples alignment in both the input and output */
    if (ac->has_optimized_func) {
        int ptr_align     = FFMIN(in->ptr_align,     out->ptr_align);
        int samples_align = FFMIN(in->samples_align, out->samples_align);
        int aligned_len   = FFALIGN(len, ac->samples_align);
        if (!(ptr_align % ac->ptr_align) && samples_align >= aligned_len) {
            len = aligned_len;
            use_generic = 0;
        }
    }
    av_log(ac->avr, AV_LOG_TRACE, "%d samples - audio_convert: %s to %s (%s)\n", len,
            av_get_sample_fmt_name(ac->in_fmt),
            av_get_sample_fmt_name(ac->out_fmt),
            use_generic ? ac->func_descr_generic : ac->func_descr);

    if (ac->apply_map) {
        ChannelMapInfo *map = &ac->avr->ch_map_info;

        if (!ff_sample_fmt_is_planar(ac->out_fmt, ac->channels)) {
            av_log(ac->avr, AV_LOG_ERROR, "cannot remap packed format during conversion\n");
            return AVERROR(EINVAL);
        }

        if (map->do_remap) {
            if (ff_sample_fmt_is_planar(ac->in_fmt, ac->channels)) {
                conv_func_flat *convert = use_generic ? ac->conv_flat_generic :
                                                        ac->conv_flat;

                for (p = 0; p < ac->planes; p++)
                    if (map->channel_map[p] >= 0)
                        convert(out->data[p], in->data[map->channel_map[p]], len);
            } else {
                uint8_t *data[AVRESAMPLE_MAX_CHANNELS];
                conv_func_deinterleave *convert = use_generic ?
                                                  ac->conv_deinterleave_generic :
                                                  ac->conv_deinterleave;

                for (p = 0; p < ac->channels; p++)
                    data[map->input_map[p]] = out->data[p];

                convert(data, in->data[0], len, ac->channels);
            }
        }
        if (map->do_copy || map->do_zero) {
            for (p = 0; p < ac->planes; p++) {
                if (map->channel_copy[p])
                    memcpy(out->data[p], out->data[map->channel_copy[p]],
                           len * out->stride);
                else if (map->channel_zero[p])
                    av_samples_set_silence(&out->data[p], 0, len, 1, ac->out_fmt);
            }
        }
    } else {
        switch (ac->func_type) {
        case CONV_FUNC_TYPE_FLAT: {
            if (!in->is_planar)
                len *= in->channels;
            if (use_generic) {
                for (p = 0; p < ac->planes; p++)
                    ac->conv_flat_generic(out->data[p], in->data[p], len);
            } else {
                for (p = 0; p < ac->planes; p++)
                    ac->conv_flat(out->data[p], in->data[p], len);
            }
            break;
        }
        case CONV_FUNC_TYPE_INTERLEAVE:
            if (use_generic)
                ac->conv_interleave_generic(out->data[0], in->data, len,
                                            ac->channels);
            else
                ac->conv_interleave(out->data[0], in->data, len, ac->channels);
            break;
        case CONV_FUNC_TYPE_DEINTERLEAVE:
            if (use_generic)
                ac->conv_deinterleave_generic(out->data, in->data[0], len,
                                              ac->channels);
            else
                ac->conv_deinterleave(out->data, in->data[0], len,
                                      ac->channels);
            break;
        }
    }

    out->nb_samples = in->nb_samples;
    return 0;
}
