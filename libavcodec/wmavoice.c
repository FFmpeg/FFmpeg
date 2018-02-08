/*
 * Windows Media Audio Voice decoder.
 * Copyright (c) 2009 Ronald S. Bultje
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
 * @brief Windows Media Audio Voice compatible decoder
 * @author Ronald S. Bultje <rsbultje@gmail.com>
 */

#include <math.h>

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "put_bits.h"
#include "wmavoice_data.h"
#include "celp_filters.h"
#include "acelp_vectors.h"
#include "acelp_filters.h"
#include "lsp.h"
#include "dct.h"
#include "rdft.h"
#include "sinewin.h"

#define MAX_BLOCKS           8   ///< maximum number of blocks per frame
#define MAX_LSPS             16  ///< maximum filter order
#define MAX_LSPS_ALIGN16     16  ///< same as #MAX_LSPS; needs to be multiple
                                 ///< of 16 for ASM input buffer alignment
#define MAX_FRAMES           3   ///< maximum number of frames per superframe
#define MAX_FRAMESIZE        160 ///< maximum number of samples per frame
#define MAX_SIGNAL_HISTORY   416 ///< maximum excitation signal history
#define MAX_SFRAMESIZE       (MAX_FRAMESIZE * MAX_FRAMES)
                                 ///< maximum number of samples per superframe
#define SFRAME_CACHE_MAXSIZE 256 ///< maximum cache size for frame data that
                                 ///< was split over two packets
#define VLC_NBITS            6   ///< number of bits to read per VLC iteration

/**
 * Frame type VLC coding.
 */
static VLC frame_type_vlc;

/**
 * Adaptive codebook types.
 */
enum {
    ACB_TYPE_NONE       = 0, ///< no adaptive codebook (only hardcoded fixed)
    ACB_TYPE_ASYMMETRIC = 1, ///< adaptive codebook with per-frame pitch, which
                             ///< we interpolate to get a per-sample pitch.
                             ///< Signal is generated using an asymmetric sinc
                             ///< window function
                             ///< @note see #wmavoice_ipol1_coeffs
    ACB_TYPE_HAMMING    = 2  ///< Per-block pitch with signal generation using
                             ///< a Hamming sinc window function
                             ///< @note see #wmavoice_ipol2_coeffs
};

/**
 * Fixed codebook types.
 */
enum {
    FCB_TYPE_SILENCE    = 0, ///< comfort noise during silence
                             ///< generated from a hardcoded (fixed) codebook
                             ///< with per-frame (low) gain values
    FCB_TYPE_HARDCODED  = 1, ///< hardcoded (fixed) codebook with per-block
                             ///< gain values
    FCB_TYPE_AW_PULSES  = 2, ///< Pitch-adaptive window (AW) pulse signals,
                             ///< used in particular for low-bitrate streams
    FCB_TYPE_EXC_PULSES = 3, ///< Innovation (fixed) codebook pulse sets in
                             ///< combinations of either single pulses or
                             ///< pulse pairs
};

/**
 * Description of frame types.
 */
static const struct frame_type_desc {
    uint8_t n_blocks;     ///< amount of blocks per frame (each block
                          ///< (contains 160/#n_blocks samples)
    uint8_t log_n_blocks; ///< log2(#n_blocks)
    uint8_t acb_type;     ///< Adaptive codebook type (ACB_TYPE_*)
    uint8_t fcb_type;     ///< Fixed codebook type (FCB_TYPE_*)
    uint8_t dbl_pulses;   ///< how many pulse vectors have pulse pairs
                          ///< (rather than just one single pulse)
                          ///< only if #fcb_type == #FCB_TYPE_EXC_PULSES
} frame_descs[17] = {
    { 1, 0, ACB_TYPE_NONE,       FCB_TYPE_SILENCE,    0 },
    { 2, 1, ACB_TYPE_NONE,       FCB_TYPE_HARDCODED,  0 },
    { 2, 1, ACB_TYPE_ASYMMETRIC, FCB_TYPE_AW_PULSES,  0 },
    { 2, 1, ACB_TYPE_ASYMMETRIC, FCB_TYPE_EXC_PULSES, 2 },
    { 2, 1, ACB_TYPE_ASYMMETRIC, FCB_TYPE_EXC_PULSES, 5 },
    { 4, 2, ACB_TYPE_ASYMMETRIC, FCB_TYPE_EXC_PULSES, 0 },
    { 4, 2, ACB_TYPE_ASYMMETRIC, FCB_TYPE_EXC_PULSES, 2 },
    { 4, 2, ACB_TYPE_ASYMMETRIC, FCB_TYPE_EXC_PULSES, 5 },
    { 2, 1, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 0 },
    { 2, 1, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 2 },
    { 2, 1, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 5 },
    { 4, 2, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 0 },
    { 4, 2, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 2 },
    { 4, 2, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 5 },
    { 8, 3, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 0 },
    { 8, 3, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 2 },
    { 8, 3, ACB_TYPE_HAMMING,    FCB_TYPE_EXC_PULSES, 5 }
};

/**
 * WMA Voice decoding context.
 */
typedef struct WMAVoiceContext {
    /**
     * @name Global values specified in the stream header / extradata or used all over.
     * @{
     */
    GetBitContext gb;             ///< packet bitreader. During decoder init,
                                  ///< it contains the extradata from the
                                  ///< demuxer. During decoding, it contains
                                  ///< packet data.
    int8_t vbm_tree[25];          ///< converts VLC codes to frame type

    int spillover_bitsize;        ///< number of bits used to specify
                                  ///< #spillover_nbits in the packet header
                                  ///< = ceil(log2(ctx->block_align << 3))
    int history_nsamples;         ///< number of samples in history for signal
                                  ///< prediction (through ACB)

    /* postfilter specific values */
    int do_apf;                   ///< whether to apply the averaged
                                  ///< projection filter (APF)
    int denoise_strength;         ///< strength of denoising in Wiener filter
                                  ///< [0-11]
    int denoise_tilt_corr;        ///< Whether to apply tilt correction to the
                                  ///< Wiener filter coefficients (postfilter)
    int dc_level;                 ///< Predicted amount of DC noise, based
                                  ///< on which a DC removal filter is used

    int lsps;                     ///< number of LSPs per frame [10 or 16]
    int lsp_q_mode;               ///< defines quantizer defaults [0, 1]
    int lsp_def_mode;             ///< defines different sets of LSP defaults
                                  ///< [0, 1]

    int min_pitch_val;            ///< base value for pitch parsing code
    int max_pitch_val;            ///< max value + 1 for pitch parsing
    int pitch_nbits;              ///< number of bits used to specify the
                                  ///< pitch value in the frame header
    int block_pitch_nbits;        ///< number of bits used to specify the
                                  ///< first block's pitch value
    int block_pitch_range;        ///< range of the block pitch
    int block_delta_pitch_nbits;  ///< number of bits used to specify the
                                  ///< delta pitch between this and the last
                                  ///< block's pitch value, used in all but
                                  ///< first block
    int block_delta_pitch_hrange; ///< 1/2 range of the delta (full range is
                                  ///< from -this to +this-1)
    uint16_t block_conv_table[4]; ///< boundaries for block pitch unit/scale
                                  ///< conversion

    /**
     * @}
     *
     * @name Packet values specified in the packet header or related to a packet.
     *
     * A packet is considered to be a single unit of data provided to this
     * decoder by the demuxer.
     * @{
     */
    int spillover_nbits;          ///< number of bits of the previous packet's
                                  ///< last superframe preceding this
                                  ///< packet's first full superframe (useful
                                  ///< for re-synchronization also)
    int has_residual_lsps;        ///< if set, superframes contain one set of
                                  ///< LSPs that cover all frames, encoded as
                                  ///< independent and residual LSPs; if not
                                  ///< set, each frame contains its own, fully
                                  ///< independent, LSPs
    int skip_bits_next;           ///< number of bits to skip at the next call
                                  ///< to #wmavoice_decode_packet() (since
                                  ///< they're part of the previous superframe)

    uint8_t sframe_cache[SFRAME_CACHE_MAXSIZE + AV_INPUT_BUFFER_PADDING_SIZE];
                                  ///< cache for superframe data split over
                                  ///< multiple packets
    int sframe_cache_size;        ///< set to >0 if we have data from an
                                  ///< (incomplete) superframe from a previous
                                  ///< packet that spilled over in the current
                                  ///< packet; specifies the amount of bits in
                                  ///< #sframe_cache
    PutBitContext pb;             ///< bitstream writer for #sframe_cache

    /**
     * @}
     *
     * @name Frame and superframe values
     * Superframe and frame data - these can change from frame to frame,
     * although some of them do in that case serve as a cache / history for
     * the next frame or superframe.
     * @{
     */
    double prev_lsps[MAX_LSPS];   ///< LSPs of the last frame of the previous
                                  ///< superframe
    int last_pitch_val;           ///< pitch value of the previous frame
    int last_acb_type;            ///< frame type [0-2] of the previous frame
    int pitch_diff_sh16;          ///< ((cur_pitch_val - #last_pitch_val)
                                  ///< << 16) / #MAX_FRAMESIZE
    float silence_gain;           ///< set for use in blocks if #ACB_TYPE_NONE

    int aw_idx_is_ext;            ///< whether the AW index was encoded in
                                  ///< 8 bits (instead of 6)
    int aw_pulse_range;           ///< the range over which #aw_pulse_set1()
                                  ///< can apply the pulse, relative to the
                                  ///< value in aw_first_pulse_off. The exact
                                  ///< position of the first AW-pulse is within
                                  ///< [pulse_off, pulse_off + this], and
                                  ///< depends on bitstream values; [16 or 24]
    int aw_n_pulses[2];           ///< number of AW-pulses in each block; note
                                  ///< that this number can be negative (in
                                  ///< which case it basically means "zero")
    int aw_first_pulse_off[2];    ///< index of first sample to which to
                                  ///< apply AW-pulses, or -0xff if unset
    int aw_next_pulse_off_cache;  ///< the position (relative to start of the
                                  ///< second block) at which pulses should
                                  ///< start to be positioned, serves as a
                                  ///< cache for pitch-adaptive window pulses
                                  ///< between blocks

    int frame_cntr;               ///< current frame index [0 - 0xFFFE]; is
                                  ///< only used for comfort noise in #pRNG()
    int nb_superframes;           ///< number of superframes in current packet
    float gain_pred_err[6];       ///< cache for gain prediction
    float excitation_history[MAX_SIGNAL_HISTORY];
                                  ///< cache of the signal of previous
                                  ///< superframes, used as a history for
                                  ///< signal generation
    float synth_history[MAX_LSPS]; ///< see #excitation_history
    /**
     * @}
     *
     * @name Postfilter values
     *
     * Variables used for postfilter implementation, mostly history for
     * smoothing and so on, and context variables for FFT/iFFT.
     * @{
     */
    RDFTContext rdft, irdft;      ///< contexts for FFT-calculation in the
                                  ///< postfilter (for denoise filter)
    DCTContext dct, dst;          ///< contexts for phase shift (in Hilbert
                                  ///< transform, part of postfilter)
    float sin[511], cos[511];     ///< 8-bit cosine/sine windows over [-pi,pi]
                                  ///< range
    float postfilter_agc;         ///< gain control memory, used in
                                  ///< #adaptive_gain_control()
    float dcf_mem[2];             ///< DC filter history
    float zero_exc_pf[MAX_SIGNAL_HISTORY + MAX_SFRAMESIZE];
                                  ///< zero filter output (i.e. excitation)
                                  ///< by postfilter
    float denoise_filter_cache[MAX_FRAMESIZE];
    int   denoise_filter_cache_size; ///< samples in #denoise_filter_cache
    DECLARE_ALIGNED(32, float, tilted_lpcs_pf)[0x80];
                                  ///< aligned buffer for LPC tilting
    DECLARE_ALIGNED(32, float, denoise_coeffs_pf)[0x80];
                                  ///< aligned buffer for denoise coefficients
    DECLARE_ALIGNED(32, float, synth_filter_out_buf)[0x80 + MAX_LSPS_ALIGN16];
                                  ///< aligned buffer for postfilter speech
                                  ///< synthesis
    /**
     * @}
     */
} WMAVoiceContext;

/**
 * Set up the variable bit mode (VBM) tree from container extradata.
 * @param gb bit I/O context.
 *           The bit context (s->gb) should be loaded with byte 23-46 of the
 *           container extradata (i.e. the ones containing the VBM tree).
 * @param vbm_tree pointer to array to which the decoded VBM tree will be
 *                 written.
 * @return 0 on success, <0 on error.
 */
static av_cold int decode_vbmtree(GetBitContext *gb, int8_t vbm_tree[25])
{
    int cntr[8] = { 0 }, n, res;

    memset(vbm_tree, 0xff, sizeof(vbm_tree[0]) * 25);
    for (n = 0; n < 17; n++) {
        res = get_bits(gb, 3);
        if (cntr[res] > 3) // should be >= 3 + (res == 7))
            return -1;
        vbm_tree[res * 3 + cntr[res]++] = n;
    }
    return 0;
}

static av_cold void wmavoice_init_static_data(void)
{
    static const uint8_t bits[] = {
         2,  2,  2,  4,  4,  4,
         6,  6,  6,  8,  8,  8,
        10, 10, 10, 12, 12, 12,
        14, 14, 14, 14
    };
    static const uint16_t codes[] = {
          0x0000, 0x0001, 0x0002,        //              00/01/10
          0x000c, 0x000d, 0x000e,        //           11+00/01/10
          0x003c, 0x003d, 0x003e,        //         1111+00/01/10
          0x00fc, 0x00fd, 0x00fe,        //       111111+00/01/10
          0x03fc, 0x03fd, 0x03fe,        //     11111111+00/01/10
          0x0ffc, 0x0ffd, 0x0ffe,        //   1111111111+00/01/10
          0x3ffc, 0x3ffd, 0x3ffe, 0x3fff // 111111111111+xx
    };

    INIT_VLC_STATIC(&frame_type_vlc, VLC_NBITS, sizeof(bits),
                    bits, 1, 1, codes, 2, 2, 132);
}

static av_cold void wmavoice_flush(AVCodecContext *ctx)
{
    WMAVoiceContext *s = ctx->priv_data;
    int n;

    s->postfilter_agc    = 0;
    s->sframe_cache_size = 0;
    s->skip_bits_next    = 0;
    for (n = 0; n < s->lsps; n++)
        s->prev_lsps[n] = M_PI * (n + 1.0) / (s->lsps + 1.0);
    memset(s->excitation_history, 0,
           sizeof(*s->excitation_history) * MAX_SIGNAL_HISTORY);
    memset(s->synth_history,      0,
           sizeof(*s->synth_history)      * MAX_LSPS);
    memset(s->gain_pred_err,      0,
           sizeof(s->gain_pred_err));

    if (s->do_apf) {
        memset(&s->synth_filter_out_buf[MAX_LSPS_ALIGN16 - s->lsps], 0,
               sizeof(*s->synth_filter_out_buf) * s->lsps);
        memset(s->dcf_mem,              0,
               sizeof(*s->dcf_mem)              * 2);
        memset(s->zero_exc_pf,          0,
               sizeof(*s->zero_exc_pf)          * s->history_nsamples);
        memset(s->denoise_filter_cache, 0, sizeof(s->denoise_filter_cache));
    }
}

/**
 * Set up decoder with parameters from demuxer (extradata etc.).
 */
static av_cold int wmavoice_decode_init(AVCodecContext *ctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    int n, flags, pitch_range, lsp16_flag;
    WMAVoiceContext *s = ctx->priv_data;

    ff_thread_once(&init_static_once, wmavoice_init_static_data);

    /**
     * Extradata layout:
     * - byte  0-18: WMAPro-in-WMAVoice extradata (see wmaprodec.c),
     * - byte 19-22: flags field (annoyingly in LE; see below for known
     *               values),
     * - byte 23-46: variable bitmode tree (really just 17 * 3 bits,
     *               rest is 0).
     */
    if (ctx->extradata_size != 46) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid extradata size %d (should be 46)\n",
               ctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }
    if (ctx->block_align <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid block alignment %d.\n", ctx->block_align);
        return AVERROR_INVALIDDATA;
    }

    flags                = AV_RL32(ctx->extradata + 18);
    s->spillover_bitsize = 3 + av_ceil_log2(ctx->block_align);
    s->do_apf            =    flags & 0x1;
    if (s->do_apf) {
        ff_rdft_init(&s->rdft,  7, DFT_R2C);
        ff_rdft_init(&s->irdft, 7, IDFT_C2R);
        ff_dct_init(&s->dct,  6, DCT_I);
        ff_dct_init(&s->dst,  6, DST_I);

        ff_sine_window_init(s->cos, 256);
        memcpy(&s->sin[255], s->cos, 256 * sizeof(s->cos[0]));
        for (n = 0; n < 255; n++) {
            s->sin[n]       = -s->sin[510 - n];
            s->cos[510 - n] =  s->cos[n];
        }
    }
    s->denoise_strength  =   (flags >> 2) & 0xF;
    if (s->denoise_strength >= 12) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid denoise filter strength %d (max=11)\n",
               s->denoise_strength);
        return AVERROR_INVALIDDATA;
    }
    s->denoise_tilt_corr = !!(flags & 0x40);
    s->dc_level          =   (flags >> 7) & 0xF;
    s->lsp_q_mode        = !!(flags & 0x2000);
    s->lsp_def_mode      = !!(flags & 0x4000);
    lsp16_flag           =    flags & 0x1000;
    if (lsp16_flag) {
        s->lsps               = 16;
    } else {
        s->lsps               = 10;
    }
    for (n = 0; n < s->lsps; n++)
        s->prev_lsps[n] = M_PI * (n + 1.0) / (s->lsps + 1.0);

    init_get_bits(&s->gb, ctx->extradata + 22, (ctx->extradata_size - 22) << 3);
    if (decode_vbmtree(&s->gb, s->vbm_tree) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid VBM tree; broken extradata?\n");
        return AVERROR_INVALIDDATA;
    }

    s->min_pitch_val    = ((ctx->sample_rate << 8)      /  400 + 50) >> 8;
    s->max_pitch_val    = ((ctx->sample_rate << 8) * 37 / 2000 + 50) >> 8;
    pitch_range         = s->max_pitch_val - s->min_pitch_val;
    if (pitch_range <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid pitch range; broken extradata?\n");
        return AVERROR_INVALIDDATA;
    }
    s->pitch_nbits      = av_ceil_log2(pitch_range);
    s->last_pitch_val   = 40;
    s->last_acb_type    = ACB_TYPE_NONE;
    s->history_nsamples = s->max_pitch_val + 8;

    if (s->min_pitch_val < 1 || s->history_nsamples > MAX_SIGNAL_HISTORY) {
        int min_sr = ((((1 << 8) - 50) * 400) + 0xFF) >> 8,
            max_sr = ((((MAX_SIGNAL_HISTORY - 8) << 8) + 205) * 2000 / 37) >> 8;

        av_log(ctx, AV_LOG_ERROR,
               "Unsupported samplerate %d (min=%d, max=%d)\n",
               ctx->sample_rate, min_sr, max_sr); // 322-22097 Hz

        return AVERROR(ENOSYS);
    }

    s->block_conv_table[0]      = s->min_pitch_val;
    s->block_conv_table[1]      = (pitch_range * 25) >> 6;
    s->block_conv_table[2]      = (pitch_range * 44) >> 6;
    s->block_conv_table[3]      = s->max_pitch_val - 1;
    s->block_delta_pitch_hrange = (pitch_range >> 3) & ~0xF;
    if (s->block_delta_pitch_hrange <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid delta pitch hrange; broken extradata?\n");
        return AVERROR_INVALIDDATA;
    }
    s->block_delta_pitch_nbits  = 1 + av_ceil_log2(s->block_delta_pitch_hrange);
    s->block_pitch_range        = s->block_conv_table[2] +
                                  s->block_conv_table[3] + 1 +
                                  2 * (s->block_conv_table[1] - 2 * s->min_pitch_val);
    s->block_pitch_nbits        = av_ceil_log2(s->block_pitch_range);

    ctx->channels               = 1;
    ctx->channel_layout         = AV_CH_LAYOUT_MONO;
    ctx->sample_fmt             = AV_SAMPLE_FMT_FLT;

    return 0;
}

/**
 * @name Postfilter functions
 * Postfilter functions (gain control, wiener denoise filter, DC filter,
 * kalman smoothening, plus surrounding code to wrap it)
 * @{
 */
/**
 * Adaptive gain control (as used in postfilter).
 *
 * Identical to #ff_adaptive_gain_control() in acelp_vectors.c, except
 * that the energy here is calculated using sum(abs(...)), whereas the
 * other codecs (e.g. AMR-NB, SIPRO) use sqrt(dotproduct(...)).
 *
 * @param out output buffer for filtered samples
 * @param in input buffer containing the samples as they are after the
 *           postfilter steps so far
 * @param speech_synth input buffer containing speech synth before postfilter
 * @param size input buffer size
 * @param alpha exponential filter factor
 * @param gain_mem pointer to filter memory (single float)
 */
static void adaptive_gain_control(float *out, const float *in,
                                  const float *speech_synth,
                                  int size, float alpha, float *gain_mem)
{
    int i;
    float speech_energy = 0.0, postfilter_energy = 0.0, gain_scale_factor;
    float mem = *gain_mem;

    for (i = 0; i < size; i++) {
        speech_energy     += fabsf(speech_synth[i]);
        postfilter_energy += fabsf(in[i]);
    }
    gain_scale_factor = postfilter_energy == 0.0 ? 0.0 :
                        (1.0 - alpha) * speech_energy / postfilter_energy;

    for (i = 0; i < size; i++) {
        mem = alpha * mem + gain_scale_factor;
        out[i] = in[i] * mem;
    }

    *gain_mem = mem;
}

/**
 * Kalman smoothing function.
 *
 * This function looks back pitch +/- 3 samples back into history to find
 * the best fitting curve (that one giving the optimal gain of the two
 * signals, i.e. the highest dot product between the two), and then
 * uses that signal history to smoothen the output of the speech synthesis
 * filter.
 *
 * @param s WMA Voice decoding context
 * @param pitch pitch of the speech signal
 * @param in input speech signal
 * @param out output pointer for smoothened signal
 * @param size input/output buffer size
 *
 * @returns -1 if no smoothening took place, e.g. because no optimal
 *          fit could be found, or 0 on success.
 */
static int kalman_smoothen(WMAVoiceContext *s, int pitch,
                           const float *in, float *out, int size)
{
    int n;
    float optimal_gain = 0, dot;
    const float *ptr = &in[-FFMAX(s->min_pitch_val, pitch - 3)],
                *end = &in[-FFMIN(s->max_pitch_val, pitch + 3)],
                *best_hist_ptr = NULL;

    /* find best fitting point in history */
    do {
        dot = avpriv_scalarproduct_float_c(in, ptr, size);
        if (dot > optimal_gain) {
            optimal_gain  = dot;
            best_hist_ptr = ptr;
        }
    } while (--ptr >= end);

    if (optimal_gain <= 0)
        return -1;
    dot = avpriv_scalarproduct_float_c(best_hist_ptr, best_hist_ptr, size);
    if (dot <= 0) // would be 1.0
        return -1;

    if (optimal_gain <= dot) {
        dot = dot / (dot + 0.6 * optimal_gain); // 0.625-1.000
    } else
        dot = 0.625;

    /* actual smoothing */
    for (n = 0; n < size; n++)
        out[n] = best_hist_ptr[n] + dot * (in[n] - best_hist_ptr[n]);

    return 0;
}

/**
 * Get the tilt factor of a formant filter from its transfer function
 * @see #tilt_factor() in amrnbdec.c, which does essentially the same,
 *      but somehow (??) it does a speech synthesis filter in the
 *      middle, which is missing here
 *
 * @param lpcs LPC coefficients
 * @param n_lpcs Size of LPC buffer
 * @returns the tilt factor
 */
static float tilt_factor(const float *lpcs, int n_lpcs)
{
    float rh0, rh1;

    rh0 = 1.0     + avpriv_scalarproduct_float_c(lpcs,  lpcs,    n_lpcs);
    rh1 = lpcs[0] + avpriv_scalarproduct_float_c(lpcs, &lpcs[1], n_lpcs - 1);

    return rh1 / rh0;
}

/**
 * Derive denoise filter coefficients (in real domain) from the LPCs.
 */
static void calc_input_response(WMAVoiceContext *s, float *lpcs,
                                int fcb_type, float *coeffs, int remainder)
{
    float last_coeff, min = 15.0, max = -15.0;
    float irange, angle_mul, gain_mul, range, sq;
    int n, idx;

    /* Create frequency power spectrum of speech input (i.e. RDFT of LPCs) */
    s->rdft.rdft_calc(&s->rdft, lpcs);
#define log_range(var, assign) do { \
        float tmp = log10f(assign);  var = tmp; \
        max       = FFMAX(max, tmp); min = FFMIN(min, tmp); \
    } while (0)
    log_range(last_coeff,  lpcs[1]         * lpcs[1]);
    for (n = 1; n < 64; n++)
        log_range(lpcs[n], lpcs[n * 2]     * lpcs[n * 2] +
                           lpcs[n * 2 + 1] * lpcs[n * 2 + 1]);
    log_range(lpcs[0],     lpcs[0]         * lpcs[0]);
#undef log_range
    range    = max - min;
    lpcs[64] = last_coeff;

    /* Now, use this spectrum to pick out these frequencies with higher
     * (relative) power/energy (which we then take to be "not noise"),
     * and set up a table (still in lpc[]) of (relative) gains per frequency.
     * These frequencies will be maintained, while others ("noise") will be
     * decreased in the filter output. */
    irange    = 64.0 / range; // so irange*(max-value) is in the range [0, 63]
    gain_mul  = range * (fcb_type == FCB_TYPE_HARDCODED ? (5.0 / 13.0) :
                                                          (5.0 / 14.7));
    angle_mul = gain_mul * (8.0 * M_LN10 / M_PI);
    for (n = 0; n <= 64; n++) {
        float pwr;

        idx = FFMAX(0, lrint((max - lpcs[n]) * irange) - 1);
        pwr = wmavoice_denoise_power_table[s->denoise_strength][idx];
        lpcs[n] = angle_mul * pwr;

        /* 70.57 =~ 1/log10(1.0331663) */
        idx = (pwr * gain_mul - 0.0295) * 70.570526123;
        if (idx > 127) { // fall back if index falls outside table range
            coeffs[n] = wmavoice_energy_table[127] *
                        powf(1.0331663, idx - 127);
        } else
            coeffs[n] = wmavoice_energy_table[FFMAX(0, idx)];
    }

    /* calculate the Hilbert transform of the gains, which we do (since this
     * is a sine input) by doing a phase shift (in theory, H(sin())=cos()).
     * Hilbert_Transform(RDFT(x)) = Laplace_Transform(x), which calculates the
     * "moment" of the LPCs in this filter. */
    s->dct.dct_calc(&s->dct, lpcs);
    s->dst.dct_calc(&s->dst, lpcs);

    /* Split out the coefficient indexes into phase/magnitude pairs */
    idx = 255 + av_clip(lpcs[64],               -255, 255);
    coeffs[0]  = coeffs[0]  * s->cos[idx];
    idx = 255 + av_clip(lpcs[64] - 2 * lpcs[63], -255, 255);
    last_coeff = coeffs[64] * s->cos[idx];
    for (n = 63;; n--) {
        idx = 255 + av_clip(-lpcs[64] - 2 * lpcs[n - 1], -255, 255);
        coeffs[n * 2 + 1] = coeffs[n] * s->sin[idx];
        coeffs[n * 2]     = coeffs[n] * s->cos[idx];

        if (!--n) break;

        idx = 255 + av_clip( lpcs[64] - 2 * lpcs[n - 1], -255, 255);
        coeffs[n * 2 + 1] = coeffs[n] * s->sin[idx];
        coeffs[n * 2]     = coeffs[n] * s->cos[idx];
    }
    coeffs[1] = last_coeff;

    /* move into real domain */
    s->irdft.rdft_calc(&s->irdft, coeffs);

    /* tilt correction and normalize scale */
    memset(&coeffs[remainder], 0, sizeof(coeffs[0]) * (128 - remainder));
    if (s->denoise_tilt_corr) {
        float tilt_mem = 0;

        coeffs[remainder - 1] = 0;
        ff_tilt_compensation(&tilt_mem,
                             -1.8 * tilt_factor(coeffs, remainder - 1),
                             coeffs, remainder);
    }
    sq = (1.0 / 64.0) * sqrtf(1 / avpriv_scalarproduct_float_c(coeffs, coeffs,
                                                               remainder));
    for (n = 0; n < remainder; n++)
        coeffs[n] *= sq;
}

/**
 * This function applies a Wiener filter on the (noisy) speech signal as
 * a means to denoise it.
 *
 * - take RDFT of LPCs to get the power spectrum of the noise + speech;
 * - using this power spectrum, calculate (for each frequency) the Wiener
 *    filter gain, which depends on the frequency power and desired level
 *    of noise subtraction (when set too high, this leads to artifacts)
 *    We can do this symmetrically over the X-axis (so 0-4kHz is the inverse
 *    of 4-8kHz);
 * - by doing a phase shift, calculate the Hilbert transform of this array
 *    of per-frequency filter-gains to get the filtering coefficients;
 * - smoothen/normalize/de-tilt these filter coefficients as desired;
 * - take RDFT of noisy sound, apply the coefficients and take its IRDFT
 *    to get the denoised speech signal;
 * - the leftover (i.e. output of the IRDFT on denoised speech data beyond
 *    the frame boundary) are saved and applied to subsequent frames by an
 *    overlap-add method (otherwise you get clicking-artifacts).
 *
 * @param s WMA Voice decoding context
 * @param fcb_type Frame (codebook) type
 * @param synth_pf input: the noisy speech signal, output: denoised speech
 *                 data; should be 16-byte aligned (for ASM purposes)
 * @param size size of the speech data
 * @param lpcs LPCs used to synthesize this frame's speech data
 */
static void wiener_denoise(WMAVoiceContext *s, int fcb_type,
                           float *synth_pf, int size,
                           const float *lpcs)
{
    int remainder, lim, n;

    if (fcb_type != FCB_TYPE_SILENCE) {
        float *tilted_lpcs = s->tilted_lpcs_pf,
              *coeffs = s->denoise_coeffs_pf, tilt_mem = 0;

        tilted_lpcs[0]           = 1.0;
        memcpy(&tilted_lpcs[1], lpcs, sizeof(lpcs[0]) * s->lsps);
        memset(&tilted_lpcs[s->lsps + 1], 0,
               sizeof(tilted_lpcs[0]) * (128 - s->lsps - 1));
        ff_tilt_compensation(&tilt_mem, 0.7 * tilt_factor(lpcs, s->lsps),
                             tilted_lpcs, s->lsps + 2);

        /* The IRDFT output (127 samples for 7-bit filter) beyond the frame
         * size is applied to the next frame. All input beyond this is zero,
         * and thus all output beyond this will go towards zero, hence we can
         * limit to min(size-1, 127-size) as a performance consideration. */
        remainder = FFMIN(127 - size, size - 1);
        calc_input_response(s, tilted_lpcs, fcb_type, coeffs, remainder);

        /* apply coefficients (in frequency spectrum domain), i.e. complex
         * number multiplication */
        memset(&synth_pf[size], 0, sizeof(synth_pf[0]) * (128 - size));
        s->rdft.rdft_calc(&s->rdft, synth_pf);
        s->rdft.rdft_calc(&s->rdft, coeffs);
        synth_pf[0] *= coeffs[0];
        synth_pf[1] *= coeffs[1];
        for (n = 1; n < 64; n++) {
            float v1 = synth_pf[n * 2], v2 = synth_pf[n * 2 + 1];
            synth_pf[n * 2]     = v1 * coeffs[n * 2] - v2 * coeffs[n * 2 + 1];
            synth_pf[n * 2 + 1] = v2 * coeffs[n * 2] + v1 * coeffs[n * 2 + 1];
        }
        s->irdft.rdft_calc(&s->irdft, synth_pf);
    }

    /* merge filter output with the history of previous runs */
    if (s->denoise_filter_cache_size) {
        lim = FFMIN(s->denoise_filter_cache_size, size);
        for (n = 0; n < lim; n++)
            synth_pf[n] += s->denoise_filter_cache[n];
        s->denoise_filter_cache_size -= lim;
        memmove(s->denoise_filter_cache, &s->denoise_filter_cache[size],
                sizeof(s->denoise_filter_cache[0]) * s->denoise_filter_cache_size);
    }

    /* move remainder of filter output into a cache for future runs */
    if (fcb_type != FCB_TYPE_SILENCE) {
        lim = FFMIN(remainder, s->denoise_filter_cache_size);
        for (n = 0; n < lim; n++)
            s->denoise_filter_cache[n] += synth_pf[size + n];
        if (lim < remainder) {
            memcpy(&s->denoise_filter_cache[lim], &synth_pf[size + lim],
                   sizeof(s->denoise_filter_cache[0]) * (remainder - lim));
            s->denoise_filter_cache_size = remainder;
        }
    }
}

/**
 * Averaging projection filter, the postfilter used in WMAVoice.
 *
 * This uses the following steps:
 * - A zero-synthesis filter (generate excitation from synth signal)
 * - Kalman smoothing on excitation, based on pitch
 * - Re-synthesized smoothened output
 * - Iterative Wiener denoise filter
 * - Adaptive gain filter
 * - DC filter
 *
 * @param s WMAVoice decoding context
 * @param synth Speech synthesis output (before postfilter)
 * @param samples Output buffer for filtered samples
 * @param size Buffer size of synth & samples
 * @param lpcs Generated LPCs used for speech synthesis
 * @param zero_exc_pf destination for zero synthesis filter (16-byte aligned)
 * @param fcb_type Frame type (silence, hardcoded, AW-pulses or FCB-pulses)
 * @param pitch Pitch of the input signal
 */
static void postfilter(WMAVoiceContext *s, const float *synth,
                       float *samples,    int size,
                       const float *lpcs, float *zero_exc_pf,
                       int fcb_type,      int pitch)
{
    float synth_filter_in_buf[MAX_FRAMESIZE / 2],
          *synth_pf = &s->synth_filter_out_buf[MAX_LSPS_ALIGN16],
          *synth_filter_in = zero_exc_pf;

    av_assert0(size <= MAX_FRAMESIZE / 2);

    /* generate excitation from input signal */
    ff_celp_lp_zero_synthesis_filterf(zero_exc_pf, lpcs, synth, size, s->lsps);

    if (fcb_type >= FCB_TYPE_AW_PULSES &&
        !kalman_smoothen(s, pitch, zero_exc_pf, synth_filter_in_buf, size))
        synth_filter_in = synth_filter_in_buf;

    /* re-synthesize speech after smoothening, and keep history */
    ff_celp_lp_synthesis_filterf(synth_pf, lpcs,
                                 synth_filter_in, size, s->lsps);
    memcpy(&synth_pf[-s->lsps], &synth_pf[size - s->lsps],
           sizeof(synth_pf[0]) * s->lsps);

    wiener_denoise(s, fcb_type, synth_pf, size, lpcs);

    adaptive_gain_control(samples, synth_pf, synth, size, 0.99,
                          &s->postfilter_agc);

    if (s->dc_level > 8) {
        /* remove ultra-low frequency DC noise / highpass filter;
         * coefficients are identical to those used in SIPR decoding,
         * and very closely resemble those used in AMR-NB decoding. */
        ff_acelp_apply_order_2_transfer_function(samples, samples,
            (const float[2]) { -1.99997,      1.0 },
            (const float[2]) { -1.9330735188, 0.93589198496 },
            0.93980580475, s->dcf_mem, size);
    }
}
/**
 * @}
 */

/**
 * Dequantize LSPs
 * @param lsps output pointer to the array that will hold the LSPs
 * @param num number of LSPs to be dequantized
 * @param values quantized values, contains n_stages values
 * @param sizes range (i.e. max value) of each quantized value
 * @param n_stages number of dequantization runs
 * @param table dequantization table to be used
 * @param mul_q LSF multiplier
 * @param base_q base (lowest) LSF values
 */
static void dequant_lsps(double *lsps, int num,
                         const uint16_t *values,
                         const uint16_t *sizes,
                         int n_stages, const uint8_t *table,
                         const double *mul_q,
                         const double *base_q)
{
    int n, m;

    memset(lsps, 0, num * sizeof(*lsps));
    for (n = 0; n < n_stages; n++) {
        const uint8_t *t_off = &table[values[n] * num];
        double base = base_q[n], mul = mul_q[n];

        for (m = 0; m < num; m++)
            lsps[m] += base + mul * t_off[m];

        table += sizes[n] * num;
    }
}

/**
 * @name LSP dequantization routines
 * LSP dequantization routines, for 10/16LSPs and independent/residual coding.
 * lsp10i() consumes 24 bits; lsp10r() consumes an additional 24 bits;
 * lsp16i() consumes 34 bits; lsp16r() consumes an additional 26 bits.
 * @{
 */
/**
 * Parse 10 independently-coded LSPs.
 */
static void dequant_lsp10i(GetBitContext *gb, double *lsps)
{
    static const uint16_t vec_sizes[4] = { 256, 64, 32, 32 };
    static const double mul_lsf[4] = {
        5.2187144800e-3,    1.4626986422e-3,
        9.6179549166e-4,    1.1325736225e-3
    };
    static const double base_lsf[4] = {
        M_PI * -2.15522e-1, M_PI * -6.1646e-2,
        M_PI * -3.3486e-2,  M_PI * -5.7408e-2
    };
    uint16_t v[4];

    v[0] = get_bits(gb, 8);
    v[1] = get_bits(gb, 6);
    v[2] = get_bits(gb, 5);
    v[3] = get_bits(gb, 5);

    dequant_lsps(lsps, 10, v, vec_sizes, 4, wmavoice_dq_lsp10i,
                 mul_lsf, base_lsf);
}

/**
 * Parse 10 independently-coded LSPs, and then derive the tables to
 * generate LSPs for the other frames from them (residual coding).
 */
static void dequant_lsp10r(GetBitContext *gb,
                           double *i_lsps, const double *old,
                           double *a1, double *a2, int q_mode)
{
    static const uint16_t vec_sizes[3] = { 128, 64, 64 };
    static const double mul_lsf[3] = {
        2.5807601174e-3,    1.2354460219e-3,   1.1763821673e-3
    };
    static const double base_lsf[3] = {
        M_PI * -1.07448e-1, M_PI * -5.2706e-2, M_PI * -5.1634e-2
    };
    const float (*ipol_tab)[2][10] = q_mode ?
        wmavoice_lsp10_intercoeff_b : wmavoice_lsp10_intercoeff_a;
    uint16_t interpol, v[3];
    int n;

    dequant_lsp10i(gb, i_lsps);

    interpol = get_bits(gb, 5);
    v[0]     = get_bits(gb, 7);
    v[1]     = get_bits(gb, 6);
    v[2]     = get_bits(gb, 6);

    for (n = 0; n < 10; n++) {
        double delta = old[n] - i_lsps[n];
        a1[n]        = ipol_tab[interpol][0][n] * delta + i_lsps[n];
        a1[10 + n]   = ipol_tab[interpol][1][n] * delta + i_lsps[n];
    }

    dequant_lsps(a2, 20, v, vec_sizes, 3, wmavoice_dq_lsp10r,
                 mul_lsf, base_lsf);
}

/**
 * Parse 16 independently-coded LSPs.
 */
static void dequant_lsp16i(GetBitContext *gb, double *lsps)
{
    static const uint16_t vec_sizes[5] = { 256, 64, 128, 64, 128 };
    static const double mul_lsf[5] = {
        3.3439586280e-3,    6.9908173703e-4,
        3.3216608306e-3,    1.0334960326e-3,
        3.1899104283e-3
    };
    static const double base_lsf[5] = {
        M_PI * -1.27576e-1, M_PI * -2.4292e-2,
        M_PI * -1.28094e-1, M_PI * -3.2128e-2,
        M_PI * -1.29816e-1
    };
    uint16_t v[5];

    v[0] = get_bits(gb, 8);
    v[1] = get_bits(gb, 6);
    v[2] = get_bits(gb, 7);
    v[3] = get_bits(gb, 6);
    v[4] = get_bits(gb, 7);

    dequant_lsps( lsps,     5,  v,     vec_sizes,    2,
                 wmavoice_dq_lsp16i1,  mul_lsf,     base_lsf);
    dequant_lsps(&lsps[5],  5, &v[2], &vec_sizes[2], 2,
                 wmavoice_dq_lsp16i2, &mul_lsf[2], &base_lsf[2]);
    dequant_lsps(&lsps[10], 6, &v[4], &vec_sizes[4], 1,
                 wmavoice_dq_lsp16i3, &mul_lsf[4], &base_lsf[4]);
}

/**
 * Parse 16 independently-coded LSPs, and then derive the tables to
 * generate LSPs for the other frames from them (residual coding).
 */
static void dequant_lsp16r(GetBitContext *gb,
                           double *i_lsps, const double *old,
                           double *a1, double *a2, int q_mode)
{
    static const uint16_t vec_sizes[3] = { 128, 128, 128 };
    static const double mul_lsf[3] = {
        1.2232979501e-3,   1.4062241527e-3,   1.6114744851e-3
    };
    static const double base_lsf[3] = {
        M_PI * -5.5830e-2, M_PI * -5.2908e-2, M_PI * -5.4776e-2
    };
    const float (*ipol_tab)[2][16] = q_mode ?
        wmavoice_lsp16_intercoeff_b : wmavoice_lsp16_intercoeff_a;
    uint16_t interpol, v[3];
    int n;

    dequant_lsp16i(gb, i_lsps);

    interpol = get_bits(gb, 5);
    v[0]     = get_bits(gb, 7);
    v[1]     = get_bits(gb, 7);
    v[2]     = get_bits(gb, 7);

    for (n = 0; n < 16; n++) {
        double delta = old[n] - i_lsps[n];
        a1[n]        = ipol_tab[interpol][0][n] * delta + i_lsps[n];
        a1[16 + n]   = ipol_tab[interpol][1][n] * delta + i_lsps[n];
    }

    dequant_lsps( a2,     10,  v,     vec_sizes,    1,
                 wmavoice_dq_lsp16r1,  mul_lsf,     base_lsf);
    dequant_lsps(&a2[10], 10, &v[1], &vec_sizes[1], 1,
                 wmavoice_dq_lsp16r2, &mul_lsf[1], &base_lsf[1]);
    dequant_lsps(&a2[20], 12, &v[2], &vec_sizes[2], 1,
                 wmavoice_dq_lsp16r3, &mul_lsf[2], &base_lsf[2]);
}

/**
 * @}
 * @name Pitch-adaptive window coding functions
 * The next few functions are for pitch-adaptive window coding.
 * @{
 */
/**
 * Parse the offset of the first pitch-adaptive window pulses, and
 * the distribution of pulses between the two blocks in this frame.
 * @param s WMA Voice decoding context private data
 * @param gb bit I/O context
 * @param pitch pitch for each block in this frame
 */
static void aw_parse_coords(WMAVoiceContext *s, GetBitContext *gb,
                            const int *pitch)
{
    static const int16_t start_offset[94] = {
        -11,  -9,  -7,  -5,  -3,  -1,   1,   3,   5,   7,   9,  11,
         13,  15,  18,  17,  19,  20,  21,  22,  23,  24,  25,  26,
         27,  28,  29,  30,  31,  32,  33,  35,  37,  39,  41,  43,
         45,  47,  49,  51,  53,  55,  57,  59,  61,  63,  65,  67,
         69,  71,  73,  75,  77,  79,  81,  83,  85,  87,  89,  91,
         93,  95,  97,  99, 101, 103, 105, 107, 109, 111, 113, 115,
        117, 119, 121, 123, 125, 127, 129, 131, 133, 135, 137, 139,
        141, 143, 145, 147, 149, 151, 153, 155, 157, 159
    };
    int bits, offset;

    /* position of pulse */
    s->aw_idx_is_ext = 0;
    if ((bits = get_bits(gb, 6)) >= 54) {
        s->aw_idx_is_ext = 1;
        bits += (bits - 54) * 3 + get_bits(gb, 2);
    }

    /* for a repeated pulse at pulse_off with a pitch_lag of pitch[], count
     * the distribution of the pulses in each block contained in this frame. */
    s->aw_pulse_range        = FFMIN(pitch[0], pitch[1]) > 32 ? 24 : 16;
    for (offset = start_offset[bits]; offset < 0; offset += pitch[0]) ;
    s->aw_n_pulses[0]        = (pitch[0] - 1 + MAX_FRAMESIZE / 2 - offset) / pitch[0];
    s->aw_first_pulse_off[0] = offset - s->aw_pulse_range / 2;
    offset                  += s->aw_n_pulses[0] * pitch[0];
    s->aw_n_pulses[1]        = (pitch[1] - 1 + MAX_FRAMESIZE - offset) / pitch[1];
    s->aw_first_pulse_off[1] = offset - (MAX_FRAMESIZE + s->aw_pulse_range) / 2;

    /* if continuing from a position before the block, reset position to
     * start of block (when corrected for the range over which it can be
     * spread in aw_pulse_set1()). */
    if (start_offset[bits] < MAX_FRAMESIZE / 2) {
        while (s->aw_first_pulse_off[1] - pitch[1] + s->aw_pulse_range > 0)
            s->aw_first_pulse_off[1] -= pitch[1];
        if (start_offset[bits] < 0)
            while (s->aw_first_pulse_off[0] - pitch[0] + s->aw_pulse_range > 0)
                s->aw_first_pulse_off[0] -= pitch[0];
    }
}

/**
 * Apply second set of pitch-adaptive window pulses.
 * @param s WMA Voice decoding context private data
 * @param gb bit I/O context
 * @param block_idx block index in frame [0, 1]
 * @param fcb structure containing fixed codebook vector info
 * @return -1 on error, 0 otherwise
 */
static int aw_pulse_set2(WMAVoiceContext *s, GetBitContext *gb,
                         int block_idx, AMRFixed *fcb)
{
    uint16_t use_mask_mem[9]; // only 5 are used, rest is padding
    uint16_t *use_mask = use_mask_mem + 2;
    /* in this function, idx is the index in the 80-bit (+ padding) use_mask
     * bit-array. Since use_mask consists of 16-bit values, the lower 4 bits
     * of idx are the position of the bit within a particular item in the
     * array (0 being the most significant bit, and 15 being the least
     * significant bit), and the remainder (>> 4) is the index in the
     * use_mask[]-array. This is faster and uses less memory than using a
     * 80-byte/80-int array. */
    int pulse_off = s->aw_first_pulse_off[block_idx],
        pulse_start, n, idx, range, aidx, start_off = 0;

    /* set offset of first pulse to within this block */
    if (s->aw_n_pulses[block_idx] > 0)
        while (pulse_off + s->aw_pulse_range < 1)
            pulse_off += fcb->pitch_lag;

    /* find range per pulse */
    if (s->aw_n_pulses[0] > 0) {
        if (block_idx == 0) {
            range = 32;
        } else /* block_idx = 1 */ {
            range = 8;
            if (s->aw_n_pulses[block_idx] > 0)
                pulse_off = s->aw_next_pulse_off_cache;
        }
    } else
        range = 16;
    pulse_start = s->aw_n_pulses[block_idx] > 0 ? pulse_off - range / 2 : 0;

    /* aw_pulse_set1() already applies pulses around pulse_off (to be exactly,
     * in the range of [pulse_off, pulse_off + s->aw_pulse_range], and thus
     * we exclude that range from being pulsed again in this function. */
    memset(&use_mask[-2], 0, 2 * sizeof(use_mask[0]));
    memset( use_mask,   -1, 5 * sizeof(use_mask[0]));
    memset(&use_mask[5], 0, 2 * sizeof(use_mask[0]));
    if (s->aw_n_pulses[block_idx] > 0)
        for (idx = pulse_off; idx < MAX_FRAMESIZE / 2; idx += fcb->pitch_lag) {
            int excl_range         = s->aw_pulse_range; // always 16 or 24
            uint16_t *use_mask_ptr = &use_mask[idx >> 4];
            int first_sh           = 16 - (idx & 15);
            *use_mask_ptr++       &= 0xFFFFu << first_sh;
            excl_range            -= first_sh;
            if (excl_range >= 16) {
                *use_mask_ptr++    = 0;
                *use_mask_ptr     &= 0xFFFF >> (excl_range - 16);
            } else
                *use_mask_ptr     &= 0xFFFF >> excl_range;
        }

    /* find the 'aidx'th offset that is not excluded */
    aidx = get_bits(gb, s->aw_n_pulses[0] > 0 ? 5 - 2 * block_idx : 4);
    for (n = 0; n <= aidx; pulse_start++) {
        for (idx = pulse_start; idx < 0; idx += fcb->pitch_lag) ;
        if (idx >= MAX_FRAMESIZE / 2) { // find from zero
            if (use_mask[0])      idx = 0x0F;
            else if (use_mask[1]) idx = 0x1F;
            else if (use_mask[2]) idx = 0x2F;
            else if (use_mask[3]) idx = 0x3F;
            else if (use_mask[4]) idx = 0x4F;
            else return -1;
            idx -= av_log2_16bit(use_mask[idx >> 4]);
        }
        if (use_mask[idx >> 4] & (0x8000 >> (idx & 15))) {
            use_mask[idx >> 4] &= ~(0x8000 >> (idx & 15));
            n++;
            start_off = idx;
        }
    }

    fcb->x[fcb->n] = start_off;
    fcb->y[fcb->n] = get_bits1(gb) ? -1.0 : 1.0;
    fcb->n++;

    /* set offset for next block, relative to start of that block */
    n = (MAX_FRAMESIZE / 2 - start_off) % fcb->pitch_lag;
    s->aw_next_pulse_off_cache = n ? fcb->pitch_lag - n : 0;
    return 0;
}

/**
 * Apply first set of pitch-adaptive window pulses.
 * @param s WMA Voice decoding context private data
 * @param gb bit I/O context
 * @param block_idx block index in frame [0, 1]
 * @param fcb storage location for fixed codebook pulse info
 */
static void aw_pulse_set1(WMAVoiceContext *s, GetBitContext *gb,
                          int block_idx, AMRFixed *fcb)
{
    int val = get_bits(gb, 12 - 2 * (s->aw_idx_is_ext && !block_idx));
    float v;

    if (s->aw_n_pulses[block_idx] > 0) {
        int n, v_mask, i_mask, sh, n_pulses;

        if (s->aw_pulse_range == 24) { // 3 pulses, 1:sign + 3:index each
            n_pulses = 3;
            v_mask   = 8;
            i_mask   = 7;
            sh       = 4;
        } else { // 4 pulses, 1:sign + 2:index each
            n_pulses = 4;
            v_mask   = 4;
            i_mask   = 3;
            sh       = 3;
        }

        for (n = n_pulses - 1; n >= 0; n--, val >>= sh) {
            fcb->y[fcb->n] = (val & v_mask) ? -1.0 : 1.0;
            fcb->x[fcb->n] = (val & i_mask) * n_pulses + n +
                                 s->aw_first_pulse_off[block_idx];
            while (fcb->x[fcb->n] < 0)
                fcb->x[fcb->n] += fcb->pitch_lag;
            if (fcb->x[fcb->n] < MAX_FRAMESIZE / 2)
                fcb->n++;
        }
    } else {
        int num2 = (val & 0x1FF) >> 1, delta, idx;

        if (num2 < 1 * 79)      { delta = 1; idx = num2 + 1; }
        else if (num2 < 2 * 78) { delta = 3; idx = num2 + 1 - 1 * 77; }
        else if (num2 < 3 * 77) { delta = 5; idx = num2 + 1 - 2 * 76; }
        else                    { delta = 7; idx = num2 + 1 - 3 * 75; }
        v = (val & 0x200) ? -1.0 : 1.0;

        fcb->no_repeat_mask |= 3 << fcb->n;
        fcb->x[fcb->n]       = idx - delta;
        fcb->y[fcb->n]       = v;
        fcb->x[fcb->n + 1]   = idx;
        fcb->y[fcb->n + 1]   = (val & 1) ? -v : v;
        fcb->n              += 2;
    }
}

/**
 * @}
 *
 * Generate a random number from frame_cntr and block_idx, which will live
 * in the range [0, 1000 - block_size] (so it can be used as an index in a
 * table of size 1000 of which you want to read block_size entries).
 *
 * @param frame_cntr current frame number
 * @param block_num current block index
 * @param block_size amount of entries we want to read from a table
 *                   that has 1000 entries
 * @return a (non-)random number in the [0, 1000 - block_size] range.
 */
static int pRNG(int frame_cntr, int block_num, int block_size)
{
    /* array to simplify the calculation of z:
     * y = (x % 9) * 5 + 6;
     * z = (49995 * x) / y;
     * Since y only has 9 values, we can remove the division by using a
     * LUT and using FASTDIV-style divisions. For each of the 9 values
     * of y, we can rewrite z as:
     * z = x * (49995 / y) + x * ((49995 % y) / y)
     * In this table, each col represents one possible value of y, the
     * first number is 49995 / y, and the second is the FASTDIV variant
     * of 49995 % y / y. */
    static const unsigned int div_tbl[9][2] = {
        { 8332,  3 * 715827883U }, // y =  6
        { 4545,  0 * 390451573U }, // y = 11
        { 3124, 11 * 268435456U }, // y = 16
        { 2380, 15 * 204522253U }, // y = 21
        { 1922, 23 * 165191050U }, // y = 26
        { 1612, 23 * 138547333U }, // y = 31
        { 1388, 27 * 119304648U }, // y = 36
        { 1219, 16 * 104755300U }, // y = 41
        { 1086, 39 *  93368855U }  // y = 46
    };
    unsigned int z, y, x = MUL16(block_num, 1877) + frame_cntr;
    if (x >= 0xFFFF) x -= 0xFFFF;   // max value of x is 8*1877+0xFFFE=0x13AA6,
                                    // so this is effectively a modulo (%)
    y = x - 9 * MULH(477218589, x); // x % 9
    z = (uint16_t) (x * div_tbl[y][0] + UMULH(x, div_tbl[y][1]));
                                    // z = x * 49995 / (y * 5 + 6)
    return z % (1000 - block_size);
}

/**
 * Parse hardcoded signal for a single block.
 * @note see #synth_block().
 */
static void synth_block_hardcoded(WMAVoiceContext *s, GetBitContext *gb,
                                 int block_idx, int size,
                                 const struct frame_type_desc *frame_desc,
                                 float *excitation)
{
    float gain;
    int n, r_idx;

    av_assert0(size <= MAX_FRAMESIZE);

    /* Set the offset from which we start reading wmavoice_std_codebook */
    if (frame_desc->fcb_type == FCB_TYPE_SILENCE) {
        r_idx = pRNG(s->frame_cntr, block_idx, size);
        gain  = s->silence_gain;
    } else /* FCB_TYPE_HARDCODED */ {
        r_idx = get_bits(gb, 8);
        gain  = wmavoice_gain_universal[get_bits(gb, 6)];
    }

    /* Clear gain prediction parameters */
    memset(s->gain_pred_err, 0, sizeof(s->gain_pred_err));

    /* Apply gain to hardcoded codebook and use that as excitation signal */
    for (n = 0; n < size; n++)
        excitation[n] = wmavoice_std_codebook[r_idx + n] * gain;
}

/**
 * Parse FCB/ACB signal for a single block.
 * @note see #synth_block().
 */
static void synth_block_fcb_acb(WMAVoiceContext *s, GetBitContext *gb,
                                int block_idx, int size,
                                int block_pitch_sh2,
                                const struct frame_type_desc *frame_desc,
                                float *excitation)
{
    static const float gain_coeff[6] = {
        0.8169, -0.06545, 0.1726, 0.0185, -0.0359, 0.0458
    };
    float pulses[MAX_FRAMESIZE / 2], pred_err, acb_gain, fcb_gain;
    int n, idx, gain_weight;
    AMRFixed fcb;

    av_assert0(size <= MAX_FRAMESIZE / 2);
    memset(pulses, 0, sizeof(*pulses) * size);

    fcb.pitch_lag      = block_pitch_sh2 >> 2;
    fcb.pitch_fac      = 1.0;
    fcb.no_repeat_mask = 0;
    fcb.n              = 0;

    /* For the other frame types, this is where we apply the innovation
     * (fixed) codebook pulses of the speech signal. */
    if (frame_desc->fcb_type == FCB_TYPE_AW_PULSES) {
        aw_pulse_set1(s, gb, block_idx, &fcb);
        if (aw_pulse_set2(s, gb, block_idx, &fcb)) {
            /* Conceal the block with silence and return.
             * Skip the correct amount of bits to read the next
             * block from the correct offset. */
            int r_idx = pRNG(s->frame_cntr, block_idx, size);

            for (n = 0; n < size; n++)
                excitation[n] =
                    wmavoice_std_codebook[r_idx + n] * s->silence_gain;
            skip_bits(gb, 7 + 1);
            return;
        }
    } else /* FCB_TYPE_EXC_PULSES */ {
        int offset_nbits = 5 - frame_desc->log_n_blocks;

        fcb.no_repeat_mask = -1;
        /* similar to ff_decode_10_pulses_35bits(), but with single pulses
         * (instead of double) for a subset of pulses */
        for (n = 0; n < 5; n++) {
            float sign;
            int pos1, pos2;

            sign           = get_bits1(gb) ? 1.0 : -1.0;
            pos1           = get_bits(gb, offset_nbits);
            fcb.x[fcb.n]   = n + 5 * pos1;
            fcb.y[fcb.n++] = sign;
            if (n < frame_desc->dbl_pulses) {
                pos2           = get_bits(gb, offset_nbits);
                fcb.x[fcb.n]   = n + 5 * pos2;
                fcb.y[fcb.n++] = (pos1 < pos2) ? -sign : sign;
            }
        }
    }
    ff_set_fixed_vector(pulses, &fcb, 1.0, size);

    /* Calculate gain for adaptive & fixed codebook signal.
     * see ff_amr_set_fixed_gain(). */
    idx = get_bits(gb, 7);
    fcb_gain = expf(avpriv_scalarproduct_float_c(s->gain_pred_err,
                                                 gain_coeff, 6) -
                    5.2409161640 + wmavoice_gain_codebook_fcb[idx]);
    acb_gain = wmavoice_gain_codebook_acb[idx];
    pred_err = av_clipf(wmavoice_gain_codebook_fcb[idx],
                        -2.9957322736 /* log(0.05) */,
                         1.6094379124 /* log(5.0)  */);

    gain_weight = 8 >> frame_desc->log_n_blocks;
    memmove(&s->gain_pred_err[gain_weight], s->gain_pred_err,
            sizeof(*s->gain_pred_err) * (6 - gain_weight));
    for (n = 0; n < gain_weight; n++)
        s->gain_pred_err[n] = pred_err;

    /* Calculation of adaptive codebook */
    if (frame_desc->acb_type == ACB_TYPE_ASYMMETRIC) {
        int len;
        for (n = 0; n < size; n += len) {
            int next_idx_sh16;
            int abs_idx    = block_idx * size + n;
            int pitch_sh16 = (s->last_pitch_val << 16) +
                             s->pitch_diff_sh16 * abs_idx;
            int pitch      = (pitch_sh16 + 0x6FFF) >> 16;
            int idx_sh16   = ((pitch << 16) - pitch_sh16) * 8 + 0x58000;
            idx            = idx_sh16 >> 16;
            if (s->pitch_diff_sh16) {
                if (s->pitch_diff_sh16 > 0) {
                    next_idx_sh16 = (idx_sh16) &~ 0xFFFF;
                } else
                    next_idx_sh16 = (idx_sh16 + 0x10000) &~ 0xFFFF;
                len = av_clip((idx_sh16 - next_idx_sh16) / s->pitch_diff_sh16 / 8,
                              1, size - n);
            } else
                len = size;

            ff_acelp_interpolatef(&excitation[n], &excitation[n - pitch],
                                  wmavoice_ipol1_coeffs, 17,
                                  idx, 9, len);
        }
    } else /* ACB_TYPE_HAMMING */ {
        int block_pitch = block_pitch_sh2 >> 2;
        idx             = block_pitch_sh2 & 3;
        if (idx) {
            ff_acelp_interpolatef(excitation, &excitation[-block_pitch],
                                  wmavoice_ipol2_coeffs, 4,
                                  idx, 8, size);
        } else
            av_memcpy_backptr((uint8_t *) excitation, sizeof(float) * block_pitch,
                              sizeof(float) * size);
    }

    /* Interpolate ACB/FCB and use as excitation signal */
    ff_weighted_vector_sumf(excitation, excitation, pulses,
                            acb_gain, fcb_gain, size);
}

/**
 * Parse data in a single block.
 *
 * @param s WMA Voice decoding context private data
 * @param gb bit I/O context
 * @param block_idx index of the to-be-read block
 * @param size amount of samples to be read in this block
 * @param block_pitch_sh2 pitch for this block << 2
 * @param lsps LSPs for (the end of) this frame
 * @param prev_lsps LSPs for the last frame
 * @param frame_desc frame type descriptor
 * @param excitation target memory for the ACB+FCB interpolated signal
 * @param synth target memory for the speech synthesis filter output
 * @return 0 on success, <0 on error.
 */
static void synth_block(WMAVoiceContext *s, GetBitContext *gb,
                        int block_idx, int size,
                        int block_pitch_sh2,
                        const double *lsps, const double *prev_lsps,
                        const struct frame_type_desc *frame_desc,
                        float *excitation, float *synth)
{
    double i_lsps[MAX_LSPS];
    float lpcs[MAX_LSPS];
    float fac;
    int n;

    if (frame_desc->acb_type == ACB_TYPE_NONE)
        synth_block_hardcoded(s, gb, block_idx, size, frame_desc, excitation);
    else
        synth_block_fcb_acb(s, gb, block_idx, size, block_pitch_sh2,
                            frame_desc, excitation);

    /* convert interpolated LSPs to LPCs */
    fac = (block_idx + 0.5) / frame_desc->n_blocks;
    for (n = 0; n < s->lsps; n++) // LSF -> LSP
        i_lsps[n] = cos(prev_lsps[n] + fac * (lsps[n] - prev_lsps[n]));
    ff_acelp_lspd2lpc(i_lsps, lpcs, s->lsps >> 1);

    /* Speech synthesis */
    ff_celp_lp_synthesis_filterf(synth, lpcs, excitation, size, s->lsps);
}

/**
 * Synthesize output samples for a single frame.
 *
 * @param ctx WMA Voice decoder context
 * @param gb bit I/O context (s->gb or one for cross-packet superframes)
 * @param frame_idx Frame number within superframe [0-2]
 * @param samples pointer to output sample buffer, has space for at least 160
 *                samples
 * @param lsps LSP array
 * @param prev_lsps array of previous frame's LSPs
 * @param excitation target buffer for excitation signal
 * @param synth target buffer for synthesized speech data
 * @return 0 on success, <0 on error.
 */
static int synth_frame(AVCodecContext *ctx, GetBitContext *gb, int frame_idx,
                       float *samples,
                       const double *lsps, const double *prev_lsps,
                       float *excitation, float *synth)
{
    WMAVoiceContext *s = ctx->priv_data;
    int n, n_blocks_x2, log_n_blocks_x2, av_uninit(cur_pitch_val);
    int pitch[MAX_BLOCKS], av_uninit(last_block_pitch);

    /* Parse frame type ("frame header"), see frame_descs */
    int bd_idx = s->vbm_tree[get_vlc2(gb, frame_type_vlc.table, 6, 3)], block_nsamples;

    if (bd_idx < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid frame type VLC code, skipping\n");
        return AVERROR_INVALIDDATA;
    }

    block_nsamples = MAX_FRAMESIZE / frame_descs[bd_idx].n_blocks;

    /* Pitch calculation for ACB_TYPE_ASYMMETRIC ("pitch-per-frame") */
    if (frame_descs[bd_idx].acb_type == ACB_TYPE_ASYMMETRIC) {
        /* Pitch is provided per frame, which is interpreted as the pitch of
         * the last sample of the last block of this frame. We can interpolate
         * the pitch of other blocks (and even pitch-per-sample) by gradually
         * incrementing/decrementing prev_frame_pitch to cur_pitch_val. */
        n_blocks_x2      = frame_descs[bd_idx].n_blocks << 1;
        log_n_blocks_x2  = frame_descs[bd_idx].log_n_blocks + 1;
        cur_pitch_val    = s->min_pitch_val + get_bits(gb, s->pitch_nbits);
        cur_pitch_val    = FFMIN(cur_pitch_val, s->max_pitch_val - 1);
        if (s->last_acb_type == ACB_TYPE_NONE ||
            20 * abs(cur_pitch_val - s->last_pitch_val) >
                (cur_pitch_val + s->last_pitch_val))
            s->last_pitch_val = cur_pitch_val;

        /* pitch per block */
        for (n = 0; n < frame_descs[bd_idx].n_blocks; n++) {
            int fac = n * 2 + 1;

            pitch[n] = (MUL16(fac,                 cur_pitch_val) +
                        MUL16((n_blocks_x2 - fac), s->last_pitch_val) +
                        frame_descs[bd_idx].n_blocks) >> log_n_blocks_x2;
        }

        /* "pitch-diff-per-sample" for calculation of pitch per sample */
        s->pitch_diff_sh16 =
            ((cur_pitch_val - s->last_pitch_val) << 16) / MAX_FRAMESIZE;
    }

    /* Global gain (if silence) and pitch-adaptive window coordinates */
    switch (frame_descs[bd_idx].fcb_type) {
    case FCB_TYPE_SILENCE:
        s->silence_gain = wmavoice_gain_silence[get_bits(gb, 8)];
        break;
    case FCB_TYPE_AW_PULSES:
        aw_parse_coords(s, gb, pitch);
        break;
    }

    for (n = 0; n < frame_descs[bd_idx].n_blocks; n++) {
        int bl_pitch_sh2;

        /* Pitch calculation for ACB_TYPE_HAMMING ("pitch-per-block") */
        switch (frame_descs[bd_idx].acb_type) {
        case ACB_TYPE_HAMMING: {
            /* Pitch is given per block. Per-block pitches are encoded as an
             * absolute value for the first block, and then delta values
             * relative to this value) for all subsequent blocks. The scale of
             * this pitch value is semi-logarithmic compared to its use in the
             * decoder, so we convert it to normal scale also. */
            int block_pitch,
                t1 = (s->block_conv_table[1] - s->block_conv_table[0]) << 2,
                t2 = (s->block_conv_table[2] - s->block_conv_table[1]) << 1,
                t3 =  s->block_conv_table[3] - s->block_conv_table[2] + 1;

            if (n == 0) {
                block_pitch = get_bits(gb, s->block_pitch_nbits);
            } else
                block_pitch = last_block_pitch - s->block_delta_pitch_hrange +
                                 get_bits(gb, s->block_delta_pitch_nbits);
            /* Convert last_ so that any next delta is within _range */
            last_block_pitch = av_clip(block_pitch,
                                       s->block_delta_pitch_hrange,
                                       s->block_pitch_range -
                                           s->block_delta_pitch_hrange);

            /* Convert semi-log-style scale back to normal scale */
            if (block_pitch < t1) {
                bl_pitch_sh2 = (s->block_conv_table[0] << 2) + block_pitch;
            } else {
                block_pitch -= t1;
                if (block_pitch < t2) {
                    bl_pitch_sh2 =
                        (s->block_conv_table[1] << 2) + (block_pitch << 1);
                } else {
                    block_pitch -= t2;
                    if (block_pitch < t3) {
                        bl_pitch_sh2 =
                            (s->block_conv_table[2] + block_pitch) << 2;
                    } else
                        bl_pitch_sh2 = s->block_conv_table[3] << 2;
                }
            }
            pitch[n] = bl_pitch_sh2 >> 2;
            break;
        }

        case ACB_TYPE_ASYMMETRIC: {
            bl_pitch_sh2 = pitch[n] << 2;
            break;
        }

        default: // ACB_TYPE_NONE has no pitch
            bl_pitch_sh2 = 0;
            break;
        }

        synth_block(s, gb, n, block_nsamples, bl_pitch_sh2,
                    lsps, prev_lsps, &frame_descs[bd_idx],
                    &excitation[n * block_nsamples],
                    &synth[n * block_nsamples]);
    }

    /* Averaging projection filter, if applicable. Else, just copy samples
     * from synthesis buffer */
    if (s->do_apf) {
        double i_lsps[MAX_LSPS];
        float lpcs[MAX_LSPS];

        for (n = 0; n < s->lsps; n++) // LSF -> LSP
            i_lsps[n] = cos(0.5 * (prev_lsps[n] + lsps[n]));
        ff_acelp_lspd2lpc(i_lsps, lpcs, s->lsps >> 1);
        postfilter(s, synth, samples, 80, lpcs,
                   &s->zero_exc_pf[s->history_nsamples + MAX_FRAMESIZE * frame_idx],
                   frame_descs[bd_idx].fcb_type, pitch[0]);

        for (n = 0; n < s->lsps; n++) // LSF -> LSP
            i_lsps[n] = cos(lsps[n]);
        ff_acelp_lspd2lpc(i_lsps, lpcs, s->lsps >> 1);
        postfilter(s, &synth[80], &samples[80], 80, lpcs,
                   &s->zero_exc_pf[s->history_nsamples + MAX_FRAMESIZE * frame_idx + 80],
                   frame_descs[bd_idx].fcb_type, pitch[0]);
    } else
        memcpy(samples, synth, 160 * sizeof(synth[0]));

    /* Cache values for next frame */
    s->frame_cntr++;
    if (s->frame_cntr >= 0xFFFF) s->frame_cntr -= 0xFFFF; // i.e. modulo (%)
    s->last_acb_type = frame_descs[bd_idx].acb_type;
    switch (frame_descs[bd_idx].acb_type) {
    case ACB_TYPE_NONE:
        s->last_pitch_val = 0;
        break;
    case ACB_TYPE_ASYMMETRIC:
        s->last_pitch_val = cur_pitch_val;
        break;
    case ACB_TYPE_HAMMING:
        s->last_pitch_val = pitch[frame_descs[bd_idx].n_blocks - 1];
        break;
    }

    return 0;
}

/**
 * Ensure minimum value for first item, maximum value for last value,
 * proper spacing between each value and proper ordering.
 *
 * @param lsps array of LSPs
 * @param num size of LSP array
 *
 * @note basically a double version of #ff_acelp_reorder_lsf(), might be
 *       useful to put in a generic location later on. Parts are also
 *       present in #ff_set_min_dist_lsf() + #ff_sort_nearly_sorted_floats(),
 *       which is in float.
 */
static void stabilize_lsps(double *lsps, int num)
{
    int n, m, l;

    /* set minimum value for first, maximum value for last and minimum
     * spacing between LSF values.
     * Very similar to ff_set_min_dist_lsf(), but in double. */
    lsps[0]       = FFMAX(lsps[0],       0.0015 * M_PI);
    for (n = 1; n < num; n++)
        lsps[n]   = FFMAX(lsps[n],       lsps[n - 1] + 0.0125 * M_PI);
    lsps[num - 1] = FFMIN(lsps[num - 1], 0.9985 * M_PI);

    /* reorder (looks like one-time / non-recursed bubblesort).
     * Very similar to ff_sort_nearly_sorted_floats(), but in double. */
    for (n = 1; n < num; n++) {
        if (lsps[n] < lsps[n - 1]) {
            for (m = 1; m < num; m++) {
                double tmp = lsps[m];
                for (l = m - 1; l >= 0; l--) {
                    if (lsps[l] <= tmp) break;
                    lsps[l + 1] = lsps[l];
                }
                lsps[l + 1] = tmp;
            }
            break;
        }
    }
}

/**
 * Synthesize output samples for a single superframe. If we have any data
 * cached in s->sframe_cache, that will be used instead of whatever is loaded
 * in s->gb.
 *
 * WMA Voice superframes contain 3 frames, each containing 160 audio samples,
 * to give a total of 480 samples per frame. See #synth_frame() for frame
 * parsing. In addition to 3 frames, superframes can also contain the LSPs
 * (if these are globally specified for all frames (residually); they can
 * also be specified individually per-frame. See the s->has_residual_lsps
 * option), and can specify the number of samples encoded in this superframe
 * (if less than 480), usually used to prevent blanks at track boundaries.
 *
 * @param ctx WMA Voice decoder context
 * @return 0 on success, <0 on error or 1 if there was not enough data to
 *         fully parse the superframe
 */
static int synth_superframe(AVCodecContext *ctx, AVFrame *frame,
                            int *got_frame_ptr)
{
    WMAVoiceContext *s = ctx->priv_data;
    GetBitContext *gb = &s->gb, s_gb;
    int n, res, n_samples = MAX_SFRAMESIZE;
    double lsps[MAX_FRAMES][MAX_LSPS];
    const double *mean_lsf = s->lsps == 16 ?
        wmavoice_mean_lsf16[s->lsp_def_mode] : wmavoice_mean_lsf10[s->lsp_def_mode];
    float excitation[MAX_SIGNAL_HISTORY + MAX_SFRAMESIZE + 12];
    float synth[MAX_LSPS + MAX_SFRAMESIZE];
    float *samples;

    memcpy(synth,      s->synth_history,
           s->lsps             * sizeof(*synth));
    memcpy(excitation, s->excitation_history,
           s->history_nsamples * sizeof(*excitation));

    if (s->sframe_cache_size > 0) {
        gb = &s_gb;
        init_get_bits(gb, s->sframe_cache, s->sframe_cache_size);
        s->sframe_cache_size = 0;
    }

    /* First bit is speech/music bit, it differentiates between WMAVoice
     * speech samples (the actual codec) and WMAVoice music samples, which
     * are really WMAPro-in-WMAVoice-superframes. I've never seen those in
     * the wild yet. */
    if (!get_bits1(gb)) {
        avpriv_request_sample(ctx, "WMAPro-in-WMAVoice");
        return AVERROR_PATCHWELCOME;
    }

    /* (optional) nr. of samples in superframe; always <= 480 and >= 0 */
    if (get_bits1(gb)) {
        if ((n_samples = get_bits(gb, 12)) > MAX_SFRAMESIZE) {
            av_log(ctx, AV_LOG_ERROR,
                   "Superframe encodes > %d samples (%d), not allowed\n",
                   MAX_SFRAMESIZE, n_samples);
            return AVERROR_INVALIDDATA;
        }
    }

    /* Parse LSPs, if global for the superframe (can also be per-frame). */
    if (s->has_residual_lsps) {
        double prev_lsps[MAX_LSPS], a1[MAX_LSPS * 2], a2[MAX_LSPS * 2];

        for (n = 0; n < s->lsps; n++)
            prev_lsps[n] = s->prev_lsps[n] - mean_lsf[n];

        if (s->lsps == 10) {
            dequant_lsp10r(gb, lsps[2], prev_lsps, a1, a2, s->lsp_q_mode);
        } else /* s->lsps == 16 */
            dequant_lsp16r(gb, lsps[2], prev_lsps, a1, a2, s->lsp_q_mode);

        for (n = 0; n < s->lsps; n++) {
            lsps[0][n]  = mean_lsf[n] + (a1[n]           - a2[n * 2]);
            lsps[1][n]  = mean_lsf[n] + (a1[s->lsps + n] - a2[n * 2 + 1]);
            lsps[2][n] += mean_lsf[n];
        }
        for (n = 0; n < 3; n++)
            stabilize_lsps(lsps[n], s->lsps);
    }

    /* synth_superframe can run multiple times per packet
     * free potential previous frame */
    av_frame_unref(frame);

    /* get output buffer */
    frame->nb_samples = MAX_SFRAMESIZE;
    if ((res = ff_get_buffer(ctx, frame, 0)) < 0)
        return res;
    frame->nb_samples = n_samples;
    samples = (float *)frame->data[0];

    /* Parse frames, optionally preceded by per-frame (independent) LSPs. */
    for (n = 0; n < 3; n++) {
        if (!s->has_residual_lsps) {
            int m;

            if (s->lsps == 10) {
                dequant_lsp10i(gb, lsps[n]);
            } else /* s->lsps == 16 */
                dequant_lsp16i(gb, lsps[n]);

            for (m = 0; m < s->lsps; m++)
                lsps[n][m] += mean_lsf[m];
            stabilize_lsps(lsps[n], s->lsps);
        }

        if ((res = synth_frame(ctx, gb, n,
                               &samples[n * MAX_FRAMESIZE],
                               lsps[n], n == 0 ? s->prev_lsps : lsps[n - 1],
                               &excitation[s->history_nsamples + n * MAX_FRAMESIZE],
                               &synth[s->lsps + n * MAX_FRAMESIZE]))) {
            *got_frame_ptr = 0;
            return res;
        }
    }

    /* Statistics? FIXME - we don't check for length, a slight overrun
     * will be caught by internal buffer padding, and anything else
     * will be skipped, not read. */
    if (get_bits1(gb)) {
        res = get_bits(gb, 4);
        skip_bits(gb, 10 * (res + 1));
    }

    if (get_bits_left(gb) < 0) {
        wmavoice_flush(ctx);
        return AVERROR_INVALIDDATA;
    }

    *got_frame_ptr = 1;

    /* Update history */
    memcpy(s->prev_lsps,           lsps[2],
           s->lsps             * sizeof(*s->prev_lsps));
    memcpy(s->synth_history,      &synth[MAX_SFRAMESIZE],
           s->lsps             * sizeof(*synth));
    memcpy(s->excitation_history, &excitation[MAX_SFRAMESIZE],
           s->history_nsamples * sizeof(*excitation));
    if (s->do_apf)
        memmove(s->zero_exc_pf,       &s->zero_exc_pf[MAX_SFRAMESIZE],
                s->history_nsamples * sizeof(*s->zero_exc_pf));

    return 0;
}

/**
 * Parse the packet header at the start of each packet (input data to this
 * decoder).
 *
 * @param s WMA Voice decoding context private data
 * @return <0 on error, nb_superframes on success.
 */
static int parse_packet_header(WMAVoiceContext *s)
{
    GetBitContext *gb = &s->gb;
    unsigned int res, n_superframes = 0;

    skip_bits(gb, 4);          // packet sequence number
    s->has_residual_lsps = get_bits1(gb);
    do {
        res = get_bits(gb, 6); // number of superframes per packet
                               // (minus first one if there is spillover)
        n_superframes += res;
    } while (res == 0x3F);
    s->spillover_nbits   = get_bits(gb, s->spillover_bitsize);

    return get_bits_left(gb) >= 0 ? n_superframes : AVERROR_INVALIDDATA;
}

/**
 * Copy (unaligned) bits from gb/data/size to pb.
 *
 * @param pb target buffer to copy bits into
 * @param data source buffer to copy bits from
 * @param size size of the source data, in bytes
 * @param gb bit I/O context specifying the current position in the source.
 *           data. This function might use this to align the bit position to
 *           a whole-byte boundary before calling #avpriv_copy_bits() on aligned
 *           source data
 * @param nbits the amount of bits to copy from source to target
 *
 * @note after calling this function, the current position in the input bit
 *       I/O context is undefined.
 */
static void copy_bits(PutBitContext *pb,
                      const uint8_t *data, int size,
                      GetBitContext *gb, int nbits)
{
    int rmn_bytes, rmn_bits;

    rmn_bits = rmn_bytes = get_bits_left(gb);
    if (rmn_bits < nbits)
        return;
    if (nbits > pb->size_in_bits - put_bits_count(pb))
        return;
    rmn_bits &= 7; rmn_bytes >>= 3;
    if ((rmn_bits = FFMIN(rmn_bits, nbits)) > 0)
        put_bits(pb, rmn_bits, get_bits(gb, rmn_bits));
    avpriv_copy_bits(pb, data + size - rmn_bytes,
                 FFMIN(nbits - rmn_bits, rmn_bytes << 3));
}

/**
 * Packet decoding: a packet is anything that the (ASF) demuxer contains,
 * and we expect that the demuxer / application provides it to us as such
 * (else you'll probably get garbage as output). Every packet has a size of
 * ctx->block_align bytes, starts with a packet header (see
 * #parse_packet_header()), and then a series of superframes. Superframe
 * boundaries may exceed packets, i.e. superframes can split data over
 * multiple (two) packets.
 *
 * For more information about frames, see #synth_superframe().
 */
static int wmavoice_decode_packet(AVCodecContext *ctx, void *data,
                                  int *got_frame_ptr, AVPacket *avpkt)
{
    WMAVoiceContext *s = ctx->priv_data;
    GetBitContext *gb = &s->gb;
    int size, res, pos;

    /* Packets are sometimes a multiple of ctx->block_align, with a packet
     * header at each ctx->block_align bytes. However, FFmpeg's ASF demuxer
     * feeds us ASF packets, which may concatenate multiple "codec" packets
     * in a single "muxer" packet, so we artificially emulate that by
     * capping the packet size at ctx->block_align. */
    for (size = avpkt->size; size > ctx->block_align; size -= ctx->block_align);
    init_get_bits(&s->gb, avpkt->data, size << 3);

    /* size == ctx->block_align is used to indicate whether we are dealing with
     * a new packet or a packet of which we already read the packet header
     * previously. */
    if (!(size % ctx->block_align)) { // new packet header
        if (!size) {
            s->spillover_nbits = 0;
            s->nb_superframes = 0;
        } else {
            if ((res = parse_packet_header(s)) < 0)
                return res;
            s->nb_superframes = res;
        }

        /* If the packet header specifies a s->spillover_nbits, then we want
         * to push out all data of the previous packet (+ spillover) before
         * continuing to parse new superframes in the current packet. */
        if (s->sframe_cache_size > 0) {
            int cnt = get_bits_count(gb);
            if (cnt + s->spillover_nbits > avpkt->size * 8) {
                s->spillover_nbits = avpkt->size * 8 - cnt;
            }
            copy_bits(&s->pb, avpkt->data, size, gb, s->spillover_nbits);
            flush_put_bits(&s->pb);
            s->sframe_cache_size += s->spillover_nbits;
            if ((res = synth_superframe(ctx, data, got_frame_ptr)) == 0 &&
                *got_frame_ptr) {
                cnt += s->spillover_nbits;
                s->skip_bits_next = cnt & 7;
                res = cnt >> 3;
                return res;
            } else
                skip_bits_long (gb, s->spillover_nbits - cnt +
                                get_bits_count(gb)); // resync
        } else if (s->spillover_nbits) {
            skip_bits_long(gb, s->spillover_nbits);  // resync
        }
    } else if (s->skip_bits_next)
        skip_bits(gb, s->skip_bits_next);

    /* Try parsing superframes in current packet */
    s->sframe_cache_size = 0;
    s->skip_bits_next = 0;
    pos = get_bits_left(gb);
    if (s->nb_superframes-- == 0) {
        *got_frame_ptr = 0;
        return size;
    } else if (s->nb_superframes > 0) {
        if ((res = synth_superframe(ctx, data, got_frame_ptr)) < 0) {
            return res;
        } else if (*got_frame_ptr) {
            int cnt = get_bits_count(gb);
            s->skip_bits_next = cnt & 7;
            res = cnt >> 3;
            return res;
        }
    } else if ((s->sframe_cache_size = pos) > 0) {
        /* ... cache it for spillover in next packet */
        init_put_bits(&s->pb, s->sframe_cache, SFRAME_CACHE_MAXSIZE);
        copy_bits(&s->pb, avpkt->data, size, gb, s->sframe_cache_size);
        // FIXME bad - just copy bytes as whole and add use the
        // skip_bits_next field
    }

    return size;
}

static av_cold int wmavoice_decode_end(AVCodecContext *ctx)
{
    WMAVoiceContext *s = ctx->priv_data;

    if (s->do_apf) {
        ff_rdft_end(&s->rdft);
        ff_rdft_end(&s->irdft);
        ff_dct_end(&s->dct);
        ff_dct_end(&s->dst);
    }

    return 0;
}

AVCodec ff_wmavoice_decoder = {
    .name             = "wmavoice",
    .long_name        = NULL_IF_CONFIG_SMALL("Windows Media Audio Voice"),
    .type             = AVMEDIA_TYPE_AUDIO,
    .id               = AV_CODEC_ID_WMAVOICE,
    .priv_data_size   = sizeof(WMAVoiceContext),
    .init             = wmavoice_decode_init,
    .close            = wmavoice_decode_end,
    .decode           = wmavoice_decode_packet,
    .capabilities     = AV_CODEC_CAP_SUBFRAMES | AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .flush            = wmavoice_flush,
};
