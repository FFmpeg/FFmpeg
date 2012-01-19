/*
 * Wmall compatible decoder
 * Copyright (c) 2007 Baptiste Coudurier, Benjamin Larsson, Ulion
 * Copyright (c) 2008 - 2011 Sascha Sommer, Benjamin Larsson
 * Copyright (c) 2011 Andreas Öman
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
 * @brief wmall decoder implementation
 * Wmall is an MDCT based codec comparable to wma standard or AAC.
 * The decoding therefore consists of the following steps:
 * - bitstream decoding
 * - reconstruction of per-channel data
 * - rescaling and inverse quantization
 * - IMDCT
 * - windowing and overlapp-add
 *
 * The compressed wmall bitstream is split into individual packets.
 * Every such packet contains one or more wma frames.
 * The compressed frames may have a variable length and frames may
 * cross packet boundaries.
 * Common to all wmall frames is the number of samples that are stored in
 * a frame.
 * The number of samples and a few other decode flags are stored
 * as extradata that has to be passed to the decoder.
 *
 * The wmall frames themselves are again split into a variable number of
 * subframes. Every subframe contains the data for 2^N time domain samples
 * where N varies between 7 and 12.
 *
 * Example wmall bitstream (in samples):
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
#include "dsputil.h"
#include "wma.h"

/** current decoder limitations */
#define WMALL_MAX_CHANNELS    8                             ///< max number of handled channels
#define MAX_SUBFRAMES  32                                    ///< max number of subframes per channel
#define MAX_BANDS      29                                    ///< max number of scale factor bands
#define MAX_FRAMESIZE  32768                                 ///< maximum compressed frame size

#define WMALL_BLOCK_MIN_BITS  6                                           ///< log2 of min block size
#define WMALL_BLOCK_MAX_BITS 12                                           ///< log2 of max block size
#define WMALL_BLOCK_MAX_SIZE (1 << WMALL_BLOCK_MAX_BITS)                 ///< maximum block size
#define WMALL_BLOCK_SIZES    (WMALL_BLOCK_MAX_BITS - WMALL_BLOCK_MIN_BITS + 1) ///< possible block sizes


#define VLCBITS            9
#define SCALEVLCBITS       8
#define VEC4MAXDEPTH    ((HUFF_VEC4_MAXBITS+VLCBITS-1)/VLCBITS)
#define VEC2MAXDEPTH    ((HUFF_VEC2_MAXBITS+VLCBITS-1)/VLCBITS)
#define VEC1MAXDEPTH    ((HUFF_VEC1_MAXBITS+VLCBITS-1)/VLCBITS)
#define SCALEMAXDEPTH   ((HUFF_SCALE_MAXBITS+SCALEVLCBITS-1)/SCALEVLCBITS)
#define SCALERLMAXDEPTH ((HUFF_SCALE_RL_MAXBITS+VLCBITS-1)/VLCBITS)

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
    uint16_t num_vec_coeffs;                          ///< number of vector coded coefficients
    DECLARE_ALIGNED(16, float, out)[WMALL_BLOCK_MAX_SIZE + WMALL_BLOCK_MAX_SIZE / 2]; ///< output buffer
    int      transient_counter;                       ///< number of transient samples from the beginning of transient zone
} WmallChannelCtx;

/**
 * @brief channel group for channel transformations
 */
typedef struct {
    uint8_t num_channels;                                     ///< number of channels in the group
    int8_t  transform;                                        ///< transform on / off
    int8_t  transform_band[MAX_BANDS];                        ///< controls if the transform is enabled for a certain band
    float   decorrelation_matrix[WMALL_MAX_CHANNELS*WMALL_MAX_CHANNELS];
    float*  channel_data[WMALL_MAX_CHANNELS];                ///< transformation coefficients
} WmallChannelGrp;

/**
 * @brief main decoder context
 */
typedef struct WmallDecodeCtx {
    /* generic decoder variables */
    AVCodecContext*  avctx;                         ///< codec context for av_log
    DSPContext       dsp;                           ///< accelerated DSP functions
    uint8_t          frame_data[MAX_FRAMESIZE +
                      FF_INPUT_BUFFER_PADDING_SIZE];///< compressed frame data
    PutBitContext    pb;                            ///< context for filling the frame_data buffer
    FFTContext       mdct_ctx[WMALL_BLOCK_SIZES];  ///< MDCT context per block size
    DECLARE_ALIGNED(16, float, tmp)[WMALL_BLOCK_MAX_SIZE]; ///< IMDCT output buffer
    float*           windows[WMALL_BLOCK_SIZES];   ///< windows for the different block sizes

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
    int8_t           num_sfb[WMALL_BLOCK_SIZES];   ///< scale factor bands per block size
    int16_t          sfb_offsets[WMALL_BLOCK_SIZES][MAX_BANDS];                    ///< scale factor band offsets (multiples of 4)
    int8_t           sf_offsets[WMALL_BLOCK_SIZES][WMALL_BLOCK_SIZES][MAX_BANDS]; ///< scale factor resample matrix
    int16_t          subwoofer_cutoffs[WMALL_BLOCK_SIZES]; ///< subwoofer cutoff values

    /* packet decode state */
    GetBitContext    pgb;                           ///< bitstream reader context for the packet
    int              next_packet_start;             ///< start offset of the next wma packet in the demuxer packet
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
    int16_t*         samples_16;                    ///< current samplebuffer pointer (16-bit)
    int16_t*         samples_16_end;                ///< maximum samplebuffer pointer
    int16_t*         samples_32;                    ///< current samplebuffer pointer (24-bit)
    int16_t*         samples_32_end;                ///< maximum samplebuffer pointer
    uint8_t          drc_gain;                      ///< gain for the DRC tool
    int8_t           skip_frame;                    ///< skip output step
    int8_t           parsed_all_subframes;          ///< all subframes decoded?

    /* subframe/block decode state */
    int16_t          subframe_len;                  ///< current subframe length
    int8_t           channels_for_cur_subframe;     ///< number of channels that contain the subframe
    int8_t           channel_indexes_for_cur_subframe[WMALL_MAX_CHANNELS];
    int8_t           num_bands;                     ///< number of scale factor bands
    int8_t           transmit_num_vec_coeffs;       ///< number of vector coded coefficients is part of the bitstream
    int16_t*         cur_sfb_offsets;               ///< sfb offsets for the current block
    uint8_t          table_idx;                     ///< index for the num_sfb, sfb_offsets, sf_offsets and subwoofer_cutoffs tables
    int8_t           esc_len;                       ///< length of escaped coefficients

    uint8_t          num_chgroups;                  ///< number of channel groups
    WmallChannelGrp chgroup[WMALL_MAX_CHANNELS];    ///< channel group information

    WmallChannelCtx channel[WMALL_MAX_CHANNELS];    ///< per channel data

    // WMA lossless

    uint8_t do_arith_coding;
    uint8_t do_ac_filter;
    uint8_t do_inter_ch_decorr;
    uint8_t do_mclms;
    uint8_t do_lpc;

    int8_t acfilter_order;
    int8_t acfilter_scaling;
    int64_t acfilter_coeffs[16];
    int acfilter_prevvalues[2][16];

    int8_t mclms_order;
    int8_t mclms_scaling;
    int16_t mclms_coeffs[128];
    int16_t mclms_coeffs_cur[4];
    int16_t mclms_prevvalues[64];   // FIXME: should be 32-bit / 16-bit depending on bit-depth
    int16_t mclms_updates[64];
    int mclms_recent;

    int movave_scaling;
    int quant_stepsize;

    struct {
        int order;
        int scaling;
        int coefsend;
        int bitsend;
        int16_t coefs[256];
    int16_t lms_prevvalues[512];    // FIXME: see above
    int16_t lms_updates[512];   // and here too
    int recent;
    } cdlms[2][9];              /* XXX: Here, 2 is the max. no. of channels allowed,
                                        9 is the maximum no. of filters per channel.
                                        Question is, why 2 if WMALL_MAX_CHANNELS == 8 */


    int cdlms_ttl[2];

    int bV3RTM;

    int is_channel_coded[2];    // XXX: same question as above applies here too (and below)
    int update_speed[2];

    int transient[2];
    int transient_pos[2];
    int seekable_tile;

    int ave_sum[2];

    int channel_residues[2][2048];


    int lpc_coefs[2][40];
    int lpc_order;
    int lpc_scaling;
    int lpc_intbits;

    int channel_coeffs[2][2048]; // FIXME: should be 32-bit / 16-bit depending on bit-depth

} WmallDecodeCtx;


#undef dprintf
#define dprintf(pctx, ...) av_log(pctx, AV_LOG_DEBUG, __VA_ARGS__)


static int num_logged_tiles = 0;
static int num_logged_subframes = 0;
static int num_lms_update_call = 0;

/**
 *@brief helper function to print the most important members of the context
 *@param s context
 */
static void av_cold dump_context(WmallDecodeCtx *s)
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

static void dump_int_buffer(uint8_t *buffer, int size, int length, int delimiter)
{
    int i;

    for (i=0 ; i<length ; i++) {
        if (!(i%delimiter))
            av_log(0, 0, "\n[%d] ", i);
        av_log(0, 0, "%d, ", *(int16_t *)(buffer + i * size));
    }
    av_log(0, 0, "\n");
}

/**
 *@brief Uninitialize the decoder and free all resources.
 *@param avctx codec context
 *@return 0 on success, < 0 otherwise
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    WmallDecodeCtx *s = avctx->priv_data;
    int i;

    for (i = 0; i < WMALL_BLOCK_SIZES; i++)
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
    WmallDecodeCtx *s = avctx->priv_data;
    uint8_t *edata_ptr = avctx->extradata;
    unsigned int channel_mask;
    int i;
    int log2_max_num_subframes;
    int num_possible_block_sizes;

    s->avctx = avctx;
    dsputil_init(&s->dsp, avctx);
    init_put_bits(&s->pb, s->frame_data, MAX_FRAMESIZE);

    if (avctx->extradata_size >= 18) {
        s->decode_flags    = AV_RL16(edata_ptr+14);
        channel_mask       = AV_RL32(edata_ptr+2);
        s->bits_per_sample = AV_RL16(edata_ptr);
        if (s->bits_per_sample == 16)
            avctx->sample_fmt = AV_SAMPLE_FMT_S16;
        else if (s->bits_per_sample == 24)
            avctx->sample_fmt = AV_SAMPLE_FMT_S32;
        else {
            av_log(avctx, AV_LOG_ERROR, "Unknown bit-depth: %d\n",
                   s->bits_per_sample);
            return AVERROR_INVALIDDATA;
        }
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
    s->skip_frame  = 1; /* skip first frame */
    s->packet_loss = 1;
    s->len_prefix  = (s->decode_flags & 0x40);

    /** get frame len */
    s->samples_per_frame = 1 << ff_wma_get_frame_len_bits(avctx->sample_rate,
                                                          3, s->decode_flags);

    /** init previous block len */
    for (i = 0; i < avctx->channels; i++)
        s->channel[i].prev_block_len = s->samples_per_frame;

    /** subframe info */
    log2_max_num_subframes  = ((s->decode_flags & 0x38) >> 3);
    s->max_num_subframes    = 1 << log2_max_num_subframes;
    s->max_subframe_len_bit = 0;
    s->subframe_len_bits    = av_log2(log2_max_num_subframes) + 1;

    num_possible_block_sizes     = log2_max_num_subframes + 1;
    s->min_samples_per_subframe  = s->samples_per_frame / s->max_num_subframes;
    s->dynamic_range_compression = (s->decode_flags & 0x80);

    s->bV3RTM = s->decode_flags & 0x100;

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
    } else if (s->num_channels > WMALL_MAX_CHANNELS) {
        av_log_ask_for_sample(avctx, "unsupported number of channels\n");
        return AVERROR_PATCHWELCOME;
    }

    avctx->channel_layout = channel_mask;
    return 0;
}

/**
 *@brief Decode the subframe length.
 *@param s context
 *@param offset sample offset in the frame
 *@return decoded subframe length on success, < 0 in case of an error
 */
static int decode_subframe_length(WmallDecodeCtx *s, int offset)
{
    int frame_len_ratio;
    int subframe_len, len;

    /** no need to read from the bitstream when only one length is possible */
    if (offset == s->samples_per_frame - s->min_samples_per_subframe)
        return s->min_samples_per_subframe;

    len = av_log2(s->max_num_subframes - 1) + 1;
    frame_len_ratio = get_bits(&s->gb, len);

    subframe_len = s->min_samples_per_subframe * (frame_len_ratio + 1);

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
static int decode_tilehdr(WmallDecodeCtx *s)
{
    uint16_t num_samples[WMALL_MAX_CHANNELS];        /**< sum of samples for all currently known subframes of a channel */
    uint8_t  contains_subframe[WMALL_MAX_CHANNELS];  /**< flag indicating if a channel contains the current subframe */
    int channels_for_cur_subframe = s->num_channels;  /**< number of channels that contain the current subframe */
    int fixed_channel_layout = 0;                     /**< flag indicating that all channels use the same subfra2me offsets and sizes */
    int min_channel_len = 0;                          /**< smallest sum of samples (channels with this length will be processed first) */
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
                    (min_channel_len == s->samples_per_frame - s->min_samples_per_subframe)) {
                    contains_subframe[c] = 1;
                }
                else {
                    contains_subframe[c] = get_bits1(&s->gb);
                }
            } else
                contains_subframe[c] = 0;
        }

        /** get subframe length, subframe_len == 0 is not allowed */
        if ((subframe_len = decode_subframe_length(s, min_channel_len)) <= 0)
            return AVERROR_INVALIDDATA;
        /** add subframes to the individual channels and find new min_channel_len */
        min_channel_len += subframe_len;
        for (c = 0; c < s->num_channels; c++) {
            WmallChannelCtx* chan = &s->channel[c];

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
                           "channel len(%d) > samples_per_frame(%d)\n",
                           num_samples[c], s->samples_per_frame);
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
            s->channel[c].subframe_offset[i] = offset;
            offset += s->channel[c].subframe_len[i];
        }
    }

    return 0;
}


static int my_log2(unsigned int i)
{
    unsigned int iLog2 = 0;
    while ((i >> iLog2) > 1)
        iLog2++;
    return iLog2;
}


/**
 *
 */
static void decode_ac_filter(WmallDecodeCtx *s)
{
    int i;
    s->acfilter_order = get_bits(&s->gb, 4) + 1;
    s->acfilter_scaling = get_bits(&s->gb, 4);

    for(i = 0; i < s->acfilter_order; i++) {
        s->acfilter_coeffs[i] = get_bits(&s->gb, s->acfilter_scaling) + 1;
    }
}


/**
 *
 */
static void decode_mclms(WmallDecodeCtx *s)
{
    s->mclms_order = (get_bits(&s->gb, 4) + 1) * 2;
    s->mclms_scaling = get_bits(&s->gb, 4);
    if(get_bits1(&s->gb)) {
        // mclms_send_coef
        int i;
        int send_coef_bits;
        int cbits = av_log2(s->mclms_scaling + 1);
        assert(cbits == my_log2(s->mclms_scaling + 1));
        if(1 << cbits < s->mclms_scaling + 1)
            cbits++;

        send_coef_bits = (cbits ? get_bits(&s->gb, cbits) : 0) + 2;

        for(i = 0; i < s->mclms_order * s->num_channels * s->num_channels; i++) {
            s->mclms_coeffs[i] = get_bits(&s->gb, send_coef_bits);
        }

        for(i = 0; i < s->num_channels; i++) {
            int c;
            for(c = 0; c < i; c++) {
                s->mclms_coeffs_cur[i * s->num_channels + c] = get_bits(&s->gb, send_coef_bits);
            }
        }
    }
}


/**
 *
 */
static void decode_cdlms(WmallDecodeCtx *s)
{
    int c, i;
    int cdlms_send_coef = get_bits1(&s->gb);

    for(c = 0; c < s->num_channels; c++) {
        s->cdlms_ttl[c] = get_bits(&s->gb, 3) + 1;
        for(i = 0; i < s->cdlms_ttl[c]; i++) {
            s->cdlms[c][i].order = (get_bits(&s->gb, 7) + 1) * 8;
        }

        for(i = 0; i < s->cdlms_ttl[c]; i++) {
            s->cdlms[c][i].scaling = get_bits(&s->gb, 4);
        }

        if(cdlms_send_coef) {
            for(i = 0; i < s->cdlms_ttl[c]; i++) {
                int cbits, shift_l, shift_r, j;
                cbits = av_log2(s->cdlms[c][i].order);
                if(1 << cbits < s->cdlms[c][i].order)
                    cbits++;
                s->cdlms[c][i].coefsend = get_bits(&s->gb, cbits) + 1;

                cbits = av_log2(s->cdlms[c][i].scaling + 1);
                if(1 << cbits < s->cdlms[c][i].scaling + 1)
                    cbits++;

                s->cdlms[c][i].bitsend = get_bits(&s->gb, cbits) + 2;
                shift_l = 32 - s->cdlms[c][i].bitsend;
                shift_r = 32 - 2 - s->cdlms[c][i].scaling;
                for(j = 0; j < s->cdlms[c][i].coefsend; j++) {
                    s->cdlms[c][i].coefs[j] =
                        (get_bits(&s->gb, s->cdlms[c][i].bitsend) << shift_l) >> shift_r;
                }
            }
        }
    }
}

/**
 *
 */
static int decode_channel_residues(WmallDecodeCtx *s, int ch, int tile_size)
{
    int i = 0;
    unsigned int ave_mean;
    s->transient[ch] = get_bits1(&s->gb);
    if(s->transient[ch]) {
            s->transient_pos[ch] = get_bits(&s->gb, av_log2(tile_size));
        if (s->transient_pos[ch])
                s->transient[ch] = 0;
            s->channel[ch].transient_counter =
                FFMAX(s->channel[ch].transient_counter, s->samples_per_frame / 2);
        } else if (s->channel[ch].transient_counter)
            s->transient[ch] = 1;

    if(s->seekable_tile) {
        ave_mean = get_bits(&s->gb, s->bits_per_sample);
        s->ave_sum[ch] = ave_mean << (s->movave_scaling + 1);
//        s->ave_sum[ch] *= 2;
    }

    if(s->seekable_tile) {
        if(s->do_inter_ch_decorr)
            s->channel_residues[ch][0] = get_sbits(&s->gb, s->bits_per_sample + 1);
        else
            s->channel_residues[ch][0] = get_sbits(&s->gb, s->bits_per_sample);
        i++;
    }
    //av_log(0, 0, "%8d: ", num_logged_tiles++);
    for(; i < tile_size; i++) {
        int quo = 0, rem, rem_bits, residue;
        while(get_bits1(&s->gb))
            quo++;
        if(quo >= 32)
            quo += get_bits_long(&s->gb, get_bits(&s->gb, 5) + 1);

               ave_mean = (s->ave_sum[ch] + (1 << s->movave_scaling)) >> (s->movave_scaling + 1);
        rem_bits = av_ceil_log2(ave_mean);
        rem = rem_bits ? get_bits(&s->gb, rem_bits) : 0;
        residue = (quo << rem_bits) + rem;

        s->ave_sum[ch] = residue + s->ave_sum[ch] - (s->ave_sum[ch] >> s->movave_scaling);

        if(residue & 1)
            residue = -(residue >> 1) - 1;
        else
            residue = residue >> 1;
        s->channel_residues[ch][i] = residue;
    }
    //dump_int_buffer(s->channel_residues[ch], 4, tile_size, 16);

    return 0;

}


/**
 *
 */
static void
decode_lpc(WmallDecodeCtx *s)
{
    int ch, i, cbits;
    s->lpc_order = get_bits(&s->gb, 5) + 1;
    s->lpc_scaling = get_bits(&s->gb, 4);
    s->lpc_intbits = get_bits(&s->gb, 3) + 1;
    cbits = s->lpc_scaling + s->lpc_intbits;
    for(ch = 0; ch < s->num_channels; ch++) {
        for(i = 0; i < s->lpc_order; i++) {
            s->lpc_coefs[ch][i] = get_sbits(&s->gb, cbits);
        }
    }
}


static void clear_codec_buffers(WmallDecodeCtx *s)
{
    int ich, ilms;

    memset(s->acfilter_coeffs    , 0, 16 * sizeof(int));
    memset(s->acfilter_prevvalues, 0, 16 * 2 * sizeof(int)); // may be wrong
    memset(s->lpc_coefs          , 0, 40 * 2 * sizeof(int));

    memset(s->mclms_coeffs    , 0, 128 * sizeof(int16_t));
    memset(s->mclms_coeffs_cur, 0,   4 * sizeof(int16_t));
    memset(s->mclms_prevvalues, 0,  64 * sizeof(int));
    memset(s->mclms_updates   , 0,  64 * sizeof(int16_t));

    for (ich = 0; ich < s->num_channels; ich++) {
        for (ilms = 0; ilms < s->cdlms_ttl[ich]; ilms++) {
            memset(s->cdlms[ich][ilms].coefs         , 0, 256 * sizeof(int16_t));
            memset(s->cdlms[ich][ilms].lms_prevvalues, 0, 512 * sizeof(int16_t));
            memset(s->cdlms[ich][ilms].lms_updates   , 0, 512 * sizeof(int16_t));
        }
        s->ave_sum[ich] = 0;
    }
}

/**
 *@brief Resets filter parameters and transient area at new seekable tile
 */
static void reset_codec(WmallDecodeCtx *s)
{
    int ich, ilms;
    s->mclms_recent = s->mclms_order * s->num_channels;
    for (ich = 0; ich < s->num_channels; ich++) {
        for (ilms = 0; ilms < s->cdlms_ttl[ich]; ilms++)
            s->cdlms[ich][ilms].recent = s->cdlms[ich][ilms].order;
        /* first sample of a seekable subframe is considered as the starting of
           a transient area which is samples_per_frame samples long */
        s->channel[ich].transient_counter = s->samples_per_frame;
        s->transient[ich] = 1;
        s->transient_pos[ich] = 0;
    }
}



static void mclms_update(WmallDecodeCtx *s, int icoef, int *pred)
{
    int i, j, ich;
    int pred_error;
    int order = s->mclms_order;
    int num_channels = s->num_channels;
    int range = 1 << (s->bits_per_sample - 1);
    int bps = s->bits_per_sample > 16 ? 4 : 2; // bytes per sample

    for (ich = 0; ich < num_channels; ich++) {
        pred_error = s->channel_residues[ich][icoef] - pred[ich];
        if (pred_error > 0) {
            for (i = 0; i < order * num_channels; i++)
                s->mclms_coeffs[i + ich * order * num_channels] +=
                    s->mclms_updates[s->mclms_recent + i];
            for (j = 0; j < ich; j++) {
                if (s->channel_residues[j][icoef] > 0)
                    s->mclms_coeffs_cur[ich * num_channels + j] += 1;
                else if (s->channel_residues[j][icoef] < 0)
                    s->mclms_coeffs_cur[ich * num_channels + j] -= 1;
            }
        } else if (pred_error < 0) {
            for (i = 0; i < order * num_channels; i++)
                s->mclms_coeffs[i + ich * order * num_channels] -=
                    s->mclms_updates[s->mclms_recent + i];
            for (j = 0; j < ich; j++) {
                if (s->channel_residues[j][icoef] > 0)
                    s->mclms_coeffs_cur[ich * num_channels + j] -= 1;
                else if (s->channel_residues[j][icoef] < 0)
                    s->mclms_coeffs_cur[ich * num_channels + j] += 1;
            }
        }
    }

    for (ich = num_channels - 1; ich >= 0; ich--) {
        s->mclms_recent--;
        s->mclms_prevvalues[s->mclms_recent] = s->channel_residues[ich][icoef];
        if (s->channel_residues[ich][icoef] > range - 1)
            s->mclms_prevvalues[s->mclms_recent] = range - 1;
        else if (s->channel_residues[ich][icoef] < -range)
            s->mclms_prevvalues[s->mclms_recent] = -range;

        s->mclms_updates[s->mclms_recent] = 0;
        if (s->channel_residues[ich][icoef] > 0)
            s->mclms_updates[s->mclms_recent] = 1;
        else if (s->channel_residues[ich][icoef] < 0)
            s->mclms_updates[s->mclms_recent] = -1;
    }

    if (s->mclms_recent == 0) {
        memcpy(&s->mclms_prevvalues[order * num_channels],
               s->mclms_prevvalues,
               bps * order * num_channels);
        memcpy(&s->mclms_updates[order * num_channels],
               s->mclms_updates,
               bps * order * num_channels);
        s->mclms_recent = num_channels * order;
    }
}

static void mclms_predict(WmallDecodeCtx *s, int icoef, int *pred)
{
    int ich, i;
    int order = s->mclms_order;
    int num_channels = s->num_channels;

    for (ich = 0; ich < num_channels; ich++) {
        if (!s->is_channel_coded[ich])
            continue;
        pred[ich] = 0;
        for (i = 0; i < order * num_channels; i++)
            pred[ich] += s->mclms_prevvalues[i + s->mclms_recent] *
                         s->mclms_coeffs[i + order * num_channels * ich];
        for (i = 0; i < ich; i++)
            pred[ich] += s->channel_residues[i][icoef] *
                         s->mclms_coeffs_cur[i + num_channels * ich];
        pred[ich] += 1 << s->mclms_scaling - 1;
        pred[ich] >>= s->mclms_scaling;
        s->channel_residues[ich][icoef] += pred[ich];
    }
}

static void revert_mclms(WmallDecodeCtx *s, int tile_size)
{
    int icoef, pred[s->num_channels];
    for (icoef = 0; icoef < tile_size; icoef++) {
        mclms_predict(s, icoef, pred);
        mclms_update(s, icoef, pred);
    }
}

static int lms_predict(WmallDecodeCtx *s, int ich, int ilms)
{
    int pred = 0;
    int icoef;
    int recent = s->cdlms[ich][ilms].recent;

    for (icoef = 0; icoef < s->cdlms[ich][ilms].order; icoef++)
        pred += s->cdlms[ich][ilms].coefs[icoef] *
                    s->cdlms[ich][ilms].lms_prevvalues[icoef + recent];

    //pred += (1 << (s->cdlms[ich][ilms].scaling - 1));
    /* XXX: Table 29 has:
            iPred >= cdlms[iCh][ilms].scaling;
       seems to me like a missing > */
    //pred >>= s->cdlms[ich][ilms].scaling;
    return pred;
}

static void lms_update(WmallDecodeCtx *s, int ich, int ilms, int input, int residue)
{
    int icoef;
    int recent = s->cdlms[ich][ilms].recent;
    int range = 1 << s->bits_per_sample - 1;
    int bps = s->bits_per_sample > 16 ? 4 : 2; // bytes per sample

    if (residue < 0) {
        for (icoef = 0; icoef < s->cdlms[ich][ilms].order; icoef++)
            s->cdlms[ich][ilms].coefs[icoef] -=
                s->cdlms[ich][ilms].lms_updates[icoef + recent];
    } else if (residue > 0) {
        for (icoef = 0; icoef < s->cdlms[ich][ilms].order; icoef++)
            s->cdlms[ich][ilms].coefs[icoef] +=
                s->cdlms[ich][ilms].lms_updates[icoef + recent];    /* spec mistakenly
                                                                    dropped the recent */
    }

    if (recent)
        recent--;
    else {
        /* XXX: This memcpy()s will probably fail if a fixed 32-bit buffer is used.
                follow kshishkov's suggestion of using a union. */
        memcpy(&s->cdlms[ich][ilms].lms_prevvalues[s->cdlms[ich][ilms].order],
               s->cdlms[ich][ilms].lms_prevvalues,
               bps * s->cdlms[ich][ilms].order);
        memcpy(&s->cdlms[ich][ilms].lms_updates[s->cdlms[ich][ilms].order],
               s->cdlms[ich][ilms].lms_updates,
               bps * s->cdlms[ich][ilms].order);
        recent = s->cdlms[ich][ilms].order - 1;
    }

    s->cdlms[ich][ilms].lms_prevvalues[recent] = av_clip(input, -range, range - 1);
    if (!input)
        s->cdlms[ich][ilms].lms_updates[recent] = 0;
    else if (input < 0)
        s->cdlms[ich][ilms].lms_updates[recent] = -s->update_speed[ich];
    else
        s->cdlms[ich][ilms].lms_updates[recent] = s->update_speed[ich];

    /* XXX: spec says:
    cdlms[iCh][ilms].updates[iRecent + cdlms[iCh][ilms].order >> 4] >>= 2;
    lms_updates[iCh][ilms][iRecent + cdlms[iCh][ilms].order >> 3] >>= 1;

        Questions is - are cdlms[iCh][ilms].updates[] and lms_updates[][][] two
        seperate buffers? Here I've assumed that the two are same which makes
        more sense to me.
    */
    s->cdlms[ich][ilms].lms_updates[recent + (s->cdlms[ich][ilms].order >> 4)] >>= 2;
    s->cdlms[ich][ilms].lms_updates[recent + (s->cdlms[ich][ilms].order >> 3)] >>= 1;
    s->cdlms[ich][ilms].recent = recent;
}

static void use_high_update_speed(WmallDecodeCtx *s, int ich)
{
    int ilms, recent, icoef;
    for (ilms = s->cdlms_ttl[ich] - 1; ilms >= 0; ilms--) {
        recent = s->cdlms[ich][ilms].recent;
        if (s->update_speed[ich] == 16)
            continue;
        if (s->bV3RTM) {
            for (icoef = 0; icoef < s->cdlms[ich][ilms].order; icoef++)
                s->cdlms[ich][ilms].lms_updates[icoef + recent] *= 2;
        } else {
            for (icoef = 0; icoef < s->cdlms[ich][ilms].order; icoef++)
                s->cdlms[ich][ilms].lms_updates[icoef] *= 2;
        }
    }
    s->update_speed[ich] = 16;
}

static void use_normal_update_speed(WmallDecodeCtx *s, int ich)
{
    int ilms, recent, icoef;
    for (ilms = s->cdlms_ttl[ich] - 1; ilms >= 0; ilms--) {
        recent = s->cdlms[ich][ilms].recent;
        if (s->update_speed[ich] == 8)
            continue;
        if (s->bV3RTM) {
            for (icoef = 0; icoef < s->cdlms[ich][ilms].order; icoef++)
                s->cdlms[ich][ilms].lms_updates[icoef + recent] /= 2;
        } else {
            for (icoef = 0; icoef < s->cdlms[ich][ilms].order; icoef++)
                s->cdlms[ich][ilms].lms_updates[icoef] /= 2;
        }
    }
    s->update_speed[ich] = 8;
}

static void revert_cdlms(WmallDecodeCtx *s, int ch, int coef_begin, int coef_end)
{
    int icoef;
    int pred;
    int ilms, num_lms;
    int residue, input;

    num_lms = s->cdlms_ttl[ch];
    for (ilms = num_lms - 1; ilms >= 0; ilms--) {
        //s->cdlms[ch][ilms].recent = s->cdlms[ch][ilms].order;
        for (icoef = coef_begin; icoef < coef_end; icoef++) {
            pred = 1 << (s->cdlms[ch][ilms].scaling - 1);
            residue = s->channel_residues[ch][icoef];
            pred += lms_predict(s, ch, ilms);
            input = residue + (pred >> s->cdlms[ch][ilms].scaling);
            lms_update(s, ch, ilms, input, residue);
            s->channel_residues[ch][icoef] = input;
        }
    }
}

static void revert_inter_ch_decorr(WmallDecodeCtx *s, int tile_size)
{
    int icoef;
    if (s->num_channels != 2)
        return;
    else {
        for (icoef = 0; icoef < tile_size; icoef++) {
            s->channel_residues[0][icoef] -= s->channel_residues[1][icoef] >> 1;
            s->channel_residues[1][icoef] += s->channel_residues[0][icoef];
        }
    }
}

static void revert_acfilter(WmallDecodeCtx *s, int tile_size)
{
    int ich, icoef;
    int pred;
    int i, j;
    int64_t *filter_coeffs = s->acfilter_coeffs;
    int scaling = s->acfilter_scaling;
    int order = s->acfilter_order;

    for (ich = 0; ich < s->num_channels; ich++) {
        int *prevvalues = s->acfilter_prevvalues[ich];
        for (i = 0; i < order; i++) {
            pred = 0;
            for (j = 0; j < order; j++) {
                if (i <= j)
                    pred += filter_coeffs[j] * prevvalues[j - i];
                else
                    pred += s->channel_residues[ich][i - j - 1] * filter_coeffs[j];
            }
            pred >>= scaling;
            s->channel_residues[ich][i] += pred;
        }
        for (i = order; i < tile_size; i++) {
            pred = 0;
            for (j = 0; j < order; j++)
                pred += s->channel_residues[ich][i - j - 1] * filter_coeffs[j];
            pred >>= scaling;
            s->channel_residues[ich][i] += pred;
        }
        for (j = 0; j < order; j++)
            prevvalues[j] = s->channel_residues[ich][tile_size - j - 1];
    }
}

/**
 *@brief Decode a single subframe (block).
 *@param s codec context
 *@return 0 on success, < 0 when decoding failed
 */
static int decode_subframe(WmallDecodeCtx *s)
{
    int offset = s->samples_per_frame;
    int subframe_len = s->samples_per_frame;
    int i, j;
    int total_samples   = s->samples_per_frame * s->num_channels;
    int rawpcm_tile;
    int padding_zeroes;

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


    s->seekable_tile = get_bits1(&s->gb);
    if(s->seekable_tile) {
        clear_codec_buffers(s);

        s->do_arith_coding    = get_bits1(&s->gb);
        if(s->do_arith_coding) {
            dprintf(s->avctx, "do_arith_coding == 1");
            abort();
        }
        s->do_ac_filter       = get_bits1(&s->gb);
        s->do_inter_ch_decorr = get_bits1(&s->gb);
        s->do_mclms           = get_bits1(&s->gb);

        if(s->do_ac_filter)
            decode_ac_filter(s);

        if(s->do_mclms)
            decode_mclms(s);

        decode_cdlms(s);
        s->movave_scaling = get_bits(&s->gb, 3);
        s->quant_stepsize = get_bits(&s->gb, 8) + 1;

            reset_codec(s);
    }

    rawpcm_tile = get_bits1(&s->gb);

    for(i = 0; i < s->num_channels; i++) {
        s->is_channel_coded[i] = 1;
    }

    if(!rawpcm_tile) {

        for(i = 0; i < s->num_channels; i++) {
            s->is_channel_coded[i] = get_bits1(&s->gb);
        }

        if(s->bV3RTM) {
            // LPC
            s->do_lpc = get_bits1(&s->gb);
            if(s->do_lpc) {
                decode_lpc(s);
            }
        } else {
            s->do_lpc = 0;
        }
    }


    if(get_bits1(&s->gb)) {
        padding_zeroes = get_bits(&s->gb, 5);
    } else {
        padding_zeroes = 0;
    }

    if(rawpcm_tile) {

        int bits = s->bits_per_sample - padding_zeroes;
        dprintf(s->avctx, "RAWPCM %d bits per sample. total %d bits, remain=%d\n", bits,
                bits * s->num_channels * subframe_len, get_bits_count(&s->gb));
        for(i = 0; i < s->num_channels; i++) {
            for(j = 0; j < subframe_len; j++) {
                s->channel_coeffs[i][j] = get_sbits(&s->gb, bits);
//                dprintf(s->avctx, "PCM[%d][%d] = 0x%04x\n", i, j, s->channel_coeffs[i][j]);
            }
        }
    } else {
        for(i = 0; i < s->num_channels; i++)
            if(s->is_channel_coded[i]) {
            decode_channel_residues(s, i, subframe_len);
            if (s->seekable_tile)
                use_high_update_speed(s, i);
            else
                use_normal_update_speed(s, i);
            revert_cdlms(s, i, 0, subframe_len);
        }
    }
    if (s->do_mclms)
        revert_mclms(s, subframe_len);
    if (s->do_inter_ch_decorr)
        revert_inter_ch_decorr(s, subframe_len);
    if(s->do_ac_filter)
        revert_acfilter(s, subframe_len);

    /* Dequantize */
    if (s->quant_stepsize != 1)
        for (i = 0; i < s->num_channels; i++)
            for (j = 0; j < subframe_len; j++)
                s->channel_residues[i][j] *= s->quant_stepsize;

    // Write to proper output buffer depending on bit-depth
    for (i = 0; i < subframe_len; i++)
        for (j = 0; j < s->num_channels; j++) {
            if (s->bits_per_sample == 16)
                *s->samples_16++ = (int16_t) s->channel_residues[j][i];
            else
                *s->samples_32++ = s->channel_residues[j][i];
        }

    /** handled one subframe */

    for (i = 0; i < s->channels_for_cur_subframe; i++) {
        int c = s->channel_indexes_for_cur_subframe[i];
        if (s->channel[c].cur_subframe >= s->channel[c].num_subframes) {
            av_log(s->avctx, AV_LOG_ERROR, "broken subframe\n");
            return AVERROR_INVALIDDATA;
        }
        ++s->channel[c].cur_subframe;
    }
    num_logged_subframes++;
    return 0;
}

/**
 *@brief Decode one WMA frame.
 *@param s codec context
 *@return 0 if the trailer bit indicates that this is the last frame,
 *        1 if there are additional frames
 */
static int decode_frame(WmallDecodeCtx *s)
{
    GetBitContext* gb = &s->gb;
    int more_frames = 0;
    int len = 0;
    int i;
    int buffer_len;

    /** check for potential output buffer overflow */
    if (s->bits_per_sample == 16)
        buffer_len = s->samples_16_end - s->samples_16;
    else
        buffer_len = s->samples_32_end - s->samples_32;
    if (s->num_channels * s->samples_per_frame > buffer_len) {
        /** return an error if no frame could be decoded at all */
        av_log(s->avctx, AV_LOG_ERROR,
               "not enough space for the output samples\n");
        s->packet_loss = 1;
        return 0;
    }

    /** get frame length */
    if (s->len_prefix)
        len = get_bits(gb, s->log2_frame_size);

    /** decode tile information */
    if (decode_tilehdr(s)) {
        s->packet_loss = 1;
        return 0;
    }

    /** read drc info */
    if (s->dynamic_range_compression) {
        s->drc_gain = get_bits(gb, 8);
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

    dprintf(s->avctx, "Frame done\n");

    if (s->skip_frame) {
        s->skip_frame = 0;
    }

    if (s->len_prefix) {
        if (len != (get_bits_count(gb) - s->frame_offset) + 2) {
            /** FIXME: not sure if this is always an error */
            av_log(s->avctx, AV_LOG_ERROR,
                   "frame[%i] would have to skip %i bits\n", s->frame_num,
                   len - (get_bits_count(gb) - s->frame_offset) - 1);
            s->packet_loss = 1;
            return 0;
        }

        /** skip the rest of the frame data */
        skip_bits_long(gb, len - (get_bits_count(gb) - s->frame_offset) - 1);
    } else {
/*
        while (get_bits_count(gb) < s->num_saved_bits && get_bits1(gb) == 0) {
            dprintf(s->avctx, "skip1\n");
        }
*/
    }

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
static int remaining_bits(WmallDecodeCtx *s, GetBitContext *gb)
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
static void save_bits(WmallDecodeCtx *s, GetBitContext* gb, int len,
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
        avpriv_copy_bits(&s->pb, gb->buffer + (get_bits_count(gb) >> 3),
                     s->num_saved_bits);
    } else {
        int align = 8 - (get_bits_count(gb) & 7);
        align = FFMIN(align, len);
        put_bits(&s->pb, align, get_bits(gb, align));
        len -= align;
        avpriv_copy_bits(&s->pb, gb->buffer + (get_bits_count(gb) >> 3), len);
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
    WmallDecodeCtx *s = avctx->priv_data;
    GetBitContext* gb  = &s->pgb;
    const uint8_t* buf = avpkt->data;
    int buf_size       = avpkt->size;
    int num_bits_prev_frame;
    int packet_sequence_number;

    if (s->bits_per_sample == 16) {
        s->samples_16     = (int16_t *) data;
        s->samples_16_end = (int16_t *) ((int8_t*)data + *data_size);
    } else {
        s->samples_32     = (void *) data;
        s->samples_32_end = (void *) ((int8_t*)data + *data_size);
    }
    *data_size = 0;

    if (s->packet_done || s->packet_loss) {
        int seekable_frame_in_packet, spliced_packet;
        s->packet_done = 0;

        /** sanity check for the buffer length */
        if (buf_size < avctx->block_align)
            return 0;

        s->next_packet_start = buf_size - avctx->block_align;
        buf_size = avctx->block_align;
        s->buf_bit_size = buf_size << 3;

        /** parse packet header */
        init_get_bits(gb, buf, s->buf_bit_size);
        packet_sequence_number = get_bits(gb, 4);
        seekable_frame_in_packet = get_bits1(gb);
        spliced_packet = get_bits1(gb);

        /** get number of bits that need to be added to the previous frame */
        num_bits_prev_frame = get_bits(gb, s->log2_frame_size);

        /** check for packet loss */
        if (!s->packet_loss &&
            ((s->packet_sequence_number + 1) & 0xF) != packet_sequence_number) {
            s->packet_loss = 1;
            av_log(avctx, AV_LOG_ERROR, "Packet loss detected! seq %x vs %x\n",
                   s->packet_sequence_number, packet_sequence_number);
        }
        s->packet_sequence_number = packet_sequence_number;

        if (num_bits_prev_frame > 0) {
            int remaining_packet_bits = s->buf_bit_size - get_bits_count(gb);
            if (num_bits_prev_frame >= remaining_packet_bits) {
                num_bits_prev_frame = remaining_packet_bits;
                s->packet_done = 1;
            }

            /** append the previous frame data to the remaining data from the
                previous packet to create a full frame */
            save_bits(s, gb, num_bits_prev_frame, 1);

            /** decode the cross packet frame if it is valid */
            if (!s->packet_loss)
                decode_frame(s);
        } else if (s->num_saved_bits - s->frame_offset) {
            dprintf(avctx, "ignoring %x previously saved bits\n",
                    s->num_saved_bits - s->frame_offset);
        }

        if (s->packet_loss) {
            /** reset number of saved bits so that the decoder
                does not start to decode incomplete frames in the
                s->len_prefix == 0 case */
            s->num_saved_bits = 0;
            s->packet_loss = 0;
        }

    } else {
        int frame_size;

        s->buf_bit_size = (avpkt->size - s->next_packet_start) << 3;
        init_get_bits(gb, avpkt->data, s->buf_bit_size);
        skip_bits(gb, s->packet_offset);

        if (s->len_prefix && remaining_bits(s, gb) > s->log2_frame_size &&
            (frame_size = show_bits(gb, s->log2_frame_size)) &&
            frame_size <= remaining_bits(s, gb)) {
            save_bits(s, gb, frame_size, 0);
            s->packet_done = !decode_frame(s);
        } else if (!s->len_prefix
                   && s->num_saved_bits > get_bits_count(&s->gb)) {
            /** when the frames do not have a length prefix, we don't know
                the compressed length of the individual frames
                however, we know what part of a new packet belongs to the
                previous frame
                therefore we save the incoming packet first, then we append
                the "previous frame" data from the next packet so that
                we get a buffer that only contains full frames */
            s->packet_done = !decode_frame(s);
        } else {
            s->packet_done = 1;
        }
    }

    if (s->packet_done && !s->packet_loss &&
        remaining_bits(s, gb) > 0) {
        /** save the rest of the data so that it can be decoded
            with the next packet */
        save_bits(s, gb, remaining_bits(s, gb), 0);
    }

    if (s->bits_per_sample == 16)
        *data_size = (int8_t *)s->samples_16 - (int8_t *)data;
    else
        *data_size = (int8_t *)s->samples_32 - (int8_t *)data;
    s->packet_offset = get_bits_count(gb) & 7;

    return (s->packet_loss) ? AVERROR_INVALIDDATA : get_bits_count(gb) >> 3;
}

/**
 *@brief Clear decoder buffers (for seeking).
 *@param avctx codec context
 */
static void flush(AVCodecContext *avctx)
{
    WmallDecodeCtx *s = avctx->priv_data;
    int i;
    /** reset output buffer as a part of it is used during the windowing of a
        new frame */
    for (i = 0; i < s->num_channels; i++)
        memset(s->channel[i].out, 0, s->samples_per_frame *
               sizeof(*s->channel[i].out));
    s->packet_loss = 1;
}


/**
 *@brief wmall decoder
 */
AVCodec ff_wmalossless_decoder = {
    "wmalossless",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_WMALOSSLESS,
    sizeof(WmallDecodeCtx),
    decode_init,
    NULL,
    decode_end,
    decode_packet,
    .capabilities = CODEC_CAP_SUBFRAMES | CODEC_CAP_EXPERIMENTAL,
    .flush= flush,
    .long_name = NULL_IF_CONFIG_SMALL("Windows Media Audio 9 Lossless"),
};
