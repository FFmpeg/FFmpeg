/*
 * FFV1 decoder template
 *
 * Copyright (c) 2003-2016 Michael Niedermayer <michaelni@gmx.at>
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

#include "ffv1_template.c"

static av_always_inline int
RENAME(decode_line)(FFV1Context *f, FFV1SliceContext *sc,
                    GetBitContext *gb,
                    int w, TYPE *sample[2], int plane_index, int bits,
                    int ac)
{
    PlaneContext *const p = &sc->plane[plane_index];
    RangeCoder *const c   = &sc->c;
    const int16_t (*quant_table)[256] = f->quant_tables[p->quant_table_index];
    int x;
    int run_count = 0;
    int run_mode  = 0;
    int run_index = sc->run_index;

    if (bits == 0) {
        for (x = 0; x < w; x++)
            sample[1][x] = 0;
        return 0;
    }

    if (is_input_end(c, gb, ac))
        return AVERROR_INVALIDDATA;

    if (sc->slice_coding_mode == 1) {
        int i;
        for (x = 0; x < w; x++) {
            int v = 0;
            for (i=0; i<bits; i++) {
                uint8_t state = 128;
                v += v + get_rac(c, &state);
            }
            sample[1][x] = v;
        }
        return 0;
    }

    for (x = 0; x < w; x++) {
        int diff, context, sign;

        if (!(x & 1023)) {
            if (is_input_end(c, gb, ac))
                return AVERROR_INVALIDDATA;
        }

        context = RENAME(get_context)(quant_table,
                                      sample[1] + x, sample[0] + x, sample[1] + x);
        if (context < 0) {
            context = -context;
            sign    = 1;
        } else
            sign = 0;

        av_assert2(context < p->context_count);

        if (ac != AC_GOLOMB_RICE) {
            diff = get_symbol_inline(c, p->state[context], 1);
        } else {
            if (context == 0 && run_mode == 0)
                run_mode = 1;

            if (run_mode) {
                if (run_count == 0 && run_mode == 1) {
                    if (get_bits1(gb)) {
                        run_count = 1 << ff_log2_run[run_index];
                        if (x + run_count <= w)
                            run_index++;
                    } else {
                        if (ff_log2_run[run_index])
                            run_count = get_bits(gb, ff_log2_run[run_index]);
                        else
                            run_count = 0;
                        if (run_index)
                            run_index--;
                        run_mode = 2;
                    }
                }
                if (sample[1][x - 1] == sample[0][x - 1]) {
                    while (run_count > 1 && w-x > 1) {
                        sample[1][x] = sample[0][x];
                        x++;
                        run_count--;
                    }
                } else {
                    while (run_count > 1 && w-x > 1) {
                        sample[1][x] = RENAME(predict)(sample[1] + x, sample[0] + x);
                        x++;
                        run_count--;
                    }
                }
                run_count--;
                if (run_count < 0) {
                    run_mode  = 0;
                    run_count = 0;
                    diff      = get_vlc_symbol(gb, &p->vlc_state[context],
                                               bits);
                    if (diff >= 0)
                        diff++;
                } else
                    diff = 0;
            } else
                diff = get_vlc_symbol(gb, &p->vlc_state[context], bits);

            ff_dlog(f->avctx, "count:%d index:%d, mode:%d, x:%d pos:%d\n",
                    run_count, run_index, run_mode, x, get_bits_count(gb));
        }

        if (sign)
            diff = -(unsigned)diff;

        sample[1][x] = av_zero_extend(RENAME(predict)(sample[1] + x, sample[0] + x) + (SUINT)diff, bits);
    }
    sc->run_index = run_index;
    return 0;
}

static int RENAME(decode_rgb_frame)(FFV1Context *f, FFV1SliceContext *sc,
                                    GetBitContext *gb,
                                    uint8_t *src[4], int w, int h, int stride[4])
{
    int x, y, p;
    TYPE *sample[4][2];
    int lbd    = f->avctx->bits_per_raw_sample <= 8;
    int bits[4], offset;
    int transparency = f->transparency;
    int ac = f->ac;
    unsigned mask[4];

    ff_ffv1_compute_bits_per_plane(f, sc, bits, &offset, mask, f->avctx->bits_per_raw_sample);

    if (sc->slice_coding_mode == 1)
        ac = 1;

    for (x = 0; x < 4; x++) {
        sample[x][0] = RENAME(sc->sample_buffer) +  x * 2      * (w + 6) + 3;
        sample[x][1] = RENAME(sc->sample_buffer) + (x * 2 + 1) * (w + 6) + 3;
    }

    sc->run_index = 0;

    memset(RENAME(sc->sample_buffer), 0, 8 * (w + 6) * sizeof(*RENAME(sc->sample_buffer)));

    for (y = 0; y < h; y++) {
        for (p = 0; p < 3 + transparency; p++) {
            int ret;
            TYPE *temp = sample[p][0]; // FIXME: try a normal buffer

            sample[p][0] = sample[p][1];
            sample[p][1] = temp;

            sample[p][1][-1]= sample[p][0][0  ];
            sample[p][0][ w]= sample[p][0][w-1];
            if (bits[p] == 9)
                ret = RENAME(decode_line)(f, sc, gb, w, sample[p], (p + 1)/2, 9, ac);
            else
                ret = RENAME(decode_line)(f, sc, gb, w, sample[p], (p + 1)/2, bits[p], ac);
            if (ret < 0)
                return ret;
        }
        for (x = 0; x < w; x++) {
            int g = sample[0][1][x];
            int b = sample[1][1][x];
            int r = sample[2][1][x];
            int a = sample[3][1][x];

            if (sc->slice_coding_mode != 1) {
                b -= offset;
                r -= offset;
                g -= (b * sc->slice_rct_by_coef + r * sc->slice_rct_ry_coef) >> 2;
                b += g;
                r += g;
            }
            if (sc->remap) {
                if (f->avctx->bits_per_raw_sample == 32) {
                    g = sc->fltmap32[0][g & mask[0]];
                    b = sc->fltmap32[1][b & mask[1]];
                    r = sc->fltmap32[2][r & mask[2]];
                    if (transparency)
                        a = sc->fltmap32[3][a & mask[3]];
                } else {
                    g = sc->fltmap[0][g & mask[0]];
                    b = sc->fltmap[1][b & mask[1]];
                    r = sc->fltmap[2][r & mask[2]];
                    if (transparency)
                        a = sc->fltmap[3][a & mask[3]];
                }
            }

            if (lbd) {
                *((uint32_t*)(src[0] + x*4 + stride[0]*y)) = b + ((unsigned)g<<8) + ((unsigned)r<<16) + ((unsigned)a<<24);
            } else if (f->avctx->bits_per_raw_sample == 32) {
                *((uint32_t*)(src[0] + x*4 + stride[0]*y)) = g;
                *((uint32_t*)(src[1] + x*4 + stride[1]*y)) = b;
                *((uint32_t*)(src[2] + x*4 + stride[2]*y)) = r;
                if (transparency)
                    *((uint32_t*)(src[3] + x*4 + stride[3]*y)) = a;
            } else if (sizeof(TYPE) == 4 || transparency) {
                *((uint16_t*)(src[0] + x*2 + stride[0]*y)) = g;
                *((uint16_t*)(src[1] + x*2 + stride[1]*y)) = b;
                *((uint16_t*)(src[2] + x*2 + stride[2]*y)) = r;
                if (transparency)
                    *((uint16_t*)(src[3] + x*2 + stride[3]*y)) = a;
            } else {
                *((uint16_t*)(src[0] + x*2 + stride[0]*y)) = b;
                *((uint16_t*)(src[1] + x*2 + stride[1]*y)) = g;
                *((uint16_t*)(src[2] + x*2 + stride[2]*y)) = r;
            }
        }
    }
    return 0;
}
