/*
 * Copyright (C) 2003-2004 the ffmpeg project
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
 * @file libavcodec/vp3.c
 * On2 VP3 Video Decoder
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
#include <unistd.h>

#include "avcodec.h"
#include "dsputil.h"
#include "bitstream.h"

#include "vp3data.h"
#include "xiph.h"

#define FRAGMENT_PIXELS 8

typedef struct Coeff {
    struct Coeff *next;
    DCTELEM coeff;
    uint8_t index;
} Coeff;

//FIXME split things out into their own arrays
typedef struct Vp3Fragment {
    Coeff *next_coeff;
    /* address of first pixel taking into account which plane the fragment
     * lives on as well as the plane stride */
    int first_pixel;
    /* this is the macroblock that the fragment belongs to */
    uint16_t macroblock;
    uint8_t coding_method;
    int8_t motion_x;
    int8_t motion_y;
} Vp3Fragment;

#define SB_NOT_CODED        0
#define SB_PARTIALLY_CODED  1
#define SB_FULLY_CODED      2

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

/* There are 6 preset schemes, plus a free-form scheme */
static const int ModeAlphabet[6][CODING_MODE_COUNT] =
{
    /* scheme 1: Last motion vector dominates */
    {    MODE_INTER_LAST_MV,    MODE_INTER_PRIOR_LAST,
         MODE_INTER_PLUS_MV,    MODE_INTER_NO_MV,
         MODE_INTRA,            MODE_USING_GOLDEN,
         MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 2 */
    {    MODE_INTER_LAST_MV,    MODE_INTER_PRIOR_LAST,
         MODE_INTER_NO_MV,      MODE_INTER_PLUS_MV,
         MODE_INTRA,            MODE_USING_GOLDEN,
         MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 3 */
    {    MODE_INTER_LAST_MV,    MODE_INTER_PLUS_MV,
         MODE_INTER_PRIOR_LAST, MODE_INTER_NO_MV,
         MODE_INTRA,            MODE_USING_GOLDEN,
         MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 4 */
    {    MODE_INTER_LAST_MV,    MODE_INTER_PLUS_MV,
         MODE_INTER_NO_MV,      MODE_INTER_PRIOR_LAST,
         MODE_INTRA,            MODE_USING_GOLDEN,
         MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 5: No motion vector dominates */
    {    MODE_INTER_NO_MV,      MODE_INTER_LAST_MV,
         MODE_INTER_PRIOR_LAST, MODE_INTER_PLUS_MV,
         MODE_INTRA,            MODE_USING_GOLDEN,
         MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

    /* scheme 6 */
    {    MODE_INTER_NO_MV,      MODE_USING_GOLDEN,
         MODE_INTER_LAST_MV,    MODE_INTER_PRIOR_LAST,
         MODE_INTER_PLUS_MV,    MODE_INTRA,
         MODE_GOLDEN_MV,        MODE_INTER_FOURMV },

};

#define MIN_DEQUANT_VAL 2

typedef struct Vp3DecodeContext {
    AVCodecContext *avctx;
    int theora, theora_tables;
    int version;
    int width, height;
    AVFrame golden_frame;
    AVFrame last_frame;
    AVFrame current_frame;
    int keyframe;
    DSPContext dsp;
    int flipped_image;

    int qis[3];
    int nqis;
    int quality_index;
    int last_quality_index;

    int superblock_count;
    int y_superblock_width;
    int y_superblock_height;
    int c_superblock_width;
    int c_superblock_height;
    int u_superblock_start;
    int v_superblock_start;
    unsigned char *superblock_coding;

    int macroblock_count;
    int macroblock_width;
    int macroblock_height;

    int fragment_count;
    int fragment_width;
    int fragment_height;

    Vp3Fragment *all_fragments;
    uint8_t *coeff_counts;
    Coeff *coeffs;
    Coeff *next_coeff;
    int fragment_start[3];

    ScanTable scantable;

    /* tables */
    uint16_t coded_dc_scale_factor[64];
    uint32_t coded_ac_scale_factor[64];
    uint8_t base_matrix[384][64];
    uint8_t qr_count[2][3];
    uint8_t qr_size [2][3][64];
    uint16_t qr_base[2][3][64];

    /* this is a list of indexes into the all_fragments array indicating
     * which of the fragments are coded */
    int *coded_fragment_list;
    int coded_fragment_list_index;
    int pixel_addresses_initialized;

    VLC dc_vlc[16];
    VLC ac_vlc_1[16];
    VLC ac_vlc_2[16];
    VLC ac_vlc_3[16];
    VLC ac_vlc_4[16];

    VLC superblock_run_length_vlc;
    VLC fragment_run_length_vlc;
    VLC mode_code_vlc;
    VLC motion_vector_vlc;

    /* these arrays need to be on 16-byte boundaries since SSE2 operations
     * index into them */
    DECLARE_ALIGNED_16(int16_t, qmat[2][4][64]);        //<qmat[is_inter][plane]

    /* This table contains superblock_count * 16 entries. Each set of 16
     * numbers corresponds to the fragment indexes 0..15 of the superblock.
     * An entry will be -1 to indicate that no entry corresponds to that
     * index. */
    int *superblock_fragments;

    /* This table contains superblock_count * 4 entries. Each set of 4
     * numbers corresponds to the macroblock indexes 0..3 of the superblock.
     * An entry will be -1 to indicate that no entry corresponds to that
     * index. */
    int *superblock_macroblocks;

    /* This table contains macroblock_count * 6 entries. Each set of 6
     * numbers corresponds to the fragment indexes 0..5 which comprise
     * the macroblock (4 Y fragments and 2 C fragments). */
    int *macroblock_fragments;
    /* This is an array that indicates how a particular macroblock
     * is coded. */
    unsigned char *macroblock_coding;

    int first_coded_y_fragment;
    int first_coded_c_fragment;
    int last_coded_y_fragment;
    int last_coded_c_fragment;

    uint8_t edge_emu_buffer[9*2048]; //FIXME dynamic alloc
    int8_t qscale_table[2048]; //FIXME dynamic alloc (width+15)/16

    /* Huffman decode */
    int hti;
    unsigned int hbits;
    int entries;
    int huff_code_size;
    uint16_t huffman_table[80][32][2];

    uint8_t filter_limit_values[64];
    DECLARE_ALIGNED_8(int, bounding_values_array[256+2]);
} Vp3DecodeContext;

/************************************************************************
 * VP3 specific functions
 ************************************************************************/

/*
 * This function sets up all of the various blocks mappings:
 * superblocks <-> fragments, macroblocks <-> fragments,
 * superblocks <-> macroblocks
 *
 * Returns 0 is successful; returns 1 if *anything* went wrong.
 */
static int init_block_mapping(Vp3DecodeContext *s)
{
    int i, j;
    signed int hilbert_walk_mb[4];

    int current_fragment = 0;
    int current_width = 0;
    int current_height = 0;
    int right_edge = 0;
    int bottom_edge = 0;
    int superblock_row_inc = 0;
    int *hilbert = NULL;
    int mapping_index = 0;

    int current_macroblock;
    int c_fragment;

    signed char travel_width[16] = {
         1,  1,  0, -1,
         0,  0,  1,  0,
         1,  0,  1,  0,
         0, -1,  0,  1
    };

    signed char travel_height[16] = {
         0,  0,  1,  0,
         1,  1,  0, -1,
         0,  1,  0, -1,
        -1,  0, -1,  0
    };

    signed char travel_width_mb[4] = {
         1,  0,  1,  0
    };

    signed char travel_height_mb[4] = {
         0,  1,  0, -1
    };

    hilbert_walk_mb[0] = 1;
    hilbert_walk_mb[1] = s->macroblock_width;
    hilbert_walk_mb[2] = 1;
    hilbert_walk_mb[3] = -s->macroblock_width;

    /* iterate through each superblock (all planes) and map the fragments */
    for (i = 0; i < s->superblock_count; i++) {
        /* time to re-assign the limits? */
        if (i == 0) {

            /* start of Y superblocks */
            right_edge = s->fragment_width;
            bottom_edge = s->fragment_height;
            current_width = -1;
            current_height = 0;
            superblock_row_inc = 3 * s->fragment_width -
                (s->y_superblock_width * 4 - s->fragment_width);

            /* the first operation for this variable is to advance by 1 */
            current_fragment = -1;

        } else if (i == s->u_superblock_start) {

            /* start of U superblocks */
            right_edge = s->fragment_width / 2;
            bottom_edge = s->fragment_height / 2;
            current_width = -1;
            current_height = 0;
            superblock_row_inc = 3 * (s->fragment_width / 2) -
                (s->c_superblock_width * 4 - s->fragment_width / 2);

            /* the first operation for this variable is to advance by 1 */
            current_fragment = s->fragment_start[1] - 1;

        } else if (i == s->v_superblock_start) {

            /* start of V superblocks */
            right_edge = s->fragment_width / 2;
            bottom_edge = s->fragment_height / 2;
            current_width = -1;
            current_height = 0;
            superblock_row_inc = 3 * (s->fragment_width / 2) -
                (s->c_superblock_width * 4 - s->fragment_width / 2);

            /* the first operation for this variable is to advance by 1 */
            current_fragment = s->fragment_start[2] - 1;

        }

        if (current_width >= right_edge - 1) {
            /* reset width and move to next superblock row */
            current_width = -1;
            current_height += 4;

            /* fragment is now at the start of a new superblock row */
            current_fragment += superblock_row_inc;
        }

        /* iterate through all 16 fragments in a superblock */
        for (j = 0; j < 16; j++) {
            current_fragment += travel_width[j] + right_edge * travel_height[j];
            current_width += travel_width[j];
            current_height += travel_height[j];

            /* check if the fragment is in bounds */
            if ((current_width < right_edge) &&
                (current_height < bottom_edge)) {
                s->superblock_fragments[mapping_index] = current_fragment;
            } else {
                s->superblock_fragments[mapping_index] = -1;
            }

            mapping_index++;
        }
    }

    /* initialize the superblock <-> macroblock mapping; iterate through
     * all of the Y plane superblocks to build this mapping */
    right_edge = s->macroblock_width;
    bottom_edge = s->macroblock_height;
    current_width = -1;
    current_height = 0;
    superblock_row_inc = s->macroblock_width -
        (s->y_superblock_width * 2 - s->macroblock_width);
    hilbert = hilbert_walk_mb;
    mapping_index = 0;
    current_macroblock = -1;
    for (i = 0; i < s->u_superblock_start; i++) {

        if (current_width >= right_edge - 1) {
            /* reset width and move to next superblock row */
            current_width = -1;
            current_height += 2;

            /* macroblock is now at the start of a new superblock row */
            current_macroblock += superblock_row_inc;
        }

        /* iterate through each potential macroblock in the superblock */
        for (j = 0; j < 4; j++) {
            current_macroblock += hilbert_walk_mb[j];
            current_width += travel_width_mb[j];
            current_height += travel_height_mb[j];

            /* check if the macroblock is in bounds */
            if ((current_width < right_edge) &&
                (current_height < bottom_edge)) {
                s->superblock_macroblocks[mapping_index] = current_macroblock;
            } else {
                s->superblock_macroblocks[mapping_index] = -1;
            }

            mapping_index++;
        }
    }

    /* initialize the macroblock <-> fragment mapping */
    current_fragment = 0;
    current_macroblock = 0;
    mapping_index = 0;
    for (i = 0; i < s->fragment_height; i += 2) {

        for (j = 0; j < s->fragment_width; j += 2) {

            s->all_fragments[current_fragment].macroblock = current_macroblock;
            s->macroblock_fragments[mapping_index++] = current_fragment;

            if (j + 1 < s->fragment_width) {
                s->all_fragments[current_fragment + 1].macroblock = current_macroblock;
                s->macroblock_fragments[mapping_index++] = current_fragment + 1;
            } else
                s->macroblock_fragments[mapping_index++] = -1;

            if (i + 1 < s->fragment_height) {
                s->all_fragments[current_fragment + s->fragment_width].macroblock =
                    current_macroblock;
                s->macroblock_fragments[mapping_index++] =
                    current_fragment + s->fragment_width;
            } else
                s->macroblock_fragments[mapping_index++] = -1;

            if ((j + 1 < s->fragment_width) && (i + 1 < s->fragment_height)) {
                s->all_fragments[current_fragment + s->fragment_width + 1].macroblock =
                    current_macroblock;
                s->macroblock_fragments[mapping_index++] =
                    current_fragment + s->fragment_width + 1;
            } else
                s->macroblock_fragments[mapping_index++] = -1;

            /* C planes */
            c_fragment = s->fragment_start[1] +
                (i * s->fragment_width / 4) + (j / 2);
            s->all_fragments[c_fragment].macroblock = s->macroblock_count;
            s->macroblock_fragments[mapping_index++] = c_fragment;

            c_fragment = s->fragment_start[2] +
                (i * s->fragment_width / 4) + (j / 2);
            s->all_fragments[c_fragment].macroblock = s->macroblock_count;
            s->macroblock_fragments[mapping_index++] = c_fragment;

            if (j + 2 <= s->fragment_width)
                current_fragment += 2;
            else
                current_fragment++;
            current_macroblock++;
        }

        current_fragment += s->fragment_width;
    }

    return 0;  /* successful path out */
}

/*
 * This function wipes out all of the fragment data.
 */
static void init_frame(Vp3DecodeContext *s, GetBitContext *gb)
{
    int i;

    /* zero out all of the fragment information */
    s->coded_fragment_list_index = 0;
    for (i = 0; i < s->fragment_count; i++) {
        s->coeff_counts[i] = 0;
        s->all_fragments[i].motion_x = 127;
        s->all_fragments[i].motion_y = 127;
        s->all_fragments[i].next_coeff= NULL;
        s->coeffs[i].index=
        s->coeffs[i].coeff=0;
        s->coeffs[i].next= NULL;
    }
}

/*
 * This function sets up the dequantization tables used for a particular
 * frame.
 */
static void init_dequantizer(Vp3DecodeContext *s)
{
    int ac_scale_factor = s->coded_ac_scale_factor[s->quality_index];
    int dc_scale_factor = s->coded_dc_scale_factor[s->quality_index];
    int i, plane, inter, qri, bmi, bmj, qistart;

    for(inter=0; inter<2; inter++){
        for(plane=0; plane<3; plane++){
            int sum=0;
            for(qri=0; qri<s->qr_count[inter][plane]; qri++){
                sum+= s->qr_size[inter][plane][qri];
                if(s->quality_index <= sum)
                    break;
            }
            qistart= sum - s->qr_size[inter][plane][qri];
            bmi= s->qr_base[inter][plane][qri  ];
            bmj= s->qr_base[inter][plane][qri+1];
            for(i=0; i<64; i++){
                int coeff= (  2*(sum    -s->quality_index)*s->base_matrix[bmi][i]
                            - 2*(qistart-s->quality_index)*s->base_matrix[bmj][i]
                            + s->qr_size[inter][plane][qri])
                           / (2*s->qr_size[inter][plane][qri]);

                int qmin= 8<<(inter + !i);
                int qscale= i ? ac_scale_factor : dc_scale_factor;

                s->qmat[inter][plane][s->dsp.idct_permutation[i]]= av_clip((qscale * coeff)/100 * 4, qmin, 4096);
            }
        }
    }

    memset(s->qscale_table, (FFMAX(s->qmat[0][0][1], s->qmat[0][1][1])+8)/16, 512); //FIXME finetune
}

/*
 * This function initializes the loop filter boundary limits if the frame's
 * quality index is different from the previous frame's.
 */
static void init_loop_filter(Vp3DecodeContext *s)
{
    int *bounding_values= s->bounding_values_array+127;
    int filter_limit;
    int x;

    filter_limit = s->filter_limit_values[s->quality_index];

    /* set up the bounding values */
    memset(s->bounding_values_array, 0, 256 * sizeof(int));
    for (x = 0; x < filter_limit; x++) {
        bounding_values[-x - filter_limit] = -filter_limit + x;
        bounding_values[-x] = -x;
        bounding_values[x] = x;
        bounding_values[x + filter_limit] = filter_limit - x;
    }
    bounding_values[129] = bounding_values[130] = filter_limit * 0x02020202;
}

/*
 * This function unpacks all of the superblock/macroblock/fragment coding
 * information from the bitstream.
 */
static int unpack_superblocks(Vp3DecodeContext *s, GetBitContext *gb)
{
    int bit = 0;
    int current_superblock = 0;
    int current_run = 0;
    int decode_fully_flags = 0;
    int decode_partial_blocks = 0;
    int first_c_fragment_seen;

    int i, j;
    int current_fragment;

    if (s->keyframe) {
        memset(s->superblock_coding, SB_FULLY_CODED, s->superblock_count);

    } else {

        /* unpack the list of partially-coded superblocks */
        bit = get_bits1(gb);
        /* toggle the bit because as soon as the first run length is
         * fetched the bit will be toggled again */
        bit ^= 1;
        while (current_superblock < s->superblock_count) {
            if (current_run-- == 0) {
                bit ^= 1;
                current_run = get_vlc2(gb,
                    s->superblock_run_length_vlc.table, 6, 2);
                if (current_run == 33)
                    current_run += get_bits(gb, 12);

                /* if any of the superblocks are not partially coded, flag
                 * a boolean to decode the list of fully-coded superblocks */
                if (bit == 0) {
                    decode_fully_flags = 1;
                } else {

                    /* make a note of the fact that there are partially coded
                     * superblocks */
                    decode_partial_blocks = 1;
                }
            }
            s->superblock_coding[current_superblock++] = bit;
        }

        /* unpack the list of fully coded superblocks if any of the blocks were
         * not marked as partially coded in the previous step */
        if (decode_fully_flags) {

            current_superblock = 0;
            current_run = 0;
            bit = get_bits1(gb);
            /* toggle the bit because as soon as the first run length is
             * fetched the bit will be toggled again */
            bit ^= 1;
            while (current_superblock < s->superblock_count) {

                /* skip any superblocks already marked as partially coded */
                if (s->superblock_coding[current_superblock] == SB_NOT_CODED) {

                    if (current_run-- == 0) {
                        bit ^= 1;
                        current_run = get_vlc2(gb,
                            s->superblock_run_length_vlc.table, 6, 2);
                        if (current_run == 33)
                            current_run += get_bits(gb, 12);
                    }
                    s->superblock_coding[current_superblock] = 2*bit;
                }
                current_superblock++;
            }
        }

        /* if there were partial blocks, initialize bitstream for
         * unpacking fragment codings */
        if (decode_partial_blocks) {

            current_run = 0;
            bit = get_bits1(gb);
            /* toggle the bit because as soon as the first run length is
             * fetched the bit will be toggled again */
            bit ^= 1;
        }
    }

    /* figure out which fragments are coded; iterate through each
     * superblock (all planes) */
    s->coded_fragment_list_index = 0;
    s->next_coeff= s->coeffs + s->fragment_count;
    s->first_coded_y_fragment = s->first_coded_c_fragment = 0;
    s->last_coded_y_fragment = s->last_coded_c_fragment = -1;
    first_c_fragment_seen = 0;
    memset(s->macroblock_coding, MODE_COPY, s->macroblock_count);
    for (i = 0; i < s->superblock_count; i++) {

        /* iterate through all 16 fragments in a superblock */
        for (j = 0; j < 16; j++) {

            /* if the fragment is in bounds, check its coding status */
            current_fragment = s->superblock_fragments[i * 16 + j];
            if (current_fragment >= s->fragment_count) {
                av_log(s->avctx, AV_LOG_ERROR, "  vp3:unpack_superblocks(): bad fragment number (%d >= %d)\n",
                    current_fragment, s->fragment_count);
                return 1;
            }
            if (current_fragment != -1) {
                if (s->superblock_coding[i] == SB_NOT_CODED) {

                    /* copy all the fragments from the prior frame */
                    s->all_fragments[current_fragment].coding_method =
                        MODE_COPY;

                } else if (s->superblock_coding[i] == SB_PARTIALLY_CODED) {

                    /* fragment may or may not be coded; this is the case
                     * that cares about the fragment coding runs */
                    if (current_run-- == 0) {
                        bit ^= 1;
                        current_run = get_vlc2(gb,
                            s->fragment_run_length_vlc.table, 5, 2);
                    }

                    if (bit) {
                        /* default mode; actual mode will be decoded in
                         * the next phase */
                        s->all_fragments[current_fragment].coding_method =
                            MODE_INTER_NO_MV;
                        s->all_fragments[current_fragment].next_coeff= s->coeffs + current_fragment;
                        s->coded_fragment_list[s->coded_fragment_list_index] =
                            current_fragment;
                        if ((current_fragment >= s->fragment_start[1]) &&
                            (s->last_coded_y_fragment == -1) &&
                            (!first_c_fragment_seen)) {
                            s->first_coded_c_fragment = s->coded_fragment_list_index;
                            s->last_coded_y_fragment = s->first_coded_c_fragment - 1;
                            first_c_fragment_seen = 1;
                        }
                        s->coded_fragment_list_index++;
                        s->macroblock_coding[s->all_fragments[current_fragment].macroblock] = MODE_INTER_NO_MV;
                    } else {
                        /* not coded; copy this fragment from the prior frame */
                        s->all_fragments[current_fragment].coding_method =
                            MODE_COPY;
                    }

                } else {

                    /* fragments are fully coded in this superblock; actual
                     * coding will be determined in next step */
                    s->all_fragments[current_fragment].coding_method =
                        MODE_INTER_NO_MV;
                    s->all_fragments[current_fragment].next_coeff= s->coeffs + current_fragment;
                    s->coded_fragment_list[s->coded_fragment_list_index] =
                        current_fragment;
                    if ((current_fragment >= s->fragment_start[1]) &&
                        (s->last_coded_y_fragment == -1) &&
                        (!first_c_fragment_seen)) {
                        s->first_coded_c_fragment = s->coded_fragment_list_index;
                        s->last_coded_y_fragment = s->first_coded_c_fragment - 1;
                        first_c_fragment_seen = 1;
                    }
                    s->coded_fragment_list_index++;
                    s->macroblock_coding[s->all_fragments[current_fragment].macroblock] = MODE_INTER_NO_MV;
                }
            }
        }
    }

    if (!first_c_fragment_seen)
        /* only Y fragments coded in this frame */
        s->last_coded_y_fragment = s->coded_fragment_list_index - 1;
    else
        /* end the list of coded C fragments */
        s->last_coded_c_fragment = s->coded_fragment_list_index - 1;

    return 0;
}

/*
 * This function unpacks all the coding mode data for individual macroblocks
 * from the bitstream.
 */
static int unpack_modes(Vp3DecodeContext *s, GetBitContext *gb)
{
    int i, j, k;
    int scheme;
    int current_macroblock;
    int current_fragment;
    int coding_mode;
    int custom_mode_alphabet[CODING_MODE_COUNT];

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
        }

        /* iterate through all of the macroblocks that contain 1 or more
         * coded fragments */
        for (i = 0; i < s->u_superblock_start; i++) {

            for (j = 0; j < 4; j++) {
                current_macroblock = s->superblock_macroblocks[i * 4 + j];
                if ((current_macroblock == -1) ||
                    (s->macroblock_coding[current_macroblock] == MODE_COPY))
                    continue;
                if (current_macroblock >= s->macroblock_count) {
                    av_log(s->avctx, AV_LOG_ERROR, "  vp3:unpack_modes(): bad macroblock number (%d >= %d)\n",
                        current_macroblock, s->macroblock_count);
                    return 1;
                }

                /* mode 7 means get 3 bits for each coding mode */
                if (scheme == 7)
                    coding_mode = get_bits(gb, 3);
                else if(scheme == 0)
                    coding_mode = custom_mode_alphabet
                        [get_vlc2(gb, s->mode_code_vlc.table, 3, 3)];
                else
                    coding_mode = ModeAlphabet[scheme-1]
                        [get_vlc2(gb, s->mode_code_vlc.table, 3, 3)];

                s->macroblock_coding[current_macroblock] = coding_mode;
                for (k = 0; k < 6; k++) {
                    current_fragment =
                        s->macroblock_fragments[current_macroblock * 6 + k];
                    if (current_fragment == -1)
                        continue;
                    if (current_fragment >= s->fragment_count) {
                        av_log(s->avctx, AV_LOG_ERROR, "  vp3:unpack_modes(): bad fragment number (%d >= %d)\n",
                            current_fragment, s->fragment_count);
                        return 1;
                    }
                    if (s->all_fragments[current_fragment].coding_method !=
                        MODE_COPY)
                        s->all_fragments[current_fragment].coding_method =
                            coding_mode;
                }
            }
        }
    }

    return 0;
}

/*
 * This function unpacks all the motion vectors for the individual
 * macroblocks from the bitstream.
 */
static int unpack_vectors(Vp3DecodeContext *s, GetBitContext *gb)
{
    int i, j, k, l;
    int coding_mode;
    int motion_x[6];
    int motion_y[6];
    int last_motion_x = 0;
    int last_motion_y = 0;
    int prior_last_motion_x = 0;
    int prior_last_motion_y = 0;
    int current_macroblock;
    int current_fragment;

    if (s->keyframe)
        return 0;

    memset(motion_x, 0, 6 * sizeof(int));
    memset(motion_y, 0, 6 * sizeof(int));

    /* coding mode 0 is the VLC scheme; 1 is the fixed code scheme */
    coding_mode = get_bits1(gb);

    /* iterate through all of the macroblocks that contain 1 or more
     * coded fragments */
    for (i = 0; i < s->u_superblock_start; i++) {

        for (j = 0; j < 4; j++) {
            current_macroblock = s->superblock_macroblocks[i * 4 + j];
            if ((current_macroblock == -1) ||
                (s->macroblock_coding[current_macroblock] == MODE_COPY))
                continue;
            if (current_macroblock >= s->macroblock_count) {
                av_log(s->avctx, AV_LOG_ERROR, "  vp3:unpack_vectors(): bad macroblock number (%d >= %d)\n",
                    current_macroblock, s->macroblock_count);
                return 1;
            }

            current_fragment = s->macroblock_fragments[current_macroblock * 6];
            if (current_fragment >= s->fragment_count) {
                av_log(s->avctx, AV_LOG_ERROR, "  vp3:unpack_vectors(): bad fragment number (%d >= %d\n",
                    current_fragment, s->fragment_count);
                return 1;
            }
            switch (s->macroblock_coding[current_macroblock]) {

            case MODE_INTER_PLUS_MV:
            case MODE_GOLDEN_MV:
                /* all 6 fragments use the same motion vector */
                if (coding_mode == 0) {
                    motion_x[0] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                    motion_y[0] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                } else {
                    motion_x[0] = fixed_motion_vector_table[get_bits(gb, 6)];
                    motion_y[0] = fixed_motion_vector_table[get_bits(gb, 6)];
                }

                for (k = 1; k < 6; k++) {
                    motion_x[k] = motion_x[0];
                    motion_y[k] = motion_y[0];
                }

                /* vector maintenance, only on MODE_INTER_PLUS_MV */
                if (s->macroblock_coding[current_macroblock] ==
                    MODE_INTER_PLUS_MV) {
                    prior_last_motion_x = last_motion_x;
                    prior_last_motion_y = last_motion_y;
                    last_motion_x = motion_x[0];
                    last_motion_y = motion_y[0];
                }
                break;

            case MODE_INTER_FOURMV:
                /* vector maintenance */
                prior_last_motion_x = last_motion_x;
                prior_last_motion_y = last_motion_y;

                /* fetch 4 vectors from the bitstream, one for each
                 * Y fragment, then average for the C fragment vectors */
                motion_x[4] = motion_y[4] = 0;
                for (k = 0; k < 4; k++) {
                    for (l = 0; l < s->coded_fragment_list_index; l++)
                        if (s->coded_fragment_list[l] == s->macroblock_fragments[6*current_macroblock + k])
                            break;
                    if (l < s->coded_fragment_list_index) {
                        if (coding_mode == 0) {
                            motion_x[k] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                            motion_y[k] = motion_vector_table[get_vlc2(gb, s->motion_vector_vlc.table, 6, 2)];
                        } else {
                            motion_x[k] = fixed_motion_vector_table[get_bits(gb, 6)];
                            motion_y[k] = fixed_motion_vector_table[get_bits(gb, 6)];
                        }
                        last_motion_x = motion_x[k];
                        last_motion_y = motion_y[k];
                    } else {
                        motion_x[k] = 0;
                        motion_y[k] = 0;
                    }
                    motion_x[4] += motion_x[k];
                    motion_y[4] += motion_y[k];
                }

                motion_x[5]=
                motion_x[4]= RSHIFT(motion_x[4], 2);
                motion_y[5]=
                motion_y[4]= RSHIFT(motion_y[4], 2);
                break;

            case MODE_INTER_LAST_MV:
                /* all 6 fragments use the last motion vector */
                motion_x[0] = last_motion_x;
                motion_y[0] = last_motion_y;
                for (k = 1; k < 6; k++) {
                    motion_x[k] = motion_x[0];
                    motion_y[k] = motion_y[0];
                }

                /* no vector maintenance (last vector remains the
                 * last vector) */
                break;

            case MODE_INTER_PRIOR_LAST:
                /* all 6 fragments use the motion vector prior to the
                 * last motion vector */
                motion_x[0] = prior_last_motion_x;
                motion_y[0] = prior_last_motion_y;
                for (k = 1; k < 6; k++) {
                    motion_x[k] = motion_x[0];
                    motion_y[k] = motion_y[0];
                }

                /* vector maintenance */
                prior_last_motion_x = last_motion_x;
                prior_last_motion_y = last_motion_y;
                last_motion_x = motion_x[0];
                last_motion_y = motion_y[0];
                break;

            default:
                /* covers intra, inter without MV, golden without MV */
                memset(motion_x, 0, 6 * sizeof(int));
                memset(motion_y, 0, 6 * sizeof(int));

                /* no vector maintenance */
                break;
            }

            /* assign the motion vectors to the correct fragments */
            for (k = 0; k < 6; k++) {
                current_fragment =
                    s->macroblock_fragments[current_macroblock * 6 + k];
                if (current_fragment == -1)
                    continue;
                if (current_fragment >= s->fragment_count) {
                    av_log(s->avctx, AV_LOG_ERROR, "  vp3:unpack_vectors(): bad fragment number (%d >= %d)\n",
                        current_fragment, s->fragment_count);
                    return 1;
                }
                s->all_fragments[current_fragment].motion_x = motion_x[k];
                s->all_fragments[current_fragment].motion_y = motion_y[k];
            }
        }
    }

    return 0;
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
                        int first_fragment, int last_fragment,
                        int eob_run)
{
    int i;
    int token;
    int zero_run = 0;
    DCTELEM coeff = 0;
    Vp3Fragment *fragment;
    uint8_t *perm= s->scantable.permutated;
    int bits_to_get;

    if ((first_fragment >= s->fragment_count) ||
        (last_fragment >= s->fragment_count)) {

        av_log(s->avctx, AV_LOG_ERROR, "  vp3:unpack_vlcs(): bad fragment number (%d -> %d ?)\n",
            first_fragment, last_fragment);
        return 0;
    }

    for (i = first_fragment; i <= last_fragment; i++) {
        int fragment_num = s->coded_fragment_list[i];

        if (s->coeff_counts[fragment_num] > coeff_index)
            continue;
        fragment = &s->all_fragments[fragment_num];

        if (!eob_run) {
            /* decode a VLC into a token */
            token = get_vlc2(gb, table->table, 5, 3);
            /* use the token to get a zero run, a coefficient, and an eob run */
            if (token <= 6) {
                eob_run = eob_run_base[token];
                if (eob_run_get_bits[token])
                    eob_run += get_bits(gb, eob_run_get_bits[token]);
                coeff = zero_run = 0;
            } else {
                bits_to_get = coeff_get_bits[token];
                if (!bits_to_get)
                    coeff = coeff_tables[token][0];
                else
                    coeff = coeff_tables[token][get_bits(gb, bits_to_get)];

                zero_run = zero_run_base[token];
                if (zero_run_get_bits[token])
                    zero_run += get_bits(gb, zero_run_get_bits[token]);
            }
        }

        if (!eob_run) {
            s->coeff_counts[fragment_num] += zero_run;
            if (s->coeff_counts[fragment_num] < 64){
                fragment->next_coeff->coeff= coeff;
                fragment->next_coeff->index= perm[s->coeff_counts[fragment_num]++]; //FIXME perm here already?
                fragment->next_coeff->next= s->next_coeff;
                s->next_coeff->next=NULL;
                fragment->next_coeff= s->next_coeff++;
            }
        } else {
            s->coeff_counts[fragment_num] |= 128;
            eob_run--;
        }
    }

    return eob_run;
}

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

    /* fetch the DC table indexes */
    dc_y_table = get_bits(gb, 4);
    dc_c_table = get_bits(gb, 4);

    /* unpack the Y plane DC coefficients */
    residual_eob_run = unpack_vlcs(s, gb, &s->dc_vlc[dc_y_table], 0,
        s->first_coded_y_fragment, s->last_coded_y_fragment, residual_eob_run);

    /* unpack the C plane DC coefficients */
    residual_eob_run = unpack_vlcs(s, gb, &s->dc_vlc[dc_c_table], 0,
        s->first_coded_c_fragment, s->last_coded_c_fragment, residual_eob_run);

    /* fetch the AC table indexes */
    ac_y_table = get_bits(gb, 4);
    ac_c_table = get_bits(gb, 4);

    /* unpack the group 1 AC coefficients (coeffs 1-5) */
    for (i = 1; i <= 5; i++) {
        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_1[ac_y_table], i,
            s->first_coded_y_fragment, s->last_coded_y_fragment, residual_eob_run);

        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_1[ac_c_table], i,
            s->first_coded_c_fragment, s->last_coded_c_fragment, residual_eob_run);
    }

    /* unpack the group 2 AC coefficients (coeffs 6-14) */
    for (i = 6; i <= 14; i++) {
        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_2[ac_y_table], i,
            s->first_coded_y_fragment, s->last_coded_y_fragment, residual_eob_run);

        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_2[ac_c_table], i,
            s->first_coded_c_fragment, s->last_coded_c_fragment, residual_eob_run);
    }

    /* unpack the group 3 AC coefficients (coeffs 15-27) */
    for (i = 15; i <= 27; i++) {
        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_3[ac_y_table], i,
            s->first_coded_y_fragment, s->last_coded_y_fragment, residual_eob_run);

        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_3[ac_c_table], i,
            s->first_coded_c_fragment, s->last_coded_c_fragment, residual_eob_run);
    }

    /* unpack the group 4 AC coefficients (coeffs 28-63) */
    for (i = 28; i <= 63; i++) {
        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_4[ac_y_table], i,
            s->first_coded_y_fragment, s->last_coded_y_fragment, residual_eob_run);

        residual_eob_run = unpack_vlcs(s, gb, &s->ac_vlc_4[ac_c_table], i,
            s->first_coded_c_fragment, s->last_coded_c_fragment, residual_eob_run);
    }

    return 0;
}

/*
 * This function reverses the DC prediction for each coded fragment in
 * the frame. Much of this function is adapted directly from the original
 * VP3 source code.
 */
#define COMPATIBLE_FRAME(x) \
  (compatible_frame[s->all_fragments[x].coding_method] == current_frame_type)
#define FRAME_CODED(x) (s->all_fragments[x].coding_method != MODE_COPY)
#define DC_COEFF(u) (s->coeffs[u].index ? 0 : s->coeffs[u].coeff) //FIXME do somethin to simplify this

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
    int predictor_transform[16][4] = {
        {  0,  0,  0,  0},
        {  0,  0,  0,128},        // PL
        {  0,  0,128,  0},        // PUR
        {  0,  0, 53, 75},        // PUR|PL
        {  0,128,  0,  0},        // PU
        {  0, 64,  0, 64},        // PU|PL
        {  0,128,  0,  0},        // PU|PUR
        {  0,  0, 53, 75},        // PU|PUR|PL
        {128,  0,  0,  0},        // PUL
        {  0,  0,  0,128},        // PUL|PL
        { 64,  0, 64,  0},        // PUL|PUR
        {  0,  0, 53, 75},        // PUL|PUR|PL
        {  0,128,  0,  0},        // PUL|PU
       {-104,116,  0,116},        // PUL|PU|PL
        { 24, 80, 24,  0},        // PUL|PU|PUR
       {-104,116,  0,116}         // PUL|PU|PUR|PL
    };

    /* This table shows which types of blocks can use other blocks for
     * prediction. For example, INTRA is the only mode in this table to
     * have a frame number of 0. That means INTRA blocks can only predict
     * from other INTRA blocks. There are 2 golden frame coding types;
     * blocks encoding in these modes can only predict from other blocks
     * that were encoded with these 1 of these 2 modes. */
    unsigned char compatible_frame[8] = {
        1,    /* MODE_INTER_NO_MV */
        0,    /* MODE_INTRA */
        1,    /* MODE_INTER_PLUS_MV */
        1,    /* MODE_INTER_LAST_MV */
        1,    /* MODE_INTER_PRIOR_MV */
        2,    /* MODE_USING_GOLDEN */
        2,    /* MODE_GOLDEN_MV */
        1     /* MODE_INTER_FOUR_MV */
    };
    int current_frame_type;

    /* there is a last DC predictor for each of the 3 frame types */
    short last_dc[3];

    int transform = 0;

    vul = vu = vur = vl = 0;
    last_dc[0] = last_dc[1] = last_dc[2] = 0;

    /* for each fragment row... */
    for (y = 0; y < fragment_height; y++) {

        /* for each fragment in a row... */
        for (x = 0; x < fragment_width; x++, i++) {

            /* reverse prediction if this block was coded */
            if (s->all_fragments[i].coding_method != MODE_COPY) {

                current_frame_type =
                    compatible_frame[s->all_fragments[i].coding_method];

                transform= 0;
                if(x){
                    l= i-1;
                    vl = DC_COEFF(l);
                    if(FRAME_CODED(l) && COMPATIBLE_FRAME(l))
                        transform |= PL;
                }
                if(y){
                    u= i-fragment_width;
                    vu = DC_COEFF(u);
                    if(FRAME_CODED(u) && COMPATIBLE_FRAME(u))
                        transform |= PU;
                    if(x){
                        ul= i-fragment_width-1;
                        vul = DC_COEFF(ul);
                        if(FRAME_CODED(ul) && COMPATIBLE_FRAME(ul))
                            transform |= PUL;
                    }
                    if(x + 1 < fragment_width){
                        ur= i-fragment_width+1;
                        vur = DC_COEFF(ur);
                        if(FRAME_CODED(ur) && COMPATIBLE_FRAME(ur))
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
                    if ((transform == 13) || (transform == 15)) {
                        if (FFABS(predicted_dc - vu) > 128)
                            predicted_dc = vu;
                        else if (FFABS(predicted_dc - vl) > 128)
                            predicted_dc = vl;
                        else if (FFABS(predicted_dc - vul) > 128)
                            predicted_dc = vul;
                    }
                }

                /* at long last, apply the predictor */
                if(s->coeffs[i].index){
                    *s->next_coeff= s->coeffs[i];
                    s->coeffs[i].index=0;
                    s->coeffs[i].coeff=0;
                    s->coeffs[i].next= s->next_coeff++;
                }
                s->coeffs[i].coeff += predicted_dc;
                /* save the DC */
                last_dc[current_frame_type] = DC_COEFF(i);
                if(DC_COEFF(i) && !(s->coeff_counts[i]&127)){
                    s->coeff_counts[i]= 129;
//                    s->all_fragments[i].next_coeff= s->next_coeff;
                    s->coeffs[i].next= s->next_coeff;
                    (s->next_coeff++)->next=NULL;
                }
            }
        }
    }
}

/*
 * Perform the final rendering for a particular slice of data.
 * The slice number ranges from 0..(macroblock_height - 1).
 */
static void render_slice(Vp3DecodeContext *s, int slice)
{
    int x;
    int16_t *dequantizer;
    DECLARE_ALIGNED_16(DCTELEM, block[64]);
    int motion_x = 0xdeadbeef, motion_y = 0xdeadbeef;
    int motion_halfpel_index;
    uint8_t *motion_source;
    int plane;
    int current_macroblock_entry = slice * s->macroblock_width * 6;

    if (slice >= s->macroblock_height)
        return;

    for (plane = 0; plane < 3; plane++) {
        uint8_t *output_plane = s->current_frame.data    [plane];
        uint8_t *  last_plane = s->   last_frame.data    [plane];
        uint8_t *golden_plane = s-> golden_frame.data    [plane];
        int stride            = s->current_frame.linesize[plane];
        int plane_width       = s->width  >> !!plane;
        int plane_height      = s->height >> !!plane;
        int y =        slice *  FRAGMENT_PIXELS << !plane ;
        int slice_height = y + (FRAGMENT_PIXELS << !plane);
        int i = s->macroblock_fragments[current_macroblock_entry + plane + 3*!!plane];

        if (!s->flipped_image) stride = -stride;


        if(FFABS(stride) > 2048)
            return; //various tables are fixed size

        /* for each fragment row in the slice (both of them)... */
        for (; y < slice_height; y += 8) {

            /* for each fragment in a row... */
            for (x = 0; x < plane_width; x += 8, i++) {

                if ((i < 0) || (i >= s->fragment_count)) {
                    av_log(s->avctx, AV_LOG_ERROR, "  vp3:render_slice(): bad fragment number (%d)\n", i);
                    return;
                }

                /* transform if this block was coded */
                if ((s->all_fragments[i].coding_method != MODE_COPY) &&
                    !((s->avctx->flags & CODEC_FLAG_GRAY) && plane)) {

                    if ((s->all_fragments[i].coding_method == MODE_USING_GOLDEN) ||
                        (s->all_fragments[i].coding_method == MODE_GOLDEN_MV))
                        motion_source= golden_plane;
                    else
                        motion_source= last_plane;

                    motion_source += s->all_fragments[i].first_pixel;
                    motion_halfpel_index = 0;

                    /* sort out the motion vector if this fragment is coded
                     * using a motion vector method */
                    if ((s->all_fragments[i].coding_method > MODE_INTRA) &&
                        (s->all_fragments[i].coding_method != MODE_USING_GOLDEN)) {
                        int src_x, src_y;
                        motion_x = s->all_fragments[i].motion_x;
                        motion_y = s->all_fragments[i].motion_y;
                        if(plane){
                            motion_x= (motion_x>>1) | (motion_x&1);
                            motion_y= (motion_y>>1) | (motion_y&1);
                        }

                        src_x= (motion_x>>1) + x;
                        src_y= (motion_y>>1) + y;
                        if ((motion_x == 127) || (motion_y == 127))
                            av_log(s->avctx, AV_LOG_ERROR, " help! got invalid motion vector! (%X, %X)\n", motion_x, motion_y);

                        motion_halfpel_index = motion_x & 0x01;
                        motion_source += (motion_x >> 1);

                        motion_halfpel_index |= (motion_y & 0x01) << 1;
                        motion_source += ((motion_y >> 1) * stride);

                        if(src_x<0 || src_y<0 || src_x + 9 >= plane_width || src_y + 9 >= plane_height){
                            uint8_t *temp= s->edge_emu_buffer;
                            if(stride<0) temp -= 9*stride;
                            else temp += 9*stride;

                            ff_emulated_edge_mc(temp, motion_source, stride, 9, 9, src_x, src_y, plane_width, plane_height);
                            motion_source= temp;
                        }
                    }


                    /* first, take care of copying a block from either the
                     * previous or the golden frame */
                    if (s->all_fragments[i].coding_method != MODE_INTRA) {
                        /* Note, it is possible to implement all MC cases with
                           put_no_rnd_pixels_l2 which would look more like the
                           VP3 source but this would be slower as
                           put_no_rnd_pixels_tab is better optimzed */
                        if(motion_halfpel_index != 3){
                            s->dsp.put_no_rnd_pixels_tab[1][motion_halfpel_index](
                                output_plane + s->all_fragments[i].first_pixel,
                                motion_source, stride, 8);
                        }else{
                            int d= (motion_x ^ motion_y)>>31; // d is 0 if motion_x and _y have the same sign, else -1
                            s->dsp.put_no_rnd_pixels_l2[1](
                                output_plane + s->all_fragments[i].first_pixel,
                                motion_source - d,
                                motion_source + stride + 1 + d,
                                stride, 8);
                        }
                        dequantizer = s->qmat[1][plane];
                    }else{
                        dequantizer = s->qmat[0][plane];
                    }

                    /* dequantize the DCT coefficients */
                    if(s->avctx->idct_algo==FF_IDCT_VP3){
                        Coeff *coeff= s->coeffs + i;
                        s->dsp.clear_block(block);
                        while(coeff->next){
                            block[coeff->index]= coeff->coeff * dequantizer[coeff->index];
                            coeff= coeff->next;
                        }
                    }else{
                        Coeff *coeff= s->coeffs + i;
                        s->dsp.clear_block(block);
                        while(coeff->next){
                            block[coeff->index]= (coeff->coeff * dequantizer[coeff->index] + 2)>>2;
                            coeff= coeff->next;
                        }
                    }

                    /* invert DCT and place (or add) in final output */

                    if (s->all_fragments[i].coding_method == MODE_INTRA) {
                        if(s->avctx->idct_algo!=FF_IDCT_VP3)
                            block[0] += 128<<3;
                        s->dsp.idct_put(
                            output_plane + s->all_fragments[i].first_pixel,
                            stride,
                            block);
                    } else {
                        s->dsp.idct_add(
                            output_plane + s->all_fragments[i].first_pixel,
                            stride,
                            block);
                    }
                } else {

                    /* copy directly from the previous frame */
                    s->dsp.put_pixels_tab[1][0](
                        output_plane + s->all_fragments[i].first_pixel,
                        last_plane + s->all_fragments[i].first_pixel,
                        stride, 8);

                }
#if 0
                /* perform the left edge filter if:
                 *   - the fragment is not on the left column
                 *   - the fragment is coded in this frame
                 *   - the fragment is not coded in this frame but the left
                 *     fragment is coded in this frame (this is done instead
                 *     of a right edge filter when rendering the left fragment
                 *     since this fragment is not available yet) */
                if ((x > 0) &&
                    ((s->all_fragments[i].coding_method != MODE_COPY) ||
                     ((s->all_fragments[i].coding_method == MODE_COPY) &&
                      (s->all_fragments[i - 1].coding_method != MODE_COPY)) )) {
                    horizontal_filter(
                        output_plane + s->all_fragments[i].first_pixel + 7*stride,
                        -stride, s->bounding_values_array + 127);
                }

                /* perform the top edge filter if:
                 *   - the fragment is not on the top row
                 *   - the fragment is coded in this frame
                 *   - the fragment is not coded in this frame but the above
                 *     fragment is coded in this frame (this is done instead
                 *     of a bottom edge filter when rendering the above
                 *     fragment since this fragment is not available yet) */
                if ((y > 0) &&
                    ((s->all_fragments[i].coding_method != MODE_COPY) ||
                     ((s->all_fragments[i].coding_method == MODE_COPY) &&
                      (s->all_fragments[i - fragment_width].coding_method != MODE_COPY)) )) {
                    vertical_filter(
                        output_plane + s->all_fragments[i].first_pixel - stride,
                        -stride, s->bounding_values_array + 127);
                }
#endif
            }
        }
    }

     /* this looks like a good place for slice dispatch... */
     /* algorithm:
      *   if (slice == s->macroblock_height - 1)
      *     dispatch (both last slice & 2nd-to-last slice);
      *   else if (slice > 0)
      *     dispatch (slice - 1);
      */

    emms_c();
}

static void apply_loop_filter(Vp3DecodeContext *s)
{
    int plane;
    int x, y;
    int *bounding_values= s->bounding_values_array+127;

#if 0
    int bounding_values_array[256];
    int filter_limit;

    /* find the right loop limit value */
    for (x = 63; x >= 0; x--) {
        if (vp31_ac_scale_factor[x] >= s->quality_index)
            break;
    }
    filter_limit = vp31_filter_limit_values[s->quality_index];

    /* set up the bounding values */
    memset(bounding_values_array, 0, 256 * sizeof(int));
    for (x = 0; x < filter_limit; x++) {
        bounding_values[-x - filter_limit] = -filter_limit + x;
        bounding_values[-x] = -x;
        bounding_values[x] = x;
        bounding_values[x + filter_limit] = filter_limit - x;
    }
#endif

    for (plane = 0; plane < 3; plane++) {
        int width           = s->fragment_width  >> !!plane;
        int height          = s->fragment_height >> !!plane;
        int fragment        = s->fragment_start        [plane];
        int stride          = s->current_frame.linesize[plane];
        uint8_t *plane_data = s->current_frame.data    [plane];
        if (!s->flipped_image) stride = -stride;

        for (y = 0; y < height; y++) {

            for (x = 0; x < width; x++) {
                /* do not perform left edge filter for left columns frags */
                if ((x > 0) &&
                    (s->all_fragments[fragment].coding_method != MODE_COPY)) {
                    s->dsp.vp3_h_loop_filter(
                        plane_data + s->all_fragments[fragment].first_pixel,
                        stride, bounding_values);
                }

                /* do not perform top edge filter for top row fragments */
                if ((y > 0) &&
                    (s->all_fragments[fragment].coding_method != MODE_COPY)) {
                    s->dsp.vp3_v_loop_filter(
                        plane_data + s->all_fragments[fragment].first_pixel,
                        stride, bounding_values);
                }

                /* do not perform right edge filter for right column
                 * fragments or if right fragment neighbor is also coded
                 * in this frame (it will be filtered in next iteration) */
                if ((x < width - 1) &&
                    (s->all_fragments[fragment].coding_method != MODE_COPY) &&
                    (s->all_fragments[fragment + 1].coding_method == MODE_COPY)) {
                    s->dsp.vp3_h_loop_filter(
                        plane_data + s->all_fragments[fragment + 1].first_pixel,
                        stride, bounding_values);
                }

                /* do not perform bottom edge filter for bottom row
                 * fragments or if bottom fragment neighbor is also coded
                 * in this frame (it will be filtered in the next row) */
                if ((y < height - 1) &&
                    (s->all_fragments[fragment].coding_method != MODE_COPY) &&
                    (s->all_fragments[fragment + width].coding_method == MODE_COPY)) {
                    s->dsp.vp3_v_loop_filter(
                        plane_data + s->all_fragments[fragment + width].first_pixel,
                        stride, bounding_values);
                }

                fragment++;
            }
        }
    }
}

/*
 * This function computes the first pixel addresses for each fragment.
 * This function needs to be invoked after the first frame is allocated
 * so that it has access to the plane strides.
 */
static void vp3_calculate_pixel_addresses(Vp3DecodeContext *s)
{
#define Y_INITIAL(chroma_shift)  s->flipped_image ? 1  : s->fragment_height >> chroma_shift
#define Y_FINISHED(chroma_shift) s->flipped_image ? y <= s->fragment_height >> chroma_shift : y > 0

    int i, x, y;
    const int y_inc = s->flipped_image ? 1 : -1;

    /* figure out the first pixel addresses for each of the fragments */
    /* Y plane */
    i = 0;
    for (y = Y_INITIAL(0); Y_FINISHED(0); y += y_inc) {
        for (x = 0; x < s->fragment_width; x++) {
            s->all_fragments[i++].first_pixel =
                s->golden_frame.linesize[0] * y * FRAGMENT_PIXELS -
                    s->golden_frame.linesize[0] +
                    x * FRAGMENT_PIXELS;
        }
    }

    /* U plane */
    i = s->fragment_start[1];
    for (y = Y_INITIAL(1); Y_FINISHED(1); y += y_inc) {
        for (x = 0; x < s->fragment_width / 2; x++) {
            s->all_fragments[i++].first_pixel =
                s->golden_frame.linesize[1] * y * FRAGMENT_PIXELS -
                    s->golden_frame.linesize[1] +
                    x * FRAGMENT_PIXELS;
        }
    }

    /* V plane */
    i = s->fragment_start[2];
    for (y = Y_INITIAL(1); Y_FINISHED(1); y += y_inc) {
        for (x = 0; x < s->fragment_width / 2; x++) {
            s->all_fragments[i++].first_pixel =
                s->golden_frame.linesize[2] * y * FRAGMENT_PIXELS -
                    s->golden_frame.linesize[2] +
                    x * FRAGMENT_PIXELS;
        }
    }
}

/*
 * This is the ffmpeg/libavcodec API init function.
 */
static av_cold int vp3_decode_init(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int i, inter, plane;
    int c_width;
    int c_height;
    int y_superblock_count;
    int c_superblock_count;

    if (avctx->codec_tag == MKTAG('V','P','3','0'))
        s->version = 0;
    else
        s->version = 1;

    s->avctx = avctx;
    s->width = (avctx->width + 15) & 0xFFFFFFF0;
    s->height = (avctx->height + 15) & 0xFFFFFFF0;
    avctx->pix_fmt = PIX_FMT_YUV420P;
    if(avctx->idct_algo==FF_IDCT_AUTO)
        avctx->idct_algo=FF_IDCT_VP3;
    dsputil_init(&s->dsp, avctx);

    ff_init_scantable(s->dsp.idct_permutation, &s->scantable, ff_zigzag_direct);

    /* initialize to an impossible value which will force a recalculation
     * in the first frame decode */
    s->quality_index = -1;

    s->y_superblock_width = (s->width + 31) / 32;
    s->y_superblock_height = (s->height + 31) / 32;
    y_superblock_count = s->y_superblock_width * s->y_superblock_height;

    /* work out the dimensions for the C planes */
    c_width = s->width / 2;
    c_height = s->height / 2;
    s->c_superblock_width = (c_width + 31) / 32;
    s->c_superblock_height = (c_height + 31) / 32;
    c_superblock_count = s->c_superblock_width * s->c_superblock_height;

    s->superblock_count = y_superblock_count + (c_superblock_count * 2);
    s->u_superblock_start = y_superblock_count;
    s->v_superblock_start = s->u_superblock_start + c_superblock_count;
    s->superblock_coding = av_malloc(s->superblock_count);

    s->macroblock_width = (s->width + 15) / 16;
    s->macroblock_height = (s->height + 15) / 16;
    s->macroblock_count = s->macroblock_width * s->macroblock_height;

    s->fragment_width = s->width / FRAGMENT_PIXELS;
    s->fragment_height = s->height / FRAGMENT_PIXELS;

    /* fragment count covers all 8x8 blocks for all 3 planes */
    s->fragment_count = s->fragment_width * s->fragment_height * 3 / 2;
    s->fragment_start[1] = s->fragment_width * s->fragment_height;
    s->fragment_start[2] = s->fragment_width * s->fragment_height * 5 / 4;

    s->all_fragments = av_malloc(s->fragment_count * sizeof(Vp3Fragment));
    s->coeff_counts = av_malloc(s->fragment_count * sizeof(*s->coeff_counts));
    s->coeffs = av_malloc(s->fragment_count * sizeof(Coeff) * 65);
    s->coded_fragment_list = av_malloc(s->fragment_count * sizeof(int));
    s->pixel_addresses_initialized = 0;

    if (!s->theora_tables)
    {
        for (i = 0; i < 64; i++) {
            s->coded_dc_scale_factor[i] = vp31_dc_scale_factor[i];
            s->coded_ac_scale_factor[i] = vp31_ac_scale_factor[i];
            s->base_matrix[0][i] = vp31_intra_y_dequant[i];
            s->base_matrix[1][i] = vp31_intra_c_dequant[i];
            s->base_matrix[2][i] = vp31_inter_dequant[i];
            s->filter_limit_values[i] = vp31_filter_limit_values[i];
        }

        for(inter=0; inter<2; inter++){
            for(plane=0; plane<3; plane++){
                s->qr_count[inter][plane]= 1;
                s->qr_size [inter][plane][0]= 63;
                s->qr_base [inter][plane][0]=
                s->qr_base [inter][plane][1]= 2*inter + (!!plane)*!inter;
            }
        }

        /* init VLC tables */
        for (i = 0; i < 16; i++) {

            /* DC histograms */
            init_vlc(&s->dc_vlc[i], 5, 32,
                &dc_bias[i][0][1], 4, 2,
                &dc_bias[i][0][0], 4, 2, 0);

            /* group 1 AC histograms */
            init_vlc(&s->ac_vlc_1[i], 5, 32,
                &ac_bias_0[i][0][1], 4, 2,
                &ac_bias_0[i][0][0], 4, 2, 0);

            /* group 2 AC histograms */
            init_vlc(&s->ac_vlc_2[i], 5, 32,
                &ac_bias_1[i][0][1], 4, 2,
                &ac_bias_1[i][0][0], 4, 2, 0);

            /* group 3 AC histograms */
            init_vlc(&s->ac_vlc_3[i], 5, 32,
                &ac_bias_2[i][0][1], 4, 2,
                &ac_bias_2[i][0][0], 4, 2, 0);

            /* group 4 AC histograms */
            init_vlc(&s->ac_vlc_4[i], 5, 32,
                &ac_bias_3[i][0][1], 4, 2,
                &ac_bias_3[i][0][0], 4, 2, 0);
        }
    } else {
        for (i = 0; i < 16; i++) {

            /* DC histograms */
            init_vlc(&s->dc_vlc[i], 5, 32,
                &s->huffman_table[i][0][1], 4, 2,
                &s->huffman_table[i][0][0], 4, 2, 0);

            /* group 1 AC histograms */
            init_vlc(&s->ac_vlc_1[i], 5, 32,
                &s->huffman_table[i+16][0][1], 4, 2,
                &s->huffman_table[i+16][0][0], 4, 2, 0);

            /* group 2 AC histograms */
            init_vlc(&s->ac_vlc_2[i], 5, 32,
                &s->huffman_table[i+16*2][0][1], 4, 2,
                &s->huffman_table[i+16*2][0][0], 4, 2, 0);

            /* group 3 AC histograms */
            init_vlc(&s->ac_vlc_3[i], 5, 32,
                &s->huffman_table[i+16*3][0][1], 4, 2,
                &s->huffman_table[i+16*3][0][0], 4, 2, 0);

            /* group 4 AC histograms */
            init_vlc(&s->ac_vlc_4[i], 5, 32,
                &s->huffman_table[i+16*4][0][1], 4, 2,
                &s->huffman_table[i+16*4][0][0], 4, 2, 0);
        }
    }

    init_vlc(&s->superblock_run_length_vlc, 6, 34,
        &superblock_run_length_vlc_table[0][1], 4, 2,
        &superblock_run_length_vlc_table[0][0], 4, 2, 0);

    init_vlc(&s->fragment_run_length_vlc, 5, 30,
        &fragment_run_length_vlc_table[0][1], 4, 2,
        &fragment_run_length_vlc_table[0][0], 4, 2, 0);

    init_vlc(&s->mode_code_vlc, 3, 8,
        &mode_code_vlc_table[0][1], 2, 1,
        &mode_code_vlc_table[0][0], 2, 1, 0);

    init_vlc(&s->motion_vector_vlc, 6, 63,
        &motion_vector_vlc_table[0][1], 2, 1,
        &motion_vector_vlc_table[0][0], 2, 1, 0);

    /* work out the block mapping tables */
    s->superblock_fragments = av_malloc(s->superblock_count * 16 * sizeof(int));
    s->superblock_macroblocks = av_malloc(s->superblock_count * 4 * sizeof(int));
    s->macroblock_fragments = av_malloc(s->macroblock_count * 6 * sizeof(int));
    s->macroblock_coding = av_malloc(s->macroblock_count + 1);
    init_block_mapping(s);

    for (i = 0; i < 3; i++) {
        s->current_frame.data[i] = NULL;
        s->last_frame.data[i] = NULL;
        s->golden_frame.data[i] = NULL;
    }

    return 0;
}

/*
 * This is the ffmpeg/libavcodec API frame decode function.
 */
static int vp3_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    Vp3DecodeContext *s = avctx->priv_data;
    GetBitContext gb;
    static int counter = 0;
    int i;

    init_get_bits(&gb, buf, buf_size * 8);

    if (s->theora && get_bits1(&gb))
    {
        av_log(avctx, AV_LOG_ERROR, "Header packet passed to frame decoder, skipping\n");
        return -1;
    }

    s->keyframe = !get_bits1(&gb);
    if (!s->theora)
        skip_bits(&gb, 1);
    s->last_quality_index = s->quality_index;

    s->nqis=0;
    do{
        s->qis[s->nqis++]= get_bits(&gb, 6);
    } while(s->theora >= 0x030200 && s->nqis<3 && get_bits1(&gb));

    s->quality_index= s->qis[0];

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_INFO, " VP3 %sframe #%d: Q index = %d\n",
            s->keyframe?"key":"", counter, s->quality_index);
    counter++;

    if (s->quality_index != s->last_quality_index) {
        init_dequantizer(s);
        init_loop_filter(s);
    }

    if (avctx->skip_frame >= AVDISCARD_NONKEY && !s->keyframe)
        return buf_size;

    if (s->keyframe) {
        if (!s->theora)
        {
            skip_bits(&gb, 4); /* width code */
            skip_bits(&gb, 4); /* height code */
            if (s->version)
            {
                s->version = get_bits(&gb, 5);
                if (counter == 1)
                    av_log(s->avctx, AV_LOG_DEBUG, "VP version: %d\n", s->version);
            }
        }
        if (s->version || s->theora)
        {
                if (get_bits1(&gb))
                    av_log(s->avctx, AV_LOG_ERROR, "Warning, unsupported keyframe coding type?!\n");
            skip_bits(&gb, 2); /* reserved? */
        }

        if (s->last_frame.data[0] == s->golden_frame.data[0]) {
            if (s->golden_frame.data[0])
                avctx->release_buffer(avctx, &s->golden_frame);
            s->last_frame= s->golden_frame; /* ensure that we catch any access to this released frame */
        } else {
            if (s->golden_frame.data[0])
                avctx->release_buffer(avctx, &s->golden_frame);
            if (s->last_frame.data[0])
                avctx->release_buffer(avctx, &s->last_frame);
        }

        s->golden_frame.reference = 3;
        if(avctx->get_buffer(avctx, &s->golden_frame) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "vp3: get_buffer() failed\n");
            return -1;
        }

        /* golden frame is also the current frame */
        s->current_frame= s->golden_frame;

        /* time to figure out pixel addresses? */
        if (!s->pixel_addresses_initialized)
        {
            vp3_calculate_pixel_addresses(s);
            s->pixel_addresses_initialized = 1;
        }
    } else {
        /* allocate a new current frame */
        s->current_frame.reference = 3;
        if (!s->pixel_addresses_initialized) {
            av_log(s->avctx, AV_LOG_ERROR, "vp3: first frame not a keyframe\n");
            return -1;
        }
        if(avctx->get_buffer(avctx, &s->current_frame) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "vp3: get_buffer() failed\n");
            return -1;
        }
    }

    s->current_frame.qscale_table= s->qscale_table; //FIXME allocate individual tables per AVFrame
    s->current_frame.qstride= 0;

    init_frame(s, &gb);

    if (unpack_superblocks(s, &gb)){
        av_log(s->avctx, AV_LOG_ERROR, "error in unpack_superblocks\n");
        return -1;
    }
    if (unpack_modes(s, &gb)){
        av_log(s->avctx, AV_LOG_ERROR, "error in unpack_modes\n");
        return -1;
    }
    if (unpack_vectors(s, &gb)){
        av_log(s->avctx, AV_LOG_ERROR, "error in unpack_vectors\n");
        return -1;
    }
    if (unpack_dct_coeffs(s, &gb)){
        av_log(s->avctx, AV_LOG_ERROR, "error in unpack_dct_coeffs\n");
        return -1;
    }

    reverse_dc_prediction(s, 0, s->fragment_width, s->fragment_height);
    if ((avctx->flags & CODEC_FLAG_GRAY) == 0) {
        reverse_dc_prediction(s, s->fragment_start[1],
            s->fragment_width / 2, s->fragment_height / 2);
        reverse_dc_prediction(s, s->fragment_start[2],
            s->fragment_width / 2, s->fragment_height / 2);
    }

    for (i = 0; i < s->macroblock_height; i++)
        render_slice(s, i);

    apply_loop_filter(s);

    *data_size=sizeof(AVFrame);
    *(AVFrame*)data= s->current_frame;

    /* release the last frame, if it is allocated and if it is not the
     * golden frame */
    if ((s->last_frame.data[0]) &&
        (s->last_frame.data[0] != s->golden_frame.data[0]))
        avctx->release_buffer(avctx, &s->last_frame);

    /* shuffle frames (last = current) */
    s->last_frame= s->current_frame;
    s->current_frame.data[0]= NULL; /* ensure that we catch any access to this released frame */

    return buf_size;
}

/*
 * This is the ffmpeg/libavcodec API module cleanup function.
 */
static av_cold int vp3_decode_end(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int i;

    av_free(s->superblock_coding);
    av_free(s->all_fragments);
    av_free(s->coeff_counts);
    av_free(s->coeffs);
    av_free(s->coded_fragment_list);
    av_free(s->superblock_fragments);
    av_free(s->superblock_macroblocks);
    av_free(s->macroblock_fragments);
    av_free(s->macroblock_coding);

    for (i = 0; i < 16; i++) {
        free_vlc(&s->dc_vlc[i]);
        free_vlc(&s->ac_vlc_1[i]);
        free_vlc(&s->ac_vlc_2[i]);
        free_vlc(&s->ac_vlc_3[i]);
        free_vlc(&s->ac_vlc_4[i]);
    }

    free_vlc(&s->superblock_run_length_vlc);
    free_vlc(&s->fragment_run_length_vlc);
    free_vlc(&s->mode_code_vlc);
    free_vlc(&s->motion_vector_vlc);

    /* release all frames */
    if (s->golden_frame.data[0] && s->golden_frame.data[0] != s->last_frame.data[0])
        avctx->release_buffer(avctx, &s->golden_frame);
    if (s->last_frame.data[0])
        avctx->release_buffer(avctx, &s->last_frame);
    /* no need to release the current_frame since it will always be pointing
     * to the same frame as either the golden or last frame */

    return 0;
}

static int read_huffman_tree(AVCodecContext *avctx, GetBitContext *gb)
{
    Vp3DecodeContext *s = avctx->priv_data;

    if (get_bits1(gb)) {
        int token;
        if (s->entries >= 32) { /* overflow */
            av_log(avctx, AV_LOG_ERROR, "huffman tree overflow\n");
            return -1;
        }
        token = get_bits(gb, 5);
        //av_log(avctx, AV_LOG_DEBUG, "hti %d hbits %x token %d entry : %d size %d\n", s->hti, s->hbits, token, s->entries, s->huff_code_size);
        s->huffman_table[s->hti][token][0] = s->hbits;
        s->huffman_table[s->hti][token][1] = s->huff_code_size;
        s->entries++;
    }
    else {
        if (s->huff_code_size >= 32) {/* overflow */
            av_log(avctx, AV_LOG_ERROR, "huffman tree overflow\n");
            return -1;
        }
        s->huff_code_size++;
        s->hbits <<= 1;
        read_huffman_tree(avctx, gb);
        s->hbits |= 1;
        read_huffman_tree(avctx, gb);
        s->hbits >>= 1;
        s->huff_code_size--;
    }
    return 0;
}

#if CONFIG_THEORA_DECODER
static int theora_decode_header(AVCodecContext *avctx, GetBitContext *gb)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int visible_width, visible_height;

    s->theora = get_bits_long(gb, 24);
    av_log(avctx, AV_LOG_DEBUG, "Theora bitstream version %X\n", s->theora);

    /* 3.2.0 aka alpha3 has the same frame orientation as original vp3 */
    /* but previous versions have the image flipped relative to vp3 */
    if (s->theora < 0x030200)
    {
        s->flipped_image = 1;
        av_log(avctx, AV_LOG_DEBUG, "Old (<alpha3) Theora bitstream, flipped image\n");
    }

    visible_width  = s->width  = get_bits(gb, 16) << 4;
    visible_height = s->height = get_bits(gb, 16) << 4;

    if(avcodec_check_dimensions(avctx, s->width, s->height)){
        av_log(avctx, AV_LOG_ERROR, "Invalid dimensions (%dx%d)\n", s->width, s->height);
        s->width= s->height= 0;
        return -1;
    }

    if (s->theora >= 0x030400)
    {
        skip_bits(gb, 32); /* total number of superblocks in a frame */
        // fixme, the next field is 36bits long
        skip_bits(gb, 32); /* total number of blocks in a frame */
        skip_bits(gb, 4); /* total number of blocks in a frame */
        skip_bits(gb, 32); /* total number of macroblocks in a frame */
    }

    if (s->theora >= 0x030200) {
        visible_width  = get_bits_long(gb, 24);
        visible_height = get_bits_long(gb, 24);

        skip_bits(gb, 8); /* offset x */
        skip_bits(gb, 8); /* offset y */
    }

    skip_bits(gb, 32); /* fps numerator */
    skip_bits(gb, 32); /* fps denumerator */
    skip_bits(gb, 24); /* aspect numerator */
    skip_bits(gb, 24); /* aspect denumerator */

    if (s->theora < 0x030200)
        skip_bits(gb, 5); /* keyframe frequency force */
    skip_bits(gb, 8); /* colorspace */
    if (s->theora >= 0x030400)
        skip_bits(gb, 2); /* pixel format: 420,res,422,444 */
    skip_bits(gb, 24); /* bitrate */

    skip_bits(gb, 6); /* quality hint */

    if (s->theora >= 0x030200)
    {
        skip_bits(gb, 5); /* keyframe frequency force */

        if (s->theora < 0x030400)
            skip_bits(gb, 5); /* spare bits */
    }

//    align_get_bits(gb);

    if (   visible_width  <= s->width  && visible_width  > s->width-16
        && visible_height <= s->height && visible_height > s->height-16)
        avcodec_set_dimensions(avctx, visible_width, visible_height);
    else
        avcodec_set_dimensions(avctx, s->width, s->height);

    return 0;
}

static int theora_decode_tables(AVCodecContext *avctx, GetBitContext *gb)
{
    Vp3DecodeContext *s = avctx->priv_data;
    int i, n, matrices, inter, plane;

    if (s->theora >= 0x030200) {
        n = get_bits(gb, 3);
        /* loop filter limit values table */
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
        s->coded_dc_scale_factor[i] = get_bits(gb, n);

    if (s->theora >= 0x030200)
        matrices = get_bits(gb, 9) + 1;
    else
        matrices = 3;

    if(matrices > 384){
        av_log(avctx, AV_LOG_ERROR, "invalid number of base matrixes\n");
        return -1;
    }

    for(n=0; n<matrices; n++){
        for (i = 0; i < 64; i++)
            s->base_matrix[n][i]= get_bits(gb, 8);
    }

    for (inter = 0; inter <= 1; inter++) {
        for (plane = 0; plane <= 2; plane++) {
            int newqr= 1;
            if (inter || plane > 0)
                newqr = get_bits1(gb);
            if (!newqr) {
                int qtj, plj;
                if(inter && get_bits1(gb)){
                    qtj = 0;
                    plj = plane;
                }else{
                    qtj= (3*inter + plane - 1) / 3;
                    plj= (plane + 2) % 3;
                }
                s->qr_count[inter][plane]= s->qr_count[qtj][plj];
                memcpy(s->qr_size[inter][plane], s->qr_size[qtj][plj], sizeof(s->qr_size[0][0]));
                memcpy(s->qr_base[inter][plane], s->qr_base[qtj][plj], sizeof(s->qr_base[0][0]));
            } else {
                int qri= 0;
                int qi = 0;

                for(;;){
                    i= get_bits(gb, av_log2(matrices-1)+1);
                    if(i>= matrices){
                        av_log(avctx, AV_LOG_ERROR, "invalid base matrix index\n");
                        return -1;
                    }
                    s->qr_base[inter][plane][qri]= i;
                    if(qi >= 63)
                        break;
                    i = get_bits(gb, av_log2(63-qi)+1) + 1;
                    s->qr_size[inter][plane][qri++]= i;
                    qi += i;
                }

                if (qi > 63) {
                    av_log(avctx, AV_LOG_ERROR, "invalid qi %d > 63\n", qi);
                    return -1;
                }
                s->qr_count[inter][plane]= qri;
            }
        }
    }

    /* Huffman tables */
    for (s->hti = 0; s->hti < 80; s->hti++) {
        s->entries = 0;
        s->huff_code_size = 1;
        if (!get_bits1(gb)) {
            s->hbits = 0;
            read_huffman_tree(avctx, gb);
            s->hbits = 1;
            read_huffman_tree(avctx, gb);
        }
    }

    s->theora_tables = 1;

    return 0;
}

static int theora_decode_init(AVCodecContext *avctx)
{
    Vp3DecodeContext *s = avctx->priv_data;
    GetBitContext gb;
    int ptype;
    uint8_t *header_start[3];
    int header_len[3];
    int i;

    s->theora = 1;

    if (!avctx->extradata_size)
    {
        av_log(avctx, AV_LOG_ERROR, "Missing extradata!\n");
        return -1;
    }

    if (ff_split_xiph_headers(avctx->extradata, avctx->extradata_size,
                              42, header_start, header_len) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Corrupt extradata\n");
        return -1;
    }

  for(i=0;i<3;i++) {
    init_get_bits(&gb, header_start[i], header_len[i]);

    ptype = get_bits(&gb, 8);

     if (!(ptype & 0x80))
     {
        av_log(avctx, AV_LOG_ERROR, "Invalid extradata!\n");
//        return -1;
     }

    // FIXME: Check for this as well.
    skip_bits(&gb, 6*8); /* "theora" */

    switch(ptype)
    {
        case 0x80:
            theora_decode_header(avctx, &gb);
                break;
        case 0x81:
// FIXME: is this needed? it breaks sometimes
//            theora_decode_comments(avctx, gb);
            break;
        case 0x82:
            theora_decode_tables(avctx, &gb);
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unknown Theora config packet: %d\n", ptype&~0x80);
            break;
    }
    if(ptype != 0x81 && 8*header_len[i] != get_bits_count(&gb))
        av_log(avctx, AV_LOG_WARNING, "%d bits left in packet %X\n", 8*header_len[i] - get_bits_count(&gb), ptype);
    if (s->theora < 0x030200)
        break;
  }

    vp3_decode_init(avctx);
    return 0;
}

AVCodec theora_decoder = {
    "theora",
    CODEC_TYPE_VIDEO,
    CODEC_ID_THEORA,
    sizeof(Vp3DecodeContext),
    theora_decode_init,
    NULL,
    vp3_decode_end,
    vp3_decode_frame,
    0,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("Theora"),
};
#endif

AVCodec vp3_decoder = {
    "vp3",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VP3,
    sizeof(Vp3DecodeContext),
    vp3_decode_init,
    NULL,
    vp3_decode_end,
    vp3_decode_frame,
    0,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("On2 VP3"),
};
