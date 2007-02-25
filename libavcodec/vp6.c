/**
 * @file vp6.c
 * VP6 compatible video decoder
 *
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *
 * The VP6F decoder accept an optional 1 byte extradata. It is composed of:
 *  - upper 4bits: difference between encoded width and visible width
 *  - lower 4bits: difference between encoded height and visible height
 */

#include <stdlib.h>

#include "avcodec.h"
#include "dsputil.h"
#include "bitstream.h"
#include "mpegvideo.h"

#include "vp56.h"
#include "vp56data.h"
#include "vp6data.h"


static int vp6_parse_header(vp56_context_t *s, uint8_t *buf, int buf_size,
                            int *golden_frame)
{
    vp56_range_coder_t *c = &s->c;
    int parse_filter_info = 0;
    int coeff_offset = 0;
    int vrt_shift = 0;
    int sub_version;
    int rows, cols;
    int res = 1;
    int separated_coeff = buf[0] & 1;

    s->framep[VP56_FRAME_CURRENT]->key_frame = !(buf[0] & 0x80);
    vp56_init_dequant(s, (buf[0] >> 1) & 0x3F);

    if (s->framep[VP56_FRAME_CURRENT]->key_frame) {
        sub_version = buf[1] >> 3;
        if (sub_version > 8)
            return 0;
        s->filter_header = buf[1] & 0x06;
        if (buf[1] & 1) {
            av_log(s->avctx, AV_LOG_ERROR, "interlacing not supported\n");
            return 0;
        }
        if (separated_coeff || !s->filter_header) {
            coeff_offset = AV_RB16(buf+2) - 2;
            buf += 2;
            buf_size -= 2;
        }

        rows = buf[2];  /* number of stored macroblock rows */
        cols = buf[3];  /* number of stored macroblock cols */
        /* buf[4] is number of displayed macroblock rows */
        /* buf[5] is number of displayed macroblock cols */

        if (16*cols != s->avctx->coded_width ||
            16*rows != s->avctx->coded_height) {
            avcodec_set_dimensions(s->avctx, 16*cols, 16*rows);
            if (s->avctx->extradata_size == 1) {
                s->avctx->width  -= s->avctx->extradata[0] >> 4;
                s->avctx->height -= s->avctx->extradata[0] & 0x0F;
            }
            res = 2;
        }

        vp56_init_range_decoder(c, buf+6, buf_size-6);
        vp56_rac_gets(c, 2);

        parse_filter_info = s->filter_header;
        if (sub_version < 8)
            vrt_shift = 5;
        s->sub_version = sub_version;
    } else {
        if (!s->sub_version)
            return 0;

        if (separated_coeff || !s->filter_header) {
            coeff_offset = AV_RB16(buf+1) - 2;
            buf += 2;
            buf_size -= 2;
        }
        vp56_init_range_decoder(c, buf+1, buf_size-1);

        *golden_frame = vp56_rac_get(c);
        if (s->filter_header) {
            s->deblock_filtering = vp56_rac_get(c);
            if (s->deblock_filtering)
                vp56_rac_get(c);
            if (s->sub_version > 7)
                parse_filter_info = vp56_rac_get(c);
        }
    }

    if (parse_filter_info) {
        if (vp56_rac_get(c)) {
            s->filter_mode = 2;
            s->sample_variance_threshold = vp56_rac_gets(c, 5) << vrt_shift;
            s->max_vector_length = 2 << vp56_rac_gets(c, 3);
        } else if (vp56_rac_get(c)) {
            s->filter_mode = 1;
        } else {
            s->filter_mode = 0;
        }
        if (s->sub_version > 7)
            s->filter_selection = vp56_rac_gets(c, 4);
        else
            s->filter_selection = 16;
    }

    vp56_rac_get(c);

    if (coeff_offset) {
        vp56_init_range_decoder(&s->cc, buf+coeff_offset,
                                buf_size-coeff_offset);
        s->ccp = &s->cc;
    } else {
        s->ccp = &s->c;
    }

    return res;
}

static void vp6_coeff_order_table_init(vp56_context_t *s)
{
    int i, pos, idx = 1;

    s->coeff_index_to_pos[0] = 0;
    for (i=0; i<16; i++)
        for (pos=1; pos<64; pos++)
            if (s->coeff_reorder[pos] == i)
                s->coeff_index_to_pos[idx++] = pos;
}

static void vp6_default_models_init(vp56_context_t *s)
{
    s->vector_model_dct[0] = 0xA2;
    s->vector_model_dct[1] = 0xA4;
    s->vector_model_sig[0] = 0x80;
    s->vector_model_sig[1] = 0x80;

    memcpy(s->mb_types_stats, vp56_def_mb_types_stats, sizeof(s->mb_types_stats));
    memcpy(s->vector_model_fdv, vp6_def_fdv_vector_model, sizeof(s->vector_model_fdv));
    memcpy(s->vector_model_pdv, vp6_def_pdv_vector_model, sizeof(s->vector_model_pdv));
    memcpy(s->coeff_model_runv, vp6_def_runv_coeff_model, sizeof(s->coeff_model_runv));
    memcpy(s->coeff_reorder, vp6_def_coeff_reorder, sizeof(s->coeff_reorder));

    vp6_coeff_order_table_init(s);
}

static void vp6_parse_vector_models(vp56_context_t *s)
{
    vp56_range_coder_t *c = &s->c;
    int comp, node;

    for (comp=0; comp<2; comp++) {
        if (vp56_rac_get_prob(c, vp6_sig_dct_pct[comp][0]))
            s->vector_model_dct[comp] = vp56_rac_gets_nn(c, 7);
        if (vp56_rac_get_prob(c, vp6_sig_dct_pct[comp][1]))
            s->vector_model_sig[comp] = vp56_rac_gets_nn(c, 7);
    }

    for (comp=0; comp<2; comp++)
        for (node=0; node<7; node++)
            if (vp56_rac_get_prob(c, vp6_pdv_pct[comp][node]))
                s->vector_model_pdv[comp][node] = vp56_rac_gets_nn(c, 7);

    for (comp=0; comp<2; comp++)
        for (node=0; node<8; node++)
            if (vp56_rac_get_prob(c, vp6_fdv_pct[comp][node]))
                s->vector_model_fdv[comp][node] = vp56_rac_gets_nn(c, 7);
}

static void vp6_parse_coeff_models(vp56_context_t *s)
{
    vp56_range_coder_t *c = &s->c;
    int def_prob[11];
    int node, cg, ctx, pos;
    int ct;    /* code type */
    int pt;    /* plane type (0 for Y, 1 for U or V) */

    memset(def_prob, 0x80, sizeof(def_prob));

    for (pt=0; pt<2; pt++)
        for (node=0; node<11; node++)
            if (vp56_rac_get_prob(c, vp6_dccv_pct[pt][node])) {
                def_prob[node] = vp56_rac_gets_nn(c, 7);
                s->coeff_model_dccv[pt][node] = def_prob[node];
            } else if (s->framep[VP56_FRAME_CURRENT]->key_frame) {
                s->coeff_model_dccv[pt][node] = def_prob[node];
            }

    if (vp56_rac_get(c)) {
        for (pos=1; pos<64; pos++)
            if (vp56_rac_get_prob(c, vp6_coeff_reorder_pct[pos]))
                s->coeff_reorder[pos] = vp56_rac_gets(c, 4);
        vp6_coeff_order_table_init(s);
    }

    for (cg=0; cg<2; cg++)
        for (node=0; node<14; node++)
            if (vp56_rac_get_prob(c, vp6_runv_pct[cg][node]))
                s->coeff_model_runv[cg][node] = vp56_rac_gets_nn(c, 7);

    for (ct=0; ct<3; ct++)
        for (pt=0; pt<2; pt++)
            for (cg=0; cg<6; cg++)
                for (node=0; node<11; node++)
                    if (vp56_rac_get_prob(c, vp6_ract_pct[ct][pt][cg][node])) {
                        def_prob[node] = vp56_rac_gets_nn(c, 7);
                        s->coeff_model_ract[pt][ct][cg][node] = def_prob[node];
                    } else if (s->framep[VP56_FRAME_CURRENT]->key_frame) {
                        s->coeff_model_ract[pt][ct][cg][node] = def_prob[node];
                    }

    /* coeff_model_dcct is a linear combination of coeff_model_dccv */
    for (pt=0; pt<2; pt++)
        for (ctx=0; ctx<3; ctx++)
            for (node=0; node<5; node++)
                s->coeff_model_dcct[pt][ctx][node] = av_clip(((s->coeff_model_dccv[pt][node] * vp6_dccv_lc[ctx][node][0] + 128) >> 8) + vp6_dccv_lc[ctx][node][1], 1, 255);
}

static void vp6_parse_vector_adjustment(vp56_context_t *s, vp56_mv_t *vect)
{
    vp56_range_coder_t *c = &s->c;
    int comp;

    *vect = (vp56_mv_t) {0,0};
    if (s->vector_candidate_pos < 2)
        *vect = s->vector_candidate[0];

    for (comp=0; comp<2; comp++) {
        int i, delta = 0;

        if (vp56_rac_get_prob(c, s->vector_model_dct[comp])) {
            static const uint8_t prob_order[] = {0, 1, 2, 7, 6, 5, 4};
            for (i=0; i<sizeof(prob_order); i++) {
                int j = prob_order[i];
                delta |= vp56_rac_get_prob(c, s->vector_model_fdv[comp][j])<<j;
            }
            if (delta & 0xF0)
                delta |= vp56_rac_get_prob(c, s->vector_model_fdv[comp][3])<<3;
            else
                delta |= 8;
        } else {
            delta = vp56_rac_get_tree(c, vp56_pva_tree,
                                      s->vector_model_pdv[comp]);
        }

        if (delta && vp56_rac_get_prob(c, s->vector_model_sig[comp]))
            delta = -delta;

        if (!comp)
            vect->x += delta;
        else
            vect->y += delta;
    }
}

static void vp6_parse_coeff(vp56_context_t *s)
{
    vp56_range_coder_t *c = s->ccp;
    uint8_t *permute = s->scantable.permutated;
    uint8_t *model, *model2, *model3;
    int coeff, sign, coeff_idx;
    int b, i, cg, idx, ctx;
    int pt = 0;    /* plane type (0 for Y, 1 for U or V) */

    for (b=0; b<6; b++) {
        int ct = 1;    /* code type */
        int run = 1;

        if (b > 3) pt = 1;

        ctx = s->left_block[vp56_b6to4[b]].not_null_dc
              + s->above_blocks[s->above_block_idx[b]].not_null_dc;
        model = s->coeff_model_dccv[pt];
        model2 = s->coeff_model_dcct[pt][ctx];

        for (coeff_idx=0; coeff_idx<64; ) {
            if ((coeff_idx>1 && ct==0) || vp56_rac_get_prob(c, model2[0])) {
                /* parse a coeff */
                if (coeff_idx == 0) {
                    s->left_block[vp56_b6to4[b]].not_null_dc = 1;
                    s->above_blocks[s->above_block_idx[b]].not_null_dc = 1;
                }

                if (vp56_rac_get_prob(c, model2[2])) {
                    if (vp56_rac_get_prob(c, model2[3])) {
                        idx = vp56_rac_get_tree(c, vp56_pc_tree, model);
                        coeff = vp56_coeff_bias[idx];
                        for (i=vp56_coeff_bit_length[idx]; i>=0; i--)
                            coeff += vp56_rac_get_prob(c, vp56_coeff_parse_table[idx][i]) << i;
                    } else {
                        if (vp56_rac_get_prob(c, model2[4]))
                            coeff = 3 + vp56_rac_get_prob(c, model[5]);
                        else
                            coeff = 2;
                    }
                    ct = 2;
                } else {
                    ct = 1;
                    coeff = 1;
                }
                sign = vp56_rac_get(c);
                coeff = (coeff ^ -sign) + sign;
                if (coeff_idx)
                    coeff *= s->dequant_ac;
                idx = s->coeff_index_to_pos[coeff_idx];
                s->block_coeff[b][permute[idx]] = coeff;
                run = 1;
            } else {
                /* parse a run */
                ct = 0;
                if (coeff_idx == 0) {
                    s->left_block[vp56_b6to4[b]].not_null_dc = 0;
                    s->above_blocks[s->above_block_idx[b]].not_null_dc = 0;
                } else {
                    if (!vp56_rac_get_prob(c, model2[1]))
                        break;

                    model3 = s->coeff_model_runv[coeff_idx >= 6];
                    run = vp56_rac_get_tree(c, vp6_pcr_tree, model3);
                    if (!run)
                        for (run=9, i=0; i<6; i++)
                            run += vp56_rac_get_prob(c, model3[i+8]) << i;
                }
            }

            cg = vp6_coeff_groups[coeff_idx+=run];
            model = model2 = s->coeff_model_ract[pt][ct][cg];
        }
    }
}

static int vp6_adjust(int v, int t)
{
    int V = v, s = v >> 31;
    V ^= s;
    V -= s;
    if (V-t-1 >= (unsigned)(t-1))
        return v;
    V = 2*t - V;
    V += s;
    V ^= s;
    return V;
}

static int vp6_block_variance(uint8_t *src, int stride)
{
    int sum = 0, square_sum = 0;
    int y, x;

    for (y=0; y<8; y+=2) {
        for (x=0; x<8; x+=2) {
            sum += src[x];
            square_sum += src[x]*src[x];
        }
        src += 2*stride;
    }
    return (16*square_sum - sum*sum) >> 8;
}

static void vp6_filter_hv2(vp56_context_t *s, uint8_t *dst, uint8_t *src,
                           int stride, int delta, int16_t weight)
{
    s->dsp.put_pixels_tab[1][0](dst, src, stride, 8);
    s->dsp.biweight_h264_pixels_tab[3](dst, src+delta, stride, 2,
                                       8-weight, weight, 0);
}

static void vp6_filter_hv4(uint8_t *dst, uint8_t *src, int stride,
                           int delta, const int16_t *weights)
{
    int x, y;

    for (y=0; y<8; y++) {
        for (x=0; x<8; x++) {
            dst[x] = av_clip_uint8((  src[x-delta  ] * weights[0]
                                 + src[x        ] * weights[1]
                                 + src[x+delta  ] * weights[2]
                                 + src[x+2*delta] * weights[3] + 64) >> 7);
        }
        src += stride;
        dst += stride;
    }
}

static void vp6_filter_diag2(vp56_context_t *s, uint8_t *dst, uint8_t *src,
                             int stride, int h_weight, int v_weight)
{
    uint8_t *tmp = s->edge_emu_buffer+16;
    int x, xmax;

    s->dsp.put_pixels_tab[1][0](tmp, src, stride, 8);
    s->dsp.biweight_h264_pixels_tab[3](tmp, src+1, stride, 2,
                                       8-h_weight, h_weight, 0);
    /* we need a 8x9 block to do vertical filter, so compute one more line */
    for (x=8*stride, xmax=x+8; x<xmax; x++)
        tmp[x] = (src[x]*(8-h_weight) + src[x+1]*h_weight + 4) >> 3;

    s->dsp.put_pixels_tab[1][0](dst, tmp, stride, 8);
    s->dsp.biweight_h264_pixels_tab[3](dst, tmp+stride, stride, 2,
                                       8-v_weight, v_weight, 0);
}

static void vp6_filter_diag4(uint8_t *dst, uint8_t *src, int stride,
                             const int16_t *h_weights,const int16_t *v_weights)
{
    int x, y;
    int tmp[8*11];
    int *t = tmp;

    src -= stride;

    for (y=0; y<11; y++) {
        for (x=0; x<8; x++) {
            t[x] = av_clip_uint8((  src[x-1] * h_weights[0]
                               + src[x  ] * h_weights[1]
                               + src[x+1] * h_weights[2]
                               + src[x+2] * h_weights[3] + 64) >> 7);
        }
        src += stride;
        t += 8;
    }

    t = tmp + 8;
    for (y=0; y<8; y++) {
        for (x=0; x<8; x++) {
            dst[x] = av_clip_uint8((  t[x-8 ] * v_weights[0]
                                 + t[x   ] * v_weights[1]
                                 + t[x+8 ] * v_weights[2]
                                 + t[x+16] * v_weights[3] + 64) >> 7);
        }
        dst += stride;
        t += 8;
    }
}

static void vp6_filter(vp56_context_t *s, uint8_t *dst, uint8_t *src,
                       int offset1, int offset2, int stride,
                       vp56_mv_t mv, int mask, int select, int luma)
{
    int filter4 = 0;
    int x8 = mv.x & mask;
    int y8 = mv.y & mask;

    if (luma) {
        x8 *= 2;
        y8 *= 2;
        filter4 = s->filter_mode;
        if (filter4 == 2) {
            if (s->max_vector_length &&
                (FFABS(mv.x) > s->max_vector_length ||
                 FFABS(mv.y) > s->max_vector_length)) {
                filter4 = 0;
            } else if (s->sample_variance_threshold
                       && (vp6_block_variance(src+offset1, stride)
                           < s->sample_variance_threshold)) {
                filter4 = 0;
            }
        }
    }

    if ((y8 && (offset2-offset1)*s->flip<0) || (!y8 && offset1 > offset2)) {
        offset1 = offset2;
    }

    if (filter4) {
        if (!y8) {                      /* left or right combine */
            vp6_filter_hv4(dst, src+offset1, stride, 1,
                           vp6_block_copy_filter[select][x8]);
        } else if (!x8) {               /* above or below combine */
            vp6_filter_hv4(dst, src+offset1, stride, stride,
                           vp6_block_copy_filter[select][y8]);
        } else if ((mv.x^mv.y) >> 31) { /* lower-left or upper-right combine */
            vp6_filter_diag4(dst, src+offset1-1, stride,
                             vp6_block_copy_filter[select][x8],
                             vp6_block_copy_filter[select][y8]);
        } else {                        /* lower-right or upper-left combine */
            vp6_filter_diag4(dst, src+offset1, stride,
                             vp6_block_copy_filter[select][x8],
                             vp6_block_copy_filter[select][y8]);
        }
    } else {
        if (!y8) {                      /* left or right combine */
            vp6_filter_hv2(s, dst, src+offset1, stride, 1, x8);
        } else if (!x8) {               /* above or below combine */
            vp6_filter_hv2(s, dst, src+offset1, stride, stride, y8);
        } else if ((mv.x^mv.y) >> 31) { /* lower-left or upper-right combine */
            vp6_filter_diag2(s, dst, src+offset1-1, stride, x8, y8);
        } else {                        /* lower-right or upper-left combine */
            vp6_filter_diag2(s, dst, src+offset1, stride, x8, y8);
        }
    }
}

static int vp6_decode_init(AVCodecContext *avctx)
{
    vp56_context_t *s = avctx->priv_data;

    vp56_init(s, avctx, avctx->codec->id == CODEC_ID_VP6);
    s->vp56_coord_div = vp6_coord_div;
    s->parse_vector_adjustment = vp6_parse_vector_adjustment;
    s->adjust = vp6_adjust;
    s->filter = vp6_filter;
    s->parse_coeff = vp6_parse_coeff;
    s->default_models_init = vp6_default_models_init;
    s->parse_vector_models = vp6_parse_vector_models;
    s->parse_coeff_models = vp6_parse_coeff_models;
    s->parse_header = vp6_parse_header;

    return 0;
}

AVCodec vp6_decoder = {
    "vp6",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VP6,
    sizeof(vp56_context_t),
    vp6_decode_init,
    NULL,
    vp56_free,
    vp56_decode_frame,
};

/* flash version, not flipped upside-down */
AVCodec vp6f_decoder = {
    "vp6f",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VP6F,
    sizeof(vp56_context_t),
    vp6_decode_init,
    NULL,
    vp56_free,
    vp56_decode_frame,
};
