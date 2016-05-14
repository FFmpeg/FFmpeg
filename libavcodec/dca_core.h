/*
 * Copyright (C) 2016 foo86
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

#ifndef AVCODEC_DCA_CORE_H
#define AVCODEC_DCA_CORE_H

#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/fixed_dsp.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "dca.h"
#include "dca_exss.h"
#include "dcadsp.h"
#include "dcadct.h"
#include "dcahuff.h"
#include "fft.h"
#include "synth_filter.h"

#define DCA_CHANNELS            7
#define DCA_SUBBANDS            32
#define DCA_SUBBANDS_X96        64
#define DCA_SUBFRAMES           16
#define DCA_SUBBAND_SAMPLES     8
#define DCA_PCMBLOCK_SAMPLES    32
#define DCA_ADPCM_COEFFS        4
#define DCA_LFE_HISTORY         8
#define DCA_ABITS_MAX           26

#define DCA_CORE_CHANNELS_MAX       6
#define DCA_DMIX_CHANNELS_MAX       4
#define DCA_XXCH_CHANNELS_MAX       2
#define DCA_EXSS_CHANNELS_MAX       8
#define DCA_EXSS_CHSETS_MAX         4

#define DCA_FILTER_MODE_X96     0x01
#define DCA_FILTER_MODE_FIXED   0x02

typedef struct DCADSPData {
    union {
        struct {
            DECLARE_ALIGNED(32, float, hist1)[1024];
            DECLARE_ALIGNED(32, float, hist2)[64];
        } flt;
        struct {
            DECLARE_ALIGNED(32, int32_t, hist1)[1024];
            DECLARE_ALIGNED(32, int32_t, hist2)[64];
        } fix;
    } u;
    int offset;
} DCADSPData;

typedef struct DCACoreDecoder {
    AVCodecContext  *avctx;
    GetBitContext   gb;

    // Bit stream header
    int     crc_present;        ///< CRC present flag
    int     npcmblocks;         ///< Number of PCM sample blocks
    int     frame_size;         ///< Primary frame byte size
    int     audio_mode;         ///< Audio channel arrangement
    int     sample_rate;        ///< Core audio sampling frequency
    int     bit_rate;           ///< Transmission bit rate
    int     drc_present;        ///< Embedded dynamic range flag
    int     ts_present;         ///< Embedded time stamp flag
    int     aux_present;        ///< Auxiliary data flag
    int     ext_audio_type;     ///< Extension audio descriptor flag
    int     ext_audio_present;  ///< Extended coding flag
    int     sync_ssf;           ///< Audio sync word insertion flag
    int     lfe_present;        ///< Low frequency effects flag
    int     predictor_history;  ///< Predictor history flag switch
    int     filter_perfect;     ///< Multirate interpolator switch
    int     source_pcm_res;     ///< Source PCM resolution
    int     es_format;          ///< Extended surround (ES) mastering flag
    int     sumdiff_front;      ///< Front sum/difference flag
    int     sumdiff_surround;   ///< Surround sum/difference flag

    // Primary audio coding header
    int         nsubframes;     ///< Number of subframes
    int         nchannels;      ///< Number of primary audio channels (incl. extension channels)
    int         ch_mask;        ///< Speaker layout mask (incl. LFE and extension channels)
    int8_t      nsubbands[DCA_CHANNELS];                ///< Subband activity count
    int8_t      subband_vq_start[DCA_CHANNELS];         ///< High frequency VQ start subband
    int8_t      joint_intensity_index[DCA_CHANNELS];    ///< Joint intensity coding index
    int8_t      transition_mode_sel[DCA_CHANNELS];      ///< Transient mode code book
    int8_t      scale_factor_sel[DCA_CHANNELS];         ///< Scale factor code book
    int8_t      bit_allocation_sel[DCA_CHANNELS];       ///< Bit allocation quantizer select
    int8_t      quant_index_sel[DCA_CHANNELS][DCA_CODE_BOOKS];  ///< Quantization index codebook select
    int32_t     scale_factor_adj[DCA_CHANNELS][DCA_CODE_BOOKS]; ///< Scale factor adjustment

    // Primary audio coding side information
    int8_t      nsubsubframes[DCA_SUBFRAMES];   ///< Subsubframe count for each subframe
    int8_t      prediction_mode[DCA_CHANNELS][DCA_SUBBANDS_X96];            ///< Prediction mode
    int16_t     prediction_vq_index[DCA_CHANNELS][DCA_SUBBANDS_X96];        ///< Prediction coefficients VQ address
    int8_t      bit_allocation[DCA_CHANNELS][DCA_SUBBANDS_X96];             ///< Bit allocation index
    int8_t      transition_mode[DCA_SUBFRAMES][DCA_CHANNELS][DCA_SUBBANDS]; ///< Transition mode
    int32_t     scale_factors[DCA_CHANNELS][DCA_SUBBANDS][2];               ///< Scale factors (2x for transients and X96)
    int8_t      joint_scale_sel[DCA_CHANNELS];                              ///< Joint subband codebook select
    int32_t     joint_scale_factors[DCA_CHANNELS][DCA_SUBBANDS_X96];        ///< Scale factors for joint subband coding

    // Auxiliary data
    int     prim_dmix_embedded; ///< Auxiliary dynamic downmix flag
    int     prim_dmix_type;     ///< Auxiliary primary channel downmix type
    int     prim_dmix_coeff[DCA_DMIX_CHANNELS_MAX * DCA_CORE_CHANNELS_MAX]; ///< Dynamic downmix code coefficients

    // Core extensions
    int     ext_audio_mask;     ///< Bit mask of fully decoded core extensions

    // XCH extension data
    int     xch_pos;    ///< Bit position of XCH frame in core substream

    // XXCH extension data
    int     xxch_crc_present;       ///< CRC presence flag for XXCH channel set header
    int     xxch_mask_nbits;        ///< Number of bits for loudspeaker mask
    int     xxch_core_mask;         ///< Core loudspeaker activity mask
    int     xxch_spkr_mask;         ///< Loudspeaker layout mask
    int     xxch_dmix_embedded;     ///< Downmix already performed by encoder
    int     xxch_dmix_scale_inv;    ///< Downmix scale factor
    int     xxch_dmix_mask[DCA_XXCH_CHANNELS_MAX];  ///< Downmix channel mapping mask
    int     xxch_dmix_coeff[DCA_XXCH_CHANNELS_MAX * DCA_CORE_CHANNELS_MAX];     ///< Downmix coefficients
    int     xxch_pos;   ///< Bit position of XXCH frame in core substream

    // X96 extension data
    int     x96_rev_no;         ///< X96 revision number
    int     x96_crc_present;    ///< CRC presence flag for X96 channel set header
    int     x96_nchannels;      ///< Number of primary channels in X96 extension
    int     x96_high_res;       ///< X96 high resolution flag
    int     x96_subband_start;  ///< First encoded subband in X96 extension
    int     x96_rand;           ///< Random seed for generating samples for unallocated X96 subbands
    int     x96_pos;            ///< Bit position of X96 frame in core substream

    // Sample buffers
    unsigned int    x96_subband_size;
    int32_t         *x96_subband_buffer;    ///< X96 subband sample buffer base
    int32_t         *x96_subband_samples[DCA_CHANNELS][DCA_SUBBANDS_X96];   ///< X96 subband samples

    unsigned int    subband_size;
    int32_t         *subband_buffer;    ///< Subband sample buffer base
    int32_t         *subband_samples[DCA_CHANNELS][DCA_SUBBANDS];   ///< Subband samples
    int32_t         *lfe_samples;    ///< Decimated LFE samples

    // DSP contexts
    DCADSPData              dcadsp_data[DCA_CHANNELS];    ///< FIR history buffers
    DCADSPContext           *dcadsp;
    DCADCTContext           dcadct;
    FFTContext              imdct[2];
    SynthFilterContext      synth;
    AVFloatDSPContext       *float_dsp;
    AVFixedDSPContext       *fixed_dsp;

    // PCM output data
    unsigned int    output_size;
    void            *output_buffer;                         ///< PCM output buffer base
    int32_t         *output_samples[DCA_SPEAKER_COUNT];     ///< PCM output for fixed point mode
    int32_t         output_history_lfe_fixed;               ///< LFE PCM history for X96 filter
    float           output_history_lfe_float;               ///< LFE PCM history for X96 filter

    int     ch_remap[DCA_SPEAKER_COUNT];   ///< Channel to speaker map
    int     request_mask;   ///< Requested channel layout (for stereo downmix)

    int     npcmsamples;    ///< Number of PCM samples per channel
    int     output_rate;    ///< Output sample rate (1x or 2x header rate)

    int     filter_mode;    ///< Previous filtering mode for detecting changes
} DCACoreDecoder;

static inline int ff_dca_core_map_spkr(DCACoreDecoder *core, int spkr)
{
    if (core->ch_mask & (1U << spkr))
        return spkr;
    if (spkr == DCA_SPEAKER_Lss && (core->ch_mask & DCA_SPEAKER_MASK_Ls))
        return DCA_SPEAKER_Ls;
    if (spkr == DCA_SPEAKER_Rss && (core->ch_mask & DCA_SPEAKER_MASK_Rs))
        return DCA_SPEAKER_Rs;
    return -1;
}

int ff_dca_core_parse(DCACoreDecoder *s, uint8_t *data, int size);
int ff_dca_core_parse_exss(DCACoreDecoder *s, uint8_t *data, DCAExssAsset *asset);
int ff_dca_core_filter_fixed(DCACoreDecoder *s, int x96_synth);
int ff_dca_core_filter_frame(DCACoreDecoder *s, AVFrame *frame);
av_cold void ff_dca_core_flush(DCACoreDecoder *s);
av_cold int ff_dca_core_init(DCACoreDecoder *s);
av_cold void ff_dca_core_close(DCACoreDecoder *s);

#endif
