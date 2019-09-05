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
#include "libavutil/opt.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "h263.h"
#include "mpeg4video.h"

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

/**
 * Return the number of bits that encoding the 8x8 block in block would need.
 * @param[in]  block_last_index last index in scantable order that refers to a non zero element in block.
 */
static inline int get_block_rate(MpegEncContext *s, int16_t block[64],
                                 int block_last_index, uint8_t scantable[64])
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
 * This function also restores s->block_last_index.
 * @param[in,out] block MB coefficients, these will be restored
 * @param[in] dir ac prediction direction for each 8x8 block
 * @param[out] st scantable for each 8x8 block
 * @param[in] zigzag_last_index index referring to the last non zero coefficient in zigzag order
 */
static inline void restore_ac_coeffs(MpegEncContext *s, int16_t block[6][64],
                                     const int dir[6], uint8_t *st[6],
                                     const int zigzag_last_index[6])
{
    int i, n;
    memcpy(s->block_last_index, zigzag_last_index, sizeof(int) * 6);

    for (n = 0; n < 6; n++) {
        int16_t *ac_val = s->ac_val[0][0] + s->block_index[n] * 16;

        st[n] = s->intra_scantable.permutated;
        if (dir[n]) {
            /* top prediction */
            for (i = 1; i < 8; i++)
                block[n][s->idsp.idct_permutation[i]] = ac_val[i + 8];
        } else {
            /* left prediction */
            for (i = 1; i < 8; i++)
                block[n][s->idsp.idct_permutation[i << 3]] = ac_val[i];
        }
    }
}

/**
 * Return the optimal value (0 or 1) for the ac_pred element for the given MB in MPEG-4.
 * This function will also update s->block_last_index and s->ac_val.
 * @param[in,out] block MB coefficients, these will be updated if 1 is returned
 * @param[in] dir ac prediction direction for each 8x8 block
 * @param[out] st scantable for each 8x8 block
 * @param[out] zigzag_last_index index referring to the last non zero coefficient in zigzag order
 */
static inline int decide_ac_pred(MpegEncContext *s, int16_t block[6][64],
                                 const int dir[6], uint8_t *st[6],
                                 int zigzag_last_index[6])
{
    int score = 0;
    int i, n;
    int8_t *const qscale_table = s->current_picture.qscale_table;

    memcpy(zigzag_last_index, s->block_last_index, sizeof(int) * 6);

    for (n = 0; n < 6; n++) {
        int16_t *ac_val, *ac_val1;

        score -= get_block_rate(s, block[n], s->block_last_index[n],
                                s->intra_scantable.permutated);

        ac_val  = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val1 = ac_val;
        if (dir[n]) {
            const int xy = s->mb_x + s->mb_y * s->mb_stride - s->mb_stride;
            /* top prediction */
            ac_val -= s->block_wrap[n] * 16;
            if (s->mb_y == 0 || s->qscale == qscale_table[xy] || n == 2 || n == 3) {
                /* same qscale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->idsp.idct_permutation[i]];
                    block[n][s->idsp.idct_permutation[i]] = level - ac_val[i + 8];
                    ac_val1[i]     = block[n][s->idsp.idct_permutation[i << 3]];
                    ac_val1[i + 8] = level;
                }
            } else {
                /* different qscale, we must rescale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->idsp.idct_permutation[i]];
                    block[n][s->idsp.idct_permutation[i]] = level - ROUNDED_DIV(ac_val[i + 8] * qscale_table[xy], s->qscale);
                    ac_val1[i]     = block[n][s->idsp.idct_permutation[i << 3]];
                    ac_val1[i + 8] = level;
                }
            }
            st[n] = s->intra_h_scantable.permutated;
        } else {
            const int xy = s->mb_x - 1 + s->mb_y * s->mb_stride;
            /* left prediction */
            ac_val -= 16;
            if (s->mb_x == 0 || s->qscale == qscale_table[xy] || n == 1 || n == 3) {
                /* same qscale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->idsp.idct_permutation[i << 3]];
                    block[n][s->idsp.idct_permutation[i << 3]] = level - ac_val[i];
                    ac_val1[i]     = level;
                    ac_val1[i + 8] = block[n][s->idsp.idct_permutation[i]];
                }
            } else {
                /* different qscale, we must rescale */
                for (i = 1; i < 8; i++) {
                    const int level = block[n][s->idsp.idct_permutation[i << 3]];
                    block[n][s->idsp.idct_permutation[i << 3]] = level - ROUNDED_DIV(ac_val[i] * qscale_table[xy], s->qscale);
                    ac_val1[i]     = level;
                    ac_val1[i + 8] = block[n][s->idsp.idct_permutation[i]];
                }
            }
            st[n] = s->intra_v_scantable.permutated;
        }

        for (i = 63; i > 0; i--)  // FIXME optimize
            if (block[n][st[n][i]])
                break;
        s->block_last_index[n] = i;

        score += get_block_rate(s, block[n], s->block_last_index[n], st[n]);
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
void ff_clean_mpeg4_qscales(MpegEncContext *s)
{
    int i;
    int8_t *const qscale_table = s->current_picture.qscale_table;

    ff_clean_h263_qscales(s);

    if (s->pict_type == AV_PICTURE_TYPE_B) {
        int odd = 0;
        /* ok, come on, this isn't funny anymore, there's more code for
         * handling this MPEG-4 mess than for the actual adaptive quantization */

        for (i = 0; i < s->mb_num; i++) {
            int mb_xy = s->mb_index2xy[i];
            odd += qscale_table[mb_xy] & 1;
        }

        if (2 * odd > s->mb_num)
            odd = 1;
        else
            odd = 0;

        for (i = 0; i < s->mb_num; i++) {
            int mb_xy = s->mb_index2xy[i];
            if ((qscale_table[mb_xy] & 1) != odd)
                qscale_table[mb_xy]++;
            if (qscale_table[mb_xy] > 31)
                qscale_table[mb_xy] = 31;
        }

        for (i = 1; i < s->mb_num; i++) {
            int mb_xy = s->mb_index2xy[i];
            if (qscale_table[mb_xy] != qscale_table[s->mb_index2xy[i - 1]] &&
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

static inline int mpeg4_get_dc_length(int level, int n)
{
    if (n < 4)
        return uni_DCtab_lum_len[level + 256];
    else
        return uni_DCtab_chrom_len[level + 256];
}

/**
 * Encode an 8x8 block.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static inline void mpeg4_encode_block(MpegEncContext *s,
                                      int16_t *block, int n, int intra_dc,
                                      uint8_t *scan_table, PutBitContext *dc_pb,
                                      PutBitContext *ac_pb)
{
    int i, last_non_zero;
    uint32_t *bits_tab;
    uint8_t *len_tab;
    const int last_index = s->block_last_index[n];

    if (s->mb_intra) {  // Note gcc (3.2.1 at least) will optimize this away
        /* MPEG-4 based DC predictor */
        mpeg4_encode_dc(dc_pb, intra_dc, n);
        if (last_index < 1)
            return;
        i = 1;
        bits_tab = uni_mpeg4_intra_rl_bits;
        len_tab  = uni_mpeg4_intra_rl_len;
    } else {
        if (last_index < 0)
            return;
        i = 0;
        bits_tab = uni_mpeg4_inter_rl_bits;
        len_tab  = uni_mpeg4_inter_rl_len;
    }

    /* AC coefs */
    last_non_zero = i - 1;
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

static int mpeg4_get_block_length(MpegEncContext *s,
                                  int16_t *block, int n,
                                  int intra_dc, uint8_t *scan_table)
{
    int i, last_non_zero;
    uint8_t *len_tab;
    const int last_index = s->block_last_index[n];
    int len = 0;

    if (s->mb_intra) {  // Note gcc (3.2.1 at least) will optimize this away
        /* MPEG-4 based DC predictor */
        len += mpeg4_get_dc_length(intra_dc, n);
        if (last_index < 1)
            return len;
        i = 1;
        len_tab = uni_mpeg4_intra_rl_len;
    } else {
        if (last_index < 0)
            return 0;
        i = 0;
        len_tab = uni_mpeg4_inter_rl_len;
    }

    /* AC coefs */
    last_non_zero = i - 1;
    for (; i < last_index; i++) {
        int level = block[scan_table[i]];
        if (level) {
            int run = i - last_non_zero - 1;
            level += 64;
            if ((level & (~127)) == 0) {
                const int index = UNI_MPEG4_ENC_INDEX(0, run, level);
                len += len_tab[index];
            } else {  // ESC3
                len += 7 + 2 + 1 + 6 + 1 + 12 + 1;
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
            len += len_tab[index];
        } else {  // ESC3
            len += 7 + 2 + 1 + 6 + 1 + 12 + 1;
        }
    }

    return len;
}

static inline void mpeg4_encode_blocks(MpegEncContext *s, int16_t block[6][64],
                                       int intra_dc[6], uint8_t **scan_table,
                                       PutBitContext *dc_pb,
                                       PutBitContext *ac_pb)
{
    int i;

    if (scan_table) {
        if (s->avctx->flags2 & AV_CODEC_FLAG2_NO_OUTPUT) {
            for (i = 0; i < 6; i++)
                skip_put_bits(&s->pb,
                              mpeg4_get_block_length(s, block[i], i,
                                                     intra_dc[i], scan_table[i]));
        } else {
            /* encode each block */
            for (i = 0; i < 6; i++)
                mpeg4_encode_block(s, block[i], i,
                                   intra_dc[i], scan_table[i], dc_pb, ac_pb);
        }
    } else {
        if (s->avctx->flags2 & AV_CODEC_FLAG2_NO_OUTPUT) {
            for (i = 0; i < 6; i++)
                skip_put_bits(&s->pb,
                              mpeg4_get_block_length(s, block[i], i, 0,
                                                     s->intra_scantable.permutated));
        } else {
            /* encode each block */
            for (i = 0; i < 6; i++)
                mpeg4_encode_block(s, block[i], i, 0,
                                   s->intra_scantable.permutated, dc_pb, ac_pb);
        }
    }
}

static inline int get_b_cbp(MpegEncContext *s, int16_t block[6][64],
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
            if (s->block_last_index[i] >= 0 && ((cbp >> (5 - i)) & 1) == 0) {
                s->block_last_index[i] = -1;
                s->bdsp.clear_block(s->block[i]);
            }
        }
    } else {
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
    }
    return cbp;
}

// FIXME this is duplicated to h263.c
static const int dquant_code[5] = { 1, 0, 9, 2, 3 };

void ff_mpeg4_encode_mb(MpegEncContext *s, int16_t block[6][64],
                        int motion_x, int motion_y)
{
    int cbpc, cbpy, pred_x, pred_y;
    PutBitContext *const pb2    = s->data_partitioning ? &s->pb2 : &s->pb;
    PutBitContext *const tex_pb = s->data_partitioning && s->pict_type != AV_PICTURE_TYPE_B ? &s->tex_pb : &s->pb;
    PutBitContext *const dc_pb  = s->data_partitioning && s->pict_type != AV_PICTURE_TYPE_I ? &s->pb2 : &s->pb;
    const int interleaved_stats = (s->avctx->flags & AV_CODEC_FLAG_PASS1) && !s->data_partitioning ? 1 : 0;

    if (!s->mb_intra) {
        int i, cbp;

        if (s->pict_type == AV_PICTURE_TYPE_B) {
            /* convert from mv_dir to type */
            static const int mb_type_table[8] = { -1, 3, 2, 1, -1, -1, -1, 0 };
            int mb_type = mb_type_table[s->mv_dir];

            if (s->mb_x == 0) {
                for (i = 0; i < 2; i++)
                    s->last_mv[i][0][0] =
                    s->last_mv[i][0][1] =
                    s->last_mv[i][1][0] =
                    s->last_mv[i][1][1] = 0;
            }

            av_assert2(s->dquant >= -2 && s->dquant <= 2);
            av_assert2((s->dquant & 1) == 0);
            av_assert2(mb_type >= 0);

            /* nothing to do if this MB was skipped in the next P-frame */
            if (s->next_picture.mbskip_table[s->mb_y * s->mb_stride + s->mb_x]) {  // FIXME avoid DCT & ...
                s->skip_count++;
                s->mv[0][0][0] =
                s->mv[0][0][1] =
                s->mv[1][0][0] =
                s->mv[1][0][1] = 0;
                s->mv_dir  = MV_DIR_FORWARD;  // doesn't matter
                s->qscale -= s->dquant;
//                s->mb_skipped = 1;

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
                s->skip_count++;
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
                s->qscale -= s->dquant;

            if (!s->progressive_sequence) {
                if (cbp)
                    put_bits(&s->pb, 1, s->interlaced_dct);
                if (mb_type)                  // not direct mode
                    put_bits(&s->pb, 1, s->mv_type == MV_TYPE_FIELD);
            }

            if (interleaved_stats)
                s->misc_bits += get_bits_diff(s);

            if (!mb_type) {
                av_assert2(s->mv_dir & MV_DIRECT);
                ff_h263_encode_motion_vector(s, motion_x, motion_y, 1);
                s->b_count++;
                s->f_count++;
            } else {
                av_assert2(mb_type > 0 && mb_type < 4);
                if (s->mv_type != MV_TYPE_FIELD) {
                    if (s->mv_dir & MV_DIR_FORWARD) {
                        ff_h263_encode_motion_vector(s,
                                                     s->mv[0][0][0] - s->last_mv[0][0][0],
                                                     s->mv[0][0][1] - s->last_mv[0][0][1],
                                                     s->f_code);
                        s->last_mv[0][0][0] =
                        s->last_mv[0][1][0] = s->mv[0][0][0];
                        s->last_mv[0][0][1] =
                        s->last_mv[0][1][1] = s->mv[0][0][1];
                        s->f_count++;
                    }
                    if (s->mv_dir & MV_DIR_BACKWARD) {
                        ff_h263_encode_motion_vector(s,
                                                     s->mv[1][0][0] - s->last_mv[1][0][0],
                                                     s->mv[1][0][1] - s->last_mv[1][0][1],
                                                     s->b_code);
                        s->last_mv[1][0][0] =
                        s->last_mv[1][1][0] = s->mv[1][0][0];
                        s->last_mv[1][0][1] =
                        s->last_mv[1][1][1] = s->mv[1][0][1];
                        s->b_count++;
                    }
                } else {
                    if (s->mv_dir & MV_DIR_FORWARD) {
                        put_bits(&s->pb, 1, s->field_select[0][0]);
                        put_bits(&s->pb, 1, s->field_select[0][1]);
                    }
                    if (s->mv_dir & MV_DIR_BACKWARD) {
                        put_bits(&s->pb, 1, s->field_select[1][0]);
                        put_bits(&s->pb, 1, s->field_select[1][1]);
                    }
                    if (s->mv_dir & MV_DIR_FORWARD) {
                        for (i = 0; i < 2; i++) {
                            ff_h263_encode_motion_vector(s,
                                                         s->mv[0][i][0] - s->last_mv[0][i][0],
                                                         s->mv[0][i][1] - s->last_mv[0][i][1] / 2,
                                                         s->f_code);
                            s->last_mv[0][i][0] = s->mv[0][i][0];
                            s->last_mv[0][i][1] = s->mv[0][i][1] * 2;
                        }
                        s->f_count++;
                    }
                    if (s->mv_dir & MV_DIR_BACKWARD) {
                        for (i = 0; i < 2; i++) {
                            ff_h263_encode_motion_vector(s,
                                                         s->mv[1][i][0] - s->last_mv[1][i][0],
                                                         s->mv[1][i][1] - s->last_mv[1][i][1] / 2,
                                                         s->b_code);
                            s->last_mv[1][i][0] = s->mv[1][i][0];
                            s->last_mv[1][i][1] = s->mv[1][i][1] * 2;
                        }
                        s->b_count++;
                    }
                }
            }

            if (interleaved_stats)
                s->mv_bits += get_bits_diff(s);

            mpeg4_encode_blocks(s, block, NULL, NULL, NULL, &s->pb);

            if (interleaved_stats)
                s->p_tex_bits += get_bits_diff(s);
        } else { /* s->pict_type==AV_PICTURE_TYPE_B */
            cbp = get_p_cbp(s, block, motion_x, motion_y);

            if ((cbp | motion_x | motion_y | s->dquant) == 0 &&
                s->mv_type == MV_TYPE_16X16) {
                /* Check if the B-frames can skip it too, as we must skip it
                 * if we skip here why didn't they just compress
                 * the skip-mb bits instead of reusing them ?! */
                if (s->max_b_frames > 0) {
                    int i;
                    int x, y, offset;
                    uint8_t *p_pic;

                    x = s->mb_x * 16;
                    y = s->mb_y * 16;

                    offset = x + y * s->linesize;
                    p_pic  = s->new_picture.f->data[0] + offset;

                    s->mb_skipped = 1;
                    for (i = 0; i < s->max_b_frames; i++) {
                        uint8_t *b_pic;
                        int diff;
                        Picture *pic = s->reordered_input_picture[i + 1];

                        if (!pic || pic->f->pict_type != AV_PICTURE_TYPE_B)
                            break;

                        b_pic = pic->f->data[0] + offset;
                        if (!pic->shared)
                            b_pic += INPLACE_OFFSET;

                        if (x + 16 > s->width || y + 16 > s->height) {
                            int x1, y1;
                            int xe = FFMIN(16, s->width - x);
                            int ye = FFMIN(16, s->height - y);
                            diff = 0;
                            for (y1 = 0; y1 < ye; y1++) {
                                for (x1 = 0; x1 < xe; x1++) {
                                    diff += FFABS(p_pic[x1 + y1 * s->linesize] - b_pic[x1 + y1 * s->linesize]);
                                }
                            }
                            diff = diff * 256 / (xe * ye);
                        } else {
                            diff = s->mecc.sad[0](NULL, p_pic, b_pic, s->linesize, 16);
                        }
                        if (diff > s->qscale * 70) {  // FIXME check that 70 is optimal
                            s->mb_skipped = 0;
                            break;
                        }
                    }
                } else
                    s->mb_skipped = 1;

                if (s->mb_skipped == 1) {
                    /* skip macroblock */
                    put_bits(&s->pb, 1, 1);

                    if (interleaved_stats) {
                        s->misc_bits++;
                        s->last_bits++;
                    }
                    s->skip_count++;

                    return;
                }
            }

            put_bits(&s->pb, 1, 0);     /* mb coded */
            cbpc  = cbp & 3;
            cbpy  = cbp >> 2;
            cbpy ^= 0xf;
            if (s->mv_type == MV_TYPE_16X16) {
                if (s->dquant)
                    cbpc += 8;
                put_bits(&s->pb,
                         ff_h263_inter_MCBPC_bits[cbpc],
                         ff_h263_inter_MCBPC_code[cbpc]);

                put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);
                if (s->dquant)
                    put_bits(pb2, 2, dquant_code[s->dquant + 2]);

                if (!s->progressive_sequence) {
                    if (cbp)
                        put_bits(pb2, 1, s->interlaced_dct);
                    put_bits(pb2, 1, 0);
                }

                if (interleaved_stats)
                    s->misc_bits += get_bits_diff(s);

                /* motion vectors: 16x16 mode */
                ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);

                ff_h263_encode_motion_vector(s,
                                             motion_x - pred_x,
                                             motion_y - pred_y,
                                             s->f_code);
            } else if (s->mv_type == MV_TYPE_FIELD) {
                if (s->dquant)
                    cbpc += 8;
                put_bits(&s->pb,
                         ff_h263_inter_MCBPC_bits[cbpc],
                         ff_h263_inter_MCBPC_code[cbpc]);

                put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);
                if (s->dquant)
                    put_bits(pb2, 2, dquant_code[s->dquant + 2]);

                av_assert2(!s->progressive_sequence);
                if (cbp)
                    put_bits(pb2, 1, s->interlaced_dct);
                put_bits(pb2, 1, 1);

                if (interleaved_stats)
                    s->misc_bits += get_bits_diff(s);

                /* motion vectors: 16x8 interlaced mode */
                ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
                pred_y /= 2;

                put_bits(&s->pb, 1, s->field_select[0][0]);
                put_bits(&s->pb, 1, s->field_select[0][1]);

                ff_h263_encode_motion_vector(s,
                                             s->mv[0][0][0] - pred_x,
                                             s->mv[0][0][1] - pred_y,
                                             s->f_code);
                ff_h263_encode_motion_vector(s,
                                             s->mv[0][1][0] - pred_x,
                                             s->mv[0][1][1] - pred_y,
                                             s->f_code);
            } else {
                av_assert2(s->mv_type == MV_TYPE_8X8);
                put_bits(&s->pb,
                         ff_h263_inter_MCBPC_bits[cbpc + 16],
                         ff_h263_inter_MCBPC_code[cbpc + 16]);
                put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);

                if (!s->progressive_sequence && cbp)
                    put_bits(pb2, 1, s->interlaced_dct);

                if (interleaved_stats)
                    s->misc_bits += get_bits_diff(s);

                for (i = 0; i < 4; i++) {
                    /* motion vectors: 8x8 mode*/
                    ff_h263_pred_motion(s, i, 0, &pred_x, &pred_y);

                    ff_h263_encode_motion_vector(s,
                                                 s->current_picture.motion_val[0][s->block_index[i]][0] - pred_x,
                                                 s->current_picture.motion_val[0][s->block_index[i]][1] - pred_y,
                                                 s->f_code);
                }
            }

            if (interleaved_stats)
                s->mv_bits += get_bits_diff(s);

            mpeg4_encode_blocks(s, block, NULL, NULL, NULL, tex_pb);

            if (interleaved_stats)
                s->p_tex_bits += get_bits_diff(s);

            s->f_count++;
        }
    } else {
        int cbp;
        int dc_diff[6];  // dc values with the dc prediction subtracted
        int dir[6];      // prediction direction
        int zigzag_last_index[6];
        uint8_t *scan_table[6];
        int i;

        for (i = 0; i < 6; i++)
            dc_diff[i] = ff_mpeg4_pred_dc(s, i, block[i][0], &dir[i], 1);

        if (s->avctx->flags & AV_CODEC_FLAG_AC_PRED) {
            s->ac_pred = decide_ac_pred(s, block, dir, scan_table, zigzag_last_index);
        } else {
            for (i = 0; i < 6; i++)
                scan_table[i] = s->intra_scantable.permutated;
        }

        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++)
            if (s->block_last_index[i] >= 1)
                cbp |= 1 << (5 - i);

        cbpc = cbp & 3;
        if (s->pict_type == AV_PICTURE_TYPE_I) {
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
        put_bits(pb2, 1, s->ac_pred);
        cbpy = cbp >> 2;
        put_bits(pb2, ff_h263_cbpy_tab[cbpy][1], ff_h263_cbpy_tab[cbpy][0]);
        if (s->dquant)
            put_bits(dc_pb, 2, dquant_code[s->dquant + 2]);

        if (!s->progressive_sequence)
            put_bits(dc_pb, 1, s->interlaced_dct);

        if (interleaved_stats)
            s->misc_bits += get_bits_diff(s);

        mpeg4_encode_blocks(s, block, dc_diff, scan_table, dc_pb, tex_pb);

        if (interleaved_stats)
            s->i_tex_bits += get_bits_diff(s);
        s->i_count++;

        /* restore ac coeffs & last_index stuff
         * if we messed them up with the prediction */
        if (s->ac_pred)
            restore_ac_coeffs(s, block, dir, scan_table, zigzag_last_index);
    }
}

/**
 * add MPEG-4 stuffing bits (01...1)
 */
void ff_mpeg4_stuffing(PutBitContext *pbc)
{
    int length;
    put_bits(pbc, 1, 0);
    length = (-put_bits_count(pbc)) & 7;
    if (length)
        put_bits(pbc, length, (1 << length) - 1);
}

/* must be called before writing the header */
void ff_set_mpeg4_time(MpegEncContext *s)
{
    if (s->pict_type == AV_PICTURE_TYPE_B) {
        ff_mpeg4_init_direct_mv(s);
    } else {
        s->last_time_base = s->time_base;
        s->time_base      = FFUDIV(s->time, s->avctx->time_base.den);
    }
}

static void mpeg4_encode_gop_header(MpegEncContext *s)
{
    int64_t hours, minutes, seconds;
    int64_t time;

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, GOP_STARTCODE);

    time = s->current_picture_ptr->f->pts;
    if (s->reordered_input_picture[1])
        time = FFMIN(time, s->reordered_input_picture[1]->f->pts);
    time = time * s->avctx->time_base.num;
    s->last_time_base = FFUDIV(time, s->avctx->time_base.den);

    seconds = FFUDIV(time, s->avctx->time_base.den);
    minutes = FFUDIV(seconds, 60); seconds = FFUMOD(seconds, 60);
    hours   = FFUDIV(minutes, 60); minutes = FFUMOD(minutes, 60);
    hours   = FFUMOD(hours  , 24);

    put_bits(&s->pb, 5, hours);
    put_bits(&s->pb, 6, minutes);
    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 6, seconds);

    put_bits(&s->pb, 1, !!(s->avctx->flags & AV_CODEC_FLAG_CLOSED_GOP));
    put_bits(&s->pb, 1, 0);  // broken link == NO

    ff_mpeg4_stuffing(&s->pb);
}

static void mpeg4_encode_visual_object_header(MpegEncContext *s)
{
    int profile_and_level_indication;
    int vo_ver_id;

    if (s->avctx->profile != FF_PROFILE_UNKNOWN) {
        profile_and_level_indication = s->avctx->profile << 4;
    } else if (s->max_b_frames || s->quarter_sample) {
        profile_and_level_indication = 0xF0;  // adv simple
    } else {
        profile_and_level_indication = 0x00;  // simple
    }

    if (s->avctx->level != FF_LEVEL_UNKNOWN)
        profile_and_level_indication |= s->avctx->level;
    else
        profile_and_level_indication |= 1;   // level 1

    if (profile_and_level_indication >> 4 == 0xF)
        vo_ver_id = 5;
    else
        vo_ver_id = 1;

    // FIXME levels

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, VOS_STARTCODE);

    put_bits(&s->pb, 8, profile_and_level_indication);

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, VISUAL_OBJ_STARTCODE);

    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 4, vo_ver_id);
    put_bits(&s->pb, 3, 1);     // priority

    put_bits(&s->pb, 4, 1);     // visual obj type== video obj

    put_bits(&s->pb, 1, 0);     // video signal type == no clue // FIXME

    ff_mpeg4_stuffing(&s->pb);
}

static void mpeg4_encode_vol_header(MpegEncContext *s,
                                    int vo_number,
                                    int vol_number)
{
    int vo_ver_id;

    if (!CONFIG_MPEG4_ENCODER)
        return;

    if (s->max_b_frames || s->quarter_sample) {
        vo_ver_id  = 5;
        s->vo_type = ADV_SIMPLE_VO_TYPE;
    } else {
        vo_ver_id  = 1;
        s->vo_type = SIMPLE_VO_TYPE;
    }

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, 0x100 + vo_number);        /* video obj */
    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, 0x120 + vol_number);       /* video obj layer */

    put_bits(&s->pb, 1, 0);             /* random access vol */
    put_bits(&s->pb, 8, s->vo_type);    /* video obj type indication */
    if (s->workaround_bugs & FF_BUG_MS) {
        put_bits(&s->pb, 1, 0);         /* is obj layer id= no */
    } else {
        put_bits(&s->pb, 1, 1);         /* is obj layer id= yes */
        put_bits(&s->pb, 4, vo_ver_id); /* is obj layer ver id */
        put_bits(&s->pb, 3, 1);         /* is obj layer priority */
    }

    s->aspect_ratio_info = ff_h263_aspect_to_info(s->avctx->sample_aspect_ratio);

    put_bits(&s->pb, 4, s->aspect_ratio_info); /* aspect ratio info */
    if (s->aspect_ratio_info == FF_ASPECT_EXTENDED) {
        av_reduce(&s->avctx->sample_aspect_ratio.num, &s->avctx->sample_aspect_ratio.den,
                   s->avctx->sample_aspect_ratio.num,  s->avctx->sample_aspect_ratio.den, 255);
        put_bits(&s->pb, 8, s->avctx->sample_aspect_ratio.num);
        put_bits(&s->pb, 8, s->avctx->sample_aspect_ratio.den);
    }

    if (s->workaround_bugs & FF_BUG_MS) {
        put_bits(&s->pb, 1, 0);         /* vol control parameters= no @@@ */
    } else {
        put_bits(&s->pb, 1, 1);         /* vol control parameters= yes */
        put_bits(&s->pb, 2, 1);         /* chroma format YUV 420/YV12 */
        put_bits(&s->pb, 1, s->low_delay);
        put_bits(&s->pb, 1, 0);         /* vbv parameters= no */
    }

    put_bits(&s->pb, 2, RECT_SHAPE);    /* vol shape= rectangle */
    put_bits(&s->pb, 1, 1);             /* marker bit */

    put_bits(&s->pb, 16, s->avctx->time_base.den);
    if (s->time_increment_bits < 1)
        s->time_increment_bits = 1;
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 1, 0);             /* fixed vop rate=no */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 13, s->width);     /* vol width */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 13, s->height);    /* vol height */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 1, s->progressive_sequence ? 0 : 1);
    put_bits(&s->pb, 1, 1);             /* obmc disable */
    if (vo_ver_id == 1)
        put_bits(&s->pb, 1, 0);       /* sprite enable */
    else
        put_bits(&s->pb, 2, 0);       /* sprite enable */

    put_bits(&s->pb, 1, 0);             /* not 8 bit == false */
    put_bits(&s->pb, 1, s->mpeg_quant); /* quant type = (0 = H.263 style) */

    if (s->mpeg_quant) {
        ff_write_quant_matrix(&s->pb, s->avctx->intra_matrix);
        ff_write_quant_matrix(&s->pb, s->avctx->inter_matrix);
    }

    if (vo_ver_id != 1)
        put_bits(&s->pb, 1, s->quarter_sample);
    put_bits(&s->pb, 1, 1);             /* complexity estimation disable */
    put_bits(&s->pb, 1, s->rtp_mode ? 0 : 1); /* resync marker disable */
    put_bits(&s->pb, 1, s->data_partitioning ? 1 : 0);
    if (s->data_partitioning)
        put_bits(&s->pb, 1, 0);         /* no rvlc */

    if (vo_ver_id != 1) {
        put_bits(&s->pb, 1, 0);         /* newpred */
        put_bits(&s->pb, 1, 0);         /* reduced res vop */
    }
    put_bits(&s->pb, 1, 0);             /* scalability */

    ff_mpeg4_stuffing(&s->pb);

    /* user data */
    if (!(s->avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
        put_bits(&s->pb, 16, 0);
        put_bits(&s->pb, 16, 0x1B2);    /* user_data */
        avpriv_put_string(&s->pb, LIBAVCODEC_IDENT, 0);
    }
}

/* write MPEG-4 VOP header */
int ff_mpeg4_encode_picture_header(MpegEncContext *s, int picture_number)
{
    uint64_t time_incr;
    int64_t time_div, time_mod;

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        if (!(s->avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
            if (s->strict_std_compliance < FF_COMPLIANCE_VERY_STRICT)  // HACK, the reference sw is buggy
                mpeg4_encode_visual_object_header(s);
            if (s->strict_std_compliance < FF_COMPLIANCE_VERY_STRICT || picture_number == 0)  // HACK, the reference sw is buggy
                mpeg4_encode_vol_header(s, 0, 0);
        }
        if (!(s->workaround_bugs & FF_BUG_MS))
            mpeg4_encode_gop_header(s);
    }

    s->partitioned_frame = s->data_partitioning && s->pict_type != AV_PICTURE_TYPE_B;

    put_bits(&s->pb, 16, 0);                /* vop header */
    put_bits(&s->pb, 16, VOP_STARTCODE);    /* vop header */
    put_bits(&s->pb, 2, s->pict_type - 1);  /* pict type: I = 0 , P = 1 */

    time_div  = FFUDIV(s->time, s->avctx->time_base.den);
    time_mod  = FFUMOD(s->time, s->avctx->time_base.den);
    time_incr = time_div - s->last_time_base;

    // This limits the frame duration to max 1 hour
    if (time_incr > 3600) {
        av_log(s->avctx, AV_LOG_ERROR, "time_incr %"PRIu64" too large\n", time_incr);
        return AVERROR(EINVAL);
    }
    while (time_incr--)
        put_bits(&s->pb, 1, 1);

    put_bits(&s->pb, 1, 0);

    put_bits(&s->pb, 1, 1);                             /* marker */
    put_bits(&s->pb, s->time_increment_bits, time_mod); /* time increment */
    put_bits(&s->pb, 1, 1);                             /* marker */
    put_bits(&s->pb, 1, 1);                             /* vop coded */
    if (s->pict_type == AV_PICTURE_TYPE_P) {
        put_bits(&s->pb, 1, s->no_rounding);    /* rounding type */
    }
    put_bits(&s->pb, 3, 0);     /* intra dc VLC threshold */
    if (!s->progressive_sequence) {
        put_bits(&s->pb, 1, s->current_picture_ptr->f->top_field_first);
        put_bits(&s->pb, 1, s->alternate_scan);
    }
    // FIXME sprite stuff

    put_bits(&s->pb, 5, s->qscale);

    if (s->pict_type != AV_PICTURE_TYPE_I)
        put_bits(&s->pb, 3, s->f_code);  /* fcode_for */
    if (s->pict_type == AV_PICTURE_TYPE_B)
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
    int slevel, run, last;

    av_assert0(MAX_LEVEL >= 64);
    av_assert0(MAX_RUN >= 63);

    for (slevel = -64; slevel < 64; slevel++) {
        if (slevel == 0)
            continue;
        for (run = 0; run < 64; run++) {
            for (last = 0; last <= 1; last++) {
                const int index = UNI_MPEG4_ENC_INDEX(last, run, slevel + 64);
                int level       = slevel < 0 ? -slevel : slevel;
                int sign        = slevel < 0 ? 1 : 0;
                int bits, len, code;
                int level1, run1;

                len_tab[index] = 100;

                /* ESC0 */
                code = get_rl_index(rl, last, run, level);
                bits = rl->table_vlc[code][0];
                len  = rl->table_vlc[code][1];
                bits = bits * 2 + sign;
                len++;

                if (code != rl->n && len < len_tab[index]) {
                    bits_tab[index] = bits;
                    len_tab[index]  = len;
                }
                /* ESC1 */
                bits = rl->table_vlc[rl->n][0];
                len  = rl->table_vlc[rl->n][1];
                bits = bits * 2;
                len++;                 // esc1
                level1 = level - rl->max_level[last][run];
                if (level1 > 0) {
                    code   = get_rl_index(rl, last, run, level1);
                    bits <<= rl->table_vlc[code][1];
                    len   += rl->table_vlc[code][1];
                    bits  += rl->table_vlc[code][0];
                    bits   = bits * 2 + sign;
                    len++;

                    if (code != rl->n && len < len_tab[index]) {
                        bits_tab[index] = bits;
                        len_tab[index]  = len;
                    }
                }
                /* ESC2 */
                bits = rl->table_vlc[rl->n][0];
                len  = rl->table_vlc[rl->n][1];
                bits = bits * 4 + 2;
                len += 2;                 // esc2
                run1 = run - rl->max_run[last][level] - 1;
                if (run1 >= 0) {
                    code   = get_rl_index(rl, last, run1, level);
                    bits <<= rl->table_vlc[code][1];
                    len   += rl->table_vlc[code][1];
                    bits  += rl->table_vlc[code][0];
                    bits   = bits * 2 + sign;
                    len++;

                    if (code != rl->n && len < len_tab[index]) {
                        bits_tab[index] = bits;
                        len_tab[index]  = len;
                    }
                }
                /* ESC3 */
                bits = rl->table_vlc[rl->n][0];
                len  = rl->table_vlc[rl->n][1];
                bits = bits * 4 + 3;
                len += 2;                 // esc3
                bits = bits * 2 + last;
                len++;
                bits = bits * 64 + run;
                len += 6;
                bits = bits * 2 + 1;
                len++;                    // marker
                bits = bits * 4096 + (slevel & 0xfff);
                len += 12;
                bits = bits * 2 + 1;
                len++;                    // marker

                if (len < len_tab[index]) {
                    bits_tab[index] = bits;
                    len_tab[index]  = len;
                }
            }
        }
    }
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int ret;
    static int done = 0;

    if (avctx->width >= (1<<13) || avctx->height >= (1<<13)) {
        av_log(avctx, AV_LOG_ERROR, "dimensions too large for MPEG-4\n");
        return AVERROR(EINVAL);
    }

    if ((ret = ff_mpv_encode_init(avctx)) < 0)
        return ret;

    if (!done) {
        done = 1;

        init_uni_dc_tab();

        ff_rl_init(&ff_mpeg4_rl_intra, ff_mpeg4_static_rl_table_store[0]);

        init_uni_mpeg4_rl_tab(&ff_mpeg4_rl_intra, uni_mpeg4_intra_rl_bits, uni_mpeg4_intra_rl_len);
        init_uni_mpeg4_rl_tab(&ff_h263_rl_inter, uni_mpeg4_inter_rl_bits, uni_mpeg4_inter_rl_len);
    }

    s->min_qcoeff               = -2048;
    s->max_qcoeff               = 2047;
    s->intra_ac_vlc_length      = uni_mpeg4_intra_rl_len;
    s->intra_ac_vlc_last_length = uni_mpeg4_intra_rl_len + 128 * 64;
    s->inter_ac_vlc_length      = uni_mpeg4_inter_rl_len;
    s->inter_ac_vlc_last_length = uni_mpeg4_inter_rl_len + 128 * 64;
    s->luma_dc_vlc_length       = uni_DCtab_lum_len;
    s->ac_esc_length            = 7 + 2 + 1 + 6 + 1 + 12 + 1;
    s->y_dc_scale_table         = ff_mpeg4_y_dc_scale_table;
    s->c_dc_scale_table         = ff_mpeg4_c_dc_scale_table;

    if (s->avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        s->avctx->extradata = av_malloc(1024);
        init_put_bits(&s->pb, s->avctx->extradata, 1024);

        if (!(s->workaround_bugs & FF_BUG_MS))
            mpeg4_encode_visual_object_header(s);
        mpeg4_encode_vol_header(s, 0, 0);

//            ff_mpeg4_stuffing(&s->pb); ?
        flush_put_bits(&s->pb);
        s->avctx->extradata_size = (put_bits_count(&s->pb) + 7) >> 3;
    }
    return 0;
}

void ff_mpeg4_init_partitions(MpegEncContext *s)
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

void ff_mpeg4_merge_partitions(MpegEncContext *s)
{
    const int pb2_len    = put_bits_count(&s->pb2);
    const int tex_pb_len = put_bits_count(&s->tex_pb);
    const int bits       = put_bits_count(&s->pb);

    if (s->pict_type == AV_PICTURE_TYPE_I) {
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
    avpriv_copy_bits(&s->pb, s->pb2.buf, pb2_len);
    avpriv_copy_bits(&s->pb, s->tex_pb.buf, tex_pb_len);
    s->last_bits = put_bits_count(&s->pb);
}

void ff_mpeg4_encode_video_packet_header(MpegEncContext *s)
{
    int mb_num_bits = av_log2(s->mb_num - 1) + 1;

    put_bits(&s->pb, ff_mpeg4_get_video_packet_prefix_length(s), 0);
    put_bits(&s->pb, 1, 1);

    put_bits(&s->pb, mb_num_bits, s->mb_x + s->mb_y * s->mb_width);
    put_bits(&s->pb, s->quant_precision, s->qscale);
    put_bits(&s->pb, 1, 0); /* no HEC */
}

#define OFFSET(x) offsetof(MpegEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "data_partitioning", "Use data partitioning.",      OFFSET(data_partitioning), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "alternate_scan",    "Enable alternate scantable.", OFFSET(alternate_scan),    AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    FF_MPV_COMMON_OPTS
    { NULL },
};

static const AVClass mpeg4enc_class = {
    .class_name = "MPEG4 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mpeg4_encoder = {
    .name           = "mpeg4",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-4 part 2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG4,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_SLICE_THREADS,
    .priv_class     = &mpeg4enc_class,
};
