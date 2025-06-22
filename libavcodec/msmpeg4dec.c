/*
 * MSMPEG4 backend for encoder and decoder
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2013 Michael Niedermayer <michaelni@gmx.at>
 *
 * msmpeg4v1 & v2 stuff by Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/thread.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodec.h"
#include "msmpeg4.h"
#include "msmpeg4dec.h"
#include "libavutil/imgutils.h"
#include "h263.h"
#include "h263data.h"
#include "h263dec.h"
#include "mpeg4videodec.h"
#include "msmpeg4data.h"
#include "msmpeg4_vc1_data.h"

#define V2_INTRA_CBPC_VLC_BITS 3
#define V2_MB_TYPE_VLC_BITS 7
#define MV_VLC_BITS 9
#define TEX_VLC_BITS 9

#define DEFAULT_INTER_INDEX 3

static const VLCElem *mv_tables[2];

static inline int msmpeg4v1_pred_dc(MpegEncContext * s, int n,
                                    int32_t **dc_val_ptr)
{
    int i;

    if (n < 4) {
        i= 0;
    } else {
        i= n-3;
    }

    *dc_val_ptr= &s->last_dc[i];
    return s->last_dc[i];
}

/****************************************/
/* decoding stuff */

const VLCElem *ff_mb_non_intra_vlc[4];
static VLCElem v2_dc_lum_vlc[1472];
static VLCElem v2_dc_chroma_vlc[1506];
static VLCElem v2_intra_cbpc_vlc[8];
static VLCElem v2_mb_type_vlc[128];
VLCElem ff_inter_intra_vlc[8];

/* This is identical to H.263 except that its range is multiplied by 2. */
static int msmpeg4v2_decode_motion(H263DecContext *const h, int pred, int f_code)
{
    int code, val, sign, shift;

    code = get_vlc2(&h->gb, ff_h263_mv_vlc, H263_MV_VLC_BITS, 2);
    ff_dlog(h->c.avctx, "MV code %d at %d %d pred: %d\n",
            code, h->c.mb_x,h->c.mb_y, pred);
    if (code < 0)
        return 0xffff;

    if (code == 0)
        return pred;
    sign = get_bits1(&h->gb);
    shift = f_code - 1;
    val = code;
    if (shift) {
        val = (val - 1) << shift;
        val |= get_bits(&h->gb, shift);
        val++;
    }
    if (sign)
        val = -val;

    val += pred;
    if (val <= -64)
        val += 64;
    else if (val >= 64)
        val -= 64;

    return val;
}

static int msmpeg4v12_decode_mb(H263DecContext *const h)
{
    MSMP4DecContext *const ms = mpv_to_msmpeg4(h);
    int cbp, code, i;
    uint32_t * const mb_type_ptr = &h->c.cur_pic.mb_type[h->c.mb_x + h->c.mb_y*h->c.mb_stride];

    if (h->c.pict_type == AV_PICTURE_TYPE_P) {
        if (ms->use_skip_mb_code) {
            if (get_bits1(&h->gb)) {
                /* skip mb */
                h->c.mb_intra = 0;
                for(i=0;i<6;i++)
                    h->c.block_last_index[i] = -1;
                h->c.mv_dir = MV_DIR_FORWARD;
                h->c.mv_type = MV_TYPE_16X16;
                h->c.mv[0][0][0] = 0;
                h->c.mv[0][0][1] = 0;
                h->c.mb_skipped = 1;
                *mb_type_ptr = MB_TYPE_SKIP | MB_TYPE_FORWARD_MV | MB_TYPE_16x16;
                return 0;
            }
        }

        if (h->c.msmpeg4_version == MSMP4_V2)
            code = get_vlc2(&h->gb, v2_mb_type_vlc, V2_MB_TYPE_VLC_BITS, 1);
        else
            code = get_vlc2(&h->gb, ff_h263_inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 2);
        if(code<0 || code>7){
            av_log(h->c.avctx, AV_LOG_ERROR, "cbpc %d invalid at %d %d\n",
                   code, h->c.mb_x, h->c.mb_y);
            return -1;
        }

        h->c.mb_intra = code >>2;

        cbp = code & 0x3;
    } else {
        h->c.mb_intra = 1;
        if (h->c.msmpeg4_version == MSMP4_V2)
            cbp = get_vlc2(&h->gb, v2_intra_cbpc_vlc, V2_INTRA_CBPC_VLC_BITS, 1);
        else
            cbp = get_vlc2(&h->gb, ff_h263_intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 2);
        if(cbp<0 || cbp>3){
            av_log(h->c.avctx, AV_LOG_ERROR, "cbpc %d invalid at %d %d\n",
                   cbp, h->c.mb_x, h->c.mb_y);
            return -1;
        }
    }

    if (!h->c.mb_intra) {
        int mx, my, cbpy;

        cbpy = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);
        if(cbpy<0){
            av_log(h->c.avctx, AV_LOG_ERROR, "cbpy %d invalid at %d %d\n",
                   cbp, h->c.mb_x, h->c.mb_y);
            return -1;
        }

        cbp|= cbpy<<2;
        if (h->c.msmpeg4_version == MSMP4_V1 || (cbp&3) != 3)
            cbp ^= 0x3C;

        ff_h263_pred_motion(&h->c, 0, 0, &mx, &my);
        mx = msmpeg4v2_decode_motion(h, mx, 1);
        my = msmpeg4v2_decode_motion(h, my, 1);

        h->c.mv_dir  = MV_DIR_FORWARD;
        h->c.mv_type = MV_TYPE_16X16;
        h->c.mv[0][0][0] = mx;
        h->c.mv[0][0][1] = my;
        *mb_type_ptr = MB_TYPE_FORWARD_MV | MB_TYPE_16x16;
    } else {
        int v;
        if (h->c.msmpeg4_version == MSMP4_V2) {
            h->c.ac_pred = get_bits1(&h->gb);
            v = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);
            if (v < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR, "cbpy vlc invalid\n");
                return -1;
            }
            cbp|= v<<2;
        } else{
            h->c.ac_pred = 0;
            v = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);
            if (v < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR, "cbpy vlc invalid\n");
                return -1;
            }
            cbp|= v<<2;
            if (h->c.pict_type==AV_PICTURE_TYPE_P) cbp^=0x3C;
        }
        *mb_type_ptr = MB_TYPE_INTRA;
    }

    h->c.bdsp.clear_blocks(h->block[0]);
    for (i = 0; i < 6; i++) {
        if (ff_msmpeg4_decode_block(ms, h->block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
        {
             av_log(h->c.avctx, AV_LOG_ERROR, "\nerror while decoding block: %d x %d (%d)\n",
                    h->c.mb_x, h->c.mb_y, i);
             return -1;
        }
    }
    return 0;
}

static int msmpeg4v34_decode_mb(H263DecContext *const h)
{
    MSMP4DecContext *const ms = mpv_to_msmpeg4(h);
    int cbp, code, i;
    uint8_t *coded_val;
    uint32_t * const mb_type_ptr = &h->c.cur_pic.mb_type[h->c.mb_x + h->c.mb_y*h->c.mb_stride];

    if (get_bits_left(&h->gb) <= 0)
        return AVERROR_INVALIDDATA;

    if (h->c.pict_type == AV_PICTURE_TYPE_P) {
        if (ms->use_skip_mb_code) {
            if (get_bits1(&h->gb)) {
                /* skip mb */
                h->c.mb_intra = 0;
                for(i=0;i<6;i++)
                    h->c.block_last_index[i] = -1;
                h->c.mv_dir = MV_DIR_FORWARD;
                h->c.mv_type = MV_TYPE_16X16;
                h->c.mv[0][0][0] = 0;
                h->c.mv[0][0][1] = 0;
                h->c.mb_skipped  = 1;
                *mb_type_ptr = MB_TYPE_SKIP | MB_TYPE_FORWARD_MV | MB_TYPE_16x16;

                return 0;
            }
        }

        code = get_vlc2(&h->gb, ff_mb_non_intra_vlc[DEFAULT_INTER_INDEX], MB_NON_INTRA_VLC_BITS, 3);
        //h->c.mb_intra = (code & 0x40) ? 0 : 1;
        h->c.mb_intra = (~code & 0x40) >> 6;

        cbp = code & 0x3f;
    } else {
        h->c.mb_intra = 1;
        code = get_vlc2(&h->gb, ff_msmp4_mb_i_vlc, MSMP4_MB_INTRA_VLC_BITS, 2);
        /* predict coded block pattern */
        cbp = 0;
        for(i=0;i<6;i++) {
            int val = ((code >> (5 - i)) & 1);
            if (i < 4) {
                int pred = ff_msmpeg4_coded_block_pred(&h->c, i, &coded_val);
                val = val ^ pred;
                *coded_val = val;
            }
            cbp |= val << (5 - i);
        }
    }

    if (!h->c.mb_intra) {
        int mx, my;
        if (ms->per_mb_rl_table && cbp) {
            ms->rl_table_index = decode012(&h->gb);
            ms->rl_chroma_table_index = ms->rl_table_index;
        }
        ff_h263_pred_motion(&h->c, 0, 0, &mx, &my);
        ff_msmpeg4_decode_motion(ms, &mx, &my);
        h->c.mv_dir  = MV_DIR_FORWARD;
        h->c.mv_type = MV_TYPE_16X16;
        h->c.mv[0][0][0] = mx;
        h->c.mv[0][0][1] = my;
        *mb_type_ptr = MB_TYPE_FORWARD_MV | MB_TYPE_16x16;
    } else {
        ff_dlog(h->c.avctx, "I at %d %d %d %06X\n", h->c.mb_x, h->c.mb_y,
                ((cbp & 3) ? 1 : 0) +((cbp & 0x3C)? 2 : 0),
                show_bits(&h->gb, 24));
        h->c.ac_pred = get_bits1(&h->gb);
        *mb_type_ptr = MB_TYPE_INTRA;
        if (h->c.inter_intra_pred) {
            h->c.h263_aic_dir = get_vlc2(&h->gb, ff_inter_intra_vlc, INTER_INTRA_VLC_BITS, 1);
            ff_dlog(h->c.avctx, "%d%d %d %d/",
                    h->c.ac_pred, h->c.h263_aic_dir, h->c.mb_x, h->c.mb_y);
        }
        if (ms->per_mb_rl_table && cbp) {
            ms->rl_table_index = decode012(&h->gb);
            ms->rl_chroma_table_index = ms->rl_table_index;
        }
    }

    h->c.bdsp.clear_blocks(h->block[0]);
    for (i = 0; i < 6; i++) {
        if (ff_msmpeg4_decode_block(ms, h->block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
        {
            av_log(h->c.avctx, AV_LOG_ERROR, "\nerror while decoding block: %d x %d (%d)\n",
                   h->c.mb_x, h->c.mb_y, i);
            return -1;
        }
    }

    return 0;
}

/* init all vlc decoding tables */
static av_cold void msmpeg4_decode_init_static(void)
{
    static VLCElem vlc_buf[3714 + 2694 + 1636 + 2648 + 1532 + 2488];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);

    INIT_FIRST_VLC_RL(ff_rl_table[0], 642);
    INIT_FIRST_VLC_RL(ff_rl_table[1], 1104);
    INIT_FIRST_VLC_RL(ff_rl_table[2], 554);
    VLC_INIT_RL(ff_rl_table[3], 940);
    VLC_INIT_RL(ff_rl_table[4], 962);
    /* ff_rl_table[5] coincides with ff_h263_rl_inter which has just been
     * initialized in ff_h263_decode_init() earlier. So just copy the VLCs. */
    av_assert1(ff_h263_rl_inter.rl_vlc[0]);
    memcpy(ff_rl_table[5].rl_vlc, ff_h263_rl_inter.rl_vlc, sizeof(ff_rl_table[5].rl_vlc));

    VLC_INIT_STATIC_TABLE(v2_dc_lum_vlc, MSMP4_DC_VLC_BITS, 512,
                          &ff_v2_dc_lum_table[0][1], 8, 4,
                          &ff_v2_dc_lum_table[0][0], 8, 4, 0);
    VLC_INIT_STATIC_TABLE(v2_dc_chroma_vlc, MSMP4_DC_VLC_BITS, 512,
                          &ff_v2_dc_chroma_table[0][1], 8, 4,
                          &ff_v2_dc_chroma_table[0][0], 8, 4, 0);

    VLC_INIT_STATIC_TABLE(v2_intra_cbpc_vlc, V2_INTRA_CBPC_VLC_BITS, 4,
                          &ff_v2_intra_cbpc[0][1], 2, 1,
                          &ff_v2_intra_cbpc[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE(v2_mb_type_vlc, V2_MB_TYPE_VLC_BITS, 8,
                          &ff_v2_mb_type[0][1], 2, 1,
                          &ff_v2_mb_type[0][0], 2, 1, 0);

    mv_tables[0] = ff_vlc_init_tables_from_lengths(&state, MV_VLC_BITS,
                                                   MSMPEG4_MV_TABLES_NB_ELEMS,
                                                   ff_msmp4_mv_table0_lens, 1,
                                                   ff_msmp4_mv_table0, 2, 2,
                                                   0, 0);
    mv_tables[1] = ff_vlc_init_tables_from_lengths(&state, MV_VLC_BITS,
                                                   MSMPEG4_MV_TABLES_NB_ELEMS,
                                                   ff_msmp4_mv_table1_lens, 1,
                                                   ff_msmp4_mv_table1, 2, 2,
                                                   0, 0);

    for (unsigned i = 0; i < 4; i++) {
        ff_mb_non_intra_vlc[i] =
            ff_vlc_init_tables_sparse(&state, MB_NON_INTRA_VLC_BITS, 128,
                                      &ff_wmv2_inter_table[i][0][1], 8, 4,
                                      &ff_wmv2_inter_table[i][0][0], 8, 4,
                                      NULL, 0, 0, 0);
    }

    VLC_INIT_STATIC_TABLE(ff_inter_intra_vlc, INTER_INTRA_VLC_BITS, 4,
                          &ff_table_inter_intra[0][1], 2, 1,
                          &ff_table_inter_intra[0][0], 2, 1, 0);
    ff_msmp4_vc1_vlcs_init_once();
}

static int msmpeg4_decode_picture_header(H263DecContext *const h)
{
    MSMP4DecContext *const ms = mpv_to_msmpeg4(h);
    int code;

    // at minimum one bit per macroblock is required at least in a valid frame,
    // we discard frames much smaller than this. Frames smaller than 1/8 of the
    // smallest "black/skip" frame generally contain not much recoverable content
    // while at the same time they have the highest computational requirements
    // per byte
    if (get_bits_left(&h->gb) * 8LL < (h->c.width+15)/16 * ((h->c.height+15)/16))
        return AVERROR_INVALIDDATA;

    if (h->c.msmpeg4_version == MSMP4_V1) {
        int start_code = get_bits_long(&h->gb, 32);
        if(start_code!=0x00000100){
            av_log(h->c.avctx, AV_LOG_ERROR, "invalid startcode\n");
            return -1;
        }

        skip_bits(&h->gb, 5); // frame number */
    }

    h->c.pict_type = get_bits(&h->gb, 2) + 1;
    if (h->c.pict_type != AV_PICTURE_TYPE_I &&
        h->c.pict_type != AV_PICTURE_TYPE_P){
        av_log(h->c.avctx, AV_LOG_ERROR, "invalid picture type\n");
        return -1;
    }
    h->c.chroma_qscale = h->c.qscale = get_bits(&h->gb, 5);
    if (h->c.qscale == 0) {
        av_log(h->c.avctx, AV_LOG_ERROR, "invalid qscale\n");
        return -1;
    }

    if (h->c.pict_type == AV_PICTURE_TYPE_I) {
        code = get_bits(&h->gb, 5);
        if (h->c.msmpeg4_version == MSMP4_V1) {
            if(code==0 || code>h->c.mb_height) {
                av_log(h->c.avctx, AV_LOG_ERROR, "invalid slice height %d\n", code);
                return -1;
            }

            h->slice_height = code;
        }else{
            /* 0x17: one slice, 0x18: two slices, ... */
            if (code < 0x17){
                av_log(h->c.avctx, AV_LOG_ERROR, "error, slice code was %X\n", code);
                return -1;
            }

            h->slice_height = h->c.mb_height / (code - 0x16);
        }

        switch (h->c.msmpeg4_version) {
        case MSMP4_V1:
        case MSMP4_V2:
            ms->rl_chroma_table_index = 2;
            ms->rl_table_index = 2;

            ms->dc_table_index = 0; //not used
            break;
        case MSMP4_V3:
            ms->rl_chroma_table_index = decode012(&h->gb);
            ms->rl_table_index        = decode012(&h->gb);

            ms->dc_table_index = get_bits1(&h->gb);
            break;
        case MSMP4_WMV1:
            ff_msmpeg4_decode_ext_header(h, (2+5+5+17+7)/8);

            if (ms->bit_rate > MBAC_BITRATE)
                ms->per_mb_rl_table = get_bits1(&h->gb);
            else
                ms->per_mb_rl_table = 0;

            if (!ms->per_mb_rl_table) {
                ms->rl_chroma_table_index = decode012(&h->gb);
                ms->rl_table_index        = decode012(&h->gb);
            }

            ms->dc_table_index = get_bits1(&h->gb);
            h->c.inter_intra_pred= 0;
            break;
        default:
            av_unreachable("msmpeg4_decode_picture_header() only used by MSMP4V1-3, WMV1");
        }
        h->c.no_rounding = 1;
        if (h->c.avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(h->c.avctx, AV_LOG_DEBUG, "qscale:%d rlc:%d rl:%d dc:%d mbrl:%d slice:%d   \n",
                h->c.qscale,
                ms->rl_chroma_table_index,
                ms->rl_table_index,
                ms->dc_table_index,
                ms->per_mb_rl_table,
                h->slice_height);
    } else {
        switch (h->c.msmpeg4_version) {
        case MSMP4_V1:
        case MSMP4_V2:
            if (h->c.msmpeg4_version == MSMP4_V1)
                ms->use_skip_mb_code = 1;
            else
                ms->use_skip_mb_code = get_bits1(&h->gb);
            ms->rl_table_index = 2;
            ms->rl_chroma_table_index = ms->rl_table_index;
            ms->dc_table_index = 0; //not used
            ms->mv_table_index = 0;
            break;
        case MSMP4_V3:
            ms->use_skip_mb_code = get_bits1(&h->gb);
            ms->rl_table_index   = decode012(&h->gb);
            ms->rl_chroma_table_index = ms->rl_table_index;

            ms->dc_table_index = get_bits1(&h->gb);

            ms->mv_table_index = get_bits1(&h->gb);
            break;
        case MSMP4_WMV1:
            ms->use_skip_mb_code = get_bits1(&h->gb);

            if (ms->bit_rate > MBAC_BITRATE)
                ms->per_mb_rl_table = get_bits1(&h->gb);
            else
                ms->per_mb_rl_table = 0;

            if (!ms->per_mb_rl_table) {
                ms->rl_table_index = decode012(&h->gb);
                ms->rl_chroma_table_index = ms->rl_table_index;
            }

            ms->dc_table_index = get_bits1(&h->gb);

            ms->mv_table_index = get_bits1(&h->gb);
            h->c.inter_intra_pred = h->c.width*h->c.height < 320*240 &&
                                  ms->bit_rate <= II_BITRATE;
            break;
        default:
            av_unreachable("msmpeg4_decode_picture_header() only used by MSMP4V1-3, WMV1");
        }

        if (h->c.avctx->debug&FF_DEBUG_PICT_INFO)
            av_log(h->c.avctx, AV_LOG_DEBUG, "skip:%d rl:%d rlc:%d dc:%d mv:%d mbrl:%d qp:%d   \n",
                ms->use_skip_mb_code,
                ms->rl_table_index,
                ms->rl_chroma_table_index,
                ms->dc_table_index,
                ms->mv_table_index,
                ms->per_mb_rl_table,
                h->c.qscale);

        if (ms->flipflop_rounding) {
            h->c.no_rounding ^= 1;
        }else{
            h->c.no_rounding = 0;
        }
    }
    ff_dlog(h->c.avctx, "%d %d %d %d %d\n", h->c.pict_type, ms->bit_rate,
            h->c.inter_intra_pred, h->c.width, h->c.height);

    ms->esc3_level_length = 0;
    ms->esc3_run_length   = 0;

    return 0;
}

int ff_msmpeg4_decode_ext_header(H263DecContext *const h, int buf_size)
{
    MSMP4DecContext *const ms = mpv_to_msmpeg4(h);
    int left = buf_size*8 - get_bits_count(&h->gb);
    int length = h->c.msmpeg4_version >= MSMP4_V3 ? 17 : 16;
    /* the alt_bitstream reader could read over the end so we need to check it */
    if(left>=length && left<length+8)
    {
        skip_bits(&h->gb, 5); /* fps */
        ms->bit_rate = get_bits(&h->gb, 11) * 1024;
        if (h->c.msmpeg4_version >= MSMP4_V3)
            ms->flipflop_rounding = get_bits1(&h->gb);
        else
            ms->flipflop_rounding = 0;
    }
    else if(left<length+8)
    {
        ms->flipflop_rounding = 0;
        if (h->c.msmpeg4_version != MSMP4_V2)
            av_log(h->c.avctx, AV_LOG_ERROR, "ext header missing, %d left\n", left);
    }
    else
    {
        av_log(h->c.avctx, AV_LOG_ERROR, "I-frame too long, ignoring ext header\n");
    }

    return 0;
}

static int msmpeg4_decode_dc(MSMP4DecContext *const ms, int n, int *dir_ptr)
{
    H263DecContext *const h = &ms->h;
    int level, pred;

    if (h->c.msmpeg4_version <= MSMP4_V2) {
        if (n < 4) {
            level = get_vlc2(&h->gb, v2_dc_lum_vlc, MSMP4_DC_VLC_BITS, 3);
        } else {
            level = get_vlc2(&h->gb, v2_dc_chroma_vlc, MSMP4_DC_VLC_BITS, 3);
        }
        if (level < 0) {
            av_log(h->c.avctx, AV_LOG_ERROR, "illegal dc vlc\n");
            *dir_ptr = 0;
            return -1;
        }
        level-=256;
    } else {
        level = get_vlc2(&h->gb, ff_msmp4_dc_vlc[ms->dc_table_index][n >= 4],
                         MSMP4_DC_VLC_BITS, 3);

        if (level == DC_MAX) {
            level = get_bits(&h->gb, 8);
            if (get_bits1(&h->gb))
                level = -level;
        } else if (level != 0) {
            if (get_bits1(&h->gb))
                level = -level;
        }
    }

    if (h->c.msmpeg4_version == MSMP4_V1) {
        int32_t *dc_val;
        pred = msmpeg4v1_pred_dc(&h->c, n, &dc_val);
        level += pred;

        /* update predictor */
        *dc_val= level;
    }else{
        int16_t *dc_val;
        pred   = ff_msmpeg4_pred_dc(&h->c, n, &dc_val, dir_ptr);
        level += pred;

        /* update predictor */
        if (n < 4) {
            *dc_val = level * h->c.y_dc_scale;
        } else {
            *dc_val = level * h->c.c_dc_scale;
        }
    }

    return level;
}

int ff_msmpeg4_decode_block(MSMP4DecContext *const ms, int16_t * block,
                              int n, int coded, const uint8_t *scan_table)
{
    H263DecContext *const h = &ms->h;
    int level, i, last, run, run_diff;
    int dc_pred_dir = -1; //unused but its passed around, so it needs to be initialized
    const RLTable *rl;
    const RL_VLC_ELEM *rl_vlc;
    int qmul, qadd;

    if (h->c.mb_intra) {
        qmul=1;
        qadd=0;

        /* DC coef */
        level = msmpeg4_decode_dc(ms, n, &dc_pred_dir);

        if (level < 0){
            av_log(h->c.avctx, AV_LOG_ERROR, "dc overflow- block: %d qscale: %d//\n", n, h->c.qscale);
            if (h->c.inter_intra_pred)
                level = 0;
        }
        if (n < 4) {
            rl = &ff_rl_table[ms->rl_table_index];
            if (level > 256 * h->c.y_dc_scale) {
                av_log(h->c.avctx, AV_LOG_ERROR, "dc overflow+ L qscale: %d//\n", h->c.qscale);
                if (!h->c.inter_intra_pred)
                    return -1;
            }
        } else {
            rl = &ff_rl_table[3 + ms->rl_chroma_table_index];
            if (level > 256 * h->c.c_dc_scale) {
                av_log(h->c.avctx, AV_LOG_ERROR, "dc overflow+ C qscale: %d//\n", h->c.qscale);
                if (!h->c.inter_intra_pred)
                    return -1;
            }
        }
        block[0] = level;

        run_diff = h->c.msmpeg4_version >= MSMP4_WMV1;
        i = 0;
        if (!coded) {
            goto not_coded;
        }
        if (h->c.ac_pred) {
            if (dc_pred_dir == 0)
                scan_table = h->c.permutated_intra_v_scantable; /* left */
            else
                scan_table = h->c.permutated_intra_h_scantable; /* top */
        } else {
            scan_table = h->c.intra_scantable.permutated;
        }
        rl_vlc= rl->rl_vlc[0];
    } else {
        qmul = h->c.qscale << 1;
        qadd = (h->c.qscale - 1) | 1;
        i = -1;
        rl = &ff_rl_table[3 + ms->rl_table_index];

        if (h->c.msmpeg4_version == MSMP4_V2)
            run_diff = 0;
        else
            run_diff = 1;

        if (!coded) {
            h->c.block_last_index[n] = i;
            return 0;
        }
        if(!scan_table)
            scan_table = h->c.inter_scantable.permutated;
        rl_vlc= rl->rl_vlc[h->c.qscale];
    }
  {
    OPEN_READER(re, &h->gb);
    for(;;) {
        UPDATE_CACHE(re, &h->gb);
        GET_RL_VLC(level, run, re, &h->gb, rl_vlc, TEX_VLC_BITS, 2, 0);
        if (level==0) {
            int cache;
            cache= GET_CACHE(re, &h->gb);
            /* escape */
            if (h->c.msmpeg4_version == MSMP4_V1 || (cache&0x80000000)==0) {
                if (h->c.msmpeg4_version == MSMP4_V1 || (cache&0x40000000)==0) {
                    /* third escape */
                    if (h->c.msmpeg4_version != MSMP4_V1)
                        LAST_SKIP_BITS(re, &h->gb, 2);
                    UPDATE_CACHE(re, &h->gb);
                    if (h->c.msmpeg4_version <= MSMP4_V3) {
                        last  = SHOW_UBITS(re, &h->gb, 1); SKIP_CACHE(re, &h->gb, 1);
                        run   = SHOW_UBITS(re, &h->gb, 6); SKIP_CACHE(re, &h->gb, 6);
                        level = SHOW_SBITS(re, &h->gb, 8);
                        SKIP_COUNTER(re, &h->gb, 1 + 6 + 8);
                    }else{
                        int sign;
                        last = SHOW_UBITS(re, &h->gb, 1); SKIP_BITS(re, &h->gb, 1);
                        if (!ms->esc3_level_length) {
                            int ll;
                            ff_dlog(h->c.avctx, "ESC-3 %X at %d %d\n",
                                    show_bits(&h->gb, 24), h->c.mb_x, h->c.mb_y);
                            if (h->c.qscale < 8) {
                                ll = SHOW_UBITS(re, &h->gb, 3); SKIP_BITS(re, &h->gb, 3);
                                if(ll==0){
                                    ll = 8+SHOW_UBITS(re, &h->gb, 1); SKIP_BITS(re, &h->gb, 1);
                                }
                            }else{
                                ll=2;
                                while (ll < 8 && SHOW_UBITS(re, &h->gb, 1) == 0) {
                                    ll++;
                                    SKIP_BITS(re, &h->gb, 1);
                                }
                                if (ll<8) SKIP_BITS(re, &h->gb, 1);
                            }

                            ms->esc3_level_length = ll;
                            ms->esc3_run_length   = SHOW_UBITS(re, &h->gb, 2) + 3; SKIP_BITS(re, &h->gb, 2);
                            UPDATE_CACHE(re, &h->gb);
                        }
                        run   = SHOW_UBITS(re, &h->gb, ms->esc3_run_length);
                        SKIP_BITS(re, &h->gb, ms->esc3_run_length);

                        sign=  SHOW_UBITS(re, &h->gb, 1);
                        SKIP_BITS(re, &h->gb, 1);

                        level = SHOW_UBITS(re, &h->gb, ms->esc3_level_length);
                        SKIP_BITS(re, &h->gb, ms->esc3_level_length);
                        if(sign) level= -level;
                    }

                    //level = level * qmul + (level>0) * qadd - (level<=0) * qadd ;
                    if (level>0) level= level * qmul + qadd;
                    else         level= level * qmul - qadd;
                    i+= run + 1;
                    if(last) i+=192;
                } else {
                    /* second escape */
                    SKIP_BITS(re, &h->gb, 2);
                    GET_RL_VLC(level, run, re, &h->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                    i+= run + rl->max_run[run>>7][level/qmul] + run_diff; //FIXME opt indexing
                    level = (level ^ SHOW_SBITS(re, &h->gb, 1)) - SHOW_SBITS(re, &h->gb, 1);
                    LAST_SKIP_BITS(re, &h->gb, 1);
                }
            } else {
                /* first escape */
                SKIP_BITS(re, &h->gb, 1);
                GET_RL_VLC(level, run, re, &h->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                i+= run;
                level = level + rl->max_level[run>>7][(run-1)&63] * qmul;//FIXME opt indexing
                level = (level ^ SHOW_SBITS(re, &h->gb, 1)) - SHOW_SBITS(re, &h->gb, 1);
                LAST_SKIP_BITS(re, &h->gb, 1);
            }
        } else {
            i+= run;
            level = (level ^ SHOW_SBITS(re, &h->gb, 1)) - SHOW_SBITS(re, &h->gb, 1);
            LAST_SKIP_BITS(re, &h->gb, 1);
        }
        if (i > 62){
            i-= 192;
            if(i&(~63)){
                const int left = get_bits_left(&h->gb);
                if (((i + 192 == 64 && level / qmul == -1) ||
                     !(h->c.avctx->err_recognition & (AV_EF_BITSTREAM|AV_EF_COMPLIANT))) &&
                    left >= 0) {
                    av_log(h->c.avctx, AV_LOG_ERROR, "ignoring overflow at %d %d\n",
                           h->c.mb_x, h->c.mb_y);
                    i = 63;
                    break;
                }else{
                    av_log(h->c.avctx, AV_LOG_ERROR, "ac-tex damaged at %d %d\n",
                           h->c.mb_x, h->c.mb_y);
                    return -1;
                }
            }

            block[scan_table[i]] = level;
            break;
        }

        block[scan_table[i]] = level;
    }
    CLOSE_READER(re, &h->gb);
  }
    if (h->c.mb_intra) {
 not_coded:
        ff_mpeg4_pred_ac(h, block, n, dc_pred_dir);
    }
    h->c.block_last_index[n] = i;

    return 0;
}

void ff_msmpeg4_decode_motion(MSMP4DecContext *const ms, int *mx_ptr, int *my_ptr)
{
    const VLCElem *const mv_vlc = mv_tables[ms->mv_table_index];
    H263DecContext *const h = &ms->h;
    int sym, mx, my;

    sym = get_vlc2(&h->gb, mv_vlc, MV_VLC_BITS, 2);
    if (sym) {
        mx = sym >> 8;
        my = sym & 0xFF;
    } else {
        /* Escape */
        mx = get_bits(&h->gb, 6);
        my = get_bits(&h->gb, 6);
    }

    mx += *mx_ptr - 32;
    my += *my_ptr - 32;
    /* WARNING : they do not do exactly modulo encoding */
    if (mx <= -64)
        mx += 64;
    else if (mx >= 64)
        mx -= 64;

    if (my <= -64)
        my += 64;
    else if (my >= 64)
        my -= 64;
    *mx_ptr = mx;
    *my_ptr = my;
}

av_cold int ff_msmpeg4_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    H263DecContext *const h = avctx->priv_data;
    int ret;

    ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);
    if (ret < 0)
        return ret;

    if (ff_h263_decode_init(avctx) < 0)
        return -1;

    // We unquantize inter blocks as we parse them.
    h->c.dct_unquantize_inter = NULL;

    h->decode_header = msmpeg4_decode_picture_header;

    ff_msmpeg4_common_init(&h->c);

    switch (h->c.msmpeg4_version) {
    case MSMP4_V1:
    case MSMP4_V2:
        h->decode_mb = msmpeg4v12_decode_mb;
        break;
    case MSMP4_V3:
    case MSMP4_WMV1:
        h->decode_mb = msmpeg4v34_decode_mb;
        break;
    case MSMP4_WMV2:
        break;
    default:
        av_unreachable("List contains all cases using ff_msmpeg4_decode_init()");
    }

    h->slice_height = h->c.mb_height; //to avoid 1/0 if the first frame is not a keyframe

    ff_thread_once(&init_static_once, msmpeg4_decode_init_static);

    return 0;
}

const FFCodec ff_msmpeg4v1_decoder = {
    .p.name         = "msmpeg4v1",
    CODEC_LONG_NAME("MPEG-4 part 2 Microsoft variant version 1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V1,
    .priv_data_size = sizeof(MSMP4DecContext),
    .init           = ff_msmpeg4_decode_init,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .close          = ff_mpv_decode_close,
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
};

const FFCodec ff_msmpeg4v2_decoder = {
    .p.name         = "msmpeg4v2",
    CODEC_LONG_NAME("MPEG-4 part 2 Microsoft variant version 2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V2,
    .priv_data_size = sizeof(MSMP4DecContext),
    .init           = ff_msmpeg4_decode_init,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .close          = ff_mpv_decode_close,
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
};

const FFCodec ff_msmpeg4v3_decoder = {
    .p.name         = "msmpeg4",
    CODEC_LONG_NAME("MPEG-4 part 2 Microsoft variant version 3"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V3,
    .priv_data_size = sizeof(MSMP4DecContext),
    .init           = ff_msmpeg4_decode_init,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .close          = ff_mpv_decode_close,
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
};

const FFCodec ff_wmv1_decoder = {
    .p.name         = "wmv1",
    CODEC_LONG_NAME("Windows Media Video 7"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV1,
    .priv_data_size = sizeof(MSMP4DecContext),
    .init           = ff_msmpeg4_decode_init,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .close          = ff_mpv_decode_close,
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
};
