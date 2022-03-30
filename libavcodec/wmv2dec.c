/*
 * Copyright (c) 2002 The FFmpeg Project
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

#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "h263dec.h"
#include "intrax8.h"
#include "mathops.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "msmpeg4.h"
#include "msmpeg4data.h"
#include "msmpeg4dec.h"
#include "simple_idct.h"
#include "wmv2.h"
#include "wmv2data.h"
#include "wmv2dec.h"

typedef struct WMV2DecContext {
    MpegEncContext s;
    WMV2Context common;
    IntraX8Context x8;
    int j_type_bit;
    int j_type;
    int abt_flag;
    int abt_type;
    int abt_type_table[6];
    int per_mb_abt;
    int per_block_abt;
    int mspel_bit;
    int cbp_table_index;
    int top_left_mv_flag;
    int per_mb_rl_bit;
    int skip_type;

    ScanTable abt_scantable[2];
    DECLARE_ALIGNED(32, int16_t, abt_block2)[6][64];
} WMV2DecContext;

static void wmv2_add_block(WMV2DecContext *w, int16_t *block1,
                           uint8_t *dst, int stride, int n)
{
    MpegEncContext *const s = &w->s;

    if (s->block_last_index[n] >= 0) {
        switch (w->abt_type_table[n]) {
        case 0:
            w->common.wdsp.idct_add(dst, stride, block1);
            break;
        case 1:
            ff_simple_idct84_add(dst, stride, block1);
            ff_simple_idct84_add(dst + 4 * stride, stride, w->abt_block2[n]);
            s->bdsp.clear_block(w->abt_block2[n]);
            break;
        case 2:
            ff_simple_idct48_add(dst, stride, block1);
            ff_simple_idct48_add(dst + 4, stride, w->abt_block2[n]);
            s->bdsp.clear_block(w->abt_block2[n]);
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "internal error in WMV2 abt\n");
        }
    }
}

void ff_wmv2_add_mb(MpegEncContext *s, int16_t block1[6][64],
                    uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr)
{
    WMV2DecContext *const w = (WMV2DecContext *) s;

    wmv2_add_block(w, block1[0], dest_y,                       s->linesize, 0);
    wmv2_add_block(w, block1[1], dest_y + 8,                   s->linesize, 1);
    wmv2_add_block(w, block1[2], dest_y + 8 * s->linesize,     s->linesize, 2);
    wmv2_add_block(w, block1[3], dest_y + 8 + 8 * s->linesize, s->linesize, 3);

    if (s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;

    wmv2_add_block(w, block1[4], dest_cb, s->uvlinesize, 4);
    wmv2_add_block(w, block1[5], dest_cr, s->uvlinesize, 5);
}

static int parse_mb_skip(WMV2DecContext *w)
{
    int mb_x, mb_y;
    int coded_mb_count = 0;
    MpegEncContext *const s = &w->s;
    uint32_t *const mb_type = s->current_picture_ptr->mb_type;

    w->skip_type = get_bits(&s->gb, 2);
    switch (w->skip_type) {
    case SKIP_TYPE_NONE:
        for (mb_y = 0; mb_y < s->mb_height; mb_y++)
            for (mb_x = 0; mb_x < s->mb_width; mb_x++)
                mb_type[mb_y * s->mb_stride + mb_x] =
                    MB_TYPE_16x16 | MB_TYPE_L0;
        break;
    case SKIP_TYPE_MPEG:
        if (get_bits_left(&s->gb) < s->mb_height * s->mb_width)
            return AVERROR_INVALIDDATA;
        for (mb_y = 0; mb_y < s->mb_height; mb_y++)
            for (mb_x = 0; mb_x < s->mb_width; mb_x++)
                mb_type[mb_y * s->mb_stride + mb_x] =
                    (get_bits1(&s->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_L0;
        break;
    case SKIP_TYPE_ROW:
        for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
            if (get_bits_left(&s->gb) < 1)
                return AVERROR_INVALIDDATA;
            if (get_bits1(&s->gb)) {
                for (mb_x = 0; mb_x < s->mb_width; mb_x++)
                    mb_type[mb_y * s->mb_stride + mb_x] =
                        MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
            } else {
                for (mb_x = 0; mb_x < s->mb_width; mb_x++)
                    mb_type[mb_y * s->mb_stride + mb_x] =
                        (get_bits1(&s->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_L0;
            }
        }
        break;
    case SKIP_TYPE_COL:
        for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (get_bits_left(&s->gb) < 1)
                return AVERROR_INVALIDDATA;
            if (get_bits1(&s->gb)) {
                for (mb_y = 0; mb_y < s->mb_height; mb_y++)
                    mb_type[mb_y * s->mb_stride + mb_x] =
                        MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
            } else {
                for (mb_y = 0; mb_y < s->mb_height; mb_y++)
                    mb_type[mb_y * s->mb_stride + mb_x] =
                        (get_bits1(&s->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_L0;
            }
        }
        break;
    }

    for (mb_y = 0; mb_y < s->mb_height; mb_y++)
        for (mb_x = 0; mb_x < s->mb_width; mb_x++)
            coded_mb_count += !IS_SKIP(mb_type[mb_y * s->mb_stride + mb_x]);

    if (coded_mb_count > get_bits_left(&s->gb))
        return AVERROR_INVALIDDATA;

    return 0;
}

static int decode_ext_header(WMV2DecContext *w)
{
    MpegEncContext *const s = &w->s;
    GetBitContext gb;
    int fps;
    int code;

    if (s->avctx->extradata_size < 4)
        return AVERROR_INVALIDDATA;

    init_get_bits(&gb, s->avctx->extradata, 32);

    fps                 = get_bits(&gb, 5);
    s->bit_rate         = get_bits(&gb, 11) * 1024;
    w->mspel_bit        = get_bits1(&gb);
    s->loop_filter      = get_bits1(&gb);
    w->abt_flag         = get_bits1(&gb);
    w->j_type_bit       = get_bits1(&gb);
    w->top_left_mv_flag = get_bits1(&gb);
    w->per_mb_rl_bit    = get_bits1(&gb);
    code                = get_bits(&gb, 3);

    if (code == 0)
        return AVERROR_INVALIDDATA;

    s->slice_height = s->mb_height / code;

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_DEBUG,
               "fps:%d, br:%"PRId64", qpbit:%d, abt_flag:%d, j_type_bit:%d, "
               "tl_mv_flag:%d, mbrl_bit:%d, code:%d, loop_filter:%d, "
               "slices:%d\n",
               fps, s->bit_rate, w->mspel_bit, w->abt_flag, w->j_type_bit,
               w->top_left_mv_flag, w->per_mb_rl_bit, code, s->loop_filter,
               code);
    return 0;
}

int ff_wmv2_decode_picture_header(MpegEncContext *s)
{
    WMV2DecContext *const w = (WMV2DecContext *) s;
    int code;

    if (s->picture_number == 0)
        decode_ext_header(w);

    s->pict_type = get_bits1(&s->gb) + 1;
    if (s->pict_type == AV_PICTURE_TYPE_I) {
        code = get_bits(&s->gb, 7);
        av_log(s->avctx, AV_LOG_DEBUG, "I7:%X/\n", code);
    }
    s->chroma_qscale = s->qscale = get_bits(&s->gb, 5);
    if (s->qscale <= 0)
        return AVERROR_INVALIDDATA;

    if (s->pict_type != AV_PICTURE_TYPE_I && show_bits(&s->gb, 1)) {
        GetBitContext gb = s->gb;
        int skip_type = get_bits(&gb, 2);
        int run = skip_type == SKIP_TYPE_COL ? s->mb_width : s->mb_height;

        while (run > 0) {
            int block = FFMIN(run, 25);
            if (get_bits(&gb, block) + 1 != 1<<block)
                break;
            run -= block;
        }
        if (!run)
            return FRAME_SKIPPED;
    }

    return 0;
}

int ff_wmv2_decode_secondary_picture_header(MpegEncContext *s)
{
    WMV2DecContext *const w = (WMV2DecContext *) s;

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        if (w->j_type_bit)
            w->j_type = get_bits1(&s->gb);
        else
            w->j_type = 0; // FIXME check

        if (!w->j_type) {
            if (w->per_mb_rl_bit)
                s->per_mb_rl_table = get_bits1(&s->gb);
            else
                s->per_mb_rl_table = 0;

            if (!s->per_mb_rl_table) {
                s->rl_chroma_table_index = decode012(&s->gb);
                s->rl_table_index        = decode012(&s->gb);
            }

            s->dc_table_index = get_bits1(&s->gb);

            // at minimum one bit per macroblock is required at least in a valid frame,
            // we discard frames much smaller than this. Frames smaller than 1/8 of the
            // smallest "black/skip" frame generally contain not much recoverable content
            // while at the same time they have the highest computational requirements
            // per byte
            if (get_bits_left(&s->gb) * 8LL < (s->width+15)/16 * ((s->height+15)/16))
                return AVERROR_INVALIDDATA;
        }
        s->inter_intra_pred = 0;
        s->no_rounding      = 1;
        if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(s->avctx, AV_LOG_DEBUG,
                   "qscale:%d rlc:%d rl:%d dc:%d mbrl:%d j_type:%d \n",
                   s->qscale, s->rl_chroma_table_index, s->rl_table_index,
                   s->dc_table_index, s->per_mb_rl_table, w->j_type);
        }
    } else {
        int cbp_index;
        int ret;
        w->j_type = 0;

        ret = parse_mb_skip(w);
        if (ret < 0)
            return ret;
        cbp_index = decode012(&s->gb);
        w->cbp_table_index = wmv2_get_cbp_table_index(s, cbp_index);

        if (w->mspel_bit)
            s->mspel = get_bits1(&s->gb);
        else
            s->mspel = 0; // FIXME check

        if (w->abt_flag) {
            w->per_mb_abt = get_bits1(&s->gb) ^ 1;
            if (!w->per_mb_abt)
                w->abt_type = decode012(&s->gb);
        }

        if (w->per_mb_rl_bit)
            s->per_mb_rl_table = get_bits1(&s->gb);
        else
            s->per_mb_rl_table = 0;

        if (!s->per_mb_rl_table) {
            s->rl_table_index        = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }

        if (get_bits_left(&s->gb) < 2)
            return AVERROR_INVALIDDATA;

        s->dc_table_index   = get_bits1(&s->gb);
        s->mv_table_index   = get_bits1(&s->gb);

        s->inter_intra_pred = 0; // (s->width * s->height < 320 * 240 && s->bit_rate <= II_BITRATE);
        s->no_rounding     ^= 1;

        if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(s->avctx, AV_LOG_DEBUG,
                   "rl:%d rlc:%d dc:%d mv:%d mbrl:%d qp:%d mspel:%d "
                   "per_mb_abt:%d abt_type:%d cbp:%d ii:%d\n",
                   s->rl_table_index, s->rl_chroma_table_index,
                   s->dc_table_index, s->mv_table_index,
                   s->per_mb_rl_table, s->qscale, s->mspel,
                   w->per_mb_abt, w->abt_type, w->cbp_table_index,
                   s->inter_intra_pred);
        }
    }
    s->esc3_level_length = 0;
    s->esc3_run_length   = 0;
    s->picture_number++; // FIXME ?

    if (w->j_type) {
        ff_intrax8_decode_picture(&w->x8, &s->current_picture,
                                  &s->gb, &s->mb_x, &s->mb_y,
                                  2 * s->qscale, (s->qscale - 1) | 1,
                                  s->loop_filter, s->low_delay);

        ff_er_add_slice(&w->s.er, 0, 0,
                        (w->s.mb_x >> 1) - 1, (w->s.mb_y >> 1) - 1,
                        ER_MB_END);
        return 1;
    }

    return 0;
}

static inline void wmv2_decode_motion(WMV2DecContext *w, int *mx_ptr, int *my_ptr)
{
    MpegEncContext *const s = &w->s;

    ff_msmpeg4_decode_motion(s, mx_ptr, my_ptr);

    if ((((*mx_ptr) | (*my_ptr)) & 1) && s->mspel)
        w->common.hshift = get_bits1(&s->gb);
    else
        w->common.hshift = 0;
}

static int16_t *wmv2_pred_motion(WMV2DecContext *w, int *px, int *py)
{
    MpegEncContext *const s = &w->s;
    int xy, wrap, diff, type;
    int16_t *A, *B, *C, *mot_val;

    wrap    = s->b8_stride;
    xy      = s->block_index[0];

    mot_val = s->current_picture.motion_val[0][xy];

    A       = s->current_picture.motion_val[0][xy     - 1];
    B       = s->current_picture.motion_val[0][xy     - wrap];
    C       = s->current_picture.motion_val[0][xy + 2 - wrap];

    if (s->mb_x && !s->first_slice_line && !s->mspel && w->top_left_mv_flag)
        diff = FFMAX(FFABS(A[0] - B[0]), FFABS(A[1] - B[1]));
    else
        diff = 0;

    if (diff >= 8)
        type = get_bits1(&s->gb);
    else
        type = 2;

    if (type == 0) {
        *px = A[0];
        *py = A[1];
    } else if (type == 1) {
        *px = B[0];
        *py = B[1];
    } else {
        /* special case for first (slice) line */
        if (s->first_slice_line) {
            *px = A[0];
            *py = A[1];
        } else {
            *px = mid_pred(A[0], B[0], C[0]);
            *py = mid_pred(A[1], B[1], C[1]);
        }
    }

    return mot_val;
}

static inline int wmv2_decode_inter_block(WMV2DecContext *w, int16_t *block,
                                          int n, int cbp)
{
    MpegEncContext *const s = &w->s;
    static const int sub_cbp_table[3] = { 2, 3, 1 };
    int sub_cbp, ret;

    if (!cbp) {
        s->block_last_index[n] = -1;
        return 0;
    }

    if (w->per_block_abt)
        w->abt_type = decode012(&s->gb);
    w->abt_type_table[n] = w->abt_type;

    if (w->abt_type) {
//        const uint8_t *scantable = w->abt_scantable[w->abt_type - 1].permutated;
        const uint8_t *scantable = w->abt_scantable[w->abt_type - 1].scantable;
//        const uint8_t *scantable = w->abt_type - 1 ? w->abt_scantable[1].permutated : w->abt_scantable[0].scantable;

        sub_cbp = sub_cbp_table[decode012(&s->gb)];

        if (sub_cbp & 1)
            if ((ret = ff_msmpeg4_decode_block(s, block, n, 1, scantable)) < 0)
                return ret;

        if (sub_cbp & 2)
            if ((ret = ff_msmpeg4_decode_block(s, w->abt_block2[n], n, 1, scantable)) < 0)
                return ret;

        s->block_last_index[n] = 63;

        return 0;
    } else {
        return ff_msmpeg4_decode_block(s, block, n, 1,
                                       s->inter_scantable.permutated);
    }
}

int ff_wmv2_decode_mb(MpegEncContext *s, int16_t block[6][64])
{
    /* The following is only allowed because this encoder
     * does not use slice threading. */
    WMV2DecContext *const w = (WMV2DecContext *) s;
    int cbp, code, i, ret;
    uint8_t *coded_val;

    if (w->j_type)
        return 0;

    if (s->pict_type == AV_PICTURE_TYPE_P) {
        if (IS_SKIP(s->current_picture.mb_type[s->mb_y * s->mb_stride + s->mb_x])) {
            /* skip mb */
            s->mb_intra = 0;
            for (i = 0; i < 6; i++)
                s->block_last_index[i] = -1;
            s->mv_dir      = MV_DIR_FORWARD;
            s->mv_type     = MV_TYPE_16X16;
            s->mv[0][0][0] = 0;
            s->mv[0][0][1] = 0;
            s->mb_skipped  = 1;
            w->common.hshift      = 0;
            return 0;
        }
        if (get_bits_left(&s->gb) <= 0)
            return AVERROR_INVALIDDATA;

        code = get_vlc2(&s->gb, ff_mb_non_intra_vlc[w->cbp_table_index].table,
                        MB_NON_INTRA_VLC_BITS, 3);
        s->mb_intra = (~code & 0x40) >> 6;

        cbp = code & 0x3f;
    } else {
        s->mb_intra = 1;
        if (get_bits_left(&s->gb) <= 0)
            return AVERROR_INVALIDDATA;
        code = get_vlc2(&s->gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
        /* predict coded block pattern */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            int val = ((code >> (5 - i)) & 1);
            if (i < 4) {
                int pred   = ff_msmpeg4_coded_block_pred(s, i, &coded_val);
                val        = val ^ pred;
                *coded_val = val;
            }
            cbp |= val << (5 - i);
        }
    }

    if (!s->mb_intra) {
        int mx, my;
        wmv2_pred_motion(w, &mx, &my);

        if (cbp) {
            s->bdsp.clear_blocks(s->block[0]);
            if (s->per_mb_rl_table) {
                s->rl_table_index        = decode012(&s->gb);
                s->rl_chroma_table_index = s->rl_table_index;
            }

            if (w->abt_flag && w->per_mb_abt) {
                w->per_block_abt = get_bits1(&s->gb);
                if (!w->per_block_abt)
                    w->abt_type = decode012(&s->gb);
            } else
                w->per_block_abt = 0;
        }

        wmv2_decode_motion(w, &mx, &my);

        s->mv_dir      = MV_DIR_FORWARD;
        s->mv_type     = MV_TYPE_16X16;
        s->mv[0][0][0] = mx;
        s->mv[0][0][1] = my;

        for (i = 0; i < 6; i++) {
            if ((ret = wmv2_decode_inter_block(w, block[i], i, (cbp >> (5 - i)) & 1)) < 0) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "\nerror while decoding inter block: %d x %d (%d)\n",
                       s->mb_x, s->mb_y, i);
                return ret;
            }
        }
    } else {
        if (s->pict_type == AV_PICTURE_TYPE_P)
            ff_dlog(s->avctx, "%d%d ", s->inter_intra_pred, cbp);
        ff_dlog(s->avctx, "I at %d %d %d %06X\n", s->mb_x, s->mb_y,
                ((cbp & 3) ? 1 : 0) + ((cbp & 0x3C) ? 2 : 0),
                show_bits(&s->gb, 24));
        s->ac_pred = get_bits1(&s->gb);
        if (s->inter_intra_pred) {
            s->h263_aic_dir = get_vlc2(&s->gb, ff_inter_intra_vlc.table,
                                       INTER_INTRA_VLC_BITS, 1);
            ff_dlog(s->avctx, "%d%d %d %d/",
                    s->ac_pred, s->h263_aic_dir, s->mb_x, s->mb_y);
        }
        if (s->per_mb_rl_table && cbp) {
            s->rl_table_index        = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }

        s->bdsp.clear_blocks(s->block[0]);
        for (i = 0; i < 6; i++) {
            if ((ret = ff_msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1, NULL)) < 0) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "\nerror while decoding intra block: %d x %d (%d)\n",
                       s->mb_x, s->mb_y, i);
                return ret;
            }
        }
    }

    return 0;
}

static av_cold int wmv2_decode_init(AVCodecContext *avctx)
{
    WMV2DecContext *const w = avctx->priv_data;
    MpegEncContext *const s = &w->s;
    int ret;

    s->private_ctx = &w->common;

    if ((ret = ff_msmpeg4_decode_init(avctx)) < 0)
        return ret;

    ff_wmv2_common_init(s);
    ff_init_scantable(s->idsp.idct_permutation, &w->abt_scantable[0],
                      ff_wmv2_scantableA);
    ff_init_scantable(s->idsp.idct_permutation, &w->abt_scantable[1],
                      ff_wmv2_scantableB);

    return ff_intrax8_common_init(avctx, &w->x8, &w->s.idsp,
                                  w->s.block, w->s.block_last_index,
                                  w->s.mb_width, w->s.mb_height);
}

static av_cold int wmv2_decode_end(AVCodecContext *avctx)
{
    WMV2DecContext *const w = avctx->priv_data;

    ff_intrax8_common_end(&w->x8);
    return ff_h263_decode_end(avctx);
}

const FFCodec ff_wmv2_decoder = {
    .p.name         = "wmv2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Windows Media Video 8"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV2,
    .priv_data_size = sizeof(WMV2DecContext),
    .init           = wmv2_decode_init,
    .close          = wmv2_decode_end,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P,
                                                     AV_PIX_FMT_NONE },
};
