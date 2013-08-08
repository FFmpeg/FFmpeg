/*
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
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
 * VP5 and VP6 compatible video decoder (common features)
 */

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "h264chroma.h"
#include "vp56.h"
#include "vp56data.h"


void ff_vp56_init_dequant(VP56Context *s, int quantizer)
{
    s->quantizer = quantizer;
    s->dequant_dc = ff_vp56_dc_dequant[quantizer] << 2;
    s->dequant_ac = ff_vp56_ac_dequant[quantizer] << 2;
}

static int vp56_get_vectors_predictors(VP56Context *s, int row, int col,
                                       VP56Frame ref_frame)
{
    int nb_pred = 0;
    VP56mv vect[2] = {{0,0}, {0,0}};
    int pos, offset;
    VP56mv mvp;

    for (pos=0; pos<12; pos++) {
        mvp.x = col + ff_vp56_candidate_predictor_pos[pos][0];
        mvp.y = row + ff_vp56_candidate_predictor_pos[pos][1];
        if (mvp.x < 0 || mvp.x >= s->mb_width ||
            mvp.y < 0 || mvp.y >= s->mb_height)
            continue;
        offset = mvp.x + s->mb_width*mvp.y;

        if (ff_vp56_reference_frame[s->macroblocks[offset].type] != ref_frame)
            continue;
        if ((s->macroblocks[offset].mv.x == vect[0].x &&
             s->macroblocks[offset].mv.y == vect[0].y) ||
            (s->macroblocks[offset].mv.x == 0 &&
             s->macroblocks[offset].mv.y == 0))
            continue;

        vect[nb_pred++] = s->macroblocks[offset].mv;
        if (nb_pred > 1) {
            nb_pred = -1;
            break;
        }
        s->vector_candidate_pos = pos;
    }

    s->vector_candidate[0] = vect[0];
    s->vector_candidate[1] = vect[1];

    return nb_pred+1;
}

static void vp56_parse_mb_type_models(VP56Context *s)
{
    VP56RangeCoder *c = &s->c;
    VP56Model *model = s->modelp;
    int i, ctx, type;

    for (ctx=0; ctx<3; ctx++) {
        if (vp56_rac_get_prob(c, 174)) {
            int idx = vp56_rac_gets(c, 4);
            memcpy(model->mb_types_stats[ctx],
                   ff_vp56_pre_def_mb_type_stats[idx][ctx],
                   sizeof(model->mb_types_stats[ctx]));
        }
        if (vp56_rac_get_prob(c, 254)) {
            for (type=0; type<10; type++) {
                for(i=0; i<2; i++) {
                    if (vp56_rac_get_prob(c, 205)) {
                        int delta, sign = vp56_rac_get(c);

                        delta = vp56_rac_get_tree(c, ff_vp56_pmbtm_tree,
                                                  ff_vp56_mb_type_model_model);
                        if (!delta)
                            delta = 4 * vp56_rac_gets(c, 7);
                        model->mb_types_stats[ctx][type][i] += (delta ^ -sign) + sign;
                    }
                }
            }
        }
    }

    /* compute MB type probability tables based on previous MB type */
    for (ctx=0; ctx<3; ctx++) {
        int p[10];

        for (type=0; type<10; type++)
            p[type] = 100 * model->mb_types_stats[ctx][type][1];

        for (type=0; type<10; type++) {
            int p02, p34, p0234, p17, p56, p89, p5689, p156789;

            /* conservative MB type probability */
            model->mb_type[ctx][type][0] = 255 - (255 * model->mb_types_stats[ctx][type][0]) / (1 + model->mb_types_stats[ctx][type][0] + model->mb_types_stats[ctx][type][1]);

            p[type] = 0;    /* same MB type => weight is null */

            /* binary tree parsing probabilities */
            p02 = p[0] + p[2];
            p34 = p[3] + p[4];
            p0234 = p02 + p34;
            p17 = p[1] + p[7];
            p56 = p[5] + p[6];
            p89 = p[8] + p[9];
            p5689 = p56 + p89;
            p156789 = p17 + p5689;

            model->mb_type[ctx][type][1] = 1 + 255 * p0234/(1+p0234+p156789);
            model->mb_type[ctx][type][2] = 1 + 255 * p02  / (1+p0234);
            model->mb_type[ctx][type][3] = 1 + 255 * p17  / (1+p156789);
            model->mb_type[ctx][type][4] = 1 + 255 * p[0] / (1+p02);
            model->mb_type[ctx][type][5] = 1 + 255 * p[3] / (1+p34);
            model->mb_type[ctx][type][6] = 1 + 255 * p[1] / (1+p17);
            model->mb_type[ctx][type][7] = 1 + 255 * p56  / (1+p5689);
            model->mb_type[ctx][type][8] = 1 + 255 * p[5] / (1+p56);
            model->mb_type[ctx][type][9] = 1 + 255 * p[8] / (1+p89);

            /* restore initial value */
            p[type] = 100 * model->mb_types_stats[ctx][type][1];
        }
    }
}

static VP56mb vp56_parse_mb_type(VP56Context *s,
                                 VP56mb prev_type, int ctx)
{
    uint8_t *mb_type_model = s->modelp->mb_type[ctx][prev_type];
    VP56RangeCoder *c = &s->c;

    if (vp56_rac_get_prob(c, mb_type_model[0]))
        return prev_type;
    else
        return vp56_rac_get_tree(c, ff_vp56_pmbt_tree, mb_type_model);
}

static void vp56_decode_4mv(VP56Context *s, int row, int col)
{
    VP56mv mv = {0,0};
    int type[4];
    int b;

    /* parse each block type */
    for (b=0; b<4; b++) {
        type[b] = vp56_rac_gets(&s->c, 2);
        if (type[b])
            type[b]++;  /* only returns 0, 2, 3 or 4 (all INTER_PF) */
    }

    /* get vectors */
    for (b=0; b<4; b++) {
        switch (type[b]) {
            case VP56_MB_INTER_NOVEC_PF:
                s->mv[b] = (VP56mv) {0,0};
                break;
            case VP56_MB_INTER_DELTA_PF:
                s->parse_vector_adjustment(s, &s->mv[b]);
                break;
            case VP56_MB_INTER_V1_PF:
                s->mv[b] = s->vector_candidate[0];
                break;
            case VP56_MB_INTER_V2_PF:
                s->mv[b] = s->vector_candidate[1];
                break;
        }
        mv.x += s->mv[b].x;
        mv.y += s->mv[b].y;
    }

    /* this is the one selected for the whole MB for prediction */
    s->macroblocks[row * s->mb_width + col].mv = s->mv[3];

    /* chroma vectors are average luma vectors */
    if (s->avctx->codec->id == AV_CODEC_ID_VP5) {
        s->mv[4].x = s->mv[5].x = RSHIFT(mv.x,2);
        s->mv[4].y = s->mv[5].y = RSHIFT(mv.y,2);
    } else {
        s->mv[4] = s->mv[5] = (VP56mv) {mv.x/4, mv.y/4};
    }
}

static VP56mb vp56_decode_mv(VP56Context *s, int row, int col)
{
    VP56mv *mv, vect = {0,0};
    int ctx, b;

    ctx = vp56_get_vectors_predictors(s, row, col, VP56_FRAME_PREVIOUS);
    s->mb_type = vp56_parse_mb_type(s, s->mb_type, ctx);
    s->macroblocks[row * s->mb_width + col].type = s->mb_type;

    switch (s->mb_type) {
        case VP56_MB_INTER_V1_PF:
            mv = &s->vector_candidate[0];
            break;

        case VP56_MB_INTER_V2_PF:
            mv = &s->vector_candidate[1];
            break;

        case VP56_MB_INTER_V1_GF:
            vp56_get_vectors_predictors(s, row, col, VP56_FRAME_GOLDEN);
            mv = &s->vector_candidate[0];
            break;

        case VP56_MB_INTER_V2_GF:
            vp56_get_vectors_predictors(s, row, col, VP56_FRAME_GOLDEN);
            mv = &s->vector_candidate[1];
            break;

        case VP56_MB_INTER_DELTA_PF:
            s->parse_vector_adjustment(s, &vect);
            mv = &vect;
            break;

        case VP56_MB_INTER_DELTA_GF:
            vp56_get_vectors_predictors(s, row, col, VP56_FRAME_GOLDEN);
            s->parse_vector_adjustment(s, &vect);
            mv = &vect;
            break;

        case VP56_MB_INTER_4V:
            vp56_decode_4mv(s, row, col);
            return s->mb_type;

        default:
            mv = &vect;
            break;
    }

    s->macroblocks[row*s->mb_width + col].mv = *mv;

    /* same vector for all blocks */
    for (b=0; b<6; b++)
        s->mv[b] = *mv;

    return s->mb_type;
}

static void vp56_add_predictors_dc(VP56Context *s, VP56Frame ref_frame)
{
    int idx = s->idct_scantable[0];
    int b;

    for (b=0; b<6; b++) {
        VP56RefDc *ab = &s->above_blocks[s->above_block_idx[b]];
        VP56RefDc *lb = &s->left_block[ff_vp56_b6to4[b]];
        int count = 0;
        int dc = 0;
        int i;

        if (ref_frame == lb->ref_frame) {
            dc += lb->dc_coeff;
            count++;
        }
        if (ref_frame == ab->ref_frame) {
            dc += ab->dc_coeff;
            count++;
        }
        if (s->avctx->codec->id == AV_CODEC_ID_VP5)
            for (i=0; i<2; i++)
                if (count < 2 && ref_frame == ab[-1+2*i].ref_frame) {
                    dc += ab[-1+2*i].dc_coeff;
                    count++;
                }
        if (count == 0)
            dc = s->prev_dc[ff_vp56_b2p[b]][ref_frame];
        else if (count == 2)
            dc /= 2;

        s->block_coeff[b][idx] += dc;
        s->prev_dc[ff_vp56_b2p[b]][ref_frame] = s->block_coeff[b][idx];
        ab->dc_coeff = s->block_coeff[b][idx];
        ab->ref_frame = ref_frame;
        lb->dc_coeff = s->block_coeff[b][idx];
        lb->ref_frame = ref_frame;
        s->block_coeff[b][idx] *= s->dequant_dc;
    }
}

static void vp56_deblock_filter(VP56Context *s, uint8_t *yuv,
                                int stride, int dx, int dy)
{
    int t = ff_vp56_filter_threshold[s->quantizer];
    if (dx)  s->vp56dsp.edge_filter_hor(yuv +         10-dx , stride, t);
    if (dy)  s->vp56dsp.edge_filter_ver(yuv + stride*(10-dy), stride, t);
}

static void vp56_mc(VP56Context *s, int b, int plane, uint8_t *src,
                    int stride, int x, int y)
{
    uint8_t *dst = s->frames[VP56_FRAME_CURRENT]->data[plane] + s->block_offset[b];
    uint8_t *src_block;
    int src_offset;
    int overlap_offset = 0;
    int mask = s->vp56_coord_div[b] - 1;
    int deblock_filtering = s->deblock_filtering;
    int dx;
    int dy;

    if (s->avctx->skip_loop_filter >= AVDISCARD_ALL ||
        (s->avctx->skip_loop_filter >= AVDISCARD_NONKEY
         && !s->frames[VP56_FRAME_CURRENT]->key_frame))
        deblock_filtering = 0;

    dx = s->mv[b].x / s->vp56_coord_div[b];
    dy = s->mv[b].y / s->vp56_coord_div[b];

    if (b >= 4) {
        x /= 2;
        y /= 2;
    }
    x += dx - 2;
    y += dy - 2;

    if (x<0 || x+12>=s->plane_width[plane] ||
        y<0 || y+12>=s->plane_height[plane]) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                            src + s->block_offset[b] + (dy-2)*stride + (dx-2),
                            stride, 12, 12, x, y,
                            s->plane_width[plane],
                            s->plane_height[plane]);
        src_block = s->edge_emu_buffer;
        src_offset = 2 + 2*stride;
    } else if (deblock_filtering) {
        /* only need a 12x12 block, but there is no such dsp function, */
        /* so copy a 16x12 block */
        s->hdsp.put_pixels_tab[0][0](s->edge_emu_buffer,
                                     src + s->block_offset[b] + (dy-2)*stride + (dx-2),
                                     stride, 12);
        src_block = s->edge_emu_buffer;
        src_offset = 2 + 2*stride;
    } else {
        src_block = src;
        src_offset = s->block_offset[b] + dy*stride + dx;
    }

    if (deblock_filtering)
        vp56_deblock_filter(s, src_block, stride, dx&7, dy&7);

    if (s->mv[b].x & mask)
        overlap_offset += (s->mv[b].x > 0) ? 1 : -1;
    if (s->mv[b].y & mask)
        overlap_offset += (s->mv[b].y > 0) ? stride : -stride;

    if (overlap_offset) {
        if (s->filter)
            s->filter(s, dst, src_block, src_offset, src_offset+overlap_offset,
                      stride, s->mv[b], mask, s->filter_selection, b<4);
        else
            s->vp3dsp.put_no_rnd_pixels_l2(dst, src_block+src_offset,
                                           src_block+src_offset+overlap_offset,
                                           stride, 8);
    } else {
        s->hdsp.put_pixels_tab[1][0](dst, src_block+src_offset, stride, 8);
    }
}

static void vp56_decode_mb(VP56Context *s, int row, int col, int is_alpha)
{
    AVFrame *frame_current, *frame_ref;
    VP56mb mb_type;
    VP56Frame ref_frame;
    int b, ab, b_max, plane, off;

    if (s->frames[VP56_FRAME_CURRENT]->key_frame)
        mb_type = VP56_MB_INTRA;
    else
        mb_type = vp56_decode_mv(s, row, col);
    ref_frame = ff_vp56_reference_frame[mb_type];

    s->parse_coeff(s);

    vp56_add_predictors_dc(s, ref_frame);

    frame_current = s->frames[VP56_FRAME_CURRENT];
    frame_ref = s->frames[ref_frame];
    if (mb_type != VP56_MB_INTRA && !frame_ref->data[0])
        return;

    ab = 6*is_alpha;
    b_max = 6 - 2*is_alpha;

    switch (mb_type) {
        case VP56_MB_INTRA:
            for (b=0; b<b_max; b++) {
                plane = ff_vp56_b2p[b+ab];
                s->vp3dsp.idct_put(frame_current->data[plane] + s->block_offset[b],
                                s->stride[plane], s->block_coeff[b]);
            }
            break;

        case VP56_MB_INTER_NOVEC_PF:
        case VP56_MB_INTER_NOVEC_GF:
            for (b=0; b<b_max; b++) {
                plane = ff_vp56_b2p[b+ab];
                off = s->block_offset[b];
                s->hdsp.put_pixels_tab[1][0](frame_current->data[plane] + off,
                                             frame_ref->data[plane] + off,
                                             s->stride[plane], 8);
                s->vp3dsp.idct_add(frame_current->data[plane] + off,
                                s->stride[plane], s->block_coeff[b]);
            }
            break;

        case VP56_MB_INTER_DELTA_PF:
        case VP56_MB_INTER_V1_PF:
        case VP56_MB_INTER_V2_PF:
        case VP56_MB_INTER_DELTA_GF:
        case VP56_MB_INTER_4V:
        case VP56_MB_INTER_V1_GF:
        case VP56_MB_INTER_V2_GF:
            for (b=0; b<b_max; b++) {
                int x_off = b==1 || b==3 ? 8 : 0;
                int y_off = b==2 || b==3 ? 8 : 0;
                plane = ff_vp56_b2p[b+ab];
                vp56_mc(s, b, plane, frame_ref->data[plane], s->stride[plane],
                        16*col+x_off, 16*row+y_off);
                s->vp3dsp.idct_add(frame_current->data[plane] + s->block_offset[b],
                                s->stride[plane], s->block_coeff[b]);
            }
            break;
    }

    if (is_alpha) {
        s->block_coeff[4][0] = 0;
        s->block_coeff[5][0] = 0;
    }
}

static int vp56_size_changed(VP56Context *s)
{
    AVCodecContext *avctx = s->avctx;
    int stride = s->frames[VP56_FRAME_CURRENT]->linesize[0];
    int i;

    s->plane_width[0]  = s->plane_width[3]  = avctx->coded_width;
    s->plane_width[1]  = s->plane_width[2]  = avctx->coded_width/2;
    s->plane_height[0] = s->plane_height[3] = avctx->coded_height;
    s->plane_height[1] = s->plane_height[2] = avctx->coded_height/2;

    for (i=0; i<4; i++)
        s->stride[i] = s->flip * s->frames[VP56_FRAME_CURRENT]->linesize[i];

    s->mb_width  = (avctx->coded_width +15) / 16;
    s->mb_height = (avctx->coded_height+15) / 16;

    if (s->mb_width > 1000 || s->mb_height > 1000) {
        avcodec_set_dimensions(avctx, 0, 0);
        av_log(avctx, AV_LOG_ERROR, "picture too big\n");
        return -1;
    }

    av_reallocp_array(&s->above_blocks, 4*s->mb_width+6,
                      sizeof(*s->above_blocks));
    av_reallocp_array(&s->macroblocks, s->mb_width*s->mb_height,
                      sizeof(*s->macroblocks));
    av_free(s->edge_emu_buffer_alloc);
    s->edge_emu_buffer_alloc = av_malloc(16*stride);
    s->edge_emu_buffer = s->edge_emu_buffer_alloc;
    if (!s->above_blocks || !s->macroblocks || !s->edge_emu_buffer_alloc)
        return AVERROR(ENOMEM);
    if (s->flip < 0)
        s->edge_emu_buffer += 15 * stride;

    if (s->alpha_context)
        return vp56_size_changed(s->alpha_context);

    return 0;
}

static int ff_vp56_decode_mbs(AVCodecContext *avctx, void *, int, int);

int ff_vp56_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                         AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    VP56Context *s = avctx->priv_data;
    AVFrame *const p = s->frames[VP56_FRAME_CURRENT];
    int remaining_buf_size = avpkt->size;
    int av_uninit(alpha_offset);
    int i, res;
    int ret;

    if (s->has_alpha) {
        if (remaining_buf_size < 3)
            return -1;
        alpha_offset = bytestream_get_be24(&buf);
        remaining_buf_size -= 3;
        if (remaining_buf_size < alpha_offset)
            return -1;
    }

    res = s->parse_header(s, buf, remaining_buf_size);
    if (res < 0)
        return res;

    if (res == VP56_SIZE_CHANGE) {
        for (i = 0; i < 4; i++) {
            av_frame_unref(s->frames[i]);
            if (s->alpha_context)
                av_frame_unref(s->alpha_context->frames[i]);
        }
    }

    if (ff_get_buffer(avctx, p, AV_GET_BUFFER_FLAG_REF) < 0)
        return -1;

    if (avctx->pix_fmt == AV_PIX_FMT_YUVA420P) {
        av_frame_unref(s->alpha_context->frames[VP56_FRAME_CURRENT]);
        if ((ret = av_frame_ref(s->alpha_context->frames[VP56_FRAME_CURRENT], p)) < 0) {
            av_frame_unref(p);
            return ret;
        }
    }

    if (res == VP56_SIZE_CHANGE) {
        if (vp56_size_changed(s)) {
            av_frame_unref(p);
            return -1;
        }
    }

    if (avctx->pix_fmt == AV_PIX_FMT_YUVA420P) {
        int bak_w = avctx->width;
        int bak_h = avctx->height;
        int bak_cw = avctx->coded_width;
        int bak_ch = avctx->coded_height;
        buf += alpha_offset;
        remaining_buf_size -= alpha_offset;

        res = s->alpha_context->parse_header(s->alpha_context, buf, remaining_buf_size);
        if (res != 0) {
            if(res==VP56_SIZE_CHANGE) {
                av_log(avctx, AV_LOG_ERROR, "Alpha reconfiguration\n");
                avctx->width  = bak_w;
                avctx->height = bak_h;
                avctx->coded_width  = bak_cw;
                avctx->coded_height = bak_ch;
            }
            av_frame_unref(p);
            return -1;
        }
    }

    avctx->execute2(avctx, ff_vp56_decode_mbs, 0, 0, (avctx->pix_fmt == AV_PIX_FMT_YUVA420P) + 1);

    if ((res = av_frame_ref(data, p)) < 0)
        return res;
    *got_frame = 1;

    return avpkt->size;
}

static int ff_vp56_decode_mbs(AVCodecContext *avctx, void *data,
                              int jobnr, int threadnr)
{
    VP56Context *s0 = avctx->priv_data;
    int is_alpha = (jobnr == 1);
    VP56Context *s = is_alpha ? s0->alpha_context : s0;
    AVFrame *const p = s->frames[VP56_FRAME_CURRENT];
    int mb_row, mb_col, mb_row_flip, mb_offset = 0;
    int block, y, uv, stride_y, stride_uv;
    int res;

    if (p->key_frame) {
        p->pict_type = AV_PICTURE_TYPE_I;
        s->default_models_init(s);
        for (block=0; block<s->mb_height*s->mb_width; block++)
            s->macroblocks[block].type = VP56_MB_INTRA;
    } else {
        p->pict_type = AV_PICTURE_TYPE_P;
        vp56_parse_mb_type_models(s);
        s->parse_vector_models(s);
        s->mb_type = VP56_MB_INTER_NOVEC_PF;
    }

    if (s->parse_coeff_models(s))
        goto next;

    memset(s->prev_dc, 0, sizeof(s->prev_dc));
    s->prev_dc[1][VP56_FRAME_CURRENT] = 128;
    s->prev_dc[2][VP56_FRAME_CURRENT] = 128;

    for (block=0; block < 4*s->mb_width+6; block++) {
        s->above_blocks[block].ref_frame = VP56_FRAME_NONE;
        s->above_blocks[block].dc_coeff = 0;
        s->above_blocks[block].not_null_dc = 0;
    }
    s->above_blocks[2*s->mb_width + 2].ref_frame = VP56_FRAME_CURRENT;
    s->above_blocks[3*s->mb_width + 4].ref_frame = VP56_FRAME_CURRENT;

    stride_y  = p->linesize[0];
    stride_uv = p->linesize[1];

    if (s->flip < 0)
        mb_offset = 7;

    /* main macroblocks loop */
    for (mb_row=0; mb_row<s->mb_height; mb_row++) {
        if (s->flip < 0)
            mb_row_flip = s->mb_height - mb_row - 1;
        else
            mb_row_flip = mb_row;

        for (block=0; block<4; block++) {
            s->left_block[block].ref_frame = VP56_FRAME_NONE;
            s->left_block[block].dc_coeff = 0;
            s->left_block[block].not_null_dc = 0;
        }
        memset(s->coeff_ctx, 0, sizeof(s->coeff_ctx));
        memset(s->coeff_ctx_last, 24, sizeof(s->coeff_ctx_last));

        s->above_block_idx[0] = 1;
        s->above_block_idx[1] = 2;
        s->above_block_idx[2] = 1;
        s->above_block_idx[3] = 2;
        s->above_block_idx[4] = 2*s->mb_width + 2 + 1;
        s->above_block_idx[5] = 3*s->mb_width + 4 + 1;

        s->block_offset[s->frbi] = (mb_row_flip*16 + mb_offset) * stride_y;
        s->block_offset[s->srbi] = s->block_offset[s->frbi] + 8*stride_y;
        s->block_offset[1] = s->block_offset[0] + 8;
        s->block_offset[3] = s->block_offset[2] + 8;
        s->block_offset[4] = (mb_row_flip*8 + mb_offset) * stride_uv;
        s->block_offset[5] = s->block_offset[4];

        for (mb_col=0; mb_col<s->mb_width; mb_col++) {
            vp56_decode_mb(s, mb_row, mb_col, is_alpha);

            for (y=0; y<4; y++) {
                s->above_block_idx[y] += 2;
                s->block_offset[y] += 16;
            }

            for (uv=4; uv<6; uv++) {
                s->above_block_idx[uv] += 1;
                s->block_offset[uv] += 8;
            }
        }
    }

next:
    if (p->key_frame || s->golden_frame) {
        av_frame_unref(s->frames[VP56_FRAME_GOLDEN]);
        if ((res = av_frame_ref(s->frames[VP56_FRAME_GOLDEN], p)) < 0)
            return res;
    }

    av_frame_unref(s->frames[VP56_FRAME_PREVIOUS]);
    FFSWAP(AVFrame *, s->frames[VP56_FRAME_CURRENT],
                      s->frames[VP56_FRAME_PREVIOUS]);
    return 0;
}

av_cold int ff_vp56_init(AVCodecContext *avctx, int flip, int has_alpha)
{
    VP56Context *s = avctx->priv_data;
    return ff_vp56_init_context(avctx, s, flip, has_alpha);
}

av_cold int ff_vp56_init_context(AVCodecContext *avctx, VP56Context *s,
                                  int flip, int has_alpha)
{
    int i;

    s->avctx = avctx;
    avctx->pix_fmt = has_alpha ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;
    if (avctx->skip_alpha) avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ff_h264chroma_init(&s->h264chroma, 8);
    ff_hpeldsp_init(&s->hdsp, avctx->flags);
    ff_videodsp_init(&s->vdsp, 8);
    ff_vp3dsp_init(&s->vp3dsp, avctx->flags);
    ff_vp56dsp_init(&s->vp56dsp, avctx->codec->id);
    for (i = 0; i < 64; i++) {
#define T(x) (x >> 3) | ((x & 7) << 3)
        s->idct_scantable[i] = T(ff_zigzag_direct[i]);
#undef T
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->frames); i++) {
        s->frames[i] = av_frame_alloc();
        if (!s->frames[i]) {
            ff_vp56_free(avctx);
            return AVERROR(ENOMEM);
        }
    }
    s->edge_emu_buffer_alloc = NULL;

    s->above_blocks = NULL;
    s->macroblocks = NULL;
    s->quantizer = -1;
    s->deblock_filtering = 1;
    s->golden_frame = 0;

    s->filter = NULL;

    s->has_alpha = has_alpha;

    s->modelp = &s->model;

    if (flip) {
        s->flip = -1;
        s->frbi = 2;
        s->srbi = 0;
    } else {
        s->flip = 1;
        s->frbi = 0;
        s->srbi = 2;
    }

    return 0;
}

av_cold int ff_vp56_free(AVCodecContext *avctx)
{
    VP56Context *s = avctx->priv_data;
    return ff_vp56_free_context(s);
}

av_cold int ff_vp56_free_context(VP56Context *s)
{
    int i;

    av_freep(&s->above_blocks);
    av_freep(&s->macroblocks);
    av_freep(&s->edge_emu_buffer_alloc);

    for (i = 0; i < FF_ARRAY_ELEMS(s->frames); i++)
        av_frame_free(&s->frames[i]);

    return 0;
}
