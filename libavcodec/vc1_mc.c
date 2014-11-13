/*
 * VC-1 and WMV3 decoder
 * Copyright (c) 2011 Mashiat Sarker Shakkhar
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
 * VC-1 and WMV3 block decoding routines
 */

#include "avcodec.h"
#include "h264chroma.h"
#include "mathops.h"
#include "mpegvideo.h"
#include "vc1.h"

/** Do motion compensation over 1 macroblock
 * Mostly adapted hpel_motion and qpel_motion from mpegvideo.c
 */
void ff_vc1_mc_1mv(VC1Context *v, int dir)
{
    MpegEncContext *s = &v->s;
    H264ChromaContext *h264chroma = &v->h264chroma;
    uint8_t *srcY, *srcU, *srcV;
    int dxy, mx, my, uvmx, uvmy, src_x, src_y, uvsrc_x, uvsrc_y;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;
    int i;
    uint8_t (*luty)[256], (*lutuv)[256];
    int use_ic;

    if ((!v->field_mode ||
         (v->ref_field_type[dir] == 1 && v->cur_field_type == 1)) &&
        !v->s.last_picture.f->data[0])
        return;

    mx = s->mv[dir][0][0];
    my = s->mv[dir][0][1];

    // store motion vectors for further use in B frames
    if (s->pict_type == AV_PICTURE_TYPE_P) {
        for (i = 0; i < 4; i++) {
            s->current_picture.motion_val[1][s->block_index[i] + v->blocks_off][0] = mx;
            s->current_picture.motion_val[1][s->block_index[i] + v->blocks_off][1] = my;
        }
    }

    uvmx = (mx + ((mx & 3) == 3)) >> 1;
    uvmy = (my + ((my & 3) == 3)) >> 1;
    v->luma_mv[s->mb_x][0] = uvmx;
    v->luma_mv[s->mb_x][1] = uvmy;

    if (v->field_mode &&
        v->cur_field_type != v->ref_field_type[dir]) {
        my   = my   - 2 + 4 * v->cur_field_type;
        uvmy = uvmy - 2 + 4 * v->cur_field_type;
    }

    // fastuvmc shall be ignored for interlaced frame picture
    if (v->fastuvmc && (v->fcm != ILACE_FRAME)) {
        uvmx = uvmx + ((uvmx < 0) ? (uvmx & 1) : -(uvmx & 1));
        uvmy = uvmy + ((uvmy < 0) ? (uvmy & 1) : -(uvmy & 1));
    }
    if (!dir) {
        if (v->field_mode && (v->cur_field_type != v->ref_field_type[dir]) && v->second_field) {
            srcY = s->current_picture.f->data[0];
            srcU = s->current_picture.f->data[1];
            srcV = s->current_picture.f->data[2];
            luty  = v->curr_luty;
            lutuv = v->curr_lutuv;
            use_ic = *v->curr_use_ic;
        } else {
            srcY = s->last_picture.f->data[0];
            srcU = s->last_picture.f->data[1];
            srcV = s->last_picture.f->data[2];
            luty  = v->last_luty;
            lutuv = v->last_lutuv;
            use_ic = v->last_use_ic;
        }
    } else {
        srcY = s->next_picture.f->data[0];
        srcU = s->next_picture.f->data[1];
        srcV = s->next_picture.f->data[2];
        luty  = v->next_luty;
        lutuv = v->next_lutuv;
        use_ic = v->next_use_ic;
    }

    if (!srcY || !srcU) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Referenced frame missing.\n");
        return;
    }

    src_x   = s->mb_x * 16 + (mx   >> 2);
    src_y   = s->mb_y * 16 + (my   >> 2);
    uvsrc_x = s->mb_x *  8 + (uvmx >> 2);
    uvsrc_y = s->mb_y *  8 + (uvmy >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        src_x   = av_clip(  src_x, -16, s->mb_width  * 16);
        src_y   = av_clip(  src_y, -16, s->mb_height * 16);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->mb_width  *  8);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->mb_height *  8);
    } else {
        src_x   = av_clip(  src_x, -17, s->avctx->coded_width);
        src_y   = av_clip(  src_y, -18, s->avctx->coded_height + 1);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
    }

    srcY += src_y   * s->linesize   + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode && v->ref_field_type[dir]) {
        srcY += s->current_picture_ptr->f->linesize[0];
        srcU += s->current_picture_ptr->f->linesize[1];
        srcV += s->current_picture_ptr->f->linesize[2];
    }

    /* for grayscale we should not try to read from unknown area */
    if (s->flags & CODEC_FLAG_GRAY) {
        srcU = s->edge_emu_buffer + 18 * s->linesize;
        srcV = s->edge_emu_buffer + 18 * s->linesize;
    }

    if (v->rangeredfrm || use_ic
        || s->h_edge_pos < 22 || v_edge_pos < 22
        || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx&3) - 16 - s->mspel * 3
        || (unsigned)(src_y - 1)        > v_edge_pos    - (my&3) - 16 - 3) {
        uint8_t *ubuf = s->edge_emu_buffer + 19 * s->linesize;
        uint8_t *vbuf = ubuf + 9 * s->uvlinesize;

        srcY -= s->mspel * (1 + s->linesize);
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, srcY,
                                 s->linesize, s->linesize,
                                 17 + s->mspel * 2, 17 + s->mspel * 2,
                                 src_x - s->mspel, src_y - s->mspel,
                                 s->h_edge_pos, v_edge_pos);
        srcY = s->edge_emu_buffer;
        s->vdsp.emulated_edge_mc(ubuf, srcU,
                                 s->uvlinesize, s->uvlinesize,
                                 8 + 1, 8 + 1,
                                 uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, v_edge_pos >> 1);
        s->vdsp.emulated_edge_mc(vbuf, srcV,
                                 s->uvlinesize, s->uvlinesize,
                                 8 + 1, 8 + 1,
                                 uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, v_edge_pos >> 1);
        srcU = ubuf;
        srcV = vbuf;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for (j = 0; j < 17 + s->mspel * 2; j++) {
                for (i = 0; i < 17 + s->mspel * 2; i++)
                    src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize;
            }
            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = ((src[i]  - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (use_ic) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for (j = 0; j < 17 + s->mspel * 2; j++) {
                int f = v->field_mode ? v->ref_field_type[dir] : ((j + src_y - s->mspel) & 1) ;
                for (i = 0; i < 17 + s->mspel * 2; i++)
                    src[i] = luty[f][src[i]];
                src += s->linesize;
            }
            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                int f = v->field_mode ? v->ref_field_type[dir] : ((j + uvsrc_y) & 1);
                for (i = 0; i < 9; i++) {
                    src[i]  = lutuv[f][src[i]];
                    src2[i] = lutuv[f][src2[i]];
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        v->vc1dsp.put_vc1_mspel_pixels_tab[0][dxy](s->dest[0]    , srcY    , s->linesize, v->rnd);
    } else { // hpel mc - always used for luma
        dxy = (my & 2) | ((mx & 2) >> 1);
        if (!v->rnd)
            s->hdsp.put_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
        else
            s->hdsp.put_no_rnd_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
    }

    if (s->flags & CODEC_FLAG_GRAY) return;
    /* Chroma MC always uses qpel bilinear */
    uvmx = (uvmx & 3) << 1;
    uvmy = (uvmy & 3) << 1;
    if (!v->rnd) {
        h264chroma->put_h264_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        h264chroma->put_h264_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    } else {
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

static inline int median4(int a, int b, int c, int d)
{
    if (a < b) {
        if (c < d) return (FFMIN(b, d) + FFMAX(a, c)) / 2;
        else       return (FFMIN(b, c) + FFMAX(a, d)) / 2;
    } else {
        if (c < d) return (FFMIN(a, d) + FFMAX(b, c)) / 2;
        else       return (FFMIN(a, c) + FFMAX(b, d)) / 2;
    }
}

/** Do motion compensation for 4-MV macroblock - luminance block
 */
void ff_vc1_mc_4mv_luma(VC1Context *v, int n, int dir, int avg)
{
    MpegEncContext *s = &v->s;
    uint8_t *srcY;
    int dxy, mx, my, src_x, src_y;
    int off;
    int fieldmv = (v->fcm == ILACE_FRAME) ? v->blk_mv_type[s->block_index[n]] : 0;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;
    uint8_t (*luty)[256];
    int use_ic;

    if ((!v->field_mode ||
         (v->ref_field_type[dir] == 1 && v->cur_field_type == 1)) &&
        !v->s.last_picture.f->data[0])
        return;

    mx = s->mv[dir][n][0];
    my = s->mv[dir][n][1];

    if (!dir) {
        if (v->field_mode && (v->cur_field_type != v->ref_field_type[dir]) && v->second_field) {
            srcY = s->current_picture.f->data[0];
            luty = v->curr_luty;
            use_ic = *v->curr_use_ic;
        } else {
            srcY = s->last_picture.f->data[0];
            luty = v->last_luty;
            use_ic = v->last_use_ic;
        }
    } else {
        srcY = s->next_picture.f->data[0];
        luty = v->next_luty;
        use_ic = v->next_use_ic;
    }

    if (!srcY) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Referenced frame missing.\n");
        return;
    }

    if (v->field_mode) {
        if (v->cur_field_type != v->ref_field_type[dir])
            my = my - 2 + 4 * v->cur_field_type;
    }

    if (s->pict_type == AV_PICTURE_TYPE_P && n == 3 && v->field_mode) {
        int same_count = 0, opp_count = 0, k;
        int chosen_mv[2][4][2], f;
        int tx, ty;
        for (k = 0; k < 4; k++) {
            f = v->mv_f[0][s->block_index[k] + v->blocks_off];
            chosen_mv[f][f ? opp_count : same_count][0] = s->mv[0][k][0];
            chosen_mv[f][f ? opp_count : same_count][1] = s->mv[0][k][1];
            opp_count  += f;
            same_count += 1 - f;
        }
        f = opp_count > same_count;
        switch (f ? opp_count : same_count) {
        case 4:
            tx = median4(chosen_mv[f][0][0], chosen_mv[f][1][0],
                         chosen_mv[f][2][0], chosen_mv[f][3][0]);
            ty = median4(chosen_mv[f][0][1], chosen_mv[f][1][1],
                         chosen_mv[f][2][1], chosen_mv[f][3][1]);
            break;
        case 3:
            tx = mid_pred(chosen_mv[f][0][0], chosen_mv[f][1][0], chosen_mv[f][2][0]);
            ty = mid_pred(chosen_mv[f][0][1], chosen_mv[f][1][1], chosen_mv[f][2][1]);
            break;
        case 2:
            tx = (chosen_mv[f][0][0] + chosen_mv[f][1][0]) / 2;
            ty = (chosen_mv[f][0][1] + chosen_mv[f][1][1]) / 2;
            break;
        default:
            av_assert0(0);
        }
        s->current_picture.motion_val[1][s->block_index[0] + v->blocks_off][0] = tx;
        s->current_picture.motion_val[1][s->block_index[0] + v->blocks_off][1] = ty;
        for (k = 0; k < 4; k++)
            v->mv_f[1][s->block_index[k] + v->blocks_off] = f;
    }

    if (v->fcm == ILACE_FRAME) {  // not sure if needed for other types of picture
        int qx, qy;
        int width  = s->avctx->coded_width;
        int height = s->avctx->coded_height >> 1;
        if (s->pict_type == AV_PICTURE_TYPE_P) {
            s->current_picture.motion_val[1][s->block_index[n] + v->blocks_off][0] = mx;
            s->current_picture.motion_val[1][s->block_index[n] + v->blocks_off][1] = my;
        }
        qx = (s->mb_x * 16) + (mx >> 2);
        qy = (s->mb_y *  8) + (my >> 3);

        if (qx < -17)
            mx -= 4 * (qx + 17);
        else if (qx > width)
            mx -= 4 * (qx - width);
        if (qy < -18)
            my -= 8 * (qy + 18);
        else if (qy > height + 1)
            my -= 8 * (qy - height - 1);
    }

    if ((v->fcm == ILACE_FRAME) && fieldmv)
        off = ((n > 1) ? s->linesize : 0) + (n & 1) * 8;
    else
        off = s->linesize * 4 * (n & 2) + (n & 1) * 8;

    src_x = s->mb_x * 16 + (n & 1) * 8 + (mx >> 2);
    if (!fieldmv)
        src_y = s->mb_y * 16 + (n & 2) * 4 + (my >> 2);
    else
        src_y = s->mb_y * 16 + ((n > 1) ? 1 : 0) + (my >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        src_x = av_clip(src_x, -16, s->mb_width  * 16);
        src_y = av_clip(src_y, -16, s->mb_height * 16);
    } else {
        src_x = av_clip(src_x, -17, s->avctx->coded_width);
        if (v->fcm == ILACE_FRAME) {
            if (src_y & 1)
                src_y = av_clip(src_y, -17, s->avctx->coded_height + 1);
            else
                src_y = av_clip(src_y, -18, s->avctx->coded_height);
        } else {
            src_y = av_clip(src_y, -18, s->avctx->coded_height + 1);
        }
    }

    srcY += src_y * s->linesize + src_x;
    if (v->field_mode && v->ref_field_type[dir])
        srcY += s->current_picture_ptr->f->linesize[0];

    if (fieldmv && !(src_y & 1))
        v_edge_pos--;
    if (fieldmv && (src_y & 1) && src_y < 4)
        src_y--;
    if (v->rangeredfrm || use_ic
        || s->h_edge_pos < 13 || v_edge_pos < 23
        || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx & 3) - 8 - s->mspel * 2
        || (unsigned)(src_y - (s->mspel << fieldmv)) > v_edge_pos - (my & 3) - ((8 + s->mspel * 2) << fieldmv)) {
        srcY -= s->mspel * (1 + (s->linesize << fieldmv));
        /* check emulate edge stride and offset */
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, srcY,
                                 s->linesize, s->linesize,
                                 9 + s->mspel * 2, (9 + s->mspel * 2) << fieldmv,
                                 src_x - s->mspel, src_y - (s->mspel << fieldmv),
                                 s->h_edge_pos, v_edge_pos);
        srcY = s->edge_emu_buffer;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src;

            src = srcY;
            for (j = 0; j < 9 + s->mspel * 2; j++) {
                for (i = 0; i < 9 + s->mspel * 2; i++)
                    src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize << fieldmv;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (use_ic) {
            int i, j;
            uint8_t *src;

            src = srcY;
            for (j = 0; j < 9 + s->mspel * 2; j++) {
                int f = v->field_mode ? v->ref_field_type[dir] : (((j<<fieldmv)+src_y - (s->mspel << fieldmv)) & 1);
                for (i = 0; i < 9 + s->mspel * 2; i++)
                    src[i] = luty[f][src[i]];
                src += s->linesize << fieldmv;
            }
        }
        srcY += s->mspel * (1 + (s->linesize << fieldmv));
    }

    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        if (avg)
            v->vc1dsp.avg_vc1_mspel_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize << fieldmv, v->rnd);
        else
            v->vc1dsp.put_vc1_mspel_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize << fieldmv, v->rnd);
    } else { // hpel mc - always used for luma
        dxy = (my & 2) | ((mx & 2) >> 1);
        if (!v->rnd)
            s->hdsp.put_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize, 8);
        else
            s->hdsp.put_no_rnd_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize, 8);
    }
}

static av_always_inline int get_chroma_mv(int *mvx, int *mvy, int *a, int flag, int *tx, int *ty)
{
    int idx, i;
    static const int count[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

    idx =  ((a[3] != flag) << 3)
         | ((a[2] != flag) << 2)
         | ((a[1] != flag) << 1)
         |  (a[0] != flag);
    if (!idx) {
        *tx = median4(mvx[0], mvx[1], mvx[2], mvx[3]);
        *ty = median4(mvy[0], mvy[1], mvy[2], mvy[3]);
        return 4;
    } else if (count[idx] == 1) {
        switch (idx) {
        case 0x1:
            *tx = mid_pred(mvx[1], mvx[2], mvx[3]);
            *ty = mid_pred(mvy[1], mvy[2], mvy[3]);
            return 3;
        case 0x2:
            *tx = mid_pred(mvx[0], mvx[2], mvx[3]);
            *ty = mid_pred(mvy[0], mvy[2], mvy[3]);
            return 3;
        case 0x4:
            *tx = mid_pred(mvx[0], mvx[1], mvx[3]);
            *ty = mid_pred(mvy[0], mvy[1], mvy[3]);
            return 3;
        case 0x8:
            *tx = mid_pred(mvx[0], mvx[1], mvx[2]);
            *ty = mid_pred(mvy[0], mvy[1], mvy[2]);
            return 3;
        }
    } else if (count[idx] == 2) {
        int t1 = 0, t2 = 0;
        for (i = 0; i < 3; i++)
            if (!a[i]) {
                t1 = i;
                break;
            }
        for (i = t1 + 1; i < 4; i++)
            if (!a[i]) {
                t2 = i;
                break;
            }
        *tx = (mvx[t1] + mvx[t2]) / 2;
        *ty = (mvy[t1] + mvy[t2]) / 2;
        return 2;
    } else {
        return 0;
    }
    return -1;
}

/** Do motion compensation for 4-MV macroblock - both chroma blocks
 */
void ff_vc1_mc_4mv_chroma(VC1Context *v, int dir)
{
    MpegEncContext *s = &v->s;
    H264ChromaContext *h264chroma = &v->h264chroma;
    uint8_t *srcU, *srcV;
    int uvmx, uvmy, uvsrc_x, uvsrc_y;
    int k, tx = 0, ty = 0;
    int mvx[4], mvy[4], intra[4], mv_f[4];
    int valid_count;
    int chroma_ref_type = v->cur_field_type;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;
    uint8_t (*lutuv)[256];
    int use_ic;

    if (!v->field_mode && !v->s.last_picture.f->data[0])
        return;
    if (s->flags & CODEC_FLAG_GRAY)
        return;

    for (k = 0; k < 4; k++) {
        mvx[k] = s->mv[dir][k][0];
        mvy[k] = s->mv[dir][k][1];
        intra[k] = v->mb_type[0][s->block_index[k]];
        if (v->field_mode)
            mv_f[k] = v->mv_f[dir][s->block_index[k] + v->blocks_off];
    }

    /* calculate chroma MV vector from four luma MVs */
    if (!v->field_mode || (v->field_mode && !v->numref)) {
        valid_count = get_chroma_mv(mvx, mvy, intra, 0, &tx, &ty);
        chroma_ref_type = v->reffield;
        if (!valid_count) {
            s->current_picture.motion_val[1][s->block_index[0] + v->blocks_off][0] = 0;
            s->current_picture.motion_val[1][s->block_index[0] + v->blocks_off][1] = 0;
            v->luma_mv[s->mb_x][0] = v->luma_mv[s->mb_x][1] = 0;
            return; //no need to do MC for intra blocks
        }
    } else {
        int dominant = 0;
        if (mv_f[0] + mv_f[1] + mv_f[2] + mv_f[3] > 2)
            dominant = 1;
        valid_count = get_chroma_mv(mvx, mvy, mv_f, dominant, &tx, &ty);
        if (dominant)
            chroma_ref_type = !v->cur_field_type;
    }
    if (v->field_mode && chroma_ref_type == 1 && v->cur_field_type == 1 && !v->s.last_picture.f->data[0])
        return;
    s->current_picture.motion_val[1][s->block_index[0] + v->blocks_off][0] = tx;
    s->current_picture.motion_val[1][s->block_index[0] + v->blocks_off][1] = ty;
    uvmx = (tx + ((tx & 3) == 3)) >> 1;
    uvmy = (ty + ((ty & 3) == 3)) >> 1;

    v->luma_mv[s->mb_x][0] = uvmx;
    v->luma_mv[s->mb_x][1] = uvmy;

    if (v->fastuvmc) {
        uvmx = uvmx + ((uvmx < 0) ? (uvmx & 1) : -(uvmx & 1));
        uvmy = uvmy + ((uvmy < 0) ? (uvmy & 1) : -(uvmy & 1));
    }
    // Field conversion bias
    if (v->cur_field_type != chroma_ref_type)
        uvmy += 2 - 4 * chroma_ref_type;

    uvsrc_x = s->mb_x * 8 + (uvmx >> 2);
    uvsrc_y = s->mb_y * 8 + (uvmy >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        uvsrc_x = av_clip(uvsrc_x, -8, s->mb_width  * 8);
        uvsrc_y = av_clip(uvsrc_y, -8, s->mb_height * 8);
    } else {
        uvsrc_x = av_clip(uvsrc_x, -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y, -8, s->avctx->coded_height >> 1);
    }

    if (!dir) {
        if (v->field_mode && (v->cur_field_type != chroma_ref_type) && v->second_field) {
            srcU = s->current_picture.f->data[1];
            srcV = s->current_picture.f->data[2];
            lutuv = v->curr_lutuv;
            use_ic = *v->curr_use_ic;
        } else {
            srcU = s->last_picture.f->data[1];
            srcV = s->last_picture.f->data[2];
            lutuv = v->last_lutuv;
            use_ic = v->last_use_ic;
        }
    } else {
        srcU = s->next_picture.f->data[1];
        srcV = s->next_picture.f->data[2];
        lutuv = v->next_lutuv;
        use_ic = v->next_use_ic;
    }

    if (!srcU) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Referenced frame missing.\n");
        return;
    }

    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode) {
        if (chroma_ref_type) {
            srcU += s->current_picture_ptr->f->linesize[1];
            srcV += s->current_picture_ptr->f->linesize[2];
        }
    }

    if (v->rangeredfrm || use_ic
        || s->h_edge_pos < 18 || v_edge_pos < 18
        || (unsigned)uvsrc_x > (s->h_edge_pos >> 1) - 9
        || (unsigned)uvsrc_y > (v_edge_pos    >> 1) - 9) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, srcU,
                                 s->uvlinesize, s->uvlinesize,
                                 8 + 1, 8 + 1, uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, v_edge_pos >> 1);
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer + 16, srcV,
                                 s->uvlinesize, s->uvlinesize,
                                 8 + 1, 8 + 1, uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, v_edge_pos >> 1);
        srcU = s->edge_emu_buffer;
        srcV = s->edge_emu_buffer + 16;

        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = ((src[i]  - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (use_ic) {
            int i, j;
            uint8_t *src, *src2;

            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                int f = v->field_mode ? chroma_ref_type : ((j + uvsrc_y) & 1);
                for (i = 0; i < 9; i++) {
                    src[i]  = lutuv[f][src[i]];
                    src2[i] = lutuv[f][src2[i]];
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
    }

    /* Chroma MC always uses qpel bilinear */
    uvmx = (uvmx & 3) << 1;
    uvmy = (uvmy & 3) << 1;
    if (!v->rnd) {
        h264chroma->put_h264_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        h264chroma->put_h264_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    } else {
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

/** Do motion compensation for 4-MV interlaced frame chroma macroblock (both U and V)
 */
void ff_vc1_mc_4mv_chroma4(VC1Context *v, int dir, int dir2, int avg)
{
    MpegEncContext *s = &v->s;
    H264ChromaContext *h264chroma = &v->h264chroma;
    uint8_t *srcU, *srcV;
    int uvsrc_x, uvsrc_y;
    int uvmx_field[4], uvmy_field[4];
    int i, off, tx, ty;
    int fieldmv = v->blk_mv_type[s->block_index[0]];
    static const int s_rndtblfield[16] = { 0, 0, 1, 2, 4, 4, 5, 6, 2, 2, 3, 8, 6, 6, 7, 12 };
    int v_dist = fieldmv ? 1 : 4; // vertical offset for lower sub-blocks
    int v_edge_pos = s->v_edge_pos >> 1;
    int use_ic;
    uint8_t (*lutuv)[256];

    if (s->flags & CODEC_FLAG_GRAY)
        return;

    for (i = 0; i < 4; i++) {
        int d = i < 2 ? dir: dir2;
        tx = s->mv[d][i][0];
        uvmx_field[i] = (tx + ((tx & 3) == 3)) >> 1;
        ty = s->mv[d][i][1];
        if (fieldmv)
            uvmy_field[i] = (ty >> 4) * 8 + s_rndtblfield[ty & 0xF];
        else
            uvmy_field[i] = (ty + ((ty & 3) == 3)) >> 1;
    }

    for (i = 0; i < 4; i++) {
        off = (i & 1) * 4 + ((i & 2) ? v_dist * s->uvlinesize : 0);
        uvsrc_x = s->mb_x * 8 +  (i & 1) * 4           + (uvmx_field[i] >> 2);
        uvsrc_y = s->mb_y * 8 + ((i & 2) ? v_dist : 0) + (uvmy_field[i] >> 2);
        // FIXME: implement proper pull-back (see vc1cropmv.c, vc1CROPMV_ChromaPullBack())
        uvsrc_x = av_clip(uvsrc_x, -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y, -8, s->avctx->coded_height >> 1);
        if (i < 2 ? dir : dir2) {
            srcU = s->next_picture.f->data[1];
            srcV = s->next_picture.f->data[2];
            lutuv  = v->next_lutuv;
            use_ic = v->next_use_ic;
        } else {
            srcU = s->last_picture.f->data[1];
            srcV = s->last_picture.f->data[2];
            lutuv  = v->last_lutuv;
            use_ic = v->last_use_ic;
        }
        if (!srcU)
            return;
        srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
        srcV += uvsrc_y * s->uvlinesize + uvsrc_x;
        uvmx_field[i] = (uvmx_field[i] & 3) << 1;
        uvmy_field[i] = (uvmy_field[i] & 3) << 1;

        if (fieldmv && !(uvsrc_y & 1))
            v_edge_pos = (s->v_edge_pos >> 1) - 1;

        if (fieldmv && (uvsrc_y & 1) && uvsrc_y < 2)
            uvsrc_y--;
        if (use_ic
            || s->h_edge_pos < 10 || v_edge_pos < (5 << fieldmv)
            || (unsigned)uvsrc_x > (s->h_edge_pos >> 1) - 5
            || (unsigned)uvsrc_y > v_edge_pos - (5 << fieldmv)) {
            s->vdsp.emulated_edge_mc(s->edge_emu_buffer, srcU,
                                     s->uvlinesize, s->uvlinesize,
                                     5, (5 << fieldmv), uvsrc_x, uvsrc_y,
                                     s->h_edge_pos >> 1, v_edge_pos);
            s->vdsp.emulated_edge_mc(s->edge_emu_buffer + 16, srcV,
                                     s->uvlinesize, s->uvlinesize,
                                     5, (5 << fieldmv), uvsrc_x, uvsrc_y,
                                     s->h_edge_pos >> 1, v_edge_pos);
            srcU = s->edge_emu_buffer;
            srcV = s->edge_emu_buffer + 16;

            /* if we deal with intensity compensation we need to scale source blocks */
            if (use_ic) {
                int i, j;
                uint8_t *src, *src2;

                src  = srcU;
                src2 = srcV;
                for (j = 0; j < 5; j++) {
                    int f = (uvsrc_y + (j << fieldmv)) & 1;
                    for (i = 0; i < 5; i++) {
                        src[i]  = lutuv[f][src[i]];
                        src2[i] = lutuv[f][src2[i]];
                    }
                    src  += s->uvlinesize << fieldmv;
                    src2 += s->uvlinesize << fieldmv;
                }
            }
        }
        if (avg) {
            if (!v->rnd) {
                h264chroma->avg_h264_chroma_pixels_tab[1](s->dest[1] + off, srcU, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
                h264chroma->avg_h264_chroma_pixels_tab[1](s->dest[2] + off, srcV, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
            } else {
                v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[1](s->dest[1] + off, srcU, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
                v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[1](s->dest[2] + off, srcV, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
            }
        } else {
            if (!v->rnd) {
                h264chroma->put_h264_chroma_pixels_tab[1](s->dest[1] + off, srcU, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
                h264chroma->put_h264_chroma_pixels_tab[1](s->dest[2] + off, srcV, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
            } else {
                v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[1](s->dest[1] + off, srcU, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
                v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[1](s->dest[2] + off, srcV, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
            }
        }
    }
}

/** Motion compensation for direct or interpolated blocks in B-frames
 */
void ff_vc1_interp_mc(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    H264ChromaContext *h264chroma = &v->h264chroma;
    uint8_t *srcY, *srcU, *srcV;
    int dxy, mx, my, uvmx, uvmy, src_x, src_y, uvsrc_x, uvsrc_y;
    int off, off_uv;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;
    int use_ic = v->next_use_ic;

    if (!v->field_mode && !v->s.next_picture.f->data[0])
        return;

    mx   = s->mv[1][0][0];
    my   = s->mv[1][0][1];
    uvmx = (mx + ((mx & 3) == 3)) >> 1;
    uvmy = (my + ((my & 3) == 3)) >> 1;
    if (v->field_mode && v->cur_field_type != v->ref_field_type[1]) {
        my   = my   - 2 + 4 * v->cur_field_type;
        uvmy = uvmy - 2 + 4 * v->cur_field_type;
    }
    if (v->fastuvmc) {
        uvmx = uvmx + ((uvmx < 0) ? -(uvmx & 1) : (uvmx & 1));
        uvmy = uvmy + ((uvmy < 0) ? -(uvmy & 1) : (uvmy & 1));
    }
    srcY = s->next_picture.f->data[0];
    srcU = s->next_picture.f->data[1];
    srcV = s->next_picture.f->data[2];

    src_x   = s->mb_x * 16 + (mx   >> 2);
    src_y   = s->mb_y * 16 + (my   >> 2);
    uvsrc_x = s->mb_x *  8 + (uvmx >> 2);
    uvsrc_y = s->mb_y *  8 + (uvmy >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        src_x   = av_clip(  src_x, -16, s->mb_width  * 16);
        src_y   = av_clip(  src_y, -16, s->mb_height * 16);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->mb_width  *  8);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->mb_height *  8);
    } else {
        src_x   = av_clip(  src_x, -17, s->avctx->coded_width);
        src_y   = av_clip(  src_y, -18, s->avctx->coded_height + 1);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
    }

    srcY += src_y   * s->linesize   + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode && v->ref_field_type[1]) {
        srcY += s->current_picture_ptr->f->linesize[0];
        srcU += s->current_picture_ptr->f->linesize[1];
        srcV += s->current_picture_ptr->f->linesize[2];
    }

    /* for grayscale we should not try to read from unknown area */
    if (s->flags & CODEC_FLAG_GRAY) {
        srcU = s->edge_emu_buffer + 18 * s->linesize;
        srcV = s->edge_emu_buffer + 18 * s->linesize;
    }

    if (v->rangeredfrm || s->h_edge_pos < 22 || v_edge_pos < 22 || use_ic
        || (unsigned)(src_x - 1) > s->h_edge_pos - (mx & 3) - 16 - 3
        || (unsigned)(src_y - 1) > v_edge_pos    - (my & 3) - 16 - 3) {
        uint8_t *ubuf = s->edge_emu_buffer + 19 * s->linesize;
        uint8_t *vbuf = ubuf + 9 * s->uvlinesize;

        srcY -= s->mspel * (1 + s->linesize);
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, srcY,
                                 s->linesize, s->linesize,
                                 17 + s->mspel * 2, 17 + s->mspel * 2,
                                 src_x - s->mspel, src_y - s->mspel,
                                 s->h_edge_pos, v_edge_pos);
        srcY = s->edge_emu_buffer;
        s->vdsp.emulated_edge_mc(ubuf, srcU,
                                 s->uvlinesize, s->uvlinesize,
                                 8 + 1, 8 + 1,
                                 uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, v_edge_pos >> 1);
        s->vdsp.emulated_edge_mc(vbuf, srcV,
                                 s->uvlinesize, s->uvlinesize,
                                 8 + 1, 8 + 1,
                                 uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, v_edge_pos >> 1);
        srcU = ubuf;
        srcV = vbuf;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for (j = 0; j < 17 + s->mspel * 2; j++) {
                for (i = 0; i < 17 + s->mspel * 2; i++)
                    src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize;
            }
            src = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = ((src[i]  - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }

        if (use_ic) {
            uint8_t (*luty )[256] = v->next_luty;
            uint8_t (*lutuv)[256] = v->next_lutuv;
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for (j = 0; j < 17 + s->mspel * 2; j++) {
                int f = v->field_mode ? v->ref_field_type[1] : ((j+src_y - s->mspel) & 1);
                for (i = 0; i < 17 + s->mspel * 2; i++)
                    src[i] = luty[f][src[i]];
                src += s->linesize;
            }
            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                int f = v->field_mode ? v->ref_field_type[1] : ((j+uvsrc_y) & 1);
                for (i = 0; i < 9; i++) {
                    src[i]  = lutuv[f][src[i]];
                    src2[i] = lutuv[f][src2[i]];
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    off    = 0;
    off_uv = 0;

    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        v->vc1dsp.avg_vc1_mspel_pixels_tab[0][dxy](s->dest[0] + off    , srcY    , s->linesize, v->rnd);
    } else { // hpel mc
        dxy = (my & 2) | ((mx & 2) >> 1);

        if (!v->rnd)
            s->hdsp.avg_pixels_tab[0][dxy](s->dest[0] + off, srcY, s->linesize, 16);
        else
            s->hdsp.avg_no_rnd_pixels_tab[dxy](s->dest[0] + off, srcY, s->linesize, 16);
    }

    if (s->flags & CODEC_FLAG_GRAY) return;
    /* Chroma MC always uses qpel blilinear */
    uvmx = (uvmx & 3) << 1;
    uvmy = (uvmy & 3) << 1;
    if (!v->rnd) {
        h264chroma->avg_h264_chroma_pixels_tab[0](s->dest[1] + off_uv, srcU, s->uvlinesize, 8, uvmx, uvmy);
        h264chroma->avg_h264_chroma_pixels_tab[0](s->dest[2] + off_uv, srcV, s->uvlinesize, 8, uvmx, uvmy);
    } else {
        v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1] + off_uv, srcU, s->uvlinesize, 8, uvmx, uvmy);
        v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2] + off_uv, srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}
