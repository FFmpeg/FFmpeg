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

static av_always_inline int RENAME(decode_line)(FFV1Context *s, int w,
                                                 TYPE *sample[2],
                                                 int plane_index, int bits)
{
    PlaneContext *const p = &s->plane[plane_index];
    RangeCoder *const c   = &s->c;
    int x;
    int run_count = 0;
    int run_mode  = 0;
    int run_index = s->run_index;

    if (is_input_end(s))
        return AVERROR_INVALIDDATA;

    if (s->slice_coding_mode == 1) {
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
            if (is_input_end(s))
                return AVERROR_INVALIDDATA;
        }

        context = RENAME(get_context)(p, sample[1] + x, sample[0] + x, sample[1] + x);
        if (context < 0) {
            context = -context;
            sign    = 1;
        } else
            sign = 0;

        av_assert2(context < p->context_count);

        if (s->ac != AC_GOLOMB_RICE) {
            diff = get_symbol_inline(c, p->state[context], 1);
        } else {
            if (context == 0 && run_mode == 0)
                run_mode = 1;

            if (run_mode) {
                if (run_count == 0 && run_mode == 1) {
                    if (get_bits1(&s->gb)) {
                        run_count = 1 << ff_log2_run[run_index];
                        if (x + run_count <= w)
                            run_index++;
                    } else {
                        if (ff_log2_run[run_index])
                            run_count = get_bits(&s->gb, ff_log2_run[run_index]);
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
                    diff      = get_vlc_symbol(&s->gb, &p->vlc_state[context],
                                               bits);
                    if (diff >= 0)
                        diff++;
                } else
                    diff = 0;
            } else
                diff = get_vlc_symbol(&s->gb, &p->vlc_state[context], bits);

            ff_dlog(s->avctx, "count:%d index:%d, mode:%d, x:%d pos:%d\n",
                    run_count, run_index, run_mode, x, get_bits_count(&s->gb));
        }

        if (sign)
            diff = -(unsigned)diff;

        sample[1][x] = av_mod_uintp2(RENAME(predict)(sample[1] + x, sample[0] + x) + (SUINT)diff, bits);
    }
    s->run_index = run_index;
    return 0;
}

static int RENAME(decode_rgb_frame)(FFV1Context *s, uint8_t *src[4], int w, int h, int stride[4])
{
    int x, y, p;
    TYPE *sample[4][2];
    int lbd    = s->avctx->bits_per_raw_sample <= 8;
    int bits   = s->avctx->bits_per_raw_sample > 0 ? s->avctx->bits_per_raw_sample : 8;
    int offset = 1 << bits;
    int transparency = s->transparency;

    for (x = 0; x < 4; x++) {
        sample[x][0] = RENAME(s->sample_buffer) +  x * 2      * (w + 6) + 3;
        sample[x][1] = RENAME(s->sample_buffer) + (x * 2 + 1) * (w + 6) + 3;
    }

    s->run_index = 0;

    memset(RENAME(s->sample_buffer), 0, 8 * (w + 6) * sizeof(*RENAME(s->sample_buffer)));

    for (y = 0; y < h; y++) {
        for (p = 0; p < 3 + transparency; p++) {
            int ret;
            TYPE *temp = sample[p][0]; // FIXME: try a normal buffer

            sample[p][0] = sample[p][1];
            sample[p][1] = temp;

            sample[p][1][-1]= sample[p][0][0  ];
            sample[p][0][ w]= sample[p][0][w-1];
            if (lbd && s->slice_coding_mode == 0)
                ret = RENAME(decode_line)(s, w, sample[p], (p + 1)/2, 9);
            else
                ret = RENAME(decode_line)(s, w, sample[p], (p + 1)/2, bits + (s->slice_coding_mode != 1));
            if (ret < 0)
                return ret;
        }
        for (x = 0; x < w; x++) {
            int g = sample[0][1][x];
            int b = sample[1][1][x];
            int r = sample[2][1][x];
            int a = sample[3][1][x];

            if (s->slice_coding_mode != 1) {
                b -= offset;
                r -= offset;
                g -= (b * s->slice_rct_by_coef + r * s->slice_rct_ry_coef) >> 2;
                b += g;
                r += g;
            }

            if (lbd)
                *((uint32_t*)(src[0] + x*4 + stride[0]*y)) = b + ((unsigned)g<<8) + ((unsigned)r<<16) + ((unsigned)a<<24);
            else if (sizeof(TYPE) == 4 || transparency) {
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
