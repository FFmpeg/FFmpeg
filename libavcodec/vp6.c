/*
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * VP6 compatible video decoder
 *
 * The VP6F decoder accepts an optional 1 byte extradata. It is composed of:
 *  - upper 4 bits: difference between encoded width and visible width
 *  - lower 4 bits: difference between encoded height and visible height
 */

#include <stdlib.h>

#include "avcodec.h"
#include "get_bits.h"
#include "huffman.h"
#include "internal.h"

#include "vp56.h"
#include "vp56data.h"
#include "vp6data.h"

#define VP6_MAX_HUFF_SIZE 12

static void vp6_parse_coeff(VP56Context *s);
static void vp6_parse_coeff_huffman(VP56Context *s);

static int vp6_parse_header(VP56Context *s, const uint8_t *buf, int buf_size,
                            int *golden_frame)
{
    VP56RangeCoder *c = &s->c;
    int parse_filter_info = 0;
    int coeff_offset = 0;
    int vrt_shift = 0;
    int sub_version;
    int rows, cols;
    int res = 0;
    int separated_coeff = buf[0] & 1;

    s->frames[VP56_FRAME_CURRENT]->key_frame = !(buf[0] & 0x80);
    ff_vp56_init_dequant(s, (buf[0] >> 1) & 0x3F);

    if (s->frames[VP56_FRAME_CURRENT]->key_frame) {
        sub_version = buf[1] >> 3;
        if (sub_version > 8)
            return AVERROR_INVALIDDATA;
        s->filter_header = buf[1] & 0x06;
        if (buf[1] & 1) {
            avpriv_report_missing_feature(s->avctx, "Interlacing");
            return AVERROR_PATCHWELCOME;
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
        if (!rows || !cols) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid size %dx%d\n", cols << 4, rows << 4);
            return AVERROR_INVALIDDATA;
        }

        if (!s->macroblocks || /* first frame */
            16*cols != s->avctx->coded_width ||
            16*rows != s->avctx->coded_height) {
            if (s->avctx->extradata_size == 0 &&
                FFALIGN(s->avctx->width,  16) == 16 * cols &&
                FFALIGN(s->avctx->height, 16) == 16 * rows) {
                // We assume this is properly signalled container cropping,
                // in an F4V file. Just set the coded_width/height, don't
                // touch the cropped ones.
                s->avctx->coded_width  = 16 * cols;
                s->avctx->coded_height = 16 * rows;
            } else {
                int ret = ff_set_dimensions(s->avctx, 16 * cols, 16 * rows);
                if (ret < 0)
                    return ret;

                if (s->avctx->extradata_size == 1) {
                    s->avctx->width  -= s->avctx->extradata[0] >> 4;
                    s->avctx->height -= s->avctx->extradata[0] & 0x0F;
                }
            }
            res = VP56_SIZE_CHANGE;
        }

        ff_vp56_init_range_decoder(c, buf+6, buf_size-6);
        vp56_rac_gets(c, 2);

        parse_filter_info = s->filter_header;
        if (sub_version < 8)
            vrt_shift = 5;
        s->sub_version = sub_version;
    } else {
        if (!s->sub_version || !s->avctx->coded_width || !s->avctx->coded_height)
            return AVERROR_INVALIDDATA;

        if (separated_coeff || !s->filter_header) {
            coeff_offset = AV_RB16(buf+1) - 2;
            buf += 2;
            buf_size -= 2;
        }
        ff_vp56_init_range_decoder(c, buf+1, buf_size-1);

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

    s->use_huffman = vp56_rac_get(c);

    s->parse_coeff = vp6_parse_coeff;
    if (coeff_offset) {
        buf      += coeff_offset;
        buf_size -= coeff_offset;
        if (buf_size < 0) {
            if (s->frames[VP56_FRAME_CURRENT]->key_frame)
                ff_set_dimensions(s->avctx, 0, 0);
            return AVERROR_INVALIDDATA;
        }
        if (s->use_huffman) {
            s->parse_coeff = vp6_parse_coeff_huffman;
            init_get_bits(&s->gb, buf, buf_size<<3);
        } else {
            ff_vp56_init_range_decoder(&s->cc, buf, buf_size);
            s->ccp = &s->cc;
        }
    } else {
        s->ccp = &s->c;
    }

    return res;
}

static void vp6_coeff_order_table_init(VP56Context *s)
{
    int i, pos, idx = 1;

    s->modelp->coeff_index_to_pos[0] = 0;
    for (i=0; i<16; i++)
        for (pos=1; pos<64; pos++)
            if (s->modelp->coeff_reorder[pos] == i)
                s->modelp->coeff_index_to_pos[idx++] = pos;
}

static void vp6_default_models_init(VP56Context *s)
{
    VP56Model *model = s->modelp;

    model->vector_dct[0] = 0xA2;
    model->vector_dct[1] = 0xA4;
    model->vector_sig[0] = 0x80;
    model->vector_sig[1] = 0x80;

    memcpy(model->mb_types_stats, ff_vp56_def_mb_types_stats, sizeof(model->mb_types_stats));
    memcpy(model->vector_fdv, vp6_def_fdv_vector_model, sizeof(model->vector_fdv));
    memcpy(model->vector_pdv, vp6_def_pdv_vector_model, sizeof(model->vector_pdv));
    memcpy(model->coeff_runv, vp6_def_runv_coeff_model, sizeof(model->coeff_runv));
    memcpy(model->coeff_reorder, vp6_def_coeff_reorder, sizeof(model->coeff_reorder));

    vp6_coeff_order_table_init(s);
}

static void vp6_parse_vector_models(VP56Context *s)
{
    VP56RangeCoder *c = &s->c;
    VP56Model *model = s->modelp;
    int comp, node;

    for (comp=0; comp<2; comp++) {
        if (vp56_rac_get_prob(c, vp6_sig_dct_pct[comp][0]))
            model->vector_dct[comp] = vp56_rac_gets_nn(c, 7);
        if (vp56_rac_get_prob(c, vp6_sig_dct_pct[comp][1]))
            model->vector_sig[comp] = vp56_rac_gets_nn(c, 7);
    }

    for (comp=0; comp<2; comp++)
        for (node=0; node<7; node++)
            if (vp56_rac_get_prob(c, vp6_pdv_pct[comp][node]))
                model->vector_pdv[comp][node] = vp56_rac_gets_nn(c, 7);

    for (comp=0; comp<2; comp++)
        for (node=0; node<8; node++)
            if (vp56_rac_get_prob(c, vp6_fdv_pct[comp][node]))
                model->vector_fdv[comp][node] = vp56_rac_gets_nn(c, 7);
}

/* nodes must ascend by count, but with descending symbol order */
static int vp6_huff_cmp(const void *va, const void *vb)
{
    const Node *a = va, *b = vb;
    return (a->count - b->count)*16 + (b->sym - a->sym);
}

static int vp6_build_huff_tree(VP56Context *s, uint8_t coeff_model[],
                               const uint8_t *map, unsigned size, VLC *vlc)
{
    Node nodes[2*VP6_MAX_HUFF_SIZE], *tmp = &nodes[size];
    int a, b, i;

    /* first compute probabilities from model */
    tmp[0].count = 256;
    for (i=0; i<size-1; i++) {
        a = tmp[i].count *        coeff_model[i]  >> 8;
        b = tmp[i].count * (255 - coeff_model[i]) >> 8;
        nodes[map[2*i  ]].count = a + !a;
        nodes[map[2*i+1]].count = b + !b;
    }

    ff_free_vlc(vlc);
    /* then build the huffman tree according to probabilities */
    return ff_huff_build_tree(s->avctx, vlc, size, FF_HUFFMAN_BITS,
                              nodes, vp6_huff_cmp,
                              FF_HUFFMAN_FLAG_HNODE_FIRST);
}

static int vp6_parse_coeff_models(VP56Context *s)
{
    VP56RangeCoder *c = &s->c;
    VP56Model *model = s->modelp;
    int def_prob[11];
    int node, cg, ctx, pos;
    int ct;    /* code type */
    int pt;    /* plane type (0 for Y, 1 for U or V) */

    memset(def_prob, 0x80, sizeof(def_prob));

    for (pt=0; pt<2; pt++)
        for (node=0; node<11; node++)
            if (vp56_rac_get_prob(c, vp6_dccv_pct[pt][node])) {
                def_prob[node] = vp56_rac_gets_nn(c, 7);
                model->coeff_dccv[pt][node] = def_prob[node];
            } else if (s->frames[VP56_FRAME_CURRENT]->key_frame) {
                model->coeff_dccv[pt][node] = def_prob[node];
            }

    if (vp56_rac_get(c)) {
        for (pos=1; pos<64; pos++)
            if (vp56_rac_get_prob(c, vp6_coeff_reorder_pct[pos]))
                model->coeff_reorder[pos] = vp56_rac_gets(c, 4);
        vp6_coeff_order_table_init(s);
    }

    for (cg=0; cg<2; cg++)
        for (node=0; node<14; node++)
            if (vp56_rac_get_prob(c, vp6_runv_pct[cg][node]))
                model->coeff_runv[cg][node] = vp56_rac_gets_nn(c, 7);

    for (ct=0; ct<3; ct++)
        for (pt=0; pt<2; pt++)
            for (cg=0; cg<6; cg++)
                for (node=0; node<11; node++)
                    if (vp56_rac_get_prob(c, vp6_ract_pct[ct][pt][cg][node])) {
                        def_prob[node] = vp56_rac_gets_nn(c, 7);
                        model->coeff_ract[pt][ct][cg][node] = def_prob[node];
                    } else if (s->frames[VP56_FRAME_CURRENT]->key_frame) {
                        model->coeff_ract[pt][ct][cg][node] = def_prob[node];
                    }

    if (s->use_huffman) {
        for (pt=0; pt<2; pt++) {
            if (vp6_build_huff_tree(s, model->coeff_dccv[pt],
                                    vp6_huff_coeff_map, 12, &s->dccv_vlc[pt]))
                return -1;
            if (vp6_build_huff_tree(s, model->coeff_runv[pt],
                                    vp6_huff_run_map, 9, &s->runv_vlc[pt]))
                return -1;
            for (ct=0; ct<3; ct++)
                for (cg = 0; cg < 6; cg++)
                    if (vp6_build_huff_tree(s, model->coeff_ract[pt][ct][cg],
                                            vp6_huff_coeff_map, 12,
                                            &s->ract_vlc[pt][ct][cg]))
                        return -1;
        }
        memset(s->nb_null, 0, sizeof(s->nb_null));
    } else {
    /* coeff_dcct is a linear combination of coeff_dccv */
    for (pt=0; pt<2; pt++)
        for (ctx=0; ctx<3; ctx++)
            for (node=0; node<5; node++)
                model->coeff_dcct[pt][ctx][node] = av_clip(((model->coeff_dccv[pt][node] * vp6_dccv_lc[ctx][node][0] + 128) >> 8) + vp6_dccv_lc[ctx][node][1], 1, 255);
    }
    return 0;
}

static void vp6_parse_vector_adjustment(VP56Context *s, VP56mv *vect)
{
    VP56RangeCoder *c = &s->c;
    VP56Model *model = s->modelp;
    int comp;

    *vect = (VP56mv) {0,0};
    if (s->vector_candidate_pos < 2)
        *vect = s->vector_candidate[0];

    for (comp=0; comp<2; comp++) {
        int i, delta = 0;

        if (vp56_rac_get_prob(c, model->vector_dct[comp])) {
            static const uint8_t prob_order[] = {0, 1, 2, 7, 6, 5, 4};
            for (i=0; i<sizeof(prob_order); i++) {
                int j = prob_order[i];
                delta |= vp56_rac_get_prob(c, model->vector_fdv[comp][j])<<j;
            }
            if (delta & 0xF0)
                delta |= vp56_rac_get_prob(c, model->vector_fdv[comp][3])<<3;
            else
                delta |= 8;
        } else {
            delta = vp56_rac_get_tree(c, ff_vp56_pva_tree,
                                      model->vector_pdv[comp]);
        }

        if (delta && vp56_rac_get_prob(c, model->vector_sig[comp]))
            delta = -delta;

        if (!comp)
            vect->x += delta;
        else
            vect->y += delta;
    }
}

/**
 * Read number of consecutive blocks with null DC or AC.
 * This value is < 74.
 */
static unsigned vp6_get_nb_null(VP56Context *s)
{
    unsigned val = get_bits(&s->gb, 2);
    if (val == 2)
        val += get_bits(&s->gb, 2);
    else if (val == 3) {
        val = get_bits1(&s->gb) << 2;
        val = 6+val + get_bits(&s->gb, 2+val);
    }
    return val;
}

static void vp6_parse_coeff_huffman(VP56Context *s)
{
    VP56Model *model = s->modelp;
    uint8_t *permute = s->idct_scantable;
    VLC *vlc_coeff;
    int coeff, sign, coeff_idx;
    int b, cg, idx;
    int pt = 0;    /* plane type (0 for Y, 1 for U or V) */

    for (b=0; b<6; b++) {
        int ct = 0;    /* code type */
        if (b > 3) pt = 1;
        vlc_coeff = &s->dccv_vlc[pt];

        for (coeff_idx = 0;;) {
            int run = 1;
            if (coeff_idx<2 && s->nb_null[coeff_idx][pt]) {
                s->nb_null[coeff_idx][pt]--;
                if (coeff_idx)
                    break;
            } else {
                if (get_bits_left(&s->gb) <= 0)
                    return;
                coeff = get_vlc2(&s->gb, vlc_coeff->table, FF_HUFFMAN_BITS, 3);
                if (coeff == 0) {
                    if (coeff_idx) {
                        int pt = (coeff_idx >= 6);
                        run += get_vlc2(&s->gb, s->runv_vlc[pt].table, FF_HUFFMAN_BITS, 3);
                        if (run >= 9)
                            run += get_bits(&s->gb, 6);
                    } else
                        s->nb_null[0][pt] = vp6_get_nb_null(s);
                    ct = 0;
                } else if (coeff == 11) {  /* end of block */
                    if (coeff_idx == 1)    /* first AC coeff ? */
                        s->nb_null[1][pt] = vp6_get_nb_null(s);
                    break;
                } else {
                    int coeff2 = ff_vp56_coeff_bias[coeff];
                    if (coeff > 4)
                        coeff2 += get_bits(&s->gb, coeff <= 9 ? coeff - 4 : 11);
                    ct = 1 + (coeff2 > 1);
                    sign = get_bits1(&s->gb);
                    coeff2 = (coeff2 ^ -sign) + sign;
                    if (coeff_idx)
                        coeff2 *= s->dequant_ac;
                    idx = model->coeff_index_to_pos[coeff_idx];
                    s->block_coeff[b][permute[idx]] = coeff2;
                }
            }
            coeff_idx+=run;
            if (coeff_idx >= 64)
                break;
            cg = FFMIN(vp6_coeff_groups[coeff_idx], 3);
            vlc_coeff = &s->ract_vlc[pt][ct][cg];
        }
    }
}

static void vp6_parse_coeff(VP56Context *s)
{
    VP56RangeCoder *c = s->ccp;
    VP56Model *model = s->modelp;
    uint8_t *permute = s->idct_scantable;
    uint8_t *model1, *model2, *model3;
    int coeff, sign, coeff_idx;
    int b, i, cg, idx, ctx;
    int pt = 0;    /* plane type (0 for Y, 1 for U or V) */

    for (b=0; b<6; b++) {
        int ct = 1;    /* code type */
        int run = 1;

        if (b > 3) pt = 1;

        ctx = s->left_block[ff_vp56_b6to4[b]].not_null_dc
              + s->above_blocks[s->above_block_idx[b]].not_null_dc;
        model1 = model->coeff_dccv[pt];
        model2 = model->coeff_dcct[pt][ctx];

        coeff_idx = 0;
        for (;;) {
            if ((coeff_idx>1 && ct==0) || vp56_rac_get_prob(c, model2[0])) {
                /* parse a coeff */
                if (vp56_rac_get_prob(c, model2[2])) {
                    if (vp56_rac_get_prob(c, model2[3])) {
                        idx = vp56_rac_get_tree(c, ff_vp56_pc_tree, model1);
                        coeff = ff_vp56_coeff_bias[idx+5];
                        for (i=ff_vp56_coeff_bit_length[idx]; i>=0; i--)
                            coeff += vp56_rac_get_prob(c, ff_vp56_coeff_parse_table[idx][i]) << i;
                    } else {
                        if (vp56_rac_get_prob(c, model2[4]))
                            coeff = 3 + vp56_rac_get_prob(c, model1[5]);
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
                idx = model->coeff_index_to_pos[coeff_idx];
                s->block_coeff[b][permute[idx]] = coeff;
                run = 1;
            } else {
                /* parse a run */
                ct = 0;
                if (coeff_idx > 0) {
                    if (!vp56_rac_get_prob(c, model2[1]))
                        break;

                    model3 = model->coeff_runv[coeff_idx >= 6];
                    run = vp56_rac_get_tree(c, vp6_pcr_tree, model3);
                    if (!run)
                        for (run=9, i=0; i<6; i++)
                            run += vp56_rac_get_prob(c, model3[i+8]) << i;
                }
            }
            coeff_idx += run;
            if (coeff_idx >= 64)
                break;
            cg = vp6_coeff_groups[coeff_idx];
            model1 = model2 = model->coeff_ract[pt][ct][cg];
        }

        s->left_block[ff_vp56_b6to4[b]].not_null_dc =
        s->above_blocks[s->above_block_idx[b]].not_null_dc = !!s->block_coeff[b][0];
    }
}

static int vp6_block_variance(uint8_t *src, ptrdiff_t stride)
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

static void vp6_filter_hv4(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
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

static void vp6_filter_diag2(VP56Context *s, uint8_t *dst, uint8_t *src,
                             ptrdiff_t stride, int h_weight, int v_weight)
{
    uint8_t *tmp = s->edge_emu_buffer+16;
    s->h264chroma.put_h264_chroma_pixels_tab[0](tmp, src, stride, 9, h_weight, 0);
    s->h264chroma.put_h264_chroma_pixels_tab[0](dst, tmp, stride, 8, 0, v_weight);
}

static void vp6_filter(VP56Context *s, uint8_t *dst, uint8_t *src,
                       int offset1, int offset2, ptrdiff_t stride,
                       VP56mv mv, int mask, int select, int luma)
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
        } else {
            s->vp56dsp.vp6_filter_diag4(dst, src+offset1+((mv.x^mv.y)>>31), stride,
                             vp6_block_copy_filter[select][x8],
                             vp6_block_copy_filter[select][y8]);
        }
    } else {
        if (!x8 || !y8) {
            s->h264chroma.put_h264_chroma_pixels_tab[0](dst, src + offset1, stride, 8, x8, y8);
        } else {
            vp6_filter_diag2(s, dst, src+offset1 + ((mv.x^mv.y)>>31), stride, x8, y8);
        }
    }
}

static av_cold int vp6_decode_init(AVCodecContext *avctx)
{
    VP56Context *s = avctx->priv_data;
    int ret;

    if ((ret = ff_vp56_init(avctx, avctx->codec->id == AV_CODEC_ID_VP6,
                            avctx->codec->id == AV_CODEC_ID_VP6A)) < 0)
        return ret;
    ff_vp6dsp_init(&s->vp56dsp);

    s->vp56_coord_div = vp6_coord_div;
    s->parse_vector_adjustment = vp6_parse_vector_adjustment;
    s->filter = vp6_filter;
    s->default_models_init = vp6_default_models_init;
    s->parse_vector_models = vp6_parse_vector_models;
    s->parse_coeff_models = vp6_parse_coeff_models;
    s->parse_header = vp6_parse_header;

    return 0;
}

static av_cold int vp6_decode_free(AVCodecContext *avctx)
{
    VP56Context *s = avctx->priv_data;
    int pt, ct, cg;

    ff_vp56_free(avctx);

    for (pt=0; pt<2; pt++) {
        ff_free_vlc(&s->dccv_vlc[pt]);
        ff_free_vlc(&s->runv_vlc[pt]);
        for (ct=0; ct<3; ct++)
            for (cg=0; cg<6; cg++)
                ff_free_vlc(&s->ract_vlc[pt][ct][cg]);
    }
    return 0;
}

AVCodec ff_vp6_decoder = {
    .name           = "vp6",
    .long_name      = NULL_IF_CONFIG_SMALL("On2 VP6"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP6,
    .priv_data_size = sizeof(VP56Context),
    .init           = vp6_decode_init,
    .close          = vp6_decode_free,
    .decode         = ff_vp56_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};

/* flash version, not flipped upside-down */
AVCodec ff_vp6f_decoder = {
    .name           = "vp6f",
    .long_name      = NULL_IF_CONFIG_SMALL("On2 VP6 (Flash version)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP6F,
    .priv_data_size = sizeof(VP56Context),
    .init           = vp6_decode_init,
    .close          = vp6_decode_free,
    .decode         = ff_vp56_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};

/* flash version, not flipped upside-down, with alpha channel */
AVCodec ff_vp6a_decoder = {
    .name           = "vp6a",
    .long_name      = NULL_IF_CONFIG_SMALL("On2 VP6 (Flash version, with alpha channel)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP6A,
    .priv_data_size = sizeof(VP56Context),
    .init           = vp6_decode_init,
    .close          = vp6_decode_free,
    .decode         = ff_vp56_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
