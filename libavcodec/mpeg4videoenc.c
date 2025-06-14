/*
 * MPEG-4 encoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2010 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/attributes.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "codec_internal.h"
#include "mpegvideo.h"
#include "h263.h"
#include "h263enc.h"
#include "mpeg4video.h"
#include "mpeg4videodata.h"
#include "mpeg4videodefs.h"
#include "mpeg4videoenc.h"
#include "mpegvideoenc.h"
#include "profiles.h"
#include "put_bits.h"
#include "version.h"

/**
 * Minimal fcode that a motion vector component would need.
 */
static uint8_t fcode_tab[MAX_MV*2+1];

/* The uni_DCtab_* tables below contain unified bits+length tables to encode DC
 * differences in MPEG-4. Unified in the sense that the specification specifies
 * this encoding in several steps. */
static uint8_t  uni_DCtab_lum_len[512];
static uint8_t  uni_DCtab_chrom_len[512];
static uint16_t uni_DCtab_lum_bits[512];
static uint16_t uni_DCtab_chrom_bits[512];

/* Unified encoding tables for run length encoding of coefficients.
 * Unified in the sense that the specification specifies the encoding in several steps. */
static uint32_t uni_mpeg4_intra_rl_bits[64 * 64 * 2 * 2];
static uint8_t  uni_mpeg4_intra_rl_len[64 * 64 * 2 * 2];
static uint32_t uni_mpeg4_inter_rl_bits[64 * 64 * 2 * 2];
static uint8_t  uni_mpeg4_inter_rl_len[64 * 64 * 2 * 2];

//#define UNI_MPEG4_ENC_INDEX(last, run, level) ((last) * 128 + (run) * 256 + (level))
//#define UNI_MPEG4_ENC_INDEX(last, run, level) ((last) * 128 * 64 + (run) + (level) * 64)
#define UNI_MPEG4_ENC_INDEX(last, run, level) ((last) * 128 * 64 + (run) * 128 + (level))

/* MPEG-4
 * inter
 * max level: 24/6
 * max run: 53/63
 *
 * intra
 * max level: 53/16
 * max run: 29/41
 */

typedef struct Mpeg4EncContext {
    MPVMainEncContext m;
    /// number of bits to represent the fractional part of time
    int time_increment_bits;
} Mpeg4EncContext;

static inline Mpeg4EncContext *mainctx_to_mpeg4(MPVMainEncContext *m)
{
    return (Mpeg4EncContext*)m;
}

/**
 * Return the number of bits that encoding the 8x8 block in block would need.
 * @param[in]  block_last_index last index in scantable order that refers to a non zero element in block.
 */
static inline int get_block_rate(MPVEncContext *const s, int16_t block[64],
                                 int block_last_index, const uint8_t scantable[64])
{
    int last = 0;
    int j;
    int rate = 0;

    for (j = 1; j <= block_last_index; j++) {
        const int index = scantable[j];
        int level = block[index];
        if (level) {
            level += 64;
            if ((level & (~127)) == 0) {
                if (j < block_last_index)
                    rate += s->intra_ac_vlc_length[UNI_AC_ENC_INDEX(j - last - 1, level)];
                else
                    rate += s->intra_ac_vlc_last_length[UNI_AC_ENC_INDEX(j - last - 1, level)];
            } else
                rate += s->ac_esc_length;

            last = j;
        }
    }

    return rate;
}

/**
 * Restore the ac coefficients in block that have been changed by decide_ac_pred().
 * This function also restores s->c.block_last_index.
 * @param[in,out] block MB coefficients, these will be restored
 * @param[in] dir ac prediction direction for each 8x8 block
 * @param[out] st scantable for each 8x8 block
 * @param[in] zigzag_last_index index referring to the last non zero coefficient in zigzag order
 */
static inline void restore_ac_coeffs(MPVEncContext *const s, int16_t block[6][64],
                                     const int dir[6], const uint8_t *st[6],
                                     const int zigzag_last_index[6])
{
    int i, n;
    memcpy(s->c.block_last_index, zigzag_last_index, sizeof(int) * 6);

    for (n = 0; n < 6; n++) {
        int16_t *ac_val = &s->c.ac_val[0][0] + s->c.block_index[n] * 16;

        st[n] = s->c.intra_scantable.permutated;
        if (dir[n]) {
            /* top prediction */
            for (i = 1; i < 8; i++)
                block[n][s->c.idsp.idct_permutation[i]] = ac_val[i + 8];
        } else {
            /* left prediction */
            for (i = 1; i < 8; i++)
                block[n][s->c.idsp.idct_permutation[i << 3]] = ac_val[i];
        }
    }
}

/**
 * Predict the dc.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dir_ptr pointer to an integer where the prediction direction will be stored
 */
static int mpeg4_pred_dc(MpegEncContext *s, int n, int *dir_ptr)
{
    const int16_t *const dc_val = s->dc_val + s->block_index[n];
    const int wrap = s->block_wrap[n];

    /* B C
     * A X
     */
    const int a = dc_val[-1];
    const int b = dc_val[-1 - wrap];
    const int c = dc_val[-wrap];
    int pred;

    // There is no need for out-of-slice handling here, as all values are set
    // appropriately when a new slice is opened.
    if (abs(a - b) < abs(b - c)) {
        pred     = c;
        *dir_ptr = 1; /* top */
    } else {
        pred     = a;
        *dir_ptr = 0; /* left */
    }
    return pred;
}

/**
 * Return the optimal value (0 or 1) for the ac_pred element for the given MB in MPEG-4.
 * This function will also update s->c.block_last_index and s->c.ac_val.
 * @param[in,out] block MB coefficients, these will be updated if 1 is returned
 * @param[in] dir ac prediction direction for each 8x8 block
 * @param[out] st scantable for each 8x8 block
 * @param[out] zigzag_last_index index referring to the last non zero coefficient in zigzag order
 */
static inline int decide_ac_pred(MPVEncContext *const s, int16_t block[6][64],
                                 const int dir[6], const uint8_t *st[6],
                                 int zigzag_last_index[6])
{
    int score = 0;
    int i, n;
    const int8_t *const qscale_table = s->c.cur_pic.qscale_table;

    memcpy(zigzag_last_index, s->c.block_last_index, sizeof(int) * 6);

    for (n = 0; n < 6; n++) {
        int16_t *ac_val, *ac_val1;

        score -= get_block_rate(s, block[n], s->c.block_last_index[n],
                                s->c.intra_scantable.permutated);

        ac_val  = &s->c.ac_val[0][0] + s->c.block_index[n] * 16;
        ac_val1 = ac_val;
        if (dir[n]) {
            const int xy = s->c.mb_x + s->c.mb_y * s->c.mb_stride - s->c.mb_stride;
            /* top prediction */
            ac_val -= s->c.block_wrap[n] * 16;
            if (s->c.first_slice_line || s->c.qscale == qscale_table[xy] || n == 2 || n == 3) {
                /* same qscale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->c.idsp.idct_permutation[i]];
                    block[n][s->c.idsp.idct_permutation[i]] = level - ac_val[i + 8];
                    ac_val1[i]     = block[n][s->c.idsp.idct_permutation[i << 3]];
                    ac_val1[i + 8] = level;
                }
            } else {
                /* different qscale, we must rescale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->c.idsp.idct_permutation[i]];
                    block[n][s->c.idsp.idct_permutation[i]] = level - ROUNDED_DIV(ac_val[i + 8] * qscale_table[xy], s->c.qscale);
                    ac_val1[i]     = block[n][s->c.idsp.idct_permutation[i << 3]];
                    ac_val1[i + 8] = level;
                }
            }
            st[n] = s->c.permutated_intra_h_scantable;
        } else {
            const int xy = s->c.mb_x - 1 + s->c.mb_y * s->c.mb_stride;
            /* left prediction */
            ac_val -= 16;
            if (s->c.mb_x == 0 || s->c.qscale == qscale_table[xy] || n == 1 || n == 3) {
                /* same qscale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->c.idsp.idct_permutation[i << 3]];
                    block[n][s->c.idsp.idct_permutation[i << 3]] = level - ac_val[i];
                    ac_val1[i]     = level;
                    ac_val1[i + 8] = block[n][s->c.idsp.idct_permutation[i]];
                }
            } else {
                /* different qscale, we must rescale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->c.idsp.idct_permutation[i << 3]];
                    block[n][s->c.idsp.idct_permutation[i << 3]] = level - ROUNDED_DIV(ac_val[i] * qscale_table[xy], s->c.qscale);
                    ac_val1[i]     = level;
                    ac_val1[i + 8] = block[n][s->c.idsp.idct_permutation[i]];
                }
            }
            st[n] = s->c.permutated_intra_v_scantable;
        }

        for (i = 63; i > 0; i--)  // FIXME optimize
            if (block[n][st[n][i]])
                break;
        s->c.block_last_index[n] = i;

        score += get_block_rate(s, block[n], s->c.block_last_index[n], st[n]);
    }

    if (score < 0) {
        return 1;
    } else {
        restore_ac_coeffs(s, block, dir, st, zigzag_last_index);
        return 0;
    }
}

/**
 * modify mb_type & qscale so that encoding is actually possible in MPEG-4
 */
void ff_clean_mpeg4_qscales(MPVEncContext *const s)
{
    ff_clean_h263_qscales(s);

    if (s->c.pict_type == AV_PICTURE_TYPE_B) {
        int8_t *const qscale_table = s->c.cur_pic.qscale_table;
        int odd = 0;
        /* ok, come on, this isn't funny anymore, there's more code for
         * handling this MPEG-4 mess than for the actual adaptive quantization */

        for (int i = 0; i < s->c.mb_num; i++) {
            int mb_xy = s->c.mb_index2xy[i];
            odd += qscale_table[mb_xy] & 1;
        }

        if (2 * odd > s->c.mb_num)
            odd = 1;
        else
            odd = 0;

        for (int i = 0; i < s->c.mb_num; i++) {
            int mb_xy = s->c.mb_index2xy[i];
            if ((qscale_table[mb_xy] & 1) != odd)
                qscale_table[mb_xy]++;
            if (qscale_table[mb_xy] > 31)
                qscale_table[mb_xy] = 31;
        }

        for (int i = 1; i < s->c.mb_num; i++) {
            int mb_xy = s->c.mb_index2xy[i];
            if (qscale_table[mb_xy] != qscale_table[s->c.mb_index2xy[i - 1]] &&
                (s->mb_type[mb_xy] & CANDIDATE_MB_TYPE_DIRECT)) {
                s->mb_type[mb_xy] |= CANDIDATE_MB_TYPE_BIDIR;
            }
        }
    }
}

/**
 * Encode the dc value.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static inline void mpeg4_encode_dc(PutBitContext *s, int level, int n)
{
    /* DC will overflow if level is outside the [-255,255] range. */
    level += 256;
    if (n < 4) {
        /* luminance */
        put_bits(s, uni_DCtab_lum_len[level], uni_DCtab_lum_bits[level]);
    } else {
        /* chrominance */
        put_bits(s, uni_DCtab_chrom_len[level], uni_DCtab_chrom_bits[level]);
    }
}

/**
 * Encode the AC coefficients of an 8x8 block.
 */
static inline void mpeg4_encode_ac_coeffs(const int16_t block[64],
                                          const int last_index, int i,
                                          const uint8_t *const scan_table,
                                          PutBitContext *const ac_pb,
                                          const uint32_t *const bits_tab,
                                          const uint8_t *const len_tab)
{
    int last_non_zero = i - 1;

    /* AC coefs */
    for (; i < last_index; i++) {
        int level = block[scan_table[i]];
        if (level) {
            int run = i - last_non_zero - 1;
            level += 64;
            if ((level & (~127)) == 0) {
                const int index = UNI_MPEG4_ENC_INDEX(0, run, level);
                put_bits(ac_pb, len_tab[index], bits_tab[index]);
            } else {  // ESC3
                put_bits(ac_pb,
                         7 + 2 + 1 + 6 + 1 + 12 + 1,
                         (3 << 23) + (3 << 21) + (0 << 20) + (run << 14) +
                         (1 << 13) + (((level - 64) & 0xfff) << 1) + 1);
            }
            last_non_zero = i;
        }
    }
    /* if (i <= last_index) */ {
        int level = block[scan_table[i]];
        int run   = i - last_non_zero - 1;
        level += 64;
        if ((level & (~127)) == 0) {
            const int index = UNI_MPEG4_ENC_INDEX(1, run, level);
            put_bits(ac_pb, len_tab[index], bits_tab[index]);
        } else {  // ESC3
            put_bits(ac_pb,
                     7 + 2 + 1 + 6 + 1 + 12 + 1,
                     (3 << 23) + (3 << 21) + (1 << 20) + (run << 14) +
                     (1 << 13) + (((level - 64) & 0xfff) << 1) + 1);
        }
    }
}

static void mpeg4_encode_blocks_inter(MPVEncContext *const s,
                                      const int16_t block[6][64],
                                      PutBitContext *ac_pb)
{
    /* encode each block */
    for (int n = 0; n < 6; ++n) {
        const int last_index = s->c.block_last_index[n];
        if (last_index < 0)
            continue;

        mpeg4_encode_ac_coeffs(block[n], last_index, 0,
                               s->c.intra_scantable.permutated, ac_pb,
                               uni_mpeg4_inter_rl_bits, uni_mpeg4_inter_rl_len);
    }
}

static void mpeg4_encode_blocks_intra(MPVEncContext *const s,
                                      const int16_t block[6][64],
                                      const int intra_dc[6],
                                      const uint8_t * const *scan_table,
                                      PutBitContext *dc_pb,
                                      PutBitContext *ac_pb)
{
    /* encode each block */
    for (int n = 0; n < 6; ++n) {
        mpeg4_encode_dc(dc_pb, intra_dc[n], n);

        const int last_index = s->c.block_last_index[n];
        if (last_index <= 0)
            continue;

        mpeg4_encode_ac_coeffs(block[n], last_index, 1,
                               scan_table[n], ac_pb,
                               uni_mpeg4_intra_rl_bits, uni_mpeg4_intra_rl_len);
    }
}

static inline int get_b_cbp(MPVEncContext *const s, int16_t block[6][64],
                            int motion_x, int motion_y, int mb_type)
{
    int cbp = 0, i;

    if (s->mpv_flags & FF_MPV_FLAG_CBP_RD) {
        int score        = 0;
        const int lambda = s->lambda2 >> (FF_LAMBDA_SHIFT - 6);

        for (i = 0; i < 6; i++) {
            if (s->coded_score[i] < 0) {
                score += s->coded_score[i];
                cbp   |= 1 << (5 - i);
            }
        }

        if (cbp) {
            int zero_score = -6;
            if ((motion_x | motion_y | s->dquant | mb_type) == 0)
                zero_score -= 4;  // 2 * MV + mb_type + cbp bit

            zero_score *= lambda;
            if (zero_score <= score)
                cbp = 0;
        }

        for (i = 0; i < 6; i++) {
            if (s->c.block_last_index[i] >= 0 && ((cbp >> (5 - i)) & 1) == 0) {
                s->c.block_last_index[i] = -1;
                s->c.bdsp.clear_block(s->c.block[i]);
            }
        }
    } else {
        for (i = 0; i < 6; i++) {
            if (s->c.block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
    }
    return cbp;
}

// FIXME this is duplicated to h263.c
static const int dquant_code[5] = { 1, 0, 9, 2, 3 };

static void mpeg4_encode_mb(MPVEncContext *const s, int16_t block[][64],
                            int motion_x, int motion_y)
{
    int cbpc, cbpy, pred_x, pred_y;
    PutBitContext *const pb2    = s->c.data_partitioning ? &s->pb2 : &s->pb;
    PutBitContext *const tex_pb = s->c.data_partitioning && s->c.pict_type != AV_PICTURE_TYPE_B ? &s->tex_pb : &s->pb;
    PutBitContext *const dc_pb  = s->c.data_partitioning && s->c.pict_type != AV_PICTURE_TYPE_I ? &s->pb2 : &s->pb;
    const int interleaved_stats = (s->c.avctx->flags & AV_CODEC_FLAG_PASS1) && !s->c.data_partitioning ? 1 : 0;

    if (!s->c.mb_intra) {
        int i, cbp;

        if (s->c.pict_type == AV_PICTURE_TYPE_B) {
            /* convert from mv_dir to type */
            static const int mb_type_table[8] = { -1, 3, 2, 1, -1, -1, -1, 0 };
            int mb_type = mb_type_table[s->c.mv_dir];

            if (s->c.mb_x == 0) {
                for (i = 0; i < 2; i++)
                    s->c.last_mv[i][0][0] =
                    s->c.last_mv[i][0][1] =
                    s->c.last_mv[i][1][0] =
                    s->c.last_mv[i][1][1] = 0;
            }

            av_assert2(s->dquant >= -2 && s->dquant <= 2);
            av_assert2((s->dquant & 1) == 0);
            av_assert2(mb_type >= 0);

            /* nothing to do if this MB was skipped in the next P-frame */
            if (s->c.next_pic.mbskip_table[s->c.mb_y * s->c.mb_stride + s->c.mb_x]) {  // FIXME avoid DCT & ...
                s->c.mv[0][0][0] =
                s->c.mv[0][0][1] =
                s->c.mv[1][0][0] =
                s->c.mv[1][0][1] = 0;
                s->c.mv_dir  = MV_DIR_FORWARD;  // doesn't matter
                s->c.qscale -= s->dquant;
//                s->c.mb_skipped = 1;

                return;
            }

            cbp = get_b_cbp(s, block, motion_x, motion_y, mb_type);

            if ((cbp | motion_x | motion_y | mb_type) == 0) {
                /* direct MB with MV={0,0} */
                av_assert2(s->dquant == 0);

                put_bits(&s->pb, 1, 1); /* mb not coded modb1=1 */

                if (interleaved_stats) {
                    s->misc_bits++;
                    s->last_bits++;
                }
                return;
            }

            put_bits(&s->pb, 1, 0);            /* mb coded modb1=0 */
            put_bits(&s->pb, 1, cbp ? 0 : 1);  /* modb2 */ // FIXME merge
            put_bits(&s->pb, mb_type + 1, 1);  // this table is so simple that we don't need it :)
            if (cbp)
                put_bits(&s->pb, 6, cbp);

            if (cbp && mb_type) {
                if (s->dquant)
                    put_bits(&s->pb, 2, (s->dquant >> 2) + 3);
                else
                    put_bits(&s->pb, 1, 0);
            } else
                s->c.qscale -= s->dquant;

            if (!s->c.progressive_sequence) {
                if (cbp)
                    put_bits(&s->pb, 1, s->c.interlaced_dct);
                if (mb_type)                  // not direct mode
                    put_bits(&s->pb, 1, s->c.mv_type == MV_TYPE_FIELD);
            }

            if (interleaved_stats)
                s->misc_bits += get_bits_diff(s);

            if (!mb_type) {
                av_assert2(s->c.mv_dir & MV_DIRECT);
                ff_h263_encode_motion_vector(s, motion_x, motion_y, 1);
            } else {
                av_assert2(mb_type > 0 && mb_type < 4);
                if (s->c.mv_type != MV_TYPE_FIELD) {
                    if (s->c.mv_dir & MV_DIR_FORWARD) {
                        ff_h263_encode_motion_vector(s,
                                                     s->c.mv[0][0][0] - s->c.last_mv[0][0][0],
                                                     s->c.mv[0][0][1] - s->c.last_mv[0][0][1],
                                                     s->f_code);
                        s->c.last_mv[0][0][0] =
                        s->c.last_mv[0][1][0] = s->c.mv[0][0][0];
                        s->c.last_mv[0][0][1] =
                        s->c.last_mv[0][1][1] = s->c.mv[0][0][1];
                    }
                    if (s->c.mv_dir & MV_DIR_BACKWARD) {
                        ff_h263_encode_motion_vector(s,
                                                     s->c.mv[1][0][0] - s->c.last_mv[1][0][0],
                                                     s->c.mv[1][0][1] - s->c.last_mv[1][0][1],
                                                     s->b_code);
                        s->c.last_mv[1][0][0] =
                        s->c.last_mv[1][1][0] = s->c.mv[1][0][0];
                        s->c.last_mv[1][0][1] =
                        s->c.last_mv[1][1][1] = s->c.mv[1][0][1];
                    }
                } else {
                    if (s->c.mv_dir & MV_DIR_FORWARD) {
                        put_bits(&s->pb, 1, s->c.field_select[0][0]);
                        put_bits(&s->pb, 1, s->c.field_select[0][1]);
                    }
                    if (s->c.mv_dir & MV_DIR_BACKWARD) {
                        put_bits(&s->pb, 1, s->c.field_select[1][0]);
                        put_bits(&s->pb, 1, s->c.field_select[1][1]);
                    }
                    if (s->c.mv_dir & MV_DIR_FORWARD) {
                        for (i = 0; i < 2; i++) {
                            ff_h263_encode_motion_vector(s,
                                                         s->c.mv[0][i][0] - s->c.last_mv[0][i][0],
                                                         s->c.mv[0][i][1] - s->c.last_mv[0][i][1] / 2,
                                                         s->f_code);
                            s->c.last_mv[0][i][0] = s->c.mv[0][i][0];
                            s->c.last_mv[0][i][1] = s->c.mv[0][i][1] * 2;
                        }
                    }
                    if (s->c.mv_dir & MV_DIR_BACKWARD) {
                        for (i = 0; i < 2; i++) {
                            ff_h263_encode_motion_vector(s,
                                                         s->c.mv[1][i][0] - s->c.last_mv[1][i][0],
                                                         s->c.mv[1][i][1] - s->c.last_mv[1][i][1] / 2,
                                                         s->b_code);
                            s->c.last_mv[1][i][0] = s->c.mv[1][i][0];
                            s->c.last_mv[1][i][1] = s->c.mv[1][i][1] * 2;
                        }
                    }
                }
            }

            if (interleaved_stats)
                s->mv_bits += get_bits_diff(s);

            mpeg4_encode_blocks_inter(s, block, &s->pb);

            if (interleaved_stats)
                s->p_tex_bits += get_bits_diff(s);
        } else { /* s->c.pict_type == AV_PICTURE_TYPE_B */
            cbp = get_p_cbp(s, block, motion_x, motion_y);

            if ((cbp | motion_x | motion_y | s->dquant) == 0 &&
                s->c.mv_type == MV_TYPE_16X16) {
                const MPVMainEncContext *const m = slice_to_mainenc(s);
                /* Check if the B-frames can skip it too, as we must skip it
                 * if we skip here why didn't they just compress
                 * the skip-mb bits instead of reusing them ?! */
                if (m->max_b_frames > 0) {
                    int x, y, offset;
                    const uint8_t *p_pic;

                    x = s->c.mb_x * 16;
                    y = s->c.mb_y * 16;

                    offset = x + y * s->c.linesize;
                    p_pic  = s->new_pic->data[0] + offset;

                    s->c.mb_skipped = 1;
                    for (int i = 0; i < m->max_b_frames; i++) {
                        const uint8_t *b_pic;
                        int diff;
                        const MPVPicture *pic = m->reordered_input_picture[i + 1];

                        if (!pic || pic->f->pict_type != AV_PICTURE_TYPE_B)
                            break;

                        b_pic = pic->f->data[0] + offset;
                        if (!pic->shared)
                            b_pic += INPLACE_OFFSET;

                        if (x + 16 > s->c.width || y + 16 > s->c.height) {
                            int x1, y1;
                            int xe = FFMIN(16, s->c.width - x);
                            int ye = FFMIN(16, s->c.height - y);
                            diff = 0;
                            for (y1 = 0; y1 < ye; y1++) {
                                for (x1 = 0; x1 < xe; x1++) {
                                    diff += FFABS(p_pic[x1 + y1 * s->c.linesize] - b_pic[x1 + y1 * s->c.linesize]);
                                }
                            }
                            diff = diff * 256 / (xe * ye);
                        } else {
                            diff = s->sad_cmp[0](NULL, p_pic, b_pic, s->c.linesize, 16);
                        }
                        if (diff > s->c.qscale * 70) {  // FIXME check that 70 is optimal
                            s->c.mb_skipped = 0;
                            break;
                        }
                    }
                } else
                    s->c.mb_skipped = 1;

                if (s->c.mb_skipped == 1) {
                    /* skip macroblock */
                    put_bits(&s->pb, 1, 1);

                    if (interleaved_stats) {
                        s->misc_bits++;
                        s->last_bits++;
                    }

                    return;
                }
            }

            put_bits(&s->pb, 1, 0);     /* mb coded */
            cbpc  = cbp & 3;
            cbpy  = cbp >> 2;
            cbpy ^= 0xf;
            if (s->c.mv_type == MV_TYPE_16X16) {
                if (s->dquant)
                    cbpc += 8;
                put_bits(&s->pb,
                         ff_h263_inter_MCBPC_bits[cbpc],
                         ff_h263_inter_MCBPC_code[cbpc]);

                put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);
                if (s->dquant)
                    put_bits(pb2, 2, dquant_code[s->dquant + 2]);

                if (!s->c.progressive_sequence) {
                    if (cbp)
                        put_bits(pb2, 1, s->c.interlaced_dct);
                    put_bits(pb2, 1, 0);
                }

                if (interleaved_stats)
                    s->misc_bits += get_bits_diff(s);

                /* motion vectors: 16x16 mode */
                ff_h263_pred_motion(&s->c, 0, 0, &pred_x, &pred_y);

                ff_h263_encode_motion_vector(s,
                                             motion_x - pred_x,
                                             motion_y - pred_y,
                                             s->f_code);
            } else if (s->c.mv_type == MV_TYPE_FIELD) {
                if (s->dquant)
                    cbpc += 8;
                put_bits(&s->pb,
                         ff_h263_inter_MCBPC_bits[cbpc],
                         ff_h263_inter_MCBPC_code[cbpc]);

                put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);
                if (s->dquant)
                    put_bits(pb2, 2, dquant_code[s->dquant + 2]);

                av_assert2(!s->c.progressive_sequence);
                if (cbp)
                    put_bits(pb2, 1, s->c.interlaced_dct);
                put_bits(pb2, 1, 1);

                if (interleaved_stats)
                    s->misc_bits += get_bits_diff(s);

                /* motion vectors: 16x8 interlaced mode */
                ff_h263_pred_motion(&s->c, 0, 0, &pred_x, &pred_y);
                pred_y /= 2;

                put_bits(&s->pb, 1, s->c.field_select[0][0]);
                put_bits(&s->pb, 1, s->c.field_select[0][1]);

                ff_h263_encode_motion_vector(s,
                                             s->c.mv[0][0][0] - pred_x,
                                             s->c.mv[0][0][1] - pred_y,
                                             s->f_code);
                ff_h263_encode_motion_vector(s,
                                             s->c.mv[0][1][0] - pred_x,
                                             s->c.mv[0][1][1] - pred_y,
                                             s->f_code);
            } else {
                av_assert2(s->c.mv_type == MV_TYPE_8X8);
                put_bits(&s->pb,
                         ff_h263_inter_MCBPC_bits[cbpc + 16],
                         ff_h263_inter_MCBPC_code[cbpc + 16]);
                put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);

                if (!s->c.progressive_sequence && cbp)
                    put_bits(pb2, 1, s->c.interlaced_dct);

                if (interleaved_stats)
                    s->misc_bits += get_bits_diff(s);

                for (i = 0; i < 4; i++) {
                    /* motion vectors: 8x8 mode*/
                    ff_h263_pred_motion(&s->c, i, 0, &pred_x, &pred_y);

                    ff_h263_encode_motion_vector(s,
                                                 s->c.cur_pic.motion_val[0][s->c.block_index[i]][0] - pred_x,
                                                 s->c.cur_pic.motion_val[0][s->c.block_index[i]][1] - pred_y,
                                                 s->f_code);
                }
            }

            if (interleaved_stats)
                s->mv_bits += get_bits_diff(s);

            mpeg4_encode_blocks_inter(s, block, tex_pb);

            if (interleaved_stats)
                s->p_tex_bits += get_bits_diff(s);
        }
    } else {
        int cbp;
        int dc_diff[6];  // dc values with the dc prediction subtracted
        int dir[6];      // prediction direction
        int zigzag_last_index[6];
        const uint8_t *scan_table[6];
        int i;

        for (int i = 0; i < 6; i++) {
            int pred  = mpeg4_pred_dc(&s->c, i, &dir[i]);
            int scale = i < 4 ? s->c.y_dc_scale : s->c.c_dc_scale;

            pred = FASTDIV((pred + (scale >> 1)), scale);
            dc_diff[i] = block[i][0] - pred;
            s->c.dc_val[s->c.block_index[i]] = av_clip_uintp2(block[i][0] * scale, 11);
        }

        if (s->c.avctx->flags & AV_CODEC_FLAG_AC_PRED) {
            s->c.ac_pred = decide_ac_pred(s, block, dir, scan_table, zigzag_last_index);
        } else {
            for (i = 0; i < 6; i++)
                scan_table[i] = s->c.intra_scantable.permutated;
        }

        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++)
            if (s->c.block_last_index[i] >= 1)
                cbp |= 1 << (5 - i);

        cbpc = cbp & 3;
        if (s->c.pict_type == AV_PICTURE_TYPE_I) {
            if (s->dquant)
                cbpc += 4;
            put_bits(&s->pb,
                     ff_h263_intra_MCBPC_bits[cbpc],
                     ff_h263_intra_MCBPC_code[cbpc]);
        } else {
            if (s->dquant)
                cbpc += 8;
            put_bits(&s->pb, 1, 0);     /* mb coded */
            put_bits(&s->pb,
                     ff_h263_inter_MCBPC_bits[cbpc + 4],
                     ff_h263_inter_MCBPC_code[cbpc + 4]);
        }
        put_bits(pb2, 1, s->c.ac_pred);
        cbpy = cbp >> 2;
        put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);
        if (s->dquant)
            put_bits(dc_pb, 2, dquant_code[s->dquant + 2]);

        if (!s->c.progressive_sequence)
            put_bits(dc_pb, 1, s->c.interlaced_dct);

        if (interleaved_stats)
            s->misc_bits += get_bits_diff(s);

        mpeg4_encode_blocks_intra(s, block, dc_diff, scan_table, dc_pb, tex_pb);

        if (interleaved_stats)
            s->i_tex_bits += get_bits_diff(s);
        s->i_count++;

        /* restore ac coeffs & last_index stuff
         * if we messed them up with the prediction */
        if (s->c.ac_pred)
            restore_ac_coeffs(s, block, dir, scan_table, zigzag_last_index);
    }
}

/**
 * add MPEG-4 stuffing bits (01...1)
 */
void ff_mpeg4_stuffing(PutBitContext *pbc)
{
    int length = 8 - (put_bits_count(pbc) & 7);

    put_bits(pbc, length, (1 << (length - 1)) - 1);
}

/* must be called before writing the header */
void ff_set_mpeg4_time(MPVEncContext *const s)
{
    if (s->c.pict_type == AV_PICTURE_TYPE_B) {
        ff_mpeg4_init_direct_mv(&s->c);
    } else {
        s->c.last_time_base = s->c.time_base;
        s->c.time_base      = FFUDIV(s->c.time, s->c.avctx->time_base.den);
    }
}

static void mpeg4_encode_gop_header(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    int64_t hours, minutes, seconds;
    int64_t time;

    put_bits32(&s->pb, GOP_STARTCODE);

    time = s->c.cur_pic.ptr->f->pts;
    if (m->reordered_input_picture[1])
        time = FFMIN(time, m->reordered_input_picture[1]->f->pts);
    time = time * s->c.avctx->time_base.num;
    s->c.last_time_base = FFUDIV(time, s->c.avctx->time_base.den);

    seconds = FFUDIV(time, s->c.avctx->time_base.den);
    minutes = FFUDIV(seconds, 60); seconds = FFUMOD(seconds, 60);
    hours   = FFUDIV(minutes, 60); minutes = FFUMOD(minutes, 60);
    hours   = FFUMOD(hours  , 24);

    put_bits(&s->pb, 5, hours);
    put_bits(&s->pb, 6, minutes);
    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 6, seconds);

    put_bits(&s->pb, 1, !!(s->c.avctx->flags & AV_CODEC_FLAG_CLOSED_GOP));
    put_bits(&s->pb, 1, 0);  // broken link == NO

    ff_mpeg4_stuffing(&s->pb);
}

static void mpeg4_encode_visual_object_header(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    int profile_and_level_indication;
    int vo_ver_id;

    if (s->c.avctx->profile != AV_PROFILE_UNKNOWN) {
        profile_and_level_indication = s->c.avctx->profile << 4;
    } else if (m->max_b_frames || s->c.quarter_sample) {
        profile_and_level_indication = 0xF0;  // adv simple
    } else {
        profile_and_level_indication = 0x00;  // simple
    }

    if (s->c.avctx->level != AV_LEVEL_UNKNOWN)
        profile_and_level_indication |= s->c.avctx->level;
    else
        profile_and_level_indication |= 1;   // level 1

    if (profile_and_level_indication >> 4 == 0xF)
        vo_ver_id = 5;
    else
        vo_ver_id = 1;

    // FIXME levels

    put_bits32(&s->pb, VOS_STARTCODE);

    put_bits(&s->pb, 8, profile_and_level_indication);

    put_bits32(&s->pb, VISUAL_OBJ_STARTCODE);

    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 4, vo_ver_id);
    put_bits(&s->pb, 3, 1);     // priority

    put_bits(&s->pb, 4, 1);     // visual obj type== video obj

    put_bits(&s->pb, 1, 0);     // video signal type == no clue // FIXME

    ff_mpeg4_stuffing(&s->pb);
}

static void mpeg4_encode_vol_header(Mpeg4EncContext *const m4,
                                    int vo_number,
                                    int vol_number)
{
    MPVEncContext *const s = &m4->m.s;
    int vo_ver_id, vo_type, aspect_ratio_info;

    if (m4->m.max_b_frames || s->c.quarter_sample) {
        vo_ver_id  = 5;
        vo_type = ADV_SIMPLE_VO_TYPE;
    } else {
        vo_ver_id  = 1;
        vo_type = SIMPLE_VO_TYPE;
    }

    put_bits32(&s->pb, 0x100 + vo_number);        /* video obj */
    put_bits32(&s->pb, 0x120 + vol_number);       /* video obj layer */

    put_bits(&s->pb, 1, 0);             /* random access vol */
    put_bits(&s->pb, 8, vo_type);       /* video obj type indication */
    put_bits(&s->pb, 1, 1);             /* is obj layer id= yes */
    put_bits(&s->pb, 4, vo_ver_id);     /* is obj layer ver id */
    put_bits(&s->pb, 3, 1);             /* is obj layer priority */

    aspect_ratio_info = ff_h263_aspect_to_info(s->c.avctx->sample_aspect_ratio);

    put_bits(&s->pb, 4, aspect_ratio_info); /* aspect ratio info */
    if (aspect_ratio_info == FF_ASPECT_EXTENDED) {
        av_reduce(&s->c.avctx->sample_aspect_ratio.num, &s->c.avctx->sample_aspect_ratio.den,
                   s->c.avctx->sample_aspect_ratio.num,  s->c.avctx->sample_aspect_ratio.den, 255);
        put_bits(&s->pb, 8, s->c.avctx->sample_aspect_ratio.num);
        put_bits(&s->pb, 8, s->c.avctx->sample_aspect_ratio.den);
    }

    put_bits(&s->pb, 1, 1);             /* vol control parameters= yes */
    put_bits(&s->pb, 2, 1);             /* chroma format YUV 420/YV12 */
    put_bits(&s->pb, 1, s->c.low_delay);
    put_bits(&s->pb, 1, 0);             /* vbv parameters= no */

    put_bits(&s->pb, 2, RECT_SHAPE);    /* vol shape= rectangle */
    put_bits(&s->pb, 1, 1);             /* marker bit */

    put_bits(&s->pb, 16, s->c.avctx->time_base.den);
    if (m4->time_increment_bits < 1)
        m4->time_increment_bits = 1;
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 1, 0);             /* fixed vop rate=no */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 13, s->c.width);     /* vol width */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 13, s->c.height);    /* vol height */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 1, s->c.progressive_sequence ? 0 : 1);
    put_bits(&s->pb, 1, 1);             /* obmc disable */
    if (vo_ver_id == 1)
        put_bits(&s->pb, 1, 0);       /* sprite enable */
    else
        put_bits(&s->pb, 2, 0);       /* sprite enable */

    put_bits(&s->pb, 1, 0);             /* not 8 bit == false */
    put_bits(&s->pb, 1, s->mpeg_quant); /* quant type = (0 = H.263 style) */

    if (s->mpeg_quant) {
        ff_write_quant_matrix(&s->pb, s->c.avctx->intra_matrix);
        ff_write_quant_matrix(&s->pb, s->c.avctx->inter_matrix);
    }

    if (vo_ver_id != 1)
        put_bits(&s->pb, 1, s->c.quarter_sample);
    put_bits(&s->pb, 1, 1);             /* complexity estimation disable */
    put_bits(&s->pb, 1, s->rtp_mode ? 0 : 1); /* resync marker disable */
    put_bits(&s->pb, 1, s->c.data_partitioning ? 1 : 0);
    if (s->c.data_partitioning)
        put_bits(&s->pb, 1, 0);         /* no rvlc */

    if (vo_ver_id != 1) {
        put_bits(&s->pb, 1, 0);         /* newpred */
        put_bits(&s->pb, 1, 0);         /* reduced res vop */
    }
    put_bits(&s->pb, 1, 0);             /* scalability */

    ff_mpeg4_stuffing(&s->pb);

    /* user data */
    if (!(s->c.avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
        put_bits32(&s->pb, USER_DATA_STARTCODE);
        ff_put_string(&s->pb, LIBAVCODEC_IDENT, 0);
    }
}

/* write MPEG-4 VOP header */
static int mpeg4_encode_picture_header(MPVMainEncContext *const m)
{
    Mpeg4EncContext *const m4 = mainctx_to_mpeg4(m);
    MPVEncContext *const s = &m->s;
    uint64_t time_incr;
    int64_t time_div, time_mod;

    put_bits_assume_flushed(&s->pb);

    if (s->c.pict_type == AV_PICTURE_TYPE_I) {
        if (!(s->c.avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
            if (s->c.avctx->strict_std_compliance < FF_COMPLIANCE_VERY_STRICT)  // HACK, the reference sw is buggy
                mpeg4_encode_visual_object_header(m);
            if (s->c.avctx->strict_std_compliance < FF_COMPLIANCE_VERY_STRICT || s->c.picture_number == 0)  // HACK, the reference sw is buggy
                mpeg4_encode_vol_header(m4, 0, 0);
        }
        mpeg4_encode_gop_header(m);
    }

    s->c.partitioned_frame = s->c.data_partitioning && s->c.pict_type != AV_PICTURE_TYPE_B;

    put_bits32(&s->pb, VOP_STARTCODE);      /* vop header */
    put_bits(&s->pb, 2, s->c.pict_type - 1);  /* pict type: I = 0 , P = 1 */

    time_div  = FFUDIV(s->c.time, s->c.avctx->time_base.den);
    time_mod  = FFUMOD(s->c.time, s->c.avctx->time_base.den);
    time_incr = time_div - s->c.last_time_base;

    // This limits the frame duration to max 1 day
    if (time_incr > 3600*24) {
        av_log(s->c.avctx, AV_LOG_ERROR, "time_incr %"PRIu64" too large\n", time_incr);
        return AVERROR(EINVAL);
    }
    while (time_incr--)
        put_bits(&s->pb, 1, 1);

    put_bits(&s->pb, 1, 0);

    put_bits(&s->pb, 1, 1);                             /* marker */
    put_bits(&s->pb, m4->time_increment_bits, time_mod); /* time increment */
    put_bits(&s->pb, 1, 1);                             /* marker */
    put_bits(&s->pb, 1, 1);                             /* vop coded */
    if (s->c.pict_type == AV_PICTURE_TYPE_P) {
        put_bits(&s->pb, 1, s->c.no_rounding);    /* rounding type */
    }
    put_bits(&s->pb, 3, 0);     /* intra dc VLC threshold */
    if (!s->c.progressive_sequence) {
        put_bits(&s->pb, 1, !!(s->c.cur_pic.ptr->f->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST));
        put_bits(&s->pb, 1, s->c.alternate_scan);
    }
    // FIXME sprite stuff

    put_bits(&s->pb, 5, s->c.qscale);

    if (s->c.pict_type != AV_PICTURE_TYPE_I)
        put_bits(&s->pb, 3, s->f_code);  /* fcode_for */
    if (s->c.pict_type == AV_PICTURE_TYPE_B)
        put_bits(&s->pb, 3, s->b_code);  /* fcode_back */

    return 0;
}

static av_cold void init_uni_dc_tab(void)
{
    int level, uni_code, uni_len;

    for (level = -256; level < 256; level++) {
        int size, v, l;
        /* find number of bits */
        size = 0;
        v    = abs(level);
        while (v) {
            v >>= 1;
            size++;
        }

        if (level < 0)
            l = (-level) ^ ((1 << size) - 1);
        else
            l = level;

        /* luminance */
        uni_code = ff_mpeg4_DCtab_lum[size][0];
        uni_len  = ff_mpeg4_DCtab_lum[size][1];

        if (size > 0) {
            uni_code <<= size;
            uni_code  |= l;
            uni_len   += size;
            if (size > 8) {
                uni_code <<= 1;
                uni_code  |= 1;
                uni_len++;
            }
        }
        uni_DCtab_lum_bits[level + 256] = uni_code;
        uni_DCtab_lum_len[level + 256]  = uni_len;

        /* chrominance */
        uni_code = ff_mpeg4_DCtab_chrom[size][0];
        uni_len  = ff_mpeg4_DCtab_chrom[size][1];

        if (size > 0) {
            uni_code <<= size;
            uni_code  |= l;
            uni_len   += size;
            if (size > 8) {
                uni_code <<= 1;
                uni_code  |= 1;
                uni_len++;
            }
        }
        uni_DCtab_chrom_bits[level + 256] = uni_code;
        uni_DCtab_chrom_len[level + 256]  = uni_len;
    }
}

static av_cold void init_uni_mpeg4_rl_tab(RLTable *rl, uint32_t *bits_tab,
                                          uint8_t *len_tab)
{
    // Type 3 escape method. The escape code is the same for both VLCs
    // (0x3, seven bits), so it is hardcoded.
    memset(len_tab, 30, 2 * 2 * 64 * 64);
    len_tab  += 64;
    bits_tab += 64;
    for (int run = 0; run < 64; ++run) {
        for (int level = 1;; ++level) {
                       //  Escape code   type 3     not last    run (6 bits)   marker   marker
            unsigned code = (3 << 23) | (3 << 21) | (0 << 20) | (run << 14) | (1 << 13) | 1;
            // first the negative levels
            bits_tab[UNI_MPEG4_ENC_INDEX(0, run, -level)] = code | (-level & 0xfff) << 1;
            bits_tab[UNI_MPEG4_ENC_INDEX(1, run, -level)] =
                bits_tab[UNI_MPEG4_ENC_INDEX(0, run, -level)] | (1 << 20) /* last */;

            if (level == 64) // positive levels have a range of 1..63
                break;
            bits_tab[UNI_MPEG4_ENC_INDEX(0, run, level)] = code | level << 1;
            bits_tab[UNI_MPEG4_ENC_INDEX(1, run, level)] =
                bits_tab[UNI_MPEG4_ENC_INDEX(0, run, level)] | (1 << 20) /* last */;
        }
        // Is this needed at all?
        len_tab[UNI_MPEG4_ENC_INDEX(0, run, 0)] =
        len_tab[UNI_MPEG4_ENC_INDEX(1, run, 0)] = 0;
    }

    uint8_t max_run[2][32] = { 0 };

#define VLC_NUM_CODES 102 // excluding the escape
    av_assert2(rl->n == VLC_NUM_CODES);
    for (int i = VLC_NUM_CODES - 1, max_level, cur_run = 0; i >= 0; --i) {
        int run = rl->table_run[i], level = rl->table_level[i];
        int last = i >= rl->last;
        unsigned code = rl->table_vlc[i][0] << 1;
        int len = rl->table_vlc[i][1] + 1;

        bits_tab[UNI_MPEG4_ENC_INDEX(last, run,  level)] = code;
        len_tab [UNI_MPEG4_ENC_INDEX(last, run,  level)] = len;
        bits_tab[UNI_MPEG4_ENC_INDEX(last, run, -level)] = code | 1;
        len_tab [UNI_MPEG4_ENC_INDEX(last, run, -level)] = len;

        if (!max_run[last][level])
            max_run[last][level] = run + 1;
        av_assert2(run + 1 <= max_run[last][level]);

        int run3 = run + max_run[last][level];
        int len3 = len + 7 + 2;

        if (run3 < 64 && len3 < len_tab[UNI_MPEG4_ENC_INDEX(last, run3, level)]) {
            unsigned code3 = code | (0x3 << 2 | 0x2) << len;
            bits_tab[UNI_MPEG4_ENC_INDEX(last, run3,  level)] = code3;
            len_tab [UNI_MPEG4_ENC_INDEX(last, run3,  level)] = len3;
            bits_tab[UNI_MPEG4_ENC_INDEX(last, run3, -level)] = code3 | 1;
            len_tab [UNI_MPEG4_ENC_INDEX(last, run3, -level)] = len3;
        }
        // table_run and table_level are ordered so that all the entries
        // with the same last and run are consecutive and level is ascending
        // among these entries. By traversing downwards we therefore automatically
        // encounter max_level of a given run first, needed for escape method 1.
        if (run != cur_run) {
            max_level = level;
            cur_run   = run;
        } else
            av_assert2(max_level > level);

        code  |= 0x3 << (len + 1);
        len   += 7 + 1;
        level += max_level;
        av_assert2(len_tab [UNI_MPEG4_ENC_INDEX(last, run,  level)] >= len);
        bits_tab[UNI_MPEG4_ENC_INDEX(last, run,  level)] = code;
        len_tab [UNI_MPEG4_ENC_INDEX(last, run,  level)] = len;
        bits_tab[UNI_MPEG4_ENC_INDEX(last, run, -level)] = code | 1;
        len_tab [UNI_MPEG4_ENC_INDEX(last, run, -level)] = len;
    }
}

static av_cold void mpeg4_encode_init_static(void)
{
    init_uni_dc_tab();

    init_uni_mpeg4_rl_tab(&ff_mpeg4_rl_intra, uni_mpeg4_intra_rl_bits, uni_mpeg4_intra_rl_len);
    init_uni_mpeg4_rl_tab(&ff_h263_rl_inter,  uni_mpeg4_inter_rl_bits, uni_mpeg4_inter_rl_len);

    for (int f_code = MAX_FCODE; f_code > 0; f_code--) {
        for (int mv = -(16 << f_code); mv < (16 << f_code); mv++)
            fcode_tab[mv + MAX_MV] = f_code;
    }
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    Mpeg4EncContext *const m4 = avctx->priv_data;
    MPVMainEncContext *const m = &m4->m;
    MPVEncContext *const s = &m->s;
    int ret;

    if (avctx->width >= (1<<13) || avctx->height >= (1<<13)) {
        av_log(avctx, AV_LOG_ERROR, "dimensions too large for MPEG-4\n");
        return AVERROR(EINVAL);
    }

    m->encode_picture_header = mpeg4_encode_picture_header;
    s->encode_mb             = mpeg4_encode_mb;

    m->fcode_tab                = fcode_tab + MAX_MV;

    s->min_qcoeff               = -2048;
    s->max_qcoeff               = 2047;
    s->intra_ac_vlc_length      = uni_mpeg4_intra_rl_len;
    s->intra_ac_vlc_last_length = uni_mpeg4_intra_rl_len + 128 * 64;
    s->inter_ac_vlc_length      = uni_mpeg4_inter_rl_len;
    s->inter_ac_vlc_last_length = uni_mpeg4_inter_rl_len + 128 * 64;
    s->luma_dc_vlc_length       = uni_DCtab_lum_len;
    s->ac_esc_length            = 7 + 2 + 1 + 6 + 1 + 12 + 1;
    s->c.y_dc_scale_table         = ff_mpeg4_y_dc_scale_table;
    s->c.c_dc_scale_table         = ff_mpeg4_c_dc_scale_table;

    ff_qpeldsp_init(&s->c.qdsp);
    if ((ret = ff_mpv_encode_init(avctx)) < 0)
        return ret;

    ff_thread_once(&init_static_once, mpeg4_encode_init_static);

    if (avctx->time_base.den > (1 << 16) - 1) {
        av_log(avctx, AV_LOG_ERROR,
               "timebase %d/%d not supported by MPEG 4 standard, "
               "the maximum admitted value for the timebase denominator "
               "is %d\n", avctx->time_base.num, avctx->time_base.den,
               (1 << 16) - 1);
        return AVERROR(EINVAL);
    }

    m4->time_increment_bits     = av_log2(avctx->time_base.den - 1) + 1;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        avctx->extradata = av_malloc(1024);
        if (!avctx->extradata)
            return AVERROR(ENOMEM);
        init_put_bits(&s->pb, avctx->extradata, 1024);

        mpeg4_encode_visual_object_header(m);
        mpeg4_encode_vol_header(m4, 0, 0);

//            ff_mpeg4_stuffing(&s->pb); ?
        flush_put_bits(&s->pb);
        avctx->extradata_size = put_bytes_output(&s->pb);
    }
    return 0;
}

void ff_mpeg4_init_partitions(MPVEncContext *const s)
{
    uint8_t *start = put_bits_ptr(&s->pb);
    uint8_t *end   = s->pb.buf_end;
    int size       = end - start;
    int pb_size    = (((intptr_t)start + size / 3) & (~3)) - (intptr_t)start;
    int tex_size   = (size - 2 * pb_size) & (~3);

    set_put_bits_buffer_size(&s->pb, pb_size);
    init_put_bits(&s->tex_pb, start + pb_size, tex_size);
    init_put_bits(&s->pb2, start + pb_size + tex_size, pb_size);
}

void ff_mpeg4_merge_partitions(MPVEncContext *const s)
{
    const int pb2_len    = put_bits_count(&s->pb2);
    const int tex_pb_len = put_bits_count(&s->tex_pb);
    const int bits       = put_bits_count(&s->pb);

    if (s->c.pict_type == AV_PICTURE_TYPE_I) {
        put_bits(&s->pb, 19, DC_MARKER);
        s->misc_bits  += 19 + pb2_len + bits - s->last_bits;
        s->i_tex_bits += tex_pb_len;
    } else {
        put_bits(&s->pb, 17, MOTION_MARKER);
        s->misc_bits  += 17 + pb2_len;
        s->mv_bits    += bits - s->last_bits;
        s->p_tex_bits += tex_pb_len;
    }

    flush_put_bits(&s->pb2);
    flush_put_bits(&s->tex_pb);

    set_put_bits_buffer_size(&s->pb, s->pb2.buf_end - s->pb.buf);
    ff_copy_bits(&s->pb, s->pb2.buf, pb2_len);
    ff_copy_bits(&s->pb, s->tex_pb.buf, tex_pb_len);
    s->last_bits = put_bits_count(&s->pb);
}

void ff_mpeg4_encode_video_packet_header(MPVEncContext *const s)
{
    int mb_num_bits = av_log2(s->c.mb_num - 1) + 1;

    put_bits(&s->pb, ff_mpeg4_get_video_packet_prefix_length(s->c.pict_type, s->f_code, s->b_code), 0);
    put_bits(&s->pb, 1, 1);

    put_bits(&s->pb, mb_num_bits, s->c.mb_x + s->c.mb_y * s->c.mb_width);
    put_bits(&s->pb, 5 /* quant_precision */, s->c.qscale);
    put_bits(&s->pb, 1, 0); /* no HEC */
}

#define OFFSET(x) offsetof(MPVEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "data_partitioning", "Use data partitioning.",      OFFSET(c.data_partitioning), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "alternate_scan",    "Enable alternate scantable.", OFFSET(c.alternate_scan),    AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "mpeg_quant",        "Use MPEG quantizers instead of H.263",
      OFFSET(mpeg_quant), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, 1, VE },
    FF_MPV_COMMON_BFRAME_OPTS
    FF_MPV_COMMON_OPTS
    FF_MPV_COMMON_MOTION_EST_OPTS
    FF_MPEG4_PROFILE_OPTS
    { NULL },
};

static const AVClass mpeg4enc_class = {
    .class_name = "MPEG4 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_mpeg4_encoder = {
    .p.name         = "mpeg4",
    CODEC_LONG_NAME("MPEG-4 part 2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG4,
    .priv_data_size = sizeof(Mpeg4EncContext),
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.priv_class   = &mpeg4enc_class,
};
