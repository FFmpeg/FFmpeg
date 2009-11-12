/*
 * MPEG-4 ALS decoder
 * Copyright (c) 2009 Thilo Borgmann <thilo.borgmann _at_ googlemail.com>
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
 * @file libavcodec/alsdec.c
 * MPEG-4 ALS decoder
 * @author Thilo Borgmann <thilo.borgmann _at_ googlemail.com>
 */


//#define DEBUG


#include "avcodec.h"
#include "get_bits.h"
#include "unary.h"
#include "mpeg4audio.h"
#include "bytestream.h"

#include "als_data.h"

enum RA_Flag {
    RA_FLAG_NONE,
    RA_FLAG_FRAMES,
    RA_FLAG_HEADER
};


typedef struct {
    uint32_t samples;         ///< number of samples, 0xFFFFFFFF if unknown
    int resolution;           ///< 000 = 8-bit; 001 = 16-bit; 010 = 24-bit; 011 = 32-bit
    int floating;             ///< 1 = IEEE 32-bit floating-point, 0 = integer
    int frame_length;         ///< frame length for each frame (last frame may differ)
    int ra_distance;          ///< distance between RA frames (in frames, 0...255)
    enum RA_Flag ra_flag;     ///< indicates where the size of ra units is stored
    int adapt_order;          ///< adaptive order: 1 = on, 0 = off
    int coef_table;           ///< table index of Rice code parameters
    int long_term_prediction; ///< long term prediction (LTP): 1 = on, 0 = off
    int max_order;            ///< maximum prediction order (0..1023)
    int block_switching;      ///< number of block switching levels
    int bgmc;                 ///< "Block Gilbert-Moore Code": 1 = on, 0 = off (Rice coding only)
    int sb_part;              ///< sub-block partition
    int joint_stereo;         ///< joint stereo: 1 = on, 0 = off
    int mc_coding;            ///< extended inter-channel coding (multi channel coding): 1 = on, 0 = off
    int chan_config;          ///< indicates that a chan_config_info field is present
    int chan_sort;            ///< channel rearrangement: 1 = on, 0 = off
    int rlslms;               ///< use "Recursive Least Square-Least Mean Square" predictor: 1 = on, 0 = off
    int chan_config_info;     ///< mapping of channels to loudspeaker locations. Unused until setting channel configuration is implemented.
    int *chan_pos;            ///< original channel positions
    uint32_t header_size;     ///< header size of original audio file in bytes, provided for debugging
    uint32_t trailer_size;    ///< trailer size of original audio file in bytes, provided for debugging
} ALSSpecificConfig;


typedef struct {
    AVCodecContext *avctx;
    ALSSpecificConfig sconf;
    GetBitContext gb;
    unsigned int cur_frame_length;  ///< length of the current frame to decode
    unsigned int frame_id;          ///< the frame ID / number of the current frame
    unsigned int js_switch;         ///< if true, joint-stereo decoding is enforced
    unsigned int num_blocks;        ///< number of blocks used in the current frame
    int32_t *quant_cof;             ///< quantized parcor coefficients
    int32_t *lpc_cof;               ///< coefficients of the direct form prediction filter
    int32_t *prev_raw_samples;      ///< contains unshifted raw samples from the previous block
    int32_t **raw_samples;          ///< decoded raw samples for each channel
    int32_t *raw_buffer;            ///< contains all decoded raw samples including carryover samples
} ALSDecContext;


static av_cold void dprint_specific_config(ALSDecContext *ctx)
{
#ifdef DEBUG
    AVCodecContext *avctx    = ctx->avctx;
    ALSSpecificConfig *sconf = &ctx->sconf;

    dprintf(avctx, "resolution = %i\n",           sconf->resolution);
    dprintf(avctx, "floating = %i\n",             sconf->floating);
    dprintf(avctx, "frame_length = %i\n",         sconf->frame_length);
    dprintf(avctx, "ra_distance = %i\n",          sconf->ra_distance);
    dprintf(avctx, "ra_flag = %i\n",              sconf->ra_flag);
    dprintf(avctx, "adapt_order = %i\n",          sconf->adapt_order);
    dprintf(avctx, "coef_table = %i\n",           sconf->coef_table);
    dprintf(avctx, "long_term_prediction = %i\n", sconf->long_term_prediction);
    dprintf(avctx, "max_order = %i\n",            sconf->max_order);
    dprintf(avctx, "block_switching = %i\n",      sconf->block_switching);
    dprintf(avctx, "bgmc = %i\n",                 sconf->bgmc);
    dprintf(avctx, "sb_part = %i\n",              sconf->sb_part);
    dprintf(avctx, "joint_stereo = %i\n",         sconf->joint_stereo);
    dprintf(avctx, "mc_coding = %i\n",            sconf->mc_coding);
    dprintf(avctx, "chan_config = %i\n",          sconf->chan_config);
    dprintf(avctx, "chan_sort = %i\n",            sconf->chan_sort);
    dprintf(avctx, "RLSLMS = %i\n",               sconf->rlslms);
    dprintf(avctx, "chan_config_info = %i\n",     sconf->chan_config_info);
    dprintf(avctx, "header_size = %i\n",          sconf->header_size);
    dprintf(avctx, "trailer_size = %i\n",         sconf->trailer_size);
#endif
}


/** Reads an ALSSpecificConfig from a buffer into the output struct.
 */
static av_cold int read_specific_config(ALSDecContext *ctx)
{
    GetBitContext gb;
    uint64_t ht_size;
    int i, config_offset, crc_enabled;
    MPEG4AudioConfig m4ac;
    ALSSpecificConfig *sconf = &ctx->sconf;
    AVCodecContext *avctx    = ctx->avctx;
    uint32_t als_id;

    init_get_bits(&gb, avctx->extradata, avctx->extradata_size * 8);

    config_offset = ff_mpeg4audio_get_config(&m4ac, avctx->extradata,
                                             avctx->extradata_size);

    if (config_offset < 0)
        return -1;

    skip_bits_long(&gb, config_offset);

    if (get_bits_left(&gb) < (30 << 3))
        return -1;

    // read the fixed items
    als_id                      = get_bits_long(&gb, 32);
    avctx->sample_rate          = m4ac.sample_rate;
    skip_bits_long(&gb, 32); // sample rate already known
    sconf->samples              = get_bits_long(&gb, 32);
    avctx->channels             = m4ac.channels;
    skip_bits(&gb, 16);      // number of channels already knwon
    skip_bits(&gb, 3);       // skip file_type
    sconf->resolution           = get_bits(&gb, 3);
    sconf->floating             = get_bits1(&gb);
    skip_bits1(&gb);         // skip msb_first
    sconf->frame_length         = get_bits(&gb, 16) + 1;
    sconf->ra_distance          = get_bits(&gb, 8);
    sconf->ra_flag              = get_bits(&gb, 2);
    sconf->adapt_order          = get_bits1(&gb);
    sconf->coef_table           = get_bits(&gb, 2);
    sconf->long_term_prediction = get_bits1(&gb);
    sconf->max_order            = get_bits(&gb, 10);
    sconf->block_switching      = get_bits(&gb, 2);
    sconf->bgmc                 = get_bits1(&gb);
    sconf->sb_part              = get_bits1(&gb);
    sconf->joint_stereo         = get_bits1(&gb);
    sconf->mc_coding            = get_bits1(&gb);
    sconf->chan_config          = get_bits1(&gb);
    sconf->chan_sort            = get_bits1(&gb);
    crc_enabled                 = get_bits1(&gb);
    sconf->rlslms               = get_bits1(&gb);
    skip_bits(&gb, 5);       // skip 5 reserved bits
    skip_bits1(&gb);         // skip aux_data_enabled


    // check for ALSSpecificConfig struct
    if (als_id != MKBETAG('A','L','S','\0'))
        return -1;

    ctx->cur_frame_length = sconf->frame_length;

    // allocate quantized parcor coefficient buffer
    if (!(ctx->quant_cof = av_malloc(sizeof(*ctx->quant_cof) * sconf->max_order)) ||
        !(ctx->lpc_cof   = av_malloc(sizeof(*ctx->lpc_cof)   * sconf->max_order))) {
        av_log(avctx, AV_LOG_ERROR, "Allocating buffer memory failed.\n");
        return AVERROR(ENOMEM);
    }

    // read channel config
    if (sconf->chan_config)
        sconf->chan_config_info = get_bits(&gb, 16);
    // TODO: use this to set avctx->channel_layout


    // read channel sorting
    if (sconf->chan_sort && avctx->channels > 1) {
        int chan_pos_bits = av_ceil_log2(avctx->channels);
        int bits_needed  = avctx->channels * chan_pos_bits + 7;
        if (get_bits_left(&gb) < bits_needed)
            return -1;

        if (!(sconf->chan_pos = av_malloc(avctx->channels * sizeof(*sconf->chan_pos))))
            return AVERROR(ENOMEM);

        for (i = 0; i < avctx->channels; i++)
            sconf->chan_pos[i] = get_bits(&gb, chan_pos_bits);

        align_get_bits(&gb);
        // TODO: use this to actually do channel sorting
    } else {
        sconf->chan_sort = 0;
    }


    // read fixed header and trailer sizes,
    // if size = 0xFFFFFFFF then there is no data field!
    if (get_bits_left(&gb) < 64)
        return -1;

    sconf->header_size  = get_bits_long(&gb, 32);
    sconf->trailer_size = get_bits_long(&gb, 32);
    if (sconf->header_size  == 0xFFFFFFFF)
        sconf->header_size  = 0;
    if (sconf->trailer_size == 0xFFFFFFFF)
        sconf->trailer_size = 0;

    ht_size = ((int64_t)(sconf->header_size) + (int64_t)(sconf->trailer_size)) << 3;


    // skip the header and trailer data
    if (get_bits_left(&gb) < ht_size)
        return -1;

    if (ht_size > INT32_MAX)
        return -1;

    skip_bits_long(&gb, ht_size);


    // skip the crc data
    if (crc_enabled) {
        if (get_bits_left(&gb) < 32)
            return -1;

        skip_bits_long(&gb, 32);
    }


    // no need to read the rest of ALSSpecificConfig (ra_unit_size & aux data)

    dprint_specific_config(ctx);

    return 0;
}


/** Checks the ALSSpecificConfig for unsupported features.
 */
static int check_specific_config(ALSDecContext *ctx)
{
    ALSSpecificConfig *sconf = &ctx->sconf;
    int error = 0;

    // report unsupported feature and set error value
    #define MISSING_ERR(cond, str, errval)              \
    {                                                   \
        if (cond) {                                     \
            av_log_missing_feature(ctx->avctx, str, 0); \
            error = errval;                             \
        }                                               \
    }

    MISSING_ERR(sconf->floating,             "Floating point decoding",     -1);
    MISSING_ERR(sconf->long_term_prediction, "Long-term prediction",        -1);
    MISSING_ERR(sconf->bgmc,                 "BGMC entropy decoding",       -1);
    MISSING_ERR(sconf->mc_coding,            "Multi-channel correlation",   -1);
    MISSING_ERR(sconf->rlslms,               "Adaptive RLS-LMS prediction", -1);
    MISSING_ERR(sconf->chan_sort,            "Channel sorting",              0);

    return error;
}


/** Parses the bs_info field to extract the block partitioning used in
 *  block switching mode, refer to ISO/IEC 14496-3, section 11.6.2.
 */
static void parse_bs_info(const uint32_t bs_info, unsigned int n,
                          unsigned int div, unsigned int **div_blocks,
                          unsigned int *num_blocks)
{
    if (n < 31 && ((bs_info << n) & 0x40000000)) {
        // if the level is valid and the investigated bit n is set
        // then recursively check both children at bits (2n+1) and (2n+2)
        n   *= 2;
        div += 1;
        parse_bs_info(bs_info, n + 1, div, div_blocks, num_blocks);
        parse_bs_info(bs_info, n + 2, div, div_blocks, num_blocks);
    } else {
        // else the bit is not set or the last level has been reached
        // (bit implicitly not set)
        **div_blocks = div;
        (*div_blocks)++;
        (*num_blocks)++;
    }
}


/** Reads and decodes a Rice codeword.
 */
static int32_t decode_rice(GetBitContext *gb, unsigned int k)
{
    int max = gb->size_in_bits - get_bits_count(gb) - k;
    int q   = get_unary(gb, 0, max);
    int r   = k ? get_bits1(gb) : !(q & 1);

    if (k > 1) {
        q <<= (k - 1);
        q  += get_bits_long(gb, k - 1);
    } else if (!k) {
        q >>= 1;
    }
    return r ? q : ~q;
}


/** Converts PARCOR coefficient k to direct filter coefficient.
 */
static void parcor_to_lpc(unsigned int k, const int32_t *par, int32_t *cof)
{
    int i, j;

    for (i = 0, j = k - 1; i < j; i++, j--) {
        int tmp1 = ((MUL64(par[k], cof[j]) + (1 << 19)) >> 20);
        cof[j]  += ((MUL64(par[k], cof[i]) + (1 << 19)) >> 20);
        cof[i]  += tmp1;
    }
    if (i == j)
        cof[i] += ((MUL64(par[k], cof[j]) + (1 << 19)) >> 20);

    cof[k] = par[k];
}


/** Reads block switching field if necessary and sets actual block sizes.
 *  Also assures that the block sizes of the last frame correspond to the
 *  actual number of samples.
 */
static void get_block_sizes(ALSDecContext *ctx, unsigned int *div_blocks,
                            uint32_t *bs_info)
{
    ALSSpecificConfig *sconf     = &ctx->sconf;
    GetBitContext *gb            = &ctx->gb;
    unsigned int *ptr_div_blocks = div_blocks;
    unsigned int b;

    if (sconf->block_switching) {
        unsigned int bs_info_len = 1 << (sconf->block_switching + 2);
        *bs_info = get_bits_long(gb, bs_info_len);
        *bs_info <<= (32 - bs_info_len);
    }

    ctx->num_blocks = 0;
    parse_bs_info(*bs_info, 0, 0, &ptr_div_blocks, &ctx->num_blocks);

    // The last frame may have an overdetermined block structure given in
    // the bitstream. In that case the defined block structure would need
    // more samples than available to be consistent.
    // The block structure is actually used but the block sizes are adapted
    // to fit the actual number of available samples.
    // Example: 5 samples, 2nd level block sizes: 2 2 2 2.
    // This results in the actual block sizes:    2 2 1 0.
    // This is not specified in 14496-3 but actually done by the reference
    // codec RM22 revision 2.
    // This appears to happen in case of an odd number of samples in the last
    // frame which is actually not allowed by the block length switching part
    // of 14496-3.
    // The ALS conformance files feature an odd number of samples in the last
    // frame.

    for (b = 0; b < ctx->num_blocks; b++)
        div_blocks[b] = ctx->sconf.frame_length >> div_blocks[b];

    if (ctx->cur_frame_length != ctx->sconf.frame_length) {
        unsigned int remaining = ctx->cur_frame_length;

        for (b = 0; b < ctx->num_blocks; b++) {
            if (remaining < div_blocks[b]) {
                div_blocks[b] = remaining;
                ctx->num_blocks = b + 1;
                break;
            }

            remaining -= div_blocks[b];
        }
    }
}


/** Reads the block data for a constant block
 */
static void read_const_block(ALSDecContext *ctx, int32_t *raw_samples,
                             unsigned int block_length, unsigned int *js_blocks)
{
    ALSSpecificConfig *sconf = &ctx->sconf;
    AVCodecContext *avctx    = ctx->avctx;
    GetBitContext *gb        = &ctx->gb;
    int32_t const_val        = 0;
    unsigned int const_block, k;

    const_block  = get_bits1(gb);    // 1 = constant value, 0 = zero block (silence)
    *js_blocks   = get_bits1(gb);

    // skip 5 reserved bits
    skip_bits(gb, 5);

    if (const_block) {
        unsigned int const_val_bits = sconf->floating ? 24 : avctx->bits_per_raw_sample;
        const_val = get_sbits_long(gb, const_val_bits);
    }

    // write raw samples into buffer
    for (k = 0; k < block_length; k++)
        raw_samples[k] = const_val;
}


/** Reads the block data for a non-constant block
 */
static int read_var_block(ALSDecContext *ctx, unsigned int ra_block,
                          int32_t *raw_samples, unsigned int block_length,
                          unsigned int *js_blocks, int32_t *raw_other,
                          unsigned int *shift_lsbs)
{
    ALSSpecificConfig *sconf = &ctx->sconf;
    AVCodecContext *avctx    = ctx->avctx;
    GetBitContext *gb        = &ctx->gb;
    unsigned int k;
    unsigned int s[8];
    unsigned int sub_blocks, log2_sub_blocks, sb_length;
    unsigned int opt_order  = 1;
    int32_t      *quant_cof = ctx->quant_cof;
    int32_t      *lpc_cof   = ctx->lpc_cof;
    unsigned int start      = 0;
    int          smp        = 0;
    int          sb, store_prev_samples;
    int64_t      y;

    *js_blocks  = get_bits1(gb);

    // determine the number of subblocks for entropy decoding
    if (!sconf->bgmc && !sconf->sb_part) {
        log2_sub_blocks = 0;
    } else {
        if (sconf->bgmc && sconf->sb_part)
            log2_sub_blocks = get_bits(gb, 2);
        else
            log2_sub_blocks = 2 * get_bits1(gb);
    }

    sub_blocks = 1 << log2_sub_blocks;

    // do not continue in case of a damaged stream since
    // block_length must be evenly divisible by sub_blocks
    if (block_length & (sub_blocks - 1)) {
        av_log(avctx, AV_LOG_WARNING,
               "Block length is not evenly divisible by the number of subblocks.\n");
        return -1;
    }

    sb_length = block_length >> log2_sub_blocks;


    if (sconf->bgmc) {
        // TODO: BGMC mode
    } else {
        s[0] = get_bits(gb, 4 + (sconf->resolution > 1));
        for (k = 1; k < sub_blocks; k++)
            s[k] = s[k - 1] + decode_rice(gb, 0);
    }

    if (get_bits1(gb))
        *shift_lsbs = get_bits(gb, 4) + 1;

    store_prev_samples = (*js_blocks && raw_other) || *shift_lsbs;


    if (!sconf->rlslms) {
        if (sconf->adapt_order) {
            int opt_order_length = av_ceil_log2(av_clip((block_length >> 3) - 1,
                                                2, sconf->max_order + 1));
            opt_order            = get_bits(gb, opt_order_length);
        } else {
            opt_order = sconf->max_order;
        }

        if (opt_order) {
            int add_base;

            if (sconf->coef_table == 3) {
                add_base = 0x7F;

                // read coefficient 0
                quant_cof[0] = 32 * parcor_scaled_values[get_bits(gb, 7)];

                // read coefficient 1
                if (opt_order > 1)
                    quant_cof[1] = -32 * parcor_scaled_values[get_bits(gb, 7)];

                // read coefficients 2 to opt_order
                for (k = 2; k < opt_order; k++)
                    quant_cof[k] = get_bits(gb, 7);
            } else {
                int k_max;
                add_base = 1;

                // read coefficient 0 to 19
                k_max = FFMIN(opt_order, 20);
                for (k = 0; k < k_max; k++) {
                    int rice_param = parcor_rice_table[sconf->coef_table][k][1];
                    int offset     = parcor_rice_table[sconf->coef_table][k][0];
                    quant_cof[k] = decode_rice(gb, rice_param) + offset;
                }

                // read coefficients 20 to 126
                k_max = FFMIN(opt_order, 127);
                for (; k < k_max; k++)
                    quant_cof[k] = decode_rice(gb, 2) + (k & 1);

                // read coefficients 127 to opt_order
                for (; k < opt_order; k++)
                    quant_cof[k] = decode_rice(gb, 1);

                quant_cof[0] = 32 * parcor_scaled_values[quant_cof[0] + 64];

                if (opt_order > 1)
                    quant_cof[1] = -32 * parcor_scaled_values[quant_cof[1] + 64];
            }

            for (k = 2; k < opt_order; k++)
                quant_cof[k] = (quant_cof[k] << 14) + (add_base << 13);
        }
    }

    // TODO: LTP mode

    // read first value and residuals in case of a random access block
    if (ra_block) {
        if (opt_order)
            raw_samples[0] = decode_rice(gb, avctx->bits_per_raw_sample - 4);
        if (opt_order > 1)
            raw_samples[1] = decode_rice(gb, s[0] + 3);
        if (opt_order > 2)
            raw_samples[2] = decode_rice(gb, s[0] + 1);

        start = FFMIN(opt_order, 3);
    }

    // read all residuals
    if (sconf->bgmc) {
        // TODO: BGMC mode
    } else {
        int32_t *current_res = raw_samples + start;

        for (sb = 0; sb < sub_blocks; sb++, start = 0)
            for (; start < sb_length; start++)
                *current_res++ = decode_rice(gb, s[sb]);
     }

    // reconstruct all samples from residuals
    if (ra_block) {
        for (smp = 0; smp < opt_order; smp++) {
            y = 1 << 19;

            for (sb = 0; sb < smp; sb++)
                y += MUL64(lpc_cof[sb],raw_samples[smp - (sb + 1)]);

            raw_samples[smp] -= y >> 20;
            parcor_to_lpc(smp, quant_cof, lpc_cof);
        }
    } else {
        for (k = 0; k < opt_order; k++)
            parcor_to_lpc(k, quant_cof, lpc_cof);

        // store previous samples in case that they have to be altered
        if (store_prev_samples)
            memcpy(ctx->prev_raw_samples, raw_samples - sconf->max_order,
                   sizeof(*ctx->prev_raw_samples) * sconf->max_order);

        // reconstruct difference signal for prediction (joint-stereo)
        if (*js_blocks && raw_other) {
            int32_t *left, *right;

            if (raw_other > raw_samples) {          // D = R - L
                left  = raw_samples;
                right = raw_other;
            } else {                                // D = R - L
                left  = raw_other;
                right = raw_samples;
            }

            for (sb = -1; sb >= -sconf->max_order; sb--)
                raw_samples[sb] = right[sb] - left[sb];
        }

        // reconstruct shifted signal
        if (*shift_lsbs)
            for (sb = -1; sb >= -sconf->max_order; sb--)
                raw_samples[sb] >>= *shift_lsbs;
    }

    // reconstruct raw samples
    for (; smp < block_length; smp++) {
        y = 1 << 19;

        for (sb = 0; sb < opt_order; sb++)
            y += MUL64(lpc_cof[sb],raw_samples[smp - (sb + 1)]);

        raw_samples[smp] -= y >> 20;
    }

    // restore previous samples in case that they have been altered
    if (store_prev_samples)
        memcpy(raw_samples - sconf->max_order, ctx->prev_raw_samples,
               sizeof(*raw_samples) * sconf->max_order);

    return 0;
}


/** Reads the block data.
 */
static int read_block_data(ALSDecContext *ctx, unsigned int ra_block,
                           int32_t *raw_samples, unsigned int block_length,
                           unsigned int *js_blocks, int32_t *raw_other)
{
    ALSSpecificConfig *sconf = &ctx->sconf;
    GetBitContext *gb        = &ctx->gb;
    unsigned int shift_lsbs  = 0;
    unsigned int k;

    // read block type flag and read the samples accordingly
    if (get_bits1(gb)) {
        if (read_var_block(ctx, ra_block, raw_samples, block_length, js_blocks,
                           raw_other, &shift_lsbs))
            return -1;
    } else {
        read_const_block(ctx, raw_samples, block_length, js_blocks);
    }

    // TODO: read RLSLMS extension data

    if (!sconf->mc_coding || ctx->js_switch)
        align_get_bits(gb);

    if (shift_lsbs)
        for (k = 0; k < block_length; k++)
            raw_samples[k] <<= shift_lsbs;

    return 0;
}


/** Computes the number of samples left to decode for the current frame and
 *  sets these samples to zero.
 */
static void zero_remaining(unsigned int b, unsigned int b_max,
                           const unsigned int *div_blocks, int32_t *buf)
{
    unsigned int count = 0;

    while (b < b_max)
        count += div_blocks[b];

    if (count)
    memset(buf, 0, sizeof(*buf) * count);
}


/** Decodes blocks independently.
 */
static int decode_blocks_ind(ALSDecContext *ctx, unsigned int ra_frame,
                             unsigned int c, const unsigned int *div_blocks,
                             unsigned int *js_blocks)
{
    int32_t *raw_sample;
    unsigned int b;
    raw_sample = ctx->raw_samples[c];

    for (b = 0; b < ctx->num_blocks; b++) {
        if (read_block_data(ctx, ra_frame, raw_sample,
                            div_blocks[b], &js_blocks[0], NULL)) {
            // damaged block, write zero for the rest of the frame
            zero_remaining(b, ctx->num_blocks, div_blocks, raw_sample);
            return -1;
        }
        raw_sample += div_blocks[b];
        ra_frame    = 0;
    }

    return 0;
}


/** Decodes blocks dependently.
 */
static int decode_blocks(ALSDecContext *ctx, unsigned int ra_frame,
                         unsigned int c, const unsigned int *div_blocks,
                         unsigned int *js_blocks)
{
    ALSSpecificConfig *sconf = &ctx->sconf;
    unsigned int offset = 0;
    int32_t *raw_samples_R;
    int32_t *raw_samples_L;
    unsigned int b;

    // decode all blocks
    for (b = 0; b < ctx->num_blocks; b++) {
        unsigned int s;
        raw_samples_L = ctx->raw_samples[c    ] + offset;
        raw_samples_R = ctx->raw_samples[c + 1] + offset;
        if (read_block_data(ctx, ra_frame, raw_samples_L, div_blocks[b],
                            &js_blocks[0], raw_samples_R) ||
            read_block_data(ctx, ra_frame, raw_samples_R, div_blocks[b],
                            &js_blocks[1], raw_samples_L)) {
            // damaged block, write zero for the rest of the frame
            zero_remaining(b, ctx->num_blocks, div_blocks, raw_samples_L);
            zero_remaining(b, ctx->num_blocks, div_blocks, raw_samples_R);
            return -1;
        }

        // reconstruct joint-stereo blocks
        if (js_blocks[0]) {
            if (js_blocks[1])
                av_log(ctx->avctx, AV_LOG_WARNING, "Invalid channel pair!\n");

            for (s = 0; s < div_blocks[b]; s++)
                raw_samples_L[s] = raw_samples_R[s] - raw_samples_L[s];
        } else if (js_blocks[1]) {
            for (s = 0; s < div_blocks[b]; s++)
                raw_samples_R[s] = raw_samples_R[s] + raw_samples_L[s];
        }

        offset  += div_blocks[b];
        ra_frame = 0;
    }

    // store carryover raw samples,
    // the others channel raw samples are stored by the calling function.
    memmove(ctx->raw_samples[c] - sconf->max_order,
            ctx->raw_samples[c] - sconf->max_order + sconf->frame_length,
            sizeof(*ctx->raw_samples[c]) * sconf->max_order);

    return 0;
}


/** Reads the frame data.
 */
static int read_frame_data(ALSDecContext *ctx, unsigned int ra_frame)
{
    ALSSpecificConfig *sconf = &ctx->sconf;
    AVCodecContext *avctx    = ctx->avctx;
    GetBitContext *gb = &ctx->gb;
    unsigned int div_blocks[32];                ///< block sizes.
    unsigned int c;
    unsigned int js_blocks[2];

    uint32_t bs_info = 0;

    // skip the size of the ra unit if present in the frame
    if (sconf->ra_flag == RA_FLAG_FRAMES && ra_frame)
        skip_bits_long(gb, 32);

    if (sconf->mc_coding && sconf->joint_stereo) {
        ctx->js_switch = get_bits1(gb);
        align_get_bits(gb);
    }

    if (!sconf->mc_coding || ctx->js_switch) {
        int independent_bs = !sconf->joint_stereo;

        for (c = 0; c < avctx->channels; c++) {
            js_blocks[0] = 0;
            js_blocks[1] = 0;

            get_block_sizes(ctx, div_blocks, &bs_info);

            // if joint_stereo and block_switching is set, independent decoding
            // is signaled via the first bit of bs_info
            if (sconf->joint_stereo && sconf->block_switching)
                if (bs_info >> 31)
                    independent_bs = 2;

            // if this is the last channel, it has to be decoded independently
            if (c == avctx->channels - 1)
                independent_bs = 1;

            if (independent_bs) {
                if (decode_blocks_ind(ctx, ra_frame, c, div_blocks, js_blocks))
                    return -1;

                independent_bs--;
            } else {
                if (decode_blocks(ctx, ra_frame, c, div_blocks, js_blocks))
                    return -1;

                c++;
            }

            // store carryover raw samples
            memmove(ctx->raw_samples[c] - sconf->max_order,
                    ctx->raw_samples[c] - sconf->max_order + sconf->frame_length,
                    sizeof(*ctx->raw_samples[c]) * sconf->max_order);
        }
    } else { // multi-channel coding
        get_block_sizes(ctx, div_blocks, &bs_info);

        // TODO: multi channel coding might use a temporary buffer instead as
        //       the actual channel is not known when read_block-data is called
        if (decode_blocks_ind(ctx, ra_frame, 0, div_blocks, js_blocks))
            return -1;
        // TODO: read_channel_data
    }

    // TODO: read_diff_float_data

    return 0;
}


/** Decodes an ALS frame.
 */
static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    ALSDecContext *ctx       = avctx->priv_data;
    ALSSpecificConfig *sconf = &ctx->sconf;
    const uint8_t *buffer    = avpkt->data;
    int buffer_size          = avpkt->size;
    int invalid_frame, size;
    unsigned int c, sample, ra_frame, bytes_read, shift;

    init_get_bits(&ctx->gb, buffer, buffer_size * 8);

    // In the case that the distance between random access frames is set to zero
    // (sconf->ra_distance == 0) no frame is treated as a random access frame.
    // For the first frame, if prediction is used, all samples used from the
    // previous frame are assumed to be zero.
    ra_frame = sconf->ra_distance && !(ctx->frame_id % sconf->ra_distance);

    // the last frame to decode might have a different length
    if (sconf->samples != 0xFFFFFFFF)
        ctx->cur_frame_length = FFMIN(sconf->samples - ctx->frame_id * (uint64_t) sconf->frame_length,
                                      sconf->frame_length);
    else
        ctx->cur_frame_length = sconf->frame_length;

    // decode the frame data
    if ((invalid_frame = read_frame_data(ctx, ra_frame) < 0))
        av_log(ctx->avctx, AV_LOG_WARNING,
               "Reading frame data failed. Skipping RA unit.\n");

    ctx->frame_id++;

    // check for size of decoded data
    size = ctx->cur_frame_length * avctx->channels *
           (av_get_bits_per_sample_format(avctx->sample_fmt) >> 3);

    if (size > *data_size) {
        av_log(avctx, AV_LOG_ERROR, "Decoded data exceeds buffer size.\n");
        return -1;
    }

    *data_size = size;

    // transform decoded frame into output format
    #define INTERLEAVE_OUTPUT(bps)                                 \
    {                                                              \
        int##bps##_t *dest = (int##bps##_t*) data;                 \
        shift = bps - ctx->avctx->bits_per_raw_sample;             \
        for (sample = 0; sample < ctx->cur_frame_length; sample++) \
            for (c = 0; c < avctx->channels; c++)                  \
                *dest++ = ctx->raw_samples[c][sample] << shift;    \
    }

    if (ctx->avctx->bits_per_raw_sample <= 16) {
        INTERLEAVE_OUTPUT(16)
    } else {
        INTERLEAVE_OUTPUT(32)
    }

    bytes_read = invalid_frame ? buffer_size :
                                 (get_bits_count(&ctx->gb) + 7) >> 3;

    return bytes_read;
}


/** Uninitializes the ALS decoder.
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    ALSDecContext *ctx = avctx->priv_data;

    av_freep(&ctx->sconf.chan_pos);

    av_freep(&ctx->quant_cof);
    av_freep(&ctx->lpc_cof);
    av_freep(&ctx->prev_raw_samples);
    av_freep(&ctx->raw_samples);
    av_freep(&ctx->raw_buffer);

    return 0;
}


/** Initializes the ALS decoder.
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    unsigned int c;
    unsigned int channel_size;
    ALSDecContext *ctx = avctx->priv_data;
    ALSSpecificConfig *sconf = &ctx->sconf;
    ctx->avctx = avctx;

    if (!avctx->extradata) {
        av_log(avctx, AV_LOG_ERROR, "Missing required ALS extradata.\n");
        return -1;
    }

    if (read_specific_config(ctx)) {
        av_log(avctx, AV_LOG_ERROR, "Reading ALSSpecificConfig failed.\n");
        decode_end(avctx);
        return -1;
    }

    if (check_specific_config(ctx)) {
        decode_end(avctx);
        return -1;
    }

    if (sconf->floating) {
        avctx->sample_fmt          = SAMPLE_FMT_FLT;
        avctx->bits_per_raw_sample = 32;
    } else {
        avctx->sample_fmt          = sconf->resolution > 1
                                     ? SAMPLE_FMT_S32 : SAMPLE_FMT_S16;
        avctx->bits_per_raw_sample = (sconf->resolution + 1) * 8;
    }

    avctx->frame_size = sconf->frame_length;
    channel_size      = sconf->frame_length + sconf->max_order;

    ctx->prev_raw_samples = av_malloc (sizeof(*ctx->prev_raw_samples) * sconf->max_order);
    ctx->raw_buffer       = av_mallocz(sizeof(*ctx->     raw_buffer)  * avctx->channels * channel_size);
    ctx->raw_samples      = av_malloc (sizeof(*ctx->     raw_samples) * avctx->channels);

    // allocate previous raw sample buffer
    if (!ctx->prev_raw_samples || !ctx->raw_buffer|| !ctx->raw_samples) {
        av_log(avctx, AV_LOG_ERROR, "Allocating buffer memory failed.\n");
        decode_end(avctx);
        return AVERROR(ENOMEM);
    }

    // assign raw samples buffers
    ctx->raw_samples[0] = ctx->raw_buffer + sconf->max_order;
    for (c = 1; c < avctx->channels; c++)
        ctx->raw_samples[c] = ctx->raw_samples[c - 1] + channel_size;

    return 0;
}


/** Flushes (resets) the frame ID after seeking.
 */
static av_cold void flush(AVCodecContext *avctx)
{
    ALSDecContext *ctx = avctx->priv_data;

    ctx->frame_id = 0;
}


AVCodec als_decoder = {
    "als",
    CODEC_TYPE_AUDIO,
    CODEC_ID_MP4ALS,
    sizeof(ALSDecContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    .flush = flush,
    .capabilities = CODEC_CAP_SUBFRAMES,
    .long_name = NULL_IF_CONFIG_SMALL("MPEG-4 Audio Lossless Coding (ALS)"),
};

