/*
 * Wmapro compatible decoder
 * Copyright (c) 2007 Baptiste Coudurier, Benjamin Larsson, Ulion
 * Copyright (c) 2008 - 2009 Sascha Sommer, Benjamin Larsson
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
 * @brief wmapro decoder implementation
 * Wmapro is an MDCT based codec comparable to wma standard or AAC.
 * The decoding therefore consists of the following steps:
 * - bitstream decoding
 * - reconstruction of per-channel data
 * - rescaling and inverse quantization
 * - IMDCT
 * - windowing and overlapp-add
 *
 * The compressed wmapro bitstream is split into individual packets.
 * Every such packet contains one or more wma frames.
 * The compressed frames may have a variable length and frames may
 * cross packet boundaries.
 * Common to all wmapro frames is the number of samples that are stored in
 * a frame.
 * The number of samples and a few other decode flags are stored
 * as extradata that has to be passed to the decoder.
 *
 * The wmapro frames themselves are again split into a variable number of
 * subframes. Every subframe contains the data for 2^N time domain samples
 * where N varies between 7 and 12.
 *
 * Example wmapro bitstream (in samples):
 *
 * ||   packet 0           || packet 1 || packet 2      packets
 * ---------------------------------------------------
 * || frame 0      || frame 1       || frame 2    ||    frames
 * ---------------------------------------------------
 * ||   |      |   ||   |   |   |   ||            ||    subframes of channel 0
 * ---------------------------------------------------
 * ||      |   |   ||   |   |   |   ||            ||    subframes of channel 1
 * ---------------------------------------------------
 *
 * The frame layouts for the individual channels of a wma frame does not need
 * to be the same.
 *
 * However, if the offsets and lengths of several subframes of a frame are the
 * same, the subframes of the channels can be grouped.
 * Every group may then use special coding techniques like M/S stereo coding
 * to improve the compression ratio. These channel transformations do not
 * need to be applied to a whole subframe. Instead, they can also work on
 * individual scale factor bands (see below).
 * The coefficients that carry the audio signal in the frequency domain
 * are transmitted as huffman-coded vectors with 4, 2 and 1 elements.
 * In addition to that, the encoder can switch to a runlevel coding scheme
 * by transmitting subframe_length / 128 zero coefficients.
 *
 * Before the audio signal can be converted to the time domain, the
 * coefficients have to be rescaled and inverse quantized.
 * A subframe is therefore split into several scale factor bands that get
 * scaled individually.
 * Scale factors are submitted for every frame but they might be shared
 * between the subframes of a channel. Scale factors are initially DPCM-coded.
 * Once scale factors are shared, the differences are transmitted as runlevel
 * codes.
 * Every subframe length and offset combination in the frame layout shares a
 * common quantization factor that can be adjusted for every channel by a
 * modifier.
 * After the inverse quantization, the coefficients get processed by an IMDCT.
 * The resulting values are then windowed with a sine window and the first half
 * of the values are added to the second half of the output from the previous
 * subframe in order to reconstruct the output samples.
 */

#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "put_bits.h"
#include "wmaprodata.h"
#include "dsputil.h"
#include "wma.h"

/** current decoder limitations */
#define WMAPRO_MAX_CHANNELS    8                             ///< max number of handled channels
#define MAX_SUBFRAMES  32                                    ///< max number of subframes per channel
#define MAX_BANDS      29                                    ///< max number of scale factor bands
#define MAX_FRAMESIZE  32768                                 ///< maximum compressed frame size

#define WMAPRO_BLOCK_MAX_BITS 12                                           ///< log2 of max block size
#define WMAPRO_BLOCK_MAX_SIZE (1 << WMAPRO_BLOCK_MAX_BITS)                 ///< maximum block size
#define WMAPRO_BLOCK_SIZES    (WMAPRO_BLOCK_MAX_BITS - BLOCK_MIN_BITS + 1) ///< possible block sizes


#define VLCBITS            9
#define SCALEVLCBITS       8
#define VEC4MAXDEPTH    ((HUFF_VEC4_MAXBITS+VLCBITS-1)/VLCBITS)
#define VEC2MAXDEPTH    ((HUFF_VEC2_MAXBITS+VLCBITS-1)/VLCBITS)
#define VEC1MAXDEPTH    ((HUFF_VEC1_MAXBITS+VLCBITS-1)/VLCBITS)
#define SCALEMAXDEPTH   ((HUFF_SCALE_MAXBITS+SCALEVLCBITS-1)/SCALEVLCBITS)
#define SCALERLMAXDEPTH ((HUFF_SCALE_RL_MAXBITS+VLCBITS-1)/VLCBITS)

static VLC              sf_vlc;           ///< scale factor DPCM vlc
static VLC              sf_rl_vlc;        ///< scale factor run length vlc
static VLC              vec4_vlc;         ///< 4 coefficients per symbol
static VLC              vec2_vlc;         ///< 2 coefficients per symbol
static VLC              vec1_vlc;         ///< 1 coefficient per symbol
static VLC              coef_vlc[2];      ///< coefficient run length vlc codes
static float            sin64[33];        ///< sinus table for decorrelation

/**
 * @brief frame specific decoder context for a single channel
 */
typedef struct {
    int16_t  prev_block_len;                          ///< length of the previous block
    uint8_t  transmit_coefs;
    uint8_t  num_subframes;
    uint16_t subframe_len[MAX_SUBFRAMES];             ///< subframe length in samples
    uint16_t subframe_offset[MAX_SUBFRAMES];          ///< subframe positions in the current frame
    uint8_t  cur_subframe;                            ///< current subframe number
    uint16_t decoded_samples;                         ///< number of already processed samples
    uint8_t  grouped;                                 ///< channel is part of a group
    int      quant_step;                              ///< quantization step for the current subframe
    int8_t   reuse_sf;                                ///< share scale factors between subframes
    int8_t   scale_factor_step;                       ///< scaling step for the current subframe
    int      max_scale_factor;                        ///< maximum scale factor for the current subframe
    int      saved_scale_factors[2][MAX_BANDS];       ///< resampled and (previously) transmitted scale factor values
    int8_t   scale_factor_idx;                        ///< index for the transmitted scale factor values (used for resampling)
    int*     scale_factors;                           ///< pointer to the scale factor values used for decoding
    uint8_t  table_idx;                               ///< index in sf_offsets for the scale factor reference block
    float*   coeffs;                                  ///< pointer to the subframe decode buffer
    DECLARE_ALIGNED(16, float, out)[WMAPRO_BLOCK_MAX_SIZE + WMAPRO_BLOCK_MAX_SIZE / 2]; ///< output buffer
} WMAProChannelCtx;

/**
 * @brief channel group for channel transformations
 */
typedef struct {
    uint8_t num_channels;                                     ///< number of channels in the group
    int8_t  transform;                                        ///< transform on / off
    int8_t  transform_band[MAX_BANDS];                        ///< controls if the transform is enabled for a certain band
    float   decorrelation_matrix[WMAPRO_MAX_CHANNELS*WMAPRO_MAX_CHANNELS];
    float*  channel_data[WMAPRO_MAX_CHANNELS];                ///< transformation coefficients
} WMAProChannelGrp;

/**
 * @brief main decoder context
 */
typedef struct WMAProDecodeCtx {
    /* generic decoder variables */
    AVCodecContext*  avctx;                         ///< codec context for av_log
    DSPContext       dsp;                           ///< accelerated DSP functions
    uint8_t          frame_data[MAX_FRAMESIZE +
                      FF_INPUT_BUFFER_PADDING_SIZE];///< compressed frame data
    PutBitContext    pb;                            ///< context for filling the frame_data buffer
    FFTContext       mdct_ctx[WMAPRO_BLOCK_SIZES];  ///< MDCT context per block size
    DECLARE_ALIGNED(16, float, tmp)[WMAPRO_BLOCK_MAX_SIZE]; ///< IMDCT output buffer
    float*           windows[WMAPRO_BLOCK_SIZES];   ///< windows for the different block sizes

    /* frame size dependent frame information (set during initialization) */
    uint32_t         decode_flags;                  ///< used compression features
    uint8_t          len_prefix;                    ///< frame is prefixed with its length
    uint8_t          dynamic_range_compression;     ///< frame contains DRC data
    uint8_t          bits_per_sample;               ///< integer audio sample size for the unscaled IMDCT output (used to scale to [-1.0, 1.0])
    uint16_t         samples_per_frame;             ///< number of samples to output
    uint16_t         log2_frame_size;
    int8_t           num_channels;                  ///< number of channels in the stream (same as AVCodecContext.num_channels)
    int8_t           lfe_channel;                   ///< lfe channel index
    uint8_t          max_num_subframes;
    uint8_t          subframe_len_bits;             ///< number of bits used for the subframe length
    uint8_t          max_subframe_len_bit;          ///< flag indicating that the subframe is of maximum size when the first subframe length bit is 1
    uint16_t         min_samples_per_subframe;
    int8_t           num_sfb[WMAPRO_BLOCK_SIZES];   ///< scale factor bands per block size
    int16_t          sfb_offsets[WMAPRO_BLOCK_SIZES][MAX_BANDS];                    ///< scale factor band offsets (multiples of 4)
    int8_t           sf_offsets[WMAPRO_BLOCK_SIZES][WMAPRO_BLOCK_SIZES][MAX_BANDS]; ///< scale factor resample matrix
    int16_t          subwoofer_cutoffs[WMAPRO_BLOCK_SIZES]; ///< subwoofer cutoff values

    /* packet decode state */
    GetBitContext    pgb;                           ///< bitstream reader context for the packet
    uint8_t          packet_offset;                 ///< frame offset in the packet
    uint8_t          packet_sequence_number;        ///< current packet number
    int              num_saved_bits;                ///< saved number of bits
    int              frame_offset;                  ///< frame offset in the bit reservoir
    int              subframe_offset;               ///< subframe offset in the bit reservoir
    uint8_t          packet_loss;                   ///< set in case of bitstream error
    uint8_t          packet_done;                   ///< set when a packet is fully decoded

    /* frame decode state */
    uint32_t         frame_num;                     ///< current frame number (not used for decoding)
    GetBitContext    gb;                            ///< bitstream reader context
    int              buf_bit_size;                  ///< buffer size in bits
    float*           samples;                       ///< current samplebuffer pointer
    float*           samples_end;                   ///< maximum samplebuffer pointer
    uint8_t          drc_gain;                      ///< gain for the DRC tool
    int8_t           skip_frame;                    ///< skip output step
    int8_t           parsed_all_subframes;          ///< all subframes decoded?

    /* subframe/block decode state */
    int16_t          subframe_len;                  ///< current subframe length
    int8_t           channels_for_cur_subframe;     ///< number of channels that contain the subframe
    int8_t           channel_indexes_for_cur_subframe[WMAPRO_MAX_CHANNELS];
    int8_t           num_bands;                     ///< number of scale factor bands
    int16_t*         cur_sfb_offsets;               ///< sfb offsets for the current block
    uint8_t          table_idx;                     ///< index for the num_sfb, sfb_offsets, sf_offsets and subwoofer_cutoffs tables
    int8_t           esc_len;                       ///< length of escaped coefficients

    uint8_t          num_chgroups;                  ///< number of channel groups
    WMAProChannelGrp chgroup[WMAPRO_MAX_CHANNELS];  ///< channel group information

    WMAProChannelCtx channel[WMAPRO_MAX_CHANNELS];  ///< per channel data
} WMAProDecodeCtx;


/**
 *@brief helper function to print the most important members of the context
 *@param s context
 */
static void av_cold dump_context(WMAProDecodeCtx *s)
{
#define PRINT(a, b)     av_log(s->avctx, AV_LOG_DEBUG, " %s = %d\n", a, b);
#define PRINT_HEX(a, b) av_log(s->avctx, AV_LOG_DEBUG, " %s = %x\n", a, b);

    PRINT("ed sample bit depth", s->bits_per_sample);
    PRINT_HEX("ed decode flags", s->decode_flags);
    PRINT("samples per frame",   s->samples_per_frame);
    PRINT("log2 frame size",     s->log2_frame_size);
    PRINT("max num subframes",   s->max_num_subframes);
    PRINT("len prefix",          s->len_prefix);
    PRINT("num channels",        s->num_channels);
}

/**
 *@brief Uninitialize the decoder and free all resources.
 *@param avctx codec context
 *@return 0 on success, < 0 otherwise
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    WMAProDecodeCtx *s = avctx->priv_data;
    int i;

    for (i = 0; i < WMAPRO_BLOCK_SIZES; i++)
        ff_mdct_end(&s->mdct_ctx[i]);

    return 0;
}

/**
 *@brief Initialize the decoder.
 *@param avctx codec context
 *@return 0 on success, -1 otherwise
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    WMAProDecodeCtx *s = avctx->priv_data;
    uint8_t *edata_ptr = avctx->extradata;
    unsigned int channel_mask;
    int i;
    int log2_max_num_subframes;
    int num_possible_block_sizes;

    s->avctx = avctx;
    dsputil_init(&s->dsp, avctx);
    init_put_bits(&s->pb, s->frame_data, MAX_FRAMESIZE);

    avctx->sample_fmt = SAMPLE_FMT_FLT;

    if (avctx->extradata_size >= 18) {
        s->decode_flags    = AV_RL16(edata_ptr+14);
        channel_mask       = AV_RL32(edata_ptr+2);
        s->bits_per_sample = AV_RL16(edata_ptr);
        /** dump the extradata */
        for (i = 0; i < avctx->extradata_size; i++)
            dprintf(avctx, "[%x] ", avctx->extradata[i]);
        dprintf(avctx, "\n");

    } else {
        av_log_ask_for_sample(avctx, "Unknown extradata size\n");
        return AVERROR_INVALIDDATA;
    }

    /** generic init */
    s->log2_frame_size = av_log2(avctx->block_align) + 4;

    /** frame info */
    s->skip_frame  = 1; /** skip first frame */
    s->packet_loss = 1;
    s->len_prefix  = (s->decode_flags & 0x40);

    if (!s->len_prefix) {
        av_log_ask_for_sample(avctx, "no length prefix\n");
        return AVERROR_INVALIDDATA;
    }

    /** get frame len */
    s->samples_per_frame = 1 << ff_wma_get_frame_len_bits(avctx->sample_rate,
                                                          3, s->decode_flags);

    /** init previous block len */
    for (i = 0; i < avctx->channels; i++)
        s->channel[i].prev_block_len = s->samples_per_frame;

    /** subframe info */
    log2_max_num_subframes       = ((s->decode_flags & 0x38) >> 3);
    s->max_num_subframes         = 1 << log2_max_num_subframes;
    if (s->max_num_subframes == 16)
        s->max_subframe_len_bit = 1;
    s->subframe_len_bits = av_log2(log2_max_num_subframes) + 1;

    num_possible_block_sizes     = log2_max_num_subframes + 1;
    s->min_samples_per_subframe  = s->samples_per_frame / s->max_num_subframes;
    s->dynamic_range_compression = (s->decode_flags & 0x80);

    if (s->max_num_subframes > MAX_SUBFRAMES) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of subframes %i\n",
               s->max_num_subframes);
        return AVERROR_INVALIDDATA;
    }

    s->num_channels = avctx->channels;

    /** extract lfe channel position */
    s->lfe_channel = -1;

    if (channel_mask & 8) {
        unsigned int mask;
        for (mask = 1; mask < 16; mask <<= 1) {
            if (channel_mask & mask)
                ++s->lfe_channel;
        }
    }

    if (s->num_channels < 0) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of channels %d\n", s->num_channels);
        return AVERROR_INVALIDDATA;
    } else if (s->num_channels > WMAPRO_MAX_CHANNELS) {
        av_log_ask_for_sample(avctx, "unsupported number of channels\n");
        return AVERROR_PATCHWELCOME;
    }

    INIT_VLC_STATIC(&sf_vlc, SCALEVLCBITS, HUFF_SCALE_SIZE,
                    scale_huffbits, 1, 1,
                    scale_huffcodes, 2, 2, 616);

    INIT_VLC_STATIC(&sf_rl_vlc, VLCBITS, HUFF_SCALE_RL_SIZE,
                    scale_rl_huffbits, 1, 1,
                    scale_rl_huffcodes, 4, 4, 1406);

    INIT_VLC_STATIC(&coef_vlc[0], VLCBITS, HUFF_COEF0_SIZE,
                    coef0_huffbits, 1, 1,
                    coef0_huffcodes, 4, 4, 2108);

    INIT_VLC_STATIC(&coef_vlc[1], VLCBITS, HUFF_COEF1_SIZE,
                    coef1_huffbits, 1, 1,
                    coef1_huffcodes, 4, 4, 3912);

    INIT_VLC_STATIC(&vec4_vlc, VLCBITS, HUFF_VEC4_SIZE,
                    vec4_huffbits, 1, 1,
                    vec4_huffcodes, 2, 2, 604);

    INIT_VLC_STATIC(&vec2_vlc, VLCBITS, HUFF_VEC2_SIZE,
                    vec2_huffbits, 1, 1,
                    vec2_huffcodes, 2, 2, 562);

    INIT_VLC_STATIC(&vec1_vlc, VLCBITS, HUFF_VEC1_SIZE,
                    vec1_huffbits, 1, 1,
                    vec1_huffcodes, 2, 2, 562);

    /** calculate number of scale factor bands and their offsets
        for every possible block size */
    for (i = 0; i < num_possible_block_sizes; i++) {
        int subframe_len = s->samples_per_frame >> i;
        int x;
        int band = 1;

        s->sfb_offsets[i][0] = 0;

        for (x = 0; x < MAX_BANDS-1 && s->sfb_offsets[i][band - 1] < subframe_len; x++) {
            int offset = (subframe_len * 2 * critical_freq[x])
                          / s->avctx->sample_rate + 2;
            offset &= ~3;
            if (offset > s->sfb_offsets[i][band - 1])
                s->sfb_offsets[i][band++] = offset;
        }
        s->sfb_offsets[i][band - 1] = subframe_len;
        s->num_sfb[i]               = band - 1;
    }


    /** Scale factors can be shared between blocks of different size
        as every block has a different scale factor band layout.
        The matrix sf_offsets is needed to find the correct scale factor.
     */

    for (i = 0; i < num_possible_block_sizes; i++) {
        int b;
        for (b = 0; b < s->num_sfb[i]; b++) {
            int x;
            int offset = ((s->sfb_offsets[i][b]
                           + s->sfb_offsets[i][b + 1] - 1) << i) >> 1;
            for (x = 0; x < num_possible_block_sizes; x++) {
                int v = 0;
                while (s->sfb_offsets[x][v + 1] << x < offset)
                    ++v;
                s->sf_offsets[i][x][b] = v;
            }
        }
    }

    /** init MDCT, FIXME: only init needed sizes */
    for (i = 0; i < WMAPRO_BLOCK_SIZES; i++)
        ff_mdct_init(&s->mdct_ctx[i], BLOCK_MIN_BITS+1+i, 1,
                     1.0 / (1 << (BLOCK_MIN_BITS + i - 1))
                     / (1 << (s->bits_per_sample - 1)));

    /** init MDCT windows: simple sinus window */
    for (i = 0; i < WMAPRO_BLOCK_SIZES; i++) {
        const int win_idx = WMAPRO_BLOCK_MAX_BITS - i;
        ff_init_ff_sine_windows(win_idx);
        s->windows[WMAPRO_BLOCK_SIZES - i - 1] = ff_sine_windows[win_idx];
    }

    /** calculate subwoofer cutoff values */
    for (i = 0; i < num_possible_block_sizes; i++) {
        int block_size = s->samples_per_frame >> i;
        int cutoff = (440*block_size + 3 * (s->avctx->sample_rate >> 1) - 1)
                     / s->avctx->sample_rate;
        s->subwoofer_cutoffs[i] = av_clip(cutoff, 4, block_size);
    }

    /** calculate sine values for the decorrelation matrix */
    for (i = 0; i < 33; i++)
        sin64[i] = sin(i*M_PI / 64.0);

    if (avctx->debug & FF_DEBUG_BITSTREAM)
        dump_context(s);

    avctx->channel_layout = channel_mask;
    return 0;
}

/**
 *@brief Decode the subframe length.
 *@param s context
 *@param offset sample offset in the frame
 *@return decoded subframe length on success, < 0 in case of an error
 */
static int decode_subframe_length(WMAProDecodeCtx *s, int offset)
{
    int frame_len_shift = 0;
    int subframe_len;

    /** no need to read from the bitstream when only one length is possible */
    if (offset == s->samples_per_frame - s->min_samples_per_subframe)
        return s->min_samples_per_subframe;

    /** 1 bit indicates if the subframe is of maximum length */
    if (s->max_subframe_len_bit) {
        if (get_bits1(&s->gb))
            frame_len_shift = 1 + get_bits(&s->gb, s->subframe_len_bits-1);
    } else
        frame_len_shift = get_bits(&s->gb, s->subframe_len_bits);

    subframe_len = s->samples_per_frame >> frame_len_shift;

    /** sanity check the length */
    if (subframe_len < s->min_samples_per_subframe ||
        subframe_len > s->samples_per_frame) {
        av_log(s->avctx, AV_LOG_ERROR, "broken frame: subframe_len %i\n",
               subframe_len);
        return AVERROR_INVALIDDATA;
    }
    return subframe_len;
}

/**
 *@brief Decode how the data in the frame is split into subframes.
 *       Every WMA frame contains the encoded data for a fixed number of
 *       samples per channel. The data for every channel might be split
 *       into several subframes. This function will reconstruct the list of
 *       subframes for every channel.
 *
 *       If the subframes are not evenly split, the algorithm estimates the
 *       channels with the lowest number of total samples.
 *       Afterwards, for each of these channels a bit is read from the
 *       bitstream that indicates if the channel contains a subframe with the
 *       next subframe size that is going to be read from the bitstream or not.
 *       If a channel contains such a subframe, the subframe size gets added to
 *       the channel's subframe list.
 *       The algorithm repeats these steps until the frame is properly divided
 *       between the individual channels.
 *
 *@param s context
 *@return 0 on success, < 0 in case of an error
 */
static int decode_tilehdr(WMAProDecodeCtx *s)
{
    uint16_t num_samples[WMAPRO_MAX_CHANNELS];        /** sum of samples for all currently known subframes of a channel */
    uint8_t  contains_subframe[WMAPRO_MAX_CHANNELS];  /** flag indicating if a channel contains the current subframe */
    int channels_for_cur_subframe = s->num_channels;  /** number of channels that contain the current subframe */
    int fixed_channel_layout = 0;                     /** flag indicating that all channels use the same subframe offsets and sizes */
    int min_channel_len = 0;                          /** smallest sum of samples (channels with this length will be processed first) */
    int c;

    /* Should never consume more than 3073 bits (256 iterations for the
     * while loop when always the minimum amount of 128 samples is substracted
     * from missing samples in the 8 channel case).
     * 1 + BLOCK_MAX_SIZE * MAX_CHANNELS / BLOCK_MIN_SIZE * (MAX_CHANNELS  + 4)
     */

    /** reset tiling information */
    for (c = 0; c < s->num_channels; c++)
        s->channel[c].num_subframes = 0;

    memset(num_samples, 0, sizeof(num_samples));

    if (s->max_num_subframes == 1 || get_bits1(&s->gb))
        fixed_channel_layout = 1;

    /** loop until the frame data is split between the subframes */
    do {
        int subframe_len;

        /** check which channels contain the subframe */
        for (c = 0; c < s->num_channels; c++) {
            if (num_samples[c] == min_channel_len) {
                if (fixed_channel_layout || channels_for_cur_subframe == 1 ||
                   (min_channel_len == s->samples_per_frame - s->min_samples_per_subframe))
                    contains_subframe[c] = 1;
                else
                    contains_subframe[c] = get_bits1(&s->gb);
            } else
                contains_subframe[c] = 0;
        }

        /** get subframe length, subframe_len == 0 is not allowed */
        if ((subframe_len = decode_subframe_length(s, min_channel_len)) <= 0)
            return AVERROR_INVALIDDATA;

        /** add subframes to the individual channels and find new min_channel_len */
        min_channel_len += subframe_len;
        for (c = 0; c < s->num_channels; c++) {
            WMAProChannelCtx* chan = &s->channel[c];

            if (contains_subframe[c]) {
                if (chan->num_subframes >= MAX_SUBFRAMES) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "broken frame: num subframes > 31\n");
                    return AVERROR_INVALIDDATA;
                }
                chan->subframe_len[chan->num_subframes] = subframe_len;
                num_samples[c] += subframe_len;
                ++chan->num_subframes;
                if (num_samples[c] > s->samples_per_frame) {
                    av_log(s->avctx, AV_LOG_ERROR, "broken frame: "
                           "channel len > samples_per_frame\n");
                    return AVERROR_INVALIDDATA;
                }
            } else if (num_samples[c] <= min_channel_len) {
                if (num_samples[c] < min_channel_len) {
                    channels_for_cur_subframe = 0;
                    min_channel_len = num_samples[c];
                }
                ++channels_for_cur_subframe;
            }
        }
    } while (min_channel_len < s->samples_per_frame);

    for (c = 0; c < s->num_channels; c++) {
        int i;
        int offset = 0;
        for (i = 0; i < s->channel[c].num_subframes; i++) {
            dprintf(s->avctx, "frame[%i] channel[%i] subframe[%i]"
                    " len %i\n", s->frame_num, c, i,
                    s->channel[c].subframe_len[i]);
            s->channel[c].subframe_offset[i] = offset;
            offset += s->channel[c].subframe_len[i];
        }
    }

    return 0;
}

/**
 *@brief Calculate a decorrelation matrix from the bitstream parameters.
 *@param s codec context
 *@param chgroup channel group for which the matrix needs to be calculated
 */
static void decode_decorrelation_matrix(WMAProDecodeCtx *s,
                                        WMAProChannelGrp *chgroup)
{
    int i;
    int offset = 0;
    int8_t rotation_offset[WMAPRO_MAX_CHANNELS * WMAPRO_MAX_CHANNELS];
    memset(chgroup->decorrelation_matrix, 0, s->num_channels *
           s->num_channels * sizeof(*chgroup->decorrelation_matrix));

    for (i = 0; i < chgroup->num_channels * (chgroup->num_channels - 1) >> 1; i++)
        rotation_offset[i] = get_bits(&s->gb, 6);

    for (i = 0; i < chgroup->num_channels; i++)
        chgroup->decorrelation_matrix[chgroup->num_channels * i + i] =
            get_bits1(&s->gb) ? 1.0 : -1.0;

    for (i = 1; i < chgroup->num_channels; i++) {
        int x;
        for (x = 0; x < i; x++) {
            int y;
            for (y = 0; y < i + 1; y++) {
                float v1 = chgroup->decorrelation_matrix[x * chgroup->num_channels + y];
                float v2 = chgroup->decorrelation_matrix[i * chgroup->num_channels + y];
                int n = rotation_offset[offset + x];
                float sinv;
                float cosv;

                if (n < 32) {
                    sinv = sin64[n];
                    cosv = sin64[32 - n];
                } else {
                    sinv =  sin64[64 -  n];
                    cosv = -sin64[n  - 32];
                }

                chgroup->decorrelation_matrix[y + x * chgroup->num_channels] =
                                               (v1 * sinv) - (v2 * cosv);
                chgroup->decorrelation_matrix[y + i * chgroup->num_channels] =
                                               (v1 * cosv) + (v2 * sinv);
            }
        }
        offset += i;
    }
}

/**
 *@brief Decode channel transformation parameters
 *@param s codec context
 *@return 0 in case of success, < 0 in case of bitstream errors
 */
static int decode_channel_transform(WMAProDecodeCtx* s)
{
    int i;
    /* should never consume more than 1921 bits for the 8 channel case
     * 1 + MAX_CHANNELS * (MAX_CHANNELS + 2 + 3 * MAX_CHANNELS * MAX_CHANNELS
     * + MAX_CHANNELS + MAX_BANDS + 1)
     */

    /** in the one channel case channel transforms are pointless */
    s->num_chgroups = 0;
    if (s->num_channels > 1) {
        int remaining_channels = s->channels_for_cur_subframe;

        if (get_bits1(&s->gb)) {
            av_log_ask_for_sample(s->avctx,
                                  "unsupported channel transform bit\n");
            return AVERROR_INVALIDDATA;
        }

        for (s->num_chgroups = 0; remaining_channels &&
             s->num_chgroups < s->channels_for_cur_subframe; s->num_chgroups++) {
            WMAProChannelGrp* chgroup = &s->chgroup[s->num_chgroups];
            float** channel_data = chgroup->channel_data;
            chgroup->num_channels = 0;
            chgroup->transform = 0;

            /** decode channel mask */
            if (remaining_channels > 2) {
                for (i = 0; i < s->channels_for_cur_subframe; i++) {
                    int channel_idx = s->channel_indexes_for_cur_subframe[i];
                    if (!s->channel[channel_idx].grouped
                        && get_bits1(&s->gb)) {
                        ++chgroup->num_channels;
                        s->channel[channel_idx].grouped = 1;
                        *channel_data++ = s->channel[channel_idx].coeffs;
                    }
                }
            } else {
                chgroup->num_channels = remaining_channels;
                for (i = 0; i < s->channels_for_cur_subframe; i++) {
                    int channel_idx = s->channel_indexes_for_cur_subframe[i];
                    if (!s->channel[channel_idx].grouped)
                        *channel_data++ = s->channel[channel_idx].coeffs;
                    s->channel[channel_idx].grouped = 1;
                }
            }

            /** decode transform type */
            if (chgroup->num_channels == 2) {
                if (get_bits1(&s->gb)) {
                    if (get_bits1(&s->gb)) {
                        av_log_ask_for_sample(s->avctx,
                                              "unsupported channel transform type\n");
                    }
                } else {
                    chgroup->transform = 1;
                    if (s->num_channels == 2) {
                        chgroup->decorrelation_matrix[0] =  1.0;
                        chgroup->decorrelation_matrix[1] = -1.0;
                        chgroup->decorrelation_matrix[2] =  1.0;
                        chgroup->decorrelation_matrix[3] =  1.0;
                    } else {
                        /** cos(pi/4) */
                        chgroup->decorrelation_matrix[0] =  0.70703125;
                        chgroup->decorrelation_matrix[1] = -0.70703125;
                        chgroup->decorrelation_matrix[2] =  0.70703125;
                        chgroup->decorrelation_matrix[3] =  0.70703125;
                    }
                }
            } else if (chgroup->num_channels > 2) {
                if (get_bits1(&s->gb)) {
                    chgroup->transform = 1;
                    if (get_bits1(&s->gb)) {
                        decode_decorrelation_matrix(s, chgroup);
                    } else {
                        /** FIXME: more than 6 coupled channels not supported */
                        if (chgroup->num_channels > 6) {
                            av_log_ask_for_sample(s->avctx,
                                                  "coupled channels > 6\n");
                        } else {
                            memcpy(chgroup->decorrelation_matrix,
                                   default_decorrelation[chgroup->num_channels],
                                   chgroup->num_channels * chgroup->num_channels *
                                   sizeof(*chgroup->decorrelation_matrix));
                        }
                    }
                }
            }

            /** decode transform on / off */
            if (chgroup->transform) {
                if (!get_bits1(&s->gb)) {
                    int i;
                    /** transform can be enabled for individual bands */
                    for (i = 0; i < s->num_bands; i++) {
                        chgroup->transform_band[i] = get_bits1(&s->gb);
                    }
                } else {
                    memset(chgroup->transform_band, 1, s->num_bands);
                }
            }
            remaining_channels -= chgroup->num_channels;
        }
    }
    return 0;
}

/**
 *@brief Extract the coefficients from the bitstream.
 *@param s codec context
 *@param c current channel number
 *@return 0 on success, < 0 in case of bitstream errors
 */
static int decode_coeffs(WMAProDecodeCtx *s, int c)
{
    /* Integers 0..15 as single-precision floats.  The table saves a
       costly int to float conversion, and storing the values as
       integers allows fast sign-flipping. */
    static const int fval_tab[16] = {
        0x00000000, 0x3f800000, 0x40000000, 0x40400000,
        0x40800000, 0x40a00000, 0x40c00000, 0x40e00000,
        0x41000000, 0x41100000, 0x41200000, 0x41300000,
        0x41400000, 0x41500000, 0x41600000, 0x41700000,
    };
    int vlctable;
    VLC* vlc;
    WMAProChannelCtx* ci = &s->channel[c];
    int rl_mode = 0;
    int cur_coeff = 0;
    int num_zeros = 0;
    const uint16_t* run;
    const float* level;

    dprintf(s->avctx, "decode coefficients for channel %i\n", c);

    vlctable = get_bits1(&s->gb);
    vlc = &coef_vlc[vlctable];

    if (vlctable) {
        run = coef1_run;
        level = coef1_level;
    } else {
        run = coef0_run;
        level = coef0_level;
    }

    /** decode vector coefficients (consumes up to 167 bits per iteration for
      4 vector coded large values) */
    while (!rl_mode && cur_coeff + 3 < s->subframe_len) {
        int vals[4];
        int i;
        unsigned int idx;

        idx = get_vlc2(&s->gb, vec4_vlc.table, VLCBITS, VEC4MAXDEPTH);

        if (idx == HUFF_VEC4_SIZE - 1) {
            for (i = 0; i < 4; i += 2) {
                idx = get_vlc2(&s->gb, vec2_vlc.table, VLCBITS, VEC2MAXDEPTH);
                if (idx == HUFF_VEC2_SIZE - 1) {
                    int v0, v1;
                    v0 = get_vlc2(&s->gb, vec1_vlc.table, VLCBITS, VEC1MAXDEPTH);
                    if (v0 == HUFF_VEC1_SIZE - 1)
                        v0 += ff_wma_get_large_val(&s->gb);
                    v1 = get_vlc2(&s->gb, vec1_vlc.table, VLCBITS, VEC1MAXDEPTH);
                    if (v1 == HUFF_VEC1_SIZE - 1)
                        v1 += ff_wma_get_large_val(&s->gb);
                    ((float*)vals)[i  ] = v0;
                    ((float*)vals)[i+1] = v1;
                } else {
                    vals[i]   = fval_tab[symbol_to_vec2[idx] >> 4 ];
                    vals[i+1] = fval_tab[symbol_to_vec2[idx] & 0xF];
                }
            }
        } else {
            vals[0] = fval_tab[ symbol_to_vec4[idx] >> 12      ];
            vals[1] = fval_tab[(symbol_to_vec4[idx] >> 8) & 0xF];
            vals[2] = fval_tab[(symbol_to_vec4[idx] >> 4) & 0xF];
            vals[3] = fval_tab[ symbol_to_vec4[idx]       & 0xF];
        }

        /** decode sign */
        for (i = 0; i < 4; i++) {
            if (vals[i]) {
                int sign = get_bits1(&s->gb) - 1;
                *(uint32_t*)&ci->coeffs[cur_coeff] = vals[i] ^ sign<<31;
                num_zeros = 0;
            } else {
                ci->coeffs[cur_coeff] = 0;
                /** switch to run level mode when subframe_len / 128 zeros
                    were found in a row */
                rl_mode |= (++num_zeros > s->subframe_len >> 8);
            }
            ++cur_coeff;
        }
    }

    /** decode run level coded coefficients */
    if (rl_mode) {
        memset(&ci->coeffs[cur_coeff], 0,
               sizeof(*ci->coeffs) * (s->subframe_len - cur_coeff));
        if (ff_wma_run_level_decode(s->avctx, &s->gb, vlc,
                                    level, run, 1, ci->coeffs,
                                    cur_coeff, s->subframe_len,
                                    s->subframe_len, s->esc_len, 0))
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

/**
 *@brief Extract scale factors from the bitstream.
 *@param s codec context
 *@return 0 on success, < 0 in case of bitstream errors
 */
static int decode_scale_factors(WMAProDecodeCtx* s)
{
    int i;

    /** should never consume more than 5344 bits
     *  MAX_CHANNELS * (1 +  MAX_BANDS * 23)
     */

    for (i = 0; i < s->channels_for_cur_subframe; i++) {
        int c = s->channel_indexes_for_cur_subframe[i];
        int* sf;
        int* sf_end;
        s->channel[c].scale_factors = s->channel[c].saved_scale_factors[!s->channel[c].scale_factor_idx];
        sf_end = s->channel[c].scale_factors + s->num_bands;

        /** resample scale factors for the new block size
         *  as the scale factors might need to be resampled several times
         *  before some  new values are transmitted, a backup of the last
         *  transmitted scale factors is kept in saved_scale_factors
         */
        if (s->channel[c].reuse_sf) {
            const int8_t* sf_offsets = s->sf_offsets[s->table_idx][s->channel[c].table_idx];
            int b;
            for (b = 0; b < s->num_bands; b++)
                s->channel[c].scale_factors[b] =
                    s->channel[c].saved_scale_factors[s->channel[c].scale_factor_idx][*sf_offsets++];
        }

        if (!s->channel[c].cur_subframe || get_bits1(&s->gb)) {

            if (!s->channel[c].reuse_sf) {
                int val;
                /** decode DPCM coded scale factors */
                s->channel[c].scale_factor_step = get_bits(&s->gb, 2) + 1;
                val = 45 / s->channel[c].scale_factor_step;
                for (sf = s->channel[c].scale_factors; sf < sf_end; sf++) {
                    val += get_vlc2(&s->gb, sf_vlc.table, SCALEVLCBITS, SCALEMAXDEPTH) - 60;
                    *sf = val;
                }
            } else {
                int i;
                /** run level decode differences to the resampled factors */
                for (i = 0; i < s->num_bands; i++) {
                    int idx;
                    int skip;
                    int val;
                    int sign;

                    idx = get_vlc2(&s->gb, sf_rl_vlc.table, VLCBITS, SCALERLMAXDEPTH);

                    if (!idx) {
                        uint32_t code = get_bits(&s->gb, 14);
                        val  =  code >> 6;
                        sign = (code & 1) - 1;
                        skip = (code & 0x3f) >> 1;
                    } else if (idx == 1) {
                        break;
                    } else {
                        skip = scale_rl_run[idx];
                        val  = scale_rl_level[idx];
                        sign = get_bits1(&s->gb)-1;
                    }

                    i += skip;
                    if (i >= s->num_bands) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "invalid scale factor coding\n");
                        return AVERROR_INVALIDDATA;
                    }
                    s->channel[c].scale_factors[i] += (val ^ sign) - sign;
                }
            }
            /** swap buffers */
            s->channel[c].scale_factor_idx = !s->channel[c].scale_factor_idx;
            s->channel[c].table_idx = s->table_idx;
            s->channel[c].reuse_sf  = 1;
        }

        /** calculate new scale factor maximum */
        s->channel[c].max_scale_factor = s->channel[c].scale_factors[0];
        for (sf = s->channel[c].scale_factors + 1; sf < sf_end; sf++) {
            s->channel[c].max_scale_factor =
                FFMAX(s->channel[c].max_scale_factor, *sf);
        }

    }
    return 0;
}

/**
 *@brief Reconstruct the individual channel data.
 *@param s codec context
 */
static void inverse_channel_transform(WMAProDecodeCtx *s)
{
    int i;

    for (i = 0; i < s->num_chgroups; i++) {
        if (s->chgroup[i].transform) {
            float data[WMAPRO_MAX_CHANNELS];
            const int num_channels = s->chgroup[i].num_channels;
            float** ch_data = s->chgroup[i].channel_data;
            float** ch_end = ch_data + num_channels;
            const int8_t* tb = s->chgroup[i].transform_band;
            int16_t* sfb;

            /** multichannel decorrelation */
            for (sfb = s->cur_sfb_offsets;
                 sfb < s->cur_sfb_offsets + s->num_bands; sfb++) {
                int y;
                if (*tb++ == 1) {
                    /** multiply values with the decorrelation_matrix */
                    for (y = sfb[0]; y < FFMIN(sfb[1], s->subframe_len); y++) {
                        const float* mat = s->chgroup[i].decorrelation_matrix;
                        const float* data_end = data + num_channels;
                        float* data_ptr = data;
                        float** ch;

                        for (ch = ch_data; ch < ch_end; ch++)
                            *data_ptr++ = (*ch)[y];

                        for (ch = ch_data; ch < ch_end; ch++) {
                            float sum = 0;
                            data_ptr = data;
                            while (data_ptr < data_end)
                                sum += *data_ptr++ * *mat++;

                            (*ch)[y] = sum;
                        }
                    }
                } else if (s->num_channels == 2) {
                    int len = FFMIN(sfb[1], s->subframe_len) - sfb[0];
                    s->dsp.vector_fmul_scalar(ch_data[0] + sfb[0],
                                              ch_data[0] + sfb[0],
                                              181.0 / 128, len);
                    s->dsp.vector_fmul_scalar(ch_data[1] + sfb[0],
                                              ch_data[1] + sfb[0],
                                              181.0 / 128, len);
                }
            }
        }
    }
}

/**
 *@brief Apply sine window and reconstruct the output buffer.
 *@param s codec context
 */
static void wmapro_window(WMAProDecodeCtx *s)
{
    int i;
    for (i = 0; i < s->channels_for_cur_subframe; i++) {
        int c = s->channel_indexes_for_cur_subframe[i];
        float* window;
        int winlen = s->channel[c].prev_block_len;
        float* start = s->channel[c].coeffs - (winlen >> 1);

        if (s->subframe_len < winlen) {
            start += (winlen - s->subframe_len) >> 1;
            winlen = s->subframe_len;
        }

        window = s->windows[av_log2(winlen) - BLOCK_MIN_BITS];

        winlen >>= 1;

        s->dsp.vector_fmul_window(start, start, start + winlen,
                                  window, 0, winlen);

        s->channel[c].prev_block_len = s->subframe_len;
    }
}

/**
 *@brief Decode a single subframe (block).
 *@param s codec context
 *@return 0 on success, < 0 when decoding failed
 */
static int decode_subframe(WMAProDecodeCtx *s)
{
    int offset = s->samples_per_frame;
    int subframe_len = s->samples_per_frame;
    int i;
    int total_samples   = s->samples_per_frame * s->num_channels;
    int transmit_coeffs = 0;
    int cur_subwoofer_cutoff;

    s->subframe_offset = get_bits_count(&s->gb);

    /** reset channel context and find the next block offset and size
        == the next block of the channel with the smallest number of
        decoded samples
    */
    for (i = 0; i < s->num_channels; i++) {
        s->channel[i].grouped = 0;
        if (offset > s->channel[i].decoded_samples) {
            offset = s->channel[i].decoded_samples;
            subframe_len =
                s->channel[i].subframe_len[s->channel[i].cur_subframe];
        }
    }

    dprintf(s->avctx,
            "processing subframe with offset %i len %i\n", offset, subframe_len);

    /** get a list of all channels that contain the estimated block */
    s->channels_for_cur_subframe = 0;
    for (i = 0; i < s->num_channels; i++) {
        const int cur_subframe = s->channel[i].cur_subframe;
        /** substract already processed samples */
        total_samples -= s->channel[i].decoded_samples;

        /** and count if there are multiple subframes that match our profile */
        if (offset == s->channel[i].decoded_samples &&
            subframe_len == s->channel[i].subframe_len[cur_subframe]) {
            total_samples -= s->channel[i].subframe_len[cur_subframe];
            s->channel[i].decoded_samples +=
                s->channel[i].subframe_len[cur_subframe];
            s->channel_indexes_for_cur_subframe[s->channels_for_cur_subframe] = i;
            ++s->channels_for_cur_subframe;
        }
    }

    /** check if the frame will be complete after processing the
        estimated block */
    if (!total_samples)
        s->parsed_all_subframes = 1;


    dprintf(s->avctx, "subframe is part of %i channels\n",
            s->channels_for_cur_subframe);

    /** calculate number of scale factor bands and their offsets */
    s->table_idx         = av_log2(s->samples_per_frame/subframe_len);
    s->num_bands         = s->num_sfb[s->table_idx];
    s->cur_sfb_offsets   = s->sfb_offsets[s->table_idx];
    cur_subwoofer_cutoff = s->subwoofer_cutoffs[s->table_idx];

    /** configure the decoder for the current subframe */
    for (i = 0; i < s->channels_for_cur_subframe; i++) {
        int c = s->channel_indexes_for_cur_subframe[i];

        s->channel[c].coeffs = &s->channel[c].out[(s->samples_per_frame >> 1)
                                                  + offset];
    }

    s->subframe_len = subframe_len;
    s->esc_len = av_log2(s->subframe_len - 1) + 1;

    /** skip extended header if any */
    if (get_bits1(&s->gb)) {
        int num_fill_bits;
        if (!(num_fill_bits = get_bits(&s->gb, 2))) {
            int len = get_bits(&s->gb, 4);
            num_fill_bits = get_bits(&s->gb, len) + 1;
        }

        if (num_fill_bits >= 0) {
            if (get_bits_count(&s->gb) + num_fill_bits > s->num_saved_bits) {
                av_log(s->avctx, AV_LOG_ERROR, "invalid number of fill bits\n");
                return AVERROR_INVALIDDATA;
            }

            skip_bits_long(&s->gb, num_fill_bits);
        }
    }

    /** no idea for what the following bit is used */
    if (get_bits1(&s->gb)) {
        av_log_ask_for_sample(s->avctx, "reserved bit set\n");
        return AVERROR_INVALIDDATA;
    }


    if (decode_channel_transform(s) < 0)
        return AVERROR_INVALIDDATA;


    for (i = 0; i < s->channels_for_cur_subframe; i++) {
        int c = s->channel_indexes_for_cur_subframe[i];
        if ((s->channel[c].transmit_coefs = get_bits1(&s->gb)))
            transmit_coeffs = 1;
    }

    if (transmit_coeffs) {
        int step;
        int quant_step = 90 * s->bits_per_sample >> 4;
        if ((get_bits1(&s->gb))) {
            /** FIXME: might change run level mode decision */
            av_log_ask_for_sample(s->avctx, "unsupported quant step coding\n");
            return AVERROR_INVALIDDATA;
        }
        /** decode quantization step */
        step = get_sbits(&s->gb, 6);
        quant_step += step;
        if (step == -32 || step == 31) {
            const int sign = (step == 31) - 1;
            int quant = 0;
            while (get_bits_count(&s->gb) + 5 < s->num_saved_bits &&
                   (step = get_bits(&s->gb, 5)) == 31) {
                quant += 31;
            }
            quant_step += ((quant + step) ^ sign) - sign;
        }
        if (quant_step < 0) {
            av_log(s->avctx, AV_LOG_DEBUG, "negative quant step\n");
        }

        /** decode quantization step modifiers for every channel */

        if (s->channels_for_cur_subframe == 1) {
            s->channel[s->channel_indexes_for_cur_subframe[0]].quant_step = quant_step;
        } else {
            int modifier_len = get_bits(&s->gb, 3);
            for (i = 0; i < s->channels_for_cur_subframe; i++) {
                int c = s->channel_indexes_for_cur_subframe[i];
                s->channel[c].quant_step = quant_step;
                if (get_bits1(&s->gb)) {
                    if (modifier_len) {
                        s->channel[c].quant_step += get_bits(&s->gb, modifier_len) + 1;
                    } else
                        ++s->channel[c].quant_step;
                }
            }
        }

        /** decode scale factors */
        if (decode_scale_factors(s) < 0)
            return AVERROR_INVALIDDATA;
    }

    dprintf(s->avctx, "BITSTREAM: subframe header length was %i\n",
            get_bits_count(&s->gb) - s->subframe_offset);

    /** parse coefficients */
    for (i = 0; i < s->channels_for_cur_subframe; i++) {
        int c = s->channel_indexes_for_cur_subframe[i];
        if (s->channel[c].transmit_coefs &&
            get_bits_count(&s->gb) < s->num_saved_bits) {
            decode_coeffs(s, c);
        } else
            memset(s->channel[c].coeffs, 0,
                   sizeof(*s->channel[c].coeffs) * subframe_len);
    }

    dprintf(s->avctx, "BITSTREAM: subframe length was %i\n",
            get_bits_count(&s->gb) - s->subframe_offset);

    if (transmit_coeffs) {
        /** reconstruct the per channel data */
        inverse_channel_transform(s);
        for (i = 0; i < s->channels_for_cur_subframe; i++) {
            int c = s->channel_indexes_for_cur_subframe[i];
            const int* sf = s->channel[c].scale_factors;
            int b;

            if (c == s->lfe_channel)
                memset(&s->tmp[cur_subwoofer_cutoff], 0, sizeof(*s->tmp) *
                       (subframe_len - cur_subwoofer_cutoff));

            /** inverse quantization and rescaling */
            for (b = 0; b < s->num_bands; b++) {
                const int end = FFMIN(s->cur_sfb_offsets[b+1], s->subframe_len);
                const int exp = s->channel[c].quant_step -
                            (s->channel[c].max_scale_factor - *sf++) *
                            s->channel[c].scale_factor_step;
                const float quant = pow(10.0, exp / 20.0);
                int start = s->cur_sfb_offsets[b];
                s->dsp.vector_fmul_scalar(s->tmp + start,
                                          s->channel[c].coeffs + start,
                                          quant, end - start);
            }

            /** apply imdct (ff_imdct_half == DCTIV with reverse) */
            ff_imdct_half(&s->mdct_ctx[av_log2(subframe_len) - BLOCK_MIN_BITS],
                          s->channel[c].coeffs, s->tmp);
        }
    }

    /** window and overlapp-add */
    wmapro_window(s);

    /** handled one subframe */
    for (i = 0; i < s->channels_for_cur_subframe; i++) {
        int c = s->channel_indexes_for_cur_subframe[i];
        if (s->channel[c].cur_subframe >= s->channel[c].num_subframes) {
            av_log(s->avctx, AV_LOG_ERROR, "broken subframe\n");
            return AVERROR_INVALIDDATA;
        }
        ++s->channel[c].cur_subframe;
    }

    return 0;
}

/**
 *@brief Decode one WMA frame.
 *@param s codec context
 *@return 0 if the trailer bit indicates that this is the last frame,
 *        1 if there are additional frames
 */
static int decode_frame(WMAProDecodeCtx *s)
{
    GetBitContext* gb = &s->gb;
    int more_frames = 0;
    int len = 0;
    int i;

    /** check for potential output buffer overflow */
    if (s->num_channels * s->samples_per_frame > s->samples_end - s->samples) {
        /** return an error if no frame could be decoded at all */
        av_log(s->avctx, AV_LOG_ERROR,
               "not enough space for the output samples\n");
        s->packet_loss = 1;
        return 0;
    }

    /** get frame length */
    if (s->len_prefix)
        len = get_bits(gb, s->log2_frame_size);

    dprintf(s->avctx, "decoding frame with length %x\n", len);

    /** decode tile information */
    if (decode_tilehdr(s)) {
        s->packet_loss = 1;
        return 0;
    }

    /** read postproc transform */
    if (s->num_channels > 1 && get_bits1(gb)) {
        av_log_ask_for_sample(s->avctx, "Unsupported postproc transform found\n");
        s->packet_loss = 1;
        return 0;
    }

    /** read drc info */
    if (s->dynamic_range_compression) {
        s->drc_gain = get_bits(gb, 8);
        dprintf(s->avctx, "drc_gain %i\n", s->drc_gain);
    }

    /** no idea what these are for, might be the number of samples
        that need to be skipped at the beginning or end of a stream */
    if (get_bits1(gb)) {
        int skip;

        /** usually true for the first frame */
        if (get_bits1(gb)) {
            skip = get_bits(gb, av_log2(s->samples_per_frame * 2));
            dprintf(s->avctx, "start skip: %i\n", skip);
        }

        /** sometimes true for the last frame */
        if (get_bits1(gb)) {
            skip = get_bits(gb, av_log2(s->samples_per_frame * 2));
            dprintf(s->avctx, "end skip: %i\n", skip);
        }

    }

    dprintf(s->avctx, "BITSTREAM: frame header length was %i\n",
            get_bits_count(gb) - s->frame_offset);

    /** reset subframe states */
    s->parsed_all_subframes = 0;
    for (i = 0; i < s->num_channels; i++) {
        s->channel[i].decoded_samples = 0;
        s->channel[i].cur_subframe    = 0;
        s->channel[i].reuse_sf        = 0;
    }

    /** decode all subframes */
    while (!s->parsed_all_subframes) {
        if (decode_subframe(s) < 0) {
            s->packet_loss = 1;
            return 0;
        }
    }

    /** interleave samples and write them to the output buffer */
    for (i = 0; i < s->num_channels; i++) {
        float* ptr  = s->samples + i;
        int incr = s->num_channels;
        float* iptr = s->channel[i].out;
        float* iend = iptr + s->samples_per_frame;

        // FIXME should create/use a DSP function here
        while (iptr < iend) {
            *ptr = *iptr++;
            ptr += incr;
        }

        /** reuse second half of the IMDCT output for the next frame */
        memcpy(&s->channel[i].out[0],
               &s->channel[i].out[s->samples_per_frame],
               s->samples_per_frame * sizeof(*s->channel[i].out) >> 1);
    }

    if (s->skip_frame) {
        s->skip_frame = 0;
    } else
        s->samples += s->num_channels * s->samples_per_frame;

    if (len != (get_bits_count(gb) - s->frame_offset) + 2) {
        /** FIXME: not sure if this is always an error */
        av_log(s->avctx, AV_LOG_ERROR, "frame[%i] would have to skip %i bits\n",
               s->frame_num, len - (get_bits_count(gb) - s->frame_offset) - 1);
        s->packet_loss = 1;
        return 0;
    }

    /** skip the rest of the frame data */
    skip_bits_long(gb, len - (get_bits_count(gb) - s->frame_offset) - 1);

    /** decode trailer bit */
    more_frames = get_bits1(gb);

    ++s->frame_num;
    return more_frames;
}

/**
 *@brief Calculate remaining input buffer length.
 *@param s codec context
 *@param gb bitstream reader context
 *@return remaining size in bits
 */
static int remaining_bits(WMAProDecodeCtx *s, GetBitContext *gb)
{
    return s->buf_bit_size - get_bits_count(gb);
}

/**
 *@brief Fill the bit reservoir with a (partial) frame.
 *@param s codec context
 *@param gb bitstream reader context
 *@param len length of the partial frame
 *@param append decides wether to reset the buffer or not
 */
static void save_bits(WMAProDecodeCtx *s, GetBitContext* gb, int len,
                      int append)
{
    int buflen;

    /** when the frame data does not need to be concatenated, the input buffer
        is resetted and additional bits from the previous frame are copyed
        and skipped later so that a fast byte copy is possible */

    if (!append) {
        s->frame_offset = get_bits_count(gb) & 7;
        s->num_saved_bits = s->frame_offset;
        init_put_bits(&s->pb, s->frame_data, MAX_FRAMESIZE);
    }

    buflen = (s->num_saved_bits + len + 8) >> 3;

    if (len <= 0 || buflen > MAX_FRAMESIZE) {
        av_log_ask_for_sample(s->avctx, "input buffer too small\n");
        s->packet_loss = 1;
        return;
    }

    s->num_saved_bits += len;
    if (!append) {
        ff_copy_bits(&s->pb, gb->buffer + (get_bits_count(gb) >> 3),
                     s->num_saved_bits);
    } else {
        int align = 8 - (get_bits_count(gb) & 7);
        align = FFMIN(align, len);
        put_bits(&s->pb, align, get_bits(gb, align));
        len -= align;
        ff_copy_bits(&s->pb, gb->buffer + (get_bits_count(gb) >> 3), len);
    }
    skip_bits_long(gb, len);

    {
        PutBitContext tmp = s->pb;
        flush_put_bits(&tmp);
    }

    init_get_bits(&s->gb, s->frame_data, s->num_saved_bits);
    skip_bits(&s->gb, s->frame_offset);
}

/**
 *@brief Decode a single WMA packet.
 *@param avctx codec context
 *@param data the output buffer
 *@param data_size number of bytes that were written to the output buffer
 *@param avpkt input packet
 *@return number of bytes that were read from the input buffer
 */
static int decode_packet(AVCodecContext *avctx,
                         void *data, int *data_size, AVPacket* avpkt)
{
    WMAProDecodeCtx *s = avctx->priv_data;
    GetBitContext* gb  = &s->pgb;
    const uint8_t* buf = avpkt->data;
    int buf_size       = avpkt->size;
    int num_bits_prev_frame;
    int packet_sequence_number;

    s->samples       = data;
    s->samples_end   = (float*)((int8_t*)data + *data_size);
    *data_size = 0;

    if (s->packet_done || s->packet_loss) {
        s->packet_done = 0;
        s->buf_bit_size = buf_size << 3;

        /** sanity check for the buffer length */
        if (buf_size < avctx->block_align)
            return 0;

        buf_size = avctx->block_align;

        /** parse packet header */
        init_get_bits(gb, buf, s->buf_bit_size);
        packet_sequence_number = get_bits(gb, 4);
        skip_bits(gb, 2);

        /** get number of bits that need to be added to the previous frame */
        num_bits_prev_frame = get_bits(gb, s->log2_frame_size);
        dprintf(avctx, "packet[%d]: nbpf %x\n", avctx->frame_number,
                num_bits_prev_frame);

        /** check for packet loss */
        if (!s->packet_loss &&
            ((s->packet_sequence_number + 1) & 0xF) != packet_sequence_number) {
            s->packet_loss = 1;
            av_log(avctx, AV_LOG_ERROR, "Packet loss detected! seq %x vs %x\n",
                   s->packet_sequence_number, packet_sequence_number);
        }
        s->packet_sequence_number = packet_sequence_number;

        if (num_bits_prev_frame > 0) {
            /** append the previous frame data to the remaining data from the
                previous packet to create a full frame */
            save_bits(s, gb, num_bits_prev_frame, 1);
            dprintf(avctx, "accumulated %x bits of frame data\n",
                    s->num_saved_bits - s->frame_offset);

            /** decode the cross packet frame if it is valid */
            if (!s->packet_loss)
                decode_frame(s);
        } else if (s->num_saved_bits - s->frame_offset) {
            dprintf(avctx, "ignoring %x previously saved bits\n",
                    s->num_saved_bits - s->frame_offset);
        }

        s->packet_loss = 0;

    } else {
        int frame_size;
        s->buf_bit_size = avpkt->size << 3;
        init_get_bits(gb, avpkt->data, s->buf_bit_size);
        skip_bits(gb, s->packet_offset);
        if (remaining_bits(s, gb) > s->log2_frame_size &&
            (frame_size = show_bits(gb, s->log2_frame_size)) &&
            frame_size <= remaining_bits(s, gb)) {
            save_bits(s, gb, frame_size, 0);
            s->packet_done = !decode_frame(s);
        } else
            s->packet_done = 1;
    }

    if (s->packet_done && !s->packet_loss &&
        remaining_bits(s, gb) > 0) {
        /** save the rest of the data so that it can be decoded
            with the next packet */
        save_bits(s, gb, remaining_bits(s, gb), 0);
    }

    *data_size = (int8_t *)s->samples - (int8_t *)data;
    s->packet_offset = get_bits_count(gb) & 7;

    return (s->packet_loss) ? AVERROR_INVALIDDATA : get_bits_count(gb) >> 3;
}

/**
 *@brief Clear decoder buffers (for seeking).
 *@param avctx codec context
 */
static void flush(AVCodecContext *avctx)
{
    WMAProDecodeCtx *s = avctx->priv_data;
    int i;
    /** reset output buffer as a part of it is used during the windowing of a
        new frame */
    for (i = 0; i < s->num_channels; i++)
        memset(s->channel[i].out, 0, s->samples_per_frame *
               sizeof(*s->channel[i].out));
    s->packet_loss = 1;
}


/**
 *@brief wmapro decoder
 */
AVCodec wmapro_decoder = {
    "wmapro",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_WMAPRO,
    sizeof(WMAProDecodeCtx),
    decode_init,
    NULL,
    decode_end,
    decode_packet,
    .capabilities = CODEC_CAP_SUBFRAMES,
    .flush= flush,
    .long_name = NULL_IF_CONFIG_SMALL("Windows Media Audio 9 Professional"),
};
