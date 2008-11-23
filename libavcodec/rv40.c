/*
 * RV40 decoder
 * Copyright (c) 2007 Konstantin Shishkov
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
 * @file rv40.c
 * RV40 decoder
 */

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "golomb.h"

#include "rv34.h"
#include "rv40vlc2.h"
#include "rv40data.h"

static VLC aic_top_vlc;
static VLC aic_mode1_vlc[AIC_MODE1_NUM], aic_mode2_vlc[AIC_MODE2_NUM];
static VLC ptype_vlc[NUM_PTYPE_VLCS], btype_vlc[NUM_BTYPE_VLCS];

/**
 * Initialize all tables.
 */
static av_cold void rv40_init_tables()
{
    int i;

    init_vlc(&aic_top_vlc, AIC_TOP_BITS, AIC_TOP_SIZE,
             rv40_aic_top_vlc_bits,  1, 1,
             rv40_aic_top_vlc_codes, 1, 1, INIT_VLC_USE_STATIC);
    for(i = 0; i < AIC_MODE1_NUM; i++){
        // Every tenth VLC table is empty
        if((i % 10) == 9) continue;
        init_vlc(&aic_mode1_vlc[i], AIC_MODE1_BITS, AIC_MODE1_SIZE,
                 aic_mode1_vlc_bits[i],  1, 1,
                 aic_mode1_vlc_codes[i], 1, 1, INIT_VLC_USE_STATIC);
    }
    for(i = 0; i < AIC_MODE2_NUM; i++){
        init_vlc(&aic_mode2_vlc[i], AIC_MODE2_BITS, AIC_MODE2_SIZE,
                 aic_mode2_vlc_bits[i],  1, 1,
                 aic_mode2_vlc_codes[i], 2, 2, INIT_VLC_USE_STATIC);
    }
    for(i = 0; i < NUM_PTYPE_VLCS; i++)
         init_vlc_sparse(&ptype_vlc[i], PTYPE_VLC_BITS, PTYPE_VLC_SIZE,
                         ptype_vlc_bits[i],  1, 1,
                         ptype_vlc_codes[i], 1, 1,
                         ptype_vlc_syms,     1, 1, INIT_VLC_USE_STATIC);
    for(i = 0; i < NUM_BTYPE_VLCS; i++)
         init_vlc_sparse(&btype_vlc[i], BTYPE_VLC_BITS, BTYPE_VLC_SIZE,
                         btype_vlc_bits[i],  1, 1,
                         btype_vlc_codes[i], 1, 1,
                         btype_vlc_syms,     1, 1, INIT_VLC_USE_STATIC);
}

/**
 * Get stored dimension from bitstream.
 *
 * If the width/height is the standard one then it's coded as a 3-bit index.
 * Otherwise it is coded as escaped 8-bit portions.
 */
static int get_dimension(GetBitContext *gb, const int *dim)
{
    int t   = get_bits(gb, 3);
    int val = dim[t];
    if(val < 0)
        val = dim[get_bits1(gb) - val];
    if(!val){
        do{
            t = get_bits(gb, 8);
            val += t << 2;
        }while(t == 0xFF);
    }
    return val;
}

/**
 * Get encoded picture size - usually this is called from rv40_parse_slice_header.
 */
static void rv40_parse_picture_size(GetBitContext *gb, int *w, int *h)
{
    *w = get_dimension(gb, rv40_standard_widths);
    *h = get_dimension(gb, rv40_standard_heights);
}

static int rv40_parse_slice_header(RV34DecContext *r, GetBitContext *gb, SliceInfo *si)
{
    int mb_bits;
    int w = r->s.width, h = r->s.height;
    int mb_size;

    memset(si, 0, sizeof(SliceInfo));
    if(get_bits1(gb))
        return -1;
    si->type = get_bits(gb, 2);
    if(si->type == 1) si->type = 0;
    si->quant = get_bits(gb, 5);
    if(get_bits(gb, 2))
        return -1;
    si->vlc_set = get_bits(gb, 2);
    skip_bits1(gb);
    si->pts = get_bits(gb, 13);
    if(!si->type || !get_bits1(gb))
        rv40_parse_picture_size(gb, &w, &h);
    if(avcodec_check_dimensions(r->s.avctx, w, h) < 0)
        return -1;
    si->width  = w;
    si->height = h;
    mb_size = ((w + 15) >> 4) * ((h + 15) >> 4);
    mb_bits = ff_rv34_get_start_offset(gb, mb_size);
    si->start = get_bits(gb, mb_bits);

    return 0;
}

/**
 * Decode 4x4 intra types array.
 */
static int rv40_decode_intra_types(RV34DecContext *r, GetBitContext *gb, int8_t *dst)
{
    MpegEncContext *s = &r->s;
    int i, j, k, v;
    int A, B, C;
    int pattern;
    int8_t *ptr;

    for(i = 0; i < 4; i++, dst += s->b4_stride){
        if(!i && s->first_slice_line){
            pattern = get_vlc2(gb, aic_top_vlc.table, AIC_TOP_BITS, 1);
            dst[0] = (pattern >> 2) & 2;
            dst[1] = (pattern >> 1) & 2;
            dst[2] =  pattern       & 2;
            dst[3] = (pattern << 1) & 2;
            continue;
        }
        ptr = dst;
        for(j = 0; j < 4; j++){
            /* Coefficients are read using VLC chosen by the prediction pattern
             * The first one (used for retrieving a pair of coefficients) is
             * constructed from the top, top right and left coefficients
             * The second one (used for retrieving only one coefficient) is
             * top + 10 * left.
             */
            A = ptr[-s->b4_stride + 1]; // it won't be used for the last coefficient in a row
            B = ptr[-s->b4_stride];
            C = ptr[-1];
            pattern = A + (B << 4) + (C << 8);
            for(k = 0; k < MODE2_PATTERNS_NUM; k++)
                if(pattern == rv40_aic_table_index[k])
                    break;
            if(j < 3 && k < MODE2_PATTERNS_NUM){ //pattern is found, decoding 2 coefficients
                v = get_vlc2(gb, aic_mode2_vlc[k].table, AIC_MODE2_BITS, 2);
                *ptr++ = v/9;
                *ptr++ = v%9;
                j++;
            }else{
                if(B != -1 && C != -1)
                    v = get_vlc2(gb, aic_mode1_vlc[B + C*10].table, AIC_MODE1_BITS, 1);
                else{ // tricky decoding
                    v = 0;
                    switch(C){
                    case -1: // code 0 -> 1, 1 -> 0
                        if(B < 2)
                            v = get_bits1(gb) ^ 1;
                        break;
                    case  0:
                    case  2: // code 0 -> 2, 1 -> 0
                        v = (get_bits1(gb) ^ 1) << 1;
                        break;
                    }
                }
                *ptr++ = v;
            }
        }
    }
    return 0;
}

/**
 * Decode macroblock information.
 */
static int rv40_decode_mb_info(RV34DecContext *r)
{
    MpegEncContext *s = &r->s;
    GetBitContext *gb = &s->gb;
    int q, i;
    int prev_type = 0;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int blocks[RV34_MB_TYPES] = {0};
    int count = 0;

    if(!r->s.mb_skip_run)
        r->s.mb_skip_run = svq3_get_ue_golomb(gb) + 1;

    if(--r->s.mb_skip_run)
         return RV34_MB_SKIP;

    if(r->avail_cache[5-1])
        blocks[r->mb_type[mb_pos - 1]]++;
    if(r->avail_cache[5-4]){
        blocks[r->mb_type[mb_pos - s->mb_stride]]++;
        if(r->avail_cache[5-2])
            blocks[r->mb_type[mb_pos - s->mb_stride + 1]]++;
        if(r->avail_cache[5-5])
            blocks[r->mb_type[mb_pos - s->mb_stride - 1]]++;
    }

    for(i = 0; i < RV34_MB_TYPES; i++){
        if(blocks[i] > count){
            count = blocks[i];
            prev_type = i;
        }
    }
    if(s->pict_type == FF_P_TYPE){
        prev_type = block_num_to_ptype_vlc_num[prev_type];
        q = get_vlc2(gb, ptype_vlc[prev_type].table, PTYPE_VLC_BITS, 1);
        if(q < PBTYPE_ESCAPE)
            return q;
        q = get_vlc2(gb, ptype_vlc[prev_type].table, PTYPE_VLC_BITS, 1);
        av_log(s->avctx, AV_LOG_ERROR, "Dquant for P-frame\n");
    }else{
        prev_type = block_num_to_btype_vlc_num[prev_type];
        q = get_vlc2(gb, btype_vlc[prev_type].table, BTYPE_VLC_BITS, 1);
        if(q < PBTYPE_ESCAPE)
            return q;
        q = get_vlc2(gb, btype_vlc[prev_type].table, BTYPE_VLC_BITS, 1);
        av_log(s->avctx, AV_LOG_ERROR, "Dquant for B-frame\n");
    }
    return 0;
}

#define CLIP_SYMM(a, b) av_clip(a, -(b), b)
/**
 * weaker deblocking very similar to the one described in 4.4.2 of JVT-A003r1
 */
static inline void rv40_weak_loop_filter(uint8_t *src, const int step,
                                         const int filter_p1, const int filter_q1,
                                         const int alpha, const int beta,
                                         const int lim_p0q0,
                                         const int lim_q1, const int lim_p1,
                                         const int diff_p1p0, const int diff_q1q0,
                                         const int diff_p1p2, const int diff_q1q2)
{
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int t, u, diff;

    t = src[0*step] - src[-1*step];
    if(!t)
        return;
    u = (alpha * FFABS(t)) >> 7;
    if(u > 3 - (filter_p1 && filter_q1))
        return;

    t <<= 2;
    if(filter_p1 && filter_q1)
        t += src[-2*step] - src[1*step];
    diff = CLIP_SYMM((t + 4) >> 3, lim_p0q0);
    src[-1*step] = cm[src[-1*step] + diff];
    src[ 0*step] = cm[src[ 0*step] - diff];
    if(FFABS(diff_p1p2) <= beta && filter_p1){
        t = (diff_p1p0 + diff_p1p2 - diff) >> 1;
        src[-2*step] = cm[src[-2*step] - CLIP_SYMM(t, lim_p1)];
    }
    if(FFABS(diff_q1q2) <= beta && filter_q1){
        t = (diff_q1q0 + diff_q1q2 + diff) >> 1;
        src[ 1*step] = cm[src[ 1*step] - CLIP_SYMM(t, lim_q1)];
    }
}

static inline void rv40_adaptive_loop_filter(uint8_t *src, const int step,
                                             const int stride, const int dmode,
                                             const int lim_q1, const int lim_p1,
                                             const int alpha,
                                             const int beta, const int beta2,
                                             const int chroma, const int edge)
{
    int diff_p1p0[4], diff_q1q0[4], diff_p1p2[4], diff_q1q2[4];
    int sum_p1p0 = 0, sum_q1q0 = 0, sum_p1p2 = 0, sum_q1q2 = 0;
    uint8_t *ptr;
    int flag_strong0 = 1, flag_strong1 = 1;
    int filter_p1, filter_q1;
    int i;
    int lims;

    for(i = 0, ptr = src; i < 4; i++, ptr += stride){
        diff_p1p0[i] = ptr[-2*step] - ptr[-1*step];
        diff_q1q0[i] = ptr[ 1*step] - ptr[ 0*step];
        sum_p1p0 += diff_p1p0[i];
        sum_q1q0 += diff_q1q0[i];
    }
    filter_p1 = FFABS(sum_p1p0) < (beta<<2);
    filter_q1 = FFABS(sum_q1q0) < (beta<<2);
    if(!filter_p1 && !filter_q1)
        return;

    for(i = 0, ptr = src; i < 4; i++, ptr += stride){
        diff_p1p2[i] = ptr[-2*step] - ptr[-3*step];
        diff_q1q2[i] = ptr[ 1*step] - ptr[ 2*step];
        sum_p1p2 += diff_p1p2[i];
        sum_q1q2 += diff_q1q2[i];
    }

    if(edge){
        flag_strong0 = filter_p1 && (FFABS(sum_p1p2) < beta2);
        flag_strong1 = filter_q1 && (FFABS(sum_q1q2) < beta2);
    }else{
        flag_strong0 = flag_strong1 = 0;
    }

    lims = filter_p1 + filter_q1 + ((lim_q1 + lim_p1) >> 1) + 1;
    if(flag_strong0 && flag_strong1){ /* strong filtering */
        for(i = 0; i < 4; i++, src += stride){
            int sflag, p0, q0, p1, q1;
            int t = src[0*step] - src[-1*step];

            if(!t) continue;
            sflag = (alpha * FFABS(t)) >> 7;
            if(sflag > 1) continue;

            p0 = (25*src[-3*step] + 26*src[-2*step]
                + 26*src[-1*step]
                + 26*src[ 0*step] + 25*src[ 1*step] + rv40_dither_l[dmode + i]) >> 7;
            q0 = (25*src[-2*step] + 26*src[-1*step]
                + 26*src[ 0*step]
                + 26*src[ 1*step] + 25*src[ 2*step] + rv40_dither_r[dmode + i]) >> 7;
            if(sflag){
                p0 = av_clip(p0, src[-1*step] - lims, src[-1*step] + lims);
                q0 = av_clip(q0, src[ 0*step] - lims, src[ 0*step] + lims);
            }
            p1 = (25*src[-4*step] + 26*src[-3*step]
                + 26*src[-2*step]
                + 26*p0           + 25*src[ 0*step] + rv40_dither_l[dmode + i]) >> 7;
            q1 = (25*src[-1*step] + 26*q0
                + 26*src[ 1*step]
                + 26*src[ 2*step] + 25*src[ 3*step] + rv40_dither_r[dmode + i]) >> 7;
            if(sflag){
                p1 = av_clip(p1, src[-2*step] - lims, src[-2*step] + lims);
                q1 = av_clip(q1, src[ 1*step] - lims, src[ 1*step] + lims);
            }
            src[-2*step] = p1;
            src[-1*step] = p0;
            src[ 0*step] = q0;
            src[ 1*step] = q1;
            if(!chroma){
                src[-3*step] = (25*src[-1*step] + 26*src[-2*step] + 51*src[-3*step] + 26*src[-4*step] + 64) >> 7;
                src[ 2*step] = (25*src[ 0*step] + 26*src[ 1*step] + 51*src[ 2*step] + 26*src[ 3*step] + 64) >> 7;
            }
        }
    }else if(filter_p1 && filter_q1){
        for(i = 0; i < 4; i++, src += stride)
            rv40_weak_loop_filter(src, step, 1, 1, alpha, beta, lims, lim_q1, lim_p1,
                                  diff_p1p0[i], diff_q1q0[i], diff_p1p2[i], diff_q1q2[i]);
    }else{
        for(i = 0; i < 4; i++, src += stride)
            rv40_weak_loop_filter(src, step, filter_p1, filter_q1,
                                  alpha, beta, lims>>1, lim_q1>>1, lim_p1>>1,
                                  diff_p1p0[i], diff_q1q0[i], diff_p1p2[i], diff_q1q2[i]);
    }
}

static void rv40_v_loop_filter(uint8_t *src, int stride, int dmode,
                               int lim_q1, int lim_p1,
                               int alpha, int beta, int beta2, int chroma, int edge){
    rv40_adaptive_loop_filter(src, 1, stride, dmode, lim_q1, lim_p1,
                              alpha, beta, beta2, chroma, edge);
}
static void rv40_h_loop_filter(uint8_t *src, int stride, int dmode,
                               int lim_q1, int lim_p1,
                               int alpha, int beta, int beta2, int chroma, int edge){
    rv40_adaptive_loop_filter(src, stride, 1, dmode, lim_q1, lim_p1,
                              alpha, beta, beta2, chroma, edge);
}

/**
 * Initialize decoder.
 */
static av_cold int rv40_decode_init(AVCodecContext *avctx)
{
    RV34DecContext *r = avctx->priv_data;

    r->rv30 = 0;
    ff_rv34_decode_init(avctx);
    if(!aic_top_vlc.bits)
        rv40_init_tables();
    r->parse_slice_header = rv40_parse_slice_header;
    r->decode_intra_types = rv40_decode_intra_types;
    r->decode_mb_info     = rv40_decode_mb_info;
    r->luma_dc_quant_i = rv40_luma_dc_quant[0];
    r->luma_dc_quant_p = rv40_luma_dc_quant[1];
    return 0;
}

AVCodec rv40_decoder = {
    "rv40",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RV40,
    sizeof(RV34DecContext),
    rv40_decode_init,
    NULL,
    ff_rv34_decode_end,
    ff_rv34_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    .long_name = NULL_IF_CONFIG_SMALL("RealVideo 4.0"),
};
