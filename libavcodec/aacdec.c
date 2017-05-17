/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 * Copyright (c) 2008-2013 Alex Converse <alex.converse@gmail.com>
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
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
 * AAC decoder
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#define FFT_FLOAT 1
#define FFT_FIXED_32 0
#define USE_FIXED 0

#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "fft.h"
#include "mdct15.h"
#include "lpc.h"
#include "kbdwin.h"
#include "sinewin.h"

#include "aac.h"
#include "aactab.h"
#include "aacdectab.h"
#include "cbrt_data.h"
#include "sbr.h"
#include "aacsbr.h"
#include "mpeg4audio.h"
#include "aacadtsdec.h"
#include "profiles.h"
#include "libavutil/intfloat.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#if ARCH_ARM
#   include "arm/aac.h"
#elif ARCH_MIPS
#   include "mips/aacdec_mips.h"
#endif

static av_always_inline void reset_predict_state(PredictorState *ps)
{
    ps->r0   = 0.0f;
    ps->r1   = 0.0f;
    ps->cor0 = 0.0f;
    ps->cor1 = 0.0f;
    ps->var0 = 1.0f;
    ps->var1 = 1.0f;
}

#ifndef VMUL2
static inline float *VMUL2(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 15] * s;
    *dst++ = v[idx>>4 & 15] * s;
    return dst;
}
#endif

#ifndef VMUL4
static inline float *VMUL4(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 3] * s;
    *dst++ = v[idx>>2 & 3] * s;
    *dst++ = v[idx>>4 & 3] * s;
    *dst++ = v[idx>>6 & 3] * s;
    return dst;
}
#endif

#ifndef VMUL2S
static inline float *VMUL2S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    union av_intfloat32 s0, s1;

    s0.f = s1.f = *scale;
    s0.i ^= sign >> 1 << 31;
    s1.i ^= sign      << 31;

    *dst++ = v[idx    & 15] * s0.f;
    *dst++ = v[idx>>4 & 15] * s1.f;

    return dst;
}
#endif

#ifndef VMUL4S
static inline float *VMUL4S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    unsigned nz = idx >> 12;
    union av_intfloat32 s = { .f = *scale };
    union av_intfloat32 t;

    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx    & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>2 & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>4 & 3] * t.f;

    sign <<= nz & 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>6 & 3] * t.f;

    return dst;
}
#endif

static av_always_inline float flt16_round(float pf)
{
    union av_intfloat32 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00008000U) & 0xFFFF0000U;
    return tmp.f;
}

static av_always_inline float flt16_even(float pf)
{
    union av_intfloat32 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00007FFFU + (tmp.i & 0x00010000U >> 16)) & 0xFFFF0000U;
    return tmp.f;
}

static av_always_inline float flt16_trunc(float pf)
{
    union av_intfloat32 pun;
    pun.f = pf;
    pun.i &= 0xFFFF0000U;
    return pun.f;
}

static av_always_inline void predict(PredictorState *ps, float *coef,
                                     int output_enable)
{
    const float a     = 0.953125; // 61.0 / 64
    const float alpha = 0.90625;  // 29.0 / 32
    float e0, e1;
    float pv;
    float k1, k2;
    float   r0 = ps->r0,     r1 = ps->r1;
    float cor0 = ps->cor0, cor1 = ps->cor1;
    float var0 = ps->var0, var1 = ps->var1;

    k1 = var0 > 1 ? cor0 * flt16_even(a / var0) : 0;
    k2 = var1 > 1 ? cor1 * flt16_even(a / var1) : 0;

    pv = flt16_round(k1 * r0 + k2 * r1);
    if (output_enable)
        *coef += pv;

    e0 = *coef;
    e1 = e0 - k1 * r0;

    ps->cor1 = flt16_trunc(alpha * cor1 + r1 * e1);
    ps->var1 = flt16_trunc(alpha * var1 + 0.5f * (r1 * r1 + e1 * e1));
    ps->cor0 = flt16_trunc(alpha * cor0 + r0 * e0);
    ps->var0 = flt16_trunc(alpha * var0 + 0.5f * (r0 * r0 + e0 * e0));

    ps->r1 = flt16_trunc(a * (r0 - k1 * e0));
    ps->r0 = flt16_trunc(a * e0);
}

/**
 * Apply dependent channel coupling (applied before IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_dependent_coupling(AACContext *ac,
                                     SingleChannelElement *target,
                                     ChannelElement *cce, int index)
{
    IndividualChannelStream *ics = &cce->ch[0].ics;
    const uint16_t *offsets = ics->swb_offset;
    float *dest = target->coeffs;
    const float *src = cce->ch[0].coeffs;
    int g, i, group, k, idx = 0;
    if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Dependent coupling is not supported together with LTP\n");
        return;
    }
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cce->ch[0].band_type[idx] != ZERO_BT) {
                const float gain = cce->coup.gain[index][idx];
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i + 1]; k++) {
                        // FIXME: SIMDify
                        dest[group * 128 + k] += gain * src[group * 128 + k];
                    }
                }
            }
        }
        dest += ics->group_len[g] * 128;
        src  += ics->group_len[g] * 128;
    }
}

/**
 * Apply independent channel coupling (applied after IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_independent_coupling(AACContext *ac,
                                       SingleChannelElement *target,
                                       ChannelElement *cce, int index)
{
    int i;
    const float gain = cce->coup.gain[index][0];
    const float *src = cce->ch[0].ret;
    float *dest = target->ret;
    const int len = 1024 << (ac->oc[1].m4ac.sbr == 1);

    for (i = 0; i < len; i++)
        dest[i] += gain * src[i];
}

#include "aacdec_template.c"

#define LOAS_SYNC_WORD   0x2b7       ///< 11 bits LOAS sync word

struct LATMContext {
    AACContext aac_ctx;     ///< containing AACContext
    int initialized;        ///< initialized after a valid extradata was seen

    // parser data
    int audio_mux_version_A; ///< LATM syntax version
    int frame_length_type;   ///< 0/1 variable/fixed frame length
    int frame_length;        ///< frame length for fixed frame length
};

static inline uint32_t latm_get_value(GetBitContext *b)
{
    int length = get_bits(b, 2);

    return get_bits_long(b, (length+1)*8);
}

static int latm_decode_audio_specific_config(struct LATMContext *latmctx,
                                             GetBitContext *gb, int asclen)
{
    AACContext *ac        = &latmctx->aac_ctx;
    AVCodecContext *avctx = ac->avctx;
    MPEG4AudioConfig m4ac = { 0 };
    GetBitContext gbc;
    int config_start_bit  = get_bits_count(gb);
    int sync_extension    = 0;
    int bits_consumed, esize, i;

    if (asclen > 0) {
        sync_extension = 1;
        asclen         = FFMIN(asclen, get_bits_left(gb));
        init_get_bits(&gbc, gb->buffer, config_start_bit + asclen);
        skip_bits_long(&gbc, config_start_bit);
    } else if (asclen == 0) {
        gbc = *gb;
    } else {
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(gb) <= 0)
        return AVERROR_INVALIDDATA;

    bits_consumed = decode_audio_specific_config_gb(NULL, avctx, &m4ac,
                                                    &gbc, config_start_bit,
                                                    sync_extension);

    if (bits_consumed < config_start_bit)
        return AVERROR_INVALIDDATA;
    bits_consumed -= config_start_bit;

    if (asclen == 0)
      asclen = bits_consumed;

    if (!latmctx->initialized ||
        ac->oc[1].m4ac.sample_rate != m4ac.sample_rate ||
        ac->oc[1].m4ac.chan_config != m4ac.chan_config) {

        if(latmctx->initialized) {
            av_log(avctx, AV_LOG_INFO, "audio config changed\n");
        } else {
            av_log(avctx, AV_LOG_DEBUG, "initializing latmctx\n");
        }
        latmctx->initialized = 0;

        esize = (asclen + 7) / 8;

        if (avctx->extradata_size < esize) {
            av_free(avctx->extradata);
            avctx->extradata = av_malloc(esize + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata)
                return AVERROR(ENOMEM);
        }

        avctx->extradata_size = esize;
        gbc = *gb;
        for (i = 0; i < esize; i++) {
          avctx->extradata[i] = get_bits(&gbc, 8);
        }
        memset(avctx->extradata+esize, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    }
    skip_bits_long(gb, asclen);

    return 0;
}

static int read_stream_mux_config(struct LATMContext *latmctx,
                                  GetBitContext *gb)
{
    int ret, audio_mux_version = get_bits(gb, 1);

    latmctx->audio_mux_version_A = 0;
    if (audio_mux_version)
        latmctx->audio_mux_version_A = get_bits(gb, 1);

    if (!latmctx->audio_mux_version_A) {

        if (audio_mux_version)
            latm_get_value(gb);                 // taraFullness

        skip_bits(gb, 1);                       // allStreamSameTimeFraming
        skip_bits(gb, 6);                       // numSubFrames
        // numPrograms
        if (get_bits(gb, 4)) {                  // numPrograms
            avpriv_request_sample(latmctx->aac_ctx.avctx, "Multiple programs");
            return AVERROR_PATCHWELCOME;
        }

        // for each program (which there is only one in DVB)

        // for each layer (which there is only one in DVB)
        if (get_bits(gb, 3)) {                   // numLayer
            avpriv_request_sample(latmctx->aac_ctx.avctx, "Multiple layers");
            return AVERROR_PATCHWELCOME;
        }

        // for all but first stream: use_same_config = get_bits(gb, 1);
        if (!audio_mux_version) {
            if ((ret = latm_decode_audio_specific_config(latmctx, gb, 0)) < 0)
                return ret;
        } else {
            int ascLen = latm_get_value(gb);
            if ((ret = latm_decode_audio_specific_config(latmctx, gb, ascLen)) < 0)
                return ret;
        }

        latmctx->frame_length_type = get_bits(gb, 3);
        switch (latmctx->frame_length_type) {
        case 0:
            skip_bits(gb, 8);       // latmBufferFullness
            break;
        case 1:
            latmctx->frame_length = get_bits(gb, 9);
            break;
        case 3:
        case 4:
        case 5:
            skip_bits(gb, 6);       // CELP frame length table index
            break;
        case 6:
        case 7:
            skip_bits(gb, 1);       // HVXC frame length table index
            break;
        }

        if (get_bits(gb, 1)) {                  // other data
            if (audio_mux_version) {
                latm_get_value(gb);             // other_data_bits
            } else {
                int esc;
                do {
                    esc = get_bits(gb, 1);
                    skip_bits(gb, 8);
                } while (esc);
            }
        }

        if (get_bits(gb, 1))                     // crc present
            skip_bits(gb, 8);                    // config_crc
    }

    return 0;
}

static int read_payload_length_info(struct LATMContext *ctx, GetBitContext *gb)
{
    uint8_t tmp;

    if (ctx->frame_length_type == 0) {
        int mux_slot_length = 0;
        do {
            if (get_bits_left(gb) < 8)
                return AVERROR_INVALIDDATA;
            tmp = get_bits(gb, 8);
            mux_slot_length += tmp;
        } while (tmp == 255);
        return mux_slot_length;
    } else if (ctx->frame_length_type == 1) {
        return ctx->frame_length;
    } else if (ctx->frame_length_type == 3 ||
               ctx->frame_length_type == 5 ||
               ctx->frame_length_type == 7) {
        skip_bits(gb, 2);          // mux_slot_length_coded
    }
    return 0;
}

static int read_audio_mux_element(struct LATMContext *latmctx,
                                  GetBitContext *gb)
{
    int err;
    uint8_t use_same_mux = get_bits(gb, 1);
    if (!use_same_mux) {
        if ((err = read_stream_mux_config(latmctx, gb)) < 0)
            return err;
    } else if (!latmctx->aac_ctx.avctx->extradata) {
        av_log(latmctx->aac_ctx.avctx, AV_LOG_DEBUG,
               "no decoder config found\n");
        return AVERROR(EAGAIN);
    }
    if (latmctx->audio_mux_version_A == 0) {
        int mux_slot_length_bytes = read_payload_length_info(latmctx, gb);
        if (mux_slot_length_bytes < 0 || mux_slot_length_bytes * 8LL > get_bits_left(gb)) {
            av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR, "incomplete frame\n");
            return AVERROR_INVALIDDATA;
        } else if (mux_slot_length_bytes * 8 + 256 < get_bits_left(gb)) {
            av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR,
                   "frame length mismatch %d << %d\n",
                   mux_slot_length_bytes * 8, get_bits_left(gb));
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}


static int latm_decode_frame(AVCodecContext *avctx, void *out,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    struct LATMContext *latmctx = avctx->priv_data;
    int                 muxlength, err;
    GetBitContext       gb;

    if ((err = init_get_bits8(&gb, avpkt->data, avpkt->size)) < 0)
        return err;

    // check for LOAS sync word
    if (get_bits(&gb, 11) != LOAS_SYNC_WORD)
        return AVERROR_INVALIDDATA;

    muxlength = get_bits(&gb, 13) + 3;
    // not enough data, the parser should have sorted this out
    if (muxlength > avpkt->size)
        return AVERROR_INVALIDDATA;

    if ((err = read_audio_mux_element(latmctx, &gb)) < 0)
        return err;

    if (!latmctx->initialized) {
        if (!avctx->extradata) {
            *got_frame_ptr = 0;
            return avpkt->size;
        } else {
            push_output_configuration(&latmctx->aac_ctx);
            if ((err = decode_audio_specific_config(
                    &latmctx->aac_ctx, avctx, &latmctx->aac_ctx.oc[1].m4ac,
                    avctx->extradata, avctx->extradata_size*8LL, 1)) < 0) {
                pop_output_configuration(&latmctx->aac_ctx);
                return err;
            }
            latmctx->initialized = 1;
        }
    }

    if (show_bits(&gb, 12) == 0xfff) {
        av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR,
               "ADTS header detected, probably as result of configuration "
               "misparsing\n");
        return AVERROR_INVALIDDATA;
    }

    switch (latmctx->aac_ctx.oc[1].m4ac.object_type) {
    case AOT_ER_AAC_LC:
    case AOT_ER_AAC_LTP:
    case AOT_ER_AAC_LD:
    case AOT_ER_AAC_ELD:
        err = aac_decode_er_frame(avctx, out, got_frame_ptr, &gb);
        break;
    default:
        err = aac_decode_frame_int(avctx, out, got_frame_ptr, &gb, avpkt);
    }
    if (err < 0)
        return err;

    return muxlength;
}

static av_cold int latm_decode_init(AVCodecContext *avctx)
{
    struct LATMContext *latmctx = avctx->priv_data;
    int ret = aac_decode_init(avctx);

    if (avctx->extradata_size > 0)
        latmctx->initialized = !ret;

    return ret;
}

AVCodec ff_aac_decoder = {
    .name            = "aac",
    .long_name       = NULL_IF_CONFIG_SMALL("AAC (Advanced Audio Coding)"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AAC,
    .priv_data_size  = sizeof(AACContext),
    .init            = aac_decode_init,
    .close           = aac_decode_close,
    .decode          = aac_decode_frame,
    .sample_fmts     = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE
    },
    .capabilities    = AV_CODEC_CAP_CHANNEL_CONF | AV_CODEC_CAP_DR1,
    .caps_internal   = FF_CODEC_CAP_INIT_THREADSAFE,
    .channel_layouts = aac_channel_layout,
    .flush = flush,
    .priv_class      = &aac_decoder_class,
    .profiles        = NULL_IF_CONFIG_SMALL(ff_aac_profiles),
};

/*
    Note: This decoder filter is intended to decode LATM streams transferred
    in MPEG transport streams which only contain one program.
    To do a more complex LATM demuxing a separate LATM demuxer should be used.
*/
AVCodec ff_aac_latm_decoder = {
    .name            = "aac_latm",
    .long_name       = NULL_IF_CONFIG_SMALL("AAC LATM (Advanced Audio Coding LATM syntax)"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AAC_LATM,
    .priv_data_size  = sizeof(struct LATMContext),
    .init            = latm_decode_init,
    .close           = aac_decode_close,
    .decode          = latm_decode_frame,
    .sample_fmts     = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE
    },
    .capabilities    = AV_CODEC_CAP_CHANNEL_CONF | AV_CODEC_CAP_DR1,
    .caps_internal   = FF_CODEC_CAP_INIT_THREADSAFE,
    .channel_layouts = aac_channel_layout,
    .flush = flush,
    .profiles        = NULL_IF_CONFIG_SMALL(ff_aac_profiles),
};
