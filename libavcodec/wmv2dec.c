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
#include "mpegvideodec.h"
#include "msmpeg4.h"
#include "msmpeg4_vc1_data.h"
#include "msmpeg4dec.h"
#include "simple_idct.h"
#include "wmv2.h"
#include "wmv2data.h"
#include "wmv2dec.h"

typedef struct WMV2DecContext {
    MSMP4DecContext ms;
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

    DECLARE_ALIGNED(32, int16_t, abt_block2)[6][64];
} WMV2DecContext;

static void wmv2_add_block(WMV2DecContext *w, int16_t blocks1[][64],
                           uint8_t *dst, int stride, int n)
{
    H263DecContext *const h = &w->ms.h;

    if (h->c.block_last_index[n] >= 0) {
        int16_t *block1 = blocks1[n];
        switch (w->abt_type_table[n]) {
        case 0:
            w->common.wdsp.idct_add(dst, stride, block1);
            break;
        case 1:
            ff_simple_idct84_add(dst, stride, block1);
            ff_simple_idct84_add(dst + 4 * stride, stride, w->abt_block2[n]);
            h->c.bdsp.clear_block(w->abt_block2[n]);
            break;
        case 2:
            ff_simple_idct48_add(dst, stride, block1);
            ff_simple_idct48_add(dst + 4, stride, w->abt_block2[n]);
            h->c.bdsp.clear_block(w->abt_block2[n]);
            break;
        default:
            av_log(h->c.avctx, AV_LOG_ERROR, "internal error in WMV2 abt\n");
        }
    }
}

void ff_wmv2_add_mb(MpegEncContext *s, int16_t block1[6][64],
                    uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr)
{
    WMV2DecContext *const w = (WMV2DecContext *) s;

    wmv2_add_block(w, block1, dest_y,                       s->linesize, 0);
    wmv2_add_block(w, block1, dest_y + 8,                   s->linesize, 1);
    wmv2_add_block(w, block1, dest_y + 8 * s->linesize,     s->linesize, 2);
    wmv2_add_block(w, block1, dest_y + 8 + 8 * s->linesize, s->linesize, 3);

    if (s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;

    wmv2_add_block(w, block1, dest_cb, s->uvlinesize, 4);
    wmv2_add_block(w, block1, dest_cr, s->uvlinesize, 5);
}

static int parse_mb_skip(WMV2DecContext *w)
{
    H263DecContext *const h = &w->ms.h;
    int coded_mb_count = 0;
    uint32_t *const mb_type = h->c.cur_pic.mb_type;

    w->skip_type = get_bits(&h->gb, 2);
    switch (w->skip_type) {
    case SKIP_TYPE_NONE:
        for (int mb_y = 0; mb_y < h->c.mb_height; mb_y++)
            for (int mb_x = 0; mb_x < h->c.mb_width; mb_x++)
                mb_type[mb_y * h->c.mb_stride + mb_x] =
                    MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
        break;
    case SKIP_TYPE_MPEG:
        if (get_bits_left(&h->gb) < h->c.mb_height * h->c.mb_width)
            return AVERROR_INVALIDDATA;
        for (int mb_y = 0; mb_y < h->c.mb_height; mb_y++)
            for (int mb_x = 0; mb_x < h->c.mb_width; mb_x++)
                mb_type[mb_y * h->c.mb_stride + mb_x] =
                    (get_bits1(&h->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
        break;
    case SKIP_TYPE_ROW:
        for (int mb_y = 0; mb_y < h->c.mb_height; mb_y++) {
            if (get_bits_left(&h->gb) < 1)
                return AVERROR_INVALIDDATA;
            if (get_bits1(&h->gb)) {
                for (int mb_x = 0; mb_x < h->c.mb_width; mb_x++)
                    mb_type[mb_y * h->c.mb_stride + mb_x] =
                        MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
            } else {
                for (int mb_x = 0; mb_x < h->c.mb_width; mb_x++)
                    mb_type[mb_y * h->c.mb_stride + mb_x] =
                        (get_bits1(&h->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
            }
        }
        break;
    case SKIP_TYPE_COL:
        for (int mb_x = 0; mb_x < h->c.mb_width; mb_x++) {
            if (get_bits_left(&h->gb) < 1)
                return AVERROR_INVALIDDATA;
            if (get_bits1(&h->gb)) {
                for (int mb_y = 0; mb_y < h->c.mb_height; mb_y++)
                    mb_type[mb_y * h->c.mb_stride + mb_x] =
                        MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
            } else {
                for (int mb_y = 0; mb_y < h->c.mb_height; mb_y++)
                    mb_type[mb_y * h->c.mb_stride + mb_x] =
                        (get_bits1(&h->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
            }
        }
        break;
    }

    for (int mb_y = 0; mb_y < h->c.mb_height; mb_y++)
        for (int mb_x = 0; mb_x < h->c.mb_width; mb_x++)
            coded_mb_count += !IS_SKIP(mb_type[mb_y * h->c.mb_stride + mb_x]);

    if (coded_mb_count > get_bits_left(&h->gb))
        return AVERROR_INVALIDDATA;

    return 0;
}

static int decode_ext_header(WMV2DecContext *w)
{
    H263DecContext *const h = &w->ms.h;
    GetBitContext gb;
    int fps;
    int code;

    if (h->c.avctx->extradata_size < 4)
        return AVERROR_INVALIDDATA;

    init_get_bits(&gb, h->c.avctx->extradata, 32);

    fps                 = get_bits(&gb, 5);
    w->ms.bit_rate      = get_bits(&gb, 11) * 1024;
    w->mspel_bit        = get_bits1(&gb);
    h->loop_filter      = get_bits1(&gb);
    w->abt_flag         = get_bits1(&gb);
    w->j_type_bit       = get_bits1(&gb);
    w->top_left_mv_flag = get_bits1(&gb);
    w->per_mb_rl_bit    = get_bits1(&gb);
    code                = get_bits(&gb, 3);

    if (code == 0)
        return AVERROR_INVALIDDATA;

    h->slice_height = h->c.mb_height / code;

    if (h->c.avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(h->c.avctx, AV_LOG_DEBUG,
               "fps:%d, br:%d, qpbit:%d, abt_flag:%d, j_type_bit:%d, "
               "tl_mv_flag:%d, mbrl_bit:%d, code:%d, loop_filter:%d, "
               "slices:%d\n",
               fps, w->ms.bit_rate, w->mspel_bit, w->abt_flag, w->j_type_bit,
               w->top_left_mv_flag, w->per_mb_rl_bit, code, h->loop_filter,
               code);
    return 0;
}

static int wmv2_decode_picture_header(H263DecContext *const h)
{
    int code;

    h->c.pict_type = get_bits1(&h->gb) + 1;
    if (h->c.pict_type == AV_PICTURE_TYPE_I) {
        code = get_bits(&h->gb, 7);
        av_log(h->c.avctx, AV_LOG_DEBUG, "I7:%X/\n", code);
    }
    h->c.chroma_qscale = h->c.qscale = get_bits(&h->gb, 5);
    if (h->c.qscale <= 0)
        return AVERROR_INVALIDDATA;

    if (h->c.pict_type != AV_PICTURE_TYPE_I && show_bits(&h->gb, 1)) {
        GetBitContext gb = h->gb;
        int skip_type = get_bits(&gb, 2);
        int run = skip_type == SKIP_TYPE_COL ? h->c.mb_width : h->c.mb_height;

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

int ff_wmv2_decode_secondary_picture_header(H263DecContext *const h)
{
    WMV2DecContext *const w = (WMV2DecContext *)h;

    if (h->c.pict_type == AV_PICTURE_TYPE_I) {
        /* Is filling with zeroes really the right thing to do? */
        memset(h->c.cur_pic.mb_type, 0,
               sizeof(*h->c.cur_pic.mb_type) * h->c.mb_height * h->c.mb_stride);
        if (w->j_type_bit)
            w->j_type = get_bits1(&h->gb);
        else
            w->j_type = 0; // FIXME check

        if (!w->j_type) {
            if (w->per_mb_rl_bit)
                w->ms.per_mb_rl_table = get_bits1(&h->gb);
            else
                w->ms.per_mb_rl_table = 0;

            if (!w->ms.per_mb_rl_table) {
                w->ms.rl_chroma_table_index = decode012(&h->gb);
                w->ms.rl_table_index        = decode012(&h->gb);
            }

            w->ms.dc_table_index = get_bits1(&h->gb);

            // at minimum one bit per macroblock is required at least in a valid frame,
            // we discard frames much smaller than this. Frames smaller than 1/8 of the
            // smallest "black/skip" frame generally contain not much recoverable content
            // while at the same time they have the highest computational requirements
            // per byte
            if (get_bits_left(&h->gb) * 8LL < (h->c.width+15)/16 * ((h->c.height+15)/16))
                return AVERROR_INVALIDDATA;
        }
        h->c.inter_intra_pred = 0;
        h->c.no_rounding      = 1;
        if (h->c.avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(h->c.avctx, AV_LOG_DEBUG,
                   "qscale:%d rlc:%d rl:%d dc:%d mbrl:%d j_type:%d \n",
                   h->c.qscale, w->ms.rl_chroma_table_index, w->ms.rl_table_index,
                   w->ms.dc_table_index, w->ms.per_mb_rl_table, w->j_type);
        }
    } else {
        int cbp_index;
        int ret;
        w->j_type = 0;

        ret = parse_mb_skip(w);
        if (ret < 0)
            return ret;
        cbp_index = decode012(&h->gb);
        w->cbp_table_index = wmv2_get_cbp_table_index(h->c.qscale, cbp_index);

        if (w->mspel_bit)
            h->c.mspel = get_bits1(&h->gb);
        else
            h->c.mspel = 0; // FIXME check

        if (w->abt_flag) {
            w->per_mb_abt = get_bits1(&h->gb) ^ 1;
            if (!w->per_mb_abt)
                w->abt_type = decode012(&h->gb);
        }

        if (w->per_mb_rl_bit)
            w->ms.per_mb_rl_table = get_bits1(&h->gb);
        else
            w->ms.per_mb_rl_table = 0;

        if (!w->ms.per_mb_rl_table) {
            w->ms.rl_table_index        = decode012(&h->gb);
            w->ms.rl_chroma_table_index = w->ms.rl_table_index;
        }

        if (get_bits_left(&h->gb) < 2)
            return AVERROR_INVALIDDATA;

        w->ms.dc_table_index = get_bits1(&h->gb);
        w->ms.mv_table_index = get_bits1(&h->gb);

        h->c.inter_intra_pred = 0; // (h->c.width * h->c.height < 320 * 240 && w->ms.bit_rate <= II_BITRATE);
        h->c.no_rounding     ^= 1;

        if (h->c.avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(h->c.avctx, AV_LOG_DEBUG,
                   "rl:%d rlc:%d dc:%d mv:%d mbrl:%d qp:%d mspel:%d "
                   "per_mb_abt:%d abt_type:%d cbp:%d ii:%d\n",
                   w->ms.rl_table_index, w->ms.rl_chroma_table_index,
                   w->ms.dc_table_index, w->ms.mv_table_index,
                   w->ms.per_mb_rl_table, h->c.qscale, h->c.mspel,
                   w->per_mb_abt, w->abt_type, w->cbp_table_index,
                   h->c.inter_intra_pred);
        }
    }
    w->ms.esc3_level_length = 0;
    w->ms.esc3_run_length   = 0;

    if (w->j_type) {
        ff_intrax8_decode_picture(&w->x8, h->c.cur_pic.ptr,
                                  &h->gb, &h->c.mb_x, &h->c.mb_y,
                                  2 * h->c.qscale, (h->c.qscale - 1) | 1,
                                  h->loop_filter, h->c.low_delay);

        ff_er_add_slice(&h->c.er, 0, 0,
                        (h->c.mb_x >> 1) - 1, (h->c.mb_y >> 1) - 1,
                        ER_MB_END);
        return 1;
    }

    return 0;
}

static inline void wmv2_decode_motion(WMV2DecContext *w, int *mx_ptr, int *my_ptr)
{
    H263DecContext *const h = &w->ms.h;

    ff_msmpeg4_decode_motion(&w->ms, mx_ptr, my_ptr);

    if ((((*mx_ptr) | (*my_ptr)) & 1) && h->c.mspel)
        w->common.hshift = get_bits1(&h->gb);
    else
        w->common.hshift = 0;
}

static int16_t *wmv2_pred_motion(WMV2DecContext *w, int *px, int *py)
{
    H263DecContext *const h = &w->ms.h;
    int diff, type;

    int wrap    = h->c.b8_stride;
    int xy      = h->c.block_index[0];

    int16_t *mot_val = h->c.cur_pic.motion_val[0][xy];

    const int16_t *A = h->c.cur_pic.motion_val[0][xy     - 1];
    const int16_t *B = h->c.cur_pic.motion_val[0][xy     - wrap];
    const int16_t *C = h->c.cur_pic.motion_val[0][xy + 2 - wrap];

    if (h->c.mb_x && !h->c.first_slice_line && !h->c.mspel && w->top_left_mv_flag)
        diff = FFMAX(FFABS(A[0] - B[0]), FFABS(A[1] - B[1]));
    else
        diff = 0;

    if (diff >= 8)
        type = get_bits1(&h->gb);
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
        if (h->c.first_slice_line) {
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
    H263DecContext *const h = &w->ms.h;
    static const int sub_cbp_table[3] = { 2, 3, 1 };
    int sub_cbp, ret;

    if (!cbp) {
        h->c.block_last_index[n] = -1;
        return 0;
    }

    if (w->per_block_abt)
        w->abt_type = decode012(&h->gb);
    w->abt_type_table[n] = w->abt_type;

    if (w->abt_type) {
        const uint8_t *scantable = w->abt_type == 1 ? ff_wmv2_scantableA : ff_wmv2_scantableB;

        sub_cbp = sub_cbp_table[decode012(&h->gb)];

        if (sub_cbp & 1) {
            ret = ff_msmpeg4_decode_block(&w->ms, block, n, 1, scantable);
            if (ret < 0)
                return ret;
        }

        if (sub_cbp & 2) {
            ret = ff_msmpeg4_decode_block(&w->ms, w->abt_block2[n], n, 1, scantable);
            if (ret < 0)
                return ret;
        }

        h->c.block_last_index[n] = 63;

        return 0;
    } else {
        return ff_msmpeg4_decode_block(&w->ms, block, n, 1,
                                       h->c.inter_scantable.permutated);
    }
}

static int wmv2_decode_mb(H263DecContext *const h)
{
    /* The following is only allowed because this decoder
     * does not use slice threading. */
    WMV2DecContext *const w = (WMV2DecContext *) h;
    MSMP4DecContext *const ms = &w->ms;
    int cbp, code, i, ret;
    uint8_t *coded_val;

    if (w->j_type)
        return 0;

    if (h->c.pict_type == AV_PICTURE_TYPE_P) {
        if (IS_SKIP(h->c.cur_pic.mb_type[h->c.mb_y * h->c.mb_stride + h->c.mb_x])) {
            /* skip mb */
            h->c.mb_intra = 0;
            for (i = 0; i < 6; i++)
                h->c.block_last_index[i] = -1;
            h->c.mv_dir      = MV_DIR_FORWARD;
            h->c.mv_type     = MV_TYPE_16X16;
            h->c.mv[0][0][0] = 0;
            h->c.mv[0][0][1] = 0;
            h->c.mb_skipped  = 1;
            w->common.hshift      = 0;
            return 0;
        }
        if (get_bits_left(&h->gb) <= 0)
            return AVERROR_INVALIDDATA;

        code = get_vlc2(&h->gb, ff_mb_non_intra_vlc[w->cbp_table_index],
                        MB_NON_INTRA_VLC_BITS, 3);
        h->c.mb_intra = (~code & 0x40) >> 6;

        cbp = code & 0x3f;
    } else {
        h->c.mb_intra = 1;
        if (get_bits_left(&h->gb) <= 0)
            return AVERROR_INVALIDDATA;
        code = get_vlc2(&h->gb, ff_msmp4_mb_i_vlc,
                        MSMP4_MB_INTRA_VLC_BITS, 2);
        /* predict coded block pattern */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            int val = ((code >> (5 - i)) & 1);
            if (i < 4) {
                int pred   = ff_msmpeg4_coded_block_pred(&h->c, i, &coded_val);
                val        = val ^ pred;
                *coded_val = val;
            }
            cbp |= val << (5 - i);
        }
    }

    if (!h->c.mb_intra) {
        int mx, my;
        wmv2_pred_motion(w, &mx, &my);

        if (cbp) {
            h->c.bdsp.clear_blocks(h->block[0]);
            if (ms->per_mb_rl_table) {
                ms->rl_table_index        = decode012(&h->gb);
                ms->rl_chroma_table_index = ms->rl_table_index;
            }

            if (w->abt_flag && w->per_mb_abt) {
                w->per_block_abt = get_bits1(&h->gb);
                if (!w->per_block_abt)
                    w->abt_type = decode012(&h->gb);
            } else
                w->per_block_abt = 0;
        }

        wmv2_decode_motion(w, &mx, &my);

        h->c.mv_dir      = MV_DIR_FORWARD;
        h->c.mv_type     = MV_TYPE_16X16;
        h->c.mv[0][0][0] = mx;
        h->c.mv[0][0][1] = my;

        for (i = 0; i < 6; i++) {
            if ((ret = wmv2_decode_inter_block(w, h->block[i], i, (cbp >> (5 - i)) & 1)) < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "\nerror while decoding inter block: %d x %d (%d)\n",
                       h->c.mb_x, h->c.mb_y, i);
                return ret;
            }
        }
    } else {
        if (h->c.pict_type == AV_PICTURE_TYPE_P)
            ff_dlog(h->c.avctx, "%d%d ", h->c.inter_intra_pred, cbp);
        ff_dlog(h->c.avctx, "I at %d %d %d %06X\n", h->c.mb_x, h->c.mb_y,
                ((cbp & 3) ? 1 : 0) + ((cbp & 0x3C) ? 2 : 0),
                show_bits(&h->gb, 24));
        h->c.ac_pred = get_bits1(&h->gb);
        if (h->c.inter_intra_pred) {
            h->c.h263_aic_dir = get_vlc2(&h->gb, ff_inter_intra_vlc,
                                       INTER_INTRA_VLC_BITS, 1);
            ff_dlog(h->c.avctx, "%d%d %d %d/",
                    h->c.ac_pred, h->c.h263_aic_dir, h->c.mb_x, h->c.mb_y);
        }
        if (ms->per_mb_rl_table && cbp) {
            ms->rl_table_index        = decode012(&h->gb);
            ms->rl_chroma_table_index = ms->rl_table_index;
        }

        h->c.bdsp.clear_blocks(h->block[0]);
        for (i = 0; i < 6; i++) {
            ret = ff_msmpeg4_decode_block(ms, h->block[i], i, (cbp >> (5 - i)) & 1, NULL);
            if (ret < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "\nerror while decoding intra block: %d x %d (%d)\n",
                       h->c.mb_x, h->c.mb_y, i);
                return ret;
            }
        }
    }

    return 0;
}

static av_cold int wmv2_decode_init(AVCodecContext *avctx)
{
    WMV2DecContext *const w = avctx->priv_data;
    H263DecContext *const h = &w->ms.h;
    MpegEncContext *const s = &h->c;
    int ret;

    s->private_ctx = &w->common;

    if ((ret = ff_msmpeg4_decode_init(avctx)) < 0)
        return ret;

    h->decode_header = wmv2_decode_picture_header;
    h->decode_mb = wmv2_decode_mb;

    ff_wmv2_common_init(s);

    decode_ext_header(w);

    return ff_intrax8_common_init(avctx, &w->x8, h->block[0],
                                  s->mb_width, s->mb_height);
}

static av_cold int wmv2_decode_end(AVCodecContext *avctx)
{
    WMV2DecContext *const w = avctx->priv_data;

    ff_intrax8_common_end(&w->x8);
    return ff_mpv_decode_close(avctx);
}

const FFCodec ff_wmv2_decoder = {
    .p.name         = "wmv2",
    CODEC_LONG_NAME("Windows Media Video 8"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV2,
    .priv_data_size = sizeof(WMV2DecContext),
    .init           = wmv2_decode_init,
    .close          = wmv2_decode_end,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
