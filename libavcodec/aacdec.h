/*
 * AAC decoder definitions and structures
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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
 * AAC decoder definitions and structures
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef AVCODEC_AACDEC_H
#define AVCODEC_AACDEC_H

#include "libavutil/float_dsp.h"
#include "libavutil/fixed_dsp.h"
#include "libavutil/mem_internal.h"
#include "libavutil/tx.h"

#include "aac.h"
#include "mpeg4audio.h"

/**
 * Output configuration status
 */
enum OCStatus {
    OC_NONE,        ///< Output unconfigured
    OC_TRIAL_PCE,   ///< Output configuration under trial specified by an inband PCE
    OC_TRIAL_FRAME, ///< Output configuration under trial specified by a frame header
    OC_GLOBAL_HDR,  ///< Output configuration set in a global header but not yet locked
    OC_LOCKED,      ///< Output configuration locked in place
};

enum AACOutputChannelOrder {
    CHANNEL_ORDER_DEFAULT,
    CHANNEL_ORDER_CODED,
};

typedef struct OutputConfiguration {
    MPEG4AudioConfig m4ac;
    uint8_t layout_map[MAX_ELEM_ID*4][3];
    int layout_map_tags;
    AVChannelLayout ch_layout;
    enum OCStatus status;
} OutputConfiguration;

/**
 * Dynamic Range Control - decoded from the bitstream but not processed further.
 */
typedef struct DynamicRangeControl {
    int pce_instance_tag;                           ///< Indicates with which program the DRC info is associated.
    int dyn_rng_sgn[17];                            ///< DRC sign information; 0 - positive, 1 - negative
    int dyn_rng_ctl[17];                            ///< DRC magnitude information
    int exclude_mask[MAX_CHANNELS];                 ///< Channels to be excluded from DRC processing.
    int band_incr;                                  ///< Number of DRC bands greater than 1 having DRC info.
    int interpolation_scheme;                       ///< Indicates the interpolation scheme used in the SBR QMF domain.
    int band_top[17];                               ///< Indicates the top of the i-th DRC band in units of 4 spectral lines.
    int prog_ref_level;                             /**< A reference level for the long-term program audio level for all
                                                     *   channels combined.
                                                     */
} DynamicRangeControl;

/**
 * main AAC decoding context
 */
struct AACContext {
    const struct AVClass  *class;
    struct AVCodecContext *avctx;
    struct AVFrame *frame;

    int is_saved;                 ///< Set if elements have stored overlap from previous frame.
    DynamicRangeControl che_drc;

    /**
     * @name Channel element related data
     * @{
     */
    ChannelElement          *che[4][MAX_ELEM_ID];
    ChannelElement  *tag_che_map[4][MAX_ELEM_ID];
    int tags_mapped;
    int warned_remapping_once;
    /** @} */

    /**
     * @name temporary aligned temporary buffers
     * (We do not want to have these on the stack.)
     * @{
     */
    DECLARE_ALIGNED(32, INTFLOAT, buf_mdct)[1024];
    /** @} */

    /**
     * @name Computed / set up during initialization
     * @{
     */
    AVTXContext *mdct120;
    AVTXContext *mdct128;
    AVTXContext *mdct480;
    AVTXContext *mdct512;
    AVTXContext *mdct960;
    AVTXContext *mdct1024;
    AVTXContext *mdct_ltp;

    av_tx_fn mdct120_fn;
    av_tx_fn mdct128_fn;
    av_tx_fn mdct480_fn;
    av_tx_fn mdct512_fn;
    av_tx_fn mdct960_fn;
    av_tx_fn mdct1024_fn;
    av_tx_fn mdct_ltp_fn;
#if USE_FIXED
    AVFixedDSPContext *fdsp;
#else
    AVFloatDSPContext *fdsp;
#endif /* USE_FIXED */
    int random_state;
    /** @} */

    /**
     * @name Members used for output
     * @{
     */
    SingleChannelElement *output_element[MAX_CHANNELS]; ///< Points to each SingleChannelElement
    /** @} */


    /**
     * @name Japanese DTV specific extension
     * @{
     */
    int force_dmono_mode;///< 0->not dmono, 1->use first channel, 2->use second channel
    int dmono_mode;      ///< 0->not dmono, 1->use first channel, 2->use second channel
    /** @} */

    enum AACOutputChannelOrder output_channel_order;

    DECLARE_ALIGNED(32, INTFLOAT, temp)[128];

    OutputConfiguration oc[2];
    int warned_num_aac_frames;
    int warned_960_sbr;
    unsigned warned_71_wide;
    int warned_gain_control;
    int warned_he_aac_mono;

    /* aacdec functions pointers */
    void (*imdct_and_windowing)(AACContext *ac, SingleChannelElement *sce);
    void (*apply_ltp)(AACContext *ac, SingleChannelElement *sce);
    void (*apply_tns)(INTFLOAT coef[1024], TemporalNoiseShaping *tns,
                      IndividualChannelStream *ics, int decode);
    void (*windowing_and_mdct_ltp)(AACContext *ac, INTFLOAT *out,
                                   INTFLOAT *in, IndividualChannelStream *ics);
    void (*update_ltp)(AACContext *ac, SingleChannelElement *sce);
    void (*vector_pow43)(int *coefs, int len);
    void (*subband_scale)(int *dst, int *src, int scale, int offset, int len, void *log_context);

};

void ff_aacdec_init_mips(AACContext *c);

#endif /* AVCODEC_AACDEC_H */
