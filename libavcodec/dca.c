/*
 * DCA compatible decoder
 * Copyright (C) 2004 Gildas Bazin
 * Copyright (C) 2004 Benjamin Zores
 * Copyright (C) 2006 Benjamin Larsson
 * Copyright (C) 2007 Konstantin Shishkov
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
 * @file dca.c
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "avcodec.h"
#include "dsputil.h"
#include "bitstream.h"
#include "dcadata.h"
#include "dcahuff.h"
#include "parser.h"

/** DCA syncwords, also used for bitstream type detection */
//@{
#define DCA_MARKER_RAW_BE 0x7FFE8001
#define DCA_MARKER_RAW_LE 0xFE7F0180
#define DCA_MARKER_14B_BE 0x1FFFE800
#define DCA_MARKER_14B_LE 0xFF1F00E8
//@}

//#define TRACE

#define DCA_PRIM_CHANNELS_MAX (5)
#define DCA_SUBBANDS (32)
#define DCA_ABITS_MAX (32)      /* Should be 28 */
#define DCA_SUBSUBFAMES_MAX (4)
#define DCA_LFE_MAX (3)

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

#define DCA_DOLBY 101           /* FIXME */

#define DCA_CHANNEL_BITS 6
#define DCA_CHANNEL_MASK 0x3F

#define DCA_LFE 0x80

#define HEADER_SIZE 14
#define CONVERT_BIAS 384

#define DCA_MAX_FRAME_SIZE 16383

/** Bit allocation */
typedef struct {
    int offset;                 ///< code values offset
    int maxbits[8];             ///< max bits in VLC
    int wrap;                   ///< wrap for get_vlc2()
    VLC vlc[8];                 ///< actual codes
} BitAlloc;

static BitAlloc dca_bitalloc_index;    ///< indexes for samples VLC select
static BitAlloc dca_tmode;             ///< transition mode VLCs
static BitAlloc dca_scalefactor;       ///< scalefactor VLCs
static BitAlloc dca_smpl_bitalloc[11]; ///< samples VLCs

/** Pre-calculated cosine modulation coefs for the QMF */
static float cos_mod[544];

static int av_always_inline get_bitalloc(GetBitContext *gb, BitAlloc *ba, int idx)
{
    return get_vlc2(gb, ba->vlc[idx].table, ba->vlc[idx].bits, ba->wrap) + ba->offset;
}

typedef struct {
    AVCodecContext *avctx;
    /* Frame header */
    int frame_type;             ///< type of the current frame
    int samples_deficit;        ///< deficit sample count
    int crc_present;            ///< crc is present in the bitstream
    int sample_blocks;          ///< number of PCM sample blocks
    int frame_size;             ///< primary frame byte size
    int amode;                  ///< audio channels arrangement
    int sample_rate;            ///< audio sampling rate
    int bit_rate;               ///< transmission bit rate

    int downmix;                ///< embedded downmix enabled
    int dynrange;               ///< embedded dynamic range flag
    int timestamp;              ///< embedded time stamp flag
    int aux_data;               ///< auxiliary data flag
    int hdcd;                   ///< source material is mastered in HDCD
    int ext_descr;              ///< extension audio descriptor flag
    int ext_coding;             ///< extended coding flag
    int aspf;                   ///< audio sync word insertion flag
    int lfe;                    ///< low frequency effects flag
    int predictor_history;      ///< predictor history flag
    int header_crc;             ///< header crc check bytes
    int multirate_inter;        ///< multirate interpolator switch
    int version;                ///< encoder software revision
    int copy_history;           ///< copy history
    int source_pcm_res;         ///< source pcm resolution
    int front_sum;              ///< front sum/difference flag
    int surround_sum;           ///< surround sum/difference flag
    int dialog_norm;            ///< dialog normalisation parameter

    /* Primary audio coding header */
    int subframes;              ///< number of subframes
    int prim_channels;          ///< number of primary audio channels
    int subband_activity[DCA_PRIM_CHANNELS_MAX];    ///< subband activity count
    int vq_start_subband[DCA_PRIM_CHANNELS_MAX];    ///< high frequency vq start subband
    int joint_intensity[DCA_PRIM_CHANNELS_MAX];     ///< joint intensity coding index
    int transient_huffman[DCA_PRIM_CHANNELS_MAX];   ///< transient mode code book
    int scalefactor_huffman[DCA_PRIM_CHANNELS_MAX]; ///< scale factor code book
    int bitalloc_huffman[DCA_PRIM_CHANNELS_MAX];    ///< bit allocation quantizer select
    int quant_index_huffman[DCA_PRIM_CHANNELS_MAX][DCA_ABITS_MAX]; ///< quantization index codebook select
    float scalefactor_adj[DCA_PRIM_CHANNELS_MAX][DCA_ABITS_MAX];   ///< scale factor adjustment

    /* Primary audio coding side information */
    int subsubframes;           ///< number of subsubframes
    int partial_samples;        ///< partial subsubframe samples count
    int prediction_mode[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS];    ///< prediction mode (ADPCM used or not)
    int prediction_vq[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS];      ///< prediction VQ coefs
    int bitalloc[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS];           ///< bit allocation index
    int transition_mode[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS];    ///< transition mode (transients)
    int scale_factor[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS][2];    ///< scale factors (2 if transient)
    int joint_huff[DCA_PRIM_CHANNELS_MAX];                       ///< joint subband scale factors codebook
    int joint_scale_factor[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS]; ///< joint subband scale factors
    int downmix_coef[DCA_PRIM_CHANNELS_MAX][2];                  ///< stereo downmix coefficients
    int dynrange_coef;                                           ///< dynamic range coefficient

    int high_freq_vq[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS];       ///< VQ encoded high frequency subbands

    float lfe_data[2 * DCA_SUBSUBFAMES_MAX * DCA_LFE_MAX *
                   2 /*history */ ];    ///< Low frequency effect data
    int lfe_scale_factor;

    /* Subband samples history (for ADPCM) */
    float subband_samples_hist[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS][4];
    float subband_fir_hist[DCA_PRIM_CHANNELS_MAX][512];
    float subband_fir_noidea[DCA_PRIM_CHANNELS_MAX][64];

    int output;                 ///< type of output
    int bias;                   ///< output bias

    DECLARE_ALIGNED_16(float, samples[1536]);  /* 6 * 256 = 1536, might only need 5 */
    DECLARE_ALIGNED_16(int16_t, tsamples[1536]);

    uint8_t dca_buffer[DCA_MAX_FRAME_SIZE];
    int dca_buffer_size;        ///< how much data is in the dca_buffer

    GetBitContext gb;
    /* Current position in DCA frame */
    int current_subframe;
    int current_subsubframe;

    int debug_flag;             ///< used for suppressing repeated error messages output
    DSPContext dsp;
} DCAContext;

static void dca_init_vlcs(void)
{
    static int vlcs_inited = 0;
    int i, j;

    if (vlcs_inited)
        return;

    dca_bitalloc_index.offset = 1;
    dca_bitalloc_index.wrap = 1;
    for (i = 0; i < 5; i++)
        init_vlc(&dca_bitalloc_index.vlc[i], bitalloc_12_vlc_bits[i], 12,
                 bitalloc_12_bits[i], 1, 1,
                 bitalloc_12_codes[i], 2, 2, 1);
    dca_scalefactor.offset = -64;
    dca_scalefactor.wrap = 2;
    for (i = 0; i < 5; i++)
        init_vlc(&dca_scalefactor.vlc[i], SCALES_VLC_BITS, 129,
                 scales_bits[i], 1, 1,
                 scales_codes[i], 2, 2, 1);
    dca_tmode.offset = 0;
    dca_tmode.wrap = 1;
    for (i = 0; i < 4; i++)
        init_vlc(&dca_tmode.vlc[i], tmode_vlc_bits[i], 4,
                 tmode_bits[i], 1, 1,
                 tmode_codes[i], 2, 2, 1);

    for(i = 0; i < 10; i++)
        for(j = 0; j < 7; j++){
            if(!bitalloc_codes[i][j]) break;
            dca_smpl_bitalloc[i+1].offset = bitalloc_offsets[i];
            dca_smpl_bitalloc[i+1].wrap = 1 + (j > 4);
            init_vlc(&dca_smpl_bitalloc[i+1].vlc[j], bitalloc_maxbits[i][j],
                     bitalloc_sizes[i],
                     bitalloc_bits[i][j], 1, 1,
                     bitalloc_codes[i][j], 2, 2, 1);
        }
    vlcs_inited = 1;
}

static inline void get_array(GetBitContext *gb, int *dst, int len, int bits)
{
    while(len--)
        *dst++ = get_bits(gb, bits);
}

static int dca_parse_frame_header(DCAContext * s)
{
    int i, j;
    static const float adj_table[4] = { 1.0, 1.1250, 1.2500, 1.4375 };
    static const int bitlen[11] = { 0, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3 };
    static const int thr[11] = { 0, 1, 3, 3, 3, 3, 7, 7, 7, 7, 7 };

    s->bias = CONVERT_BIAS;

    init_get_bits(&s->gb, s->dca_buffer, s->dca_buffer_size * 8);

    /* Sync code */
    get_bits(&s->gb, 32);

    /* Frame header */
    s->frame_type        = get_bits(&s->gb, 1);
    s->samples_deficit   = get_bits(&s->gb, 5) + 1;
    s->crc_present       = get_bits(&s->gb, 1);
    s->sample_blocks     = get_bits(&s->gb, 7) + 1;
    s->frame_size        = get_bits(&s->gb, 14) + 1;
    if (s->frame_size < 95)
        return -1;
    s->amode             = get_bits(&s->gb, 6);
    s->sample_rate       = dca_sample_rates[get_bits(&s->gb, 4)];
    if (!s->sample_rate)
        return -1;
    s->bit_rate          = dca_bit_rates[get_bits(&s->gb, 5)];
    if (!s->bit_rate)
        return -1;

    s->downmix           = get_bits(&s->gb, 1);
    s->dynrange          = get_bits(&s->gb, 1);
    s->timestamp         = get_bits(&s->gb, 1);
    s->aux_data          = get_bits(&s->gb, 1);
    s->hdcd              = get_bits(&s->gb, 1);
    s->ext_descr         = get_bits(&s->gb, 3);
    s->ext_coding        = get_bits(&s->gb, 1);
    s->aspf              = get_bits(&s->gb, 1);
    s->lfe               = get_bits(&s->gb, 2);
    s->predictor_history = get_bits(&s->gb, 1);

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
    s->output = DCA_STEREO;

#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "frame type: %i\n", s->frame_type);
    av_log(s->avctx, AV_LOG_DEBUG, "samples deficit: %i\n", s->samples_deficit);
    av_log(s->avctx, AV_LOG_DEBUG, "crc present: %i\n", s->crc_present);
    av_log(s->avctx, AV_LOG_DEBUG, "sample blocks: %i (%i samples)\n",
           s->sample_blocks, s->sample_blocks * 32);
    av_log(s->avctx, AV_LOG_DEBUG, "frame size: %i bytes\n", s->frame_size);
    av_log(s->avctx, AV_LOG_DEBUG, "amode: %i (%i channels)\n",
           s->amode, dca_channels[s->amode]);
    av_log(s->avctx, AV_LOG_DEBUG, "sample rate: %i (%i Hz)\n",
           s->sample_rate, dca_sample_rates[s->sample_rate]);
    av_log(s->avctx, AV_LOG_DEBUG, "bit rate: %i (%i bits/s)\n",
           s->bit_rate, dca_bit_rates[s->bit_rate]);
    av_log(s->avctx, AV_LOG_DEBUG, "downmix: %i\n", s->downmix);
    av_log(s->avctx, AV_LOG_DEBUG, "dynrange: %i\n", s->dynrange);
    av_log(s->avctx, AV_LOG_DEBUG, "timestamp: %i\n", s->timestamp);
    av_log(s->avctx, AV_LOG_DEBUG, "aux_data: %i\n", s->aux_data);
    av_log(s->avctx, AV_LOG_DEBUG, "hdcd: %i\n", s->hdcd);
    av_log(s->avctx, AV_LOG_DEBUG, "ext descr: %i\n", s->ext_descr);
    av_log(s->avctx, AV_LOG_DEBUG, "ext coding: %i\n", s->ext_coding);
    av_log(s->avctx, AV_LOG_DEBUG, "aspf: %i\n", s->aspf);
    av_log(s->avctx, AV_LOG_DEBUG, "lfe: %i\n", s->lfe);
    av_log(s->avctx, AV_LOG_DEBUG, "predictor history: %i\n",
           s->predictor_history);
    av_log(s->avctx, AV_LOG_DEBUG, "header crc: %i\n", s->header_crc);
    av_log(s->avctx, AV_LOG_DEBUG, "multirate inter: %i\n",
           s->multirate_inter);
    av_log(s->avctx, AV_LOG_DEBUG, "version number: %i\n", s->version);
    av_log(s->avctx, AV_LOG_DEBUG, "copy history: %i\n", s->copy_history);
    av_log(s->avctx, AV_LOG_DEBUG,
           "source pcm resolution: %i (%i bits/sample)\n",
           s->source_pcm_res, dca_bits_per_sample[s->source_pcm_res]);
    av_log(s->avctx, AV_LOG_DEBUG, "front sum: %i\n", s->front_sum);
    av_log(s->avctx, AV_LOG_DEBUG, "surround sum: %i\n", s->surround_sum);
    av_log(s->avctx, AV_LOG_DEBUG, "dialog norm: %i\n", s->dialog_norm);
    av_log(s->avctx, AV_LOG_DEBUG, "\n");
#endif

    /* Primary audio coding header */
    s->subframes         = get_bits(&s->gb, 4) + 1;
    s->prim_channels     = get_bits(&s->gb, 3) + 1;


    for (i = 0; i < s->prim_channels; i++) {
        s->subband_activity[i] = get_bits(&s->gb, 5) + 2;
        if (s->subband_activity[i] > DCA_SUBBANDS)
            s->subband_activity[i] = DCA_SUBBANDS;
    }
    for (i = 0; i < s->prim_channels; i++) {
        s->vq_start_subband[i] = get_bits(&s->gb, 5) + 1;
        if (s->vq_start_subband[i] > DCA_SUBBANDS)
            s->vq_start_subband[i] = DCA_SUBBANDS;
    }
    get_array(&s->gb, s->joint_intensity,     s->prim_channels, 3);
    get_array(&s->gb, s->transient_huffman,   s->prim_channels, 2);
    get_array(&s->gb, s->scalefactor_huffman, s->prim_channels, 3);
    get_array(&s->gb, s->bitalloc_huffman,    s->prim_channels, 3);

    /* Get codebooks quantization indexes */
    memset(s->quant_index_huffman, 0, sizeof(s->quant_index_huffman));
    for (j = 1; j < 11; j++)
        for (i = 0; i < s->prim_channels; i++)
            s->quant_index_huffman[i][j] = get_bits(&s->gb, bitlen[j]);

    /* Get scale factor adjustment */
    for (j = 0; j < 11; j++)
        for (i = 0; i < s->prim_channels; i++)
            s->scalefactor_adj[i][j] = 1;

    for (j = 1; j < 11; j++)
        for (i = 0; i < s->prim_channels; i++)
            if (s->quant_index_huffman[i][j] < thr[j])
                s->scalefactor_adj[i][j] = adj_table[get_bits(&s->gb, 2)];

    if (s->crc_present) {
        /* Audio header CRC check */
        get_bits(&s->gb, 16);
    }

    s->current_subframe = 0;
    s->current_subsubframe = 0;

#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "subframes: %i\n", s->subframes);
    av_log(s->avctx, AV_LOG_DEBUG, "prim channels: %i\n", s->prim_channels);
    for(i = 0; i < s->prim_channels; i++){
        av_log(s->avctx, AV_LOG_DEBUG, "subband activity: %i\n", s->subband_activity[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "vq start subband: %i\n", s->vq_start_subband[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "joint intensity: %i\n", s->joint_intensity[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "transient mode codebook: %i\n", s->transient_huffman[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "scale factor codebook: %i\n", s->scalefactor_huffman[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "bit allocation quantizer: %i\n", s->bitalloc_huffman[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "quant index huff:");
        for (j = 0; j < 11; j++)
            av_log(s->avctx, AV_LOG_DEBUG, " %i",
                   s->quant_index_huffman[i][j]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
        av_log(s->avctx, AV_LOG_DEBUG, "scalefac adj:");
        for (j = 0; j < 11; j++)
            av_log(s->avctx, AV_LOG_DEBUG, " %1.3f", s->scalefactor_adj[i][j]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
#endif

    return 0;
}


static inline int get_scale(GetBitContext *gb, int level, int index, int value)
{
   if (level < 5) {
       /* huffman encoded */
       value += get_bitalloc(gb, &dca_scalefactor, index);
   } else if(level < 8)
       value = get_bits(gb, level + 1);
   return value;
}

static int dca_subframe_header(DCAContext * s)
{
    /* Primary audio coding side information */
    int j, k;

    s->subsubframes = get_bits(&s->gb, 2) + 1;
    s->partial_samples = get_bits(&s->gb, 3);
    for (j = 0; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++)
            s->prediction_mode[j][k] = get_bits(&s->gb, 1);
    }

    /* Get prediction codebook */
    for (j = 0; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++) {
            if (s->prediction_mode[j][k] > 0) {
                /* (Prediction coefficient VQ address) */
                s->prediction_vq[j][k] = get_bits(&s->gb, 12);
            }
        }
    }

    /* Bit allocation index */
    for (j = 0; j < s->prim_channels; j++) {
        for (k = 0; k < s->vq_start_subband[j]; k++) {
            if (s->bitalloc_huffman[j] == 6)
                s->bitalloc[j][k] = get_bits(&s->gb, 5);
            else if (s->bitalloc_huffman[j] == 5)
                s->bitalloc[j][k] = get_bits(&s->gb, 4);
            else {
                s->bitalloc[j][k] =
                    get_bitalloc(&s->gb, &dca_bitalloc_index, j);
            }

            if (s->bitalloc[j][k] > 26) {
//                 av_log(s->avctx,AV_LOG_DEBUG,"bitalloc index [%i][%i] too big (%i)\n",
//                          j, k, s->bitalloc[j][k]);
                return -1;
            }
        }
    }

    /* Transition mode */
    for (j = 0; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++) {
            s->transition_mode[j][k] = 0;
            if (s->subsubframes > 1 &&
                k < s->vq_start_subband[j] && s->bitalloc[j][k] > 0) {
                s->transition_mode[j][k] =
                    get_bitalloc(&s->gb, &dca_tmode, s->transient_huffman[j]);
            }
        }
    }

    for (j = 0; j < s->prim_channels; j++) {
        uint32_t *scale_table;
        int scale_sum;

        memset(s->scale_factor[j], 0, s->subband_activity[j] * sizeof(s->scale_factor[0][0][0]) * 2);

        if (s->scalefactor_huffman[j] == 6)
            scale_table = (uint32_t *) scale_factor_quant7;
        else
            scale_table = (uint32_t *) scale_factor_quant6;

        /* When huffman coded, only the difference is encoded */
        scale_sum = 0;

        for (k = 0; k < s->subband_activity[j]; k++) {
            if (k >= s->vq_start_subband[j] || s->bitalloc[j][k] > 0) {
                scale_sum = get_scale(&s->gb, s->scalefactor_huffman[j], j, scale_sum);
                s->scale_factor[j][k][0] = scale_table[scale_sum];
            }

            if (k < s->vq_start_subband[j] && s->transition_mode[j][k]) {
                /* Get second scale factor */
                scale_sum = get_scale(&s->gb, s->scalefactor_huffman[j], j, scale_sum);
                s->scale_factor[j][k][1] = scale_table[scale_sum];
            }
        }
    }

    /* Joint subband scale factor codebook select */
    for (j = 0; j < s->prim_channels; j++) {
        /* Transmitted only if joint subband coding enabled */
        if (s->joint_intensity[j] > 0)
            s->joint_huff[j] = get_bits(&s->gb, 3);
    }

    /* Scale factors for joint subband coding */
    for (j = 0; j < s->prim_channels; j++) {
        int source_channel;

        /* Transmitted only if joint subband coding enabled */
        if (s->joint_intensity[j] > 0) {
            int scale = 0;
            source_channel = s->joint_intensity[j] - 1;

            /* When huffman coded, only the difference is encoded
             * (is this valid as well for joint scales ???) */

            for (k = s->subband_activity[j]; k < s->subband_activity[source_channel]; k++) {
                scale = get_scale(&s->gb, s->joint_huff[j], j, 0);
                scale += 64;    /* bias */
                s->joint_scale_factor[j][k] = scale;    /*joint_scale_table[scale]; */
            }

            if (!s->debug_flag & 0x02) {
                av_log(s->avctx, AV_LOG_DEBUG,
                       "Joint stereo coding not supported\n");
                s->debug_flag |= 0x02;
            }
        }
    }

    /* Stereo downmix coefficients */
    if (s->prim_channels > 2 && s->downmix) {
        for (j = 0; j < s->prim_channels; j++) {
            s->downmix_coef[j][0] = get_bits(&s->gb, 7);
            s->downmix_coef[j][1] = get_bits(&s->gb, 7);
        }
    }

    /* Dynamic range coefficient */
    if (s->dynrange)
        s->dynrange_coef = get_bits(&s->gb, 8);

    /* Side information CRC check word */
    if (s->crc_present) {
        get_bits(&s->gb, 16);
    }

    /*
     * Primary audio data arrays
     */

    /* VQ encoded high frequency subbands */
    for (j = 0; j < s->prim_channels; j++)
        for (k = s->vq_start_subband[j]; k < s->subband_activity[j]; k++)
            /* 1 vector -> 32 samples */
            s->high_freq_vq[j][k] = get_bits(&s->gb, 10);

    /* Low frequency effect data */
    if (s->lfe) {
        /* LFE samples */
        int lfe_samples = 2 * s->lfe * s->subsubframes;
        float lfe_scale;

        for (j = lfe_samples; j < lfe_samples * 2; j++) {
            /* Signed 8 bits int */
            s->lfe_data[j] = get_sbits(&s->gb, 8);
        }

        /* Scale factor index */
        s->lfe_scale_factor = scale_factor_quant7[get_bits(&s->gb, 8)];

        /* Quantization step size * scale factor */
        lfe_scale = 0.035 * s->lfe_scale_factor;

        for (j = lfe_samples; j < lfe_samples * 2; j++)
            s->lfe_data[j] *= lfe_scale;
    }

#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "subsubframes: %i\n", s->subsubframes);
    av_log(s->avctx, AV_LOG_DEBUG, "partial samples: %i\n",
           s->partial_samples);
    for (j = 0; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "prediction mode:");
        for (k = 0; k < s->subband_activity[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, " %i", s->prediction_mode[j][k]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = 0; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++)
                av_log(s->avctx, AV_LOG_DEBUG,
                       "prediction coefs: %f, %f, %f, %f\n",
                       (float) adpcm_vb[s->prediction_vq[j][k]][0] / 8192,
                       (float) adpcm_vb[s->prediction_vq[j][k]][1] / 8192,
                       (float) adpcm_vb[s->prediction_vq[j][k]][2] / 8192,
                       (float) adpcm_vb[s->prediction_vq[j][k]][3] / 8192);
    }
    for (j = 0; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "bitalloc index: ");
        for (k = 0; k < s->vq_start_subband[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, "%2.2i ", s->bitalloc[j][k]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = 0; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "Transition mode:");
        for (k = 0; k < s->subband_activity[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, " %i", s->transition_mode[j][k]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = 0; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "Scale factor:");
        for (k = 0; k < s->subband_activity[j]; k++) {
            if (k >= s->vq_start_subband[j] || s->bitalloc[j][k] > 0)
                av_log(s->avctx, AV_LOG_DEBUG, " %i", s->scale_factor[j][k][0]);
            if (k < s->vq_start_subband[j] && s->transition_mode[j][k])
                av_log(s->avctx, AV_LOG_DEBUG, " %i(t)", s->scale_factor[j][k][1]);
        }
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = 0; j < s->prim_channels; j++) {
        if (s->joint_intensity[j] > 0) {
            av_log(s->avctx, AV_LOG_DEBUG, "Joint scale factor index:\n");
            for (k = s->subband_activity[j]; k < s->subband_activity[source_channel]; k++)
                av_log(s->avctx, AV_LOG_DEBUG, " %i", s->joint_scale_factor[j][k]);
            av_log(s->avctx, AV_LOG_DEBUG, "\n");
        }
    }
    if (s->prim_channels > 2 && s->downmix) {
        av_log(s->avctx, AV_LOG_DEBUG, "Downmix coeffs:\n");
        for (j = 0; j < s->prim_channels; j++) {
            av_log(s->avctx, AV_LOG_DEBUG, "Channel 0,%d = %f\n", j, dca_downmix_coeffs[s->downmix_coef[j][0]]);
            av_log(s->avctx, AV_LOG_DEBUG, "Channel 1,%d = %f\n", j, dca_downmix_coeffs[s->downmix_coef[j][1]]);
        }
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = 0; j < s->prim_channels; j++)
        for (k = s->vq_start_subband[j]; k < s->subband_activity[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, "VQ index: %i\n", s->high_freq_vq[j][k]);
    if(s->lfe){
        av_log(s->avctx, AV_LOG_DEBUG, "LFE samples:\n");
        for (j = lfe_samples; j < lfe_samples * 2; j++)
            av_log(s->avctx, AV_LOG_DEBUG, " %f", s->lfe_data[j]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
#endif

    return 0;
}

static void qmf_32_subbands(DCAContext * s, int chans,
                            float samples_in[32][8], float *samples_out,
                            float scale, float bias)
{
    float *prCoeff;
    int i, j, k;
    float praXin[33], *raXin = &praXin[1];

    float *subband_fir_hist = s->subband_fir_hist[chans];
    float *subband_fir_hist2 = s->subband_fir_noidea[chans];

    int chindex = 0, subindex;

    praXin[0] = 0.0;

    /* Select filter */
    if (!s->multirate_inter)    /* Non-perfect reconstruction */
        prCoeff = (float *) fir_32bands_nonperfect;
    else                        /* Perfect reconstruction */
        prCoeff = (float *) fir_32bands_perfect;

    /* Reconstructed channel sample index */
    for (subindex = 0; subindex < 8; subindex++) {
        float t1, t2, sum[16], diff[16];

        /* Load in one sample from each subband and clear inactive subbands */
        for (i = 0; i < s->subband_activity[chans]; i++)
            raXin[i] = samples_in[i][subindex];
        for (; i < 32; i++)
            raXin[i] = 0.0;

        /* Multiply by cosine modulation coefficients and
         * create temporary arrays SUM and DIFF */
        for (j = 0, k = 0; k < 16; k++) {
            t1 = 0.0;
            t2 = 0.0;
            for (i = 0; i < 16; i++, j++){
                t1 += (raXin[2 * i] + raXin[2 * i + 1]) * cos_mod[j];
                t2 += (raXin[2 * i] + raXin[2 * i - 1]) * cos_mod[j + 256];
            }
            sum[k] = t1 + t2;
            diff[k] = t1 - t2;
        }

        j = 512;
        /* Store history */
        for (k = 0; k < 16; k++)
            subband_fir_hist[k] = cos_mod[j++] * sum[k];
        for (k = 0; k < 16; k++)
            subband_fir_hist[32-k-1] = cos_mod[j++] * diff[k];

        /* Multiply by filter coefficients */
        for (k = 31, i = 0; i < 32; i++, k--)
            for (j = 0; j < 512; j += 64){
                subband_fir_hist2[i]    += prCoeff[i+j]  * ( subband_fir_hist[i+j] - subband_fir_hist[j+k]);
                subband_fir_hist2[i+32] += prCoeff[i+j+32]*(-subband_fir_hist[i+j] - subband_fir_hist[j+k]);
            }

        /* Create 32 PCM output samples */
        for (i = 0; i < 32; i++)
            samples_out[chindex++] = subband_fir_hist2[i] * scale + bias;

        /* Update working arrays */
        memmove(&subband_fir_hist[32], &subband_fir_hist[0], (512 - 32) * sizeof(float));
        memmove(&subband_fir_hist2[0], &subband_fir_hist2[32], 32 * sizeof(float));
        memset(&subband_fir_hist2[32], 0, 32 * sizeof(float));
    }
}

static void lfe_interpolation_fir(int decimation_select,
                                  int num_deci_sample, float *samples_in,
                                  float *samples_out, float scale,
                                  float bias)
{
    /* samples_in: An array holding decimated samples.
     *   Samples in current subframe starts from samples_in[0],
     *   while samples_in[-1], samples_in[-2], ..., stores samples
     *   from last subframe as history.
     *
     * samples_out: An array holding interpolated samples
     */

    int decifactor, k, j;
    const float *prCoeff;

    int interp_index = 0;       /* Index to the interpolated samples */
    int deciindex;

    /* Select decimation filter */
    if (decimation_select == 1) {
        decifactor = 128;
        prCoeff = lfe_fir_128;
    } else {
        decifactor = 64;
        prCoeff = lfe_fir_64;
    }
    /* Interpolation */
    for (deciindex = 0; deciindex < num_deci_sample; deciindex++) {
        /* One decimated sample generates decifactor interpolated ones */
        for (k = 0; k < decifactor; k++) {
            float rTmp = 0.0;
            //FIXME the coeffs are symetric, fix that
            for (j = 0; j < 512 / decifactor; j++)
                rTmp += samples_in[deciindex - j] * prCoeff[k + j * decifactor];
            samples_out[interp_index++] = rTmp / scale + bias;
        }
    }
}

/* downmixing routines */
#define MIX_REAR1(samples, si1) \
     samples[i] += samples[si1]; \
     samples[i+256] += samples[si1];

#define MIX_REAR2(samples, si1, si2) \
     samples[i] += samples[si1]; \
     samples[i+256] += samples[si2];

#define MIX_FRONT3(samples) \
    t = samples[i]; \
    samples[i] += samples[i+256]; \
    samples[i+256] = samples[i+512] + t;

#define DOWNMIX_TO_STEREO(op1, op2) \
    for(i = 0; i < 256; i++){ \
        op1 \
        op2 \
    }

static void dca_downmix(float *samples, int srcfmt)
{
    int i;
    float t;

    switch (srcfmt) {
    case DCA_MONO:
    case DCA_CHANNEL:
    case DCA_STEREO_TOTAL:
    case DCA_STEREO_SUMDIFF:
    case DCA_4F2R:
        av_log(NULL, 0, "Not implemented!\n");
        break;
    case DCA_STEREO:
        break;
    case DCA_3F:
        DOWNMIX_TO_STEREO(MIX_FRONT3(samples),);
        break;
    case DCA_2F1R:
        DOWNMIX_TO_STEREO(MIX_REAR1(samples, i + 512),);
        break;
    case DCA_3F1R:
        DOWNMIX_TO_STEREO(MIX_FRONT3(samples),
                          MIX_REAR1(samples, i + 768));
        break;
    case DCA_2F2R:
        DOWNMIX_TO_STEREO(MIX_REAR2(samples, i + 512, i + 768),);
        break;
    case DCA_3F2R:
        DOWNMIX_TO_STEREO(MIX_FRONT3(samples),
                          MIX_REAR2(samples, i + 768, i + 1024));
        break;
    }
}


/* Very compact version of the block code decoder that does not use table
 * look-up but is slightly slower */
static int decode_blockcode(int code, int levels, int *values)
{
    int i;
    int offset = (levels - 1) >> 1;

    for (i = 0; i < 4; i++) {
        values[i] = (code % levels) - offset;
        code /= levels;
    }

    if (code == 0)
        return 0;
    else {
        av_log(NULL, AV_LOG_ERROR, "ERROR: block code look-up failed\n");
        return -1;
    }
}

static const uint8_t abits_sizes[7] = { 7, 10, 12, 13, 15, 17, 19 };
static const uint8_t abits_levels[7] = { 3, 5, 7, 9, 13, 17, 25 };

static int dca_subsubframe(DCAContext * s)
{
    int k, l;
    int subsubframe = s->current_subsubframe;

    float *quant_step_table;

    /* FIXME */
    float subband_samples[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS][8];

    /*
     * Audio data
     */

    /* Select quantization step size table */
    if (s->bit_rate == 0x1f)
        quant_step_table = (float *) lossless_quant_d;
    else
        quant_step_table = (float *) lossy_quant_d;

    for (k = 0; k < s->prim_channels; k++) {
        for (l = 0; l < s->vq_start_subband[k]; l++) {
            int m;

            /* Select the mid-tread linear quantizer */
            int abits = s->bitalloc[k][l];

            float quant_step_size = quant_step_table[abits];
            float rscale;

            /*
             * Determine quantization index code book and its type
             */

            /* Select quantization index code book */
            int sel = s->quant_index_huffman[k][abits];

            /*
             * Extract bits from the bit stream
             */
            if(!abits){
                memset(subband_samples[k][l], 0, 8 * sizeof(subband_samples[0][0][0]));
            }else if(abits >= 11 || !dca_smpl_bitalloc[abits].vlc[sel].table){
                if(abits <= 7){
                    /* Block code */
                    int block_code1, block_code2, size, levels;
                    int block[8];

                    size = abits_sizes[abits-1];
                    levels = abits_levels[abits-1];

                    block_code1 = get_bits(&s->gb, size);
                    /* FIXME Should test return value */
                    decode_blockcode(block_code1, levels, block);
                    block_code2 = get_bits(&s->gb, size);
                    decode_blockcode(block_code2, levels, &block[4]);
                    for (m = 0; m < 8; m++)
                        subband_samples[k][l][m] = block[m];
                }else{
                    /* no coding */
                    for (m = 0; m < 8; m++)
                        subband_samples[k][l][m] = get_sbits(&s->gb, abits - 3);
                }
            }else{
                /* Huffman coded */
                for (m = 0; m < 8; m++)
                    subband_samples[k][l][m] = get_bitalloc(&s->gb, &dca_smpl_bitalloc[abits], sel);
            }

            /* Deal with transients */
            if (s->transition_mode[k][l] &&
                subsubframe >= s->transition_mode[k][l])
                rscale = quant_step_size * s->scale_factor[k][l][1];
            else
                rscale = quant_step_size * s->scale_factor[k][l][0];

            rscale *= s->scalefactor_adj[k][sel];

            for (m = 0; m < 8; m++)
                subband_samples[k][l][m] *= rscale;

            /*
             * Inverse ADPCM if in prediction mode
             */
            if (s->prediction_mode[k][l]) {
                int n;
                for (m = 0; m < 8; m++) {
                    for (n = 1; n <= 4; n++)
                        if (m >= n)
                            subband_samples[k][l][m] +=
                                (adpcm_vb[s->prediction_vq[k][l]][n - 1] *
                                 subband_samples[k][l][m - n] / 8192);
                        else if (s->predictor_history)
                            subband_samples[k][l][m] +=
                                (adpcm_vb[s->prediction_vq[k][l]][n - 1] *
                                 s->subband_samples_hist[k][l][m - n +
                                                               4] / 8192);
                }
            }
        }

        /*
         * Decode VQ encoded high frequencies
         */
        for (l = s->vq_start_subband[k]; l < s->subband_activity[k]; l++) {
            /* 1 vector -> 32 samples but we only need the 8 samples
             * for this subsubframe. */
            int m;

            if (!s->debug_flag & 0x01) {
                av_log(s->avctx, AV_LOG_DEBUG, "Stream with high frequencies VQ coding\n");
                s->debug_flag |= 0x01;
            }

            for (m = 0; m < 8; m++) {
                subband_samples[k][l][m] =
                    high_freq_vq[s->high_freq_vq[k][l]][subsubframe * 8 +
                                                        m]
                    * (float) s->scale_factor[k][l][0] / 16.0;
            }
        }
    }

    /* Check for DSYNC after subsubframe */
    if (s->aspf || subsubframe == s->subsubframes - 1) {
        if (0xFFFF == get_bits(&s->gb, 16)) {   /* 0xFFFF */
#ifdef TRACE
            av_log(s->avctx, AV_LOG_DEBUG, "Got subframe DSYNC\n");
#endif
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "Didn't get subframe DSYNC\n");
        }
    }

    /* Backup predictor history for adpcm */
    for (k = 0; k < s->prim_channels; k++)
        for (l = 0; l < s->vq_start_subband[k]; l++)
            memcpy(s->subband_samples_hist[k][l], &subband_samples[k][l][4],
                        4 * sizeof(subband_samples[0][0][0]));

    /* 32 subbands QMF */
    for (k = 0; k < s->prim_channels; k++) {
/*        static float pcm_to_double[8] =
            {32768.0, 32768.0, 524288.0, 524288.0, 0, 8388608.0, 8388608.0};*/
         qmf_32_subbands(s, k, subband_samples[k], &s->samples[256 * k],
                            2.0 / 3 /*pcm_to_double[s->source_pcm_res] */ ,
                            0 /*s->bias */ );
    }

    /* Down mixing */

    if (s->prim_channels > dca_channels[s->output & DCA_CHANNEL_MASK]) {
        dca_downmix(s->samples, s->amode);
    }

    /* Generate LFE samples for this subsubframe FIXME!!! */
    if (s->output & DCA_LFE) {
        int lfe_samples = 2 * s->lfe * s->subsubframes;
        int i_channels = dca_channels[s->output & DCA_CHANNEL_MASK];

        lfe_interpolation_fir(s->lfe, 2 * s->lfe,
                              s->lfe_data + lfe_samples +
                              2 * s->lfe * subsubframe,
                              &s->samples[256 * i_channels],
                              8388608.0, s->bias);
        /* Outputs 20bits pcm samples */
    }

    return 0;
}


static int dca_subframe_footer(DCAContext * s)
{
    int aux_data_count = 0, i;
    int lfe_samples;

    /*
     * Unpack optional information
     */

    if (s->timestamp)
        get_bits(&s->gb, 32);

    if (s->aux_data)
        aux_data_count = get_bits(&s->gb, 6);

    for (i = 0; i < aux_data_count; i++)
        get_bits(&s->gb, 8);

    if (s->crc_present && (s->downmix || s->dynrange))
        get_bits(&s->gb, 16);

    lfe_samples = 2 * s->lfe * s->subsubframes;
    for (i = 0; i < lfe_samples; i++) {
        s->lfe_data[i] = s->lfe_data[i + lfe_samples];
    }

    return 0;
}

/**
 * Decode a dca frame block
 *
 * @param s     pointer to the DCAContext
 */

static int dca_decode_block(DCAContext * s)
{

    /* Sanity check */
    if (s->current_subframe >= s->subframes) {
        av_log(s->avctx, AV_LOG_DEBUG, "check failed: %i>%i",
               s->current_subframe, s->subframes);
        return -1;
    }

    if (!s->current_subsubframe) {
#ifdef TRACE
        av_log(s->avctx, AV_LOG_DEBUG, "DSYNC dca_subframe_header\n");
#endif
        /* Read subframe header */
        if (dca_subframe_header(s))
            return -1;
    }

    /* Read subsubframe */
#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "DSYNC dca_subsubframe\n");
#endif
    if (dca_subsubframe(s))
        return -1;

    /* Update state */
    s->current_subsubframe++;
    if (s->current_subsubframe >= s->subsubframes) {
        s->current_subsubframe = 0;
        s->current_subframe++;
    }
    if (s->current_subframe >= s->subframes) {
#ifdef TRACE
        av_log(s->avctx, AV_LOG_DEBUG, "DSYNC dca_subframe_footer\n");
#endif
        /* Read subframe footer */
        if (dca_subframe_footer(s))
            return -1;
    }

    return 0;
}

/**
 * Convert bitstream to one representation based on sync marker
 */
static int dca_convert_bitstream(uint8_t * src, int src_size, uint8_t * dst,
                          int max_size)
{
    uint32_t mrk;
    int i, tmp;
    uint16_t *ssrc = (uint16_t *) src, *sdst = (uint16_t *) dst;
    PutBitContext pb;

    mrk = AV_RB32(src);
    switch (mrk) {
    case DCA_MARKER_RAW_BE:
        memcpy(dst, src, FFMIN(src_size, max_size));
        return FFMIN(src_size, max_size);
    case DCA_MARKER_RAW_LE:
        for (i = 0; i < (FFMIN(src_size, max_size) + 1) >> 1; i++)
            *sdst++ = bswap_16(*ssrc++);
        return FFMIN(src_size, max_size);
    case DCA_MARKER_14B_BE:
    case DCA_MARKER_14B_LE:
        init_put_bits(&pb, dst, max_size);
        for (i = 0; i < (src_size + 1) >> 1; i++, src += 2) {
            tmp = ((mrk == DCA_MARKER_14B_BE) ? AV_RB16(src) : AV_RL16(src)) & 0x3FFF;
            put_bits(&pb, 14, tmp);
        }
        flush_put_bits(&pb);
        return (put_bits_count(&pb) + 7) >> 3;
    default:
        return -1;
    }
}

/**
 * Main frame decoding function
 * FIXME add arguments
 */
static int dca_decode_frame(AVCodecContext * avctx,
                            void *data, int *data_size,
                            uint8_t * buf, int buf_size)
{

    int i, j, k;
    int16_t *samples = data;
    DCAContext *s = avctx->priv_data;
    int channels;


    s->dca_buffer_size = dca_convert_bitstream(buf, buf_size, s->dca_buffer, DCA_MAX_FRAME_SIZE);
    if (s->dca_buffer_size == -1) {
        av_log(avctx, AV_LOG_ERROR, "Not a DCA frame\n");
        return -1;
    }

    init_get_bits(&s->gb, s->dca_buffer, s->dca_buffer_size * 8);
    if (dca_parse_frame_header(s) < 0) {
        //seems like the frame is corrupt, try with the next one
        return buf_size;
    }
    //set AVCodec values with parsed data
    avctx->sample_rate = s->sample_rate;
    avctx->channels = 2; //FIXME
    avctx->bit_rate = s->bit_rate;

    channels = dca_channels[s->output];
    if(*data_size < (s->sample_blocks / 8) * 256 * sizeof(int16_t) * channels)
        return -1;
    *data_size = 0;
    for (i = 0; i < (s->sample_blocks / 8); i++) {
        dca_decode_block(s);
        s->dsp.float_to_int16(s->tsamples, s->samples, 256 * channels);
        /* interleave samples */
        for (j = 0; j < 256; j++) {
            for (k = 0; k < channels; k++)
                samples[k] = s->tsamples[j + k * 256];
            samples += channels;
        }
        *data_size += 256 * sizeof(int16_t) * channels;
    }

    return buf_size;
}



/**
 * Build the cosine modulation tables for the QMF
 *
 * @param s     pointer to the DCAContext
 */

static void pre_calc_cosmod(DCAContext * s)
{
    int i, j, k;
    static int cosmod_inited = 0;

    if(cosmod_inited) return;
    for (j = 0, k = 0; k < 16; k++)
        for (i = 0; i < 16; i++)
            cos_mod[j++] = cos((2 * i + 1) * (2 * k + 1) * M_PI / 64);

    for (k = 0; k < 16; k++)
        for (i = 0; i < 16; i++)
            cos_mod[j++] = cos((i) * (2 * k + 1) * M_PI / 32);

    for (k = 0; k < 16; k++)
        cos_mod[j++] = 0.25 / (2 * cos((2 * k + 1) * M_PI / 128));

    for (k = 0; k < 16; k++)
        cos_mod[j++] = -0.25 / (2.0 * sin((2 * k + 1) * M_PI / 128));

    cosmod_inited = 1;
}


/**
 * DCA initialization
 *
 * @param avctx     pointer to the AVCodecContext
 */

static int dca_decode_init(AVCodecContext * avctx)
{
    DCAContext *s = avctx->priv_data;

    s->avctx = avctx;
    dca_init_vlcs();
    pre_calc_cosmod(s);

    dsputil_init(&s->dsp, avctx);
    return 0;
}


AVCodec dca_decoder = {
    .name = "dca",
    .type = CODEC_TYPE_AUDIO,
    .id = CODEC_ID_DTS,
    .priv_data_size = sizeof(DCAContext),
    .init = dca_decode_init,
    .decode = dca_decode_frame,
};

#ifdef CONFIG_DCA_PARSER

typedef struct DCAParseContext {
    ParseContext pc;
    uint32_t lastmarker;
} DCAParseContext;

#define IS_MARKER(state, i, buf, buf_size) \
 ((state == DCA_MARKER_14B_LE && (i < buf_size-2) && (buf[i+1] & 0xF0) == 0xF0 && buf[i+2] == 0x07) \
 || (state == DCA_MARKER_14B_BE && (i < buf_size-2) && buf[i+1] == 0x07 && (buf[i+2] & 0xF0) == 0xF0) \
 || state == DCA_MARKER_RAW_LE || state == DCA_MARKER_RAW_BE)

/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int dca_find_frame_end(DCAParseContext * pc1, const uint8_t * buf,
                              int buf_size)
{
    int start_found, i;
    uint32_t state;
    ParseContext *pc = &pc1->pc;

    start_found = pc->frame_start_found;
    state = pc->state;

    i = 0;
    if (!start_found) {
        for (i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (IS_MARKER(state, i, buf, buf_size)) {
                if (pc1->lastmarker && state == pc1->lastmarker) {
                    start_found = 1;
                    break;
                } else if (!pc1->lastmarker) {
                    start_found = 1;
                    pc1->lastmarker = state;
                    break;
                }
            }
        }
    }
    if (start_found) {
        for (; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (state == pc1->lastmarker && IS_MARKER(state, i, buf, buf_size)) {
                pc->frame_start_found = 0;
                pc->state = -1;
                return i - 3;
            }
        }
    }
    pc->frame_start_found = start_found;
    pc->state = state;
    return END_NOT_FOUND;
}

static int dca_parse_init(AVCodecParserContext * s)
{
    DCAParseContext *pc1 = s->priv_data;

    pc1->lastmarker = 0;
    return 0;
}

static int dca_parse(AVCodecParserContext * s,
                     AVCodecContext * avctx,
                     uint8_t ** poutbuf, int *poutbuf_size,
                     const uint8_t * buf, int buf_size)
{
    DCAParseContext *pc1 = s->priv_data;
    ParseContext *pc = &pc1->pc;
    int next;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = dca_find_frame_end(pc1, buf, buf_size);

        if (ff_combine_frame(pc, next, (uint8_t **) & buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }
    *poutbuf = (uint8_t *) buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser dca_parser = {
    {CODEC_ID_DTS},
    sizeof(DCAParseContext),
    dca_parse_init,
    dca_parse,
    ff_parse_close,
};
#endif /* CONFIG_DCA_PARSER */
