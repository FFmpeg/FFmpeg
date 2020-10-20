/*
 * Copyright (C) 2003-2004 The FFmpeg project
 * Copyright (C) 2019 Peter Ross
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
 * On2 VP3/VP4 Video Decoder
 *
 * VP3 Video Decoder by Mike Melanson (mike at multimedia.cx)
 * For more information about the VP3 coding process, visit:
 *   http://wiki.multimedia.cx/index.php?title=On2_VP3
 *
 * Theora decoder by Alex Beregszaszi
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "get_bits.h"
#include "hpeldsp.h"
#include "internal.h"
#include "mathops.h"
#include "thread.h"
#include "videodsp.h"
#include "vp3data.h"
#include "vp4data.h"
#include "vp3dsp.h"
#include "xiph.h"

#define FRAGMENT_PIXELS 8

// FIXME split things out into their own arrays
typedef struct Vp3Fragment {
    int16_t dc;
    uint8_t coding_method;
    uint8_t qpi;
} Vp3Fragment;

#define SB_NOT_CODED        0
#define SB_PARTIALLY_CODED  1
#define SB_FULLY_CODED      2

// This is the maximum length of a single long bit run that can be encoded
// for superblock coding or block qps. Theora special-cases this to read a
// bit instead of flipping the current bit to allow for runs longer than 4129.
#define MAXIMUM_LONG_BIT_RUN 4129

#define MODE_INTER_NO_MV      0
#define MODE_INTRA            1
#define MODE_INTER_PLUS_MV    2
#define MODE_INTER_LAST_MV    3
#define MODE_INTER_PRIOR_LAST 4
#define MODE_USING_GOLDEN     5
#define MODE_GOLDEN_MV        6
#define MODE_INTER_FOURMV     7
#define CODING_MODE_COUNT     8

/* special internal mode */
#define MODE_COPY             8

static int theora_decode_header(AVCodecContext *avctx, GetBitContext *gb);
static int theora_decode_tables(AVCodecContext *avctx, GetBitContext *gb);


/* There are 6 preset schemes, plus a free-form scheme */
static const int ModeAlphabet[6][CODING_MODE_COUNT] = {
    /* scheme 1: Last motion vector dominates */
    { MODE_INTER_LAST_MV,    MODE_INTER_PRIOR_LAST,
      MODE_INTER_PLUS_MV,    MODE_INTER_NO_MV,
      MODE_INTRA,            MODE_USING_GOLDEN,
      MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 2 */
    { MODE_INTER_LAST_MV,    MODE_INTER_PRIOR_LAST,
      MODE_INTER_NO_MV,      MODE_INTER_PLUS_MV,
      MODE_INTRA,            MODE_USING_GOLDEN,
      MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 3 */
    { MODE_INTER_LAST_MV,    MODE_INTER_PLUS_MV,
      MODE_INTER_PRIOR_LAST, MODE_INTER_NO_MV,
      MODE_INTRA,            MODE_USING_GOLDEN,
      MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 4 */
    { MODE_INTER_LAST_MV,    MODE_INTER_PLUS_MV,
      MODE_INTER_NO_MV,      MODE_INTER_PRIOR_LAST,
      MODE_INTRA,            MODE_USING_GOLDEN,
      MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 5: No motion vector dominates */
    { MODE_INTER_NO_MV,      MODE_INTER_LAST_MV,
      MODE_INTER_PRIOR_LAST, MODE_INTER_PLUS_MV,
      MODE_INTRA,            MODE_USING_GOLDEN,
      MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 6 */
    { MODE_INTER_NO_MV,      MODE_USING_GOLDEN,
      MODE_INTER_LAST_MV,    MODE_INTER_PRIOR_LAST,
      MODE_INTER_PLUS_MV,    MODE_INTRA,
      MODE_GOLDEN_MV,        MODE_INTER_FOURMV },
};

static const uint8_t hilbert_offset[16][2] = {
    { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 },
    { 0, 2 }, { 0, 3 }, { 1, 3 }, { 1, 2 },
    { 2, 2 }, { 2, 3 }, { 3, 3 }, { 3, 2 },
    { 3, 1 }, { 2, 1 }, { 2, 0 }, { 3, 0 }
};

enum {
    VP4_DC_INTRA  = 0,
    VP4_DC_INTER  = 1,
    VP4_DC_GOLDEN = 2,
    NB_VP4_DC_TYPES,
    VP4_DC_UNDEFINED = NB_VP4_DC_TYPES
};

static const uint8_t vp4_pred_block_type_map[8] = {
    [MODE_INTER_NO_MV]      = VP4_DC_INTER,
    [MODE_INTRA]            = VP4_DC_INTRA,
    [MODE_INTER_PLUS_MV]    = VP4_DC_INTER,
    [MODE_INTER_LAST_MV]    = VP4_DC_INTER,
    [MODE_INTER_PRIOR_LAST] = VP4_DC_INTER,
    [MODE_USING_GOLDEN]     = VP4_DC_GOLDEN,
    [MODE_GOLDEN_MV]        = VP4_DC_GOLDEN,
    [MODE_INTER_FOURMV]     = VP4_DC_INTER,
};

typedef struct {
    int dc;
    int type;
} VP4Predictor;

#define MIN_DEQUANT_VAL 2

typedef struct HuffEntry {
    uint8_t len, sym;
} HuffEntry;

typedef struct HuffTable {
    HuffEntry entries[32];
    uint8_t   nb_entries;
} HuffTable;

typedef struct Vp3DecodeContext {
    AVCodecContext *avctx;
    int theora, theora_tables, theora_header;
    int version;
    int width, height;
    int chroma_x_shift, chroma_y_shift;
    ThreadFrame golden_frame;
    ThreadFrame last_frame;
    ThreadFrame current_frame;
    int keyframe;
    uint8_t idct_permutation[64];
    uint8_t idct_scantable[64];
    HpelDSPContext hdsp;
    VideoDSPContext vdsp;
    VP3DSPContext vp3dsp;
    DECLARE_ALIGNED(16, int16_t, block)[64];
    int flipped_image;
    int last_slice_end;
    int skip_loop_filter;

    int qps[3];
    int nqps;
    int last_qps[3];

    int superblock_count;
    int y_superblock_width;
    int y_superblock_height;
    int y_superblock_count;
    int c_superblock_width;
    int c_superblock_height;
    int c_superblock_count;
    int u_superblock_start;
    int v_superblock_start;
    unsigned char *superblock_coding;

    int macroblock_count; /* y macroblock count */
    int macroblock_width;
    int macroblock_height;
    int c_macroblock_count;
    int c_macroblock_width;
    int c_macroblock_height;
    int yuv_macroblock_count; /* y+u+v macroblock count */

    int fragment_count;
    int fragment_width[2];
    int fragment_height[2];

    Vp3Fragment *all_fragments;
    int fragment_start[3];
    int data_offset[3];
    uint8_t offset_x;
    uint8_t offset_y;
    int offset_x_warned;

    int8_t (*motion_val[2])[2];

    /* tables */
    uint16_t coded_dc_scale_factor[2][64];
    uint32_t coded_ac_scale_factor[64];
    uint8_t base_matrix[384][64];
    uint8_t qr_count[2][3];
    uint8_t qr_size[2][3][64];
    uint16_t qr_base[2][3][64];

    /**
     * This is a list of all tokens in bitstream order. Reordering takes place
     * by pulling from each level during IDCT. As a consequence, IDCT must be
     * in Hilbert order, making the minimum slice height 64 for 4:2:0 and 32
     * otherwise. The 32 different tokens with up to 12 bits of extradata are
     * collapsed into 3 types, packed as follows:
     *   (from the low to high bits)
     *
     * 2 bits: type (0,1,2)
     *   0: EOB run, 14 bits for run length (12 needed)
     *   1: zero run, 7 bits for run length
     *                7 bits for the next coefficient (3 needed)
     *   2: coefficient, 14 bits (11 needed)
     *
     * Coefficients are signed, so are packed in the highest bits for automatic
     * sign extension.
     */
    int16_t *dct_tokens[3][64];
    int16_t *dct_tokens_base;
#define TOKEN_EOB(eob_run)              ((eob_run) << 2)
#define TOKEN_ZERO_RUN(coeff, zero_run) (((coeff) * 512) + ((zero_run) << 2) + 1)
#define TOKEN_COEFF(coeff)              (((coeff) * 4) + 2)

    /**
     * number of blocks that contain DCT coefficients at
     * the given level or higher
     */
    int num_coded_frags[3][64];
    int total_num_coded_frags;

    /* this is a list of indexes into the all_fragments array indicating
     * which of the fragments are coded */
    int *coded_fragment_list[3];

    int *kf_coded_fragment_list;
    int *nkf_coded_fragment_list;
    int num_kf_coded_fragment[3];

    /* The first 16 of the following VLCs are for the dc coefficients;
       the others are four groups of 16 VLCs each for ac coefficients. */
    VLC coeff_vlc[5 * 16];

    VLC superblock_run_length_vlc; /* version < 2 */
    VLC fragment_run_length_vlc; /* version < 2 */
    VLC block_pattern_vlc[2]; /* version >= 2*/
    VLC mode_code_vlc;
    VLC motion_vector_vlc; /* version < 2 */
    VLC vp4_mv_vlc[2][7]; /* version >=2 */

    /* these arrays need to be on 16-byte boundaries since SSE2 operations
     * index into them */
    DECLARE_ALIGNED(16, int16_t, qmat)[3][2][3][64];     ///< qmat[qpi][is_inter][plane]

    /* This table contains superblock_count * 16 entries. Each set of 16
     * numbers corresponds to the fragment indexes 0..15 of the superblock.
     * An entry will be -1 to indicate that no entry corresponds to that
     * index. */
    int *superblock_fragments;

    /* This is an array that indicates how a particular macroblock
     * is coded. */
    unsigned char *macroblock_coding;

    uint8_t *edge_emu_buffer;

    /* Huffman decode */
    HuffTable huffman_table[5 * 16];

    uint8_t filter_limit_values[64];
    DECLARE_ALIGNED(8, int, bounding_values_array)[256 + 2];

    VP4Predictor * dc_pred_row; /* dc_pred_row[y_superblock_width * 4] */
} Vp3DecodeContext;

/************************************************************************
 * VP3 specific functions
 ************************************************************************/

static av_cold void free_tables(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;

    av_freep(&s->superblock_coding);
    av_freep(&s->all_fragments);
    av_freep(&s->nkf_coded_fragment_list);
    av_freep(&s->kf_coded_fragment_list);
    av_freep(&s->dct_tokens_base);
    av_freep(&s->superblock_fragments);
    av_freep(&s->macroblock_coding);
    av_freep(&s->dc_pred_row);
    av_freep(&s->motion_val[0]);
    av_freep(&s->motion_val[1]);
}

static void vp3_decode_flush(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;

    if (s->golden_frame.f)
        ff_thread_release_buffer(avctx, &s->golden_frame);
    if (s->last_frame.f)
        ff_thread_release_buffer(avctx, &s->last_frame);
    if (s->current_frame.f)
        ff_thread_release_buffer(avctx, &s->current_frame);
}

static av_cold int vp3_decode_end(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int i, j;

    free_tables(avctx);
    av_freep(&s->edge_emu_buffer);

    s->theora_tables = 0;

    /* release all frames */
    vp3_decode_flush(avctx);
    av_frame_free(&s->current_frame.f);
    av_frame_free(&s->last_frame.f);
    av_frame_free(&s->golden_frame.f);

    for (i = 0; i < FF_ARRAY_ELEMS(s->coeff_vlc); i++)
        ff_free_vlc(&s->coeff_vlc[i]);

    ff_free_vlc(&s->superblock_run_length_vlc);
    ff_free_vlc(&s->fragment_run_length_vlc);
    ff_free_vlc(&s->mode_code_vlc);
    ff_free_vlc(&s->motion_vector_vlc);

    for (j = 0; j < 2; j++)
        for (i = 0; i < 7; i++)
            ff_free_vlc(&s->vp4_mv_vlc[j][i]);

    for (i = 0; i < 2; i++)
        ff_free_vlc(&s->block_pattern_vlc[i]);
    return 0;
}

/**
 * This function sets up all of the various blocks mappings:
 * superblocks <-> fragments, macroblocks <-> fragments,
 * superblocks <-> macroblocks
 *
 * @return 0 is successful; returns 1 if *anything* went wrong.
 */
static int init_block_mapping(Vp3DecodeContext *s)
{
    int sb_x, sb_y, plane;
    int x, y, i, j = 0;

    for (plane = 0; plane < 3; plane++) {
        int sb_width    = plane ? s->c_superblock_width
                                : s->y_superblock_width;
        int sb_height   = plane ? s->c_superblock_height
                                : s->y_superblock_height;
        int frag_width  = s->fragment_width[!!plane];
        int frag_height = s->fragment_height[!!plane];

        for (sb_y = 0; sb_y < sb_height; sb_y++)
            for (sb_x = 0; sb_x < sb_width; sb_x++)
                for (i = 0; i < 16; i++) {
                    x = 4 * sb_x + hilbert_offset[i][0];
                    y = 4 * sb_y + hilbert_offset[i][1];

                    if (x < frag_width && y < frag_height)
                        s->superblock_fragments[j++] = s->fragment_start[plane] +
                                                       y * frag_width + x;
                    else
                        s->superblock_fragments[j++] = -1;
                }
    }

    return 0;  /* successful path out */
}

/*
 * This function sets up the dequantization tables used for a particular
 * frame.
 */
static void init_dequantizer(Vp3DecodeContext *s, int qpi)
{
    int ac_scale_factor = s->coded_ac_scale_factor[s->qps[qpi]];
    int i, plane, inter, qri, bmi, bmj, qistart;

    for (inter = 0; inter < 2; inter++) {
        for (plane = 0; plane < 3; plane++) {
            int dc_scale_factor = s->coded_dc_scale_factor[!!plane][s->qps[qpi]];
            int sum = 0;
            for (qri = 0; qri < s->qr_count[inter][plane]; qri++) {
                sum += s->qr_size[inter][plane][qri];
                if (s->qps[qpi] <= sum)
                    break;
            }
            qistart = sum - s->qr_size[inter][plane][qri];
            bmi     = s->qr_base[inter][plane][qri];
            bmj     = s->qr_base[inter][plane][qri + 1];
            for (i = 0; i < 64; i++) {
                int coeff = (2 * (sum     - s->qps[qpi]) * s->base_matrix[bmi][i] -
                             2 * (qistart - s->qps[qpi]) * s->base_matrix[bmj][i] +
                             s->qr_size[inter][plane][qri]) /
                            (2 * s->qr_size[inter][plane][qri]);

                int qmin   = 8 << (inter + !i);
                int qscale = i ? ac_scale_factor : dc_scale_factor;
                int qbias = (1 + inter) * 3;
                s->qmat[qpi][inter][plane][s->idct_permutation[i]] =
                    (i == 0 || s->version < 2) ? av_clip((qscale * coeff) / 100 * 4, qmin, 4096)
                                               : (qscale * (coeff - qbias) / 100 + qbias) * 4;
            }
            /* all DC coefficients use the same quant so as not to interfere
             * with DC prediction */
            s->qmat[qpi][inter][plane][0] = s->qmat[0][inter][plane][0];
        }
    }
}

/*
 * This function initializes the loop filter boundary limits if the frame's
 * quality index is different from the previous frame's.
 *
 * The filter_limit_values may not be larger than 127.
 */
static void init_loop_filter(Vp3DecodeContext *s)
{
    ff_vp3dsp_set_bounding_values(s->bounding_values_array, s->filter_limit_values[s->qps[0]]);
}

/*
 * This function unpacks all of the superblock/macroblock/fragment coding
 * information from the bitstream.
 */
static int unpack_superblocks(Vp3DecodeContext *s, GetBitContext *gb)
{
    int superblock_starts[3] = {
        0, s->u_superblock_start, s->v_superblock_start
    };
    int bit = 0;
    int current_superblock = 0;
    int current_run = 0;
    int num_partial_superblocks = 0;

    int i, j;
    int current_fragment;
    int plane;
    int plane0_num_coded_frags = 0;

    if (s->keyframe) {
        memset(s->superblock_coding, SB_FULLY_CODED, s->superblock_count);
    } else {
        /* unpack the list of partially-coded superblocks */
        bit         = get_bits1(gb) ^ 1;
        current_run = 0;

        while (current_superblock < s->superblock_count && get_bits_left(gb) > 0) {
            if (s->theora && current_run == MAXIMUM_LONG_BIT_RUN)
                bit = get_bits1(gb);
            else
                bit ^= 1;

            current_run = get_vlc2(gb, s->superblock_run_length_vlc.table,
                                   6, 2) + 1;
            if (current_run == 34)
                current_run += get_bits(gb, 12);

            if (current_run > s->superblock_count - current_superblock) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Invalid partially coded superblock run length\n");
                return -1;
            }

            memset(s->superblock_coding + current_superblock, bit, current_run);

            current_superblock += current_run;
            if (bit)
                num_partial_superblocks += current_run;
        }

        /* unpack the list of fully coded superblocks if any of the blocks were
         * not marked as partially coded in the previous step */
        if (num_partial_superblocks < s->superblock_count) {
            int superblocks_decoded = 0;

            current_superblock = 0;
            bit                = get_bits1(gb) ^ 1;
            current_run        = 0;

            while (superblocks_decoded < s->superblock_count - num_partial_superblocks &&
                   get_bits_left(gb) > 0) {
                if (s->theora && current_run == MAXIMUM_LONG_BIT_RUN)
                    bit = get_bits1(gb);
                else
                    bit ^= 1;

                current_run = get_vlc2(gb, s->superblock_run_length_vlc.table,
                                       6, 2) + 1;
                if (current_run == 34)
                    current_run += get_bits(gb, 12);

                for (j = 0; j < current_run; current_superblock++) {
                    if (current_superblock >= s->superblock_count) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Invalid fully coded superblock run length\n");
                        return -1;
                    }

                    /* skip any superblocks already marked as partially coded */
                    if (s->superblock_coding[current_superblock] == SB_NOT_CODED) {
                        s->superblock_coding[current_superblock] = 2 * bit;
                        j++;
                    }
                }
                superblocks_decoded += current_run;
            }
        }

        /* if there were partial blocks, initialize bitstream for
         * unpacking fragment codings */
        if (num_partial_superblocks) {
            current_run = 0;
            bit         = get_bits1(gb);
            /* toggle the bit because as soon as the first run length is
             * fetched the bit will be toggled again */
            bit ^= 1;
        }
    }

    /* figure out which fragments are coded; iterate through each
     * superblock (all planes) */
    s->total_num_coded_frags = 0;
    memset(s->macroblock_coding, MODE_COPY, s->macroblock_count);

    s->coded_fragment_list[0] = s->keyframe ? s->kf_coded_fragment_list
                                            : s->nkf_coded_fragment_list;

    for (plane = 0; plane < 3; plane++) {
        int sb_start = superblock_starts[plane];
        int sb_end   = sb_start + (plane ? s->c_superblock_count
                                         : s->y_superblock_count);
        int num_coded_frags = 0;

        if (s->keyframe) {
            if (s->num_kf_coded_fragment[plane] == -1) {
                for (i = sb_start; i < sb_end; i++) {
                    /* iterate through all 16 fragments in a superblock */
                    for (j = 0; j < 16; j++) {
                        /* if the fragment is in bounds, check its coding status */
                        current_fragment = s->superblock_fragments[i * 16 + j];
                        if (current_fragment != -1) {
                            s->coded_fragment_list[plane][num_coded_frags++] =
                                current_fragment;
                        }
                    }
                }
                s->num_kf_coded_fragment[plane] = num_coded_frags;
            } else
                num_coded_frags = s->num_kf_coded_fragment[plane];
        } else {
            for (i = sb_start; i < sb_end && get_bits_left(gb) > 0; i++) {
                if (get_bits_left(gb) < plane0_num_coded_frags >> 2) {
                    return AVERROR_INVALIDDATA;
                }
                /* iterate through all 16 fragments in a superblock */
                for (j = 0; j < 16; j++) {
                    /* if the fragment is in bounds, check its coding status */
                    current_fragment = s->superblock_fragments[i * 16 + j];
                    if (current_fragment != -1) {
                        int coded = s->superblock_coding[i];

                        if (coded == SB_PARTIALLY_CODED) {
                            /* fragment may or may not be coded; this is the case
                             * that cares about the fragment coding runs */
                            if (current_run-- == 0) {
                                bit        ^= 1;
                                current_run = get_vlc2(gb, s->fragment_run_length_vlc.table, 5, 2);
                            }
                            coded = bit;
                        }

                        if (coded) {
                            /* default mode; actual mode will be decoded in
                             * the next phase */
                            s->all_fragments[current_fragment].coding_method =
                                MODE_INTER_NO_MV;
                            s->coded_fragment_list[plane][num_coded_frags++] =
                                current_fragment;
                        } else {
                            /* not coded; copy this fragment from the prior frame */
                            s->all_fragments[current_fragment].coding_method =
                                MODE_COPY;
                        }
                    }
                }
            }
        }
        if (!plane)
            plane0_num_coded_frags = num_coded_frags;
        s->total_num_coded_frags += num_coded_frags;
        for (i = 0; i < 64; i++)
            s->num_coded_frags[plane][i] = num_coded_frags;
        if (plane < 2)
            s->coded_fragment_list[plane + 1] = s->coded_fragment_list[plane] +
                                                num_coded_frags;
    }
    return 0;
}

#define BLOCK_X (2 * mb_x + (k & 1))
#define BLOCK_Y (2 * mb_y + (k >> 1))

#if CONFIG_VP4_DECODER
/**
 * @return number of blocks, or > yuv_macroblock_count on error.
 *         return value is always >= 1.
 */
static int vp4_get_mb_count(Vp3DecodeContext *s, GetBitContext *gb)
{
    int v = 1;
    int bits;
    while ((bits = show_bits(gb, 9)) == 0x1ff) {
        skip_bits(gb, 9);
        v += 256;
        if (v > s->yuv_macroblock_count) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid run length\n");
            return v;
        }
    }
#define body(n) { \
    skip_bits(gb, 2 + n); \
    v += (1 << n) + get_bits(gb, n); }
#define thresh(n) (0x200 - (0x80 >> n))
#define else_if(n) else if (bits < thresh(n)) body(n)
    if (bits < 0x100) {
        skip_bits(gb, 1);
    } else if (bits < thresh(0)) {
        skip_bits(gb, 2);
        v += 1;
    }
    else_if(1)
    else_if(2)
    else_if(3)
    else_if(4)
    else_if(5)
    else_if(6)
    else body(7)
#undef body
#undef thresh
#undef else_if
    return v;
}

static int vp4_get_block_pattern(Vp3DecodeContext *s, GetBitContext *gb, int *next_block_pattern_table)
{
    int v = get_vlc2(gb, s->block_pattern_vlc[*next_block_pattern_table].table, 3, 2);
    if (v == -1) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid block pattern\n");
        *next_block_pattern_table = 0;
        return 0;
    }
    *next_block_pattern_table = vp4_block_pattern_table_selector[v];
    return v + 1;
}

static int vp4_unpack_macroblocks(Vp3DecodeContext *s, GetBitContext *gb)
{
    int plane, i, j, k, fragment;
    int next_block_pattern_table;
    int bit, current_run, has_partial;

    memset(s->macroblock_coding, MODE_COPY, s->macroblock_count);

    if (s->keyframe)
        return 0;

    has_partial = 0;
    bit         = get_bits1(gb);
    for (i = 0; i < s->yuv_macroblock_count; i += current_run) {
        if (get_bits_left(gb) <= 0)
            return AVERROR_INVALIDDATA;
        current_run = vp4_get_mb_count(s, gb);
        if (current_run > s->yuv_macroblock_count - i)
            return -1;
        memset(s->superblock_coding + i, 2 * bit, current_run);
        bit ^= 1;
        has_partial |= bit;
    }

    if (has_partial) {
        if (get_bits_left(gb) <= 0)
            return AVERROR_INVALIDDATA;
        bit  = get_bits1(gb);
        current_run = vp4_get_mb_count(s, gb);
        for (i = 0; i < s->yuv_macroblock_count; i++) {
            if (!s->superblock_coding[i]) {
                if (!current_run) {
                    bit ^= 1;
                    current_run = vp4_get_mb_count(s, gb);
                }
                s->superblock_coding[i] = bit;
                current_run--;
            }
        }
        if (current_run) /* handle situation when vp4_get_mb_count() fails */
            return -1;
    }

    next_block_pattern_table = 0;
    i = 0;
    for (plane = 0; plane < 3; plane++) {
        int sb_x, sb_y;
        int sb_width = plane ? s->c_superblock_width : s->y_superblock_width;
        int sb_height = plane ? s->c_superblock_height : s->y_superblock_height;
        int mb_width = plane ? s->c_macroblock_width : s->macroblock_width;
        int mb_height = plane ? s->c_macroblock_height : s->macroblock_height;
        int fragment_width = s->fragment_width[!!plane];
        int fragment_height = s->fragment_height[!!plane];

        for (sb_y = 0; sb_y < sb_height; sb_y++) {
            for (sb_x = 0; sb_x < sb_width; sb_x++) {
                for (j = 0; j < 4; j++) {
                    int mb_x = 2 * sb_x + (j >> 1);
                    int mb_y = 2 * sb_y + (j >> 1) ^ (j & 1);
                    int mb_coded, pattern, coded;

                    if (mb_x >= mb_width || mb_y >= mb_height)
                        continue;

                    mb_coded = s->superblock_coding[i++];

                    if (mb_coded == SB_FULLY_CODED)
                        pattern = 0xF;
                    else if (mb_coded == SB_PARTIALLY_CODED)
                        pattern = vp4_get_block_pattern(s, gb, &next_block_pattern_table);
                    else
                        pattern = 0;

                    for (k = 0; k < 4; k++) {
                        if (BLOCK_X >= fragment_width || BLOCK_Y >= fragment_height)
                            continue;
                        fragment = s->fragment_start[plane] + BLOCK_Y * fragment_width + BLOCK_X;
                        coded = pattern & (8 >> k);
                        /* MODE_INTER_NO_MV is the default for coded fragments.
                           the actual method is decoded in the next phase. */
                        s->all_fragments[fragment].coding_method = coded ? MODE_INTER_NO_MV : MODE_COPY;
                    }
                }
            }
        }
    }
    return 0;
}
#endif

/*
 * This function unpacks all the coding mode data for individual macroblocks
 * from the bitstream.
 */
static int unpack_modes(Vp3DecodeContext *s, GetBitContext *gb)
{
    int i, j, k, sb_x, sb_y;
    int scheme;
    int current_macroblock;
    int current_fragment;
    int coding_mode;
    int custom_mode_alphabet[CODING_MODE_COUNT];
    const int *alphabet;
    Vp3Fragment *frag;

    if (s->keyframe) {
        for (i = 0; i < s->fragment_count; i++)
            s->all_fragments[i].coding_method = MODE_INTRA;
    } else {
        /* fetch the mode coding scheme for this frame */
        scheme = get_bits(gb, 3);

        /* is it a custom coding scheme? */
        if (scheme == 0) {
            for (i = 0; i < 8; i++)
                custom_mode_alphabet[i] = MODE_INTER_NO_MV;
            for (i = 0; i < 8; i++)
                custom_mode_alphabet[get_bits(gb, 3)] = i;
            alphabet = custom_mode_alphabet;
        } else
            alphabet = ModeAlphabet[scheme - 1];

        /* iterate through all of the macroblocks that contain 1 or more
         * coded fragments */
        for (sb_y = 0; sb_y < s->y_superblock_height; sb_y++) {
            for (sb_x = 0; sb_x < s->y_superblock_width; sb_x++) {
                if (get_bits_left(gb) <= 0)
                    return -1;

                for (j = 0; j < 4; j++) {
                    int mb_x = 2 * sb_x + (j >> 1);
                    int mb_y = 2 * sb_y + (((j >> 1) + j) & 1);
                    current_macroblock = mb_y * s->macroblock_width + mb_x;

                    if (mb_x >= s->macroblock_width ||
                        mb_y >= s->macroblock_height)
                        continue;

                    /* coding modes are only stored if the macroblock has
                     * at least one luma block coded, otherwise it must be
                     * INTER_NO_MV */
                    for (k = 0; k < 4; k++) {
                        current_fragment = BLOCK_Y *
                                           s->fragment_width[0] + BLOCK_X;
                        if (s->all_fragments[current_fragment].coding_method != MODE_COPY)
                            break;
                    }
                    if (k == 4) {
                        s->macroblock_coding[current_macroblock] = MODE_INTER_NO_MV;
                        continue;
                    }

                    /* mode 7 means get 3 bits for each coding mode */
                    if (scheme == 7)
                        coding_mode = get_bits(gb, 3);
                    else
                        coding_mode = alphabet[get_vlc2(gb, s->mode_code_vlc.table, 3, 3)];

                    s->macroblock_coding[current_macroblock] = coding_mode;
                    for (k = 0; k < 4; k++) {
                        frag = s->all_fragments + BLOCK_Y * s->fragment_width[0] + BLOCK_X;
                        if (frag->coding_method != MODE_COPY)
                            frag->coding_method = coding_mode;
                    }

#define SET_CHROMA_MODES                                                      \
    if (frag[s->fragment_start[1]].coding_method != MODE_COPY)                \
        frag[s->fragment_start[1]].coding_method = coding_mode;               \
    if (frag[s->fragment_start[2]].coding_method != MODE_COPY)                \
        frag[s->fragment_start[2]].coding_method = coding_mode;

                    if (s->chroma_y_shift) {
                        frag = s->all_fragments + mb_y *
                               s->fragment_width[1] + mb_x;
                        SET_CHROMA_MODES
                    } else if (s->chroma_x_shift) {
                        frag = s->all_fragments +
                               2 * mb_y * s->fragment_width[1] + mb_x;
                        for (k = 0; k < 2; k++) {
                            SET_CHROMA_MODES
                            frag += s->fragment_width[1];
                        }
                    } else {
                        for (k = 0; k < 4; k++) {
                            frag = s->all_fragments +
                                   BLOCK_Y * s->fragment_width[1] + BLOCK_X;
                            SET_CHROMA_MODES
                        }
                    }
                }
            }
        }
    }

    return 0;
}

static int vp4_get_mv(Vp3DecodeContext *s, GetBitContext *gb, int axis, int last_motion)
{
    int v = get_vlc2(gb, s->vp4_mv_vlc[axis][vp4_mv_table_selector[FFABS(last_motion)]].table, 6, 2) - 31;
    return last_motion < 0 ? -v : v;
}

/*
 * This function unpacks all the motion vectors for the individual
 * macroblocks from the bitstream.
 */
static int unpack_vectors(Vp3DecodeContext *s, GetBitContext *gb)
{
    int j, k, sb_x, sb_y;
    int coding_mode;
    int motion_x[4];
    int motion_y[4];
    int last_motion_x = 0;
    int last_motion_y = 0;
    int prior_last_motion_x = 0;
    int prior_last_motion_y = 0;
    int last_gold_motion_x = 0;
    int last_gold_motion_y = 0;
    int current_macroblock;
    int current_fragment;
    int frag;

    if (s->keyframe)
        return 0;

    /* coding mode 0 is the VLC scheme; 1 is the fixed code scheme; 2 is VP4 code scheme */
    coding_mode = s->version < 2 ? get_bits1(gb) : 2;

    /* iterate through all of the macroblocks that contain 1 or more
     * coded fragments */
    for (sb_y = 0; sb_y < s->y_superblock_height; sb_y++) {
        for (sb_x = 0; sb_x < s->y_superblock_width; sb_x++) {
            if (get_bits_left(gb) <= 0)
                return -1;

            for (j = 0; j < 4; j++) {
                int mb_x = 2 * sb_x + (j >> 1);
                int mb_y = 2 * sb_y + (((j >> 1) + j) & 1);
                current_macroblock = mb_y * s->macroblock_width + mb_x;

                if (mb_x >= s->macroblock_width  ||
                    mb_y >= s->macroblock_height ||
                    s->macroblock_coding[current_macroblock] == MODE_COPY)
                    continue;

                switch (s->macroblock_coding[current_macroblock]) {
                case MODE_GOLDEN_MV:
                    if (coding_mode == 2) { /* VP4 */
                        last_gold_motion_x = motion_x[0] = vp4_get_mv(s, gb, 0, last_gold_motion_x);
                        last_gold_motion_y = motion_y[0] = vp4_get_mv(s, gb, 1, last_gold_motion_y);
                        break;
                    } /* otherwise fall through */
                case MODE_INTER_PLUS_MV:
                    /* all 6 fragments use the same motion vector */
                    if (coding_mode == 0) {
                        motion_x[0] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                        motion_y[0] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                    } else if (coding_mode == 1) {
                        motion_x[0] = fixed_motion_vector_table[get_bits(gb, 6)];
                        motion_y[0] = fixed_motion_vector_table[get_bits(gb, 6)];
                    } else { /* VP4 */
                        motion_x[0] = vp4_get_mv(s, gb, 0, last_motion_x);
                        motion_y[0] = vp4_get_mv(s, gb, 1, last_motion_y);
                    }

                    /* vector maintenance, only on MODE_INTER_PLUS_MV */
                    if (s->macroblock_coding[current_macroblock] == MODE_INTER_PLUS_MV) {
                        prior_last_motion_x = last_motion_x;
                        prior_last_motion_y = last_motion_y;
                        last_motion_x       = motion_x[0];
                        last_motion_y       = motion_y[0];
                    }
                    break;

                case MODE_INTER_FOURMV:
                    /* vector maintenance */
                    prior_last_motion_x = last_motion_x;
                    prior_last_motion_y = last_motion_y;

                    /* fetch 4 vectors from the bitstream, one for each
                     * Y fragment, then average for the C fragment vectors */
                    for (k = 0; k < 4; k++) {
                        current_fragment = BLOCK_Y * s->fragment_width[0] + BLOCK_X;
                        if (s->all_fragments[current_fragment].coding_method != MODE_COPY) {
                            if (coding_mode == 0) {
                                motion_x[k] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                                motion_y[k] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                            } else if (coding_mode == 1) {
                                motion_x[k] = fixed_motion_vector_table[get_bits(gb, 6)];
                                motion_y[k] = fixed_motion_vector_table[get_bits(gb, 6)];
                            } else { /* VP4 */
                                motion_x[k] = vp4_get_mv(s, gb, 0, prior_last_motion_x);
                                motion_y[k] = vp4_get_mv(s, gb, 1, prior_last_motion_y);
                            }
                            last_motion_x = motion_x[k];
                            last_motion_y = motion_y[k];
                        } else {
                            motion_x[k] = 0;
                            motion_y[k] = 0;
                        }
                    }
                    break;

                case MODE_INTER_LAST_MV:
                    /* all 6 fragments use the last motion vector */
                    motion_x[0] = last_motion_x;
                    motion_y[0] = last_motion_y;

                    /* no vector maintenance (last vector remains the
                     * last vector) */
                    break;

                case MODE_INTER_PRIOR_LAST:
                    /* all 6 fragments use the motion vector prior to the
                     * last motion vector */
                    motion_x[0] = prior_last_motion_x;
                    motion_y[0] = prior_last_motion_y;

                    /* vector maintenance */
                    prior_last_motion_x = last_motion_x;
                    prior_last_motion_y = last_motion_y;
                    last_motion_x       = motion_x[0];
                    last_motion_y       = motion_y[0];
                    break;

                default:
                    /* covers intra, inter without MV, golden without MV */
                    motion_x[0] = 0;
                    motion_y[0] = 0;

                    /* no vector maintenance */
                    break;
                }

                /* assign the motion vectors to the correct fragments */
                for (k = 0; k < 4; k++) {
                    current_fragment =
                        BLOCK_Y * s->fragment_width[0] + BLOCK_X;
                    if (s->macroblock_coding[current_macroblock] == MODE_INTER_FOURMV) {
                        s->motion_val[0][current_fragment][0] = motion_x[k];
                        s->motion_val[0][current_fragment][1] = motion_y[k];
                    } else {
                        s->motion_val[0][current_fragment][0] = motion_x[0];
                        s->motion_val[0][current_fragment][1] = motion_y[0];
                    }
                }

                if (s->chroma_y_shift) {
                    if (s->macroblock_coding[current_macroblock] == MODE_INTER_FOURMV) {
                        motion_x[0] = RSHIFT(motion_x[0] + motion_x[1] +
                                             motion_x[2] + motion_x[3], 2);
                        motion_y[0] = RSHIFT(motion_y[0] + motion_y[1] +
                                             motion_y[2] + motion_y[3], 2);
                    }
                    if (s->version <= 2) {
                        motion_x[0] = (motion_x[0] >> 1) | (motion_x[0] & 1);
                        motion_y[0] = (motion_y[0] >> 1) | (motion_y[0] & 1);
                    }
                    frag = mb_y * s->fragment_width[1] + mb_x;
                    s->motion_val[1][frag][0] = motion_x[0];
                    s->motion_val[1][frag][1] = motion_y[0];
                } else if (s->chroma_x_shift) {
                    if (s->macroblock_coding[current_macroblock] == MODE_INTER_FOURMV) {
                        motion_x[0] = RSHIFT(motion_x[0] + motion_x[1], 1);
                        motion_y[0] = RSHIFT(motion_y[0] + motion_y[1], 1);
                        motion_x[1] = RSHIFT(motion_x[2] + motion_x[3], 1);
                        motion_y[1] = RSHIFT(motion_y[2] + motion_y[3], 1);
                    } else {
                        motion_x[1] = motion_x[0];
                        motion_y[1] = motion_y[0];
                    }
                    if (s->version <= 2) {
                        motion_x[0] = (motion_x[0] >> 1) | (motion_x[0] & 1);
                        motion_x[1] = (motion_x[1] >> 1) | (motion_x[1] & 1);
                    }
                    frag = 2 * mb_y * s->fragment_width[1] + mb_x;
                    for (k = 0; k < 2; k++) {
                        s->motion_val[1][frag][0] = motion_x[k];
                        s->motion_val[1][frag][1] = motion_y[k];
                        frag += s->fragment_width[1];
                    }
                } else {
                    for (k = 0; k < 4; k++) {
                        frag = BLOCK_Y * s->fragment_width[1] + BLOCK_X;
                        if (s->macroblock_coding[current_macroblock] == MODE_INTER_FOURMV) {
                            s->motion_val[1][frag][0] = motion_x[k];
                            s->motion_val[1][frag][1] = motion_y[k];
                        } else {
                            s->motion_val[1][frag][0] = motion_x[0];
                            s->motion_val[1][frag][1] = motion_y[0];
                        }
                    }
                }
            }
        }
    }

    return 0;
}

static int unpack_block_qpis(Vp3DecodeContext *s, GetBitContext *gb)
{
    int qpi, i, j, bit, run_length, blocks_decoded, num_blocks_at_qpi;
    int num_blocks = s->total_num_coded_frags;

    for (qpi = 0; qpi < s->nqps - 1 && num_blocks > 0; qpi++) {
        i = blocks_decoded = num_blocks_at_qpi = 0;

        bit        = get_bits1(gb) ^ 1;
        run_length = 0;

        do {
            if (run_length == MAXIMUM_LONG_BIT_RUN)
                bit = get_bits1(gb);
            else
                bit ^= 1;

            run_length = get_vlc2(gb, s->superblock_run_length_vlc.table, 6, 2) + 1;
            if (run_length == 34)
                run_length += get_bits(gb, 12);
            blocks_decoded += run_length;

            if (!bit)
                num_blocks_at_qpi += run_length;

            for (j = 0; j < run_length; i++) {
                if (i >= s->total_num_coded_frags)
                    return -1;

                if (s->all_fragments[s->coded_fragment_list[0][i]].qpi == qpi) {
                    s->all_fragments[s->coded_fragment_list[0][i]].qpi += bit;
                    j++;
                }
            }
        } while (blocks_decoded < num_blocks && get_bits_left(gb) > 0);

        num_blocks -= num_blocks_at_qpi;
    }

    return 0;
}

static inline int get_eob_run(GetBitContext *gb, int token)
{
    int v = eob_run_table[token].base;
    if (eob_run_table[token].bits)
        v += get_bits(gb, eob_run_table[token].bits);
    return v;
}

static inline int get_coeff(GetBitContext *gb, int token, int16_t *coeff)
{
    int bits_to_get, zero_run;

    bits_to_get = coeff_get_bits[token];
    if (bits_to_get)
        bits_to_get = get_bits(gb, bits_to_get);
    *coeff = coeff_tables[token][bits_to_get];

    zero_run = zero_run_base[token];
    if (zero_run_get_bits[token])
        zero_run += get_bits(gb, zero_run_get_bits[token]);

    return zero_run;
}

/*
 * This function is called by unpack_dct_coeffs() to extract the VLCs from
 * the bitstream. The VLCs encode tokens which are used to unpack DCT
 * data. This function unpacks all the VLCs for either the Y plane or both
 * C planes, and is called for DC coefficients or different AC coefficient
 * levels (since different coefficient types require different VLC tables.
 *
 * This function returns a residual eob run. E.g, if a particular token gave
 * instructions to EOB the next 5 fragments and there were only 2 fragments
 * left in the current fragment range, 3 would be returned so that it could
 * be passed into the next call to this same function.
 */
static int unpack_vlcs(Vp3DecodeContext *s, GetBitContext *gb,
                       VLC *table, int coeff_index,
                       int plane,
                       int eob_run)
{
    int i, j = 0;
    int token;
    int zero_run  = 0;
    int16_t coeff = 0;
    int blocks_ended;
    int coeff_i = 0;
    int num_coeffs      = s->num_coded_frags[plane][coeff_index];
    int16_t *dct_tokens = s->dct_tokens[plane][coeff_index];

    /* local references to structure members to avoid repeated dereferences */
    int *coded_fragment_list   = s->coded_fragment_list[plane];
    Vp3Fragment *all_fragments = s->all_fragments;
    VLC_TYPE(*vlc_table)[2] = table->table;

    if (num_coeffs < 0) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Invalid number of coefficients at level %d\n", coeff_index);
        return AVERROR_INVALIDDATA;
    }

    if (eob_run > num_coeffs) {
        coeff_i      =
        blocks_ended = num_coeffs;
        eob_run     -= num_coeffs;
    } else {
        coeff_i      =
        blocks_ended = eob_run;
        eob_run      = 0;
    }

    // insert fake EOB token to cover the split between planes or zzi
    if (blocks_ended)
        dct_tokens[j++] = blocks_ended << 2;

    while (coeff_i < num_coeffs && get_bits_left(gb) > 0) {
        /* decode a VLC into a token */
        token = get_vlc2(gb, vlc_table, 11, 3);
        /* use the token to get a zero run, a coefficient, and an eob run */
        if ((unsigned) token <= 6U) {
            eob_run = get_eob_run(gb, token);
            if (!eob_run)
                eob_run = INT_MAX;

            // record only the number of blocks ended in this plane,
            // any spill will be recorded in the next plane.
            if (eob_run > num_coeffs - coeff_i) {
                dct_tokens[j++] = TOKEN_EOB(num_coeffs - coeff_i);
                blocks_ended   += num_coeffs - coeff_i;
                eob_run        -= num_coeffs - coeff_i;
                coeff_i         = num_coeffs;
            } else {
                dct_tokens[j++] = TOKEN_EOB(eob_run);
                blocks_ended   += eob_run;
                coeff_i        += eob_run;
                eob_run         = 0;
            }
        } else if (token >= 0) {
            zero_run = get_coeff(gb, token, &coeff);

            if (zero_run) {
                dct_tokens[j++] = TOKEN_ZERO_RUN(coeff, zero_run);
            } else {
                // Save DC into the fragment structure. DC prediction is
                // done in raster order, so the actual DC can't be in with
                // other tokens. We still need the token in dct_tokens[]
                // however, or else the structure collapses on itself.
                if (!coeff_index)
                    all_fragments[coded_fragment_list[coeff_i]].dc = coeff;

                dct_tokens[j++] = TOKEN_COEFF(coeff);
            }

            if (coeff_index + zero_run > 64) {
                av_log(s->avctx, AV_LOG_DEBUG,
                       "Invalid zero run of %d with %d coeffs left\n",
                       zero_run, 64 - coeff_index);
                zero_run = 64 - coeff_index;
            }

            // zero runs code multiple coefficients,
            // so don't try to decode coeffs for those higher levels
            for (i = coeff_index + 1; i <= coeff_index + zero_run; i++)
                s->num_coded_frags[plane][i]--;
            coeff_i++;
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid token %d\n", token);
            return -1;
        }
    }

    if (blocks_ended > s->num_coded_frags[plane][coeff_index])
        av_log(s->avctx, AV_LOG_ERROR, "More blocks ended than coded!\n");

    // decrement the number of blocks that have higher coefficients for each
    // EOB run at this level
    if (blocks_ended)
        for (i = coeff_index + 1; i < 64; i++)
            s->num_coded_frags[plane][i] -= blocks_ended;

    // setup the next buffer
    if (plane < 2)
        s->dct_tokens[plane + 1][coeff_index] = dct_tokens + j;
    else if (coeff_index < 63)
        s->dct_tokens[0][coeff_index + 1] = dct_tokens + j;

    return eob_run;
}

static void reverse_dc_prediction(Vp3DecodeContext *s,
                                  int first_fragment,
                                  int fragment_width,
                                  int fragment_height);
/*
 * This function unpacks all of the DCT coefficient data from the
 * bitstream.
 */
static int unpack_dct_coeffs(Vp3DecodeContext *s, GetBitContext *gb)
{
    int i;
    int dc_y_table;
    int dc_c_table;
    int ac_y_table;
    int ac_c_table;
    int residual_eob_run = 0;
    VLC *y_tables[64];
    VLC *c_tables[64];

    s->dct_tokens[0][0] = s->dct_tokens_base;

    if (get_bits_left(gb) < 16)
        return AVERROR_INVALIDDATA;

    /* fetch the DC table indexes */
    dc_y_table = get_bits(gb, 4);
    dc_c_table = get_bits(gb, 4);

    /* unpack the Y plane DC coefficients */
    residual_eob_run = unpack_vlcs(s, gb, &s->coeff_vlc[dc_y_table], 0,
                                   0, residual_eob_run);
    if (residual_eob_run < 0)
        return residual_eob_run;
    if (get_bits_left(gb) < 8)
        return AVERROR_INVALIDDATA;

    /* reverse prediction of the Y-plane DC coefficients */
    reverse_dc_prediction(s, 0, s->fragment_width[0], s->fragment_height[0]);

    /* unpack the C plane DC coefficients */
    residual_eob_run = unpack_vlcs(s, gb, &s->coeff_vlc[dc_c_table], 0,
                                   1, residual_eob_run);
    if (residual_eob_run < 0)
        return residual_eob_run;
    residual_eob_run = unpack_vlcs(s, gb, &s->coeff_vlc[dc_c_table], 0,
                                   2, residual_eob_run);
    if (residual_eob_run < 0)
        return residual_eob_run;

    /* reverse prediction of the C-plane DC coefficients */
    if (!(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
        reverse_dc_prediction(s, s->fragment_start[1],
                              s->fragment_width[1], s->fragment_height[1]);
        reverse_dc_prediction(s, s->fragment_start[2],
                              s->fragment_width[1], s->fragment_height[1]);
    }

    if (get_bits_left(gb) < 8)
        return AVERROR_INVALIDDATA;
    /* fetch the AC table indexes */
    ac_y_table = get_bits(gb, 4);
    ac_c_table = get_bits(gb, 4);

    /* build tables of AC VLC tables */
    for (i = 1; i <= 5; i++) {
        /* AC VLC table group 1 */
        y_tables[i] = &s->coeff_vlc[ac_y_table + 16];
        c_tables[i] = &s->coeff_vlc[ac_c_table + 16];
    }
    for (i = 6; i <= 14; i++) {
        /* AC VLC table group 2 */
        y_tables[i] = &s->coeff_vlc[ac_y_table + 32];
        c_tables[i] = &s->coeff_vlc[ac_c_table + 32];
    }
    for (i = 15; i <= 27; i++) {
        /* AC VLC table group 3 */
        y_tables[i] = &s->coeff_vlc[ac_y_table + 48];
        c_tables[i] = &s->coeff_vlc[ac_c_table + 48];
    }
    for (i = 28; i <= 63; i++) {
        /* AC VLC table group 4 */
        y_tables[i] = &s->coeff_vlc[ac_y_table + 64];
        c_tables[i] = &s->coeff_vlc[ac_c_table + 64];
    }

    /* decode all AC coefficients */
    for (i = 1; i <= 63; i++) {
        residual_eob_run = unpack_vlcs(s, gb, y_tables[i], i,
                                       0, residual_eob_run);
        if (residual_eob_run < 0)
            return residual_eob_run;

        residual_eob_run = unpack_vlcs(s, gb, c_tables[i], i,
                                       1, residual_eob_run);
        if (residual_eob_run < 0)
            return residual_eob_run;
        residual_eob_run = unpack_vlcs(s, gb, c_tables[i], i,
                                       2, residual_eob_run);
        if (residual_eob_run < 0)
            return residual_eob_run;
    }

    return 0;
}

#if CONFIG_VP4_DECODER
/**
 * eob_tracker[] is instead of TOKEN_EOB(value)
 * a dummy TOKEN_EOB(0) value is used to make vp3_dequant work
 *
 * @return < 0 on error
 */
static int vp4_unpack_vlcs(Vp3DecodeContext *s, GetBitContext *gb,
                       VLC *vlc_tables[64],
                       int plane, int eob_tracker[64], int fragment)
{
    int token;
    int zero_run  = 0;
    int16_t coeff = 0;
    int coeff_i = 0;
    int eob_run;

    while (!eob_tracker[coeff_i]) {
        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;

        token = get_vlc2(gb, vlc_tables[coeff_i]->table, 11, 3);

        /* use the token to get a zero run, a coefficient, and an eob run */
        if ((unsigned) token <= 6U) {
            eob_run = get_eob_run(gb, token);
            *s->dct_tokens[plane][coeff_i]++ = TOKEN_EOB(0);
            eob_tracker[coeff_i] = eob_run - 1;
            return 0;
        } else if (token >= 0) {
            zero_run = get_coeff(gb, token, &coeff);

            if (zero_run) {
                if (coeff_i + zero_run > 64) {
                    av_log(s->avctx, AV_LOG_DEBUG,
                        "Invalid zero run of %d with %d coeffs left\n",
                        zero_run, 64 - coeff_i);
                    zero_run = 64 - coeff_i;
                }
                *s->dct_tokens[plane][coeff_i]++ = TOKEN_ZERO_RUN(coeff, zero_run);
                coeff_i += zero_run;
            } else {
                if (!coeff_i)
                    s->all_fragments[fragment].dc = coeff;

                *s->dct_tokens[plane][coeff_i]++ = TOKEN_COEFF(coeff);
            }
            coeff_i++;
            if (coeff_i >= 64) /* > 64 occurs when there is a zero_run overflow */
                return 0; /* stop */
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid token %d\n", token);
            return -1;
        }
    }
    *s->dct_tokens[plane][coeff_i]++ = TOKEN_EOB(0);
    eob_tracker[coeff_i]--;
    return 0;
}

static void vp4_dc_predictor_reset(VP4Predictor *p)
{
    p->dc = 0;
    p->type = VP4_DC_UNDEFINED;
}

static void vp4_dc_pred_before(const Vp3DecodeContext *s, VP4Predictor dc_pred[6][6], int sb_x)
{
    int i, j;

    for (i = 0; i < 4; i++)
        dc_pred[0][i + 1] = s->dc_pred_row[sb_x * 4 + i];

    for (j = 1; j < 5; j++)
        for (i = 0; i < 4; i++)
            vp4_dc_predictor_reset(&dc_pred[j][i + 1]);
}

static void vp4_dc_pred_after(Vp3DecodeContext *s, VP4Predictor dc_pred[6][6], int sb_x)
{
    int i;

    for (i = 0; i < 4; i++)
        s->dc_pred_row[sb_x * 4 + i] = dc_pred[4][i + 1];

    for (i = 1; i < 5; i++)
        dc_pred[i][0] = dc_pred[i][4];
}

/* note: dc_pred points to the current block */
static int vp4_dc_pred(const Vp3DecodeContext *s, const VP4Predictor * dc_pred, const int * last_dc, int type, int plane)
{
    int count = 0;
    int dc = 0;

    if (dc_pred[-6].type == type) {
        dc += dc_pred[-6].dc;
        count++;
    }

    if (dc_pred[6].type == type) {
        dc += dc_pred[6].dc;
        count++;
    }

    if (count != 2 && dc_pred[-1].type == type) {
        dc += dc_pred[-1].dc;
        count++;
    }

    if (count != 2 && dc_pred[1].type == type) {
        dc += dc_pred[1].dc;
        count++;
    }

    /* using division instead of shift to correctly handle negative values */
    return count == 2 ? dc / 2 : last_dc[type];
}

static void vp4_set_tokens_base(Vp3DecodeContext *s)
{
    int plane, i;
    int16_t *base = s->dct_tokens_base;
    for (plane = 0; plane < 3; plane++) {
        for (i = 0; i < 64; i++) {
            s->dct_tokens[plane][i] = base;
            base += s->fragment_width[!!plane] * s->fragment_height[!!plane];
        }
    }
}

static int vp4_unpack_dct_coeffs(Vp3DecodeContext *s, GetBitContext *gb)
{
    int i, j;
    int dc_y_table;
    int dc_c_table;
    int ac_y_table;
    int ac_c_table;
    VLC *tables[2][64];
    int plane, sb_y, sb_x;
    int eob_tracker[64];
    VP4Predictor dc_pred[6][6];
    int last_dc[NB_VP4_DC_TYPES];

    if (get_bits_left(gb) < 16)
        return AVERROR_INVALIDDATA;

    /* fetch the DC table indexes */
    dc_y_table = get_bits(gb, 4);
    dc_c_table = get_bits(gb, 4);

    ac_y_table = get_bits(gb, 4);
    ac_c_table = get_bits(gb, 4);

    /* build tables of DC/AC VLC tables */

    /* DC table group */
    tables[0][0] = &s->coeff_vlc[dc_y_table];
    tables[1][0] = &s->coeff_vlc[dc_c_table];
    for (i = 1; i <= 5; i++) {
        /* AC VLC table group 1 */
        tables[0][i] = &s->coeff_vlc[ac_y_table + 16];
        tables[1][i] = &s->coeff_vlc[ac_c_table + 16];
    }
    for (i = 6; i <= 14; i++) {
        /* AC VLC table group 2 */
        tables[0][i] = &s->coeff_vlc[ac_y_table + 32];
        tables[1][i] = &s->coeff_vlc[ac_c_table + 32];
    }
    for (i = 15; i <= 27; i++) {
        /* AC VLC table group 3 */
        tables[0][i] = &s->coeff_vlc[ac_y_table + 48];
        tables[1][i] = &s->coeff_vlc[ac_c_table + 48];
    }
    for (i = 28; i <= 63; i++) {
        /* AC VLC table group 4 */
        tables[0][i] = &s->coeff_vlc[ac_y_table + 64];
        tables[1][i] = &s->coeff_vlc[ac_c_table + 64];
    }

    vp4_set_tokens_base(s);

    memset(last_dc, 0, sizeof(last_dc));

    for (plane = 0; plane < ((s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 1 : 3); plane++) {
        memset(eob_tracker, 0, sizeof(eob_tracker));

        /* initialise dc prediction */
        for (i = 0; i < s->fragment_width[!!plane]; i++)
            vp4_dc_predictor_reset(&s->dc_pred_row[i]);

        for (j = 0; j < 6; j++)
            for (i = 0; i < 6; i++)
                vp4_dc_predictor_reset(&dc_pred[j][i]);

        for (sb_y = 0; sb_y * 4 < s->fragment_height[!!plane]; sb_y++) {
            for (sb_x = 0; sb_x *4 < s->fragment_width[!!plane]; sb_x++) {
                vp4_dc_pred_before(s, dc_pred, sb_x);
                for (j = 0; j < 16; j++) {
                        int hx = hilbert_offset[j][0];
                        int hy = hilbert_offset[j][1];
                        int x  = 4 * sb_x + hx;
                        int y  = 4 * sb_y + hy;
                        VP4Predictor *this_dc_pred = &dc_pred[hy + 1][hx + 1];
                        int fragment, dc_block_type;

                        if (x >= s->fragment_width[!!plane] || y >= s->fragment_height[!!plane])
                            continue;

                        fragment = s->fragment_start[plane] + y * s->fragment_width[!!plane] + x;

                        if (s->all_fragments[fragment].coding_method == MODE_COPY)
                            continue;

                        if (vp4_unpack_vlcs(s, gb, tables[!!plane], plane, eob_tracker, fragment) < 0)
                            return -1;

                        dc_block_type = vp4_pred_block_type_map[s->all_fragments[fragment].coding_method];

                        s->all_fragments[fragment].dc +=
                            vp4_dc_pred(s, this_dc_pred, last_dc, dc_block_type, plane);

                        this_dc_pred->type = dc_block_type,
                        this_dc_pred->dc   = last_dc[dc_block_type] = s->all_fragments[fragment].dc;
                }
                vp4_dc_pred_after(s, dc_pred, sb_x);
            }
        }
    }

    vp4_set_tokens_base(s);

    return 0;
}
#endif

/*
 * This function reverses the DC prediction for each coded fragment in
 * the frame. Much of this function is adapted directly from the original
 * VP3 source code.
 */
#define COMPATIBLE_FRAME(x)                                                   \
    (compatible_frame[s->all_fragments[x].coding_method] == current_frame_type)
#define DC_COEFF(u) s->all_fragments[u].dc

static void reverse_dc_prediction(Vp3DecodeContext *s,
                                  int first_fragment,
                                  int fragment_width,
                                  int fragment_height)
{
#define PUL 8
#define PU 4
#define PUR 2
#define PL 1

    int x, y;
    int i = first_fragment;

    int predicted_dc;

    /* DC values for the left, up-left, up, and up-right fragments */
    int vl, vul, vu, vur;

    /* indexes for the left, up-left, up, and up-right fragments */
    int l, ul, u, ur;

    /*
     * The 6 fields mean:
     *   0: up-left multiplier
     *   1: up multiplier
     *   2: up-right multiplier
     *   3: left multiplier
     */
    static const int predictor_transform[16][4] = {
        {    0,   0,   0,   0 },
        {    0,   0,   0, 128 }, // PL
        {    0,   0, 128,   0 }, // PUR
        {    0,   0,  53,  75 }, // PUR|PL
        {    0, 128,   0,   0 }, // PU
        {    0,  64,   0,  64 }, // PU |PL
        {    0, 128,   0,   0 }, // PU |PUR
        {    0,   0,  53,  75 }, // PU |PUR|PL
        {  128,   0,   0,   0 }, // PUL
        {    0,   0,   0, 128 }, // PUL|PL
        {   64,   0,  64,   0 }, // PUL|PUR
        {    0,   0,  53,  75 }, // PUL|PUR|PL
        {    0, 128,   0,   0 }, // PUL|PU
        { -104, 116,   0, 116 }, // PUL|PU |PL
        {   24,  80,  24,   0 }, // PUL|PU |PUR
        { -104, 116,   0, 116 }  // PUL|PU |PUR|PL
    };

    /* This table shows which types of blocks can use other blocks for
     * prediction. For example, INTRA is the only mode in this table to
     * have a frame number of 0. That means INTRA blocks can only predict
     * from other INTRA blocks. There are 2 golden frame coding types;
     * blocks encoding in these modes can only predict from other blocks
     * that were encoded with these 1 of these 2 modes. */
    static const unsigned char compatible_frame[9] = {
        1,    /* MODE_INTER_NO_MV */
        0,    /* MODE_INTRA */
        1,    /* MODE_INTER_PLUS_MV */
        1,    /* MODE_INTER_LAST_MV */
        1,    /* MODE_INTER_PRIOR_MV */
        2,    /* MODE_USING_GOLDEN */
        2,    /* MODE_GOLDEN_MV */
        1,    /* MODE_INTER_FOUR_MV */
        3     /* MODE_COPY */
    };
    int current_frame_type;

    /* there is a last DC predictor for each of the 3 frame types */
    short last_dc[3];

    int transform = 0;

    vul =
    vu  =
    vur =
    vl  = 0;
    last_dc[0] =
    last_dc[1] =
    last_dc[2] = 0;

    /* for each fragment row... */
    for (y = 0; y < fragment_height; y++) {
        /* for each fragment in a row... */
        for (x = 0; x < fragment_width; x++, i++) {

            /* reverse prediction if this block was coded */
            if (s->all_fragments[i].coding_method != MODE_COPY) {
                current_frame_type =
                    compatible_frame[s->all_fragments[i].coding_method];

                transform = 0;
                if (x) {
                    l  = i - 1;
                    vl = DC_COEFF(l);
                    if (COMPATIBLE_FRAME(l))
                        transform |= PL;
                }
                if (y) {
                    u  = i - fragment_width;
                    vu = DC_COEFF(u);
                    if (COMPATIBLE_FRAME(u))
                        transform |= PU;
                    if (x) {
                        ul  = i - fragment_width - 1;
                        vul = DC_COEFF(ul);
                        if (COMPATIBLE_FRAME(ul))
                            transform |= PUL;
                    }
                    if (x + 1 < fragment_width) {
                        ur  = i - fragment_width + 1;
                        vur = DC_COEFF(ur);
                        if (COMPATIBLE_FRAME(ur))
                            transform |= PUR;
                    }
                }

                if (transform == 0) {
                    /* if there were no fragments to predict from, use last
                     * DC saved */
                    predicted_dc = last_dc[current_frame_type];
                } else {
                    /* apply the appropriate predictor transform */
                    predicted_dc =
                        (predictor_transform[transform][0] * vul) +
                        (predictor_transform[transform][1] * vu) +
                        (predictor_transform[transform][2] * vur) +
                        (predictor_transform[transform][3] * vl);

                    predicted_dc /= 128;

                    /* check for outranging on the [ul u l] and
                     * [ul u ur l] predictors */
                    if ((transform == 15) || (transform == 13)) {
                        if (FFABS(predicted_dc - vu) > 128)
                            predicted_dc = vu;
                        else if (FFABS(predicted_dc - vl) > 128)
                            predicted_dc = vl;
                        else if (FFABS(predicted_dc - vul) > 128)
                            predicted_dc = vul;
                    }
                }

                /* at long last, apply the predictor */
                DC_COEFF(i) += predicted_dc;
                /* save the DC */
                last_dc[current_frame_type] = DC_COEFF(i);
            }
        }
    }
}

static void apply_loop_filter(Vp3DecodeContext *s, int plane,
                              int ystart, int yend)
{
    int x, y;
    int *bounding_values = s->bounding_values_array + 127;

    int width           = s->fragment_width[!!plane];
    int height          = s->fragment_height[!!plane];
    int fragment        = s->fragment_start[plane] + ystart * width;
    ptrdiff_t stride    = s->current_frame.f->linesize[plane];
    uint8_t *plane_data = s->current_frame.f->data[plane];
    if (!s->flipped_image)
        stride = -stride;
    plane_data += s->data_offset[plane] + 8 * ystart * stride;

    for (y = ystart; y < yend; y++) {
        for (x = 0; x < width; x++) {
            /* This code basically just deblocks on the edges of coded blocks.
             * However, it has to be much more complicated because of the
             * brain damaged deblock ordering used in VP3/Theora. Order matters
             * because some pixels get filtered twice. */
            if (s->all_fragments[fragment].coding_method != MODE_COPY) {
                /* do not perform left edge filter for left columns frags */
                if (x > 0) {
                    s->vp3dsp.h_loop_filter(
                        plane_data + 8 * x,
                        stride, bounding_values);
                }

                /* do not perform top edge filter for top row fragments */
                if (y > 0) {
                    s->vp3dsp.v_loop_filter(
                        plane_data + 8 * x,
                        stride, bounding_values);
                }

                /* do not perform right edge filter for right column
                 * fragments or if right fragment neighbor is also coded
                 * in this frame (it will be filtered in next iteration) */
                if ((x < width - 1) &&
                    (s->all_fragments[fragment + 1].coding_method == MODE_COPY)) {
                    s->vp3dsp.h_loop_filter(
                        plane_data + 8 * x + 8,
                        stride, bounding_values);
                }

                /* do not perform bottom edge filter for bottom row
                 * fragments or if bottom fragment neighbor is also coded
                 * in this frame (it will be filtered in the next row) */
                if ((y < height - 1) &&
                    (s->all_fragments[fragment + width].coding_method == MODE_COPY)) {
                    s->vp3dsp.v_loop_filter(
                        plane_data + 8 * x + 8 * stride,
                        stride, bounding_values);
                }
            }

            fragment++;
        }
        plane_data += 8 * stride;
    }
}

/**
 * Pull DCT tokens from the 64 levels to decode and dequant the coefficients
 * for the next block in coding order
 */
static inline int vp3_dequant(Vp3DecodeContext *s, Vp3Fragment *frag,
                              int plane, int inter, int16_t block[64])
{
    int16_t *dequantizer = s->qmat[frag->qpi][inter][plane];
    uint8_t *perm = s->idct_scantable;
    int i = 0;

    do {
        int token = *s->dct_tokens[plane][i];
        switch (token & 3) {
        case 0: // EOB
            if (--token < 4) // 0-3 are token types so the EOB run must now be 0
                s->dct_tokens[plane][i]++;
            else
                *s->dct_tokens[plane][i] = token & ~3;
            goto end;
        case 1: // zero run
            s->dct_tokens[plane][i]++;
            i += (token >> 2) & 0x7f;
            if (i > 63) {
                av_log(s->avctx, AV_LOG_ERROR, "Coefficient index overflow\n");
                return i;
            }
            block[perm[i]] = (token >> 9) * dequantizer[perm[i]];
            i++;
            break;
        case 2: // coeff
            block[perm[i]] = (token >> 2) * dequantizer[perm[i]];
            s->dct_tokens[plane][i++]++;
            break;
        default: // shouldn't happen
            return i;
        }
    } while (i < 64);
    // return value is expected to be a valid level
    i--;
end:
    // the actual DC+prediction is in the fragment structure
    block[0] = frag->dc * s->qmat[0][inter][plane][0];
    return i;
}

/**
 * called when all pixels up to row y are complete
 */
static void vp3_draw_horiz_band(Vp3DecodeContext *s, int y)
{
    int h, cy, i;
    int offset[AV_NUM_DATA_POINTERS];

    if (HAVE_THREADS && s->avctx->active_thread_type & FF_THREAD_FRAME) {
        int y_flipped = s->flipped_image ? s->height - y : y;

        /* At the end of the frame, report INT_MAX instead of the height of
         * the frame. This makes the other threads' ff_thread_await_progress()
         * calls cheaper, because they don't have to clip their values. */
        ff_thread_report_progress(&s->current_frame,
                                  y_flipped == s->height ? INT_MAX
                                                         : y_flipped - 1,
                                  0);
    }

    if (!s->avctx->draw_horiz_band)
        return;

    h = y - s->last_slice_end;
    s->last_slice_end = y;
    y -= h;

    if (!s->flipped_image)
        y = s->height - y - h;

    cy        = y >> s->chroma_y_shift;
    offset[0] = s->current_frame.f->linesize[0] * y;
    offset[1] = s->current_frame.f->linesize[1] * cy;
    offset[2] = s->current_frame.f->linesize[2] * cy;
    for (i = 3; i < AV_NUM_DATA_POINTERS; i++)
        offset[i] = 0;

    emms_c();
    s->avctx->draw_horiz_band(s->avctx, s->current_frame.f, offset, y, 3, h);
}

/**
 * Wait for the reference frame of the current fragment.
 * The progress value is in luma pixel rows.
 */
static void await_reference_row(Vp3DecodeContext *s, Vp3Fragment *fragment,
                                int motion_y, int y)
{
    ThreadFrame *ref_frame;
    int ref_row;
    int border = motion_y & 1;

    if (fragment->coding_method == MODE_USING_GOLDEN ||
        fragment->coding_method == MODE_GOLDEN_MV)
        ref_frame = &s->golden_frame;
    else
        ref_frame = &s->last_frame;

    ref_row = y + (motion_y >> 1);
    ref_row = FFMAX(FFABS(ref_row), ref_row + 8 + border);

    ff_thread_await_progress(ref_frame, ref_row, 0);
}

#if CONFIG_VP4_DECODER
/**
 * @return non-zero if temp (edge_emu_buffer) was populated
 */
static int vp4_mc_loop_filter(Vp3DecodeContext *s, int plane, int motion_x, int motion_y, int bx, int by,
       uint8_t * motion_source, int stride, int src_x, int src_y, uint8_t *temp)
{
    int motion_shift = plane ? 4 : 2;
    int subpel_mask = plane ? 3 : 1;
    int *bounding_values = s->bounding_values_array + 127;

    int i;
    int x, y;
    int x2, y2;
    int x_subpel, y_subpel;
    int x_offset, y_offset;

    int block_width = plane ? 8 : 16;
    int plane_width  = s->width  >> (plane && s->chroma_x_shift);
    int plane_height = s->height >> (plane && s->chroma_y_shift);

#define loop_stride 12
    uint8_t loop[12 * loop_stride];

    /* using division instead of shift to correctly handle negative values */
    x = 8 * bx + motion_x / motion_shift;
    y = 8 * by + motion_y / motion_shift;

    x_subpel = motion_x & subpel_mask;
    y_subpel = motion_y & subpel_mask;

    if (x_subpel || y_subpel) {
        x--;
        y--;

        if (x_subpel)
            x = FFMIN(x, x + FFSIGN(motion_x));

        if (y_subpel)
            y = FFMIN(y, y + FFSIGN(motion_y));

        x2 = x + block_width;
        y2 = y + block_width;

        if (x2 < 0 || x2 >= plane_width || y2 < 0 || y2 >= plane_height)
            return 0;

        x_offset = (-(x + 2) & 7) + 2;
        y_offset = (-(y + 2) & 7) + 2;

        if (x_offset > 8 + x_subpel && y_offset > 8 + y_subpel)
            return 0;

        s->vdsp.emulated_edge_mc(loop, motion_source - stride - 1,
             loop_stride, stride,
             12, 12, src_x - 1, src_y - 1,
             plane_width,
             plane_height);

        if (x_offset <= 8 + x_subpel)
            ff_vp3dsp_h_loop_filter_12(loop + x_offset, loop_stride, bounding_values);

        if (y_offset <= 8 + y_subpel)
            ff_vp3dsp_v_loop_filter_12(loop + y_offset*loop_stride, loop_stride, bounding_values);

    } else {

        x_offset = -x & 7;
        y_offset = -y & 7;

        if (!x_offset && !y_offset)
            return 0;

        s->vdsp.emulated_edge_mc(loop, motion_source - stride - 1,
             loop_stride, stride,
             12, 12, src_x - 1, src_y - 1,
             plane_width,
             plane_height);

#define safe_loop_filter(name, ptr, stride, bounding_values) \
    if ((uintptr_t)(ptr) & 7) \
        s->vp3dsp.name##_unaligned(ptr, stride, bounding_values); \
    else \
        s->vp3dsp.name(ptr, stride, bounding_values);

        if (x_offset)
            safe_loop_filter(h_loop_filter, loop + loop_stride + x_offset + 1, loop_stride, bounding_values);

        if (y_offset)
            safe_loop_filter(v_loop_filter, loop + (y_offset + 1)*loop_stride + 1, loop_stride, bounding_values);
    }

    for (i = 0; i < 9; i++)
        memcpy(temp + i*stride, loop + (i + 1) * loop_stride + 1, 9);

    return 1;
}
#endif

/*
 * Perform the final rendering for a particular slice of data.
 * The slice number ranges from 0..(c_superblock_height - 1).
 */
static void render_slice(Vp3DecodeContext *s, int slice)
{
    int x, y, i, j, fragment;
    int16_t *block = s->block;
    int motion_x = 0xdeadbeef, motion_y = 0xdeadbeef;
    int motion_halfpel_index;
    uint8_t *motion_source;
    int plane, first_pixel;

    if (slice >= s->c_superblock_height)
        return;

    for (plane = 0; plane < 3; plane++) {
        uint8_t *output_plane = s->current_frame.f->data[plane] +
                                s->data_offset[plane];
        uint8_t *last_plane = s->last_frame.f->data[plane] +
                              s->data_offset[plane];
        uint8_t *golden_plane = s->golden_frame.f->data[plane] +
                                s->data_offset[plane];
        ptrdiff_t stride = s->current_frame.f->linesize[plane];
        int plane_width  = s->width  >> (plane && s->chroma_x_shift);
        int plane_height = s->height >> (plane && s->chroma_y_shift);
        int8_t(*motion_val)[2] = s->motion_val[!!plane];

        int sb_x, sb_y = slice << (!plane && s->chroma_y_shift);
        int slice_height = sb_y + 1 + (!plane && s->chroma_y_shift);
        int slice_width  = plane ? s->c_superblock_width
                                 : s->y_superblock_width;

        int fragment_width  = s->fragment_width[!!plane];
        int fragment_height = s->fragment_height[!!plane];
        int fragment_start  = s->fragment_start[plane];

        int do_await = !plane && HAVE_THREADS &&
                       (s->avctx->active_thread_type & FF_THREAD_FRAME);

        if (!s->flipped_image)
            stride = -stride;
        if (CONFIG_GRAY && plane && (s->avctx->flags & AV_CODEC_FLAG_GRAY))
            continue;

        /* for each superblock row in the slice (both of them)... */
        for (; sb_y < slice_height; sb_y++) {
            /* for each superblock in a row... */
            for (sb_x = 0; sb_x < slice_width; sb_x++) {
                /* for each block in a superblock... */
                for (j = 0; j < 16; j++) {
                    x        = 4 * sb_x + hilbert_offset[j][0];
                    y        = 4 * sb_y + hilbert_offset[j][1];
                    fragment = y * fragment_width + x;

                    i = fragment_start + fragment;

                    // bounds check
                    if (x >= fragment_width || y >= fragment_height)
                        continue;

                    first_pixel = 8 * y * stride + 8 * x;

                    if (do_await &&
                        s->all_fragments[i].coding_method != MODE_INTRA)
                        await_reference_row(s, &s->all_fragments[i],
                                            motion_val[fragment][1],
                                            (16 * y) >> s->chroma_y_shift);

                    /* transform if this block was coded */
                    if (s->all_fragments[i].coding_method != MODE_COPY) {
                        if ((s->all_fragments[i].coding_method == MODE_USING_GOLDEN) ||
                            (s->all_fragments[i].coding_method == MODE_GOLDEN_MV))
                            motion_source = golden_plane;
                        else
                            motion_source = last_plane;

                        motion_source       += first_pixel;
                        motion_halfpel_index = 0;

                        /* sort out the motion vector if this fragment is coded
                         * using a motion vector method */
                        if ((s->all_fragments[i].coding_method > MODE_INTRA) &&
                            (s->all_fragments[i].coding_method != MODE_USING_GOLDEN)) {
                            int src_x, src_y;
                            int standard_mc = 1;
                            motion_x = motion_val[fragment][0];
                            motion_y = motion_val[fragment][1];
#if CONFIG_VP4_DECODER
                            if (plane && s->version >= 2) {
                                motion_x = (motion_x >> 1) | (motion_x & 1);
                                motion_y = (motion_y >> 1) | (motion_y & 1);
                            }
#endif

                            src_x = (motion_x >> 1) + 8 * x;
                            src_y = (motion_y >> 1) + 8 * y;

                            motion_halfpel_index = motion_x & 0x01;
                            motion_source       += (motion_x >> 1);

                            motion_halfpel_index |= (motion_y & 0x01) << 1;
                            motion_source        += ((motion_y >> 1) * stride);

#if CONFIG_VP4_DECODER
                            if (s->version >= 2) {
                                uint8_t *temp = s->edge_emu_buffer;
                                if (stride < 0)
                                    temp -= 8 * stride;
                                if (vp4_mc_loop_filter(s, plane, motion_val[fragment][0], motion_val[fragment][1], x, y, motion_source, stride, src_x, src_y, temp)) {
                                    motion_source = temp;
                                    standard_mc = 0;
                                }
                            }
#endif

                            if (standard_mc && (
                                src_x < 0 || src_y < 0 ||
                                src_x + 9 >= plane_width ||
                                src_y + 9 >= plane_height)) {
                                uint8_t *temp = s->edge_emu_buffer;
                                if (stride < 0)
                                    temp -= 8 * stride;

                                s->vdsp.emulated_edge_mc(temp, motion_source,
                                                         stride, stride,
                                                         9, 9, src_x, src_y,
                                                         plane_width,
                                                         plane_height);
                                motion_source = temp;
                            }
                        }

                        /* first, take care of copying a block from either the
                         * previous or the golden frame */
                        if (s->all_fragments[i].coding_method != MODE_INTRA) {
                            /* Note, it is possible to implement all MC cases
                             * with put_no_rnd_pixels_l2 which would look more
                             * like the VP3 source but this would be slower as
                             * put_no_rnd_pixels_tab is better optimized */
                            if (motion_halfpel_index != 3) {
                                s->hdsp.put_no_rnd_pixels_tab[1][motion_halfpel_index](
                                    output_plane + first_pixel,
                                    motion_source, stride, 8);
                            } else {
                                /* d is 0 if motion_x and _y have the same sign,
                                 * else -1 */
                                int d = (motion_x ^ motion_y) >> 31;
                                s->vp3dsp.put_no_rnd_pixels_l2(output_plane + first_pixel,
                                                               motion_source - d,
                                                               motion_source + stride + 1 + d,
                                                               stride, 8);
                            }
                        }

                        /* invert DCT and place (or add) in final output */

                        if (s->all_fragments[i].coding_method == MODE_INTRA) {
                            vp3_dequant(s, s->all_fragments + i,
                                        plane, 0, block);
                            s->vp3dsp.idct_put(output_plane + first_pixel,
                                               stride,
                                               block);
                        } else {
                            if (vp3_dequant(s, s->all_fragments + i,
                                            plane, 1, block)) {
                                s->vp3dsp.idct_add(output_plane + first_pixel,
                                                   stride,
                                                   block);
                            } else {
                                s->vp3dsp.idct_dc_add(output_plane + first_pixel,
                                                      stride, block);
                            }
                        }
                    } else {
                        /* copy directly from the previous frame */
                        s->hdsp.put_pixels_tab[1][0](
                            output_plane + first_pixel,
                            last_plane + first_pixel,
                            stride, 8);
                    }
                }
            }

            // Filter up to the last row in the superblock row
            if (s->version < 2 && !s->skip_loop_filter)
                apply_loop_filter(s, plane, 4 * sb_y - !!sb_y,
                                  FFMIN(4 * sb_y + 3, fragment_height - 1));
        }
    }

    /* this looks like a good place for slice dispatch... */
    /* algorithm:
     *   if (slice == s->macroblock_height - 1)
     *     dispatch (both last slice & 2nd-to-last slice);
     *   else if (slice > 0)
     *     dispatch (slice - 1);
     */

    vp3_draw_horiz_band(s, FFMIN((32 << s->chroma_y_shift) * (slice + 1) - 16,
                                 s->height - 16));
}

/// Allocate tables for per-frame data in Vp3DecodeContext
static av_cold int allocate_tables(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int y_fragment_count, c_fragment_count;

    free_tables(avctx);

    y_fragment_count = s->fragment_width[0] * s->fragment_height[0];
    c_fragment_count = s->fragment_width[1] * s->fragment_height[1];

    /* superblock_coding is used by unpack_superblocks (VP3/Theora) and vp4_unpack_macroblocks (VP4) */
    s->superblock_coding = av_mallocz(FFMAX(s->superblock_count, s->yuv_macroblock_count));
    s->all_fragments     = av_mallocz_array(s->fragment_count, sizeof(Vp3Fragment));

    s-> kf_coded_fragment_list = av_mallocz_array(s->fragment_count, sizeof(int));
    s->nkf_coded_fragment_list = av_mallocz_array(s->fragment_count, sizeof(int));
    memset(s-> num_kf_coded_fragment, -1, sizeof(s-> num_kf_coded_fragment));

    s->dct_tokens_base = av_mallocz_array(s->fragment_count,
                                          64 * sizeof(*s->dct_tokens_base));
    s->motion_val[0] = av_mallocz_array(y_fragment_count, sizeof(*s->motion_val[0]));
    s->motion_val[1] = av_mallocz_array(c_fragment_count, sizeof(*s->motion_val[1]));

    /* work out the block mapping tables */
    s->superblock_fragments = av_mallocz_array(s->superblock_count, 16 * sizeof(int));
    s->macroblock_coding    = av_mallocz(s->macroblock_count + 1);

    s->dc_pred_row = av_malloc_array(s->y_superblock_width * 4, sizeof(*s->dc_pred_row));

    if (!s->superblock_coding    || !s->all_fragments          ||
        !s->dct_tokens_base      || !s->kf_coded_fragment_list ||
        !s->nkf_coded_fragment_list ||
        !s->superblock_fragments || !s->macroblock_coding      ||
        !s->dc_pred_row ||
        !s->motion_val[0]        || !s->motion_val[1]) {
        return -1;
    }

    init_block_mapping(s);

    return 0;
}

static av_cold int init_frames(Vp3DecodeContext *s)
{
    s->current_frame.f = av_frame_alloc();
    s->last_frame.f    = av_frame_alloc();
    s->golden_frame.f  = av_frame_alloc();

    if (!s->current_frame.f || !s->last_frame.f || !s->golden_frame.f)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int theora_init_huffman_tables(VLC *vlc, const HuffTable *huff)
{
    uint32_t code = 0, codes[32];

    for (unsigned i = 0; i < huff->nb_entries; i++) {
        codes[i] = code        >> (31 - huff->entries[i].len);
        code    += 0x80000000U >> huff->entries[i].len;
    }
    return ff_init_vlc_sparse(vlc, 11, huff->nb_entries,
                              &huff->entries[0].len, sizeof(huff->entries[0]), 1,
                              codes, 4, 4,
                              &huff->entries[0].sym, sizeof(huff->entries[0]), 1, 0);
}

static av_cold int vp3_decode_init(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int i, inter, plane, ret;
    int c_width;
    int c_height;
    int y_fragment_count, c_fragment_count;
#if CONFIG_VP4_DECODER
    int j;
#endif

    ret = init_frames(s);
    if (ret < 0)
        return ret;

    if (avctx->codec_tag == MKTAG('V', 'P', '4', '0'))
        s->version = 3;
    else if (avctx->codec_tag == MKTAG('V', 'P', '3', '0'))
        s->version = 0;
    else
        s->version = 1;

    s->avctx  = avctx;
    s->width  = FFALIGN(avctx->coded_width, 16);
    s->height = FFALIGN(avctx->coded_height, 16);
    if (avctx->codec_id != AV_CODEC_ID_THEORA)
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avctx->chroma_sample_location = AVCHROMA_LOC_CENTER;
    ff_hpeldsp_init(&s->hdsp, avctx->flags | AV_CODEC_FLAG_BITEXACT);
    ff_videodsp_init(&s->vdsp, 8);
    ff_vp3dsp_init(&s->vp3dsp, avctx->flags);

    for (i = 0; i < 64; i++) {
#define TRANSPOSE(x) (((x) >> 3) | (((x) & 7) << 3))
        s->idct_permutation[i] = TRANSPOSE(i);
        s->idct_scantable[i]   = TRANSPOSE(ff_zigzag_direct[i]);
#undef TRANSPOSE
    }

    /* initialize to an impossible value which will force a recalculation
     * in the first frame decode */
    for (i = 0; i < 3; i++)
        s->qps[i] = -1;

    ret = av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_x_shift, &s->chroma_y_shift);
    if (ret)
        return ret;

    s->y_superblock_width  = (s->width  + 31) / 32;
    s->y_superblock_height = (s->height + 31) / 32;
    s->y_superblock_count  = s->y_superblock_width * s->y_superblock_height;

    /* work out the dimensions for the C planes */
    c_width                = s->width >> s->chroma_x_shift;
    c_height               = s->height >> s->chroma_y_shift;
    s->c_superblock_width  = (c_width  + 31) / 32;
    s->c_superblock_height = (c_height + 31) / 32;
    s->c_superblock_count  = s->c_superblock_width * s->c_superblock_height;

    s->superblock_count   = s->y_superblock_count + (s->c_superblock_count * 2);
    s->u_superblock_start = s->y_superblock_count;
    s->v_superblock_start = s->u_superblock_start + s->c_superblock_count;

    s->macroblock_width  = (s->width  + 15) / 16;
    s->macroblock_height = (s->height + 15) / 16;
    s->macroblock_count  = s->macroblock_width * s->macroblock_height;
    s->c_macroblock_width  = (c_width  + 15) / 16;
    s->c_macroblock_height = (c_height + 15) / 16;
    s->c_macroblock_count  = s->c_macroblock_width * s->c_macroblock_height;
    s->yuv_macroblock_count = s->macroblock_count + 2 * s->c_macroblock_count;

    s->fragment_width[0]  = s->width / FRAGMENT_PIXELS;
    s->fragment_height[0] = s->height / FRAGMENT_PIXELS;
    s->fragment_width[1]  = s->fragment_width[0] >> s->chroma_x_shift;
    s->fragment_height[1] = s->fragment_height[0] >> s->chroma_y_shift;

    /* fragment count covers all 8x8 blocks for all 3 planes */
    y_fragment_count     = s->fragment_width[0] * s->fragment_height[0];
    c_fragment_count     = s->fragment_width[1] * s->fragment_height[1];
    s->fragment_count    = y_fragment_count + 2 * c_fragment_count;
    s->fragment_start[1] = y_fragment_count;
    s->fragment_start[2] = y_fragment_count + c_fragment_count;

    if (!s->theora_tables) {
        for (i = 0; i < 64; i++) {
            s->coded_dc_scale_factor[0][i] = s->version < 2 ? vp31_dc_scale_factor[i] : vp4_y_dc_scale_factor[i];
            s->coded_dc_scale_factor[1][i] = s->version < 2 ? vp31_dc_scale_factor[i] : vp4_uv_dc_scale_factor[i];
            s->coded_ac_scale_factor[i] = s->version < 2 ? vp31_ac_scale_factor[i] : vp4_ac_scale_factor[i];
            s->base_matrix[0][i]        = s->version < 2 ? vp31_intra_y_dequant[i] : vp4_generic_dequant[i];
            s->base_matrix[1][i]        = s->version < 2 ? vp31_intra_c_dequant[i] : vp4_generic_dequant[i];
            s->base_matrix[2][i]        = s->version < 2 ? vp31_inter_dequant[i]   : vp4_generic_dequant[i];
            s->filter_limit_values[i]   = s->version < 2 ? vp31_filter_limit_values[i] : vp4_filter_limit_values[i];
        }

        for (inter = 0; inter < 2; inter++) {
            for (plane = 0; plane < 3; plane++) {
                s->qr_count[inter][plane]   = 1;
                s->qr_size[inter][plane][0] = 63;
                s->qr_base[inter][plane][0] =
                s->qr_base[inter][plane][1] = 2 * inter + (!!plane) * !inter;
            }
        }

        /* init VLC tables */
        if (s->version < 2) {
            for (i = 0; i < FF_ARRAY_ELEMS(s->coeff_vlc); i++) {
                if ((ret = init_vlc(&s->coeff_vlc[i], 11, 32,
                                    &vp3_bias[i][0][1], 4, 2,
                                    &vp3_bias[i][0][0], 4, 2, 0)) < 0)
                    return ret;
            }
#if CONFIG_VP4_DECODER
        } else { /* version >= 2 */
            for (i = 0; i < FF_ARRAY_ELEMS(s->coeff_vlc); i++) {
                if ((ret = init_vlc(&s->coeff_vlc[i], 11, 32,
                                    &vp4_bias[i][0][1], 4, 2,
                                    &vp4_bias[i][0][0], 4, 2, 0)) < 0)
                    return ret;
            }
#endif
        }
    } else {
        for (i = 0; i < FF_ARRAY_ELEMS(s->coeff_vlc); i++) {
            ret = theora_init_huffman_tables(&s->coeff_vlc[i], &s->huffman_table[i]);
            if (ret < 0)
                return ret;
        }
    }

    if ((ret = init_vlc(&s->superblock_run_length_vlc, 6, 34,
                        &superblock_run_length_vlc_table[0][1], 4, 2,
                        &superblock_run_length_vlc_table[0][0], 4, 2, 0)) < 0)
        return ret;

    if ((ret = init_vlc(&s->fragment_run_length_vlc, 5, 30,
                        &fragment_run_length_vlc_table[0][1], 4, 2,
                        &fragment_run_length_vlc_table[0][0], 4, 2, 0)) < 0)
        return ret;

    if ((ret = init_vlc(&s->mode_code_vlc, 3, 8,
                        &mode_code_vlc_table[0][1], 2, 1,
                        &mode_code_vlc_table[0][0], 2, 1, 0)) < 0)
        return ret;

    if ((ret = init_vlc(&s->motion_vector_vlc, 6, 63,
                        &motion_vector_vlc_table[0][1], 2, 1,
                        &motion_vector_vlc_table[0][0], 2, 1, 0)) < 0)
        return ret;

#if CONFIG_VP4_DECODER
    for (j = 0; j < 2; j++)
        for (i = 0; i < 7; i++)
            if ((ret = init_vlc(&s->vp4_mv_vlc[j][i], 6, 63,
                                &vp4_mv_vlc[j][i][0][1], 4, 2,
                                &vp4_mv_vlc[j][i][0][0], 4, 2, 0)) < 0)
                return ret;

    /* version >= 2 */
    for (i = 0; i < 2; i++)
        if ((ret = init_vlc(&s->block_pattern_vlc[i], 3, 14,
                            &vp4_block_pattern_vlc[i][0][1], 2, 1,
                            &vp4_block_pattern_vlc[i][0][0], 2, 1, 0)) < 0)
            return ret;
#endif

    return allocate_tables(avctx);
}

/// Release and shuffle frames after decode finishes
static int update_frames(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int ret = 0;

    /* shuffle frames (last = current) */
    ff_thread_release_buffer(avctx, &s->last_frame);
    ret = ff_thread_ref_frame(&s->last_frame, &s->current_frame);
    if (ret < 0)
        goto fail;

    if (s->keyframe) {
        ff_thread_release_buffer(avctx, &s->golden_frame);
        ret = ff_thread_ref_frame(&s->golden_frame, &s->current_frame);
    }

fail:
    ff_thread_release_buffer(avctx, &s->current_frame);
    return ret;
}

#if HAVE_THREADS
static int ref_frame(Vp3DecodeContext *s, ThreadFrame *dst, ThreadFrame *src)
{
    ff_thread_release_buffer(s->avctx, dst);
    if (src->f->data[0])
        return ff_thread_ref_frame(dst, src);
    return 0;
}

static int ref_frames(Vp3DecodeContext *dst, Vp3DecodeContext *src)
{
    int ret;
    if ((ret = ref_frame(dst, &dst->current_frame, &src->current_frame)) < 0 ||
        (ret = ref_frame(dst, &dst->golden_frame,  &src->golden_frame)) < 0  ||
        (ret = ref_frame(dst, &dst->last_frame,    &src->last_frame)) < 0)
        return ret;
    return 0;
}

static int vp3_update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    Vp3DecodeContext *s = dst->priv_data, *s1 = src->priv_data;
    int qps_changed = 0, i, err;

    if (!s1->current_frame.f->data[0] ||
        s->width != s1->width || s->height != s1->height) {
        if (s != s1)
            ref_frames(s, s1);
        return -1;
    }

    if (s != s1) {
        // copy previous frame data
        if ((err = ref_frames(s, s1)) < 0)
            return err;

        s->keyframe = s1->keyframe;

        // copy qscale data if necessary
        for (i = 0; i < 3; i++) {
            if (s->qps[i] != s1->qps[1]) {
                qps_changed = 1;
                memcpy(&s->qmat[i], &s1->qmat[i], sizeof(s->qmat[i]));
            }
        }

        if (s->qps[0] != s1->qps[0])
            memcpy(&s->bounding_values_array, &s1->bounding_values_array,
                   sizeof(s->bounding_values_array));

        if (qps_changed) {
            memcpy(s->qps,      s1->qps,      sizeof(s->qps));
            memcpy(s->last_qps, s1->last_qps, sizeof(s->last_qps));
            s->nqps = s1->nqps;
        }
    }

    return update_frames(dst);
}
#endif

static int vp3_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    AVFrame     *frame  = data;
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    Vp3DecodeContext *s = avctx->priv_data;
    GetBitContext gb;
    int i, ret;

    if ((ret = init_get_bits8(&gb, buf, buf_size)) < 0)
        return ret;

#if CONFIG_THEORA_DECODER
    if (s->theora && get_bits1(&gb)) {
        int type = get_bits(&gb, 7);
        skip_bits_long(&gb, 6*8); /* "theora" */

        if (s->avctx->active_thread_type&FF_THREAD_FRAME) {
            av_log(avctx, AV_LOG_ERROR, "midstream reconfiguration with multithreading is unsupported, try -threads 1\n");
            return AVERROR_PATCHWELCOME;
        }
        if (type == 0) {
            vp3_decode_end(avctx);
            ret = theora_decode_header(avctx, &gb);

            if (ret >= 0)
                ret = vp3_decode_init(avctx);
            if (ret < 0) {
                vp3_decode_end(avctx);
                return ret;
            }
            return buf_size;
        } else if (type == 2) {
            vp3_decode_end(avctx);
            ret = theora_decode_tables(avctx, &gb);
            if (ret >= 0)
                ret = vp3_decode_init(avctx);
            if (ret < 0) {
                vp3_decode_end(avctx);
                return ret;
            }
            return buf_size;
        }

        av_log(avctx, AV_LOG_ERROR,
               "Header packet passed to frame decoder, skipping\n");
        return -1;
    }
#endif

    s->keyframe = !get_bits1(&gb);
    if (!s->all_fragments) {
        av_log(avctx, AV_LOG_ERROR, "Data packet without prior valid headers\n");
        return -1;
    }
    if (!s->theora)
        skip_bits(&gb, 1);
    for (i = 0; i < 3; i++)
        s->last_qps[i] = s->qps[i];

    s->nqps = 0;
    do {
        s->qps[s->nqps++] = get_bits(&gb, 6);
    } while (s->theora >= 0x030200 && s->nqps < 3 && get_bits1(&gb));
    for (i = s->nqps; i < 3; i++)
        s->qps[i] = -1;

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_INFO, " VP3 %sframe #%d: Q index = %d\n",
               s->keyframe ? "key" : "", avctx->frame_number + 1, s->qps[0]);

    s->skip_loop_filter = !s->filter_limit_values[s->qps[0]] ||
                          avctx->skip_loop_filter >= (s->keyframe ? AVDISCARD_ALL
                                                                  : AVDISCARD_NONKEY);

    if (s->qps[0] != s->last_qps[0])
        init_loop_filter(s);

    for (i = 0; i < s->nqps; i++)
        // reinit all dequantizers if the first one changed, because
        // the DC of the first quantizer must be used for all matrices
        if (s->qps[i] != s->last_qps[i] || s->qps[0] != s->last_qps[0])
            init_dequantizer(s, i);

    if (avctx->skip_frame >= AVDISCARD_NONKEY && !s->keyframe)
        return buf_size;

    s->current_frame.f->pict_type = s->keyframe ? AV_PICTURE_TYPE_I
                                                : AV_PICTURE_TYPE_P;
    s->current_frame.f->key_frame = s->keyframe;
    if ((ret = ff_thread_get_buffer(avctx, &s->current_frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        goto error;

    if (!s->edge_emu_buffer)
        s->edge_emu_buffer = av_malloc(9 * FFABS(s->current_frame.f->linesize[0]));

    if (s->keyframe) {
        if (!s->theora) {
            skip_bits(&gb, 4); /* width code */
            skip_bits(&gb, 4); /* height code */
            if (s->version) {
                s->version = get_bits(&gb, 5);
                if (avctx->frame_number == 0)
                    av_log(s->avctx, AV_LOG_DEBUG,
                           "VP version: %d\n", s->version);
            }
        }
        if (s->version || s->theora) {
            if (get_bits1(&gb))
                av_log(s->avctx, AV_LOG_ERROR,
                       "Warning, unsupported keyframe coding type?!\n");
            skip_bits(&gb, 2); /* reserved? */

#if CONFIG_VP4_DECODER
            if (s->version >= 2) {
                int mb_height, mb_width;
                int mb_width_mul, mb_width_div, mb_height_mul, mb_height_div;

                mb_height = get_bits(&gb, 8);
                mb_width  = get_bits(&gb, 8);
                if (mb_height != s->macroblock_height ||
                    mb_width != s->macroblock_width)
                    avpriv_request_sample(s->avctx, "macroblock dimension mismatch");

                mb_width_mul = get_bits(&gb, 5);
                mb_width_div = get_bits(&gb, 3);
                mb_height_mul = get_bits(&gb, 5);
                mb_height_div = get_bits(&gb, 3);
                if (mb_width_mul != 1 || mb_width_div != 1 || mb_height_mul != 1 || mb_height_div != 1)
                    avpriv_request_sample(s->avctx, "unexpected macroblock dimension multipler/divider");

                if (get_bits(&gb, 2))
                    avpriv_request_sample(s->avctx, "unknown bits");
            }
#endif
        }
    } else {
        if (!s->golden_frame.f->data[0]) {
            av_log(s->avctx, AV_LOG_WARNING,
                   "vp3: first frame not a keyframe\n");

            s->golden_frame.f->pict_type = AV_PICTURE_TYPE_I;
            if ((ret = ff_thread_get_buffer(avctx, &s->golden_frame,
                                     AV_GET_BUFFER_FLAG_REF)) < 0)
                goto error;
            ff_thread_release_buffer(avctx, &s->last_frame);
            if ((ret = ff_thread_ref_frame(&s->last_frame,
                                           &s->golden_frame)) < 0)
                goto error;
            ff_thread_report_progress(&s->last_frame, INT_MAX, 0);
        }
    }

    memset(s->all_fragments, 0, s->fragment_count * sizeof(Vp3Fragment));
    ff_thread_finish_setup(avctx);

    if (s->version < 2) {
        if ((ret = unpack_superblocks(s, &gb)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "error in unpack_superblocks\n");
            goto error;
        }
#if CONFIG_VP4_DECODER
    } else {
        if ((ret = vp4_unpack_macroblocks(s, &gb)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "error in vp4_unpack_macroblocks\n");
            goto error;
    }
#endif
    }
    if ((ret = unpack_modes(s, &gb)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "error in unpack_modes\n");
        goto error;
    }
    if (ret = unpack_vectors(s, &gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "error in unpack_vectors\n");
        goto error;
    }
    if ((ret = unpack_block_qpis(s, &gb)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "error in unpack_block_qpis\n");
        goto error;
    }

    if (s->version < 2) {
        if ((ret = unpack_dct_coeffs(s, &gb)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "error in unpack_dct_coeffs\n");
            goto error;
        }
#if CONFIG_VP4_DECODER
    } else {
        if ((ret = vp4_unpack_dct_coeffs(s, &gb)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "error in vp4_unpack_dct_coeffs\n");
            goto error;
        }
#endif
    }

    for (i = 0; i < 3; i++) {
        int height = s->height >> (i && s->chroma_y_shift);
        if (s->flipped_image)
            s->data_offset[i] = 0;
        else
            s->data_offset[i] = (height - 1) * s->current_frame.f->linesize[i];
    }

    s->last_slice_end = 0;
    for (i = 0; i < s->c_superblock_height; i++)
        render_slice(s, i);

    // filter the last row
    if (s->version < 2)
        for (i = 0; i < 3; i++) {
            int row = (s->height >> (3 + (i && s->chroma_y_shift))) - 1;
            apply_loop_filter(s, i, row, row + 1);
        }
    vp3_draw_horiz_band(s, s->height);

    /* output frame, offset as needed */
    if ((ret = av_frame_ref(data, s->current_frame.f)) < 0)
        return ret;

    frame->crop_left   = s->offset_x;
    frame->crop_right  = avctx->coded_width - avctx->width - s->offset_x;
    frame->crop_top    = s->offset_y;
    frame->crop_bottom = avctx->coded_height - avctx->height - s->offset_y;

    *got_frame = 1;

    if (!HAVE_THREADS || !(s->avctx->active_thread_type & FF_THREAD_FRAME)) {
        ret = update_frames(avctx);
        if (ret < 0)
            return ret;
    }

    return buf_size;

error:
    ff_thread_report_progress(&s->current_frame, INT_MAX, 0);

    if (!HAVE_THREADS || !(s->avctx->active_thread_type & FF_THREAD_FRAME))
        av_frame_unref(s->current_frame.f);

    return ret;
}

static int read_huffman_tree(HuffTable *huff, GetBitContext *gb, int length,
                             AVCodecContext *avctx)
{
    if (get_bits1(gb)) {
        int token;
        if (huff->nb_entries >= 32) { /* overflow */
            av_log(avctx, AV_LOG_ERROR, "huffman tree overflow\n");
            return -1;
        }
        token = get_bits(gb, 5);
        ff_dlog(avctx, "code length %d, curr entry %d, token %d\n",
                length, huff->nb_entries, token);
        huff->entries[huff->nb_entries++] = (HuffEntry){ length, token };
    } else {
        /* The following bound follows from the fact that nb_entries <= 32. */
        if (length >= 31) { /* overflow */
            av_log(avctx, AV_LOG_ERROR, "huffman tree overflow\n");
            return -1;
        }
        length++;
        if (read_huffman_tree(huff, gb, length, avctx))
            return -1;
        if (read_huffman_tree(huff, gb, length, avctx))
            return -1;
    }
    return 0;
}

#if CONFIG_THEORA_DECODER
static const enum AVPixelFormat theora_pix_fmts[4] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P
};

static int theora_decode_header(AVCodecContext *avctx, GetBitContext *gb)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int visible_width, visible_height, colorspace;
    uint8_t offset_x = 0, offset_y = 0;
    int ret;
    AVRational fps, aspect;

    s->theora_header = 0;
    s->theora = get_bits(gb, 24);
    av_log(avctx, AV_LOG_DEBUG, "Theora bitstream version %X\n", s->theora);
    if (!s->theora) {
        s->theora = 1;
        avpriv_request_sample(s->avctx, "theora 0");
    }

    /* 3.2.0 aka alpha3 has the same frame orientation as original vp3
     * but previous versions have the image flipped relative to vp3 */
    if (s->theora < 0x030200) {
        s->flipped_image = 1;
        av_log(avctx, AV_LOG_DEBUG,
               "Old (<alpha3) Theora bitstream, flipped image\n");
    }

    visible_width  =
    s->width       = get_bits(gb, 16) << 4;
    visible_height =
    s->height      = get_bits(gb, 16) << 4;

    if (s->theora >= 0x030200) {
        visible_width  = get_bits(gb, 24);
        visible_height = get_bits(gb, 24);

        offset_x = get_bits(gb, 8); /* offset x */
        offset_y = get_bits(gb, 8); /* offset y, from bottom */
    }

    /* sanity check */
    if (av_image_check_size(visible_width, visible_height, 0, avctx) < 0 ||
        visible_width  + offset_x > s->width ||
        visible_height + offset_y > s->height) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid frame dimensions - w:%d h:%d x:%d y:%d (%dx%d).\n",
               visible_width, visible_height, offset_x, offset_y,
               s->width, s->height);
        return AVERROR_INVALIDDATA;
    }

    fps.num = get_bits_long(gb, 32);
    fps.den = get_bits_long(gb, 32);
    if (fps.num && fps.den) {
        if (fps.num < 0 || fps.den < 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid framerate\n");
            return AVERROR_INVALIDDATA;
        }
        av_reduce(&avctx->framerate.den, &avctx->framerate.num,
                  fps.den, fps.num, 1 << 30);
    }

    aspect.num = get_bits(gb, 24);
    aspect.den = get_bits(gb, 24);
    if (aspect.num && aspect.den) {
        av_reduce(&avctx->sample_aspect_ratio.num,
                  &avctx->sample_aspect_ratio.den,
                  aspect.num, aspect.den, 1 << 30);
        ff_set_sar(avctx, avctx->sample_aspect_ratio);
    }

    if (s->theora < 0x030200)
        skip_bits(gb, 5); /* keyframe frequency force */
    colorspace = get_bits(gb, 8);
    skip_bits(gb, 24); /* bitrate */

    skip_bits(gb, 6); /* quality hint */

    if (s->theora >= 0x030200) {
        skip_bits(gb, 5); /* keyframe frequency force */
        avctx->pix_fmt = theora_pix_fmts[get_bits(gb, 2)];
        if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Invalid pixel format\n");
            return AVERROR_INVALIDDATA;
        }
        skip_bits(gb, 3); /* reserved */
    } else
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ret = ff_set_dimensions(avctx, s->width, s->height);
    if (ret < 0)
        return ret;
    if (!(avctx->flags2 & AV_CODEC_FLAG2_IGNORE_CROP)) {
        avctx->width  = visible_width;
        avctx->height = visible_height;
        // translate offsets from theora axis ([0,0] lower left)
        // to normal axis ([0,0] upper left)
        s->offset_x = offset_x;
        s->offset_y = s->height - visible_height - offset_y;
    }

    if (colorspace == 1)
        avctx->color_primaries = AVCOL_PRI_BT470M;
    else if (colorspace == 2)
        avctx->color_primaries = AVCOL_PRI_BT470BG;

    if (colorspace == 1 || colorspace == 2) {
        avctx->colorspace = AVCOL_SPC_BT470BG;
        avctx->color_trc  = AVCOL_TRC_BT709;
    }

    s->theora_header = 1;
    return 0;
}

static int theora_decode_tables(AVCodecContext *avctx, GetBitContext *gb)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int i, n, matrices, inter, plane, ret;

    if (!s->theora_header)
        return AVERROR_INVALIDDATA;

    if (s->theora >= 0x030200) {
        n = get_bits(gb, 3);
        /* loop filter limit values table */
        if (n)
            for (i = 0; i < 64; i++)
                s->filter_limit_values[i] = get_bits(gb, n);
    }

    if (s->theora >= 0x030200)
        n = get_bits(gb, 4) + 1;
    else
        n = 16;
    /* quality threshold table */
    for (i = 0; i < 64; i++)
        s->coded_ac_scale_factor[i] = get_bits(gb, n);

    if (s->theora >= 0x030200)
        n = get_bits(gb, 4) + 1;
    else
        n = 16;
    /* dc scale factor table */
    for (i = 0; i < 64; i++)
        s->coded_dc_scale_factor[0][i] =
        s->coded_dc_scale_factor[1][i] = get_bits(gb, n);

    if (s->theora >= 0x030200)
        matrices = get_bits(gb, 9) + 1;
    else
        matrices = 3;

    if (matrices > 384) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of base matrixes\n");
        return -1;
    }

    for (n = 0; n < matrices; n++)
        for (i = 0; i < 64; i++)
            s->base_matrix[n][i] = get_bits(gb, 8);

    for (inter = 0; inter <= 1; inter++) {
        for (plane = 0; plane <= 2; plane++) {
            int newqr = 1;
            if (inter || plane > 0)
                newqr = get_bits1(gb);
            if (!newqr) {
                int qtj, plj;
                if (inter && get_bits1(gb)) {
                    qtj = 0;
                    plj = plane;
                } else {
                    qtj = (3 * inter + plane - 1) / 3;
                    plj = (plane + 2) % 3;
                }
                s->qr_count[inter][plane] = s->qr_count[qtj][plj];
                memcpy(s->qr_size[inter][plane], s->qr_size[qtj][plj],
                       sizeof(s->qr_size[0][0]));
                memcpy(s->qr_base[inter][plane], s->qr_base[qtj][plj],
                       sizeof(s->qr_base[0][0]));
            } else {
                int qri = 0;
                int qi  = 0;

                for (;;) {
                    i = get_bits(gb, av_log2(matrices - 1) + 1);
                    if (i >= matrices) {
                        av_log(avctx, AV_LOG_ERROR,
                               "invalid base matrix index\n");
                        return -1;
                    }
                    s->qr_base[inter][plane][qri] = i;
                    if (qi >= 63)
                        break;
                    i = get_bits(gb, av_log2(63 - qi) + 1) + 1;
                    s->qr_size[inter][plane][qri++] = i;
                    qi += i;
                }

                if (qi > 63) {
                    av_log(avctx, AV_LOG_ERROR, "invalid qi %d > 63\n", qi);
                    return -1;
                }
                s->qr_count[inter][plane] = qri;
            }
        }
    }

    /* Huffman tables */
    for (int i = 0; i < FF_ARRAY_ELEMS(s->huffman_table); i++) {
        s->huffman_table[i].nb_entries = 0;
        if ((ret = read_huffman_tree(&s->huffman_table[i], gb, 0, avctx)) < 0)
            return ret;
    }

    s->theora_tables = 1;

    return 0;
}

static av_cold int theora_decode_init(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    GetBitContext gb;
    int ptype;
    const uint8_t *header_start[3];
    int header_len[3];
    int i;
    int ret;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    s->theora = 1;

    if (!avctx->extradata_size) {
        av_log(avctx, AV_LOG_ERROR, "Missing extradata!\n");
        return -1;
    }

    if (avpriv_split_xiph_headers(avctx->extradata, avctx->extradata_size,
                                  42, header_start, header_len) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Corrupt extradata\n");
        return -1;
    }

    for (i = 0; i < 3; i++) {
        if (header_len[i] <= 0)
            continue;
        ret = init_get_bits8(&gb, header_start[i], header_len[i]);
        if (ret < 0)
            return ret;

        ptype = get_bits(&gb, 8);

        if (!(ptype & 0x80)) {
            av_log(avctx, AV_LOG_ERROR, "Invalid extradata!\n");
//          return -1;
        }

        // FIXME: Check for this as well.
        skip_bits_long(&gb, 6 * 8); /* "theora" */

        switch (ptype) {
        case 0x80:
            if (theora_decode_header(avctx, &gb) < 0)
                return -1;
            break;
        case 0x81:
// FIXME: is this needed? it breaks sometimes
//            theora_decode_comments(avctx, gb);
            break;
        case 0x82:
            if (theora_decode_tables(avctx, &gb))
                return -1;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                   "Unknown Theora config packet: %d\n", ptype & ~0x80);
            break;
        }
        if (ptype != 0x81 && 8 * header_len[i] != get_bits_count(&gb))
            av_log(avctx, AV_LOG_WARNING,
                   "%d bits left in packet %X\n",
                   8 * header_len[i] - get_bits_count(&gb), ptype);
        if (s->theora < 0x030200)
            break;
    }

    return vp3_decode_init(avctx);
}

AVCodec ff_theora_decoder = {
    .name                  = "theora",
    .long_name             = NULL_IF_CONFIG_SMALL("Theora"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_THEORA,
    .priv_data_size        = sizeof(Vp3DecodeContext),
    .init                  = theora_decode_init,
    .close                 = vp3_decode_end,
    .decode                = vp3_decode_frame,
    .capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DRAW_HORIZ_BAND |
                             AV_CODEC_CAP_FRAME_THREADS,
    .flush                 = vp3_decode_flush,
    .update_thread_context = ONLY_IF_THREADS_ENABLED(vp3_update_thread_context),
    .caps_internal         = FF_CODEC_CAP_EXPORTS_CROPPING | FF_CODEC_CAP_ALLOCATE_PROGRESS |
                             FF_CODEC_CAP_INIT_CLEANUP,
};
#endif

AVCodec ff_vp3_decoder = {
    .name                  = "vp3",
    .long_name             = NULL_IF_CONFIG_SMALL("On2 VP3"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_VP3,
    .priv_data_size        = sizeof(Vp3DecodeContext),
    .init                  = vp3_decode_init,
    .close                 = vp3_decode_end,
    .decode                = vp3_decode_frame,
    .capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DRAW_HORIZ_BAND |
                             AV_CODEC_CAP_FRAME_THREADS,
    .flush                 = vp3_decode_flush,
    .update_thread_context = ONLY_IF_THREADS_ENABLED(vp3_update_thread_context),
    .caps_internal         = FF_CODEC_CAP_ALLOCATE_PROGRESS | FF_CODEC_CAP_INIT_CLEANUP,
};

#if CONFIG_VP4_DECODER
AVCodec ff_vp4_decoder = {
    .name                  = "vp4",
    .long_name             = NULL_IF_CONFIG_SMALL("On2 VP4"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_VP4,
    .priv_data_size        = sizeof(Vp3DecodeContext),
    .init                  = vp3_decode_init,
    .close                 = vp3_decode_end,
    .decode                = vp3_decode_frame,
    .capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DRAW_HORIZ_BAND |
                             AV_CODEC_CAP_FRAME_THREADS,
    .flush                 = vp3_decode_flush,
    .update_thread_context = ONLY_IF_THREADS_ENABLED(vp3_update_thread_context),
    .caps_internal         = FF_CODEC_CAP_ALLOCATE_PROGRESS | FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
