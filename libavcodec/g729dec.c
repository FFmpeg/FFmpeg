/*
 * G.729 decoder
 * Copyright (c) 2008 Vladimir Voroshilov
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
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "avcodec.h"
#include "libavutil/avutil.h"
#include "get_bits.h"

#include "g729.h"
#include "lsp.h"
#include "celp_math.h"
#include "acelp_filters.h"
#include "acelp_pitch_delay.h"
#include "acelp_vectors.h"
#include "g729data.h"

/**
 * minimum quantized LSF value (3.2.4)
 * 0.005 in Q13
 */
#define LSFQ_MIN                   40

/**
 * maximum quantized LSF value (3.2.4)
 * 3.135 in Q13
 */
#define LSFQ_MAX                   25681

/**
 * minimum LSF distance (3.2.4)
 * 0.0391 in Q13
 */
#define LSFQ_DIFF_MIN              321

/**
 * minimum gain pitch value (3.8, Equation 47)
 * 0.2 in (1.14)
 */
#define SHARP_MIN                  3277

/**
 * maximum gain pitch value (3.8, Equation 47)
 * (EE) This does not comply with the specification.
 * Specification says about 0.8, which should be
 * 13107 in (1.14), but reference C code uses
 * 13017 (equals to 0.7945) instead of it.
 */
#define SHARP_MAX                  13017

typedef struct {
    uint8_t ac_index_bits[2];   ///< adaptive codebook index for second subframe (size in bits)
    uint8_t parity_bit;         ///< parity bit for pitch delay
    uint8_t gc_1st_index_bits;  ///< gain codebook (first stage) index (size in bits)
    uint8_t gc_2nd_index_bits;  ///< gain codebook (second stage) index (size in bits)
    uint8_t fc_signs_bits;      ///< number of pulses in fixed-codebook vector
    uint8_t fc_indexes_bits;    ///< size (in bits) of fixed-codebook index entry
} G729FormatDescription;

static const G729FormatDescription format_g729_8k = {
    .ac_index_bits     = {8,5},
    .parity_bit        = 1,
    .gc_1st_index_bits = GC_1ST_IDX_BITS_8K,
    .gc_2nd_index_bits = GC_2ND_IDX_BITS_8K,
    .fc_signs_bits     = 4,
    .fc_indexes_bits   = 13,
};

static const G729FormatDescription format_g729d_6k4 = {
    .ac_index_bits     = {8,4},
    .parity_bit        = 0,
    .gc_1st_index_bits = GC_1ST_IDX_BITS_6K4,
    .gc_2nd_index_bits = GC_2ND_IDX_BITS_6K4,
    .fc_signs_bits     = 2,
    .fc_indexes_bits   = 9,
};

/**
 * \brief pseudo random number generator
 */
static inline uint16_t g729_prng(uint16_t value)
{
    return 31821 * value + 13849;
}

/**
 * Get parity bit of bit 2..7
 */
static inline int get_parity(uint8_t value)
{
   return (0x6996966996696996ULL >> (value >> 2)) & 1;
}

static av_cold int decoder_init(AVCodecContext * avctx)
{
    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono sound is supported (requested channels: %d).\n", avctx->channels);
        return AVERROR_NOFMT;
    }

    /* Both 8kbit/s and 6.4kbit/s modes uses two subframes per frame. */
    avctx->frame_size = SUBFRAME_SIZE << 1;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int16_t *out_frame = data;
    GetBitContext gb;
    G729FormatDescription format;
    int frame_erasure = 0;    ///< frame erasure detected during decoding
    int bad_pitch = 0;        ///< parity check failed
    int i;
    uint8_t ma_predictor;     ///< switched MA predictor of LSP quantizer
    uint8_t quantizer_1st;    ///< first stage vector of quantizer
    uint8_t quantizer_2nd_lo; ///< second stage lower vector of quantizer (size in bits)
    uint8_t quantizer_2nd_hi; ///< second stage higher vector of quantizer (size in bits)

    if (*data_size < SUBFRAME_SIZE << 2) {
        av_log(avctx, AV_LOG_ERROR, "Error processing packet: output buffer too small\n");
        return AVERROR(EIO);
    }

    if (buf_size == 10) {
        format = format_g729_8k;
        av_log(avctx, AV_LOG_DEBUG, "Packet type: %s\n", "G.729 @ 8kbit/s");
    } else if (buf_size == 8) {
        format = format_g729d_6k4;
        av_log(avctx, AV_LOG_DEBUG, "Packet type: %s\n", "G.729D @ 6.4kbit/s");
    } else {
        av_log(avctx, AV_LOG_ERROR, "Packet size %d is unknown.\n", buf_size);
        return (AVERROR_NOFMT);
    }

    for (i=0; i < buf_size; i++)
        frame_erasure |= buf[i];
    frame_erasure = !frame_erasure;

    init_get_bits(&gb, buf, buf_size);

    ma_predictor     = get_bits(&gb, 1);
    quantizer_1st    = get_bits(&gb, VQ_1ST_BITS);
    quantizer_2nd_lo = get_bits(&gb, VQ_2ND_BITS);
    quantizer_2nd_hi = get_bits(&gb, VQ_2ND_BITS);

    for (i = 0; i < 2; i++) {
        uint8_t ac_index;      ///< adaptive codebook index
        uint8_t pulses_signs;  ///< fixed-codebook vector pulse signs
        int fc_indexes;        ///< fixed-codebook indexes
        uint8_t gc_1st_index;  ///< gain codebook (first stage) index
        uint8_t gc_2nd_index;  ///< gain codebook (second stage) index

        ac_index      = get_bits(&gb, format.ac_index_bits[i]);
        if(!i && format.parity_bit)
            bad_pitch = get_parity(ac_index) == get_bits1(&gb);
        fc_indexes    = get_bits(&gb, format.fc_indexes_bits);
        pulses_signs  = get_bits(&gb, format.fc_signs_bits);
        gc_1st_index  = get_bits(&gb, format.gc_1st_index_bits);
        gc_2nd_index  = get_bits(&gb, format.gc_2nd_index_bits);

        ff_acelp_weighted_vector_sum(fc + pitch_delay_int[i],
                                     fc + pitch_delay_int[i],
                                     fc, 1 << 14,
                                     av_clip(ctx->gain_pitch, SHARP_MIN, SHARP_MAX),
                                     0, 14,
                                     SUBFRAME_SIZE - pitch_delay_int[i]);

        if (frame_erasure) {
            ctx->gain_pitch = (29491 * ctx->gain_pitch) >> 15; // 0.90 (0.15)
            ctx->gain_code  = ( 2007 * ctx->gain_code ) >> 11; // 0.98 (0.11)

            gain_corr_factor = 0;
        } else {
            ctx->gain_pitch  = cb_gain_1st_8k[gc_1st_index][0] +
                               cb_gain_2nd_8k[gc_2nd_index][0];
            gain_corr_factor = cb_gain_1st_8k[gc_1st_index][1] +
                               cb_gain_2nd_8k[gc_2nd_index][1];

        ff_acelp_weighted_vector_sum(ctx->exc + i * SUBFRAME_SIZE,
                                     ctx->exc + i * SUBFRAME_SIZE, fc,
                                     (!voicing && frame_erasure) ? 0 : ctx->gain_pitch,
                                     ( voicing && frame_erasure) ? 0 : ctx->gain_code,
                                     1 << 13, 14, SUBFRAME_SIZE);
    }

    *data_size = SUBFRAME_SIZE << 2;
    return buf_size;
}

AVCodec g729_decoder =
{
    "g729",
    CODEC_TYPE_AUDIO,
    CODEC_ID_G729,
    sizeof(G729Context),
    decoder_init,
    NULL,
    NULL,
    decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("G.729"),
};
