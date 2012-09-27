/*
 * VC-1 and WMV3 decoder
 * Copyright (c) 2006-2007 Konstantin Shishkov
 * Partly based on vc9.c (c) 2005 Anonymous, Alex Beregszaszi, Michael Niedermayer
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
 * VC-1 and WMV3 decoder
 *
 */
#include "internal.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263.h"
#include "vc1.h"
#include "vc1data.h"
#include "vc1acdata.h"
#include "msmpeg4data.h"
#include "unary.h"
#include "simple_idct.h"
#include "mathops.h"
#include "vdpau_internal.h"

#undef NDEBUG
#include <assert.h>

#define MB_INTRA_VLC_BITS 9
#define DC_VLC_BITS 9
#define AC_VLC_BITS 9
static const uint16_t table_mb_intra[64][2];


static const uint16_t vlc_offs[] = {
       0,   520,   552,   616,  1128,  1160, 1224, 1740, 1772, 1836, 1900, 2436,
    2986,  3050,  3610,  4154,  4218,  4746, 5326, 5390, 5902, 6554, 7658, 8620,
    9262, 10202, 10756, 11310, 12228, 15078
};

/**
 * Init VC-1 specific tables and VC1Context members
 * @param v The VC1Context to initialize
 * @return Status
 */
static int vc1_init_common(VC1Context *v)
{
    static int done = 0;
    int i = 0;
    static VLC_TYPE vlc_table[15078][2];

    v->hrd_rate = v->hrd_buffer = NULL;

    /* VLC tables */
    if(!done)
    {
        INIT_VLC_STATIC(&ff_vc1_bfraction_vlc, VC1_BFRACTION_VLC_BITS, 23,
                 ff_vc1_bfraction_bits, 1, 1,
                 ff_vc1_bfraction_codes, 1, 1, 1 << VC1_BFRACTION_VLC_BITS);
        INIT_VLC_STATIC(&ff_vc1_norm2_vlc, VC1_NORM2_VLC_BITS, 4,
                 ff_vc1_norm2_bits, 1, 1,
                 ff_vc1_norm2_codes, 1, 1, 1 << VC1_NORM2_VLC_BITS);
        INIT_VLC_STATIC(&ff_vc1_norm6_vlc, VC1_NORM6_VLC_BITS, 64,
                 ff_vc1_norm6_bits, 1, 1,
                 ff_vc1_norm6_codes, 2, 2, 556);
        INIT_VLC_STATIC(&ff_vc1_imode_vlc, VC1_IMODE_VLC_BITS, 7,
                 ff_vc1_imode_bits, 1, 1,
                 ff_vc1_imode_codes, 1, 1, 1 << VC1_IMODE_VLC_BITS);
        for (i=0; i<3; i++)
        {
            ff_vc1_ttmb_vlc[i].table = &vlc_table[vlc_offs[i*3+0]];
            ff_vc1_ttmb_vlc[i].table_allocated = vlc_offs[i*3+1] - vlc_offs[i*3+0];
            init_vlc(&ff_vc1_ttmb_vlc[i], VC1_TTMB_VLC_BITS, 16,
                     ff_vc1_ttmb_bits[i], 1, 1,
                     ff_vc1_ttmb_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_ttblk_vlc[i].table = &vlc_table[vlc_offs[i*3+1]];
            ff_vc1_ttblk_vlc[i].table_allocated = vlc_offs[i*3+2] - vlc_offs[i*3+1];
            init_vlc(&ff_vc1_ttblk_vlc[i], VC1_TTBLK_VLC_BITS, 8,
                     ff_vc1_ttblk_bits[i], 1, 1,
                     ff_vc1_ttblk_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_subblkpat_vlc[i].table = &vlc_table[vlc_offs[i*3+2]];
            ff_vc1_subblkpat_vlc[i].table_allocated = vlc_offs[i*3+3] - vlc_offs[i*3+2];
            init_vlc(&ff_vc1_subblkpat_vlc[i], VC1_SUBBLKPAT_VLC_BITS, 15,
                     ff_vc1_subblkpat_bits[i], 1, 1,
                     ff_vc1_subblkpat_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        for(i=0; i<4; i++)
        {
            ff_vc1_4mv_block_pattern_vlc[i].table = &vlc_table[vlc_offs[i*3+9]];
            ff_vc1_4mv_block_pattern_vlc[i].table_allocated = vlc_offs[i*3+10] - vlc_offs[i*3+9];
            init_vlc(&ff_vc1_4mv_block_pattern_vlc[i], VC1_4MV_BLOCK_PATTERN_VLC_BITS, 16,
                     ff_vc1_4mv_block_pattern_bits[i], 1, 1,
                     ff_vc1_4mv_block_pattern_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_cbpcy_p_vlc[i].table = &vlc_table[vlc_offs[i*3+10]];
            ff_vc1_cbpcy_p_vlc[i].table_allocated = vlc_offs[i*3+11] - vlc_offs[i*3+10];
            init_vlc(&ff_vc1_cbpcy_p_vlc[i], VC1_CBPCY_P_VLC_BITS, 64,
                     ff_vc1_cbpcy_p_bits[i], 1, 1,
                     ff_vc1_cbpcy_p_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_mv_diff_vlc[i].table = &vlc_table[vlc_offs[i*3+11]];
            ff_vc1_mv_diff_vlc[i].table_allocated = vlc_offs[i*3+12] - vlc_offs[i*3+11];
            init_vlc(&ff_vc1_mv_diff_vlc[i], VC1_MV_DIFF_VLC_BITS, 73,
                     ff_vc1_mv_diff_bits[i], 1, 1,
                     ff_vc1_mv_diff_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
        }
        for(i=0; i<8; i++){
            ff_vc1_ac_coeff_table[i].table = &vlc_table[vlc_offs[i+21]];
            ff_vc1_ac_coeff_table[i].table_allocated = vlc_offs[i+22] - vlc_offs[i+21];
            init_vlc(&ff_vc1_ac_coeff_table[i], AC_VLC_BITS, vc1_ac_sizes[i],
                     &vc1_ac_tables[i][0][1], 8, 4,
                     &vc1_ac_tables[i][0][0], 8, 4, INIT_VLC_USE_NEW_STATIC);
        }
        done = 1;
    }

    /* Other defaults */
    v->pq = -1;
    v->mvrange = 0; /* 7.1.1.18, p80 */

    return 0;
}

/***********************************************************************/
/**
 * @defgroup vc1bitplane VC-1 Bitplane decoding
 * @see 8.7, p56
 * @{
 */

/**
 * Imode types
 * @{
 */
enum Imode {
    IMODE_RAW,
    IMODE_NORM2,
    IMODE_DIFF2,
    IMODE_NORM6,
    IMODE_DIFF6,
    IMODE_ROWSKIP,
    IMODE_COLSKIP
};
/** @} */ //imode defines


/** @} */ //Bitplane group

static void vc1_loop_filter_iblk(MpegEncContext *s, int pq)
{
    int i, j;
    if(!s->first_slice_line)
        s->dsp.vc1_v_loop_filter16(s->dest[0], s->linesize, pq);
    s->dsp.vc1_v_loop_filter16(s->dest[0] + 8*s->linesize, s->linesize, pq);
    for(i = !s->mb_x*8; i < 16; i += 8)
        s->dsp.vc1_h_loop_filter16(s->dest[0] + i, s->linesize, pq);
    for(j = 0; j < 2; j++){
        if(!s->first_slice_line)
            s->dsp.vc1_v_loop_filter8(s->dest[j+1], s->uvlinesize, pq);
        if(s->mb_x)
            s->dsp.vc1_h_loop_filter8(s->dest[j+1], s->uvlinesize, pq);
    }
}

/** Put block onto picture
 */
static void vc1_put_block(VC1Context *v, DCTELEM block[6][64])
{
    uint8_t *Y;
    int ys, us, vs;
    DSPContext *dsp = &v->s.dsp;

    if(v->rangeredfrm) {
        int i, j, k;
        for(k = 0; k < 6; k++)
            for(j = 0; j < 8; j++)
                for(i = 0; i < 8; i++)
                    block[k][i + j*8] = ((block[k][i + j*8] - 128) << 1) + 128;

    }
    ys = v->s.current_picture.linesize[0];
    us = v->s.current_picture.linesize[1];
    vs = v->s.current_picture.linesize[2];
    Y = v->s.dest[0];

    dsp->put_pixels_clamped(block[0], Y, ys);
    dsp->put_pixels_clamped(block[1], Y + 8, ys);
    Y += ys * 8;
    dsp->put_pixels_clamped(block[2], Y, ys);
    dsp->put_pixels_clamped(block[3], Y + 8, ys);

    if(!(v->s.flags & CODEC_FLAG_GRAY)) {
        dsp->put_pixels_clamped(block[4], v->s.dest[1], us);
        dsp->put_pixels_clamped(block[5], v->s.dest[2], vs);
    }
}

/** Do motion compensation over 1 macroblock
 * Mostly adapted hpel_motion and qpel_motion from mpegvideo.c
 */
static void vc1_mc_1mv(VC1Context *v, int dir)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcY, *srcU, *srcV;
    int dxy, mx, my, uvmx, uvmy, src_x, src_y, uvsrc_x, uvsrc_y;

    if(!v->s.last_picture.data[0])return;

    mx = s->mv[dir][0][0];
    my = s->mv[dir][0][1];

    // store motion vectors for further use in B frames
    if(s->pict_type == FF_P_TYPE) {
        s->current_picture.motion_val[1][s->block_index[0]][0] = mx;
        s->current_picture.motion_val[1][s->block_index[0]][1] = my;
    }
    uvmx = (mx + ((mx & 3) == 3)) >> 1;
    uvmy = (my + ((my & 3) == 3)) >> 1;
    if(v->fastuvmc) {
        uvmx = uvmx + ((uvmx<0)?(uvmx&1):-(uvmx&1));
        uvmy = uvmy + ((uvmy<0)?(uvmy&1):-(uvmy&1));
    }
    if(!dir) {
        srcY = s->last_picture.data[0];
        srcU = s->last_picture.data[1];
        srcV = s->last_picture.data[2];
    } else {
        srcY = s->next_picture.data[0];
        srcU = s->next_picture.data[1];
        srcV = s->next_picture.data[2];
    }

    src_x = s->mb_x * 16 + (mx >> 2);
    src_y = s->mb_y * 16 + (my >> 2);
    uvsrc_x = s->mb_x * 8 + (uvmx >> 2);
    uvsrc_y = s->mb_y * 8 + (uvmy >> 2);

    if(v->profile != PROFILE_ADVANCED){
        src_x   = av_clip(  src_x, -16, s->mb_width  * 16);
        src_y   = av_clip(  src_y, -16, s->mb_height * 16);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->mb_width  *  8);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->mb_height *  8);
    }else{
        src_x   = av_clip(  src_x, -17, s->avctx->coded_width);
        src_y   = av_clip(  src_y, -18, s->avctx->coded_height + 1);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
    }

    srcY += src_y * s->linesize + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    /* for grayscale we should not try to read from unknown area */
    if(s->flags & CODEC_FLAG_GRAY) {
        srcU = s->edge_emu_buffer + 18 * s->linesize;
        srcV = s->edge_emu_buffer + 18 * s->linesize;
    }

    if(v->rangeredfrm || (v->mv_mode == MV_PMODE_INTENSITY_COMP)
       || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx&3) - 16 - s->mspel*3
       || (unsigned)(src_y - s->mspel) > s->v_edge_pos - (my&3) - 16 - s->mspel*3){
        uint8_t *uvbuf= s->edge_emu_buffer + 19 * s->linesize;

        srcY -= s->mspel * (1 + s->linesize);
        ff_emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize, 17+s->mspel*2, 17+s->mspel*2,
                            src_x - s->mspel, src_y - s->mspel, s->h_edge_pos, s->v_edge_pos);
        srcY = s->edge_emu_buffer;
        ff_emulated_edge_mc(uvbuf     , srcU, s->uvlinesize, 8+1, 8+1,
                            uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        ff_emulated_edge_mc(uvbuf + 16, srcV, s->uvlinesize, 8+1, 8+1,
                            uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        srcU = uvbuf;
        srcV = uvbuf + 16;
        /* if we deal with range reduction we need to scale source blocks */
        if(v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for(j = 0; j < 17 + s->mspel*2; j++) {
                for(i = 0; i < 17 + s->mspel*2; i++) src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize;
            }
            src = srcU; src2 = srcV;
            for(j = 0; j < 9; j++) {
                for(i = 0; i < 9; i++) {
                    src[i] = ((src[i] - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if(v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for(j = 0; j < 17 + s->mspel*2; j++) {
                for(i = 0; i < 17 + s->mspel*2; i++) src[i] = v->luty[src[i]];
                src += s->linesize;
            }
            src = srcU; src2 = srcV;
            for(j = 0; j < 9; j++) {
                for(i = 0; i < 9; i++) {
                    src[i] = v->lutuv[src[i]];
                    src2[i] = v->lutuv[src2[i]];
                }
                src += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if(s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        dsp->put_vc1_mspel_pixels_tab[dxy](s->dest[0]    , srcY    , s->linesize, v->rnd);
        dsp->put_vc1_mspel_pixels_tab[dxy](s->dest[0] + 8, srcY + 8, s->linesize, v->rnd);
        srcY += s->linesize * 8;
        dsp->put_vc1_mspel_pixels_tab[dxy](s->dest[0] + 8 * s->linesize    , srcY    , s->linesize, v->rnd);
        dsp->put_vc1_mspel_pixels_tab[dxy](s->dest[0] + 8 * s->linesize + 8, srcY + 8, s->linesize, v->rnd);
    } else { // hpel mc - always used for luma
        dxy = (my & 2) | ((mx & 2) >> 1);

        if(!v->rnd)
            dsp->put_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
        else
            dsp->put_no_rnd_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
    }

    if(s->flags & CODEC_FLAG_GRAY) return;
    /* Chroma MC always uses qpel bilinear */
    uvmx = (uvmx&3)<<1;
    uvmy = (uvmy&3)<<1;
    if(!v->rnd){
        dsp->put_h264_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->put_h264_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }else{
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

/** Do motion compensation for 4-MV macroblock - luminance block
 */
static void vc1_mc_4mv_luma(VC1Context *v, int n)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcY;
    int dxy, mx, my, src_x, src_y;
    int off;

    if(!v->s.last_picture.data[0])return;
    mx = s->mv[0][n][0];
    my = s->mv[0][n][1];
    srcY = s->last_picture.data[0];

    off = s->linesize * 4 * (n&2) + (n&1) * 8;

    src_x = s->mb_x * 16 + (n&1) * 8 + (mx >> 2);
    src_y = s->mb_y * 16 + (n&2) * 4 + (my >> 2);

    if(v->profile != PROFILE_ADVANCED){
        src_x   = av_clip(  src_x, -16, s->mb_width  * 16);
        src_y   = av_clip(  src_y, -16, s->mb_height * 16);
    }else{
        src_x   = av_clip(  src_x, -17, s->avctx->coded_width);
        src_y   = av_clip(  src_y, -18, s->avctx->coded_height + 1);
    }

    srcY += src_y * s->linesize + src_x;

    if(v->rangeredfrm || (v->mv_mode == MV_PMODE_INTENSITY_COMP)
       || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx&3) - 8 - s->mspel*2
       || (unsigned)(src_y - s->mspel) > s->v_edge_pos - (my&3) - 8 - s->mspel*2){
        srcY -= s->mspel * (1 + s->linesize);
        ff_emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize, 9+s->mspel*2, 9+s->mspel*2,
                            src_x - s->mspel, src_y - s->mspel, s->h_edge_pos, s->v_edge_pos);
        srcY = s->edge_emu_buffer;
        /* if we deal with range reduction we need to scale source blocks */
        if(v->rangeredfrm) {
            int i, j;
            uint8_t *src;

            src = srcY;
            for(j = 0; j < 9 + s->mspel*2; j++) {
                for(i = 0; i < 9 + s->mspel*2; i++) src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if(v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            int i, j;
            uint8_t *src;

            src = srcY;
            for(j = 0; j < 9 + s->mspel*2; j++) {
                for(i = 0; i < 9 + s->mspel*2; i++) src[i] = v->luty[src[i]];
                src += s->linesize;
            }
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if(s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        dsp->put_vc1_mspel_pixels_tab[dxy](s->dest[0] + off, srcY, s->linesize, v->rnd);
    } else { // hpel mc - always used for luma
        dxy = (my & 2) | ((mx & 2) >> 1);
        if(!v->rnd)
            dsp->put_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize, 8);
        else
            dsp->put_no_rnd_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize, 8);
    }
}

static inline int median4(int a, int b, int c, int d)
{
    if(a < b) {
        if(c < d) return (FFMIN(b, d) + FFMAX(a, c)) / 2;
        else      return (FFMIN(b, c) + FFMAX(a, d)) / 2;
    } else {
        if(c < d) return (FFMIN(a, d) + FFMAX(b, c)) / 2;
        else      return (FFMIN(a, c) + FFMAX(b, d)) / 2;
    }
}


/** Do motion compensation for 4-MV macroblock - both chroma blocks
 */
static void vc1_mc_4mv_chroma(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcU, *srcV;
    int uvmx, uvmy, uvsrc_x, uvsrc_y;
    int i, idx, tx = 0, ty = 0;
    int mvx[4], mvy[4], intra[4];
    static const int count[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

    if(!v->s.last_picture.data[0])return;
    if(s->flags & CODEC_FLAG_GRAY) return;

    for(i = 0; i < 4; i++) {
        mvx[i] = s->mv[0][i][0];
        mvy[i] = s->mv[0][i][1];
        intra[i] = v->mb_type[0][s->block_index[i]];
    }

    /* calculate chroma MV vector from four luma MVs */
    idx = (intra[3] << 3) | (intra[2] << 2) | (intra[1] << 1) | intra[0];
    if(!idx) { // all blocks are inter
        tx = median4(mvx[0], mvx[1], mvx[2], mvx[3]);
        ty = median4(mvy[0], mvy[1], mvy[2], mvy[3]);
    } else if(count[idx] == 1) { // 3 inter blocks
        switch(idx) {
        case 0x1:
            tx = mid_pred(mvx[1], mvx[2], mvx[3]);
            ty = mid_pred(mvy[1], mvy[2], mvy[3]);
            break;
        case 0x2:
            tx = mid_pred(mvx[0], mvx[2], mvx[3]);
            ty = mid_pred(mvy[0], mvy[2], mvy[3]);
            break;
        case 0x4:
            tx = mid_pred(mvx[0], mvx[1], mvx[3]);
            ty = mid_pred(mvy[0], mvy[1], mvy[3]);
            break;
        case 0x8:
            tx = mid_pred(mvx[0], mvx[1], mvx[2]);
            ty = mid_pred(mvy[0], mvy[1], mvy[2]);
            break;
        }
    } else if(count[idx] == 2) {
        int t1 = 0, t2 = 0;
        for(i=0; i<3;i++) if(!intra[i]) {t1 = i; break;}
        for(i= t1+1; i<4; i++)if(!intra[i]) {t2 = i; break;}
        tx = (mvx[t1] + mvx[t2]) / 2;
        ty = (mvy[t1] + mvy[t2]) / 2;
    } else {
        s->current_picture.motion_val[1][s->block_index[0]][0] = 0;
        s->current_picture.motion_val[1][s->block_index[0]][1] = 0;
        return; //no need to do MC for inter blocks
    }

    s->current_picture.motion_val[1][s->block_index[0]][0] = tx;
    s->current_picture.motion_val[1][s->block_index[0]][1] = ty;
    uvmx = (tx + ((tx&3) == 3)) >> 1;
    uvmy = (ty + ((ty&3) == 3)) >> 1;
    if(v->fastuvmc) {
        uvmx = uvmx + ((uvmx<0)?(uvmx&1):-(uvmx&1));
        uvmy = uvmy + ((uvmy<0)?(uvmy&1):-(uvmy&1));
    }

    uvsrc_x = s->mb_x * 8 + (uvmx >> 2);
    uvsrc_y = s->mb_y * 8 + (uvmy >> 2);

    if(v->profile != PROFILE_ADVANCED){
        uvsrc_x = av_clip(uvsrc_x,  -8, s->mb_width  *  8);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->mb_height *  8);
    }else{
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
    }

    srcU = s->last_picture.data[1] + uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV = s->last_picture.data[2] + uvsrc_y * s->uvlinesize + uvsrc_x;
    if(v->rangeredfrm || (v->mv_mode == MV_PMODE_INTENSITY_COMP)
       || (unsigned)uvsrc_x > (s->h_edge_pos >> 1) - 9
       || (unsigned)uvsrc_y > (s->v_edge_pos >> 1) - 9){
        ff_emulated_edge_mc(s->edge_emu_buffer     , srcU, s->uvlinesize, 8+1, 8+1,
                            uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        ff_emulated_edge_mc(s->edge_emu_buffer + 16, srcV, s->uvlinesize, 8+1, 8+1,
                            uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        srcU = s->edge_emu_buffer;
        srcV = s->edge_emu_buffer + 16;

        /* if we deal with range reduction we need to scale source blocks */
        if(v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src = srcU; src2 = srcV;
            for(j = 0; j < 9; j++) {
                for(i = 0; i < 9; i++) {
                    src[i] = ((src[i] - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if(v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            int i, j;
            uint8_t *src, *src2;

            src = srcU; src2 = srcV;
            for(j = 0; j < 9; j++) {
                for(i = 0; i < 9; i++) {
                    src[i] = v->lutuv[src[i]];
                    src2[i] = v->lutuv[src2[i]];
                }
                src += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
    }

    /* Chroma MC always uses qpel bilinear */
    uvmx = (uvmx&3)<<1;
    uvmy = (uvmy&3)<<1;
    if(!v->rnd){
        dsp->put_h264_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->put_h264_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }else{
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

/***********************************************************************/
/**
 * @defgroup vc1block VC-1 Block-level functions
 * @see 7.1.4, p91 and 8.1.1.7, p(1)04
 * @{
 */

/**
 * @def GET_MQUANT
 * @brief Get macroblock-level quantizer scale
 */
#define GET_MQUANT()                                           \
  if (v->dquantfrm)                                            \
  {                                                            \
    int edges = 0;                                             \
    if (v->dqprofile == DQPROFILE_ALL_MBS)                     \
    {                                                          \
      if (v->dqbilevel)                                        \
      {                                                        \
        mquant = (get_bits1(gb)) ? v->altpq : v->pq;           \
      }                                                        \
      else                                                     \
      {                                                        \
        mqdiff = get_bits(gb, 3);                              \
        if (mqdiff != 7) mquant = v->pq + mqdiff;              \
        else mquant = get_bits(gb, 5);                         \
      }                                                        \
    }                                                          \
    if(v->dqprofile == DQPROFILE_SINGLE_EDGE)                  \
        edges = 1 << v->dqsbedge;                              \
    else if(v->dqprofile == DQPROFILE_DOUBLE_EDGES)            \
        edges = (3 << v->dqsbedge) % 15;                       \
    else if(v->dqprofile == DQPROFILE_FOUR_EDGES)              \
        edges = 15;                                            \
    if((edges&1) && !s->mb_x)                                  \
        mquant = v->altpq;                                     \
    if((edges&2) && s->first_slice_line)                       \
        mquant = v->altpq;                                     \
    if((edges&4) && s->mb_x == (s->mb_width - 1))              \
        mquant = v->altpq;                                     \
    if((edges&8) && s->mb_y == (s->mb_height - 1))             \
        mquant = v->altpq;                                     \
  }

/**
 * @def GET_MVDATA(_dmv_x, _dmv_y)
 * @brief Get MV differentials
 * @see MVDATA decoding from 8.3.5.2, p(1)20
 * @param _dmv_x Horizontal differential for decoded MV
 * @param _dmv_y Vertical differential for decoded MV
 */
#define GET_MVDATA(_dmv_x, _dmv_y)                                  \
  index = 1 + get_vlc2(gb, ff_vc1_mv_diff_vlc[s->mv_table_index].table,\
                       VC1_MV_DIFF_VLC_BITS, 2);                    \
  if (index > 36)                                                   \
  {                                                                 \
    mb_has_coeffs = 1;                                              \
    index -= 37;                                                    \
  }                                                                 \
  else mb_has_coeffs = 0;                                           \
  s->mb_intra = 0;                                                  \
  if (!index) { _dmv_x = _dmv_y = 0; }                              \
  else if (index == 35)                                             \
  {                                                                 \
    _dmv_x = get_bits(gb, v->k_x - 1 + s->quarter_sample);          \
    _dmv_y = get_bits(gb, v->k_y - 1 + s->quarter_sample);          \
  }                                                                 \
  else if (index == 36)                                             \
  {                                                                 \
    _dmv_x = 0;                                                     \
    _dmv_y = 0;                                                     \
    s->mb_intra = 1;                                                \
  }                                                                 \
  else                                                              \
  {                                                                 \
    index1 = index%6;                                               \
    if (!s->quarter_sample && index1 == 5) val = 1;                 \
    else                                   val = 0;                 \
    if(size_table[index1] - val > 0)                                \
        val = get_bits(gb, size_table[index1] - val);               \
    else                                   val = 0;                 \
    sign = 0 - (val&1);                                             \
    _dmv_x = (sign ^ ((val>>1) + offset_table[index1])) - sign;     \
                                                                    \
    index1 = index/6;                                               \
    if (!s->quarter_sample && index1 == 5) val = 1;                 \
    else                                   val = 0;                 \
    if(size_table[index1] - val > 0)                                \
        val = get_bits(gb, size_table[index1] - val);               \
    else                                   val = 0;                 \
    sign = 0 - (val&1);                                             \
    _dmv_y = (sign ^ ((val>>1) + offset_table[index1])) - sign;     \
  }

/** Predict and set motion vector
 */
static inline void vc1_pred_mv(MpegEncContext *s, int n, int dmv_x, int dmv_y, int mv1, int r_x, int r_y, uint8_t* is_intra)
{
    int xy, wrap, off = 0;
    int16_t *A, *B, *C;
    int px, py;
    int sum;

    /* scale MV difference to be quad-pel */
    dmv_x <<= 1 - s->quarter_sample;
    dmv_y <<= 1 - s->quarter_sample;

    wrap = s->b8_stride;
    xy = s->block_index[n];

    if(s->mb_intra){
        s->mv[0][n][0] = s->current_picture.motion_val[0][xy][0] = 0;
        s->mv[0][n][1] = s->current_picture.motion_val[0][xy][1] = 0;
        s->current_picture.motion_val[1][xy][0] = 0;
        s->current_picture.motion_val[1][xy][1] = 0;
        if(mv1) { /* duplicate motion data for 1-MV block */
            s->current_picture.motion_val[0][xy + 1][0] = 0;
            s->current_picture.motion_val[0][xy + 1][1] = 0;
            s->current_picture.motion_val[0][xy + wrap][0] = 0;
            s->current_picture.motion_val[0][xy + wrap][1] = 0;
            s->current_picture.motion_val[0][xy + wrap + 1][0] = 0;
            s->current_picture.motion_val[0][xy + wrap + 1][1] = 0;
            s->current_picture.motion_val[1][xy + 1][0] = 0;
            s->current_picture.motion_val[1][xy + 1][1] = 0;
            s->current_picture.motion_val[1][xy + wrap][0] = 0;
            s->current_picture.motion_val[1][xy + wrap][1] = 0;
            s->current_picture.motion_val[1][xy + wrap + 1][0] = 0;
            s->current_picture.motion_val[1][xy + wrap + 1][1] = 0;
        }
        return;
    }

    C = s->current_picture.motion_val[0][xy - 1];
    A = s->current_picture.motion_val[0][xy - wrap];
    if(mv1)
        off = (s->mb_x == (s->mb_width - 1)) ? -1 : 2;
    else {
        //in 4-MV mode different blocks have different B predictor position
        switch(n){
        case 0:
            off = (s->mb_x > 0) ? -1 : 1;
            break;
        case 1:
            off = (s->mb_x == (s->mb_width - 1)) ? -1 : 1;
            break;
        case 2:
            off = 1;
            break;
        case 3:
            off = -1;
        }
    }
    B = s->current_picture.motion_val[0][xy - wrap + off];

    if(!s->first_slice_line || (n==2 || n==3)) { // predictor A is not out of bounds
        if(s->mb_width == 1) {
            px = A[0];
            py = A[1];
        } else {
            px = mid_pred(A[0], B[0], C[0]);
            py = mid_pred(A[1], B[1], C[1]);
        }
    } else if(s->mb_x || (n==1 || n==3)) { // predictor C is not out of bounds
        px = C[0];
        py = C[1];
    } else {
        px = py = 0;
    }
    /* Pullback MV as specified in 8.3.5.3.4 */
    {
        int qx, qy, X, Y;
        qx = (s->mb_x << 6) + ((n==1 || n==3) ? 32 : 0);
        qy = (s->mb_y << 6) + ((n==2 || n==3) ? 32 : 0);
        X = (s->mb_width << 6) - 4;
        Y = (s->mb_height << 6) - 4;
        if(mv1) {
            if(qx + px < -60) px = -60 - qx;
            if(qy + py < -60) py = -60 - qy;
        } else {
            if(qx + px < -28) px = -28 - qx;
            if(qy + py < -28) py = -28 - qy;
        }
        if(qx + px > X) px = X - qx;
        if(qy + py > Y) py = Y - qy;
    }
    /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
    if((!s->first_slice_line || (n==2 || n==3)) && (s->mb_x || (n==1 || n==3))) {
        if(is_intra[xy - wrap])
            sum = FFABS(px) + FFABS(py);
        else
            sum = FFABS(px - A[0]) + FFABS(py - A[1]);
        if(sum > 32) {
            if(get_bits1(&s->gb)) {
                px = A[0];
                py = A[1];
            } else {
                px = C[0];
                py = C[1];
            }
        } else {
            if(is_intra[xy - 1])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - C[0]) + FFABS(py - C[1]);
            if(sum > 32) {
                if(get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            }
        }
    }
    /* store MV using signed modulus of MV range defined in 4.11 */
    s->mv[0][n][0] = s->current_picture.motion_val[0][xy][0] = ((px + dmv_x + r_x) & ((r_x << 1) - 1)) - r_x;
    s->mv[0][n][1] = s->current_picture.motion_val[0][xy][1] = ((py + dmv_y + r_y) & ((r_y << 1) - 1)) - r_y;
    if(mv1) { /* duplicate motion data for 1-MV block */
        s->current_picture.motion_val[0][xy + 1][0] = s->current_picture.motion_val[0][xy][0];
        s->current_picture.motion_val[0][xy + 1][1] = s->current_picture.motion_val[0][xy][1];
        s->current_picture.motion_val[0][xy + wrap][0] = s->current_picture.motion_val[0][xy][0];
        s->current_picture.motion_val[0][xy + wrap][1] = s->current_picture.motion_val[0][xy][1];
        s->current_picture.motion_val[0][xy + wrap + 1][0] = s->current_picture.motion_val[0][xy][0];
        s->current_picture.motion_val[0][xy + wrap + 1][1] = s->current_picture.motion_val[0][xy][1];
    }
}

/** Motion compensation for direct or interpolated blocks in B-frames
 */
static void vc1_interp_mc(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcY, *srcU, *srcV;
    int dxy, mx, my, uvmx, uvmy, src_x, src_y, uvsrc_x, uvsrc_y;

    if(!v->s.next_picture.data[0])return;

    mx = s->mv[1][0][0];
    my = s->mv[1][0][1];
    uvmx = (mx + ((mx & 3) == 3)) >> 1;
    uvmy = (my + ((my & 3) == 3)) >> 1;
    if(v->fastuvmc) {
        uvmx = uvmx + ((uvmx<0)?-(uvmx&1):(uvmx&1));
        uvmy = uvmy + ((uvmy<0)?-(uvmy&1):(uvmy&1));
    }
    srcY = s->next_picture.data[0];
    srcU = s->next_picture.data[1];
    srcV = s->next_picture.data[2];

    src_x = s->mb_x * 16 + (mx >> 2);
    src_y = s->mb_y * 16 + (my >> 2);
    uvsrc_x = s->mb_x * 8 + (uvmx >> 2);
    uvsrc_y = s->mb_y * 8 + (uvmy >> 2);

    if(v->profile != PROFILE_ADVANCED){
        src_x   = av_clip(  src_x, -16, s->mb_width  * 16);
        src_y   = av_clip(  src_y, -16, s->mb_height * 16);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->mb_width  *  8);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->mb_height *  8);
    }else{
        src_x   = av_clip(  src_x, -17, s->avctx->coded_width);
        src_y   = av_clip(  src_y, -18, s->avctx->coded_height + 1);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
    }

    srcY += src_y * s->linesize + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    /* for grayscale we should not try to read from unknown area */
    if(s->flags & CODEC_FLAG_GRAY) {
        srcU = s->edge_emu_buffer + 18 * s->linesize;
        srcV = s->edge_emu_buffer + 18 * s->linesize;
    }

    if(v->rangeredfrm
       || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx&3) - 16 - s->mspel*3
       || (unsigned)(src_y - s->mspel) > s->v_edge_pos - (my&3) - 16 - s->mspel*3){
        uint8_t *uvbuf= s->edge_emu_buffer + 19 * s->linesize;

        srcY -= s->mspel * (1 + s->linesize);
        ff_emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize, 17+s->mspel*2, 17+s->mspel*2,
                            src_x - s->mspel, src_y - s->mspel, s->h_edge_pos, s->v_edge_pos);
        srcY = s->edge_emu_buffer;
        ff_emulated_edge_mc(uvbuf     , srcU, s->uvlinesize, 8+1, 8+1,
                            uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        ff_emulated_edge_mc(uvbuf + 16, srcV, s->uvlinesize, 8+1, 8+1,
                            uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        srcU = uvbuf;
        srcV = uvbuf + 16;
        /* if we deal with range reduction we need to scale source blocks */
        if(v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for(j = 0; j < 17 + s->mspel*2; j++) {
                for(i = 0; i < 17 + s->mspel*2; i++) src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize;
            }
            src = srcU; src2 = srcV;
            for(j = 0; j < 9; j++) {
                for(i = 0; i < 9; i++) {
                    src[i] = ((src[i] - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if(s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        dsp->avg_vc1_mspel_pixels_tab[dxy](s->dest[0]    , srcY    , s->linesize, v->rnd);
        dsp->avg_vc1_mspel_pixels_tab[dxy](s->dest[0] + 8, srcY + 8, s->linesize, v->rnd);
        srcY += s->linesize * 8;
        dsp->avg_vc1_mspel_pixels_tab[dxy](s->dest[0] + 8 * s->linesize    , srcY    , s->linesize, v->rnd);
        dsp->avg_vc1_mspel_pixels_tab[dxy](s->dest[0] + 8 * s->linesize + 8, srcY + 8, s->linesize, v->rnd);
    } else { // hpel mc
        dxy = (my & 2) | ((mx & 2) >> 1);

        if(!v->rnd)
            dsp->avg_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
        else
            dsp->avg_no_rnd_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
    }

    if(s->flags & CODEC_FLAG_GRAY) return;
    /* Chroma MC always uses qpel blilinear */
    uvmx = (uvmx&3)<<1;
    uvmy = (uvmy&3)<<1;
    if(!v->rnd){
        dsp->avg_h264_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->avg_h264_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }else{
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

static av_always_inline int scale_mv(int value, int bfrac, int inv, int qs)
{
    int n = bfrac;

#if B_FRACTION_DEN==256
    if(inv)
        n -= 256;
    if(!qs)
        return 2 * ((value * n + 255) >> 9);
    return (value * n + 128) >> 8;
#else
    if(inv)
        n -= B_FRACTION_DEN;
    if(!qs)
        return 2 * ((value * n + B_FRACTION_DEN - 1) / (2 * B_FRACTION_DEN));
    return (value * n + B_FRACTION_DEN/2) / B_FRACTION_DEN;
#endif
}

/** Reconstruct motion vector for B-frame and do motion compensation
 */
static inline void vc1_b_mc(VC1Context *v, int dmv_x[2], int dmv_y[2], int direct, int mode)
{
    if(v->use_ic) {
        v->mv_mode2 = v->mv_mode;
        v->mv_mode = MV_PMODE_INTENSITY_COMP;
    }
    if(direct) {
        vc1_mc_1mv(v, 0);
        vc1_interp_mc(v);
        if(v->use_ic) v->mv_mode = v->mv_mode2;
        return;
    }
    if(mode == BMV_TYPE_INTERPOLATED) {
        vc1_mc_1mv(v, 0);
        vc1_interp_mc(v);
        if(v->use_ic) v->mv_mode = v->mv_mode2;
        return;
    }

    if(v->use_ic && (mode == BMV_TYPE_BACKWARD)) v->mv_mode = v->mv_mode2;
    vc1_mc_1mv(v, (mode == BMV_TYPE_BACKWARD));
    if(v->use_ic) v->mv_mode = v->mv_mode2;
}

static inline void vc1_pred_b_mv(VC1Context *v, int dmv_x[2], int dmv_y[2], int direct, int mvtype)
{
    MpegEncContext *s = &v->s;
    int xy, wrap, off = 0;
    int16_t *A, *B, *C;
    int px, py;
    int sum;
    int r_x, r_y;
    const uint8_t *is_intra = v->mb_type[0];

    r_x = v->range_x;
    r_y = v->range_y;
    /* scale MV difference to be quad-pel */
    dmv_x[0] <<= 1 - s->quarter_sample;
    dmv_y[0] <<= 1 - s->quarter_sample;
    dmv_x[1] <<= 1 - s->quarter_sample;
    dmv_y[1] <<= 1 - s->quarter_sample;

    wrap = s->b8_stride;
    xy = s->block_index[0];

    if(s->mb_intra) {
        s->current_picture.motion_val[0][xy][0] =
        s->current_picture.motion_val[0][xy][1] =
        s->current_picture.motion_val[1][xy][0] =
        s->current_picture.motion_val[1][xy][1] = 0;
        return;
    }
    s->mv[0][0][0] = scale_mv(s->next_picture.motion_val[1][xy][0], v->bfraction, 0, s->quarter_sample);
    s->mv[0][0][1] = scale_mv(s->next_picture.motion_val[1][xy][1], v->bfraction, 0, s->quarter_sample);
    s->mv[1][0][0] = scale_mv(s->next_picture.motion_val[1][xy][0], v->bfraction, 1, s->quarter_sample);
    s->mv[1][0][1] = scale_mv(s->next_picture.motion_val[1][xy][1], v->bfraction, 1, s->quarter_sample);

    /* Pullback predicted motion vectors as specified in 8.4.5.4 */
    s->mv[0][0][0] = av_clip(s->mv[0][0][0], -60 - (s->mb_x << 6), (s->mb_width  << 6) - 4 - (s->mb_x << 6));
    s->mv[0][0][1] = av_clip(s->mv[0][0][1], -60 - (s->mb_y << 6), (s->mb_height << 6) - 4 - (s->mb_y << 6));
    s->mv[1][0][0] = av_clip(s->mv[1][0][0], -60 - (s->mb_x << 6), (s->mb_width  << 6) - 4 - (s->mb_x << 6));
    s->mv[1][0][1] = av_clip(s->mv[1][0][1], -60 - (s->mb_y << 6), (s->mb_height << 6) - 4 - (s->mb_y << 6));
    if(direct) {
        s->current_picture.motion_val[0][xy][0] = s->mv[0][0][0];
        s->current_picture.motion_val[0][xy][1] = s->mv[0][0][1];
        s->current_picture.motion_val[1][xy][0] = s->mv[1][0][0];
        s->current_picture.motion_val[1][xy][1] = s->mv[1][0][1];
        return;
    }

    if((mvtype == BMV_TYPE_FORWARD) || (mvtype == BMV_TYPE_INTERPOLATED)) {
        C = s->current_picture.motion_val[0][xy - 2];
        A = s->current_picture.motion_val[0][xy - wrap*2];
        off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        B = s->current_picture.motion_val[0][xy - wrap*2 + off];

        if(!s->mb_x) C[0] = C[1] = 0;
        if(!s->first_slice_line) { // predictor A is not out of bounds
            if(s->mb_width == 1) {
                px = A[0];
                py = A[1];
            } else {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            }
        } else if(s->mb_x) { // predictor C is not out of bounds
            px = C[0];
            py = C[1];
        } else {
            px = py = 0;
        }
        /* Pullback MV as specified in 8.3.5.3.4 */
        {
            int qx, qy, X, Y;
            if(v->profile < PROFILE_ADVANCED) {
                qx = (s->mb_x << 5);
                qy = (s->mb_y << 5);
                X = (s->mb_width << 5) - 4;
                Y = (s->mb_height << 5) - 4;
                if(qx + px < -28) px = -28 - qx;
                if(qy + py < -28) py = -28 - qy;
                if(qx + px > X) px = X - qx;
                if(qy + py > Y) py = Y - qy;
            } else {
                qx = (s->mb_x << 6);
                qy = (s->mb_y << 6);
                X = (s->mb_width << 6) - 4;
                Y = (s->mb_height << 6) - 4;
                if(qx + px < -60) px = -60 - qx;
                if(qy + py < -60) py = -60 - qy;
                if(qx + px > X) px = X - qx;
                if(qy + py > Y) py = Y - qy;
            }
        }
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
        if(0 && !s->first_slice_line && s->mb_x) {
            if(is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - A[0]) + FFABS(py - A[1]);
            if(sum > 32) {
                if(get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            } else {
                if(is_intra[xy - 2])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - C[0]) + FFABS(py - C[1]);
                if(sum > 32) {
                    if(get_bits1(&s->gb)) {
                        px = A[0];
                        py = A[1];
                    } else {
                        px = C[0];
                        py = C[1];
                    }
                }
            }
        }
        /* store MV using signed modulus of MV range defined in 4.11 */
        s->mv[0][0][0] = ((px + dmv_x[0] + r_x) & ((r_x << 1) - 1)) - r_x;
        s->mv[0][0][1] = ((py + dmv_y[0] + r_y) & ((r_y << 1) - 1)) - r_y;
    }
    if((mvtype == BMV_TYPE_BACKWARD) || (mvtype == BMV_TYPE_INTERPOLATED)) {
        C = s->current_picture.motion_val[1][xy - 2];
        A = s->current_picture.motion_val[1][xy - wrap*2];
        off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        B = s->current_picture.motion_val[1][xy - wrap*2 + off];

        if(!s->mb_x) C[0] = C[1] = 0;
        if(!s->first_slice_line) { // predictor A is not out of bounds
            if(s->mb_width == 1) {
                px = A[0];
                py = A[1];
            } else {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            }
        } else if(s->mb_x) { // predictor C is not out of bounds
            px = C[0];
            py = C[1];
        } else {
            px = py = 0;
        }
        /* Pullback MV as specified in 8.3.5.3.4 */
        {
            int qx, qy, X, Y;
            if(v->profile < PROFILE_ADVANCED) {
                qx = (s->mb_x << 5);
                qy = (s->mb_y << 5);
                X = (s->mb_width << 5) - 4;
                Y = (s->mb_height << 5) - 4;
                if(qx + px < -28) px = -28 - qx;
                if(qy + py < -28) py = -28 - qy;
                if(qx + px > X) px = X - qx;
                if(qy + py > Y) py = Y - qy;
            } else {
                qx = (s->mb_x << 6);
                qy = (s->mb_y << 6);
                X = (s->mb_width << 6) - 4;
                Y = (s->mb_height << 6) - 4;
                if(qx + px < -60) px = -60 - qx;
                if(qy + py < -60) py = -60 - qy;
                if(qx + px > X) px = X - qx;
                if(qy + py > Y) py = Y - qy;
            }
        }
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
        if(0 && !s->first_slice_line && s->mb_x) {
            if(is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - A[0]) + FFABS(py - A[1]);
            if(sum > 32) {
                if(get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            } else {
                if(is_intra[xy - 2])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - C[0]) + FFABS(py - C[1]);
                if(sum > 32) {
                    if(get_bits1(&s->gb)) {
                        px = A[0];
                        py = A[1];
                    } else {
                        px = C[0];
                        py = C[1];
                    }
                }
            }
        }
        /* store MV using signed modulus of MV range defined in 4.11 */

        s->mv[1][0][0] = ((px + dmv_x[1] + r_x) & ((r_x << 1) - 1)) - r_x;
        s->mv[1][0][1] = ((py + dmv_y[1] + r_y) & ((r_y << 1) - 1)) - r_y;
    }
    s->current_picture.motion_val[0][xy][0] = s->mv[0][0][0];
    s->current_picture.motion_val[0][xy][1] = s->mv[0][0][1];
    s->current_picture.motion_val[1][xy][0] = s->mv[1][0][0];
    s->current_picture.motion_val[1][xy][1] = s->mv[1][0][1];
}

/** Get predicted DC value for I-frames only
 * prediction dir: left=0, top=1
 * @param s MpegEncContext
 * @param overlap flag indicating that overlap filtering is used
 * @param pq integer part of picture quantizer
 * @param[in] n block index in the current MB
 * @param dc_val_ptr Pointer to DC predictor
 * @param dir_ptr Prediction direction for use in AC prediction
 */
static inline int vc1_i_pred_dc(MpegEncContext *s, int overlap, int pq, int n,
                              int16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred, scale;
    int16_t *dc_val;
    static const uint16_t dcpred[32] = {
    -1, 1024,  512,  341,  256,  205,  171,  146,  128,
         114,  102,   93,   85,   79,   73,   68,   64,
          60,   57,   54,   51,   49,   47,   45,   43,
          41,   39,   38,   37,   35,   34,   33
    };

    /* find prediction - wmv3_dc_scale always used here in fact */
    if (n < 4)     scale = s->y_dc_scale;
    else           scale = s->c_dc_scale;

    wrap = s->block_wrap[n];
    dc_val= s->dc_val[0] + s->block_index[n];

    /* B A
     * C X
     */
    c = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    a = dc_val[ - wrap];

    if (pq < 9 || !overlap)
    {
        /* Set outer values */
        if (s->first_slice_line && (n!=2 && n!=3)) b=a=dcpred[scale];
        if (s->mb_x == 0 && (n!=1 && n!=3)) b=c=dcpred[scale];
    }
    else
    {
        /* Set outer values */
        if (s->first_slice_line && (n!=2 && n!=3)) b=a=0;
        if (s->mb_x == 0 && (n!=1 && n!=3)) b=c=0;
    }

    if (abs(a - b) <= abs(b - c)) {
        pred = c;
        *dir_ptr = 1;//left
    } else {
        pred = a;
        *dir_ptr = 0;//top
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}


/** Get predicted DC value
 * prediction dir: left=0, top=1
 * @param s MpegEncContext
 * @param overlap flag indicating that overlap filtering is used
 * @param pq integer part of picture quantizer
 * @param[in] n block index in the current MB
 * @param a_avail flag indicating top block availability
 * @param c_avail flag indicating left block availability
 * @param dc_val_ptr Pointer to DC predictor
 * @param dir_ptr Prediction direction for use in AC prediction
 */
static inline int vc1_pred_dc(MpegEncContext *s, int overlap, int pq, int n,
                              int a_avail, int c_avail,
                              int16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred;
    int16_t *dc_val;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int q1, q2 = 0;

    wrap = s->block_wrap[n];
    dc_val= s->dc_val[0] + s->block_index[n];

    /* B A
     * C X
     */
    c = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    a = dc_val[ - wrap];
    /* scale predictors if needed */
    q1 = s->current_picture.qscale_table[mb_pos];
    if(c_avail && (n!= 1 && n!=3)) {
        q2 = s->current_picture.qscale_table[mb_pos - 1];
        if(q2 && q2 != q1)
            c = (c * s->y_dc_scale_table[q2] * ff_vc1_dqscale[s->y_dc_scale_table[q1] - 1] + 0x20000) >> 18;
    }
    if(a_avail && (n!= 2 && n!=3)) {
        q2 = s->current_picture.qscale_table[mb_pos - s->mb_stride];
        if(q2 && q2 != q1)
            a = (a * s->y_dc_scale_table[q2] * ff_vc1_dqscale[s->y_dc_scale_table[q1] - 1] + 0x20000) >> 18;
    }
    if(a_avail && c_avail && (n!=3)) {
        int off = mb_pos;
        if(n != 1) off--;
        if(n != 2) off -= s->mb_stride;
        q2 = s->current_picture.qscale_table[off];
        if(q2 && q2 != q1)
            b = (b * s->y_dc_scale_table[q2] * ff_vc1_dqscale[s->y_dc_scale_table[q1] - 1] + 0x20000) >> 18;
    }

    if(a_avail && c_avail) {
        if(abs(a - b) <= abs(b - c)) {
            pred = c;
            *dir_ptr = 1;//left
        } else {
            pred = a;
            *dir_ptr = 0;//top
        }
    } else if(a_avail) {
        pred = a;
        *dir_ptr = 0;//top
    } else if(c_avail) {
        pred = c;
        *dir_ptr = 1;//left
    } else {
        pred = 0;
        *dir_ptr = 1;//left
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}

/** @} */ // Block group

/**
 * @defgroup vc1_std_mb VC1 Macroblock-level functions in Simple/Main Profiles
 * @see 7.1.4, p91 and 8.1.1.7, p(1)04
 * @{
 */

static inline int vc1_coded_block_pred(MpegEncContext * s, int n, uint8_t **coded_block_ptr)
{
    int xy, wrap, pred, a, b, c;

    xy = s->block_index[n];
    wrap = s->b8_stride;

    /* B C
     * A X
     */
    a = s->coded_block[xy - 1       ];
    b = s->coded_block[xy - 1 - wrap];
    c = s->coded_block[xy     - wrap];

    if (b == c) {
        pred = a;
    } else {
        pred = c;
    }

    /* store value */
    *coded_block_ptr = &s->coded_block[xy];

    return pred;
}

/**
 * Decode one AC coefficient
 * @param v The VC1 context
 * @param last Last coefficient
 * @param skip How much zero coefficients to skip
 * @param value Decoded AC coefficient value
 * @param codingset set of VLC to decode data
 * @see 8.1.3.4
 */
static void vc1_decode_ac_coeff(VC1Context *v, int *last, int *skip, int *value, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    int index, escape, run = 0, level = 0, lst = 0;

    index = get_vlc2(gb, ff_vc1_ac_coeff_table[codingset].table, AC_VLC_BITS, 3);
    if (index != vc1_ac_sizes[codingset] - 1) {
        run = vc1_index_decode_table[codingset][index][0];
        level = vc1_index_decode_table[codingset][index][1];
        lst = index >= vc1_last_decode_table[codingset] || get_bits_left(gb) < 0;
        if(get_bits1(gb))
            level = -level;
    } else {
        escape = decode210(gb);
        if (escape != 2) {
            index = get_vlc2(gb, ff_vc1_ac_coeff_table[codingset].table, AC_VLC_BITS, 3);
            run = vc1_index_decode_table[codingset][index][0];
            level = vc1_index_decode_table[codingset][index][1];
            lst = index >= vc1_last_decode_table[codingset];
            if(escape == 0) {
                if(lst)
                    level += vc1_last_delta_level_table[codingset][run];
                else
                    level += vc1_delta_level_table[codingset][run];
            } else {
                if(lst)
                    run += vc1_last_delta_run_table[codingset][level] + 1;
                else
                    run += vc1_delta_run_table[codingset][level] + 1;
            }
            if(get_bits1(gb))
                level = -level;
        } else {
            int sign;
            lst = get_bits1(gb);
            if(v->s.esc3_level_length == 0) {
                if(v->pq < 8 || v->dquantfrm) { // table 59
                    v->s.esc3_level_length = get_bits(gb, 3);
                    if(!v->s.esc3_level_length)
                        v->s.esc3_level_length = get_bits(gb, 2) + 8;
                } else { //table 60
                    v->s.esc3_level_length = get_unary(gb, 1, 6) + 2;
                }
                v->s.esc3_run_length = 3 + get_bits(gb, 2);
            }
            run = get_bits(gb, v->s.esc3_run_length);
            sign = get_bits1(gb);
            level = get_bits(gb, v->s.esc3_level_length);
            if(sign)
                level = -level;
        }
    }

    *last = lst;
    *skip = run;
    *value = level;
}

/** Decode intra block in intra frames - should be faster than decode_intra_block
 * @param v VC1Context
 * @param block block to decode
 * @param[in] n subblock index
 * @param coded are AC coeffs present or not
 * @param codingset set of VLC to decode data
 */
static int vc1_decode_i_block(VC1Context *v, DCTELEM block[64], int n, int coded, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;

    /* Get DC differential */
    if (n < 4) {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    } else {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    }
    if (dcdiff < 0){
        av_log(s->avctx, AV_LOG_ERROR, "Illegal DC VLC\n");
        return -1;
    }
    if (dcdiff)
    {
        if (dcdiff == 119 /* ESC index value */)
        {
            /* TODO: Optimize */
            if (v->pq == 1) dcdiff = get_bits(gb, 10);
            else if (v->pq == 2) dcdiff = get_bits(gb, 9);
            else dcdiff = get_bits(gb, 8);
        }
        else
        {
            if (v->pq == 1)
                dcdiff = (dcdiff<<2) + get_bits(gb, 2) - 3;
            else if (v->pq == 2)
                dcdiff = (dcdiff<<1) + get_bits1(gb)   - 1;
        }
        if (get_bits1(gb))
            dcdiff = -dcdiff;
    }

    /* Prediction */
    dcdiff += vc1_i_pred_dc(&v->s, v->overlap, v->pq, n, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */
    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }
    /* Skip ? */
    if (!coded) {
        goto not_coded;
    }

    //AC Decoding
    i = 1;

    {
        int last = 0, skip, value;
        const int8_t *zz_table;
        int scale;
        int k;

        scale = v->pq * 2 + v->halfpq;

        if(v->s.ac_pred) {
            if(!dc_pred_dir)
                zz_table = wmv1_scantable[2];
            else
                zz_table = wmv1_scantable[3];
        } else
            zz_table = wmv1_scantable[1];

        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;
        if(dc_pred_dir) //left
            ac_val -= 16;
        else //top
            ac_val -= 16 * s->block_wrap[n];

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if(i > 63)
                break;
            block[zz_table[i++]] = value;
        }

        /* apply AC prediction if needed */
        if(s->ac_pred) {
            if(dc_pred_dir) { //left
                for(k = 1; k < 8; k++)
                    block[k << 3] += ac_val[k];
            } else { //top
                for(k = 1; k < 8; k++)
                    block[k] += ac_val[k + 8];
            }
        }
        /* save AC coeffs for further prediction */
        for(k = 1; k < 8; k++) {
            ac_val2[k] = block[k << 3];
            ac_val2[k + 8] = block[k];
        }

        /* scale AC coeffs */
        for(k = 1; k < 64; k++)
            if(block[k]) {
                block[k] *= scale;
                if(!v->pquantizer)
                    block[k] += (block[k] < 0) ? -v->pq : v->pq;
            }

        if(s->ac_pred) i = 63;
    }

not_coded:
    if(!coded) {
        int k, scale;
        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;

        i = 0;
        scale = v->pq * 2 + v->halfpq;
        memset(ac_val2, 0, 16 * 2);
        if(dc_pred_dir) {//left
            ac_val -= 16;
            if(s->ac_pred)
                memcpy(ac_val2, ac_val, 8 * 2);
        } else {//top
            ac_val -= 16 * s->block_wrap[n];
            if(s->ac_pred)
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
        }

        /* apply AC prediction if needed */
        if(s->ac_pred) {
            if(dc_pred_dir) { //left
                for(k = 1; k < 8; k++) {
                    block[k << 3] = ac_val[k] * scale;
                    if(!v->pquantizer && block[k << 3])
                        block[k << 3] += (block[k << 3] < 0) ? -v->pq : v->pq;
                }
            } else { //top
                for(k = 1; k < 8; k++) {
                    block[k] = ac_val[k + 8] * scale;
                    if(!v->pquantizer && block[k])
                        block[k] += (block[k] < 0) ? -v->pq : v->pq;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode intra block in intra frames - should be faster than decode_intra_block
 * @param v VC1Context
 * @param block block to decode
 * @param[in] n subblock number
 * @param coded are AC coeffs present or not
 * @param codingset set of VLC to decode data
 * @param mquant quantizer value for this macroblock
 */
static int vc1_decode_i_block_adv(VC1Context *v, DCTELEM block[64], int n, int coded, int codingset, int mquant)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;
    int a_avail = v->a_avail, c_avail = v->c_avail;
    int use_pred = s->ac_pred;
    int scale;
    int q1, q2 = 0;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;

    /* Get DC differential */
    if (n < 4) {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    } else {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    }
    if (dcdiff < 0){
        av_log(s->avctx, AV_LOG_ERROR, "Illegal DC VLC\n");
        return -1;
    }
    if (dcdiff)
    {
        if (dcdiff == 119 /* ESC index value */)
        {
            /* TODO: Optimize */
            if (mquant == 1) dcdiff = get_bits(gb, 10);
            else if (mquant == 2) dcdiff = get_bits(gb, 9);
            else dcdiff = get_bits(gb, 8);
        }
        else
        {
            if (mquant == 1)
                dcdiff = (dcdiff<<2) + get_bits(gb, 2) - 3;
            else if (mquant == 2)
                dcdiff = (dcdiff<<1) + get_bits1(gb)   - 1;
        }
        if (get_bits1(gb))
            dcdiff = -dcdiff;
    }

    /* Prediction */
    dcdiff += vc1_pred_dc(&v->s, v->overlap, mquant, n, v->a_avail, v->c_avail, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */
    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }

    //AC Decoding
    i = 1;

    /* check if AC is needed at all */
    if(!a_avail && !c_avail) use_pred = 0;
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
    ac_val2 = ac_val;

    scale = mquant * 2 + ((mquant == v->pq) ? v->halfpq : 0);

    if(dc_pred_dir) //left
        ac_val -= 16;
    else //top
        ac_val -= 16 * s->block_wrap[n];

    q1 = s->current_picture.qscale_table[mb_pos];
    if(dc_pred_dir && c_avail && mb_pos) q2 = s->current_picture.qscale_table[mb_pos - 1];
    if(!dc_pred_dir && a_avail && mb_pos >= s->mb_stride) q2 = s->current_picture.qscale_table[mb_pos - s->mb_stride];
    if(dc_pred_dir && n==1) q2 = q1;
    if(!dc_pred_dir && n==2) q2 = q1;
    if(n==3) q2 = q1;

    if(coded) {
        int last = 0, skip, value;
        const int8_t *zz_table;
        int k;

        if(v->s.ac_pred) {
            if(!dc_pred_dir)
                zz_table = wmv1_scantable[2];
            else
                zz_table = wmv1_scantable[3];
        } else
            zz_table = wmv1_scantable[1];

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if(i > 63)
                break;
            block[zz_table[i++]] = value;
        }

        /* apply AC prediction if needed */
        if(use_pred) {
            /* scale predictors if needed*/
            if(q2 && q1!=q2) {
                q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;

                if(dc_pred_dir) { //left
                    for(k = 1; k < 8; k++)
                        block[k << 3] += (ac_val[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                } else { //top
                    for(k = 1; k < 8; k++)
                        block[k] += (ac_val[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            } else {
                if(dc_pred_dir) { //left
                    for(k = 1; k < 8; k++)
                        block[k << 3] += ac_val[k];
                } else { //top
                    for(k = 1; k < 8; k++)
                        block[k] += ac_val[k + 8];
                }
            }
        }
        /* save AC coeffs for further prediction */
        for(k = 1; k < 8; k++) {
            ac_val2[k] = block[k << 3];
            ac_val2[k + 8] = block[k];
        }

        /* scale AC coeffs */
        for(k = 1; k < 64; k++)
            if(block[k]) {
                block[k] *= scale;
                if(!v->pquantizer)
                    block[k] += (block[k] < 0) ? -mquant : mquant;
            }

        if(use_pred) i = 63;
    } else { // no AC coeffs
        int k;

        memset(ac_val2, 0, 16 * 2);
        if(dc_pred_dir) {//left
            if(use_pred) {
                memcpy(ac_val2, ac_val, 8 * 2);
                if(q2 && q1!=q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    for(k = 1; k < 8; k++)
                        ac_val2[k] = (ac_val2[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        } else {//top
            if(use_pred) {
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
                if(q2 && q1!=q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    for(k = 1; k < 8; k++)
                        ac_val2[k + 8] = (ac_val2[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        }

        /* apply AC prediction if needed */
        if(use_pred) {
            if(dc_pred_dir) { //left
                for(k = 1; k < 8; k++) {
                    block[k << 3] = ac_val2[k] * scale;
                    if(!v->pquantizer && block[k << 3])
                        block[k << 3] += (block[k << 3] < 0) ? -mquant : mquant;
                }
            } else { //top
                for(k = 1; k < 8; k++) {
                    block[k] = ac_val2[k + 8] * scale;
                    if(!v->pquantizer && block[k])
                        block[k] += (block[k] < 0) ? -mquant : mquant;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode intra block in inter frames - more generic version than vc1_decode_i_block
 * @param v VC1Context
 * @param block block to decode
 * @param[in] n subblock index
 * @param coded are AC coeffs present or not
 * @param mquant block quantizer
 * @param codingset set of VLC to decode data
 */
static int vc1_decode_intra_block(VC1Context *v, DCTELEM block[64], int n, int coded, int mquant, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int a_avail = v->a_avail, c_avail = v->c_avail;
    int use_pred = s->ac_pred;
    int scale;
    int q1, q2 = 0;

    s->dsp.clear_block(block);

    /* XXX: Guard against dumb values of mquant */
    mquant = (mquant < 1) ? 0 : ( (mquant>31) ? 31 : mquant );

    /* Set DC scale - y and c use the same */
    s->y_dc_scale = s->y_dc_scale_table[mquant];
    s->c_dc_scale = s->c_dc_scale_table[mquant];

    /* Get DC differential */
    if (n < 4) {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    } else {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    }
    if (dcdiff < 0){
        av_log(s->avctx, AV_LOG_ERROR, "Illegal DC VLC\n");
        return -1;
    }
    if (dcdiff)
    {
        if (dcdiff == 119 /* ESC index value */)
        {
            /* TODO: Optimize */
            if (mquant == 1) dcdiff = get_bits(gb, 10);
            else if (mquant == 2) dcdiff = get_bits(gb, 9);
            else dcdiff = get_bits(gb, 8);
        }
        else
        {
            if (mquant == 1)
                dcdiff = (dcdiff<<2) + get_bits(gb, 2) - 3;
            else if (mquant == 2)
                dcdiff = (dcdiff<<1) + get_bits1(gb)   - 1;
        }
        if (get_bits1(gb))
            dcdiff = -dcdiff;
    }

    /* Prediction */
    dcdiff += vc1_pred_dc(&v->s, v->overlap, mquant, n, a_avail, c_avail, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */

    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }

    //AC Decoding
    i = 1;

    /* check if AC is needed at all and adjust direction if needed */
    if(!a_avail) dc_pred_dir = 1;
    if(!c_avail) dc_pred_dir = 0;
    if(!a_avail && !c_avail) use_pred = 0;
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
    ac_val2 = ac_val;

    scale = mquant * 2 + v->halfpq;

    if(dc_pred_dir) //left
        ac_val -= 16;
    else //top
        ac_val -= 16 * s->block_wrap[n];

    q1 = s->current_picture.qscale_table[mb_pos];
    if(dc_pred_dir && c_avail && mb_pos) q2 = s->current_picture.qscale_table[mb_pos - 1];
    if(!dc_pred_dir && a_avail && mb_pos >= s->mb_stride) q2 = s->current_picture.qscale_table[mb_pos - s->mb_stride];
    if(dc_pred_dir && n==1) q2 = q1;
    if(!dc_pred_dir && n==2) q2 = q1;
    if(n==3) q2 = q1;

    if(coded) {
        int last = 0, skip, value;
        const int8_t *zz_table;
        int k;

        zz_table = wmv1_scantable[0];

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if(i > 63)
                break;
            block[zz_table[i++]] = value;
        }

        /* apply AC prediction if needed */
        if(use_pred) {
            /* scale predictors if needed*/
            if(q2 && q1!=q2) {
                q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;

                if(dc_pred_dir) { //left
                    for(k = 1; k < 8; k++)
                        block[k << 3] += (ac_val[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                } else { //top
                    for(k = 1; k < 8; k++)
                        block[k] += (ac_val[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            } else {
                if(dc_pred_dir) { //left
                    for(k = 1; k < 8; k++)
                        block[k << 3] += ac_val[k];
                } else { //top
                    for(k = 1; k < 8; k++)
                        block[k] += ac_val[k + 8];
                }
            }
        }
        /* save AC coeffs for further prediction */
        for(k = 1; k < 8; k++) {
            ac_val2[k] = block[k << 3];
            ac_val2[k + 8] = block[k];
        }

        /* scale AC coeffs */
        for(k = 1; k < 64; k++)
            if(block[k]) {
                block[k] *= scale;
                if(!v->pquantizer)
                    block[k] += (block[k] < 0) ? -mquant : mquant;
            }

        if(use_pred) i = 63;
    } else { // no AC coeffs
        int k;

        memset(ac_val2, 0, 16 * 2);
        if(dc_pred_dir) {//left
            if(use_pred) {
                memcpy(ac_val2, ac_val, 8 * 2);
                if(q2 && q1!=q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    for(k = 1; k < 8; k++)
                        ac_val2[k] = (ac_val2[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        } else {//top
            if(use_pred) {
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
                if(q2 && q1!=q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    for(k = 1; k < 8; k++)
                        ac_val2[k + 8] = (ac_val2[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        }

        /* apply AC prediction if needed */
        if(use_pred) {
            if(dc_pred_dir) { //left
                for(k = 1; k < 8; k++) {
                    block[k << 3] = ac_val2[k] * scale;
                    if(!v->pquantizer && block[k << 3])
                        block[k << 3] += (block[k << 3] < 0) ? -mquant : mquant;
                }
            } else { //top
                for(k = 1; k < 8; k++) {
                    block[k] = ac_val2[k + 8] * scale;
                    if(!v->pquantizer && block[k])
                        block[k] += (block[k] < 0) ? -mquant : mquant;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode P block
 */
static int vc1_decode_p_block(VC1Context *v, DCTELEM block[64], int n, int mquant, int ttmb, int first_block,
                              uint8_t *dst, int linesize, int skip_block, int apply_filter, int cbp_top, int cbp_left)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int subblkpat = 0;
    int scale, off, idx, last, skip, value;
    int ttblk = ttmb & 7;
    int pat = 0;

    s->dsp.clear_block(block);

    if(ttmb == -1) {
        ttblk = ff_vc1_ttblk_to_tt[v->tt_index][get_vlc2(gb, ff_vc1_ttblk_vlc[v->tt_index].table, VC1_TTBLK_VLC_BITS, 1)];
    }
    if(ttblk == TT_4X4) {
        subblkpat = ~(get_vlc2(gb, ff_vc1_subblkpat_vlc[v->tt_index].table, VC1_SUBBLKPAT_VLC_BITS, 1) + 1);
    }
    if((ttblk != TT_8X8 && ttblk != TT_4X4) && (v->ttmbf || (ttmb != -1 && (ttmb & 8) && !first_block))) {
        subblkpat = decode012(gb);
        if(subblkpat) subblkpat ^= 3; //swap decoded pattern bits
        if(ttblk == TT_8X4_TOP || ttblk == TT_8X4_BOTTOM) ttblk = TT_8X4;
        if(ttblk == TT_4X8_RIGHT || ttblk == TT_4X8_LEFT) ttblk = TT_4X8;
    }
    scale = 2 * mquant + ((v->pq == mquant) ? v->halfpq : 0);

    // convert transforms like 8X4_TOP to generic TT and SUBBLKPAT
    if(ttblk == TT_8X4_TOP || ttblk == TT_8X4_BOTTOM) {
        subblkpat = 2 - (ttblk == TT_8X4_TOP);
        ttblk = TT_8X4;
    }
    if(ttblk == TT_4X8_RIGHT || ttblk == TT_4X8_LEFT) {
        subblkpat = 2 - (ttblk == TT_4X8_LEFT);
        ttblk = TT_4X8;
    }
    switch(ttblk) {
    case TT_8X8:
        pat = 0xF;
        i = 0;
        last = 0;
        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
            i += skip;
            if(i > 63)
                break;
            idx = wmv1_scantable[0][i++];
            block[idx] = value * scale;
            if(!v->pquantizer)
                block[idx] += (block[idx] < 0) ? -mquant : mquant;
        }
        if(!skip_block){
            if(i==1)
                s->dsp.vc1_inv_trans_8x8_dc(dst, linesize, block);
            else{
                s->dsp.vc1_inv_trans_8x8(block);
                s->dsp.add_pixels_clamped(block, dst, linesize);
            }
            if(apply_filter && cbp_top  & 0xC)
                s->dsp.vc1_v_loop_filter8(dst, linesize, v->pq);
            if(apply_filter && cbp_left & 0xA)
                s->dsp.vc1_h_loop_filter8(dst, linesize, v->pq);
        }
        break;
    case TT_4X4:
        pat = ~subblkpat & 0xF;
        for(j = 0; j < 4; j++) {
            last = subblkpat & (1 << (3 - j));
            i = 0;
            off = (j & 1) * 4 + (j & 2) * 16;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if(i > 15)
                    break;
                idx = ff_vc1_simple_progressive_4x4_zz[i++];
                block[idx + off] = value * scale;
                if(!v->pquantizer)
                    block[idx + off] += (block[idx + off] < 0) ? -mquant : mquant;
            }
            if(!(subblkpat & (1 << (3 - j))) && !skip_block){
                if(i==1)
                    s->dsp.vc1_inv_trans_4x4_dc(dst + (j&1)*4 + (j&2)*2*linesize, linesize, block + off);
                else
                    s->dsp.vc1_inv_trans_4x4(dst + (j&1)*4 + (j&2)*2*linesize, linesize, block + off);
                if(apply_filter && (j&2 ? pat & (1<<(j-2)) : (cbp_top & (1 << (j + 2)))))
                    s->dsp.vc1_v_loop_filter4(dst + (j&1)*4 + (j&2)*2*linesize, linesize, v->pq);
                if(apply_filter && (j&1 ? pat & (1<<(j-1)) : (cbp_left & (1 << (j + 1)))))
                    s->dsp.vc1_h_loop_filter4(dst + (j&1)*4 + (j&2)*2*linesize, linesize, v->pq);
            }
        }
        break;
    case TT_8X4:
        pat = ~((subblkpat & 2)*6 + (subblkpat & 1)*3) & 0xF;
        for(j = 0; j < 2; j++) {
            last = subblkpat & (1 << (1 - j));
            i = 0;
            off = j * 32;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if(i > 31)
                    break;
                idx = v->zz_8x4[i++]+off;
                block[idx] = value * scale;
                if(!v->pquantizer)
                    block[idx] += (block[idx] < 0) ? -mquant : mquant;
            }
            if(!(subblkpat & (1 << (1 - j))) && !skip_block){
                if(i==1)
                    s->dsp.vc1_inv_trans_8x4_dc(dst + j*4*linesize, linesize, block + off);
                else
                    s->dsp.vc1_inv_trans_8x4(dst + j*4*linesize, linesize, block + off);
                if(apply_filter && j ? pat & 0x3 : (cbp_top & 0xC))
                    s->dsp.vc1_v_loop_filter8(dst + j*4*linesize, linesize, v->pq);
                if(apply_filter && cbp_left & (2 << j))
                    s->dsp.vc1_h_loop_filter4(dst + j*4*linesize, linesize, v->pq);
            }
        }
        break;
    case TT_4X8:
        pat = ~(subblkpat*5) & 0xF;
        for(j = 0; j < 2; j++) {
            last = subblkpat & (1 << (1 - j));
            i = 0;
            off = j * 4;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if(i > 31)
                    break;
                idx = v->zz_4x8[i++]+off;
                block[idx] = value * scale;
                if(!v->pquantizer)
                    block[idx] += (block[idx] < 0) ? -mquant : mquant;
            }
            if(!(subblkpat & (1 << (1 - j))) && !skip_block){
                if(i==1)
                    s->dsp.vc1_inv_trans_4x8_dc(dst + j*4, linesize, block + off);
                else
                    s->dsp.vc1_inv_trans_4x8(dst + j*4, linesize, block + off);
                if(apply_filter && cbp_top & (2 << j))
                    s->dsp.vc1_v_loop_filter4(dst + j*4, linesize, v->pq);
                if(apply_filter && j ? pat & 0x5 : (cbp_left & 0xA))
                    s->dsp.vc1_h_loop_filter8(dst + j*4, linesize, v->pq);
            }
        }
        break;
    }
    return pat;
}

/** @} */ // Macroblock group

static const int size_table  [6] = { 0, 2, 3, 4,  5,  8 };
static const int offset_table[6] = { 0, 1, 3, 7, 15, 31 };

/** Decode one P-frame MB (in Simple/Main profile)
 */
static int vc1_decode_p_mb(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp; /* cbp decoding stuff */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttfrm; /* MB Transform type */

    int mb_has_coeffs = 1; /* last_flag */
    int dmv_x, dmv_y; /* Differential MV components */
    int index, index1; /* LUT indexes */
    int val, sign; /* temp values */
    int first_block = 1;
    int dst_idx, off;
    int skipped, fourmv;
    int block_cbp = 0, pat;
    int apply_loop_filter;

    mquant = v->pq; /* Loosy initialization */

    if (v->mv_type_is_raw)
        fourmv = get_bits1(gb);
    else
        fourmv = v->mv_type_mb_plane[mb_pos];
    if (v->skip_is_raw)
        skipped = get_bits1(gb);
    else
        skipped = v->s.mbskip_table[mb_pos];

    apply_loop_filter = s->loop_filter && !(s->avctx->skip_loop_filter >= AVDISCARD_NONKEY);
    if (!fourmv) /* 1MV mode */
    {
        if (!skipped)
        {
            GET_MVDATA(dmv_x, dmv_y);

            if (s->mb_intra) {
                s->current_picture.motion_val[1][s->block_index[0]][0] = 0;
                s->current_picture.motion_val[1][s->block_index[0]][1] = 0;
            }
            s->current_picture.mb_type[mb_pos] = s->mb_intra ? MB_TYPE_INTRA : MB_TYPE_16x16;
            vc1_pred_mv(s, 0, dmv_x, dmv_y, 1, v->range_x, v->range_y, v->mb_type[0]);

            /* FIXME Set DC val for inter block ? */
            if (s->mb_intra && !mb_has_coeffs)
            {
                GET_MQUANT();
                s->ac_pred = get_bits1(gb);
                cbp = 0;
            }
            else if (mb_has_coeffs)
            {
                if (s->mb_intra) s->ac_pred = get_bits1(gb);
                cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
                GET_MQUANT();
            }
            else
            {
                mquant = v->pq;
                cbp = 0;
            }
            s->current_picture.qscale_table[mb_pos] = mquant;

            if (!v->ttmbf && !s->mb_intra && mb_has_coeffs)
                ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table,
                                VC1_TTMB_VLC_BITS, 2);
            if(!s->mb_intra) vc1_mc_1mv(v, 0);
            dst_idx = 0;
            for (i=0; i<6; i++)
            {
                s->dc_val[0][s->block_index[i]] = 0;
                dst_idx += i >> 2;
                val = ((cbp >> (5 - i)) & 1);
                off = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
                v->mb_type[0][s->block_index[i]] = s->mb_intra;
                if(s->mb_intra) {
                    /* check if prediction blocks A and C are available */
                    v->a_avail = v->c_avail = 0;
                    if(i == 2 || i == 3 || !s->first_slice_line)
                        v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
                    if(i == 1 || i == 3 || s->mb_x)
                        v->c_avail = v->mb_type[0][s->block_index[i] - 1];

                    vc1_decode_intra_block(v, s->block[i], i, val, mquant, (i&4)?v->codingset2:v->codingset);
                    if((i>3) && (s->flags & CODEC_FLAG_GRAY)) continue;
                    s->dsp.vc1_inv_trans_8x8(s->block[i]);
                    if(v->rangeredfrm) for(j = 0; j < 64; j++) s->block[i][j] <<= 1;
                    s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2));
                    if(v->pq >= 9 && v->overlap) {
                        if(v->c_avail)
                            s->dsp.vc1_h_overlap(s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2));
                        if(v->a_avail)
                            s->dsp.vc1_v_overlap(s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2));
                    }
                    if(apply_loop_filter && s->mb_x && s->mb_x != (s->mb_width - 1) && s->mb_y && s->mb_y != (s->mb_height - 1)){
                        int left_cbp, top_cbp;
                        if(i & 4){
                            left_cbp = v->cbp[s->mb_x - 1]            >> (i * 4);
                            top_cbp  = v->cbp[s->mb_x - s->mb_stride] >> (i * 4);
                        }else{
                            left_cbp = (i & 1) ? (cbp >> ((i-1)*4)) : (v->cbp[s->mb_x - 1]           >> ((i+1)*4));
                            top_cbp  = (i & 2) ? (cbp >> ((i-2)*4)) : (v->cbp[s->mb_x - s->mb_stride] >> ((i+2)*4));
                        }
                        if(left_cbp & 0xC)
                            s->dsp.vc1_v_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                        if(top_cbp  & 0xA)
                            s->dsp.vc1_h_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                    }
                    block_cbp |= 0xF << (i << 2);
                } else if(val) {
                    int left_cbp = 0, top_cbp = 0, filter = 0;
                    if(apply_loop_filter && s->mb_x && s->mb_x != (s->mb_width - 1) && s->mb_y && s->mb_y != (s->mb_height - 1)){
                        filter = 1;
                        if(i & 4){
                            left_cbp = v->cbp[s->mb_x - 1]            >> (i * 4);
                            top_cbp  = v->cbp[s->mb_x - s->mb_stride] >> (i * 4);
                        }else{
                            left_cbp = (i & 1) ? (cbp >> ((i-1)*4)) : (v->cbp[s->mb_x - 1]           >> ((i+1)*4));
                            top_cbp  = (i & 2) ? (cbp >> ((i-2)*4)) : (v->cbp[s->mb_x - s->mb_stride] >> ((i+2)*4));
                        }
                        if(left_cbp & 0xC)
                            s->dsp.vc1_v_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                        if(top_cbp  & 0xA)
                            s->dsp.vc1_h_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                    }
                    pat = vc1_decode_p_block(v, s->block[i], i, mquant, ttmb, first_block, s->dest[dst_idx] + off, (i&4)?s->uvlinesize:s->linesize, (i&4) && (s->flags & CODEC_FLAG_GRAY), filter, left_cbp, top_cbp);
                    block_cbp |= pat << (i << 2);
                    if(!v->ttmbf && ttmb < 8) ttmb = -1;
                    first_block = 0;
                }
            }
        }
        else //Skipped
        {
            s->mb_intra = 0;
            for(i = 0; i < 6; i++) {
                v->mb_type[0][s->block_index[i]] = 0;
                s->dc_val[0][s->block_index[i]] = 0;
            }
            s->current_picture.mb_type[mb_pos] = MB_TYPE_SKIP;
            s->current_picture.qscale_table[mb_pos] = 0;
            vc1_pred_mv(s, 0, 0, 0, 1, v->range_x, v->range_y, v->mb_type[0]);
            vc1_mc_1mv(v, 0);
            return 0;
        }
    } //1MV mode
    else //4MV mode
    {
        if (!skipped /* unskipped MB */)
        {
            int intra_count = 0, coded_inter = 0;
            int is_intra[6], is_coded[6];
            /* Get CBPCY */
            cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
            for (i=0; i<6; i++)
            {
                val = ((cbp >> (5 - i)) & 1);
                s->dc_val[0][s->block_index[i]] = 0;
                s->mb_intra = 0;
                if(i < 4) {
                    dmv_x = dmv_y = 0;
                    s->mb_intra = 0;
                    mb_has_coeffs = 0;
                    if(val) {
                        GET_MVDATA(dmv_x, dmv_y);
                    }
                    vc1_pred_mv(s, i, dmv_x, dmv_y, 0, v->range_x, v->range_y, v->mb_type[0]);
                    if(!s->mb_intra) vc1_mc_4mv_luma(v, i);
                    intra_count += s->mb_intra;
                    is_intra[i] = s->mb_intra;
                    is_coded[i] = mb_has_coeffs;
                }
                if(i&4){
                    is_intra[i] = (intra_count >= 3);
                    is_coded[i] = val;
                }
                if(i == 4) vc1_mc_4mv_chroma(v);
                v->mb_type[0][s->block_index[i]] = is_intra[i];
                if(!coded_inter) coded_inter = !is_intra[i] & is_coded[i];
            }
            // if there are no coded blocks then don't do anything more
            if(!intra_count && !coded_inter) return 0;
            dst_idx = 0;
            GET_MQUANT();
            s->current_picture.qscale_table[mb_pos] = mquant;
            /* test if block is intra and has pred */
            {
                int intrapred = 0;
                for(i=0; i<6; i++)
                    if(is_intra[i]) {
                        if(((!s->first_slice_line || (i==2 || i==3)) && v->mb_type[0][s->block_index[i] - s->block_wrap[i]])
                            || ((s->mb_x || (i==1 || i==3)) && v->mb_type[0][s->block_index[i] - 1])) {
                            intrapred = 1;
                            break;
                        }
                    }
                if(intrapred)s->ac_pred = get_bits1(gb);
                else s->ac_pred = 0;
            }
            if (!v->ttmbf && coded_inter)
                ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
            for (i=0; i<6; i++)
            {
                dst_idx += i >> 2;
                off = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
                s->mb_intra = is_intra[i];
                if (is_intra[i]) {
                    /* check if prediction blocks A and C are available */
                    v->a_avail = v->c_avail = 0;
                    if(i == 2 || i == 3 || !s->first_slice_line)
                        v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
                    if(i == 1 || i == 3 || s->mb_x)
                        v->c_avail = v->mb_type[0][s->block_index[i] - 1];

                    vc1_decode_intra_block(v, s->block[i], i, is_coded[i], mquant, (i&4)?v->codingset2:v->codingset);
                    if((i>3) && (s->flags & CODEC_FLAG_GRAY)) continue;
                    s->dsp.vc1_inv_trans_8x8(s->block[i]);
                    if(v->rangeredfrm) for(j = 0; j < 64; j++) s->block[i][j] <<= 1;
                    s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, (i&4)?s->uvlinesize:s->linesize);
                    if(v->pq >= 9 && v->overlap) {
                        if(v->c_avail)
                            s->dsp.vc1_h_overlap(s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2));
                        if(v->a_avail)
                            s->dsp.vc1_v_overlap(s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2));
                    }
                    if(v->s.loop_filter && s->mb_x && s->mb_x != (s->mb_width - 1) && s->mb_y && s->mb_y != (s->mb_height - 1)){
                        int left_cbp, top_cbp;
                        if(i & 4){
                            left_cbp = v->cbp[s->mb_x - 1]            >> (i * 4);
                            top_cbp  = v->cbp[s->mb_x - s->mb_stride] >> (i * 4);
                        }else{
                            left_cbp = (i & 1) ? (cbp >> ((i-1)*4)) : (v->cbp[s->mb_x - 1]           >> ((i+1)*4));
                            top_cbp  = (i & 2) ? (cbp >> ((i-2)*4)) : (v->cbp[s->mb_x - s->mb_stride] >> ((i+2)*4));
                        }
                        if(left_cbp & 0xC)
                            s->dsp.vc1_v_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                        if(top_cbp  & 0xA)
                            s->dsp.vc1_h_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                    }
                    block_cbp |= 0xF << (i << 2);
                } else if(is_coded[i]) {
                    int left_cbp = 0, top_cbp = 0, filter = 0;
                    if(v->s.loop_filter && s->mb_x && s->mb_x != (s->mb_width - 1) && s->mb_y && s->mb_y != (s->mb_height - 1)){
                        filter = 1;
                        if(i & 4){
                            left_cbp = v->cbp[s->mb_x - 1]            >> (i * 4);
                            top_cbp  = v->cbp[s->mb_x - s->mb_stride] >> (i * 4);
                        }else{
                            left_cbp = (i & 1) ? (cbp >> ((i-1)*4)) : (v->cbp[s->mb_x - 1]           >> ((i+1)*4));
                            top_cbp  = (i & 2) ? (cbp >> ((i-2)*4)) : (v->cbp[s->mb_x - s->mb_stride] >> ((i+2)*4));
                        }
                        if(left_cbp & 0xC)
                            s->dsp.vc1_v_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                        if(top_cbp  & 0xA)
                            s->dsp.vc1_h_loop_filter8(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize, v->pq);
                    }
                    pat = vc1_decode_p_block(v, s->block[i], i, mquant, ttmb, first_block, s->dest[dst_idx] + off, (i&4)?s->uvlinesize:s->linesize, (i&4) && (s->flags & CODEC_FLAG_GRAY), filter, left_cbp, top_cbp);
                    block_cbp |= pat << (i << 2);
                    if(!v->ttmbf && ttmb < 8) ttmb = -1;
                    first_block = 0;
                }
            }
            return 0;
        }
        else //Skipped MB
        {
            s->mb_intra = 0;
            s->current_picture.qscale_table[mb_pos] = 0;
            for (i=0; i<6; i++) {
                v->mb_type[0][s->block_index[i]] = 0;
                s->dc_val[0][s->block_index[i]] = 0;
            }
            for (i=0; i<4; i++)
            {
                vc1_pred_mv(s, i, 0, 0, 0, v->range_x, v->range_y, v->mb_type[0]);
                vc1_mc_4mv_luma(v, i);
            }
            vc1_mc_4mv_chroma(v);
            s->current_picture.qscale_table[mb_pos] = 0;
            return 0;
        }
    }
    v->cbp[s->mb_x] = block_cbp;

    /* Should never happen */
    return -1;
}

/** Decode one B-frame MB (in Main profile)
 */
static void vc1_decode_b_mb(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp = 0; /* cbp decoding stuff */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttfrm; /* MB Transform type */
    int mb_has_coeffs = 0; /* last_flag */
    int index, index1; /* LUT indexes */
    int val, sign; /* temp values */
    int first_block = 1;
    int dst_idx, off;
    int skipped, direct;
    int dmv_x[2], dmv_y[2];
    int bmvtype = BMV_TYPE_BACKWARD;

    mquant = v->pq; /* Loosy initialization */
    s->mb_intra = 0;

    if (v->dmb_is_raw)
        direct = get_bits1(gb);
    else
        direct = v->direct_mb_plane[mb_pos];
    if (v->skip_is_raw)
        skipped = get_bits1(gb);
    else
        skipped = v->s.mbskip_table[mb_pos];

    dmv_x[0] = dmv_x[1] = dmv_y[0] = dmv_y[1] = 0;
    for(i = 0; i < 6; i++) {
        v->mb_type[0][s->block_index[i]] = 0;
        s->dc_val[0][s->block_index[i]] = 0;
    }
    s->current_picture.qscale_table[mb_pos] = 0;

    if (!direct) {
        if (!skipped) {
            GET_MVDATA(dmv_x[0], dmv_y[0]);
            dmv_x[1] = dmv_x[0];
            dmv_y[1] = dmv_y[0];
        }
        if(skipped || !s->mb_intra) {
            bmvtype = decode012(gb);
            switch(bmvtype) {
            case 0:
                bmvtype = (v->bfraction >= (B_FRACTION_DEN/2)) ? BMV_TYPE_BACKWARD : BMV_TYPE_FORWARD;
                break;
            case 1:
                bmvtype = (v->bfraction >= (B_FRACTION_DEN/2)) ? BMV_TYPE_FORWARD : BMV_TYPE_BACKWARD;
                break;
            case 2:
                bmvtype = BMV_TYPE_INTERPOLATED;
                dmv_x[0] = dmv_y[0] = 0;
            }
        }
    }
    for(i = 0; i < 6; i++)
        v->mb_type[0][s->block_index[i]] = s->mb_intra;

    if (skipped) {
        if(direct) bmvtype = BMV_TYPE_INTERPOLATED;
        vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
        vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
        return;
    }
    if (direct) {
        cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
        GET_MQUANT();
        s->mb_intra = 0;
        s->current_picture.qscale_table[mb_pos] = mquant;
        if(!v->ttmbf)
            ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
        dmv_x[0] = dmv_y[0] = dmv_x[1] = dmv_y[1] = 0;
        vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
        vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
    } else {
        if(!mb_has_coeffs && !s->mb_intra) {
            /* no coded blocks - effectively skipped */
            vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
            vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
            return;
        }
        if(s->mb_intra && !mb_has_coeffs) {
            GET_MQUANT();
            s->current_picture.qscale_table[mb_pos] = mquant;
            s->ac_pred = get_bits1(gb);
            cbp = 0;
            vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
        } else {
            if(bmvtype == BMV_TYPE_INTERPOLATED) {
                GET_MVDATA(dmv_x[0], dmv_y[0]);
                if(!mb_has_coeffs) {
                    /* interpolated skipped block */
                    vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
                    vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
                    return;
                }
            }
            vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
            if(!s->mb_intra) {
                vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
            }
            if(s->mb_intra)
                s->ac_pred = get_bits1(gb);
            cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
            GET_MQUANT();
            s->current_picture.qscale_table[mb_pos] = mquant;
            if(!v->ttmbf && !s->mb_intra && mb_has_coeffs)
                ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
        }
    }
    dst_idx = 0;
    for (i=0; i<6; i++)
    {
        s->dc_val[0][s->block_index[i]] = 0;
        dst_idx += i >> 2;
        val = ((cbp >> (5 - i)) & 1);
        off = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
        v->mb_type[0][s->block_index[i]] = s->mb_intra;
        if(s->mb_intra) {
            /* check if prediction blocks A and C are available */
            v->a_avail = v->c_avail = 0;
            if(i == 2 || i == 3 || !s->first_slice_line)
                v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
            if(i == 1 || i == 3 || s->mb_x)
                v->c_avail = v->mb_type[0][s->block_index[i] - 1];

            vc1_decode_intra_block(v, s->block[i], i, val, mquant, (i&4)?v->codingset2:v->codingset);
            if((i>3) && (s->flags & CODEC_FLAG_GRAY)) continue;
            s->dsp.vc1_inv_trans_8x8(s->block[i]);
            if(v->rangeredfrm) for(j = 0; j < 64; j++) s->block[i][j] <<= 1;
            s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2));
        } else if(val) {
            vc1_decode_p_block(v, s->block[i], i, mquant, ttmb, first_block, s->dest[dst_idx] + off, (i&4)?s->uvlinesize:s->linesize, (i&4) && (s->flags & CODEC_FLAG_GRAY), 0, 0, 0);
            if(!v->ttmbf && ttmb < 8) ttmb = -1;
            first_block = 0;
        }
    }
}

/** Decode blocks of I-frame
 */
static void vc1_decode_i_blocks(VC1Context *v)
{
    int k, j;
    MpegEncContext *s = &v->s;
    int cbp, val;
    uint8_t *coded_val;
    int mb_pos;

    /* select codingmode used for VLC tables selection */
    switch(v->y_ac_table_index){
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch(v->c_ac_table_index){
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    /* Set DC scale - y and c use the same */
    s->y_dc_scale = s->y_dc_scale_table[v->pq];
    s->c_dc_scale = s->c_dc_scale_table[v->pq];

    //do frame decode
    s->mb_x = s->mb_y = 0;
    s->mb_intra = 1;
    s->first_slice_line = 1;
    for(s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for(; s->mb_x < s->mb_width; s->mb_x++) {
            ff_update_block_index(s);
            s->dsp.clear_blocks(s->block[0]);
            mb_pos = s->mb_x + s->mb_y * s->mb_width;
            s->current_picture.mb_type[mb_pos] = MB_TYPE_INTRA;
            s->current_picture.qscale_table[mb_pos] = v->pq;
            s->current_picture.motion_val[1][s->block_index[0]][0] = 0;
            s->current_picture.motion_val[1][s->block_index[0]][1] = 0;

            // do actual MB decoding and displaying
            cbp = get_vlc2(&v->s.gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
            v->s.ac_pred = get_bits1(&v->s.gb);

            for(k = 0; k < 6; k++) {
                val = ((cbp >> (5 - k)) & 1);

                if (k < 4) {
                    int pred = vc1_coded_block_pred(&v->s, k, &coded_val);
                    val = val ^ pred;
                    *coded_val = val;
                }
                cbp |= val << (5 - k);

                vc1_decode_i_block(v, s->block[k], k, val, (k<4)? v->codingset : v->codingset2);

                s->dsp.vc1_inv_trans_8x8(s->block[k]);
                if(v->pq >= 9 && v->overlap) {
                    for(j = 0; j < 64; j++) s->block[k][j] += 128;
                }
            }

            vc1_put_block(v, s->block);
            if(v->pq >= 9 && v->overlap) {
                if(s->mb_x) {
                    s->dsp.vc1_h_overlap(s->dest[0], s->linesize);
                    s->dsp.vc1_h_overlap(s->dest[0] + 8 * s->linesize, s->linesize);
                    if(!(s->flags & CODEC_FLAG_GRAY)) {
                        s->dsp.vc1_h_overlap(s->dest[1], s->uvlinesize);
                        s->dsp.vc1_h_overlap(s->dest[2], s->uvlinesize);
                    }
                }
                s->dsp.vc1_h_overlap(s->dest[0] + 8, s->linesize);
                s->dsp.vc1_h_overlap(s->dest[0] + 8 * s->linesize + 8, s->linesize);
                if(!s->first_slice_line) {
                    s->dsp.vc1_v_overlap(s->dest[0], s->linesize);
                    s->dsp.vc1_v_overlap(s->dest[0] + 8, s->linesize);
                    if(!(s->flags & CODEC_FLAG_GRAY)) {
                        s->dsp.vc1_v_overlap(s->dest[1], s->uvlinesize);
                        s->dsp.vc1_v_overlap(s->dest[2], s->uvlinesize);
                    }
                }
                s->dsp.vc1_v_overlap(s->dest[0] + 8 * s->linesize, s->linesize);
                s->dsp.vc1_v_overlap(s->dest[0] + 8 * s->linesize + 8, s->linesize);
            }
            if(v->s.loop_filter) vc1_loop_filter_iblk(s, v->pq);

            if(get_bits_count(&s->gb) > v->bits) {
                ff_er_add_slice(s, 0, 0, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END));
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i\n", get_bits_count(&s->gb), v->bits);
                return;
            }
        }
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
        s->first_slice_line = 0;
    }
    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, (AC_END|DC_END|MV_END));
}

/** Decode blocks of I-frame for advanced profile
 */
static void vc1_decode_i_blocks_adv(VC1Context *v)
{
    int k, j;
    MpegEncContext *s = &v->s;
    int cbp, val;
    uint8_t *coded_val;
    int mb_pos;
    int mquant = v->pq;
    int mqdiff;
    int overlap;
    GetBitContext *gb = &s->gb;

    /* select codingmode used for VLC tables selection */
    switch(v->y_ac_table_index){
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch(v->c_ac_table_index){
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    //do frame decode
    s->mb_x = s->mb_y = 0;
    s->mb_intra = 1;
    s->first_slice_line = 1;
    for(s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for(;s->mb_x < s->mb_width; s->mb_x++) {
            ff_update_block_index(s);
            s->dsp.clear_blocks(s->block[0]);
            mb_pos = s->mb_x + s->mb_y * s->mb_stride;
            s->current_picture.mb_type[mb_pos] = MB_TYPE_INTRA;
            s->current_picture.motion_val[1][s->block_index[0]][0] = 0;
            s->current_picture.motion_val[1][s->block_index[0]][1] = 0;

            // do actual MB decoding and displaying
            cbp = get_vlc2(&v->s.gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
            if(v->acpred_is_raw)
                v->s.ac_pred = get_bits1(&v->s.gb);
            else
                v->s.ac_pred = v->acpred_plane[mb_pos];

            if(v->condover == CONDOVER_SELECT) {
                if(v->overflg_is_raw)
                    overlap = get_bits1(&v->s.gb);
                else
                    overlap = v->over_flags_plane[mb_pos];
            } else
                overlap = (v->condover == CONDOVER_ALL);

            GET_MQUANT();

            s->current_picture.qscale_table[mb_pos] = mquant;
            /* Set DC scale - y and c use the same */
            s->y_dc_scale = s->y_dc_scale_table[mquant];
            s->c_dc_scale = s->c_dc_scale_table[mquant];

            for(k = 0; k < 6; k++) {
                val = ((cbp >> (5 - k)) & 1);

                if (k < 4) {
                    int pred = vc1_coded_block_pred(&v->s, k, &coded_val);
                    val = val ^ pred;
                    *coded_val = val;
                }
                cbp |= val << (5 - k);

                v->a_avail = !s->first_slice_line || (k==2 || k==3);
                v->c_avail = !!s->mb_x || (k==1 || k==3);

                vc1_decode_i_block_adv(v, s->block[k], k, val, (k<4)? v->codingset : v->codingset2, mquant);

                s->dsp.vc1_inv_trans_8x8(s->block[k]);
                for(j = 0; j < 64; j++) s->block[k][j] += 128;
            }

            vc1_put_block(v, s->block);
            if(overlap) {
                if(s->mb_x) {
                    s->dsp.vc1_h_overlap(s->dest[0], s->linesize);
                    s->dsp.vc1_h_overlap(s->dest[0] + 8 * s->linesize, s->linesize);
                    if(!(s->flags & CODEC_FLAG_GRAY)) {
                        s->dsp.vc1_h_overlap(s->dest[1], s->uvlinesize);
                        s->dsp.vc1_h_overlap(s->dest[2], s->uvlinesize);
                    }
                }
                s->dsp.vc1_h_overlap(s->dest[0] + 8, s->linesize);
                s->dsp.vc1_h_overlap(s->dest[0] + 8 * s->linesize + 8, s->linesize);
                if(!s->first_slice_line) {
                    s->dsp.vc1_v_overlap(s->dest[0], s->linesize);
                    s->dsp.vc1_v_overlap(s->dest[0] + 8, s->linesize);
                    if(!(s->flags & CODEC_FLAG_GRAY)) {
                        s->dsp.vc1_v_overlap(s->dest[1], s->uvlinesize);
                        s->dsp.vc1_v_overlap(s->dest[2], s->uvlinesize);
                    }
                }
                s->dsp.vc1_v_overlap(s->dest[0] + 8 * s->linesize, s->linesize);
                s->dsp.vc1_v_overlap(s->dest[0] + 8 * s->linesize + 8, s->linesize);
            }
            if(v->s.loop_filter) vc1_loop_filter_iblk(s, v->pq);

            if(get_bits_count(&s->gb) > v->bits) {
                ff_er_add_slice(s, 0, 0, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END));
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i\n", get_bits_count(&s->gb), v->bits);
                return;
            }
        }
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
        s->first_slice_line = 0;
    }
    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, (AC_END|DC_END|MV_END));
}

static void vc1_decode_p_blocks(VC1Context *v)
{
    MpegEncContext *s = &v->s;

    /* select codingmode used for VLC tables selection */
    switch(v->c_ac_table_index){
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch(v->c_ac_table_index){
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    s->first_slice_line = 1;
    memset(v->cbp_base, 0, sizeof(v->cbp_base[0])*2*s->mb_stride);
    for(s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for(; s->mb_x < s->mb_width; s->mb_x++) {
            ff_update_block_index(s);

            vc1_decode_p_mb(v);
            if(get_bits_count(&s->gb) > v->bits || get_bits_count(&s->gb) < 0) {
                ff_er_add_slice(s, 0, 0, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END));
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i at %ix%i\n", get_bits_count(&s->gb), v->bits,s->mb_x,s->mb_y);
                return;
            }
        }
        memmove(v->cbp_base, v->cbp, sizeof(v->cbp_base[0])*s->mb_stride);
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
        s->first_slice_line = 0;
    }
    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, (AC_END|DC_END|MV_END));
}

static void vc1_decode_b_blocks(VC1Context *v)
{
    MpegEncContext *s = &v->s;

    /* select codingmode used for VLC tables selection */
    switch(v->c_ac_table_index){
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch(v->c_ac_table_index){
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    s->first_slice_line = 1;
    for(s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for(; s->mb_x < s->mb_width; s->mb_x++) {
            ff_update_block_index(s);

            vc1_decode_b_mb(v);
            if(get_bits_count(&s->gb) > v->bits || get_bits_count(&s->gb) < 0) {
                ff_er_add_slice(s, 0, 0, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END));
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i at %ix%i\n", get_bits_count(&s->gb), v->bits,s->mb_x,s->mb_y);
                return;
            }
            if(v->s.loop_filter) vc1_loop_filter_iblk(s, v->pq);
        }
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
        s->first_slice_line = 0;
    }
    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, (AC_END|DC_END|MV_END));
}

static void vc1_decode_skip_blocks(VC1Context *v)
{
    MpegEncContext *s = &v->s;

    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, (AC_END|DC_END|MV_END));
    s->first_slice_line = 1;
    for(s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        ff_update_block_index(s);
        memcpy(s->dest[0], s->last_picture.data[0] + s->mb_y * 16 * s->linesize, s->linesize * 16);
        memcpy(s->dest[1], s->last_picture.data[1] + s->mb_y * 8 * s->uvlinesize, s->uvlinesize * 8);
        memcpy(s->dest[2], s->last_picture.data[2] + s->mb_y * 8 * s->uvlinesize, s->uvlinesize * 8);
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
        s->first_slice_line = 0;
    }
    s->pict_type = FF_P_TYPE;
}

static void vc1_decode_blocks(VC1Context *v)
{

    v->s.esc3_level_length = 0;
    if(v->x8_type){
        ff_intrax8_decode_picture(&v->x8, 2*v->pq+v->halfpq, v->pq*(!v->pquantizer) );
    }else{

        switch(v->s.pict_type) {
        case FF_I_TYPE:
            if(v->profile == PROFILE_ADVANCED)
                vc1_decode_i_blocks_adv(v);
            else
                vc1_decode_i_blocks(v);
            break;
        case FF_P_TYPE:
            if(v->p_frame_skipped)
                vc1_decode_skip_blocks(v);
            else
                vc1_decode_p_blocks(v);
            break;
        case FF_B_TYPE:
            if(v->bi_type){
                if(v->profile == PROFILE_ADVANCED)
                    vc1_decode_i_blocks_adv(v);
                else
                    vc1_decode_i_blocks(v);
            }else
                vc1_decode_b_blocks(v);
            break;
        }
    }
}

/** Initialize a VC1/WMV3 decoder
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 * @todo TODO: Decypher remaining bits in extra_data
 */
static av_cold int vc1_decode_init(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    GetBitContext gb;

    if (!avctx->extradata_size || !avctx->extradata) return -1;
    if (!(avctx->flags & CODEC_FLAG_GRAY))
        avctx->pix_fmt = avctx->get_format(avctx, avctx->codec->pix_fmts);
    else
        avctx->pix_fmt = PIX_FMT_GRAY8;
    avctx->hwaccel = ff_find_hwaccel(avctx->codec->id, avctx->pix_fmt);
    v->s.avctx = avctx;
    avctx->flags |= CODEC_FLAG_EMU_EDGE;
    v->s.flags |= CODEC_FLAG_EMU_EDGE;

    if(avctx->idct_algo==FF_IDCT_AUTO){
        avctx->idct_algo=FF_IDCT_WMV2;
    }

    if(ff_msmpeg4_decode_init(avctx) < 0)
        return -1;
    if (vc1_init_common(v) < 0) return -1;

    avctx->coded_width = avctx->width;
    avctx->coded_height = avctx->height;
    if (avctx->codec_id == CODEC_ID_WMV3)
    {
        int count = 0;

        // looks like WMV3 has a sequence header stored in the extradata
        // advanced sequence header may be before the first frame
        // the last byte of the extradata is a version number, 1 for the
        // samples we can decode

        init_get_bits(&gb, avctx->extradata, avctx->extradata_size*8);

        if (vc1_decode_sequence_header(avctx, v, &gb) < 0)
          return -1;

        count = avctx->extradata_size*8 - get_bits_count(&gb);
        if (count>0)
        {
            av_log(avctx, AV_LOG_INFO, "Extra data: %i bits left, value: %X\n",
                   count, get_bits(&gb, count));
        }
        else if (count < 0)
        {
            av_log(avctx, AV_LOG_INFO, "Read %i bits in overflow\n", -count);
        }
    } else { // VC1/WVC1
        const uint8_t *start = avctx->extradata;
        uint8_t *end = avctx->extradata + avctx->extradata_size;
        const uint8_t *next;
        int size, buf2_size;
        uint8_t *buf2 = NULL;
        int seq_initialized = 0, ep_initialized = 0;

        if(avctx->extradata_size < 16) {
            av_log(avctx, AV_LOG_ERROR, "Extradata size too small: %i\n", avctx->extradata_size);
            return -1;
        }

        buf2 = av_mallocz(avctx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        start = find_next_marker(start, end); // in WVC1 extradata first byte is its size, but can be 0 in mkv
        next = start;
        for(; next < end; start = next){
            next = find_next_marker(start + 4, end);
            size = next - start - 4;
            if(size <= 0) continue;
            buf2_size = vc1_unescape_buffer(start + 4, size, buf2);
            init_get_bits(&gb, buf2, buf2_size * 8);
            switch(AV_RB32(start)){
            case VC1_CODE_SEQHDR:
                if(vc1_decode_sequence_header(avctx, v, &gb) < 0){
                    av_free(buf2);
                    return -1;
                }
                seq_initialized = 1;
                break;
            case VC1_CODE_ENTRYPOINT:
                if(vc1_decode_entry_point(avctx, v, &gb) < 0){
                    av_free(buf2);
                    return -1;
                }
                ep_initialized = 1;
                break;
            }
        }
        av_free(buf2);
        if(!seq_initialized || !ep_initialized){
            av_log(avctx, AV_LOG_ERROR, "Incomplete extradata\n");
            return -1;
        }
    }
    avctx->has_b_frames= !!(avctx->max_b_frames);
    s->low_delay = !avctx->has_b_frames;

    s->mb_width = (avctx->coded_width+15)>>4;
    s->mb_height = (avctx->coded_height+15)>>4;

    /* Allocate mb bitplanes */
    v->mv_type_mb_plane = av_malloc(s->mb_stride * s->mb_height);
    v->direct_mb_plane = av_malloc(s->mb_stride * s->mb_height);
    v->acpred_plane = av_malloc(s->mb_stride * s->mb_height);
    v->over_flags_plane = av_malloc(s->mb_stride * s->mb_height);

    v->cbp_base = av_malloc(sizeof(v->cbp_base[0]) * 2 * s->mb_stride);
    v->cbp = v->cbp_base + s->mb_stride;

    /* allocate block type info in that way so it could be used with s->block_index[] */
    v->mb_type_base = av_malloc(s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2);
    v->mb_type[0] = v->mb_type_base + s->b8_stride + 1;
    v->mb_type[1] = v->mb_type_base + s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride + 1;
    v->mb_type[2] = v->mb_type[1] + s->mb_stride * (s->mb_height + 1);

    /* Init coded blocks info */
    if (v->profile == PROFILE_ADVANCED)
    {
//        if (alloc_bitplane(&v->over_flags_plane, s->mb_width, s->mb_height) < 0)
//            return -1;
//        if (alloc_bitplane(&v->ac_pred_plane, s->mb_width, s->mb_height) < 0)
//            return -1;
    }

    ff_intrax8_common_init(&v->x8,s);
    return 0;
}


/** Decode a VC1/WMV3 frame
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 */
static int vc1_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame *pict = data;
    uint8_t *buf2 = NULL;
    const uint8_t *buf_start = buf;

    /* no supplementary picture */
    if (buf_size == 0) {
        /* special case for last picture */
        if (s->low_delay==0 && s->next_picture_ptr) {
            *pict= *(AVFrame*)s->next_picture_ptr;
            s->next_picture_ptr= NULL;

            *data_size = sizeof(AVFrame);
        }

        return 0;
    }

    /* We need to set current_picture_ptr before reading the header,
     * otherwise we cannot store anything in there. */
    if(s->current_picture_ptr==NULL || s->current_picture_ptr->data[0]){
        int i= ff_find_unused_picture(s, 0);
        s->current_picture_ptr= &s->picture[i];
    }

    if (s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU){
        if (v->profile < PROFILE_ADVANCED)
            avctx->pix_fmt = PIX_FMT_VDPAU_WMV3;
        else
            avctx->pix_fmt = PIX_FMT_VDPAU_VC1;
    }

    //for advanced profile we may need to parse and unescape data
    if (avctx->codec_id == CODEC_ID_VC1) {
        int buf_size2 = 0;
        buf2 = av_mallocz(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);

        if(IS_MARKER(AV_RB32(buf))){ /* frame starts with marker and needs to be parsed */
            const uint8_t *start, *end, *next;
            int size;

            next = buf;
            for(start = buf, end = buf + buf_size; next < end; start = next){
                next = find_next_marker(start + 4, end);
                size = next - start - 4;
                if(size <= 0) continue;
                switch(AV_RB32(start)){
                case VC1_CODE_FRAME:
                    if (avctx->hwaccel ||
                        s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
                        buf_start = start;
                    buf_size2 = vc1_unescape_buffer(start + 4, size, buf2);
                    break;
                case VC1_CODE_ENTRYPOINT: /* it should be before frame data */
                    buf_size2 = vc1_unescape_buffer(start + 4, size, buf2);
                    init_get_bits(&s->gb, buf2, buf_size2*8);
                    vc1_decode_entry_point(avctx, v, &s->gb);
                    break;
                case VC1_CODE_SLICE:
                    av_log(avctx, AV_LOG_ERROR, "Sliced decoding is not implemented (yet)\n");
                    av_free(buf2);
                    return -1;
                }
            }
        }else if(v->interlace && ((buf[0] & 0xC0) == 0xC0)){ /* WVC1 interlaced stores both fields divided by marker */
            const uint8_t *divider;

            divider = find_next_marker(buf, buf + buf_size);
            if((divider == (buf + buf_size)) || AV_RB32(divider) != VC1_CODE_FIELD){
                av_log(avctx, AV_LOG_ERROR, "Error in WVC1 interlaced frame\n");
                av_free(buf2);
                return -1;
            }

            buf_size2 = vc1_unescape_buffer(buf, divider - buf, buf2);
            // TODO
            if(!v->warn_interlaced++)
                av_log(v->s.avctx, AV_LOG_ERROR, "Interlaced WVC1 support is not implemented\n");
            av_free(buf2);return -1;
        }else{
            buf_size2 = vc1_unescape_buffer(buf, buf_size, buf2);
        }
        init_get_bits(&s->gb, buf2, buf_size2*8);
    } else
        init_get_bits(&s->gb, buf, buf_size*8);
    // do parse frame header
    if(v->profile < PROFILE_ADVANCED) {
        if(vc1_parse_frame_header(v, &s->gb) == -1) {
            av_free(buf2);
            return -1;
        }
    } else {
        if(vc1_parse_frame_header_adv(v, &s->gb) == -1) {
            av_free(buf2);
            return -1;
        }
    }

    if(s->pict_type != FF_I_TYPE && !v->res_rtm_flag){
        av_free(buf2);
        return -1;
    }

    // for hurry_up==5
    s->current_picture.pict_type= s->pict_type;
    s->current_picture.key_frame= s->pict_type == FF_I_TYPE;

    /* skip B-frames if we don't have reference frames */
    if(s->last_picture_ptr==NULL && (s->pict_type==FF_B_TYPE || s->dropable)){
        av_free(buf2);
        return -1;//buf_size;
    }
    /* skip b frames if we are in a hurry */
    if(avctx->hurry_up && s->pict_type==FF_B_TYPE) return -1;//buf_size;
    if(   (avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type==FF_B_TYPE)
       || (avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type!=FF_I_TYPE)
       ||  avctx->skip_frame >= AVDISCARD_ALL) {
        av_free(buf2);
        return buf_size;
    }
    /* skip everything if we are in a hurry>=5 */
    if(avctx->hurry_up>=5) {
        av_free(buf2);
        return -1;//buf_size;
    }

    if(s->next_p_frame_damaged){
        if(s->pict_type==FF_B_TYPE)
            return buf_size;
        else
            s->next_p_frame_damaged=0;
    }

    if(MPV_frame_start(s, avctx) < 0) {
        av_free(buf2);
        return -1;
    }

    s->me.qpel_put= s->dsp.put_qpel_pixels_tab;
    s->me.qpel_avg= s->dsp.avg_qpel_pixels_tab;

    if ((CONFIG_VC1_VDPAU_DECODER)
        &&s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_vc1_decode_picture(s, buf_start, (buf + buf_size) - buf_start);
    else if (avctx->hwaccel) {
        if (avctx->hwaccel->start_frame(avctx, buf, buf_size) < 0)
            return -1;
        if (avctx->hwaccel->decode_slice(avctx, buf_start, (buf + buf_size) - buf_start) < 0)
            return -1;
        if (avctx->hwaccel->end_frame(avctx) < 0)
            return -1;
    } else {
        ff_er_frame_start(s);

        v->bits = buf_size * 8;
        vc1_decode_blocks(v);
//av_log(s->avctx, AV_LOG_INFO, "Consumed %i/%i bits\n", get_bits_count(&s->gb), buf_size*8);
//  if(get_bits_count(&s->gb) > buf_size * 8)
//      return -1;
        ff_er_frame_end(s);
    }

    MPV_frame_end(s);

assert(s->current_picture.pict_type == s->current_picture_ptr->pict_type);
assert(s->current_picture.pict_type == s->pict_type);
    if (s->pict_type == FF_B_TYPE || s->low_delay) {
        *pict= *(AVFrame*)s->current_picture_ptr;
    } else if (s->last_picture_ptr != NULL) {
        *pict= *(AVFrame*)s->last_picture_ptr;
    }

    if(s->last_picture_ptr || s->low_delay){
        *data_size = sizeof(AVFrame);
        ff_print_debug_info(s, pict);
    }

    av_free(buf2);
    return buf_size;
}


/** Close a VC1/WMV3 decoder
 * @warning Initial try at using MpegEncContext stuff
 */
static av_cold int vc1_decode_end(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;

    av_freep(&v->hrd_rate);
    av_freep(&v->hrd_buffer);
    MPV_common_end(&v->s);
    av_freep(&v->mv_type_mb_plane);
    av_freep(&v->direct_mb_plane);
    av_freep(&v->acpred_plane);
    av_freep(&v->over_flags_plane);
    av_freep(&v->mb_type_base);
    av_freep(&v->cbp_base);
    ff_intrax8_common_end(&v->x8);
    return 0;
}


AVCodec vc1_decoder = {
    "vc1",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_VC1,
    sizeof(VC1Context),
    vc1_decode_init,
    NULL,
    vc1_decode_end,
    vc1_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    NULL,
    .flush          = ff_mpeg_flush,
    .long_name = NULL_IF_CONFIG_SMALL("SMPTE VC-1"),
    .pix_fmts = ff_hwaccel_pixfmt_list_420
};

#if CONFIG_WMV3_DECODER
AVCodec wmv3_decoder = {
    "wmv3",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_WMV3,
    sizeof(VC1Context),
    vc1_decode_init,
    NULL,
    vc1_decode_end,
    vc1_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    NULL,
    .flush          = ff_mpeg_flush,
    .long_name = NULL_IF_CONFIG_SMALL("Windows Media Video 9"),
    .pix_fmts = ff_hwaccel_pixfmt_list_420
};
#endif

#if CONFIG_WMV3_VDPAU_DECODER
AVCodec wmv3_vdpau_decoder = {
    "wmv3_vdpau",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_WMV3,
    sizeof(VC1Context),
    vc1_decode_init,
    NULL,
    vc1_decode_end,
    vc1_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_HWACCEL_VDPAU,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("Windows Media Video 9 VDPAU"),
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_VDPAU_WMV3, PIX_FMT_NONE}
};
#endif

#if CONFIG_VC1_VDPAU_DECODER
AVCodec vc1_vdpau_decoder = {
    "vc1_vdpau",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_VC1,
    sizeof(VC1Context),
    vc1_decode_init,
    NULL,
    vc1_decode_end,
    vc1_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_HWACCEL_VDPAU,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("SMPTE VC-1 VDPAU"),
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_VDPAU_VC1, PIX_FMT_NONE}
};
#endif
