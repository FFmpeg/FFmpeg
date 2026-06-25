/*
 * AAC encoder
 * Copyright (C) 2008 Konstantin Shishkov
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

#ifndef AVCODEC_AACENC_H
#define AVCODEC_AACENC_H

#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem_internal.h"
#include "libavutil/tx.h"

#include "avcodec.h"
#include "put_bits.h"

#include "aac.h"
#include "aacencdsp.h"
#include "audio_frame_queue.h"
#include "psymodel.h"

#include "lpc.h"

#define CLIP_AVOIDANCE_FACTOR 0.95f

typedef enum AACCoder {
    AAC_CODER_TWOLOOP,
    AAC_CODER_FAST,
    AAC_CODER_NMR,

    AAC_CODER_NB,
}AACCoder;

/**
 * Predictor State
 */
typedef struct PredictorState {
    float cor0;
    float cor1;
    float var0;
    float var1;
    float r0;
    float r1;
    float k1;
    float x_est;
} PredictorState;

typedef struct AACEncOptions {
    int coder;
    int pns;
    int tns;
    int pce;
    int mid_side;
    int intensity_stereo;
    int nmr_speed;          ///< NMR coder speed level: 0 = slowest/best, higher is faster
} AACEncOptions;

/**
 * Individual Channel Stream
 */
typedef struct IndividualChannelStream {
    uint8_t max_sfb;            ///< number of scalefactor bands per group
    enum WindowSequence window_sequence[2];
    uint8_t use_kb_window[2];   ///< If set, use Kaiser-Bessel window, otherwise use a sine window.
    uint8_t group_len[8];
    const uint16_t *swb_offset; ///< table of offsets to the lowest spectral coefficient of a scalefactor band, sfb, for a particular window
    const uint8_t *swb_sizes;   ///< table of scalefactor band sizes for a particular window
    int num_swb;                ///< number of scalefactor window bands
    int num_windows;
    int tns_max_bands;
    uint8_t window_clipping[8]; ///< set if a certain window is near clipping
    float clip_avoidance_factor; ///< set if any window is near clipping to the necessary atennuation factor to avoid it
} IndividualChannelStream;

/**
 * Temporal Noise Shaping
 */
typedef struct TemporalNoiseShaping {
    int present;
    int n_filt[8];
    int length[8][4];
    int direction[8][4];
    int order[8][4];
    int coef_idx[8][4][TNS_MAX_ORDER];
    float coef[8][4][TNS_MAX_ORDER];
} TemporalNoiseShaping;

/**
 * Single Channel Element - used for both SCE and LFE elements.
 */
typedef struct SingleChannelElement {
    IndividualChannelStream ics;
    TemporalNoiseShaping tns;
    Pulse pulse;
    enum BandType band_type[128];                   ///< band types
    enum BandType band_alt[128];                    ///< alternative band type
    int sf_idx[128];                                ///< scalefactor indices
    uint8_t zeroes[128];                            ///< band is not coded
    uint8_t can_pns[128];                           ///< band is allowed to PNS (informative)
    float  is_ener[128];                            ///< Intensity stereo pos
    float pns_ener[128];                            ///< Noise energy values
    DECLARE_ALIGNED(32, float, pcoeffs)[1024];      ///< coefficients for IMDCT, pristine
    DECLARE_ALIGNED(32, float, coeffs)[1024];       ///< coefficients for IMDCT, maybe processed
    DECLARE_ALIGNED(32, float, ret_buf)[2048];      ///< PCM output buffer
    PredictorState predictor_state[MAX_PREDICTORS];
} SingleChannelElement;

/**
 * channel element - generic struct for SCE/CPE/CCE/LFE
 */
typedef struct ChannelElement {
    // CPE specific
    int common_window;        ///< Set if channels share a common 'IndividualChannelStream' in bitstream.
    int     ms_mode;          ///< Signals mid/side stereo flags coding mode
    uint8_t is_mode;          ///< Set if any bands have been encoded using intensity stereo
    uint8_t ms_mask[128];     ///< Set if mid/side stereo is used for each scalefactor window band
    uint8_t is_mask[128];     ///< Set if intensity stereo is used
    // shared
    SingleChannelElement ch[2];
} ChannelElement;

struct AACEncContext;

typedef struct AACCoefficientsEncoder {
    void (*search_for_quantizers)(AVCodecContext *avctx, struct AACEncContext *s,
                                  SingleChannelElement *sce, const float lambda);
    void (*encode_window_bands_info)(struct AACEncContext *s, SingleChannelElement *sce,
                                     int win, int group_len, const float lambda);
    void (*quantize_and_encode_band)(struct AACEncContext *s, PutBitContext *pb, const float *in, float *out, int size,
                                     int scale_idx, int cb, const float lambda, int rtz);
    void (*encode_tns_info)(struct AACEncContext *s, SingleChannelElement *sce);
    void (*apply_tns_filt)(struct AACEncContext *s, SingleChannelElement *sce);
    void (*set_special_band_scalefactors)(struct AACEncContext *s, SingleChannelElement *sce);
    void (*search_for_pns)(struct AACEncContext *s, AVCodecContext *avctx, SingleChannelElement *sce);
    void (*mark_pns)(struct AACEncContext *s, AVCodecContext *avctx, SingleChannelElement *sce);
    void (*search_for_tns)(struct AACEncContext *s, SingleChannelElement *sce);
    void (*search_for_ms)(struct AACEncContext *s, ChannelElement *cpe);
    void (*search_for_is)(struct AACEncContext *s, AVCodecContext *avctx, ChannelElement *cpe);
} AACCoefficientsEncoder;

extern const AACCoefficientsEncoder ff_aac_coders[];

typedef struct AACQuantizeBandCostCacheEntry {
    float rd;
    float energy;
    int bits;
    char cb;
    char rtz;
    uint16_t generation;
} AACQuantizeBandCostCacheEntry;

/** per-band scalefactor candidates above the finest codeable sf (NMR coder) */
#define NMR_NCAND 96

/**
 * NMR coder per-band candidate cost curves (~96 KiB) and rate-control carry-over
 */
typedef struct AACNMRCurves {
    float nd[128][NMR_NCAND];                    ///< dist / threshold per candidate
    int   nb[128][NMR_NCAND];                    ///< spectral bits per candidate
    float lam[16];                               ///< per-channel operating lambda of the previous frame, 0 = none yet
    int   counted[16];                           ///< per-channel bits the trellis accounted for in the last solve
    float side_ema;                              ///< running estimate of real-minus-counted bits per frame
    int   side_inited;                           ///< side_ema holds a measurement

    int64_t rc_frame_num;                        ///< frame the reservoir was last advanced for
    float   lam_rc;                              ///< global-lambda rate control: operating lambda, 0 until bootstrapped
    int     rc_fill;                             ///< virtual bit reservoir fill, + = bits saved vs nominal
    int     frames_since_short;                  ///< long-block frames since the last short run (the "gap"): large = isolated transient
    int     prev_was_short;                      ///< previous frame was a short block (for run-start detection)
    float   run_burst;                           ///< transient bit-burst factor, set at run start and held across the short run
} AACNMRCurves;

typedef struct AACPCEInfo {
    AVChannelLayout layout;
    uint8_t num_ele[4];                          ///< front, side, back, lfe
    uint8_t pairing[3][8];                       ///< front, side, back
    uint8_t index[4][8];                         ///< front, side, back, lfe
    uint8_t config_map[16];                      ///< configs the encoder's channel specific settings
    uint8_t reorder_map[16];                     ///< maps channels from lavc to aac order
} AACPCEInfo;

/**
 * AAC encoder context
 */
typedef struct AACEncContext {
    AVClass *av_class;
    AACEncOptions options;                       ///< encoding options
    PutBitContext pb;
    AVTXContext *mdct1024;                       ///< long (1024 samples) frame transform context
    av_tx_fn mdct1024_fn;
    AVTXContext *mdct128;                        ///< short (128 samples) frame transform context
    av_tx_fn mdct128_fn;
    AVFloatDSPContext *fdsp;
    AACPCEInfo pce;                              ///< PCE data, if needed
    float *planar_samples[16];                   ///< saved preprocessed input

    int profile;                                 ///< copied from avctx
    int needs_pce;                               ///< flag for non-standard layout
    LPCContext lpc;                              ///< used by TNS
    int samplerate_index;                        ///< MPEG-4 samplerate index
    int channels;                                ///< channel count
    int bandwidth;                               ///< coding bandwidth in Hz, fixed at init; the psy model and the coders' band cutoff agree on it
    const uint8_t *reorder_map;                  ///< lavc to aac reorder map
    const uint8_t *chan_map;                     ///< channel configuration map

    ChannelElement *cpe;                         ///< channel elements
    FFPsyContext psy;
    const AACCoefficientsEncoder *coder;
    int cur_channel;                             ///< current channel for coder context
    int random_state;
    float lambda;
    int last_frame_pb_count;                     ///< number of bits for the previous frame
    float lambda_sum;                            ///< sum(lambda), for Qvg reporting
    int lambda_count;                            ///< count(lambda), for Qvg reporting
    /* tool-usage stats, reported at close: per-coded-band for PNS (channel bands),
     * per-coded-pair-band for M/S and I/S (CPE bands) */
    uint64_t stat_ch_bands, stat_pns;            ///< coded channel-bands, of which PNS
    uint64_t stat_cpe_bands, stat_ms, stat_is;   ///< coded CPE pair-bands, of which M/S, I/S
    uint64_t stat_chans, stat_short;             ///< coded channels, of which short-block (transient)
    uint64_t stat_tns_long, stat_tns_short;      ///< TNS-active channels among long / short blocks
    enum RawDataBlockType cur_type;              ///< channel group type cur_channel belongs to

    AudioFrameQueue afq;
    DECLARE_ALIGNED(32, int,   qcoefs)[96];      ///< quantized coefficients
    DECLARE_ALIGNED(32, float, scoefs)[1024];    ///< scaled coefficients

    uint16_t quantize_band_cost_cache_generation;
    AACQuantizeBandCostCacheEntry quantize_band_cost_cache[256][128]; ///< memoization area for quantize_band_cost

    AACEncDSPContext aacdsp;
    AACNMRCurves *nmr;                            ///< NMR coder scratch (NULL unless coder == nmr)

    struct {
        float *samples;
    } buffer;
} AACEncContext;

void ff_quantize_band_cost_cache_init(struct AACEncContext *s);


#endif /* AVCODEC_AACENC_H */
