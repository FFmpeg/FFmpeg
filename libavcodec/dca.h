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

#ifndef AVCODEC_DCA_H
#define AVCODEC_DCA_H

#include <stdint.h>

#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"

#include "avcodec.h"
#include "dcadsp.h"
#include "fmtconvert.h"
#include "get_bits.h"

#define DCA_PRIM_CHANNELS_MAX  (7)
#define DCA_ABITS_MAX         (32)      /* Should be 28 */
#define DCA_SUBSUBFRAMES_MAX   (4)
#define DCA_SUBFRAMES_MAX     (16)
#define DCA_BLOCKS_MAX        (16)
#define DCA_LFE_MAX            (3)
#define DCA_CHSETS_MAX         (4)
#define DCA_CHSET_CHANS_MAX    (8)

#define DCA_PRIM_CHANNELS_MAX  (7)
#define DCA_ABITS_MAX         (32)      /* Should be 28 */
#define DCA_SUBSUBFRAMES_MAX   (4)
#define DCA_SUBFRAMES_MAX     (16)
#define DCA_BLOCKS_MAX        (16)
#define DCA_LFE_MAX            (3)
#define DCA_XLL_FBANDS_MAX     (4)
#define DCA_XLL_SEGMENTS_MAX  (16)
#define DCA_XLL_CHSETS_MAX    (16)
#define DCA_XLL_CHANNELS_MAX  (16)
#define DCA_XLL_AORDER_MAX    (15)

/* Arbitrary limit; not sure what the maximum really is, but much larger. */
#define DCA_XLL_DMIX_NCOEFFS_MAX (18)

#define DCA_MAX_FRAME_SIZE       16384
#define DCA_MAX_EXSS_HEADER_SIZE  4096

#define DCA_BUFFER_PADDING_SIZE   1024

enum DCAExtensionMask {
    DCA_EXT_CORE       = 0x001, ///< core in core substream
    DCA_EXT_XXCH       = 0x002, ///< XXCh channels extension in core substream
    DCA_EXT_X96        = 0x004, ///< 96/24 extension in core substream
    DCA_EXT_XCH        = 0x008, ///< XCh channel extension in core substream
    DCA_EXT_EXSS_CORE  = 0x010, ///< core in ExSS (extension substream)
    DCA_EXT_EXSS_XBR   = 0x020, ///< extended bitrate extension in ExSS
    DCA_EXT_EXSS_XXCH  = 0x040, ///< XXCh channels extension in ExSS
    DCA_EXT_EXSS_X96   = 0x080, ///< 96/24 extension in ExSS
    DCA_EXT_EXSS_LBR   = 0x100, ///< low bitrate component in ExSS
    DCA_EXT_EXSS_XLL   = 0x200, ///< lossless extension in ExSS
};

typedef struct XllChSetSubHeader {
    int channels;               ///< number of channels in channel set, at most 16
    int residual_encode;        ///< residual channel encoding
    int bit_resolution;         ///< input sample bit-width
    int bit_width;              ///< original input sample bit-width
    int sampling_frequency;     ///< sampling frequency
    int samp_freq_interp;       ///< sampling frequency interpolation multiplier
    int replacement_set;        ///< replacement channel set group
    int active_replace_set;     ///< current channel set is active channel set
    int primary_ch_set;
    int downmix_coeff_code_embedded;
    int downmix_embedded;
    int downmix_type;
    int hier_chset;             ///< hierarchical channel set
    int downmix_ncoeffs;
    int downmix_coeffs[DCA_XLL_DMIX_NCOEFFS_MAX];
    int ch_mask_enabled;
    int ch_mask;
    int mapping_coeffs_present;
    int num_freq_bands;

    /* m_nOrigChanOrder */
    uint8_t orig_chan_order[DCA_XLL_FBANDS_MAX][DCA_XLL_CHANNELS_MAX];
    uint8_t orig_chan_order_inv[DCA_XLL_FBANDS_MAX][DCA_XLL_CHANNELS_MAX];
    /* Coefficients for channel pairs (at most 8), m_anPWChPairsCoeffs */
    int8_t pw_ch_pairs_coeffs[DCA_XLL_FBANDS_MAX][DCA_XLL_CHANNELS_MAX/2];
    /* m_nCurrHighestLPCOrder */
    uint8_t adapt_order_max[DCA_XLL_FBANDS_MAX];
    /* m_pnAdaptPredOrder */
    uint8_t adapt_order[DCA_XLL_FBANDS_MAX][DCA_XLL_CHANNELS_MAX];
    /* m_pnFixedPredOrder */
    uint8_t fixed_order[DCA_XLL_FBANDS_MAX][DCA_XLL_CHANNELS_MAX];
    /* m_pnLPCReflCoeffsQInd, unsigned version */
    uint8_t lpc_refl_coeffs_q_ind[DCA_XLL_FBANDS_MAX]
                                 [DCA_XLL_CHANNELS_MAX][DCA_XLL_AORDER_MAX];

    int lsb_fsize[DCA_XLL_FBANDS_MAX];
    int8_t scalable_lsbs[DCA_XLL_FBANDS_MAX][DCA_XLL_CHANNELS_MAX];
    int8_t bit_width_adj_per_ch[DCA_XLL_FBANDS_MAX][DCA_XLL_CHANNELS_MAX];
} XllChSetSubHeader;

typedef struct XllNavi {
    GetBitContext gb;  // Context for parsing the data segments
    unsigned band_size[DCA_XLL_FBANDS_MAX];
    unsigned segment_size[DCA_XLL_FBANDS_MAX][DCA_XLL_SEGMENTS_MAX];
    unsigned chset_size[DCA_XLL_FBANDS_MAX][DCA_XLL_SEGMENTS_MAX][DCA_XLL_CHSETS_MAX];
} XllNavi;

typedef struct QMF64_table {
    float dct4_coeff[32][32];
    float dct2_coeff[32][32];
    float rcos[32];
    float rsin[32];
} QMF64_table;

/* Primary audio coding header */
typedef struct DCAAudioHeader {
    int subband_activity[DCA_PRIM_CHANNELS_MAX];    ///< subband activity count
    int vq_start_subband[DCA_PRIM_CHANNELS_MAX];    ///< high frequency vq start subband
    int joint_intensity[DCA_PRIM_CHANNELS_MAX];     ///< joint intensity coding index
    int transient_huffman[DCA_PRIM_CHANNELS_MAX];   ///< transient mode code book
    int scalefactor_huffman[DCA_PRIM_CHANNELS_MAX]; ///< scale factor code book
    int bitalloc_huffman[DCA_PRIM_CHANNELS_MAX];    ///< bit allocation quantizer select
    int quant_index_huffman[DCA_PRIM_CHANNELS_MAX][DCA_ABITS_MAX];  ///< quantization index codebook select
    uint32_t scalefactor_adj[DCA_PRIM_CHANNELS_MAX][DCA_ABITS_MAX]; ///< scale factor adjustment

    int subframes;              ///< number of subframes
    int total_channels;         ///< number of channels including extensions
    int prim_channels;          ///< number of primary audio channels
} DCAAudioHeader;

typedef struct DCAChan {
    DECLARE_ALIGNED(32, int32_t, subband_samples)[DCA_BLOCKS_MAX][DCA_SUBBANDS][SAMPLES_PER_SUBBAND];

    /* Subband samples history (for ADPCM) */
    DECLARE_ALIGNED(32, int32_t, subband_samples_hist)[DCA_SUBBANDS][4];
    int hist_index;

    /* Half size is sufficient for core decoding, but for 96 kHz data
     * we need QMF with 64 subbands and 1024 samples. */
    DECLARE_ALIGNED(32, float, subband_fir_hist)[1024];
    DECLARE_ALIGNED(32, float, subband_fir_noidea)[64];

    /* Primary audio coding side information */
    int prediction_mode[DCA_SUBBANDS];    ///< prediction mode (ADPCM used or not)
    int prediction_vq[DCA_SUBBANDS];      ///< prediction VQ coefs
    int bitalloc[DCA_SUBBANDS];           ///< bit allocation index
    int transition_mode[DCA_SUBBANDS];    ///< transition mode (transients)
    int32_t scale_factor[DCA_SUBBANDS][2];///< scale factors (2 if transient)
    int joint_huff;                       ///< joint subband scale factors codebook
    int joint_scale_factor[DCA_SUBBANDS]; ///< joint subband scale factors

    int32_t  high_freq_vq[DCA_SUBBANDS];  ///< VQ encoded high frequency subbands
} DCAChan;


typedef struct DCAContext {
    const AVClass *class;       ///< class for AVOptions
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
    int bit_rate_index;         ///< transmission bit rate index

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
    DCAAudioHeader audio_header;

    /* Primary audio coding side information */
    int subsubframes[DCA_SUBFRAMES_MAX];                         ///< number of subsubframes
    int partial_samples[DCA_SUBFRAMES_MAX];                      ///< partial subsubframe samples count
    float downmix_coef[DCA_PRIM_CHANNELS_MAX + 1][2];            ///< stereo downmix coefficients
    int dynrange_coef;                                           ///< dynamic range coefficient

    /* Core substream's embedded downmix coefficients (cf. ETSI TS 102 114 V1.4.1)
     * Input:  primary audio channels (incl. LFE if present)
     * Output: downmix audio channels (up to 4, no LFE) */
    uint8_t  core_downmix;                                       ///< embedded downmix coefficients available
    uint8_t  core_downmix_amode;                                 ///< audio channel arrangement of embedded downmix
    uint16_t core_downmix_codes[DCA_PRIM_CHANNELS_MAX + 1][4];   ///< embedded downmix coefficients (9-bit codes)


    float lfe_data[2 * DCA_LFE_MAX * (DCA_BLOCKS_MAX + 4)];      ///< Low frequency effect data
    int lfe_scale_factor;

    /* Subband samples history (for ADPCM) */
    DECLARE_ALIGNED(32, float, raXin)[32];

    DCAChan dca_chan[DCA_PRIM_CHANNELS_MAX];

    int output;                 ///< type of output

    float *samples_chanptr[DCA_PRIM_CHANNELS_MAX + 1];
    float *extra_channels[DCA_PRIM_CHANNELS_MAX + 1];
    uint8_t *extra_channels_buffer;
    unsigned int extra_channels_buffer_size;

    uint8_t dca_buffer[DCA_MAX_FRAME_SIZE + DCA_MAX_EXSS_HEADER_SIZE + DCA_BUFFER_PADDING_SIZE];
    int dca_buffer_size;        ///< how much data is in the dca_buffer

    const int8_t *channel_order_tab;  ///< channel reordering table, lfe and non lfe
    GetBitContext gb;
    /* Current position in DCA frame */
    int current_subframe;
    int current_subsubframe;

    int core_ext_mask;          ///< present extensions in the core substream
    int exss_ext_mask;          ///< Non-core extensions

    /* XCh extension information */
    int xch_present;            ///< XCh extension present and valid
    int xch_base_channel;       ///< index of first (only) channel containing XCH data
    int xch_disable;            ///< whether the XCh extension should be decoded or not

    /* XXCH extension information */
    int xxch_chset;
    int xxch_nbits_spk_mask;
    uint32_t xxch_core_spkmask;
    uint32_t xxch_spk_masks[4]; /* speaker masks, last element is core mask */
    int xxch_chset_nch[4];
    float xxch_dmix_sf[DCA_CHSETS_MAX];

    uint32_t xxch_dmix_embedded;  /* lower layer has mix pre-embedded, per chset */
    float xxch_dmix_coeff[DCA_PRIM_CHANNELS_MAX][32]; /* worst case sizing */

    int8_t xxch_order_tab[32];
    int8_t lfe_index;

    /* XLL extension information */
    int xll_disable;
    int xll_nch_sets;           ///< number of channel sets per frame
    int xll_channels;           ///< total number of channels (in all channel sets)
    int xll_residual_channels;  ///< number of residual channels
    int xll_segments;           ///< number of segments per frame
    int xll_log_smpl_in_seg;    ///< supposedly this is "nBits4SamplLoci"
    int xll_smpl_in_seg;        ///< samples in segment per one frequency band for the first channel set
    int xll_bits4seg_size;      ///< number of bits used to read segment size
    int xll_banddata_crc;       ///< presence of CRC16 within each frequency band
    int xll_scalable_lsb;
    int xll_bits4ch_mask;       ///< channel position mask
    int xll_fixed_lsb_width;
    XllChSetSubHeader xll_chsets[DCA_XLL_CHSETS_MAX];
    XllNavi xll_navi;
    int *xll_sample_buf;
    unsigned int xll_sample_buf_size;

    /* ExSS header parser */
    int static_fields;          ///< static fields present
    int mix_metadata;           ///< mixing metadata present
    int num_mix_configs;        ///< number of mix out configurations
    int mix_config_num_ch[4];   ///< number of channels in each mix out configuration

    int profile;
    int one2one_map_chtospkr;

    int debug_flag;             ///< used for suppressing repeated error messages output
    AVFloatDSPContext *fdsp;
    FFTContext imdct;
    SynthFilterContext synth;
    DCADSPContext dcadsp;
    QMF64_table *qmf64_table;
    FmtConvertContext fmt_conv;
} DCAContext;

extern av_export const uint32_t avpriv_dca_sample_rates[16];

/**
 * Convert bitstream to one representation based on sync marker
 */
int avpriv_dca_convert_bitstream(const uint8_t *src, int src_size, uint8_t *dst,
                             int max_size);

int ff_dca_xbr_parse_frame(DCAContext *s);
int ff_dca_xxch_decode_frame(DCAContext *s);

void ff_dca_exss_parse_header(DCAContext *s);

int ff_dca_xll_decode_header(DCAContext *s);
int ff_dca_xll_decode_navi(DCAContext *s, int asset_end);
int ff_dca_xll_decode_audio(DCAContext *s, AVFrame *frame);

#endif /* AVCODEC_DCA_H */
