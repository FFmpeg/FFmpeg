/*
 * DCA compatible decoder
 * Copyright (C) 2004 Gildas Bazin
 * Copyright (C) 2004 Benjamin Zores
 * Copyright (C) 2006 Benjamin Larsson
 * Copyright (C) 2007 Konstantin Shishkov
 * Copyright (C) 2012 Paul B Mahol
 * Copyright (C) 2014 Niels Möller
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "avcodec.h"
#include "dca.h"
#include "dca_syncwords.h"
#include "dcadata.h"
#include "dcadsp.h"
#include "dcahuff.h"
#include "fft.h"
#include "fmtconvert.h"
#include "get_bits.h"
#include "internal.h"
#include "mathops.h"
#include "profiles.h"
#include "put_bits.h"
#include "synth_filter.h"

#if ARCH_ARM
#   include "arm/dca.h"
#endif

enum DCAMode {
    DCA_MONO = 0,
    DCA_CHANNEL,
    DCA_STEREO,
    DCA_STEREO_SUMDIFF,
    DCA_STEREO_TOTAL,
    DCA_3F,
    DCA_2F1R,
    DCA_3F1R,
    DCA_2F2R,
    DCA_3F2R,
    DCA_4F2R
};

/* -1 are reserved or unknown */
static const int dca_ext_audio_descr_mask[] = {
    DCA_EXT_XCH,
    -1,
    DCA_EXT_X96,
    DCA_EXT_XCH | DCA_EXT_X96,
    -1,
    -1,
    DCA_EXT_XXCH,
    -1,
};

/* Tables for mapping dts channel configurations to libavcodec multichannel api.
 * Some compromises have been made for special configurations. Most configurations
 * are never used so complete accuracy is not needed.
 *
 * L = left, R = right, C = center, S = surround, F = front, R = rear, T = total, OV = overhead.
 * S  -> side, when both rear and back are configured move one of them to the side channel
 * OV -> center back
 * All 2 channel configurations -> AV_CH_LAYOUT_STEREO
 */
static const uint64_t dca_core_channel_layout[] = {
    AV_CH_FRONT_CENTER,                                                     ///< 1, A
    AV_CH_LAYOUT_STEREO,                                                    ///< 2, A + B (dual mono)
    AV_CH_LAYOUT_STEREO,                                                    ///< 2, L + R (stereo)
    AV_CH_LAYOUT_STEREO,                                                    ///< 2, (L + R) + (L - R) (sum-difference)
    AV_CH_LAYOUT_STEREO,                                                    ///< 2, LT + RT (left and right total)
    AV_CH_LAYOUT_STEREO | AV_CH_FRONT_CENTER,                               ///< 3, C + L + R
    AV_CH_LAYOUT_STEREO | AV_CH_BACK_CENTER,                                ///< 3, L + R + S
    AV_CH_LAYOUT_STEREO | AV_CH_FRONT_CENTER | AV_CH_BACK_CENTER,           ///< 4, C + L + R + S
    AV_CH_LAYOUT_STEREO | AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT,               ///< 4, L + R + SL + SR

    AV_CH_LAYOUT_STEREO | AV_CH_FRONT_CENTER | AV_CH_SIDE_LEFT |
    AV_CH_SIDE_RIGHT,                                                       ///< 5, C + L + R + SL + SR

    AV_CH_LAYOUT_STEREO | AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT |
    AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER,               ///< 6, CL + CR + L + R + SL + SR

    AV_CH_LAYOUT_STEREO | AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT |
    AV_CH_FRONT_CENTER  | AV_CH_BACK_CENTER,                                ///< 6, C + L + R + LR + RR + OV

    AV_CH_FRONT_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER |
    AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_BACK_CENTER   |
    AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT,                                     ///< 6, CF + CR + LF + RF + LR + RR

    AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_CENTER   |
    AV_CH_FRONT_RIGHT_OF_CENTER | AV_CH_LAYOUT_STEREO |
    AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT,                                     ///< 7, CL + C + CR + L + R + SL + SR

    AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER |
    AV_CH_LAYOUT_STEREO | AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT |
    AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT,                                     ///< 8, CL + CR + L + R + SL1 + SL2 + SR1 + SR2

    AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_CENTER   |
    AV_CH_FRONT_RIGHT_OF_CENTER | AV_CH_LAYOUT_STEREO |
    AV_CH_SIDE_LEFT | AV_CH_BACK_CENTER | AV_CH_SIDE_RIGHT,                 ///< 8, CL + C + CR + L + R + SL + S + SR
};

#define DCA_DOLBY                  101           /* FIXME */

#define DCA_CHANNEL_BITS             6
#define DCA_CHANNEL_MASK          0x3F

#define DCA_LFE                   0x80

#define HEADER_SIZE                 14

#define DCA_NSYNCAUX        0x9A1105A0

/** Bit allocation */
typedef struct BitAlloc {
    int offset;                 ///< code values offset
    int maxbits[8];             ///< max bits in VLC
    int wrap;                   ///< wrap for get_vlc2()
    VLC vlc[8];                 ///< actual codes
} BitAlloc;

static BitAlloc dca_bitalloc_index;    ///< indexes for samples VLC select
static BitAlloc dca_tmode;             ///< transition mode VLCs
static BitAlloc dca_scalefactor;       ///< scalefactor VLCs
static BitAlloc dca_smpl_bitalloc[11]; ///< samples VLCs

static av_always_inline int get_bitalloc(GetBitContext *gb, BitAlloc *ba,
                                         int idx)
{
    return get_vlc2(gb, ba->vlc[idx].table, ba->vlc[idx].bits, ba->wrap) +
           ba->offset;
}

static av_cold void dca_init_vlcs(void)
{
    static int vlcs_initialized = 0;
    int i, j, c = 14;
    static VLC_TYPE dca_table[23622][2];

    if (vlcs_initialized)
        return;

    dca_bitalloc_index.offset = 1;
    dca_bitalloc_index.wrap   = 2;
    for (i = 0; i < 5; i++) {
        dca_bitalloc_index.vlc[i].table           = &dca_table[ff_dca_vlc_offs[i]];
        dca_bitalloc_index.vlc[i].table_allocated = ff_dca_vlc_offs[i + 1] - ff_dca_vlc_offs[i];
        init_vlc(&dca_bitalloc_index.vlc[i], bitalloc_12_vlc_bits[i], 12,
                 bitalloc_12_bits[i], 1, 1,
                 bitalloc_12_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
    }
    dca_scalefactor.offset = -64;
    dca_scalefactor.wrap   = 2;
    for (i = 0; i < 5; i++) {
        dca_scalefactor.vlc[i].table           = &dca_table[ff_dca_vlc_offs[i + 5]];
        dca_scalefactor.vlc[i].table_allocated = ff_dca_vlc_offs[i + 6] - ff_dca_vlc_offs[i + 5];
        init_vlc(&dca_scalefactor.vlc[i], SCALES_VLC_BITS, 129,
                 scales_bits[i], 1, 1,
                 scales_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
    }
    dca_tmode.offset = 0;
    dca_tmode.wrap   = 1;
    for (i = 0; i < 4; i++) {
        dca_tmode.vlc[i].table           = &dca_table[ff_dca_vlc_offs[i + 10]];
        dca_tmode.vlc[i].table_allocated = ff_dca_vlc_offs[i + 11] - ff_dca_vlc_offs[i + 10];
        init_vlc(&dca_tmode.vlc[i], tmode_vlc_bits[i], 4,
                 tmode_bits[i], 1, 1,
                 tmode_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
    }

    for (i = 0; i < 10; i++)
        for (j = 0; j < 7; j++) {
            if (!bitalloc_codes[i][j])
                break;
            dca_smpl_bitalloc[i + 1].offset                 = bitalloc_offsets[i];
            dca_smpl_bitalloc[i + 1].wrap                   = 1 + (j > 4);
            dca_smpl_bitalloc[i + 1].vlc[j].table           = &dca_table[ff_dca_vlc_offs[c]];
            dca_smpl_bitalloc[i + 1].vlc[j].table_allocated = ff_dca_vlc_offs[c + 1] - ff_dca_vlc_offs[c];

            init_vlc(&dca_smpl_bitalloc[i + 1].vlc[j], bitalloc_maxbits[i][j],
                     bitalloc_sizes[i],
                     bitalloc_bits[i][j], 1, 1,
                     bitalloc_codes[i][j], 2, 2, INIT_VLC_USE_NEW_STATIC);
            c++;
        }
    vlcs_initialized = 1;
}

static inline void get_array(GetBitContext *gb, int *dst, int len, int bits)
{
    while (len--)
        *dst++ = get_bits(gb, bits);
}

static int dca_parse_audio_coding_header(DCAContext *s, int base_channel)
{
    int i, j;
    static const uint8_t adj_table[4] = { 16, 18, 20, 23 };
    static const int bitlen[11] = { 0, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3 };
    static const int thr[11]    = { 0, 1, 3, 3, 3, 3, 7, 7, 7, 7, 7 };

    s->audio_header.total_channels = get_bits(&s->gb, 3) + 1 + base_channel;
    s->audio_header.prim_channels  = s->audio_header.total_channels;

    if (s->audio_header.prim_channels > DCA_PRIM_CHANNELS_MAX)
        s->audio_header.prim_channels = DCA_PRIM_CHANNELS_MAX;

    for (i = base_channel; i < s->audio_header.prim_channels; i++) {
        s->audio_header.subband_activity[i] = get_bits(&s->gb, 5) + 2;
        if (s->audio_header.subband_activity[i] > DCA_SUBBANDS)
            s->audio_header.subband_activity[i] = DCA_SUBBANDS;
    }
    for (i = base_channel; i < s->audio_header.prim_channels; i++) {
        s->audio_header.vq_start_subband[i] = get_bits(&s->gb, 5) + 1;
        if (s->audio_header.vq_start_subband[i] > DCA_SUBBANDS)
            s->audio_header.vq_start_subband[i] = DCA_SUBBANDS;
    }
    get_array(&s->gb, s->audio_header.joint_intensity + base_channel,
              s->audio_header.prim_channels - base_channel, 3);
    get_array(&s->gb, s->audio_header.transient_huffman + base_channel,
              s->audio_header.prim_channels - base_channel, 2);
    get_array(&s->gb, s->audio_header.scalefactor_huffman + base_channel,
              s->audio_header.prim_channels - base_channel, 3);
    get_array(&s->gb, s->audio_header.bitalloc_huffman + base_channel,
              s->audio_header.prim_channels - base_channel, 3);

    /* Get codebooks quantization indexes */
    if (!base_channel)
        memset(s->audio_header.quant_index_huffman, 0, sizeof(s->audio_header.quant_index_huffman));
    for (j = 1; j < 11; j++)
        for (i = base_channel; i < s->audio_header.prim_channels; i++)
            s->audio_header.quant_index_huffman[i][j] = get_bits(&s->gb, bitlen[j]);

    /* Get scale factor adjustment */
    for (j = 0; j < 11; j++)
        for (i = base_channel; i < s->audio_header.prim_channels; i++)
            s->audio_header.scalefactor_adj[i][j] = 16;

    for (j = 1; j < 11; j++)
        for (i = base_channel; i < s->audio_header.prim_channels; i++)
            if (s->audio_header.quant_index_huffman[i][j] < thr[j])
                s->audio_header.scalefactor_adj[i][j] = adj_table[get_bits(&s->gb, 2)];

    if (s->crc_present) {
        /* Audio header CRC check */
        get_bits(&s->gb, 16);
    }

    s->current_subframe    = 0;
    s->current_subsubframe = 0;

    return 0;
}

static int dca_parse_frame_header(DCAContext *s)
{
    init_get_bits(&s->gb, s->dca_buffer, s->dca_buffer_size * 8);

    /* Sync code */
    skip_bits_long(&s->gb, 32);

    /* Frame header */
    s->frame_type        = get_bits(&s->gb, 1);
    s->samples_deficit   = get_bits(&s->gb, 5) + 1;
    s->crc_present       = get_bits(&s->gb, 1);
    s->sample_blocks     = get_bits(&s->gb, 7) + 1;
    s->frame_size        = get_bits(&s->gb, 14) + 1;
    if (s->frame_size < 95)
        return AVERROR_INVALIDDATA;
    s->amode             = get_bits(&s->gb, 6);
    s->sample_rate       = avpriv_dca_sample_rates[get_bits(&s->gb, 4)];
    if (!s->sample_rate)
        return AVERROR_INVALIDDATA;
    s->bit_rate_index    = get_bits(&s->gb, 5);
    s->bit_rate          = ff_dca_bit_rates[s->bit_rate_index];
    if (!s->bit_rate)
        return AVERROR_INVALIDDATA;

    skip_bits1(&s->gb); // always 0 (reserved, cf. ETSI TS 102 114 V1.4.1)
    s->dynrange          = get_bits(&s->gb, 1);
    s->timestamp         = get_bits(&s->gb, 1);
    s->aux_data          = get_bits(&s->gb, 1);
    s->hdcd              = get_bits(&s->gb, 1);
    s->ext_descr         = get_bits(&s->gb, 3);
    s->ext_coding        = get_bits(&s->gb, 1);
    s->aspf              = get_bits(&s->gb, 1);
    s->lfe               = get_bits(&s->gb, 2);
    s->predictor_history = get_bits(&s->gb, 1);

    if (s->lfe > 2) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LFE value: %d\n", s->lfe);
        return AVERROR_INVALIDDATA;
    }

    /* TODO: check CRC */
    if (s->crc_present)
        s->header_crc    = get_bits(&s->gb, 16);

    s->multirate_inter   = get_bits(&s->gb, 1);
    s->version           = get_bits(&s->gb, 4);
    s->copy_history      = get_bits(&s->gb, 2);
    s->source_pcm_res    = get_bits(&s->gb, 3);
    s->front_sum         = get_bits(&s->gb, 1);
    s->surround_sum      = get_bits(&s->gb, 1);
    s->dialog_norm       = get_bits(&s->gb, 4);

    /* FIXME: channels mixing levels */
    s->output = s->amode;
    if (s->lfe)
        s->output |= DCA_LFE;

    /* Primary audio coding header */
    s->audio_header.subframes = get_bits(&s->gb, 4) + 1;

    return dca_parse_audio_coding_header(s, 0);
}

static inline int get_scale(GetBitContext *gb, int level, int value, int log2range)
{
    if (level < 5) {
        /* huffman encoded */
        value += get_bitalloc(gb, &dca_scalefactor, level);
        value  = av_clip(value, 0, (1 << log2range) - 1);
    } else if (level < 8) {
        if (level + 1 > log2range) {
            skip_bits(gb, level + 1 - log2range);
            value = get_bits(gb, log2range);
        } else {
            value = get_bits(gb, level + 1);
        }
    }
    return value;
}

static int dca_subframe_header(DCAContext *s, int base_channel, int block_index)
{
    /* Primary audio coding side information */
    int j, k;

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    if (!base_channel) {
        s->subsubframes[s->current_subframe]    = get_bits(&s->gb, 2) + 1;
        s->partial_samples[s->current_subframe] = get_bits(&s->gb, 3);
    }

    for (j = base_channel; j < s->audio_header.prim_channels; j++) {
        for (k = 0; k < s->audio_header.subband_activity[j]; k++)
            s->dca_chan[j].prediction_mode[k] = get_bits(&s->gb, 1);
    }

    /* Get prediction codebook */
    for (j = base_channel; j < s->audio_header.prim_channels; j++) {
        for (k = 0; k < s->audio_header.subband_activity[j]; k++) {
            if (s->dca_chan[j].prediction_mode[k] > 0) {
                /* (Prediction coefficient VQ address) */
                s->dca_chan[j].prediction_vq[k] = get_bits(&s->gb, 12);
            }
        }
    }

    /* Bit allocation index */
    for (j = base_channel; j < s->audio_header.prim_channels; j++) {
        for (k = 0; k < s->audio_header.vq_start_subband[j]; k++) {
            if (s->audio_header.bitalloc_huffman[j] == 6)
                s->dca_chan[j].bitalloc[k] = get_bits(&s->gb, 5);
            else if (s->audio_header.bitalloc_huffman[j] == 5)
                s->dca_chan[j].bitalloc[k] = get_bits(&s->gb, 4);
            else if (s->audio_header.bitalloc_huffman[j] == 7) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Invalid bit allocation index\n");
                return AVERROR_INVALIDDATA;
            } else {
                s->dca_chan[j].bitalloc[k] =
                    get_bitalloc(&s->gb, &dca_bitalloc_index, s->audio_header.bitalloc_huffman[j]);
            }

            if (s->dca_chan[j].bitalloc[k] > 26) {
                ff_dlog(s->avctx, "bitalloc index [%i][%i] too big (%i)\n",
                        j, k, s->dca_chan[j].bitalloc[k]);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    /* Transition mode */
    for (j = base_channel; j < s->audio_header.prim_channels; j++) {
        for (k = 0; k < s->audio_header.subband_activity[j]; k++) {
            s->dca_chan[j].transition_mode[k] = 0;
            if (s->subsubframes[s->current_subframe] > 1 &&
                k < s->audio_header.vq_start_subband[j] && s->dca_chan[j].bitalloc[k] > 0) {
                s->dca_chan[j].transition_mode[k] =
                    get_bitalloc(&s->gb, &dca_tmode, s->audio_header.transient_huffman[j]);
            }
        }
    }

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    for (j = base_channel; j < s->audio_header.prim_channels; j++) {
        const uint32_t *scale_table;
        int scale_sum, log_size;

        memset(s->dca_chan[j].scale_factor, 0,
               s->audio_header.subband_activity[j] * sizeof(s->dca_chan[j].scale_factor[0][0]) * 2);

        if (s->audio_header.scalefactor_huffman[j] == 6) {
            scale_table = ff_dca_scale_factor_quant7;
            log_size    = 7;
        } else {
            scale_table = ff_dca_scale_factor_quant6;
            log_size    = 6;
        }

        /* When huffman coded, only the difference is encoded */
        scale_sum = 0;

        for (k = 0; k < s->audio_header.subband_activity[j]; k++) {
            if (k >= s->audio_header.vq_start_subband[j] || s->dca_chan[j].bitalloc[k] > 0) {
                scale_sum = get_scale(&s->gb, s->audio_header.scalefactor_huffman[j], scale_sum, log_size);
                s->dca_chan[j].scale_factor[k][0] = scale_table[scale_sum];
            }

            if (k < s->audio_header.vq_start_subband[j] && s->dca_chan[j].transition_mode[k]) {
                /* Get second scale factor */
                scale_sum = get_scale(&s->gb, s->audio_header.scalefactor_huffman[j], scale_sum, log_size);
                s->dca_chan[j].scale_factor[k][1] = scale_table[scale_sum];
            }
        }
    }

    /* Joint subband scale factor codebook select */
    for (j = base_channel; j < s->audio_header.prim_channels; j++) {
        /* Transmitted only if joint subband coding enabled */
        if (s->audio_header.joint_intensity[j] > 0)
            s->dca_chan[j].joint_huff = get_bits(&s->gb, 3);
    }

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    /* Scale factors for joint subband coding */
    for (j = base_channel; j < s->audio_header.prim_channels; j++) {
        int source_channel;

        /* Transmitted only if joint subband coding enabled */
        if (s->audio_header.joint_intensity[j] > 0) {
            int scale = 0;
            source_channel = s->audio_header.joint_intensity[j] - 1;

            /* When huffman coded, only the difference is encoded
             * (is this valid as well for joint scales ???) */

            for (k = s->audio_header.subband_activity[j];
                 k < s->audio_header.subband_activity[source_channel]; k++) {
                scale = get_scale(&s->gb, s->dca_chan[j].joint_huff, 64 /* bias */, 7);
                s->dca_chan[j].joint_scale_factor[k] = scale;    /*joint_scale_table[scale]; */
            }

            if (!(s->debug_flag & 0x02)) {
                av_log(s->avctx, AV_LOG_DEBUG,
                       "Joint stereo coding not supported\n");
                s->debug_flag |= 0x02;
            }
        }
    }

    /* Dynamic range coefficient */
    if (!base_channel && s->dynrange)
        s->dynrange_coef = get_bits(&s->gb, 8);

    /* Side information CRC check word */
    if (s->crc_present) {
        get_bits(&s->gb, 16);
    }

    /*
     * Primary audio data arrays
     */

    /* VQ encoded high frequency subbands */
    for (j = base_channel; j < s->audio_header.prim_channels; j++)
        for (k = s->audio_header.vq_start_subband[j]; k < s->audio_header.subband_activity[j]; k++)
            /* 1 vector -> 32 samples */
            s->dca_chan[j].high_freq_vq[k] = get_bits(&s->gb, 10);

    /* Low frequency effect data */
    if (!base_channel && s->lfe) {
        /* LFE samples */
        int lfe_samples    = 2 * s->lfe * (4 + block_index);
        int lfe_end_sample = 2 * s->lfe * (4 + block_index + s->subsubframes[s->current_subframe]);
        float lfe_scale;

        for (j = lfe_samples; j < lfe_end_sample; j++) {
            /* Signed 8 bits int */
            s->lfe_data[j] = get_sbits(&s->gb, 8);
        }

        /* Scale factor index */
        skip_bits(&s->gb, 1);
        s->lfe_scale_factor = ff_dca_scale_factor_quant7[get_bits(&s->gb, 7)];

        /* Quantization step size * scale factor */
        lfe_scale = 0.035 * s->lfe_scale_factor;

        for (j = lfe_samples; j < lfe_end_sample; j++)
            s->lfe_data[j] *= lfe_scale;
    }

    return 0;
}

static void qmf_32_subbands(DCAContext *s, int chans,
                            float samples_in[DCA_SUBBANDS][SAMPLES_PER_SUBBAND], float *samples_out,
                            float scale)
{
    const float *prCoeff;

    int sb_act = s->audio_header.subband_activity[chans];

    scale *= sqrt(1 / 8.0);

    /* Select filter */
    if (!s->multirate_inter)    /* Non-perfect reconstruction */
        prCoeff = ff_dca_fir_32bands_nonperfect;
    else                        /* Perfect reconstruction */
        prCoeff = ff_dca_fir_32bands_perfect;

    s->dcadsp.qmf_32_subbands(samples_in, sb_act, &s->synth, &s->imdct,
                              s->dca_chan[chans].subband_fir_hist,
                              &s->dca_chan[chans].hist_index,
                              s->dca_chan[chans].subband_fir_noidea, prCoeff,
                              samples_out, s->raXin, scale);
}

static QMF64_table *qmf64_precompute(void)
{
    unsigned i, j;
    QMF64_table *table = av_malloc(sizeof(*table));
    if (!table)
        return NULL;

    for (i = 0; i < 32; i++)
        for (j = 0; j < 32; j++)
            table->dct4_coeff[i][j] = cos((2 * i + 1) * (2 * j + 1) * M_PI / 128);
    for (i = 0; i < 32; i++)
        for (j = 0; j < 32; j++)
            table->dct2_coeff[i][j] = cos((2 * i + 1) *      j      * M_PI /  64);

    /* FIXME: Is the factor 0.125 = 1/8 right? */
    for (i = 0; i < 32; i++)
        table->rcos[i] =  0.125 / cos((2 * i + 1) * M_PI / 256);
    for (i = 0; i < 32; i++)
        table->rsin[i] = -0.125 / sin((2 * i + 1) * M_PI / 256);

    return table;
}

/* FIXME: Totally unoptimized. Based on the reference code and
 * http://multimedia.cx/mirror/dca-transform.pdf, with guessed tweaks
 * for doubling the size. */
static void qmf_64_subbands(DCAContext *s, int chans,
                            float samples_in[DCA_SUBBANDS_X96K][SAMPLES_PER_SUBBAND],
                            float *samples_out, float scale)
{
    float raXin[64];
    float A[32], B[32];
    float *raX = s->dca_chan[chans].subband_fir_hist;
    float *raZ = s->dca_chan[chans].subband_fir_noidea;
    unsigned i, j, k, subindex;

    for (i = s->audio_header.subband_activity[chans]; i < DCA_SUBBANDS_X96K; i++)
        raXin[i] = 0.0;
    for (subindex = 0; subindex < SAMPLES_PER_SUBBAND; subindex++) {
        for (i = 0; i < s->audio_header.subband_activity[chans]; i++)
            raXin[i] = samples_in[i][subindex];

        for (k = 0; k < 32; k++) {
            A[k] = 0.0;
            for (i = 0; i < 32; i++)
                A[k] += (raXin[2 * i] + raXin[2 * i + 1]) * s->qmf64_table->dct4_coeff[k][i];
        }
        for (k = 0; k < 32; k++) {
            B[k] = raXin[0] * s->qmf64_table->dct2_coeff[k][0];
            for (i = 1; i < 32; i++)
                B[k] += (raXin[2 * i] + raXin[2 * i - 1]) * s->qmf64_table->dct2_coeff[k][i];
        }
        for (k = 0; k < 32; k++) {
            raX[k]      = s->qmf64_table->rcos[k] * (A[k] + B[k]);
            raX[63 - k] = s->qmf64_table->rsin[k] * (A[k] - B[k]);
        }

        for (i = 0; i < DCA_SUBBANDS_X96K; i++) {
            float out = raZ[i];
            for (j = 0; j < 1024; j += 128)
                out += ff_dca_fir_64bands[j + i] * (raX[j + i] - raX[j + 63 - i]);
            *samples_out++ = out * scale;
        }

        for (i = 0; i < DCA_SUBBANDS_X96K; i++) {
            float hist = 0.0;
            for (j = 0; j < 1024; j += 128)
                hist += ff_dca_fir_64bands[64 + j + i] * (-raX[i + j] - raX[j + 63 - i]);

            raZ[i] = hist;
        }

        /* FIXME: Make buffer circular, to avoid this move. */
        memmove(raX + 64, raX, (1024 - 64) * sizeof(*raX));
    }
}

static void lfe_interpolation_fir(DCAContext *s, const float *samples_in,
                                  float *samples_out)
{
    /* samples_in: An array holding decimated samples.
     *   Samples in current subframe starts from samples_in[0],
     *   while samples_in[-1], samples_in[-2], ..., stores samples
     *   from last subframe as history.
     *
     * samples_out: An array holding interpolated samples
     */

    int idx;
    const float *prCoeff;
    int deciindex;

    /* Select decimation filter */
    if (s->lfe == 1) {
        idx     = 1;
        prCoeff = ff_dca_lfe_fir_128;
    } else {
        idx = 0;
        if (s->exss_ext_mask & DCA_EXT_EXSS_XLL)
            prCoeff = ff_dca_lfe_xll_fir_64;
        else
            prCoeff = ff_dca_lfe_fir_64;
    }
    /* Interpolation */
    for (deciindex = 0; deciindex < 2 * s->lfe; deciindex++) {
        s->dcadsp.lfe_fir[idx](samples_out, samples_in, prCoeff);
        samples_in++;
        samples_out += 2 * 32 * (1 + idx);
    }
}

/* downmixing routines */
#define MIX_REAR1(samples, s1, rs, coef)            \
    samples[0][i] += samples[s1][i] * coef[rs][0];  \
    samples[1][i] += samples[s1][i] * coef[rs][1];

#define MIX_REAR2(samples, s1, s2, rs, coef)                                          \
    samples[0][i] += samples[s1][i] * coef[rs][0] + samples[s2][i] * coef[rs + 1][0]; \
    samples[1][i] += samples[s1][i] * coef[rs][1] + samples[s2][i] * coef[rs + 1][1];

#define MIX_FRONT3(samples, coef)                                      \
    t = samples[c][i];                                                 \
    u = samples[l][i];                                                 \
    v = samples[r][i];                                                 \
    samples[0][i] = t * coef[0][0] + u * coef[1][0] + v * coef[2][0];  \
    samples[1][i] = t * coef[0][1] + u * coef[1][1] + v * coef[2][1];

#define DOWNMIX_TO_STEREO(op1, op2)             \
    for (i = 0; i < 256; i++) {                 \
        op1                                     \
        op2                                     \
    }

static void dca_downmix(float **samples, int srcfmt, int lfe_present,
                        float coef[DCA_PRIM_CHANNELS_MAX + 1][2],
                        const int8_t *channel_mapping)
{
    int c, l, r, sl, sr, s;
    int i;
    float t, u, v;

    switch (srcfmt) {
    case DCA_MONO:
    case DCA_4F2R:
        av_log(NULL, 0, "Not implemented!\n");
        break;
    case DCA_CHANNEL:
    case DCA_STEREO:
    case DCA_STEREO_TOTAL:
    case DCA_STEREO_SUMDIFF:
        break;
    case DCA_3F:
        c = channel_mapping[0];
        l = channel_mapping[1];
        r = channel_mapping[2];
        DOWNMIX_TO_STEREO(MIX_FRONT3(samples, coef), );
        break;
    case DCA_2F1R:
        s = channel_mapping[2];
        DOWNMIX_TO_STEREO(MIX_REAR1(samples, s, 2, coef), );
        break;
    case DCA_3F1R:
        c = channel_mapping[0];
        l = channel_mapping[1];
        r = channel_mapping[2];
        s = channel_mapping[3];
        DOWNMIX_TO_STEREO(MIX_FRONT3(samples, coef),
                          MIX_REAR1(samples, s, 3, coef));
        break;
    case DCA_2F2R:
        sl = channel_mapping[2];
        sr = channel_mapping[3];
        DOWNMIX_TO_STEREO(MIX_REAR2(samples, sl, sr, 2, coef), );
        break;
    case DCA_3F2R:
        c  = channel_mapping[0];
        l  = channel_mapping[1];
        r  = channel_mapping[2];
        sl = channel_mapping[3];
        sr = channel_mapping[4];
        DOWNMIX_TO_STEREO(MIX_FRONT3(samples, coef),
                          MIX_REAR2(samples, sl, sr, 3, coef));
        break;
    }
    if (lfe_present) {
        int lf_buf = ff_dca_lfe_index[srcfmt];
        int lf_idx =  ff_dca_channels[srcfmt];
        for (i = 0; i < 256; i++) {
            samples[0][i] += samples[lf_buf][i] * coef[lf_idx][0];
            samples[1][i] += samples[lf_buf][i] * coef[lf_idx][1];
        }
    }
}

#ifndef decode_blockcodes
/* Very compact version of the block code decoder that does not use table
 * look-up but is slightly slower */
static int decode_blockcode(int code, int levels, int32_t *values)
{
    int i;
    int offset = (levels - 1) >> 1;

    for (i = 0; i < 4; i++) {
        int div = FASTDIV(code, levels);
        values[i] = code - offset - div * levels;
        code      = div;
    }

    return code;
}

static int decode_blockcodes(int code1, int code2, int levels, int32_t *values)
{
    return decode_blockcode(code1, levels, values) |
           decode_blockcode(code2, levels, values + 4);
}
#endif

static const uint8_t abits_sizes[7]  = { 7, 10, 12, 13, 15, 17, 19 };
static const uint8_t abits_levels[7] = { 3,  5,  7,  9, 13, 17, 25 };

static int dca_subsubframe(DCAContext *s, int base_channel, int block_index)
{
    int k, l;
    int subsubframe = s->current_subsubframe;
    const uint32_t *quant_step_table;

    /*
     * Audio data
     */

    /* Select quantization step size table */
    if (s->bit_rate_index == 0x1f)
        quant_step_table = ff_dca_lossless_quant;
    else
        quant_step_table = ff_dca_lossy_quant;

    for (k = base_channel; k < s->audio_header.prim_channels; k++) {
        int32_t (*subband_samples)[8] = s->dca_chan[k].subband_samples[block_index];

        if (get_bits_left(&s->gb) < 0)
            return AVERROR_INVALIDDATA;

        for (l = 0; l < s->audio_header.vq_start_subband[k]; l++) {
            int m;

            /* Select the mid-tread linear quantizer */
            int abits = s->dca_chan[k].bitalloc[l];

            uint32_t quant_step_size = quant_step_table[abits];

            /*
             * Extract bits from the bit stream
             */
            if (!abits)
                memset(subband_samples[l], 0, SAMPLES_PER_SUBBAND *
                       sizeof(subband_samples[l][0]));
            else {
                uint32_t rscale;
                /* Deal with transients */
                int sfi = s->dca_chan[k].transition_mode[l] &&
                    subsubframe >= s->dca_chan[k].transition_mode[l];
                /* Determine quantization index code book and its type.
                   Select quantization index code book */
                int sel = s->audio_header.quant_index_huffman[k][abits];

                rscale = (s->dca_chan[k].scale_factor[l][sfi] *
                          s->audio_header.scalefactor_adj[k][sel] + 8) >> 4;

                if (abits >= 11 || !dca_smpl_bitalloc[abits].vlc[sel].table) {
                    if (abits <= 7) {
                        /* Block code */
                        int block_code1, block_code2, size, levels, err;

                        size   = abits_sizes[abits - 1];
                        levels = abits_levels[abits - 1];

                        block_code1 = get_bits(&s->gb, size);
                        block_code2 = get_bits(&s->gb, size);
                        err         = decode_blockcodes(block_code1, block_code2,
                                                        levels, subband_samples[l]);
                        if (err) {
                            av_log(s->avctx, AV_LOG_ERROR,
                                   "ERROR: block code look-up failed\n");
                            return AVERROR_INVALIDDATA;
                        }
                    } else {
                        /* no coding */
                        for (m = 0; m < SAMPLES_PER_SUBBAND; m++)
                            subband_samples[l][m] = get_sbits(&s->gb, abits - 3);
                    }
                } else {
                    /* Huffman coded */
                    for (m = 0; m < SAMPLES_PER_SUBBAND; m++)
                        subband_samples[l][m] = get_bitalloc(&s->gb,
                                                             &dca_smpl_bitalloc[abits], sel);
                }
                s->dcadsp.dequantize(subband_samples[l], quant_step_size, rscale);
            }
        }

        for (l = 0; l < s->audio_header.vq_start_subband[k]; l++) {
            int m;
            /*
             * Inverse ADPCM if in prediction mode
             */
            if (s->dca_chan[k].prediction_mode[l]) {
                int n;
                if (s->predictor_history)
                    subband_samples[l][0] += (ff_dca_adpcm_vb[s->dca_chan[k].prediction_vq[l]][0] *
                                              (int64_t)s->dca_chan[k].subband_samples_hist[l][3] +
                                              ff_dca_adpcm_vb[s->dca_chan[k].prediction_vq[l]][1] *
                                              (int64_t)s->dca_chan[k].subband_samples_hist[l][2] +
                                              ff_dca_adpcm_vb[s->dca_chan[k].prediction_vq[l]][2] *
                                              (int64_t)s->dca_chan[k].subband_samples_hist[l][1] +
                                              ff_dca_adpcm_vb[s->dca_chan[k].prediction_vq[l]][3] *
                                              (int64_t)s->dca_chan[k].subband_samples_hist[l][0]) +
                                              (1 << 12) >> 13;
                for (m = 1; m < SAMPLES_PER_SUBBAND; m++) {
                    int64_t sum = ff_dca_adpcm_vb[s->dca_chan[k].prediction_vq[l]][0] *
                                  (int64_t)subband_samples[l][m - 1];
                    for (n = 2; n <= 4; n++)
                        if (m >= n)
                            sum += ff_dca_adpcm_vb[s->dca_chan[k].prediction_vq[l]][n - 1] *
                                   (int64_t)subband_samples[l][m - n];
                        else if (s->predictor_history)
                            sum += ff_dca_adpcm_vb[s->dca_chan[k].prediction_vq[l]][n - 1] *
                                   (int64_t)s->dca_chan[k].subband_samples_hist[l][m - n + 4];
                    subband_samples[l][m] += (int32_t)(sum + (1 << 12) >> 13);
                }
            }

        }
        /* Backup predictor history for adpcm */
        for (l = 0; l < DCA_SUBBANDS; l++)
            AV_COPY128(s->dca_chan[k].subband_samples_hist[l], &subband_samples[l][4]);


        /*
         * Decode VQ encoded high frequencies
         */
        if (s->audio_header.subband_activity[k] > s->audio_header.vq_start_subband[k]) {
            if (!s->debug_flag & 0x01) {
                av_log(s->avctx, AV_LOG_DEBUG,
                       "Stream with high frequencies VQ coding\n");
                s->debug_flag |= 0x01;
            }

            s->dcadsp.decode_hf(subband_samples, s->dca_chan[k].high_freq_vq,
                                ff_dca_high_freq_vq,
                                subsubframe * SAMPLES_PER_SUBBAND,
                                s->dca_chan[k].scale_factor,
                                s->audio_header.vq_start_subband[k],
                                s->audio_header.subband_activity[k]);
        }
    }

    /* Check for DSYNC after subsubframe */
    if (s->aspf || subsubframe == s->subsubframes[s->current_subframe] - 1) {
        if (get_bits(&s->gb, 16) != 0xFFFF) {
            av_log(s->avctx, AV_LOG_ERROR, "Didn't get subframe DSYNC\n");
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int dca_filter_channels(DCAContext *s, int block_index, int upsample)
{
    int k;

    if (upsample) {
        LOCAL_ALIGNED(32, float, samples, [DCA_SUBBANDS_X96K], [SAMPLES_PER_SUBBAND]);

        if (!s->qmf64_table) {
            s->qmf64_table = qmf64_precompute();
            if (!s->qmf64_table)
                return AVERROR(ENOMEM);
        }

        /* 64 subbands QMF */
        for (k = 0; k < s->audio_header.prim_channels; k++) {
            int32_t (*subband_samples)[SAMPLES_PER_SUBBAND] =
                     s->dca_chan[k].subband_samples[block_index];

            s->fmt_conv.int32_to_float(samples[0], subband_samples[0],
                                       DCA_SUBBANDS_X96K * SAMPLES_PER_SUBBAND);

            if (s->channel_order_tab[k] >= 0)
                qmf_64_subbands(s, k, samples,
                                s->samples_chanptr[s->channel_order_tab[k]],
                                /* Upsampling needs a factor 2 here. */
                                M_SQRT2 / 32768.0);
        }
    } else {
        /* 32 subbands QMF */
        LOCAL_ALIGNED(32, float, samples, [DCA_SUBBANDS], [SAMPLES_PER_SUBBAND]);

        for (k = 0; k < s->audio_header.prim_channels; k++) {
            int32_t (*subband_samples)[SAMPLES_PER_SUBBAND] =
                     s->dca_chan[k].subband_samples[block_index];

            s->fmt_conv.int32_to_float(samples[0], subband_samples[0],
                                       DCA_SUBBANDS * SAMPLES_PER_SUBBAND);

            if (s->channel_order_tab[k] >= 0)
                qmf_32_subbands(s, k, samples,
                                s->samples_chanptr[s->channel_order_tab[k]],
                                M_SQRT1_2 / 32768.0);
        }
    }

    /* Generate LFE samples for this subsubframe FIXME!!! */
    if (s->lfe) {
        float *samples = s->samples_chanptr[ff_dca_lfe_index[s->amode]];
        lfe_interpolation_fir(s,
                              s->lfe_data + 2 * s->lfe * (block_index + 4),
                              samples);
        if (upsample) {
            unsigned i;
            /* Should apply the filter in Table 6-11 when upsampling. For
             * now, just duplicate. */
            for (i = 511; i > 0; i--) {
                samples[2 * i]     =
                samples[2 * i + 1] = samples[i];
            }
            samples[1] = samples[0];
        }
    }

    /* FIXME: This downmixing is probably broken with upsample.
     * Probably totally broken also with XLL in general. */
    /* Downmixing to Stereo */
    if (s->audio_header.prim_channels + !!s->lfe > 2 &&
        s->avctx->request_channel_layout == AV_CH_LAYOUT_STEREO) {
        dca_downmix(s->samples_chanptr, s->amode, !!s->lfe, s->downmix_coef,
                    s->channel_order_tab);
    }

    return 0;
}

static int dca_subframe_footer(DCAContext *s, int base_channel)
{
    int in, out, aux_data_count, aux_data_end, reserved;
    uint32_t nsyncaux;

    /*
     * Unpack optional information
     */

    /* presumably optional information only appears in the core? */
    if (!base_channel) {
        if (s->timestamp)
            skip_bits_long(&s->gb, 32);

        if (s->aux_data) {
            aux_data_count = get_bits(&s->gb, 6);

            // align (32-bit)
            skip_bits_long(&s->gb, (-get_bits_count(&s->gb)) & 31);

            aux_data_end = 8 * aux_data_count + get_bits_count(&s->gb);

            if ((nsyncaux = get_bits_long(&s->gb, 32)) != DCA_NSYNCAUX) {
                av_log(s->avctx, AV_LOG_ERROR, "nSYNCAUX mismatch %#"PRIx32"\n",
                       nsyncaux);
                return AVERROR_INVALIDDATA;
            }

            if (get_bits1(&s->gb)) { // bAUXTimeStampFlag
                avpriv_request_sample(s->avctx,
                                      "Auxiliary Decode Time Stamp Flag");
                // align (4-bit)
                skip_bits(&s->gb, (-get_bits_count(&s->gb)) & 4);
                // 44 bits: nMSByte (8), nMarker (4), nLSByte (28), nMarker (4)
                skip_bits_long(&s->gb, 44);
            }

            if ((s->core_downmix = get_bits1(&s->gb))) {
                int am = get_bits(&s->gb, 3);
                switch (am) {
                case 0:
                    s->core_downmix_amode = DCA_MONO;
                    break;
                case 1:
                    s->core_downmix_amode = DCA_STEREO;
                    break;
                case 2:
                    s->core_downmix_amode = DCA_STEREO_TOTAL;
                    break;
                case 3:
                    s->core_downmix_amode = DCA_3F;
                    break;
                case 4:
                    s->core_downmix_amode = DCA_2F1R;
                    break;
                case 5:
                    s->core_downmix_amode = DCA_2F2R;
                    break;
                case 6:
                    s->core_downmix_amode = DCA_3F1R;
                    break;
                default:
                    av_log(s->avctx, AV_LOG_ERROR,
                           "Invalid mode %d for embedded downmix coefficients\n",
                           am);
                    return AVERROR_INVALIDDATA;
                }
                for (out = 0; out < ff_dca_channels[s->core_downmix_amode]; out++) {
                    for (in = 0; in < s->audio_header.prim_channels + !!s->lfe; in++) {
                        uint16_t tmp = get_bits(&s->gb, 9);
                        if ((tmp & 0xFF) > 241) {
                            av_log(s->avctx, AV_LOG_ERROR,
                                   "Invalid downmix coefficient code %"PRIu16"\n",
                                   tmp);
                            return AVERROR_INVALIDDATA;
                        }
                        s->core_downmix_codes[in][out] = tmp;
                    }
                }
            }

            align_get_bits(&s->gb); // byte align
            skip_bits(&s->gb, 16);  // nAUXCRC16

            /*
             * additional data (reserved, cf. ETSI TS 102 114 V1.4.1)
             *
             * Note: don't check for overreads, aux_data_count can't be trusted.
             */
            if ((reserved = (aux_data_end - get_bits_count(&s->gb))) > 0) {
                avpriv_request_sample(s->avctx,
                                      "Core auxiliary data reserved content");
                skip_bits_long(&s->gb, reserved);
            }
        }

        if (s->crc_present && s->dynrange)
            get_bits(&s->gb, 16);
    }

    return 0;
}

/**
 * Decode a dca frame block
 *
 * @param s     pointer to the DCAContext
 */

static int dca_decode_block(DCAContext *s, int base_channel, int block_index)
{
    int ret;

    /* Sanity check */
    if (s->current_subframe >= s->audio_header.subframes) {
        av_log(s->avctx, AV_LOG_DEBUG, "check failed: %i>%i",
               s->current_subframe, s->audio_header.subframes);
        return AVERROR_INVALIDDATA;
    }

    if (!s->current_subsubframe) {
        /* Read subframe header */
        if ((ret = dca_subframe_header(s, base_channel, block_index)))
            return ret;
    }

    /* Read subsubframe */
    if ((ret = dca_subsubframe(s, base_channel, block_index)))
        return ret;

    /* Update state */
    s->current_subsubframe++;
    if (s->current_subsubframe >= s->subsubframes[s->current_subframe]) {
        s->current_subsubframe = 0;
        s->current_subframe++;
    }
    if (s->current_subframe >= s->audio_header.subframes) {
        /* Read subframe footer */
        if ((ret = dca_subframe_footer(s, base_channel)))
            return ret;
    }

    return 0;
}

static float dca_dmix_code(unsigned code)
{
    int sign = (code >> 8) - 1;
    code &= 0xff;
    return ((ff_dca_dmixtable[code] ^ sign) - sign) * (1.0 / (1U << 15));
}

static int scan_for_extensions(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;
    int core_ss_end, ret = 0;

    core_ss_end = FFMIN(s->frame_size, s->dca_buffer_size) * 8;

    /* only scan for extensions if ext_descr was unknown or indicated a
     * supported XCh extension */
    if (s->core_ext_mask < 0 || s->core_ext_mask & DCA_EXT_XCH) {
        /* if ext_descr was unknown, clear s->core_ext_mask so that the
         * extensions scan can fill it up */
        s->core_ext_mask = FFMAX(s->core_ext_mask, 0);

        /* extensions start at 32-bit boundaries into bitstream */
        skip_bits_long(&s->gb, (-get_bits_count(&s->gb)) & 31);

        while (core_ss_end - get_bits_count(&s->gb) >= 32) {
            uint32_t bits = get_bits_long(&s->gb, 32);
            int i;

            switch (bits) {
            case DCA_SYNCWORD_XCH: {
                int ext_amode, xch_fsize;

                s->xch_base_channel = s->audio_header.prim_channels;

                /* validate sync word using XCHFSIZE field */
                xch_fsize = show_bits(&s->gb, 10);
                if ((s->frame_size != (get_bits_count(&s->gb) >> 3) - 4 + xch_fsize) &&
                    (s->frame_size != (get_bits_count(&s->gb) >> 3) - 4 + xch_fsize + 1))
                    continue;

                /* skip length-to-end-of-frame field for the moment */
                skip_bits(&s->gb, 10);

                s->core_ext_mask |= DCA_EXT_XCH;

                /* extension amode(number of channels in extension) should be 1 */
                /* AFAIK XCh is not used for more channels */
                if ((ext_amode = get_bits(&s->gb, 4)) != 1) {
                    av_log(avctx, AV_LOG_ERROR,
                           "XCh extension amode %d not supported!\n",
                           ext_amode);
                    continue;
                }

                /* much like core primary audio coding header */
                dca_parse_audio_coding_header(s, s->xch_base_channel);

                for (i = 0; i < (s->sample_blocks / 8); i++)
                    if ((ret = dca_decode_block(s, s->xch_base_channel, i))) {
                        av_log(avctx, AV_LOG_ERROR, "error decoding XCh extension\n");
                        continue;
                    }

                s->xch_present = 1;
                break;
            }
            case DCA_SYNCWORD_XXCH:
                /* XXCh: extended channels */
                /* usually found either in core or HD part in DTS-HD HRA streams,
                 * but not in DTS-ES which contains XCh extensions instead */
                s->core_ext_mask |= DCA_EXT_XXCH;
                break;

            case 0x1d95f262: {
                int fsize96 = show_bits(&s->gb, 12) + 1;
                if (s->frame_size != (get_bits_count(&s->gb) >> 3) - 4 + fsize96)
                    continue;

                av_log(avctx, AV_LOG_DEBUG, "X96 extension found at %d bits\n",
                       get_bits_count(&s->gb));
                skip_bits(&s->gb, 12);
                av_log(avctx, AV_LOG_DEBUG, "FSIZE96 = %d bytes\n", fsize96);
                av_log(avctx, AV_LOG_DEBUG, "REVNO = %d\n", get_bits(&s->gb, 4));

                s->core_ext_mask |= DCA_EXT_X96;
                break;
            }
            }

            skip_bits_long(&s->gb, (-get_bits_count(&s->gb)) & 31);
        }
    } else {
        /* no supported extensions, skip the rest of the core substream */
        skip_bits_long(&s->gb, core_ss_end - get_bits_count(&s->gb));
    }

    if (s->core_ext_mask & DCA_EXT_X96)
        s->profile = FF_PROFILE_DTS_96_24;
    else if (s->core_ext_mask & (DCA_EXT_XCH | DCA_EXT_XXCH))
        s->profile = FF_PROFILE_DTS_ES;

    /* check for ExSS (HD part) */
    if (s->dca_buffer_size - s->frame_size > 32 &&
        get_bits_long(&s->gb, 32) == DCA_SYNCWORD_SUBSTREAM)
        ff_dca_exss_parse_header(s);

    return ret;
}

static int set_channel_layout(AVCodecContext *avctx, int channels, int num_core_channels)
{
    DCAContext *s = avctx->priv_data;
    int i;

    if (s->amode < 16) {
        avctx->channel_layout = dca_core_channel_layout[s->amode];

        if (s->audio_header.prim_channels + !!s->lfe > 2 &&
            avctx->request_channel_layout == AV_CH_LAYOUT_STEREO) {
            /*
             * Neither the core's auxiliary data nor our default tables contain
             * downmix coefficients for the additional channel coded in the XCh
             * extension, so when we're doing a Stereo downmix, don't decode it.
             */
            s->xch_disable = 1;
        }

        if (s->xch_present && !s->xch_disable) {
            avctx->channel_layout |= AV_CH_BACK_CENTER;
            if (s->lfe) {
                avctx->channel_layout |= AV_CH_LOW_FREQUENCY;
                s->channel_order_tab = ff_dca_channel_reorder_lfe_xch[s->amode];
            } else {
                s->channel_order_tab = ff_dca_channel_reorder_nolfe_xch[s->amode];
            }
        } else {
            channels       = num_core_channels + !!s->lfe;
            s->xch_present = 0; /* disable further xch processing */
            if (s->lfe) {
                avctx->channel_layout |= AV_CH_LOW_FREQUENCY;
                s->channel_order_tab = ff_dca_channel_reorder_lfe[s->amode];
            } else
                s->channel_order_tab = ff_dca_channel_reorder_nolfe[s->amode];
        }

        if (channels > !!s->lfe &&
            s->channel_order_tab[channels - 1 - !!s->lfe] < 0)
            return AVERROR_INVALIDDATA;

        if (num_core_channels + !!s->lfe > 2 &&
            avctx->request_channel_layout == AV_CH_LAYOUT_STEREO) {
            channels              = 2;
            s->output             = s->audio_header.prim_channels == 2 ? s->amode : DCA_STEREO;
            avctx->channel_layout = AV_CH_LAYOUT_STEREO;

            /* Stereo downmix coefficients
             *
             * The decoder can only downmix to 2-channel, so we need to ensure
             * embedded downmix coefficients are actually targeting 2-channel.
             */
            if (s->core_downmix && (s->core_downmix_amode == DCA_STEREO ||
                                    s->core_downmix_amode == DCA_STEREO_TOTAL)) {
                for (i = 0; i < num_core_channels + !!s->lfe; i++) {
                    /* Range checked earlier */
                    s->downmix_coef[i][0] = dca_dmix_code(s->core_downmix_codes[i][0]);
                    s->downmix_coef[i][1] = dca_dmix_code(s->core_downmix_codes[i][1]);
                }
                s->output = s->core_downmix_amode;
            } else {
                int am = s->amode & DCA_CHANNEL_MASK;
                if (am >= FF_ARRAY_ELEMS(ff_dca_default_coeffs)) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "Invalid channel mode %d\n", am);
                    return AVERROR_INVALIDDATA;
                }
                if (num_core_channels + !!s->lfe >
                    FF_ARRAY_ELEMS(ff_dca_default_coeffs[0])) {
                    avpriv_request_sample(s->avctx, "Downmixing %d channels",
                                          s->audio_header.prim_channels + !!s->lfe);
                    return AVERROR_PATCHWELCOME;
                }
                for (i = 0; i < num_core_channels + !!s->lfe; i++) {
                    s->downmix_coef[i][0] = ff_dca_default_coeffs[am][i][0];
                    s->downmix_coef[i][1] = ff_dca_default_coeffs[am][i][1];
                }
            }
            ff_dlog(s->avctx, "Stereo downmix coeffs:\n");
            for (i = 0; i < num_core_channels + !!s->lfe; i++) {
                ff_dlog(s->avctx, "L, input channel %d = %f\n", i,
                        s->downmix_coef[i][0]);
                ff_dlog(s->avctx, "R, input channel %d = %f\n", i,
                        s->downmix_coef[i][1]);
            }
            ff_dlog(s->avctx, "\n");
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "Non standard configuration %d !\n", s->amode);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

/**
 * Main frame decoding function
 * FIXME add arguments
 */
static int dca_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;

    int lfe_samples;
    int num_core_channels = 0;
    int i, ret;
    float  **samples_flt;
    DCAContext *s = avctx->priv_data;
    int channels, full_channels;
    int upsample = 0;

    s->exss_ext_mask = 0;
    s->xch_present   = 0;

    s->dca_buffer_size = ff_dca_convert_bitstream(buf, buf_size, s->dca_buffer,
                                                  DCA_MAX_FRAME_SIZE + DCA_MAX_EXSS_HEADER_SIZE);
    if (s->dca_buffer_size == AVERROR_INVALIDDATA) {
        av_log(avctx, AV_LOG_ERROR, "Not a valid DCA frame\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = dca_parse_frame_header(s)) < 0) {
        // seems like the frame is corrupt, try with the next one
        return ret;
    }
    // set AVCodec values with parsed data
    avctx->sample_rate = s->sample_rate;
    avctx->bit_rate    = s->bit_rate;

    s->profile = FF_PROFILE_DTS;

    for (i = 0; i < (s->sample_blocks / SAMPLES_PER_SUBBAND); i++) {
        if ((ret = dca_decode_block(s, 0, i))) {
            av_log(avctx, AV_LOG_ERROR, "error decoding block\n");
            return ret;
        }
    }

    /* record number of core channels incase less than max channels are requested */
    num_core_channels = s->audio_header.prim_channels;

    if (s->ext_coding)
        s->core_ext_mask = dca_ext_audio_descr_mask[s->ext_descr];
    else
        s->core_ext_mask = 0;

    ret = scan_for_extensions(avctx);

    avctx->profile = s->profile;

    full_channels = channels = s->audio_header.prim_channels + !!s->lfe;

    ret = set_channel_layout(avctx, channels, num_core_channels);
    if (ret < 0)
        return ret;
    avctx->channels = channels;

    /* get output buffer */
    frame->nb_samples = 256 * (s->sample_blocks / SAMPLES_PER_SUBBAND);
    if (s->exss_ext_mask & DCA_EXT_EXSS_XLL) {
        int xll_nb_samples = s->xll_segments * s->xll_smpl_in_seg;
        /* Check for invalid/unsupported conditions first */
        if (s->xll_residual_channels > channels) {
            av_log(s->avctx, AV_LOG_WARNING,
                   "DCA: too many residual channels (%d, core channels %d). Disabling XLL\n",
                   s->xll_residual_channels, channels);
            s->exss_ext_mask &= ~DCA_EXT_EXSS_XLL;
        } else if (xll_nb_samples != frame->nb_samples &&
                   2 * frame->nb_samples != xll_nb_samples) {
            av_log(s->avctx, AV_LOG_WARNING,
                   "DCA: unsupported upsampling (%d XLL samples, %d core samples). Disabling XLL\n",
                   xll_nb_samples, frame->nb_samples);
            s->exss_ext_mask &= ~DCA_EXT_EXSS_XLL;
        } else {
            if (2 * frame->nb_samples == xll_nb_samples) {
                av_log(s->avctx, AV_LOG_INFO,
                       "XLL: upsampling core channels by a factor of 2\n");
                upsample = 1;

                frame->nb_samples = xll_nb_samples;
                // FIXME: Is it good enough to copy from the first channel set?
                avctx->sample_rate = s->xll_chsets[0].sampling_frequency;
            }
            /* If downmixing to stereo, don't decode additional channels.
             * FIXME: Using the xch_disable flag for this doesn't seem right. */
            if (!s->xch_disable)
                avctx->channels += s->xll_channels - s->xll_residual_channels;
        }
    }

    /* FIXME: This is an ugly hack, to just revert to the default
     * layout if we have additional channels. Need to convert the XLL
     * channel masks to libav channel_layout mask. */
    if (av_get_channel_layout_nb_channels(avctx->channel_layout) != avctx->channels)
        avctx->channel_layout = 0;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    samples_flt = (float **) frame->extended_data;

    /* allocate buffer for extra channels if downmixing */
    if (avctx->channels < full_channels) {
        ret = av_samples_get_buffer_size(NULL, full_channels - channels,
                                         frame->nb_samples,
                                         avctx->sample_fmt, 0);
        if (ret < 0)
            return ret;

        av_fast_malloc(&s->extra_channels_buffer,
                       &s->extra_channels_buffer_size, ret);
        if (!s->extra_channels_buffer)
            return AVERROR(ENOMEM);

        ret = av_samples_fill_arrays((uint8_t **) s->extra_channels, NULL,
                                     s->extra_channels_buffer,
                                     full_channels - channels,
                                     frame->nb_samples, avctx->sample_fmt, 0);
        if (ret < 0)
            return ret;
    }

    /* filter to get final output */
    for (i = 0; i < (s->sample_blocks / SAMPLES_PER_SUBBAND); i++) {
        int ch;
        unsigned block = upsample ? 512 : 256;
        for (ch = 0; ch < channels; ch++)
            s->samples_chanptr[ch] = samples_flt[ch] + i * block;
        for (; ch < full_channels; ch++)
            s->samples_chanptr[ch] = s->extra_channels[ch - channels] + i * block;

        dca_filter_channels(s, i, upsample);

        /* If this was marked as a DTS-ES stream we need to subtract back- */
        /* channel from SL & SR to remove matrixed back-channel signal */
        if ((s->source_pcm_res & 1) && s->xch_present) {
            float *back_chan = s->samples_chanptr[s->channel_order_tab[s->xch_base_channel]];
            float *lt_chan   = s->samples_chanptr[s->channel_order_tab[s->xch_base_channel - 2]];
            float *rt_chan   = s->samples_chanptr[s->channel_order_tab[s->xch_base_channel - 1]];
            s->fdsp.vector_fmac_scalar(lt_chan, back_chan, -M_SQRT1_2, 256);
            s->fdsp.vector_fmac_scalar(rt_chan, back_chan, -M_SQRT1_2, 256);
        }
    }

    /* update lfe history */
    lfe_samples = 2 * s->lfe * (s->sample_blocks / SAMPLES_PER_SUBBAND);
    for (i = 0; i < 2 * s->lfe * 4; i++)
        s->lfe_data[i] = s->lfe_data[i + lfe_samples];

    if (s->exss_ext_mask & DCA_EXT_EXSS_XLL) {
        ret = ff_dca_xll_decode_audio(s, frame);
        if (ret < 0)
            return ret;
    }
    /* AVMatrixEncoding
     *
     * DCA_STEREO_TOTAL (Lt/Rt) is equivalent to Dolby Surround */
    ret = ff_side_data_update_matrix_encoding(frame,
                                              (s->output & ~DCA_LFE) == DCA_STEREO_TOTAL ?
                                              AV_MATRIX_ENCODING_DOLBY : AV_MATRIX_ENCODING_NONE);
    if (ret < 0)
        return ret;

    *got_frame_ptr = 1;

    return buf_size;
}

/**
 * DCA initialization
 *
 * @param avctx     pointer to the AVCodecContext
 */

static av_cold int dca_decode_init(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;

    s->avctx = avctx;
    dca_init_vlcs();

    avpriv_float_dsp_init(&s->fdsp, avctx->flags & AV_CODEC_FLAG_BITEXACT);
    ff_mdct_init(&s->imdct, 6, 1, 1.0);
    ff_synth_filter_init(&s->synth);
    ff_dcadsp_init(&s->dcadsp);
    ff_fmt_convert_init(&s->fmt_conv, avctx);

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    /* allow downmixing to stereo */
    if (avctx->channels > 2 &&
        avctx->request_channel_layout == AV_CH_LAYOUT_STEREO)
        avctx->channels = 2;

    return 0;
}

static av_cold int dca_decode_end(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;
    ff_mdct_end(&s->imdct);
    av_freep(&s->extra_channels_buffer);
    av_freep(&s->xll_sample_buf);
    av_freep(&s->qmf64_table);
    return 0;
}

static const AVOption options[] = {
    { "disable_xch", "disable decoding of the XCh extension", offsetof(DCAContext, xch_disable), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM },
    { "disable_xll", "disable decoding of the XLL extension", offsetof(DCAContext, xll_disable), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM },
    { NULL },
};

static const AVClass dca_decoder_class = {
    .class_name = "DCA decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_dca_decoder = {
    .name            = "dca",
    .long_name       = NULL_IF_CONFIG_SMALL("DCA (DTS Coherent Acoustics)"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_DTS,
    .priv_data_size  = sizeof(DCAContext),
    .init            = dca_decode_init,
    .decode          = dca_decode_frame,
    .close           = dca_decode_end,
    .capabilities    = AV_CODEC_CAP_CHANNEL_CONF | AV_CODEC_CAP_DR1,
    .sample_fmts     = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                       AV_SAMPLE_FMT_NONE },
    .profiles        = NULL_IF_CONFIG_SMALL(ff_dca_profiles),
    .priv_class      = &dca_decoder_class,
};
