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

static av_always_inline void vc1_scale_luma(uint8_t *srcY,
                                            int k, int linesize)
{
    int i, j;
    for (j = 0; j < k; j++) {
        for (i = 0; i < k; i++)
            srcY[i] = ((srcY[i] - 128) >> 1) + 128;
        srcY += linesize;
    }
}

static av_always_inline void vc1_scale_chroma(uint8_t *srcU, uint8_t *srcV,
                                              int k, int uvlinesize)
{
    int i, j;
    for (j = 0; j < k; j++) {
        for (i = 0; i < k; i++) {
            srcU[i] = ((srcU[i] - 128) >> 1) + 128;
            srcV[i] = ((srcV[i] - 128) >> 1) + 128;
        }
        srcU += uvlinesize;
        srcV += uvlinesize;
    }
}

static av_always_inline void vc1_lut_scale_luma(uint8_t *srcY,
                                                const uint8_t *lut1, const uint8_t *lut2,
                                                int k, int linesize)
{
    int i, j;

    for (j = 0; j < k; j += 2) {
        for (i = 0; i < k; i++)
            srcY[i] = lut1[srcY[i]];
        srcY += linesize;

        if (j + 1 == k)
            break;

        for (i = 0; i < k; i++)
            srcY[i] = lut2[srcY[i]];
        srcY += linesize;
    }
}

static av_always_inline void vc1_lut_scale_chroma(uint8_t *srcU, uint8_t *srcV,
                                                  const uint8_t *lut1, const uint8_t *lut2,
                                                  int k, int uvlinesize)
{
    int i, j;

    for (j = 0; j < k; j += 2) {
        for (i = 0; i < k; i++) {
            srcU[i] = lut1[srcU[i]];
            srcV[i] = lut1[srcV[i]];
        }
        srcU += uvlinesize;
        srcV += uvlinesize;

        if (j + 1 == k)
            break;

        for (i = 0; i < k; i++) {
            srcU[i] = lut2[srcU[i]];
            srcV[i] = lut2[srcV[i]];
        }
        srcU += uvlinesize;
        srcV += uvlinesize;
    }
}

static const uint8_t popcount4[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };

static av_always_inline int get_luma_mv(VC1Context *v, int dir, int16_t *tx, int16_t *ty)
{
    MpegEncContext *s = &v->s;
    int idx = v->mv_f[dir][s->block_index[0] + v->blocks_off] |
             (v->mv_f[dir][s->block_index[1] + v->blocks_off] << 1) |
             (v->mv_f[dir][s->block_index[2] + v->blocks_off] << 2) |
             (v->mv_f[dir][s->block_index[3] + v->blocks_off] << 3);
    static const uint8_t index2[16] = { 0, 0, 0, 0x23, 0, 0x13, 0x03, 0, 0, 0x12, 0x02, 0, 0x01, 0, 0, 0 };
    int opp_count = popcount4[idx];

    switch (opp_count) {
    case 0:
    case 4:
        *tx = median4(s->mv[dir][0][0], s->mv[dir][1][0], s->mv[dir][2][0], s->mv[dir][3][0]);
        *ty = median4(s->mv[dir][0][1], s->mv[dir][1][1], s->mv[dir][2][1], s->mv[dir][3][1]);
        break;
    case 1:
        *tx = mid_pred(s->mv[dir][idx < 2][0], s->mv[dir][1 + (idx < 4)][0], s->mv[dir][2 + (idx < 8)][0]);
        *ty = mid_pred(s->mv[dir][idx < 2][1], s->mv[dir][1 + (idx < 4)][1], s->mv[dir][2 + (idx < 8)][1]);
        break;
    case 3:
        *tx = mid_pred(s->mv[dir][idx > 0xd][0], s->mv[dir][1 + (idx > 0xb)][0], s->mv[dir][2 + (idx > 0x7)][0]);
        *ty = mid_pred(s->mv[dir][idx > 0xd][1], s->mv[dir][1 + (idx > 0xb)][1], s->mv[dir][2 + (idx > 0x7)][1]);
        break;
    case 2:
        *tx = (s->mv[dir][index2[idx] >> 4][0] + s->mv[dir][index2[idx] & 0xf][0]) / 2;
        *ty = (s->mv[dir][index2[idx] >> 4][1] + s->mv[dir][index2[idx] & 0xf][1]) / 2;
        break;
    }
    return opp_count;
}

static av_always_inline int get_chroma_mv(VC1Context *v, int dir, int16_t *tx, int16_t *ty)
{
    MpegEncContext *s = &v->s;
    int idx = !v->mb_type[0][s->block_index[0]] |
             (!v->mb_type[0][s->block_index[1]] << 1) |
             (!v->mb_type[0][s->block_index[2]] << 2) |
             (!v->mb_type[0][s->block_index[3]] << 3);
    static const uint8_t index2[16] = { 0, 0, 0, 0x01, 0, 0x02, 0x12, 0, 0, 0x03, 0x13, 0, 0x23, 0, 0, 0 };
    int valid_count = popcount4[idx];

    switch (valid_count) {
    case 4:
        *tx = median4(s->mv[dir][0][0], s->mv[dir][1][0], s->mv[dir][2][0], s->mv[dir][3][0]);
        *ty = median4(s->mv[dir][0][1], s->mv[dir][1][1], s->mv[dir][2][1], s->mv[dir][3][1]);
        break;
    case 3:
        *tx = mid_pred(s->mv[dir][idx > 0xd][0], s->mv[dir][1 + (idx > 0xb)][0], s->mv[dir][2 + (idx > 0x7)][0]);
        *ty = mid_pred(s->mv[dir][idx > 0xd][1], s->mv[dir][1 + (idx > 0xb)][1], s->mv[dir][2 + (idx > 0x7)][1]);
        break;
    case 2:
        *tx = (s->mv[dir][index2[idx] >> 4][0] + s->mv[dir][index2[idx] & 0xf][0]) / 2;
        *ty = (s->mv[dir][index2[idx] >> 4][1] + s->mv[dir][index2[idx] & 0xf][1]) / 2;
        break;
    default:
        return 0;
    }
    return valid_count;
}

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
    const uint8_t (*luty)[256], (*lutuv)[256];
    int use_ic;
    int interlace;
    int linesize, uvlinesize;

    if ((!v->field_mode ||
         (v->ref_field_type[dir] == 1 && v->cur_field_type == 1)) &&
        !v->s.last_pic.data[0])
        return;

    linesize   = s->cur_pic.ptr->f->linesize[0];
    uvlinesize = s->cur_pic.ptr->f->linesize[1];

    mx = s->mv[dir][0][0];
    my = s->mv[dir][0][1];

    // store motion vectors for further use in B-frames
    if (s->pict_type == AV_PICTURE_TYPE_P) {
        for (i = 0; i < 4; i++) {
            s->cur_pic.motion_val[1][s->block_index[i] + v->blocks_off][0] = mx;
            s->cur_pic.motion_val[1][s->block_index[i] + v->blocks_off][1] = my;
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
            srcY = s->cur_pic.data[0];
            srcU = s->cur_pic.data[1];
            srcV = s->cur_pic.data[2];
            luty  = v->curr_luty;
            lutuv = v->curr_lutuv;
            use_ic = *v->curr_use_ic;
            interlace = 1;
        } else {
            srcY = s->last_pic.data[0];
            srcU = s->last_pic.data[1];
            srcV = s->last_pic.data[2];
            luty  = v->last_luty;
            lutuv = v->last_lutuv;
            use_ic = v->last_use_ic;
            interlace = v->last_interlaced;
        }
    } else {
        srcY = s->next_pic.data[0];
        srcU = s->next_pic.data[1];
        srcV = s->next_pic.data[2];
        luty  = v->next_luty;
        lutuv = v->next_lutuv;
        use_ic = v->next_use_ic;
        interlace = v->next_interlaced;
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
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        if (v->fcm == ILACE_FRAME) {
            src_y = av_clip(src_y, -18 + (src_y & 1), s->avctx->coded_height + (src_y & 1));
            uvsrc_y = av_clip(uvsrc_y, -8 + (uvsrc_y & 1), (s->avctx->coded_height >> 1) + (uvsrc_y & 1));
        } else {
            src_y = av_clip(src_y, -18, s->avctx->coded_height + 1);
            uvsrc_y = av_clip(uvsrc_y, -8, s->avctx->coded_height >> 1);
        }
    }

    srcY += src_y   * s->linesize   + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode && v->ref_field_type[dir]) {
        srcY += linesize;
        srcU += uvlinesize;
        srcV += uvlinesize;
    }

    /* for grayscale we should not try to read from unknown area */
    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY) {
        srcU = s->sc.edge_emu_buffer + 18 * s->linesize;
        srcV = s->sc.edge_emu_buffer + 18 * s->linesize;
    }

    if (v->rangeredfrm || use_ic
        || s->h_edge_pos < 22 || v_edge_pos < 22
        || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx&3) - 16 - s->mspel * 3
        || (unsigned)(src_y - 1)        > v_edge_pos    - (my&3) - 16 - 3) {
        uint8_t *ubuf = s->sc.edge_emu_buffer + 19 * s->linesize;
        uint8_t *vbuf = ubuf + 9 * s->uvlinesize;
        const int k = 17 + s->mspel * 2;

        srcY -= s->mspel * (1 + s->linesize);
        if (interlace) {
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcY,
                                     linesize << 1,
                                     linesize << 1,
                                     k,
                                     v->field_mode ? k : k + 1 >> 1,
                                     src_x - s->mspel,
                                     src_y - s->mspel >> !v->field_mode,
                                     s->h_edge_pos,
                                     s->v_edge_pos >> 1);
            if (!v->field_mode)
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + linesize,
                                         srcY + linesize,
                                         linesize << 1,
                                         linesize << 1,
                                         k,
                                         k >> 1,
                                         src_x - s->mspel,
                                         src_y - s->mspel + 1 >> 1,
                                         s->h_edge_pos,
                                         s->v_edge_pos >> 1);
        } else
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcY,
                                     linesize,
                                     linesize,
                                     k,
                                     v->field_mode ? (k << 1) - 1 : k,
                                     src_x - s->mspel,
                                     v->field_mode ? 2 * (src_y - s->mspel) + v->ref_field_type[dir] :
                                                     src_y - s->mspel,
                                     s->h_edge_pos,
                                     s->v_edge_pos);
        srcY = s->sc.edge_emu_buffer;
        if (interlace) {
            s->vdsp.emulated_edge_mc(ubuf,
                                     srcU,
                                     uvlinesize << 1,
                                     uvlinesize << 1,
                                     9,
                                     v->field_mode ? 9 : 5,
                                     uvsrc_x,
                                     uvsrc_y >> !v->field_mode,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 2);
            s->vdsp.emulated_edge_mc(vbuf,
                                     srcV,
                                     uvlinesize << 1,
                                     uvlinesize << 1,
                                     9,
                                     v->field_mode ? 9 : 5,
                                     uvsrc_x,
                                     uvsrc_y >> !v->field_mode,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 2);
            if (!v->field_mode) {
                s->vdsp.emulated_edge_mc(ubuf + uvlinesize,
                                         srcU + uvlinesize,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         9,
                                         4,
                                         uvsrc_x,
                                         uvsrc_y + 1 >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
                s->vdsp.emulated_edge_mc(vbuf + uvlinesize,
                                         srcV + uvlinesize,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         9,
                                         4,
                                         uvsrc_x,
                                         uvsrc_y + 1 >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
            }
        } else {
            s->vdsp.emulated_edge_mc(ubuf,
                                     srcU,
                                     uvlinesize,
                                     uvlinesize,
                                     9,
                                     v->field_mode ? 17 : 9,
                                     uvsrc_x,
                                     v->field_mode ? 2 * uvsrc_y + v->ref_field_type[dir] : uvsrc_y,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 1);
            s->vdsp.emulated_edge_mc(vbuf,
                                     srcV,
                                     uvlinesize,
                                     uvlinesize,
                                     9,
                                     v->field_mode ? 17 : 9,
                                     uvsrc_x,
                                     v->field_mode ? 2 * uvsrc_y + v->ref_field_type[dir] : uvsrc_y,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 1);
        }
        srcU = ubuf;
        srcV = vbuf;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            vc1_scale_luma(srcY, k, s->linesize);
            vc1_scale_chroma(srcU, srcV, 9, s->uvlinesize);
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (use_ic) {
            vc1_lut_scale_luma(srcY,
                               luty[v->field_mode ? v->ref_field_type[dir] : ((0 + src_y - s->mspel) & 1)],
                               luty[v->field_mode ? v->ref_field_type[dir] : ((1 + src_y - s->mspel) & 1)],
                               k, s->linesize);
            vc1_lut_scale_chroma(srcU, srcV,
                                 lutuv[v->field_mode ? v->ref_field_type[dir] : ((0 + uvsrc_y) & 1)],
                                 lutuv[v->field_mode ? v->ref_field_type[dir] : ((1 + uvsrc_y) & 1)],
                                 9, s->uvlinesize);
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        v->vc1dsp.put_vc1_mspel_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, v->rnd);
    } else { // hpel mc - always used for luma
        dxy = (my & 2) | ((mx & 2) >> 1);
        if (!v->rnd)
            s->hdsp.put_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
        else
            s->hdsp.put_no_rnd_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
    }

    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;
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
    if (v->field_mode) {
        v->mv_f[dir][s->block_index[4] + v->mb_off] = v->cur_field_type != v->ref_field_type[dir];
        v->mv_f[dir][s->block_index[5] + v->mb_off] = v->cur_field_type != v->ref_field_type[dir];
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
    const uint8_t (*luty)[256];
    int use_ic;
    int interlace;
    int linesize;

    if ((!v->field_mode ||
         (v->ref_field_type[dir] == 1 && v->cur_field_type == 1)) &&
        !v->s.last_pic.data[0])
        return;

    linesize = s->cur_pic.ptr->f->linesize[0];

    mx = s->mv[dir][n][0];
    my = s->mv[dir][n][1];

    if (!dir) {
        if (v->field_mode && (v->cur_field_type != v->ref_field_type[dir]) && v->second_field) {
            srcY = s->cur_pic.data[0];
            luty = v->curr_luty;
            use_ic = *v->curr_use_ic;
            interlace = 1;
        } else {
            srcY = s->last_pic.data[0];
            luty = v->last_luty;
            use_ic = v->last_use_ic;
            interlace = v->last_interlaced;
        }
    } else {
        srcY = s->next_pic.data[0];
        luty = v->next_luty;
        use_ic = v->next_use_ic;
        interlace = v->next_interlaced;
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
        int opp_count = get_luma_mv(v, 0,
                                    &s->cur_pic.motion_val[1][s->block_index[0] + v->blocks_off][0],
                                    &s->cur_pic.motion_val[1][s->block_index[0] + v->blocks_off][1]);
        int k, f = opp_count > 2;
        for (k = 0; k < 4; k++)
            v->mv_f[1][s->block_index[k] + v->blocks_off] = f;
    }

    if (v->fcm == ILACE_FRAME) {  // not sure if needed for other types of picture
        int qx, qy;
        int width  = s->avctx->coded_width;
        int height = s->avctx->coded_height >> 1;
        if (s->pict_type == AV_PICTURE_TYPE_P) {
            s->cur_pic.motion_val[1][s->block_index[n] + v->blocks_off][0] = mx;
            s->cur_pic.motion_val[1][s->block_index[n] + v->blocks_off][1] = my;
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
        if (v->fcm == ILACE_FRAME)
            src_y = av_clip(src_y, -18 + (src_y & 1), s->avctx->coded_height + (src_y & 1));
        else
            src_y = av_clip(src_y, -18, s->avctx->coded_height + 1);
    }

    srcY += src_y * s->linesize + src_x;
    if (v->field_mode && v->ref_field_type[dir])
        srcY += linesize;

    if (v->rangeredfrm || use_ic
        || s->h_edge_pos < 13 || v_edge_pos < 23
        || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx & 3) - 8 - s->mspel * 2
        || (unsigned)(src_y - (s->mspel << fieldmv)) > v_edge_pos - (my & 3) - ((8 + s->mspel * 2) << fieldmv)) {
        const int k = 9 + s->mspel * 2;

        srcY -= s->mspel * (1 + (s->linesize << fieldmv));
        /* check emulate edge stride and offset */
        if (interlace) {
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcY,
                                     linesize << 1,
                                     linesize << 1,
                                     k,
                                     v->field_mode ? k : (k << fieldmv) + 1 >> 1,
                                     src_x - s->mspel,
                                     src_y - (s->mspel << fieldmv) >> !v->field_mode,
                                     s->h_edge_pos,
                                     s->v_edge_pos >> 1);
            if (!v->field_mode && !fieldmv)
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + linesize,
                                         srcY + linesize,
                                         linesize << 1,
                                         linesize << 1,
                                         k,
                                         k >> 1,
                                         src_x - s->mspel,
                                         src_y - s->mspel + 1 >> 1,
                                         s->h_edge_pos,
                                         s->v_edge_pos >> 1);
        } else
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcY,
                                     linesize,
                                     linesize,
                                     k,
                                     v->field_mode ? (k << 1) - 1 : k << fieldmv,
                                     src_x - s->mspel,
                                     v->field_mode ? 2 * (src_y - s->mspel) + v->ref_field_type[dir] :
                                                     src_y - (s->mspel << fieldmv),
                                     s->h_edge_pos,
                                     s->v_edge_pos);
        srcY = s->sc.edge_emu_buffer;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            vc1_scale_luma(srcY, k, s->linesize << fieldmv);
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (use_ic) {
            vc1_lut_scale_luma(srcY,
                               luty[v->field_mode ? v->ref_field_type[dir] : (((0<<fieldmv)+src_y - (s->mspel << fieldmv)) & 1)],
                               luty[v->field_mode ? v->ref_field_type[dir] : (((1<<fieldmv)+src_y - (s->mspel << fieldmv)) & 1)],
                               k, s->linesize << fieldmv);
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

/** Do motion compensation for 4-MV macroblock - both chroma blocks
 */
void ff_vc1_mc_4mv_chroma(VC1Context *v, int dir)
{
    MpegEncContext *s = &v->s;
    H264ChromaContext *h264chroma = &v->h264chroma;
    uint8_t *srcU, *srcV;
    int uvmx, uvmy, uvsrc_x, uvsrc_y;
    int16_t tx, ty;
    int chroma_ref_type;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;
    const uint8_t (*lutuv)[256];
    int use_ic;
    int interlace;
    int uvlinesize;

    if (!v->field_mode && !v->s.last_pic.data[0])
        return;
    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;

    /* calculate chroma MV vector from four luma MVs */
    if (!v->field_mode || !v->numref) {
        int valid_count = get_chroma_mv(v, dir, &tx, &ty);
        if (!valid_count) {
            s->cur_pic.motion_val[1][s->block_index[0] + v->blocks_off][0] = 0;
            s->cur_pic.motion_val[1][s->block_index[0] + v->blocks_off][1] = 0;
            v->luma_mv[s->mb_x][0] = v->luma_mv[s->mb_x][1] = 0;
            return; //no need to do MC for intra blocks
        }
        chroma_ref_type = v->ref_field_type[dir];
    } else {
        int opp_count = get_luma_mv(v, dir, &tx, &ty);
        chroma_ref_type = v->cur_field_type ^ (opp_count > 2);
    }
    if (v->field_mode && chroma_ref_type == 1 && v->cur_field_type == 1 && !v->s.last_pic.data[0])
        return;
    s->cur_pic.motion_val[1][s->block_index[0] + v->blocks_off][0] = tx;
    s->cur_pic.motion_val[1][s->block_index[0] + v->blocks_off][1] = ty;

    uvlinesize = s->cur_pic.ptr->f->linesize[1];

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
            srcU = s->cur_pic.data[1];
            srcV = s->cur_pic.data[2];
            lutuv = v->curr_lutuv;
            use_ic = *v->curr_use_ic;
            interlace = 1;
        } else {
            srcU = s->last_pic.data[1];
            srcV = s->last_pic.data[2];
            lutuv = v->last_lutuv;
            use_ic = v->last_use_ic;
            interlace = v->last_interlaced;
        }
    } else {
        srcU = s->next_pic.data[1];
        srcV = s->next_pic.data[2];
        lutuv = v->next_lutuv;
        use_ic = v->next_use_ic;
        interlace = v->next_interlaced;
    }

    if (!srcU) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Referenced frame missing.\n");
        return;
    }

    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode) {
        if (chroma_ref_type) {
            srcU += uvlinesize;
            srcV += uvlinesize;
        }
    }

    if (v->rangeredfrm || use_ic
        || s->h_edge_pos < 18 || v_edge_pos < 18
        || (unsigned)uvsrc_x > (s->h_edge_pos >> 1) - 9
        || (unsigned)uvsrc_y > (v_edge_pos    >> 1) - 9) {
        if (interlace) {
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcU,
                                     uvlinesize << 1,
                                     uvlinesize << 1,
                                     9,
                                     v->field_mode ? 9 : 5,
                                     uvsrc_x,
                                     uvsrc_y >> !v->field_mode,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 2);
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + 16,
                                     srcV,
                                     uvlinesize << 1,
                                     uvlinesize << 1,
                                     9,
                                     v->field_mode ? 9 : 5,
                                     uvsrc_x,
                                     uvsrc_y >> !v->field_mode,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 2);
            if (!v->field_mode) {
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + uvlinesize,
                                         srcU + uvlinesize,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         9,
                                         4,
                                         uvsrc_x,
                                         uvsrc_y + 1 >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + 16 + uvlinesize,
                                         srcV + uvlinesize,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         9,
                                         4,
                                         uvsrc_x,
                                         uvsrc_y + 1 >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
            }
        } else {
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcU,
                                     uvlinesize,
                                     uvlinesize,
                                     9,
                                     v->field_mode ? 17 : 9,
                                     uvsrc_x,
                                     v->field_mode ? 2 * uvsrc_y + chroma_ref_type : uvsrc_y,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 1);
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + 16,
                                     srcV,
                                     uvlinesize,
                                     uvlinesize,
                                     9,
                                     v->field_mode ? 17 : 9,
                                     uvsrc_x,
                                     v->field_mode ? 2 * uvsrc_y + chroma_ref_type : uvsrc_y,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 1);
        }
        srcU = s->sc.edge_emu_buffer;
        srcV = s->sc.edge_emu_buffer + 16;

        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            vc1_scale_chroma(srcU, srcV, 9, s->uvlinesize);
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (use_ic) {
            vc1_lut_scale_chroma(srcU, srcV,
                                 lutuv[v->field_mode ? chroma_ref_type : ((0 + uvsrc_y) & 1)],
                                 lutuv[v->field_mode ? chroma_ref_type : ((1 + uvsrc_y) & 1)],
                                 9, s->uvlinesize);
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
    if (v->field_mode) {
        v->mv_f[dir][s->block_index[4] + v->mb_off] = v->cur_field_type != chroma_ref_type;
        v->mv_f[dir][s->block_index[5] + v->mb_off] = v->cur_field_type != chroma_ref_type;
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
    static const uint8_t s_rndtblfield[16] = { 0, 0, 1, 2, 4, 4, 5, 6, 2, 2, 3, 8, 6, 6, 7, 12 };
    int v_dist = fieldmv ? 1 : 4; // vertical offset for lower sub-blocks
    int v_edge_pos = s->v_edge_pos >> 1;
    int use_ic;
    int interlace;
    int uvlinesize;
    const uint8_t (*lutuv)[256];

    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;

    uvlinesize = s->cur_pic.ptr->f->linesize[1];

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
        if (v->fcm == ILACE_FRAME)
            uvsrc_y = av_clip(uvsrc_y, -8 + (uvsrc_y & 1), (s->avctx->coded_height >> 1) + (uvsrc_y & 1));
        else
            uvsrc_y = av_clip(uvsrc_y, -8, s->avctx->coded_height >> 1);
        if (i < 2 ? dir : dir2) {
            srcU = s->next_pic.data[1];
            srcV = s->next_pic.data[2];
            lutuv  = v->next_lutuv;
            use_ic = v->next_use_ic;
            interlace = v->next_interlaced;
        } else {
            srcU = s->last_pic.data[1];
            srcV = s->last_pic.data[2];
            lutuv  = v->last_lutuv;
            use_ic = v->last_use_ic;
            interlace = v->last_interlaced;
        }
        if (!srcU)
            return;
        srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
        srcV += uvsrc_y * s->uvlinesize + uvsrc_x;
        uvmx_field[i] = (uvmx_field[i] & 3) << 1;
        uvmy_field[i] = (uvmy_field[i] & 3) << 1;

        if (use_ic
            || s->h_edge_pos < 10 || v_edge_pos < (5 << fieldmv)
            || (unsigned)uvsrc_x > (s->h_edge_pos >> 1) - 5
            || (unsigned)uvsrc_y > v_edge_pos - (5 << fieldmv)) {
            if (interlace) {
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                         srcU,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         5,
                                         (5 << fieldmv) + 1 >> 1,
                                         uvsrc_x,
                                         uvsrc_y >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + 16,
                                         srcV,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         5,
                                         (5 << fieldmv) + 1 >> 1,
                                         uvsrc_x,
                                         uvsrc_y >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
                if (!fieldmv) {
                    s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + uvlinesize,
                                             srcU + uvlinesize,
                                             uvlinesize << 1,
                                             uvlinesize << 1,
                                             5,
                                             2,
                                             uvsrc_x,
                                             uvsrc_y + 1 >> 1,
                                             s->h_edge_pos >> 1,
                                             s->v_edge_pos >> 2);
                    s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + 16 + uvlinesize,
                                             srcV + uvlinesize,
                                             uvlinesize << 1,
                                             uvlinesize << 1,
                                             5,
                                             2,
                                             uvsrc_x,
                                             uvsrc_y + 1 >> 1,
                                             s->h_edge_pos >> 1,
                                             s->v_edge_pos >> 2);
                }
            } else {
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                         srcU,
                                         uvlinesize,
                                         uvlinesize,
                                         5,
                                         5 << fieldmv,
                                         uvsrc_x,
                                         uvsrc_y,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 1);
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + 16,
                                         srcV,
                                         uvlinesize,
                                         uvlinesize,
                                         5,
                                         5 << fieldmv,
                                         uvsrc_x,
                                         uvsrc_y,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 1);
            }
            srcU = s->sc.edge_emu_buffer;
            srcV = s->sc.edge_emu_buffer + 16;

            /* if we deal with intensity compensation we need to scale source blocks */
            if (use_ic) {
                vc1_lut_scale_chroma(srcU, srcV,
                                     lutuv[(uvsrc_y + (0 << fieldmv)) & 1],
                                     lutuv[(uvsrc_y + (1 << fieldmv)) & 1],
                                     5, s->uvlinesize << fieldmv);
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
    int v_edge_pos = s->v_edge_pos >> v->field_mode;
    int use_ic = v->next_use_ic;
    int interlace = v->next_interlaced;
    int linesize, uvlinesize;

    if (!v->field_mode && !v->s.next_pic.data[0])
        return;

    linesize   = s->cur_pic.ptr->f->linesize[0];
    uvlinesize = s->cur_pic.ptr->f->linesize[1];

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
    srcY = s->next_pic.data[0];
    srcU = s->next_pic.data[1];
    srcV = s->next_pic.data[2];

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
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        if (v->fcm == ILACE_FRAME) {
            src_y = av_clip(src_y, -18 + (src_y & 1), s->avctx->coded_height + (src_y & 1));
            uvsrc_y = av_clip(uvsrc_y, -8 + (uvsrc_y & 1), (s->avctx->coded_height >> 1) + (uvsrc_y & 1));
        } else {
            src_y = av_clip(src_y, -18, s->avctx->coded_height + 1);
            uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
        }
    }

    srcY += src_y   * s->linesize   + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode && v->ref_field_type[1]) {
        srcY += linesize;
        srcU += uvlinesize;
        srcV += uvlinesize;
    }

    /* for grayscale we should not try to read from unknown area */
    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY) {
        srcU = s->sc.edge_emu_buffer + 18 * s->linesize;
        srcV = s->sc.edge_emu_buffer + 18 * s->linesize;
    }

    if (v->rangeredfrm || s->h_edge_pos < 22 || v_edge_pos < 22 || use_ic
        || (unsigned)(src_x - 1) > s->h_edge_pos - (mx & 3) - 16 - 3
        || (unsigned)(src_y - 1) > v_edge_pos    - (my & 3) - 16 - 3) {
        uint8_t *ubuf = s->sc.edge_emu_buffer + 19 * s->linesize;
        uint8_t *vbuf = ubuf + 9 * s->uvlinesize;
        const int k = 17 + s->mspel * 2;

        srcY -= s->mspel * (1 + s->linesize);
        if (interlace) {
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcY,
                                     linesize << 1,
                                     linesize << 1,
                                     k,
                                     v->field_mode ? k : (k + 1 >> 1),
                                     src_x - s->mspel,
                                     src_y - s->mspel >> !v->field_mode,
                                     s->h_edge_pos,
                                     s->v_edge_pos >> 1);
            if (!v->field_mode)
                s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer + linesize,
                                         srcY + linesize,
                                         linesize << 1,
                                         linesize << 1,
                                         k,
                                         k >> 1,
                                         src_x - s->mspel,
                                         src_y - s->mspel + 1 >> 1,
                                         s->h_edge_pos,
                                         s->v_edge_pos >> 1);
        } else
            s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer,
                                     srcY,
                                     linesize,
                                     linesize,
                                     k,
                                     v->field_mode ? (k << 1) - 1 : k,
                                     src_x - s->mspel,
                                     v->field_mode ? 2 * (src_y - s->mspel) + v->ref_field_type[1] :
                                                     src_y - s->mspel,
                                     s->h_edge_pos,
                                     s->v_edge_pos);
        srcY = s->sc.edge_emu_buffer;
        if (interlace) {
            s->vdsp.emulated_edge_mc(ubuf,
                                     srcU,
                                     uvlinesize << 1,
                                     uvlinesize << 1,
                                     9,
                                     v->field_mode ? 9 : 5,
                                     uvsrc_x,
                                     uvsrc_y >> !v->field_mode,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 2);
            s->vdsp.emulated_edge_mc(vbuf,
                                     srcV,
                                     uvlinesize << 1,
                                     uvlinesize << 1,
                                     9,
                                     v->field_mode ? 9 : 5,
                                     uvsrc_x,
                                     uvsrc_y >> !v->field_mode,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 2);
            if (!v->field_mode) {
                s->vdsp.emulated_edge_mc(ubuf + uvlinesize,
                                         srcU + uvlinesize,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         9,
                                         4,
                                         uvsrc_x,
                                         uvsrc_y + 1 >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
                s->vdsp.emulated_edge_mc(vbuf + uvlinesize,
                                         srcV + uvlinesize,
                                         uvlinesize << 1,
                                         uvlinesize << 1,
                                         9,
                                         4,
                                         uvsrc_x,
                                         uvsrc_y + 1 >> 1,
                                         s->h_edge_pos >> 1,
                                         s->v_edge_pos >> 2);
            }
        } else {
            s->vdsp.emulated_edge_mc(ubuf,
                                     srcU,
                                     uvlinesize,
                                     uvlinesize,
                                     9,
                                     v->field_mode ? 17 : 9,
                                     uvsrc_x,
                                     v->field_mode ? 2 * uvsrc_y + v->ref_field_type[1] : uvsrc_y,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 1);
            s->vdsp.emulated_edge_mc(vbuf,
                                     srcV,
                                     uvlinesize,
                                     uvlinesize,
                                     9,
                                     v->field_mode ? 17 : 9,
                                     uvsrc_x,
                                     v->field_mode ? 2 * uvsrc_y + v->ref_field_type[1] : uvsrc_y,
                                     s->h_edge_pos >> 1,
                                     s->v_edge_pos >> 1);
        }
        srcU = ubuf;
        srcV = vbuf;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            vc1_scale_luma(srcY, k, s->linesize);
            vc1_scale_chroma(srcU, srcV, 9, s->uvlinesize);
        }

        if (use_ic) {
            const uint8_t (*luty )[256] = v->next_luty;
            const uint8_t (*lutuv)[256] = v->next_lutuv;
            vc1_lut_scale_luma(srcY,
                               luty[v->field_mode ? v->ref_field_type[1] : ((0+src_y - s->mspel) & 1)],
                               luty[v->field_mode ? v->ref_field_type[1] : ((1+src_y - s->mspel) & 1)],
                               k, s->linesize);
            vc1_lut_scale_chroma(srcU, srcV,
                                 lutuv[v->field_mode ? v->ref_field_type[1] : ((0+uvsrc_y) & 1)],
                                 lutuv[v->field_mode ? v->ref_field_type[1] : ((1+uvsrc_y) & 1)],
                                 9, s->uvlinesize);
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        v->vc1dsp.avg_vc1_mspel_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, v->rnd);
    } else { // hpel mc
        dxy = (my & 2) | ((mx & 2) >> 1);

        if (!v->rnd)
            s->hdsp.avg_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
        else
            s->hdsp.avg_no_rnd_pixels_tab[dxy](s->dest[0], srcY, s->linesize, 16);
    }

    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;
    /* Chroma MC always uses qpel bilinear */
    uvmx = (uvmx & 3) << 1;
    uvmy = (uvmy & 3) << 1;
    if (!v->rnd) {
        h264chroma->avg_h264_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        h264chroma->avg_h264_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    } else {
        v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1], srcU, s->uvlinesize, 8, uvmx, uvmy);
        v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2], srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}
