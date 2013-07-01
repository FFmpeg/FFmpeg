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

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "avcodec.h"
#include "fft.h"
#include "get_bits.h"
#include "dcadata.h"
#include "dcahuff.h"
#include "dca.h"
#include "mathops.h"
#include "synth_filter.h"
#include "dcadsp.h"
#include "fmtconvert.h"
#include "internal.h"

#if ARCH_ARM
#   include "arm/dca.h"
#endif

//#define TRACE

#define DCA_PRIM_CHANNELS_MAX  (7)
#define DCA_SUBBANDS          (64)
#define DCA_ABITS_MAX         (32)      /* Should be 28 */
#define DCA_SUBSUBFRAMES_MAX   (4)
#define DCA_SUBFRAMES_MAX     (16)
#define DCA_BLOCKS_MAX        (16)
#define DCA_LFE_MAX            (3)
#define DCA_CHSETS_MAX         (4)
#define DCA_CHSET_CHANS_MAX    (8)

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

/* these are unconfirmed but should be mostly correct */
enum DCAExSSSpeakerMask {
    DCA_EXSS_FRONT_CENTER          = 0x0001,
    DCA_EXSS_FRONT_LEFT_RIGHT      = 0x0002,
    DCA_EXSS_SIDE_REAR_LEFT_RIGHT  = 0x0004,
    DCA_EXSS_LFE                   = 0x0008,
    DCA_EXSS_REAR_CENTER           = 0x0010,
    DCA_EXSS_FRONT_HIGH_LEFT_RIGHT = 0x0020,
    DCA_EXSS_REAR_LEFT_RIGHT       = 0x0040,
    DCA_EXSS_FRONT_HIGH_CENTER     = 0x0080,
    DCA_EXSS_OVERHEAD              = 0x0100,
    DCA_EXSS_CENTER_LEFT_RIGHT     = 0x0200,
    DCA_EXSS_WIDE_LEFT_RIGHT       = 0x0400,
    DCA_EXSS_SIDE_LEFT_RIGHT       = 0x0800,
    DCA_EXSS_LFE2                  = 0x1000,
    DCA_EXSS_SIDE_HIGH_LEFT_RIGHT  = 0x2000,
    DCA_EXSS_REAR_HIGH_CENTER      = 0x4000,
    DCA_EXSS_REAR_HIGH_LEFT_RIGHT  = 0x8000,
};

enum DCAXxchSpeakerMask {
    DCA_XXCH_FRONT_CENTER          = 0x0000001,
    DCA_XXCH_FRONT_LEFT            = 0x0000002,
    DCA_XXCH_FRONT_RIGHT           = 0x0000004,
    DCA_XXCH_SIDE_REAR_LEFT        = 0x0000008,
    DCA_XXCH_SIDE_REAR_RIGHT       = 0x0000010,
    DCA_XXCH_LFE1                  = 0x0000020,
    DCA_XXCH_REAR_CENTER           = 0x0000040,
    DCA_XXCH_SURROUND_REAR_LEFT    = 0x0000080,
    DCA_XXCH_SURROUND_REAR_RIGHT   = 0x0000100,
    DCA_XXCH_SIDE_SURROUND_LEFT    = 0x0000200,
    DCA_XXCH_SIDE_SURROUND_RIGHT   = 0x0000400,
    DCA_XXCH_FRONT_CENTER_LEFT     = 0x0000800,
    DCA_XXCH_FRONT_CENTER_RIGHT    = 0x0001000,
    DCA_XXCH_FRONT_HIGH_LEFT       = 0x0002000,
    DCA_XXCH_FRONT_HIGH_CENTER     = 0x0004000,
    DCA_XXCH_FRONT_HIGH_RIGHT      = 0x0008000,
    DCA_XXCH_LFE2                  = 0x0010000,
    DCA_XXCH_SIDE_FRONT_LEFT       = 0x0020000,
    DCA_XXCH_SIDE_FRONT_RIGHT      = 0x0040000,
    DCA_XXCH_OVERHEAD              = 0x0080000,
    DCA_XXCH_SIDE_HIGH_LEFT        = 0x0100000,
    DCA_XXCH_SIDE_HIGH_RIGHT       = 0x0200000,
    DCA_XXCH_REAR_HIGH_CENTER      = 0x0400000,
    DCA_XXCH_REAR_HIGH_LEFT        = 0x0800000,
    DCA_XXCH_REAR_HIGH_RIGHT       = 0x1000000,
    DCA_XXCH_REAR_LOW_CENTER       = 0x2000000,
    DCA_XXCH_REAR_LOW_LEFT         = 0x4000000,
    DCA_XXCH_REAR_LOW_RIGHT        = 0x8000000,
};

static const uint32_t map_xxch_to_native[28] = {
    AV_CH_FRONT_CENTER,
    AV_CH_FRONT_LEFT,
    AV_CH_FRONT_RIGHT,
    AV_CH_SIDE_LEFT,
    AV_CH_SIDE_RIGHT,
    AV_CH_LOW_FREQUENCY,
    AV_CH_BACK_CENTER,
    AV_CH_BACK_LEFT,
    AV_CH_BACK_RIGHT,
    AV_CH_SIDE_LEFT,           /* side surround left -- dup sur side L */
    AV_CH_SIDE_RIGHT,          /* side surround right -- dup sur side R */
    AV_CH_FRONT_LEFT_OF_CENTER,
    AV_CH_FRONT_RIGHT_OF_CENTER,
    AV_CH_TOP_FRONT_LEFT,
    AV_CH_TOP_FRONT_CENTER,
    AV_CH_TOP_FRONT_RIGHT,
    AV_CH_LOW_FREQUENCY,        /* lfe2 -- duplicate lfe1 position */
    AV_CH_FRONT_LEFT_OF_CENTER, /* side front left -- dup front cntr L */
    AV_CH_FRONT_RIGHT_OF_CENTER,/* side front right -- dup front cntr R */
    AV_CH_TOP_CENTER,           /* overhead */
    AV_CH_TOP_FRONT_LEFT,       /* side high left -- dup */
    AV_CH_TOP_FRONT_RIGHT,      /* side high right -- dup */
    AV_CH_TOP_BACK_CENTER,
    AV_CH_TOP_BACK_LEFT,
    AV_CH_TOP_BACK_RIGHT,
    AV_CH_BACK_CENTER,          /* rear low center -- dup */
    AV_CH_BACK_LEFT,            /* rear low left -- dup */
    AV_CH_BACK_RIGHT            /* read low right -- dup  */
};

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

/* extensions that reside in core substream */
#define DCA_CORE_EXTS (DCA_EXT_XCH | DCA_EXT_XXCH | DCA_EXT_X96)

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

static const int8_t dca_lfe_index[] = {
    1, 2, 2, 2, 2, 3, 2, 3, 2, 3, 2, 3, 1, 3, 2, 3
};

static const int8_t dca_channel_reorder_lfe[][9] = {
    { 0, -1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 2,  0,  1, -1, -1, -1, -1, -1, -1},
    { 0,  1,  3, -1, -1, -1, -1, -1, -1},
    { 2,  0,  1,  4, -1, -1, -1, -1, -1},
    { 0,  1,  3,  4, -1, -1, -1, -1, -1},
    { 2,  0,  1,  4,  5, -1, -1, -1, -1},
    { 3,  4,  0,  1,  5,  6, -1, -1, -1},
    { 2,  0,  1,  4,  5,  6, -1, -1, -1},
    { 0,  6,  4,  5,  2,  3, -1, -1, -1},
    { 4,  2,  5,  0,  1,  6,  7, -1, -1},
    { 5,  6,  0,  1,  7,  3,  8,  4, -1},
    { 4,  2,  5,  0,  1,  6,  8,  7, -1},
};

static const int8_t dca_channel_reorder_lfe_xch[][9] = {
    { 0,  2, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1,  3, -1, -1, -1, -1, -1, -1},
    { 0,  1,  3, -1, -1, -1, -1, -1, -1},
    { 0,  1,  3, -1, -1, -1, -1, -1, -1},
    { 0,  1,  3, -1, -1, -1, -1, -1, -1},
    { 2,  0,  1,  4, -1, -1, -1, -1, -1},
    { 0,  1,  3,  4, -1, -1, -1, -1, -1},
    { 2,  0,  1,  4,  5, -1, -1, -1, -1},
    { 0,  1,  4,  5,  3, -1, -1, -1, -1},
    { 2,  0,  1,  5,  6,  4, -1, -1, -1},
    { 3,  4,  0,  1,  6,  7,  5, -1, -1},
    { 2,  0,  1,  4,  5,  6,  7, -1, -1},
    { 0,  6,  4,  5,  2,  3,  7, -1, -1},
    { 4,  2,  5,  0,  1,  7,  8,  6, -1},
    { 5,  6,  0,  1,  8,  3,  9,  4,  7},
    { 4,  2,  5,  0,  1,  6,  9,  8,  7},
};

static const int8_t dca_channel_reorder_nolfe[][9] = {
    { 0, -1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 2,  0,  1, -1, -1, -1, -1, -1, -1},
    { 0,  1,  2, -1, -1, -1, -1, -1, -1},
    { 2,  0,  1,  3, -1, -1, -1, -1, -1},
    { 0,  1,  2,  3, -1, -1, -1, -1, -1},
    { 2,  0,  1,  3,  4, -1, -1, -1, -1},
    { 2,  3,  0,  1,  4,  5, -1, -1, -1},
    { 2,  0,  1,  3,  4,  5, -1, -1, -1},
    { 0,  5,  3,  4,  1,  2, -1, -1, -1},
    { 3,  2,  4,  0,  1,  5,  6, -1, -1},
    { 4,  5,  0,  1,  6,  2,  7,  3, -1},
    { 3,  2,  4,  0,  1,  5,  7,  6, -1},
};

static const int8_t dca_channel_reorder_nolfe_xch[][9] = {
    { 0,  1, -1, -1, -1, -1, -1, -1, -1},
    { 0,  1,  2, -1, -1, -1, -1, -1, -1},
    { 0,  1,  2, -1, -1, -1, -1, -1, -1},
    { 0,  1,  2, -1, -1, -1, -1, -1, -1},
    { 0,  1,  2, -1, -1, -1, -1, -1, -1},
    { 2,  0,  1,  3, -1, -1, -1, -1, -1},
    { 0,  1,  2,  3, -1, -1, -1, -1, -1},
    { 2,  0,  1,  3,  4, -1, -1, -1, -1},
    { 0,  1,  3,  4,  2, -1, -1, -1, -1},
    { 2,  0,  1,  4,  5,  3, -1, -1, -1},
    { 2,  3,  0,  1,  5,  6,  4, -1, -1},
    { 2,  0,  1,  3,  4,  5,  6, -1, -1},
    { 0,  5,  3,  4,  1,  2,  6, -1, -1},
    { 3,  2,  4,  0,  1,  6,  7,  5, -1},
    { 4,  5,  0,  1,  7,  2,  8,  3,  6},
    { 3,  2,  4,  0,  1,  5,  8,  7,  6},
};

#define DCA_DOLBY                  101           /* FIXME */

#define DCA_CHANNEL_BITS             6
#define DCA_CHANNEL_MASK          0x3F

#define DCA_LFE                   0x80

#define HEADER_SIZE                 14

#define DCA_MAX_FRAME_SIZE       16384
#define DCA_MAX_EXSS_HEADER_SIZE  4096

#define DCA_BUFFER_PADDING_SIZE   1024

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

static av_always_inline int get_bitalloc(GetBitContext *gb, BitAlloc *ba,
                                         int idx)
{
    return get_vlc2(gb, ba->vlc[idx].table, ba->vlc[idx].bits, ba->wrap) +
           ba->offset;
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
    int bit_rate_index;         ///< transmission bit rate index

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
    int total_channels;         ///< number of channels including extensions
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
    int subsubframes[DCA_SUBFRAMES_MAX];                         ///< number of subsubframes
    int partial_samples[DCA_SUBFRAMES_MAX];                      ///< partial subsubframe samples count
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

    float lfe_data[2 * DCA_LFE_MAX * (DCA_BLOCKS_MAX + 4)];      ///< Low frequency effect data
    int lfe_scale_factor;

    /* Subband samples history (for ADPCM) */
    DECLARE_ALIGNED(16, float, subband_samples_hist)[DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS][4];
    DECLARE_ALIGNED(32, float, subband_fir_hist)[DCA_PRIM_CHANNELS_MAX][512];
    DECLARE_ALIGNED(32, float, subband_fir_noidea)[DCA_PRIM_CHANNELS_MAX][32];
    int hist_index[DCA_PRIM_CHANNELS_MAX];
    DECLARE_ALIGNED(32, float, raXin)[32];

    int output;                 ///< type of output

    DECLARE_ALIGNED(32, float, subband_samples)[DCA_BLOCKS_MAX][DCA_PRIM_CHANNELS_MAX][DCA_SUBBANDS][8];
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

    /* XCh extension information */
    int xch_present;            ///< XCh extension present and valid
    int xch_base_channel;       ///< index of first (only) channel containing XCH data

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

    /* ExSS header parser */
    int static_fields;          ///< static fields present
    int mix_metadata;           ///< mixing metadata present
    int num_mix_configs;        ///< number of mix out configurations
    int mix_config_num_ch[4];   ///< number of channels in each mix out configuration

    int profile;

    int debug_flag;             ///< used for suppressing repeated error messages output
    AVFloatDSPContext fdsp;
    FFTContext imdct;
    SynthFilterContext synth;
    DCADSPContext dcadsp;
    FmtConvertContext fmt_conv;
} DCAContext;

static const uint16_t dca_vlc_offs[] = {
        0,   512,   640,   768,  1282,  1794,  2436,  3080,  3770,  4454,  5364,
     5372,  5380,  5388,  5392,  5396,  5412,  5420,  5428,  5460,  5492,  5508,
     5572,  5604,  5668,  5796,  5860,  5892,  6412,  6668,  6796,  7308,  7564,
     7820,  8076,  8620,  9132,  9388,  9910, 10166, 10680, 11196, 11726, 12240,
    12752, 13298, 13810, 14326, 14840, 15500, 16022, 16540, 17158, 17678, 18264,
    18796, 19352, 19926, 20468, 21472, 22398, 23014, 23622,
};

static av_cold void dca_init_vlcs(void)
{
    static int vlcs_initialized = 0;
    int i, j, c = 14;
    static VLC_TYPE dca_table[23622][2];

    if (vlcs_initialized)
        return;

    dca_bitalloc_index.offset = 1;
    dca_bitalloc_index.wrap = 2;
    for (i = 0; i < 5; i++) {
        dca_bitalloc_index.vlc[i].table = &dca_table[dca_vlc_offs[i]];
        dca_bitalloc_index.vlc[i].table_allocated = dca_vlc_offs[i + 1] - dca_vlc_offs[i];
        init_vlc(&dca_bitalloc_index.vlc[i], bitalloc_12_vlc_bits[i], 12,
                 bitalloc_12_bits[i], 1, 1,
                 bitalloc_12_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
    }
    dca_scalefactor.offset = -64;
    dca_scalefactor.wrap = 2;
    for (i = 0; i < 5; i++) {
        dca_scalefactor.vlc[i].table = &dca_table[dca_vlc_offs[i + 5]];
        dca_scalefactor.vlc[i].table_allocated = dca_vlc_offs[i + 6] - dca_vlc_offs[i + 5];
        init_vlc(&dca_scalefactor.vlc[i], SCALES_VLC_BITS, 129,
                 scales_bits[i], 1, 1,
                 scales_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
    }
    dca_tmode.offset = 0;
    dca_tmode.wrap = 1;
    for (i = 0; i < 4; i++) {
        dca_tmode.vlc[i].table = &dca_table[dca_vlc_offs[i + 10]];
        dca_tmode.vlc[i].table_allocated = dca_vlc_offs[i + 11] - dca_vlc_offs[i + 10];
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
            dca_smpl_bitalloc[i + 1].vlc[j].table           = &dca_table[dca_vlc_offs[c]];
            dca_smpl_bitalloc[i + 1].vlc[j].table_allocated = dca_vlc_offs[c + 1] - dca_vlc_offs[c];

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

static inline int dca_xxch2index(DCAContext *s, int xxch_ch)
{
    int i, base, mask;

    /* locate channel set containing the channel */
    for (i = -1, base = 0, mask = (s->xxch_core_spkmask & ~DCA_XXCH_LFE1);
         i <= s->xxch_chset && !(mask & xxch_ch); mask = s->xxch_spk_masks[++i])
        base += av_popcount(mask);

    return base + av_popcount(mask & (xxch_ch - 1));
}

static int dca_parse_audio_coding_header(DCAContext *s, int base_channel,
                                         int xxch)
{
    int i, j;
    static const float adj_table[4] = { 1.0, 1.1250, 1.2500, 1.4375 };
    static const int bitlen[11] = { 0, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3 };
    static const int thr[11]    = { 0, 1, 3, 3, 3, 3, 7, 7, 7, 7, 7 };
    int hdr_pos = 0, hdr_size = 0;
    float sign, mag, scale_factor;
    int this_chans, acc_mask;
    int embedded_downmix;
    int nchans, mask[8];
    int coeff, ichan;

    /* xxch has arbitrary sized audio coding headers */
    if (xxch) {
        hdr_pos  = get_bits_count(&s->gb);
        hdr_size = get_bits(&s->gb, 7) + 1;
    }

    nchans = get_bits(&s->gb, 3) + 1;
    s->total_channels = nchans + base_channel;
    s->prim_channels  = s->total_channels;

    /* obtain speaker layout mask & downmix coefficients for XXCH */
    if (xxch) {
        acc_mask = s->xxch_core_spkmask;

        this_chans = get_bits(&s->gb, s->xxch_nbits_spk_mask - 6) << 6;
        s->xxch_spk_masks[s->xxch_chset] = this_chans;
        s->xxch_chset_nch[s->xxch_chset] = nchans;

        for (i = 0; i <= s->xxch_chset; i++)
            acc_mask |= s->xxch_spk_masks[i];

        /* check for downmixing information */
        if (get_bits1(&s->gb)) {
            embedded_downmix = get_bits1(&s->gb);
            scale_factor     =
               1.0f / dca_downmix_scale_factors[(get_bits(&s->gb, 6) - 1) << 2];

            s->xxch_dmix_sf[s->xxch_chset] = scale_factor;

            for (i = base_channel; i < s->prim_channels; i++) {
                mask[i] = get_bits(&s->gb, s->xxch_nbits_spk_mask);
            }

            for (j = base_channel; j < s->prim_channels; j++) {
                memset(s->xxch_dmix_coeff[j], 0, sizeof(s->xxch_dmix_coeff[0]));
                s->xxch_dmix_embedded |= (embedded_downmix << j);
                for (i = 0; i < s->xxch_nbits_spk_mask; i++) {
                    if (mask[j] & (1 << i)) {
                        if ((1 << i) == DCA_XXCH_LFE1) {
                            av_log(s->avctx, AV_LOG_WARNING,
                                   "DCA-XXCH: dmix to LFE1 not supported.\n");
                            continue;
                        }

                        coeff = get_bits(&s->gb, 7);
                        sign  = (coeff & 64) ? 1.0 : -1.0;
                        mag   = dca_downmix_scale_factors[((coeff & 63) - 1) << 2];
                        ichan = dca_xxch2index(s, 1 << i);
                        s->xxch_dmix_coeff[j][ichan] = sign * mag;
                    }
                }
            }
        }
    }

    if (s->prim_channels > DCA_PRIM_CHANNELS_MAX)
        s->prim_channels = DCA_PRIM_CHANNELS_MAX;


    for (i = base_channel; i < s->prim_channels; i++) {
        s->subband_activity[i] = get_bits(&s->gb, 5) + 2;
        if (s->subband_activity[i] > DCA_SUBBANDS)
            s->subband_activity[i] = DCA_SUBBANDS;
    }
    for (i = base_channel; i < s->prim_channels; i++) {
        s->vq_start_subband[i] = get_bits(&s->gb, 5) + 1;
        if (s->vq_start_subband[i] > DCA_SUBBANDS)
            s->vq_start_subband[i] = DCA_SUBBANDS;
    }
    get_array(&s->gb, s->joint_intensity + base_channel,     s->prim_channels - base_channel, 3);
    get_array(&s->gb, s->transient_huffman + base_channel,   s->prim_channels - base_channel, 2);
    get_array(&s->gb, s->scalefactor_huffman + base_channel, s->prim_channels - base_channel, 3);
    get_array(&s->gb, s->bitalloc_huffman + base_channel,    s->prim_channels - base_channel, 3);

    /* Get codebooks quantization indexes */
    if (!base_channel)
        memset(s->quant_index_huffman, 0, sizeof(s->quant_index_huffman));
    for (j = 1; j < 11; j++)
        for (i = base_channel; i < s->prim_channels; i++)
            s->quant_index_huffman[i][j] = get_bits(&s->gb, bitlen[j]);

    /* Get scale factor adjustment */
    for (j = 0; j < 11; j++)
        for (i = base_channel; i < s->prim_channels; i++)
            s->scalefactor_adj[i][j] = 1;

    for (j = 1; j < 11; j++)
        for (i = base_channel; i < s->prim_channels; i++)
            if (s->quant_index_huffman[i][j] < thr[j])
                s->scalefactor_adj[i][j] = adj_table[get_bits(&s->gb, 2)];

    if (!xxch) {
        if (s->crc_present) {
            /* Audio header CRC check */
            get_bits(&s->gb, 16);
        }
    } else {
        /* Skip to the end of the header, also ignore CRC if present  */
        i = get_bits_count(&s->gb);
        if (hdr_pos + 8 * hdr_size > i)
            skip_bits_long(&s->gb, hdr_pos + 8 * hdr_size - i);
    }

    s->current_subframe    = 0;
    s->current_subsubframe = 0;

#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "subframes: %i\n", s->subframes);
    av_log(s->avctx, AV_LOG_DEBUG, "prim channels: %i\n", s->prim_channels);
    for (i = base_channel; i < s->prim_channels; i++) {
        av_log(s->avctx, AV_LOG_DEBUG, "subband activity: %i\n",
               s->subband_activity[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "vq start subband: %i\n",
               s->vq_start_subband[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "joint intensity: %i\n",
               s->joint_intensity[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "transient mode codebook: %i\n",
               s->transient_huffman[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "scale factor codebook: %i\n",
               s->scalefactor_huffman[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "bit allocation quantizer: %i\n",
               s->bitalloc_huffman[i]);
        av_log(s->avctx, AV_LOG_DEBUG, "quant index huff:");
        for (j = 0; j < 11; j++)
            av_log(s->avctx, AV_LOG_DEBUG, " %i", s->quant_index_huffman[i][j]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
        av_log(s->avctx, AV_LOG_DEBUG, "scalefac adj:");
        for (j = 0; j < 11; j++)
            av_log(s->avctx, AV_LOG_DEBUG, " %1.3f", s->scalefactor_adj[i][j]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
#endif

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
    s->bit_rate          = dca_bit_rates[s->bit_rate_index];
    if (!s->bit_rate)
        return AVERROR_INVALIDDATA;

    s->downmix           = get_bits(&s->gb, 1); /* note: this is FixedBit == 0 */
    s->dynrange          = get_bits(&s->gb, 1);
    s->timestamp         = get_bits(&s->gb, 1);
    s->aux_data          = get_bits(&s->gb, 1);
    s->hdcd              = get_bits(&s->gb, 1);
    s->ext_descr         = get_bits(&s->gb, 3);
    s->ext_coding        = get_bits(&s->gb, 1);
    s->aspf              = get_bits(&s->gb, 1);
    s->lfe               = get_bits(&s->gb, 2);
    s->predictor_history = get_bits(&s->gb, 1);

    if (s->lfe == 3) {
        s->lfe = 0;
        avpriv_request_sample(s->avctx, "LFE = 3");
        return AVERROR_PATCHWELCOME;
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

#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "frame type: %i\n", s->frame_type);
    av_log(s->avctx, AV_LOG_DEBUG, "samples deficit: %i\n", s->samples_deficit);
    av_log(s->avctx, AV_LOG_DEBUG, "crc present: %i\n", s->crc_present);
    av_log(s->avctx, AV_LOG_DEBUG, "sample blocks: %i (%i samples)\n",
           s->sample_blocks, s->sample_blocks * 32);
    av_log(s->avctx, AV_LOG_DEBUG, "frame size: %i bytes\n", s->frame_size);
    av_log(s->avctx, AV_LOG_DEBUG, "amode: %i (%i channels)\n",
           s->amode, dca_channels[s->amode]);
    av_log(s->avctx, AV_LOG_DEBUG, "sample rate: %i Hz\n",
           s->sample_rate);
    av_log(s->avctx, AV_LOG_DEBUG, "bit rate: %i bits/s\n",
           s->bit_rate);
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

    return dca_parse_audio_coding_header(s, 0, 0);
}


static inline int get_scale(GetBitContext *gb, int level, int value, int log2range)
{
    if (level < 5) {
        /* huffman encoded */
        value += get_bitalloc(gb, &dca_scalefactor, level);
        value = av_clip(value, 0, (1 << log2range) - 1);
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

    for (j = base_channel; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++)
            s->prediction_mode[j][k] = get_bits(&s->gb, 1);
    }

    /* Get prediction codebook */
    for (j = base_channel; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++) {
            if (s->prediction_mode[j][k] > 0) {
                /* (Prediction coefficient VQ address) */
                s->prediction_vq[j][k] = get_bits(&s->gb, 12);
            }
        }
    }

    /* Bit allocation index */
    for (j = base_channel; j < s->prim_channels; j++) {
        for (k = 0; k < s->vq_start_subband[j]; k++) {
            if (s->bitalloc_huffman[j] == 6)
                s->bitalloc[j][k] = get_bits(&s->gb, 5);
            else if (s->bitalloc_huffman[j] == 5)
                s->bitalloc[j][k] = get_bits(&s->gb, 4);
            else if (s->bitalloc_huffman[j] == 7) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Invalid bit allocation index\n");
                return AVERROR_INVALIDDATA;
            } else {
                s->bitalloc[j][k] =
                    get_bitalloc(&s->gb, &dca_bitalloc_index, s->bitalloc_huffman[j]);
            }

            if (s->bitalloc[j][k] > 26) {
                av_dlog(s->avctx, "bitalloc index [%i][%i] too big (%i)\n",
                        j, k, s->bitalloc[j][k]);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    /* Transition mode */
    for (j = base_channel; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++) {
            s->transition_mode[j][k] = 0;
            if (s->subsubframes[s->current_subframe] > 1 &&
                k < s->vq_start_subband[j] && s->bitalloc[j][k] > 0) {
                s->transition_mode[j][k] =
                    get_bitalloc(&s->gb, &dca_tmode, s->transient_huffman[j]);
            }
        }
    }

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    for (j = base_channel; j < s->prim_channels; j++) {
        const uint32_t *scale_table;
        int scale_sum, log_size;

        memset(s->scale_factor[j], 0,
               s->subband_activity[j] * sizeof(s->scale_factor[0][0][0]) * 2);

        if (s->scalefactor_huffman[j] == 6) {
            scale_table = scale_factor_quant7;
            log_size = 7;
        } else {
            scale_table = scale_factor_quant6;
            log_size = 6;
        }

        /* When huffman coded, only the difference is encoded */
        scale_sum = 0;

        for (k = 0; k < s->subband_activity[j]; k++) {
            if (k >= s->vq_start_subband[j] || s->bitalloc[j][k] > 0) {
                scale_sum = get_scale(&s->gb, s->scalefactor_huffman[j], scale_sum, log_size);
                s->scale_factor[j][k][0] = scale_table[scale_sum];
            }

            if (k < s->vq_start_subband[j] && s->transition_mode[j][k]) {
                /* Get second scale factor */
                scale_sum = get_scale(&s->gb, s->scalefactor_huffman[j], scale_sum, log_size);
                s->scale_factor[j][k][1] = scale_table[scale_sum];
            }
        }
    }

    /* Joint subband scale factor codebook select */
    for (j = base_channel; j < s->prim_channels; j++) {
        /* Transmitted only if joint subband coding enabled */
        if (s->joint_intensity[j] > 0)
            s->joint_huff[j] = get_bits(&s->gb, 3);
    }

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    /* Scale factors for joint subband coding */
    for (j = base_channel; j < s->prim_channels; j++) {
        int source_channel;

        /* Transmitted only if joint subband coding enabled */
        if (s->joint_intensity[j] > 0) {
            int scale = 0;
            source_channel = s->joint_intensity[j] - 1;

            /* When huffman coded, only the difference is encoded
             * (is this valid as well for joint scales ???) */

            for (k = s->subband_activity[j]; k < s->subband_activity[source_channel]; k++) {
                scale = get_scale(&s->gb, s->joint_huff[j], 64 /* bias */, 7);
                s->joint_scale_factor[j][k] = scale;    /*joint_scale_table[scale]; */
            }

            if (!(s->debug_flag & 0x02)) {
                av_log(s->avctx, AV_LOG_DEBUG,
                       "Joint stereo coding not supported\n");
                s->debug_flag |= 0x02;
            }
        }
    }

    /* Stereo downmix coefficients */
    if (!base_channel && s->prim_channels > 2) {
        if (s->downmix) {
            for (j = base_channel; j < s->prim_channels; j++) {
                s->downmix_coef[j][0] = get_bits(&s->gb, 7);
                s->downmix_coef[j][1] = get_bits(&s->gb, 7);
            }
        } else {
            int am = s->amode & DCA_CHANNEL_MASK;
            if (am >= FF_ARRAY_ELEMS(dca_default_coeffs)) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Invalid channel mode %d\n", am);
                return AVERROR_INVALIDDATA;
            }
            for (j = base_channel; j < FFMIN(s->prim_channels, FF_ARRAY_ELEMS(dca_default_coeffs[am])); j++) {
                s->downmix_coef[j][0] = dca_default_coeffs[am][j][0];
                s->downmix_coef[j][1] = dca_default_coeffs[am][j][1];
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
    for (j = base_channel; j < s->prim_channels; j++)
        for (k = s->vq_start_subband[j]; k < s->subband_activity[j]; k++)
            /* 1 vector -> 32 samples */
            s->high_freq_vq[j][k] = get_bits(&s->gb, 10);

    /* Low frequency effect data */
    if (!base_channel && s->lfe) {
        int quant7;
        /* LFE samples */
        int lfe_samples = 2 * s->lfe * (4 + block_index);
        int lfe_end_sample = 2 * s->lfe * (4 + block_index + s->subsubframes[s->current_subframe]);
        float lfe_scale;

        for (j = lfe_samples; j < lfe_end_sample; j++) {
            /* Signed 8 bits int */
            s->lfe_data[j] = get_sbits(&s->gb, 8);
        }

        /* Scale factor index */
        quant7 = get_bits(&s->gb, 8);
        if (quant7 > 127) {
            avpriv_request_sample(s->avctx, "LFEScaleIndex larger than 127");
            return AVERROR_INVALIDDATA;
        }
        s->lfe_scale_factor = scale_factor_quant7[quant7];

        /* Quantization step size * scale factor */
        lfe_scale = 0.035 * s->lfe_scale_factor;

        for (j = lfe_samples; j < lfe_end_sample; j++)
            s->lfe_data[j] *= lfe_scale;
    }

#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "subsubframes: %i\n",
           s->subsubframes[s->current_subframe]);
    av_log(s->avctx, AV_LOG_DEBUG, "partial samples: %i\n",
           s->partial_samples[s->current_subframe]);

    for (j = base_channel; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "prediction mode:");
        for (k = 0; k < s->subband_activity[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, " %i", s->prediction_mode[j][k]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = base_channel; j < s->prim_channels; j++) {
        for (k = 0; k < s->subband_activity[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG,
                   "prediction coefs: %f, %f, %f, %f\n",
                   (float) adpcm_vb[s->prediction_vq[j][k]][0] / 8192,
                   (float) adpcm_vb[s->prediction_vq[j][k]][1] / 8192,
                   (float) adpcm_vb[s->prediction_vq[j][k]][2] / 8192,
                   (float) adpcm_vb[s->prediction_vq[j][k]][3] / 8192);
    }
    for (j = base_channel; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "bitalloc index: ");
        for (k = 0; k < s->vq_start_subband[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, "%2.2i ", s->bitalloc[j][k]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = base_channel; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "Transition mode:");
        for (k = 0; k < s->subband_activity[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, " %i", s->transition_mode[j][k]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = base_channel; j < s->prim_channels; j++) {
        av_log(s->avctx, AV_LOG_DEBUG, "Scale factor:");
        for (k = 0; k < s->subband_activity[j]; k++) {
            if (k >= s->vq_start_subband[j] || s->bitalloc[j][k] > 0)
                av_log(s->avctx, AV_LOG_DEBUG, " %i", s->scale_factor[j][k][0]);
            if (k < s->vq_start_subband[j] && s->transition_mode[j][k])
                av_log(s->avctx, AV_LOG_DEBUG, " %i(t)", s->scale_factor[j][k][1]);
        }
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = base_channel; j < s->prim_channels; j++) {
        if (s->joint_intensity[j] > 0) {
            int source_channel = s->joint_intensity[j] - 1;
            av_log(s->avctx, AV_LOG_DEBUG, "Joint scale factor index:\n");
            for (k = s->subband_activity[j]; k < s->subband_activity[source_channel]; k++)
                av_log(s->avctx, AV_LOG_DEBUG, " %i", s->joint_scale_factor[j][k]);
            av_log(s->avctx, AV_LOG_DEBUG, "\n");
        }
    }
    if (!base_channel && s->prim_channels > 2 && s->downmix) {
        av_log(s->avctx, AV_LOG_DEBUG, "Downmix coeffs:\n");
        for (j = 0; j < s->prim_channels; j++) {
            av_log(s->avctx, AV_LOG_DEBUG, "Channel 0, %d = %f\n", j,
                   dca_downmix_coeffs[s->downmix_coef[j][0]]);
            av_log(s->avctx, AV_LOG_DEBUG, "Channel 1, %d = %f\n", j,
                   dca_downmix_coeffs[s->downmix_coef[j][1]]);
        }
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
    for (j = base_channel; j < s->prim_channels; j++)
        for (k = s->vq_start_subband[j]; k < s->subband_activity[j]; k++)
            av_log(s->avctx, AV_LOG_DEBUG, "VQ index: %i\n", s->high_freq_vq[j][k]);
    if (!base_channel && s->lfe) {
        int lfe_samples = 2 * s->lfe * (4 + block_index);
        int lfe_end_sample = 2 * s->lfe * (4 + block_index + s->subsubframes[s->current_subframe]);

        av_log(s->avctx, AV_LOG_DEBUG, "LFE samples:\n");
        for (j = lfe_samples; j < lfe_end_sample; j++)
            av_log(s->avctx, AV_LOG_DEBUG, " %f", s->lfe_data[j]);
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
    }
#endif

    return 0;
}

static void qmf_32_subbands(DCAContext *s, int chans,
                            float samples_in[32][8], float *samples_out,
                            float scale)
{
    const float *prCoeff;
    int i;

    int sb_act = s->subband_activity[chans];
    int subindex;

    scale *= sqrt(1 / 8.0);

    /* Select filter */
    if (!s->multirate_inter)    /* Non-perfect reconstruction */
        prCoeff = fir_32bands_nonperfect;
    else                        /* Perfect reconstruction */
        prCoeff = fir_32bands_perfect;

    for (i = sb_act; i < 32; i++)
        s->raXin[i] = 0.0;

    /* Reconstructed channel sample index */
    for (subindex = 0; subindex < 8; subindex++) {
        /* Load in one sample from each subband and clear inactive subbands */
        for (i = 0; i < sb_act; i++) {
            unsigned sign = (i - 1) & 2;
            uint32_t v    = AV_RN32A(&samples_in[i][subindex]) ^ sign << 30;
            AV_WN32A(&s->raXin[i], v);
        }

        s->synth.synth_filter_float(&s->imdct,
                                    s->subband_fir_hist[chans],
                                    &s->hist_index[chans],
                                    s->subband_fir_noidea[chans], prCoeff,
                                    samples_out, s->raXin, scale);
        samples_out += 32;
    }
}

static void lfe_interpolation_fir(DCAContext *s, int decimation_select,
                                  int num_deci_sample, float *samples_in,
                                  float *samples_out, float scale)
{
    /* samples_in: An array holding decimated samples.
     *   Samples in current subframe starts from samples_in[0],
     *   while samples_in[-1], samples_in[-2], ..., stores samples
     *   from last subframe as history.
     *
     * samples_out: An array holding interpolated samples
     */

    int decifactor;
    const float *prCoeff;
    int deciindex;

    /* Select decimation filter */
    if (decimation_select == 1) {
        decifactor = 64;
        prCoeff = lfe_fir_128;
    } else {
        decifactor = 32;
        prCoeff = lfe_fir_64;
    }
    /* Interpolation */
    for (deciindex = 0; deciindex < num_deci_sample; deciindex++) {
        s->dcadsp.lfe_fir(samples_out, samples_in, prCoeff, decifactor, scale);
        samples_in++;
        samples_out += 2 * decifactor;
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

static void dca_downmix(float **samples, int srcfmt,
                        int downmix_coef[DCA_PRIM_CHANNELS_MAX][2],
                        const int8_t *channel_mapping)
{
    int c, l, r, sl, sr, s;
    int i;
    float t, u, v;
    float coef[DCA_PRIM_CHANNELS_MAX][2];

    for (i = 0; i < DCA_PRIM_CHANNELS_MAX; i++) {
        coef[i][0] = dca_downmix_coeffs[downmix_coef[i][0]];
        coef[i][1] = dca_downmix_coeffs[downmix_coef[i][1]];
    }

    switch (srcfmt) {
    case DCA_MONO:
    case DCA_CHANNEL:
    case DCA_STEREO_TOTAL:
    case DCA_STEREO_SUMDIFF:
    case DCA_4F2R:
        av_log(NULL, AV_LOG_ERROR, "Not implemented!\n");
        break;
    case DCA_STEREO:
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
        code = div;
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

#ifndef int8x8_fmul_int32
static inline void int8x8_fmul_int32(float *dst, const int8_t *src, int scale)
{
    float fscale = scale / 16.0;
    int i;
    for (i = 0; i < 8; i++)
        dst[i] = src[i] * fscale;
}
#endif

static int dca_subsubframe(DCAContext *s, int base_channel, int block_index)
{
    int k, l;
    int subsubframe = s->current_subsubframe;

    const float *quant_step_table;

    /* FIXME */
    float (*subband_samples)[DCA_SUBBANDS][8] = s->subband_samples[block_index];
    LOCAL_ALIGNED_16(int32_t, block, [8]);

    /*
     * Audio data
     */

    /* Select quantization step size table */
    if (s->bit_rate_index == 0x1f)
        quant_step_table = lossless_quant_d;
    else
        quant_step_table = lossy_quant_d;

    for (k = base_channel; k < s->prim_channels; k++) {
        if (get_bits_left(&s->gb) < 0)
            return AVERROR_INVALIDDATA;

        for (l = 0; l < s->vq_start_subband[k]; l++) {
            int m;

            /* Select the mid-tread linear quantizer */
            int abits = s->bitalloc[k][l];

            float quant_step_size = quant_step_table[abits];

            /*
             * Determine quantization index code book and its type
             */

            /* Select quantization index code book */
            int sel = s->quant_index_huffman[k][abits];

            /*
             * Extract bits from the bit stream
             */
            if (!abits) {
                memset(subband_samples[k][l], 0, 8 * sizeof(subband_samples[0][0][0]));
            } else {
                /* Deal with transients */
                int sfi = s->transition_mode[k][l] && subsubframe >= s->transition_mode[k][l];
                float rscale = quant_step_size * s->scale_factor[k][l][sfi] *
                               s->scalefactor_adj[k][sel];

                if (abits >= 11 || !dca_smpl_bitalloc[abits].vlc[sel].table) {
                    if (abits <= 7) {
                        /* Block code */
                        int block_code1, block_code2, size, levels, err;

                        size   = abits_sizes[abits - 1];
                        levels = abits_levels[abits - 1];

                        block_code1 = get_bits(&s->gb, size);
                        block_code2 = get_bits(&s->gb, size);
                        err = decode_blockcodes(block_code1, block_code2,
                                                levels, block);
                        if (err) {
                            av_log(s->avctx, AV_LOG_ERROR,
                                   "ERROR: block code look-up failed\n");
                            return AVERROR_INVALIDDATA;
                        }
                    } else {
                        /* no coding */
                        for (m = 0; m < 8; m++)
                            block[m] = get_sbits(&s->gb, abits - 3);
                    }
                } else {
                    /* Huffman coded */
                    for (m = 0; m < 8; m++)
                        block[m] = get_bitalloc(&s->gb,
                                                &dca_smpl_bitalloc[abits], sel);
                }

                s->fmt_conv.int32_to_float_fmul_scalar(subband_samples[k][l],
                                                       block, rscale, 8);
            }

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
                                 s->subband_samples_hist[k][l][m - n + 4] / 8192);
                }
            }
        }

        /*
         * Decode VQ encoded high frequencies
         */
        for (l = s->vq_start_subband[k]; l < s->subband_activity[k]; l++) {
            /* 1 vector -> 32 samples but we only need the 8 samples
             * for this subsubframe. */
            int hfvq = s->high_freq_vq[k][l];

            if (!s->debug_flag & 0x01) {
                av_log(s->avctx, AV_LOG_DEBUG,
                       "Stream with high frequencies VQ coding\n");
                s->debug_flag |= 0x01;
            }

            int8x8_fmul_int32(subband_samples[k][l],
                              &high_freq_vq[hfvq][subsubframe * 8],
                              s->scale_factor[k][l][0]);
        }
    }

    /* Check for DSYNC after subsubframe */
    if (s->aspf || subsubframe == s->subsubframes[s->current_subframe] - 1) {
        if (0xFFFF == get_bits(&s->gb, 16)) {   /* 0xFFFF */
#ifdef TRACE
            av_log(s->avctx, AV_LOG_DEBUG, "Got subframe DSYNC\n");
#endif
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "Didn't get subframe DSYNC\n");
        }
    }

    /* Backup predictor history for adpcm */
    for (k = base_channel; k < s->prim_channels; k++)
        for (l = 0; l < s->vq_start_subband[k]; l++)
            memcpy(s->subband_samples_hist[k][l],
                   &subband_samples[k][l][4],
                   4 * sizeof(subband_samples[0][0][0]));

    return 0;
}

static int dca_filter_channels(DCAContext *s, int block_index)
{
    float (*subband_samples)[DCA_SUBBANDS][8] = s->subband_samples[block_index];
    int k;

    /* 32 subbands QMF */
    for (k = 0; k < s->prim_channels; k++) {
/*        static float pcm_to_double[8] = { 32768.0, 32768.0, 524288.0, 524288.0,
                                            0, 8388608.0, 8388608.0 };*/
        if (s->channel_order_tab[k] >= 0)
            qmf_32_subbands(s, k, subband_samples[k],
                            s->samples_chanptr[s->channel_order_tab[k]],
                            M_SQRT1_2 / 32768.0 /* pcm_to_double[s->source_pcm_res] */);
    }

    /* Down mixing */
    if (s->avctx->request_channels == 2 && s->prim_channels > 2) {
        dca_downmix(s->samples_chanptr, s->amode, s->downmix_coef, s->channel_order_tab);
    }

    /* Generate LFE samples for this subsubframe FIXME!!! */
    if (s->output & DCA_LFE) {
        lfe_interpolation_fir(s, s->lfe, 2 * s->lfe,
                              s->lfe_data + 2 * s->lfe * (block_index + 4),
                              s->samples_chanptr[s->lfe_index],
                              1.0 / (256.0 * 32768.0));
        /* Outputs 20bits pcm samples */
    }

    return 0;
}


static int dca_subframe_footer(DCAContext *s, int base_channel)
{
    int aux_data_count = 0, i;

    /*
     * Unpack optional information
     */

    /* presumably optional information only appears in the core? */
    if (!base_channel) {
        if (s->timestamp)
            skip_bits_long(&s->gb, 32);

        if (s->aux_data)
            aux_data_count = get_bits(&s->gb, 6);

        for (i = 0; i < aux_data_count; i++)
            get_bits(&s->gb, 8);

        if (s->crc_present && (s->downmix || s->dynrange))
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
    if (s->current_subframe >= s->subframes) {
        av_log(s->avctx, AV_LOG_DEBUG, "check failed: %i>%i",
               s->current_subframe, s->subframes);
        return AVERROR_INVALIDDATA;
    }

    if (!s->current_subsubframe) {
#ifdef TRACE
        av_log(s->avctx, AV_LOG_DEBUG, "DSYNC dca_subframe_header\n");
#endif
        /* Read subframe header */
        if ((ret = dca_subframe_header(s, base_channel, block_index)))
            return ret;
    }

    /* Read subsubframe */
#ifdef TRACE
    av_log(s->avctx, AV_LOG_DEBUG, "DSYNC dca_subsubframe\n");
#endif
    if ((ret = dca_subsubframe(s, base_channel, block_index)))
        return ret;

    /* Update state */
    s->current_subsubframe++;
    if (s->current_subsubframe >= s->subsubframes[s->current_subframe]) {
        s->current_subsubframe = 0;
        s->current_subframe++;
    }
    if (s->current_subframe >= s->subframes) {
#ifdef TRACE
        av_log(s->avctx, AV_LOG_DEBUG, "DSYNC dca_subframe_footer\n");
#endif
        /* Read subframe footer */
        if ((ret = dca_subframe_footer(s, base_channel)))
            return ret;
    }

    return 0;
}

/**
 * Return the number of channels in an ExSS speaker mask (HD)
 */
static int dca_exss_mask2count(int mask)
{
    /* count bits that mean speaker pairs twice */
    return av_popcount(mask) +
           av_popcount(mask & (DCA_EXSS_CENTER_LEFT_RIGHT      |
                               DCA_EXSS_FRONT_LEFT_RIGHT       |
                               DCA_EXSS_FRONT_HIGH_LEFT_RIGHT  |
                               DCA_EXSS_WIDE_LEFT_RIGHT        |
                               DCA_EXSS_SIDE_LEFT_RIGHT        |
                               DCA_EXSS_SIDE_HIGH_LEFT_RIGHT   |
                               DCA_EXSS_SIDE_REAR_LEFT_RIGHT   |
                               DCA_EXSS_REAR_LEFT_RIGHT        |
                               DCA_EXSS_REAR_HIGH_LEFT_RIGHT));
}

/**
 * Skip mixing coefficients of a single mix out configuration (HD)
 */
static void dca_exss_skip_mix_coeffs(GetBitContext *gb, int channels, int out_ch)
{
    int i;

    for (i = 0; i < channels; i++) {
        int mix_map_mask = get_bits(gb, out_ch);
        int num_coeffs = av_popcount(mix_map_mask);
        skip_bits_long(gb, num_coeffs * 6);
    }
}

/**
 * Parse extension substream asset header (HD)
 */
static int dca_exss_parse_asset_header(DCAContext *s)
{
    int header_pos = get_bits_count(&s->gb);
    int header_size;
    int channels = 0;
    int embedded_stereo = 0;
    int embedded_6ch    = 0;
    int drc_code_present;
    int av_uninit(extensions_mask);
    int i, j;

    if (get_bits_left(&s->gb) < 16)
        return -1;

    /* We will parse just enough to get to the extensions bitmask with which
     * we can set the profile value. */

    header_size = get_bits(&s->gb, 9) + 1;
    skip_bits(&s->gb, 3); // asset index

    if (s->static_fields) {
        if (get_bits1(&s->gb))
            skip_bits(&s->gb, 4); // asset type descriptor
        if (get_bits1(&s->gb))
            skip_bits_long(&s->gb, 24); // language descriptor

        if (get_bits1(&s->gb)) {
            /* How can one fit 1024 bytes of text here if the maximum value
             * for the asset header size field above was 512 bytes? */
            int text_length = get_bits(&s->gb, 10) + 1;
            if (get_bits_left(&s->gb) < text_length * 8)
                return -1;
            skip_bits_long(&s->gb, text_length * 8); // info text
        }

        skip_bits(&s->gb, 5); // bit resolution - 1
        skip_bits(&s->gb, 4); // max sample rate code
        channels = get_bits(&s->gb, 8) + 1;

        if (get_bits1(&s->gb)) { // 1-to-1 channels to speakers
            int spkr_remap_sets;
            int spkr_mask_size = 16;
            int num_spkrs[7];

            if (channels > 2)
                embedded_stereo = get_bits1(&s->gb);
            if (channels > 6)
                embedded_6ch = get_bits1(&s->gb);

            if (get_bits1(&s->gb)) {
                spkr_mask_size = (get_bits(&s->gb, 2) + 1) << 2;
                skip_bits(&s->gb, spkr_mask_size); // spkr activity mask
            }

            spkr_remap_sets = get_bits(&s->gb, 3);

            for (i = 0; i < spkr_remap_sets; i++) {
                /* std layout mask for each remap set */
                num_spkrs[i] = dca_exss_mask2count(get_bits(&s->gb, spkr_mask_size));
            }

            for (i = 0; i < spkr_remap_sets; i++) {
                int num_dec_ch_remaps = get_bits(&s->gb, 5) + 1;
                if (get_bits_left(&s->gb) < 0)
                    return -1;

                for (j = 0; j < num_spkrs[i]; j++) {
                    int remap_dec_ch_mask = get_bits_long(&s->gb, num_dec_ch_remaps);
                    int num_dec_ch = av_popcount(remap_dec_ch_mask);
                    skip_bits_long(&s->gb, num_dec_ch * 5); // remap codes
                }
            }

        } else {
            skip_bits(&s->gb, 3); // representation type
        }
    }

    drc_code_present = get_bits1(&s->gb);
    if (drc_code_present)
        get_bits(&s->gb, 8); // drc code

    if (get_bits1(&s->gb))
        skip_bits(&s->gb, 5); // dialog normalization code

    if (drc_code_present && embedded_stereo)
        get_bits(&s->gb, 8); // drc stereo code

    if (s->mix_metadata && get_bits1(&s->gb)) {
        skip_bits(&s->gb, 1); // external mix
        skip_bits(&s->gb, 6); // post mix gain code

        if (get_bits(&s->gb, 2) != 3) // mixer drc code
            skip_bits(&s->gb, 3); // drc limit
        else
            skip_bits(&s->gb, 8); // custom drc code

        if (get_bits1(&s->gb)) // channel specific scaling
            for (i = 0; i < s->num_mix_configs; i++)
                skip_bits_long(&s->gb, s->mix_config_num_ch[i] * 6); // scale codes
        else
            skip_bits_long(&s->gb, s->num_mix_configs * 6); // scale codes

        for (i = 0; i < s->num_mix_configs; i++) {
            if (get_bits_left(&s->gb) < 0)
                return -1;
            dca_exss_skip_mix_coeffs(&s->gb, channels, s->mix_config_num_ch[i]);
            if (embedded_6ch)
                dca_exss_skip_mix_coeffs(&s->gb, 6, s->mix_config_num_ch[i]);
            if (embedded_stereo)
                dca_exss_skip_mix_coeffs(&s->gb, 2, s->mix_config_num_ch[i]);
        }
    }

    switch (get_bits(&s->gb, 2)) {
    case 0: extensions_mask = get_bits(&s->gb, 12); break;
    case 1: extensions_mask = DCA_EXT_EXSS_XLL;     break;
    case 2: extensions_mask = DCA_EXT_EXSS_LBR;     break;
    case 3: extensions_mask = 0; /* aux coding */   break;
    }

    /* not parsed further, we were only interested in the extensions mask */

    if (get_bits_left(&s->gb) < 0)
        return -1;

    if (get_bits_count(&s->gb) - header_pos > header_size * 8) {
        av_log(s->avctx, AV_LOG_WARNING, "Asset header size mismatch.\n");
        return -1;
    }
    skip_bits_long(&s->gb, header_pos + header_size * 8 - get_bits_count(&s->gb));

    if (extensions_mask & DCA_EXT_EXSS_XLL)
        s->profile = FF_PROFILE_DTS_HD_MA;
    else if (extensions_mask & (DCA_EXT_EXSS_XBR | DCA_EXT_EXSS_X96 |
                                DCA_EXT_EXSS_XXCH))
        s->profile = FF_PROFILE_DTS_HD_HRA;

    if (!(extensions_mask & DCA_EXT_CORE))
        av_log(s->avctx, AV_LOG_WARNING, "DTS core detection mismatch.\n");
    if ((extensions_mask & DCA_CORE_EXTS) != s->core_ext_mask)
        av_log(s->avctx, AV_LOG_WARNING,
               "DTS extensions detection mismatch (%d, %d)\n",
               extensions_mask & DCA_CORE_EXTS, s->core_ext_mask);

    return 0;
}

static int dca_xbr_parse_frame(DCAContext *s)
{
    int scale_table_high[DCA_CHSET_CHANS_MAX][DCA_SUBBANDS][2];
    int active_bands[DCA_CHSETS_MAX][DCA_CHSET_CHANS_MAX];
    int abits_high[DCA_CHSET_CHANS_MAX][DCA_SUBBANDS];
    int anctemp[DCA_CHSET_CHANS_MAX];
    int chset_fsize[DCA_CHSETS_MAX];
    int n_xbr_ch[DCA_CHSETS_MAX];
    int hdr_size, num_chsets, xbr_tmode, hdr_pos;
    int i, j, k, l, chset, chan_base;

    av_log(s->avctx, AV_LOG_DEBUG, "DTS-XBR: decoding XBR extension\n");

    /* get bit position of sync header */
    hdr_pos = get_bits_count(&s->gb) - 32;

    hdr_size = get_bits(&s->gb, 6) + 1;
    num_chsets = get_bits(&s->gb, 2) + 1;

    for(i = 0; i < num_chsets; i++)
        chset_fsize[i] = get_bits(&s->gb, 14) + 1;

    xbr_tmode = get_bits1(&s->gb);

    for(i = 0; i < num_chsets; i++) {
        n_xbr_ch[i] = get_bits(&s->gb, 3) + 1;
        k = get_bits(&s->gb, 2) + 5;
        for(j = 0; j < n_xbr_ch[i]; j++)
            active_bands[i][j] = get_bits(&s->gb, k) + 1;
    }

    /* skip to the end of the header */
    i = get_bits_count(&s->gb);
    if(hdr_pos + hdr_size * 8 > i)
        skip_bits_long(&s->gb, hdr_pos + hdr_size * 8 - i);

    /* loop over the channel data sets */
    /* only decode as many channels as we've decoded base data for */
    for(chset = 0, chan_base = 0;
        chset < num_chsets && chan_base + n_xbr_ch[chset] <= s->prim_channels;
        chan_base += n_xbr_ch[chset++]) {
        int start_posn = get_bits_count(&s->gb);
        int subsubframe = 0;
        int subframe = 0;

        /* loop over subframes */
        for (k = 0; k < (s->sample_blocks / 8); k++) {
            /* parse header if we're on first subsubframe of a block */
            if(subsubframe == 0) {
                /* Parse subframe header */
                for(i = 0; i < n_xbr_ch[chset]; i++) {
                    anctemp[i] = get_bits(&s->gb, 2) + 2;
                }

                for(i = 0; i < n_xbr_ch[chset]; i++) {
                    get_array(&s->gb, abits_high[i], active_bands[chset][i], anctemp[i]);
                }

                for(i = 0; i < n_xbr_ch[chset]; i++) {
                    anctemp[i] = get_bits(&s->gb, 3);
                    if(anctemp[i] < 1) {
                        av_log(s->avctx, AV_LOG_ERROR, "DTS-XBR: SYNC ERROR\n");
                        return AVERROR_INVALIDDATA;
                    }
                }

                /* generate scale factors */
                for(i = 0; i < n_xbr_ch[chset]; i++) {
                    const uint32_t *scale_table;
                    int nbits;

                    if (s->scalefactor_huffman[chan_base+i] == 6) {
                        scale_table = scale_factor_quant7;
                    } else {
                        scale_table = scale_factor_quant6;
                    }

                    nbits = anctemp[i];

                    for(j = 0; j < active_bands[chset][i]; j++) {
                        if(abits_high[i][j] > 0) {
                            scale_table_high[i][j][0] =
                                scale_table[get_bits(&s->gb, nbits)];

                            if(xbr_tmode && s->transition_mode[i][j]) {
                                scale_table_high[i][j][1] =
                                    scale_table[get_bits(&s->gb, nbits)];
                            }
                        }
                    }
                }
            }

            /* decode audio array for this block */
            for(i = 0; i < n_xbr_ch[chset]; i++) {
                for(j = 0; j < active_bands[chset][i]; j++) {
                    const int xbr_abits = abits_high[i][j];
                    const float quant_step_size = lossless_quant_d[xbr_abits];
                    const int sfi = xbr_tmode && s->transition_mode[i][j] && subsubframe >= s->transition_mode[i][j];
                    const float rscale = quant_step_size * scale_table_high[i][j][sfi];
                    float *subband_samples = s->subband_samples[k][chan_base+i][j];
                    int block[8];

                    if(xbr_abits <= 0)
                        continue;

                    if(xbr_abits > 7) {
                        get_array(&s->gb, block, 8, xbr_abits - 3);
                    } else {
                        int block_code1, block_code2, size, levels, err;

                        size   = abits_sizes[xbr_abits - 1];
                        levels = abits_levels[xbr_abits - 1];

                        block_code1 = get_bits(&s->gb, size);
                        block_code2 = get_bits(&s->gb, size);
                        err = decode_blockcodes(block_code1, block_code2,
                                                levels, block);
                        if (err) {
                            av_log(s->avctx, AV_LOG_ERROR,
                                   "ERROR: DTS-XBR: block code look-up failed\n");
                            return AVERROR_INVALIDDATA;
                        }
                    }

                    /* scale & sum into subband */
                    for(l = 0; l < 8; l++)
                        subband_samples[l] += (float)block[l] * rscale;
                }
            }

            /* check DSYNC marker */
            if(s->aspf || subsubframe == s->subsubframes[subframe] - 1) {
                if(get_bits(&s->gb, 16) != 0xffff) {
                    av_log(s->avctx, AV_LOG_ERROR, "DTS-XBR: Didn't get subframe DSYNC\n");
                    return AVERROR_INVALIDDATA;
                }
            }

            /* advance sub-sub-frame index */
            if(++subsubframe >= s->subsubframes[subframe]) {
                subsubframe = 0;
                subframe++;
            }
        }

        /* skip to next channel set */
        i = get_bits_count(&s->gb);
        if(start_posn + chset_fsize[chset] * 8 != i) {
            j = start_posn + chset_fsize[chset] * 8 - i;
            if(j < 0 || j >= 8)
                av_log(s->avctx, AV_LOG_ERROR, "DTS-XBR: end of channel set,"
                       " skipping further than expected (%d bits)\n", j);
            skip_bits_long(&s->gb, j);
        }
    }

    return 0;
}

/* parse initial header for XXCH and dump details */
static int dca_xxch_decode_frame(DCAContext *s)
{
    int hdr_size, spkmsk_bits, num_chsets, core_spk, hdr_pos;
    int i, chset, base_channel, chstart, fsize[8];

    /* assume header word has already been parsed */
    hdr_pos     = get_bits_count(&s->gb) - 32;
    hdr_size    = get_bits(&s->gb, 6) + 1;
  /*chhdr_crc   =*/ skip_bits1(&s->gb);
    spkmsk_bits = get_bits(&s->gb, 5) + 1;
    num_chsets  = get_bits(&s->gb, 2) + 1;

    for (i = 0; i < num_chsets; i++)
        fsize[i] = get_bits(&s->gb, 14) + 1;

    core_spk               = get_bits(&s->gb, spkmsk_bits);
    s->xxch_core_spkmask   = core_spk;
    s->xxch_nbits_spk_mask = spkmsk_bits;
    s->xxch_dmix_embedded  = 0;

    /* skip to the end of the header */
    i = get_bits_count(&s->gb);
    if (hdr_pos + hdr_size * 8 > i)
        skip_bits_long(&s->gb, hdr_pos + hdr_size * 8 - i);

    for (chset = 0; chset < num_chsets; chset++) {
        chstart       = get_bits_count(&s->gb);
        base_channel  = s->prim_channels;
        s->xxch_chset = chset;

        /* XXCH and Core headers differ, see 6.4.2 "XXCH Channel Set Header" vs.
           5.3.2 "Primary Audio Coding Header", DTS Spec 1.3.1 */
        dca_parse_audio_coding_header(s, base_channel, 1);

        /* decode channel data */
        for (i = 0; i < (s->sample_blocks / 8); i++) {
            if (dca_decode_block(s, base_channel, i)) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Error decoding DTS-XXCH extension\n");
                continue;
            }
        }

        /* skip to end of this section */
        i = get_bits_count(&s->gb);
        if (chstart + fsize[chset] * 8 > i)
            skip_bits_long(&s->gb, chstart + fsize[chset] * 8 - i);
    }
    s->xxch_chset = num_chsets;

    return 0;
}

/**
 * Parse extension substream header (HD)
 */
static void dca_exss_parse_header(DCAContext *s)
{
    int asset_size[8];
    int ss_index;
    int blownup;
    int num_audiop = 1;
    int num_assets = 1;
    int active_ss_mask[8];
    int i, j;
    int start_posn;
    int hdrsize;
    uint32_t mkr;

    if (get_bits_left(&s->gb) < 52)
        return;

    start_posn = get_bits_count(&s->gb) - 32;

    skip_bits(&s->gb, 8); // user data
    ss_index = get_bits(&s->gb, 2);

    blownup = get_bits1(&s->gb);
    hdrsize = get_bits(&s->gb,  8 + 4 * blownup) + 1; // header_size
    skip_bits(&s->gb, 16 + 4 * blownup); // hd_size

    s->static_fields = get_bits1(&s->gb);
    if (s->static_fields) {
        skip_bits(&s->gb, 2); // reference clock code
        skip_bits(&s->gb, 3); // frame duration code

        if (get_bits1(&s->gb))
            skip_bits_long(&s->gb, 36); // timestamp

        /* a single stream can contain multiple audio assets that can be
         * combined to form multiple audio presentations */

        num_audiop = get_bits(&s->gb, 3) + 1;
        if (num_audiop > 1) {
            avpriv_request_sample(s->avctx,
                                  "Multiple DTS-HD audio presentations");
            /* ignore such streams for now */
            return;
        }

        num_assets = get_bits(&s->gb, 3) + 1;
        if (num_assets > 1) {
            avpriv_request_sample(s->avctx, "Multiple DTS-HD audio assets");
            /* ignore such streams for now */
            return;
        }

        for (i = 0; i < num_audiop; i++)
            active_ss_mask[i] = get_bits(&s->gb, ss_index + 1);

        for (i = 0; i < num_audiop; i++)
            for (j = 0; j <= ss_index; j++)
                if (active_ss_mask[i] & (1 << j))
                    skip_bits(&s->gb, 8); // active asset mask

        s->mix_metadata = get_bits1(&s->gb);
        if (s->mix_metadata) {
            int mix_out_mask_size;

            skip_bits(&s->gb, 2); // adjustment level
            mix_out_mask_size  = (get_bits(&s->gb, 2) + 1) << 2;
            s->num_mix_configs =  get_bits(&s->gb, 2) + 1;

            for (i = 0; i < s->num_mix_configs; i++) {
                int mix_out_mask        = get_bits(&s->gb, mix_out_mask_size);
                s->mix_config_num_ch[i] = dca_exss_mask2count(mix_out_mask);
            }
        }
    }

    for (i = 0; i < num_assets; i++)
        asset_size[i] = get_bits_long(&s->gb, 16 + 4 * blownup);

    for (i = 0; i < num_assets; i++) {
        if (dca_exss_parse_asset_header(s))
            return;
    }

    /* not parsed further, we were only interested in the extensions mask
     * from the asset header */

    if (num_assets > 0) {
        j = get_bits_count(&s->gb);
        if (start_posn + hdrsize * 8 > j)
            skip_bits_long(&s->gb, start_posn + hdrsize * 8 - j);

        for (i = 0; i < num_assets; i++) {
            start_posn = get_bits_count(&s->gb);
            mkr        = get_bits_long(&s->gb, 32);

            /* parse extensions that we know about */
            if (mkr == 0x655e315e) {
                dca_xbr_parse_frame(s);
            } else if (mkr == 0x47004a03) {
                dca_xxch_decode_frame(s);
                s->core_ext_mask |= DCA_EXT_XXCH; /* xxx use for chan reordering */
            } else {
                av_log(s->avctx, AV_LOG_DEBUG,
                       "DTS-ExSS: unknown marker = 0x%08x\n", mkr);
            }

            /* skip to end of block */
            j = get_bits_count(&s->gb);
            if (start_posn + asset_size[i] * 8 > j)
                skip_bits_long(&s->gb, start_posn + asset_size[i] * 8 - j);
        }
    }
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
    int buf_size = avpkt->size;
    int channel_mask;
    int channel_layout;
    int lfe_samples;
    int num_core_channels = 0;
    int i, ret;
    float **samples_flt;
    float *src_chan;
    float *dst_chan;
    DCAContext *s = avctx->priv_data;
    int core_ss_end;
    int channels, full_channels;
    float scale;
    int achan;
    int chset;
    int mask;
    int lavc;
    int posn;
    int j, k;
    int endch;

    s->xch_present = 0;

    s->dca_buffer_size = ff_dca_convert_bitstream(buf, buf_size, s->dca_buffer,
                                                  DCA_MAX_FRAME_SIZE + DCA_MAX_EXSS_HEADER_SIZE);
    if (s->dca_buffer_size == AVERROR_INVALIDDATA) {
        av_log(avctx, AV_LOG_ERROR, "Not a valid DCA frame\n");
        return AVERROR_INVALIDDATA;
    }

    init_get_bits(&s->gb, s->dca_buffer, s->dca_buffer_size * 8);
    if ((ret = dca_parse_frame_header(s)) < 0) {
        //seems like the frame is corrupt, try with the next one
        return ret;
    }
    //set AVCodec values with parsed data
    avctx->sample_rate = s->sample_rate;
    avctx->bit_rate    = s->bit_rate;

    s->profile = FF_PROFILE_DTS;

    for (i = 0; i < (s->sample_blocks / 8); i++) {
        if ((ret = dca_decode_block(s, 0, i))) {
            av_log(avctx, AV_LOG_ERROR, "error decoding block\n");
            return ret;
        }
    }

    /* record number of core channels incase less than max channels are requested */
    num_core_channels = s->prim_channels;

    if (s->ext_coding)
        s->core_ext_mask = dca_ext_audio_descr_mask[s->ext_descr];
    else
        s->core_ext_mask = 0;

    core_ss_end = FFMIN(s->frame_size, s->dca_buffer_size) * 8;

    /* only scan for extensions if ext_descr was unknown or indicated a
     * supported XCh extension */
    if (s->core_ext_mask < 0 || s->core_ext_mask & (DCA_EXT_XCH | DCA_EXT_XXCH)) {

        /* if ext_descr was unknown, clear s->core_ext_mask so that the
         * extensions scan can fill it up */
        s->core_ext_mask = FFMAX(s->core_ext_mask, 0);

        /* extensions start at 32-bit boundaries into bitstream */
        skip_bits_long(&s->gb, (-get_bits_count(&s->gb)) & 31);

        while (core_ss_end - get_bits_count(&s->gb) >= 32) {
            uint32_t bits = get_bits_long(&s->gb, 32);

            switch (bits) {
            case 0x5a5a5a5a: {
                int ext_amode, xch_fsize;

                s->xch_base_channel = s->prim_channels;

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
                    av_log(avctx, AV_LOG_ERROR, "XCh extension amode %d not"
                           " supported!\n", ext_amode);
                    continue;
                }

                if (s->xch_base_channel < 2) {
                    avpriv_request_sample(avctx, "XCh with fewer than 2 base channels");
                    continue;
                }

                /* much like core primary audio coding header */
                dca_parse_audio_coding_header(s, s->xch_base_channel, 0);

                for (i = 0; i < (s->sample_blocks / 8); i++)
                    if ((ret = dca_decode_block(s, s->xch_base_channel, i))) {
                        av_log(avctx, AV_LOG_ERROR, "error decoding XCh extension\n");
                        continue;
                    }

                s->xch_present = 1;
                break;
            }
            case 0x47004a03:
                /* XXCh: extended channels */
                /* usually found either in core or HD part in DTS-HD HRA streams,
                 * but not in DTS-ES which contains XCh extensions instead */
                s->core_ext_mask |= DCA_EXT_XXCH;
                dca_xxch_decode_frame(s);
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
        get_bits_long(&s->gb, 32) == DCA_HD_MARKER)
        dca_exss_parse_header(s);

    avctx->profile = s->profile;

    full_channels = channels = s->prim_channels + !!s->lfe;

    /* If we have XXCH then the channel layout is managed differently */
    /* note that XLL will also have another way to do things */
    if (!(s->core_ext_mask & DCA_EXT_XXCH)
        || (s->core_ext_mask & DCA_EXT_XXCH && avctx->request_channels > 0
            && avctx->request_channels
            < num_core_channels + !!s->lfe + s->xxch_chset_nch[0]))
    { /* xxx should also do MA extensions */
        if (s->amode < 16) {
            avctx->channel_layout = dca_core_channel_layout[s->amode];

            if (s->xch_present && (!avctx->request_channels ||
                                   avctx->request_channels
                                   > num_core_channels + !!s->lfe)) {
                avctx->channel_layout |= AV_CH_BACK_CENTER;
                if (s->lfe) {
                    avctx->channel_layout |= AV_CH_LOW_FREQUENCY;
                    s->channel_order_tab = dca_channel_reorder_lfe_xch[s->amode];
                } else {
                    s->channel_order_tab = dca_channel_reorder_nolfe_xch[s->amode];
                }
                if (s->channel_order_tab[s->xch_base_channel] < 0)
                    return AVERROR_INVALIDDATA;
            } else {
                channels = num_core_channels + !!s->lfe;
                s->xch_present = 0; /* disable further xch processing */
                if (s->lfe) {
                    avctx->channel_layout |= AV_CH_LOW_FREQUENCY;
                    s->channel_order_tab = dca_channel_reorder_lfe[s->amode];
                } else
                    s->channel_order_tab = dca_channel_reorder_nolfe[s->amode];
            }

            if (channels > !!s->lfe &&
                s->channel_order_tab[channels - 1 - !!s->lfe] < 0)
                return AVERROR_INVALIDDATA;

            if (av_get_channel_layout_nb_channels(avctx->channel_layout) != channels) {
                av_log(avctx, AV_LOG_ERROR, "Number of channels %d mismatches layout %d\n", channels, av_get_channel_layout_nb_channels(avctx->channel_layout));
                return AVERROR_INVALIDDATA;
            }

            if (avctx->request_channels == 2 && s->prim_channels > 2) {
                channels = 2;
                s->output = DCA_STEREO;
                avctx->channel_layout = AV_CH_LAYOUT_STEREO;
            }
            else if (avctx->request_channel_layout & AV_CH_LAYOUT_NATIVE) {
                static const int8_t dca_channel_order_native[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
                s->channel_order_tab = dca_channel_order_native;
            }
            s->lfe_index = dca_lfe_index[s->amode];
        } else {
            av_log(avctx, AV_LOG_ERROR,
                   "Non standard configuration %d !\n", s->amode);
            return AVERROR_INVALIDDATA;
        }

        s->xxch_dmix_embedded = 0;
    } else {
        /* we only get here if an XXCH channel set can be added to the mix */
        channel_mask = s->xxch_core_spkmask;

        if (avctx->request_channels > 0
            && avctx->request_channels < s->prim_channels) {
            channels = num_core_channels + !!s->lfe;
            for (i = 0; i < s->xxch_chset && channels + s->xxch_chset_nch[i]
                                              <= avctx->request_channels; i++) {
                channels += s->xxch_chset_nch[i];
                channel_mask |= s->xxch_spk_masks[i];
            }
        } else {
            channels = s->prim_channels + !!s->lfe;
            for (i = 0; i < s->xxch_chset; i++) {
                channel_mask |= s->xxch_spk_masks[i];
            }
        }

        /* Given the DTS spec'ed channel mask, generate an avcodec version */
        channel_layout = 0;
        for (i = 0; i < s->xxch_nbits_spk_mask; ++i) {
            if (channel_mask & (1 << i)) {
                channel_layout |= map_xxch_to_native[i];
            }
        }

        /* make sure that we have managed to get equivelant dts/avcodec channel
         * masks in some sense -- unfortunately some channels could overlap */
        if (av_popcount(channel_mask) != av_popcount(channel_layout)) {
            av_log(avctx, AV_LOG_DEBUG,
                   "DTS-XXCH: Inconsistant avcodec/dts channel layouts\n");
            return AVERROR_INVALIDDATA;
        }

        avctx->channel_layout = channel_layout;

        if (!(avctx->request_channel_layout & AV_CH_LAYOUT_NATIVE)) {
            /* Estimate DTS --> avcodec ordering table */
            for (chset = -1, j = 0; chset < s->xxch_chset; ++chset) {
                mask = chset >= 0 ? s->xxch_spk_masks[chset]
                                  : s->xxch_core_spkmask;
                for (i = 0; i < s->xxch_nbits_spk_mask; i++) {
                    if (mask & ~(DCA_XXCH_LFE1 | DCA_XXCH_LFE2) & (1 << i)) {
                        lavc = map_xxch_to_native[i];
                        posn = av_popcount(channel_layout & (lavc - 1));
                        s->xxch_order_tab[j++] = posn;
                    }
                }
            }

            s->lfe_index = av_popcount(channel_layout & (AV_CH_LOW_FREQUENCY-1));
        } else { /* native ordering */
            for (i = 0; i < channels; i++)
                s->xxch_order_tab[i] = i;

            s->lfe_index = channels - 1;
        }

        s->channel_order_tab = s->xxch_order_tab;
    }

    if (avctx->channels != channels) {
        if (avctx->channels)
            av_log(avctx, AV_LOG_INFO, "Number of channels changed in DCA decoder (%d -> %d)\n", avctx->channels, channels);
        avctx->channels = channels;
    }

    /* get output buffer */
    frame->nb_samples = 256 * (s->sample_blocks / 8);
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples_flt = (float **)frame->extended_data;

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

        ret = av_samples_fill_arrays((uint8_t **)s->extra_channels, NULL,
                                     s->extra_channels_buffer,
                                     full_channels - channels,
                                     frame->nb_samples, avctx->sample_fmt, 0);
        if (ret < 0)
            return ret;
    }

    /* filter to get final output */
    for (i = 0; i < (s->sample_blocks / 8); i++) {
        int ch;

        for (ch = 0; ch < channels; ch++)
            s->samples_chanptr[ch] = samples_flt[ch] + i * 256;
        for (; ch < full_channels; ch++)
            s->samples_chanptr[ch] = s->extra_channels[ch - channels] + i * 256;

        dca_filter_channels(s, i);

        /* If this was marked as a DTS-ES stream we need to subtract back- */
        /* channel from SL & SR to remove matrixed back-channel signal */
        if ((s->source_pcm_res & 1) && s->xch_present) {
            float *back_chan = s->samples_chanptr[s->channel_order_tab[s->xch_base_channel]];
            float *lt_chan   = s->samples_chanptr[s->channel_order_tab[s->xch_base_channel - 2]];
            float *rt_chan   = s->samples_chanptr[s->channel_order_tab[s->xch_base_channel - 1]];
            s->fdsp.vector_fmac_scalar(lt_chan, back_chan, -M_SQRT1_2, 256);
            s->fdsp.vector_fmac_scalar(rt_chan, back_chan, -M_SQRT1_2, 256);
        }

        /* If stream contains XXCH, we might need to undo an embedded downmix */
        if (s->xxch_dmix_embedded) {
            /* Loop over channel sets in turn */
            ch = num_core_channels;
            for (chset = 0; chset < s->xxch_chset; chset++) {
                endch = ch + s->xxch_chset_nch[chset];
                mask = s->xxch_dmix_embedded;

                /* undo downmix */
                for (j = ch; j < endch; j++) {
                    if (mask & (1 << j)) { /* this channel has been mixed-out */
                        src_chan = s->samples_chanptr[s->channel_order_tab[j]];
                        for (k = 0; k < endch; k++) {
                            achan = s->channel_order_tab[k];
                            scale = s->xxch_dmix_coeff[j][k];
                            if (scale != 0.0) {
                                dst_chan = s->samples_chanptr[achan];
                                s->fdsp.vector_fmac_scalar(dst_chan, src_chan,
                                                           -scale, 256);
                            }
                        }
                    }
                }

                /* if a downmix has been embedded then undo the pre-scaling */
                if ((mask & (1 << ch)) && s->xxch_dmix_sf[chset] != 1.0f) {
                    scale = s->xxch_dmix_sf[chset];

                    for (j = 0; j < ch; j++) {
                        src_chan = s->samples_chanptr[s->channel_order_tab[j]];
                        for (k = 0; k < 256; k++)
                            src_chan[k] *= scale;
                    }

                    /* LFE channel is always part of core, scale if it exists */
                    if (s->lfe) {
                        src_chan = s->samples_chanptr[s->lfe_index];
                        for (k = 0; k < 256; k++)
                            src_chan[k] *= scale;
                    }
                }

                ch = endch;
            }

        }
    }

    /* update lfe history */
    lfe_samples = 2 * s->lfe * (s->sample_blocks / 8);
    for (i = 0; i < 2 * s->lfe * 4; i++)
        s->lfe_data[i] = s->lfe_data[i + lfe_samples];

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

    avpriv_float_dsp_init(&s->fdsp, avctx->flags & CODEC_FLAG_BITEXACT);
    ff_mdct_init(&s->imdct, 6, 1, 1.0);
    ff_synth_filter_init(&s->synth);
    ff_dcadsp_init(&s->dcadsp);
    ff_fmt_convert_init(&s->fmt_conv, avctx);

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    /* allow downmixing to stereo */
    if (avctx->channels > 0 && avctx->request_channels < avctx->channels &&
        avctx->request_channels == 2) {
        avctx->channels = avctx->request_channels;
    }

    return 0;
}

static av_cold int dca_decode_end(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;
    ff_mdct_end(&s->imdct);
    av_freep(&s->extra_channels_buffer);
    return 0;
}

static const AVProfile profiles[] = {
    { FF_PROFILE_DTS,        "DTS"        },
    { FF_PROFILE_DTS_ES,     "DTS-ES"     },
    { FF_PROFILE_DTS_96_24,  "DTS 96/24"  },
    { FF_PROFILE_DTS_HD_HRA, "DTS-HD HRA" },
    { FF_PROFILE_DTS_HD_MA,  "DTS-HD MA"  },
    { FF_PROFILE_UNKNOWN },
};

AVCodec ff_dca_decoder = {
    .name            = "dca",
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_DTS,
    .priv_data_size  = sizeof(DCAContext),
    .init            = dca_decode_init,
    .decode          = dca_decode_frame,
    .close           = dca_decode_end,
    .long_name       = NULL_IF_CONFIG_SMALL("DCA (DTS Coherent Acoustics)"),
    .capabilities    = CODEC_CAP_CHANNEL_CONF | CODEC_CAP_DR1,
    .sample_fmts     = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                       AV_SAMPLE_FMT_NONE },
    .profiles        = NULL_IF_CONFIG_SMALL(profiles),
};
