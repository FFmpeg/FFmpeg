/*
 * FFV1 encoder template
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

static av_always_inline int RENAME(encode_line)(FFV1Context *s, int w,
                                                TYPE *sample[3],
                                                int plane_index, int bits)
{
    PlaneContext *const p = &s->plane[plane_index];
    RangeCoder *const c   = &s->c;
    int x;
    int run_index = s->run_index;
    int run_count = 0;
    int run_mode  = 0;

    if (s->ac != AC_GOLOMB_RICE) {
        if (c->bytestream_end - c->bytestream < w * 35) {
            av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        if (put_bytes_left(&s->pb, 0) < w * 4) {
            av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (s->slice_coding_mode == 1) {
        for (x = 0; x < w; x++) {
            int i;
            int v = sample[0][x];
            for (i = bits-1; i>=0; i--) {
                uint8_t state = 128;
                put_rac(c, &state, (v>>i) & 1);
            }
        }
        return 0;
    }

    for (x = 0; x < w; x++) {
        int diff, context;

        context = RENAME(get_context)(p, sample[0] + x, sample[1] + x, sample[2] + x);
        diff    = sample[0][x] - RENAME(predict)(sample[0] + x, sample[1] + x);

        if (context < 0) {
            context = -context;
            diff    = -diff;
        }

        diff = fold(diff, bits);

        if (s->ac != AC_GOLOMB_RICE) {
            if (s->flags & AV_CODEC_FLAG_PASS1) {
                put_symbol_inline(c, p->state[context], diff, 1, s->rc_stat,
                                  s->rc_stat2[p->quant_table_index][context]);
            } else {
                put_symbol_inline(c, p->state[context], diff, 1, NULL, NULL);
            }
        } else {
            if (context == 0)
                run_mode = 1;

            if (run_mode) {
                if (diff) {
                    while (run_count >= 1 << ff_log2_run[run_index]) {
                        run_count -= 1 << ff_log2_run[run_index];
                        run_index++;
                        put_bits(&s->pb, 1, 1);
                    }

                    put_bits(&s->pb, 1 + ff_log2_run[run_index], run_count);
                    if (run_index)
                        run_index--;
                    run_count = 0;
                    run_mode  = 0;
                    if (diff > 0)
                        diff--;
                } else {
                    run_count++;
                }
            }

            ff_dlog(s->avctx, "count:%d index:%d, mode:%d, x:%d pos:%d\n",
                    run_count, run_index, run_mode, x,
                    (int)put_bits_count(&s->pb));

            if (run_mode == 0)
                put_vlc_symbol(&s->pb, &p->vlc_state[context], diff, bits);
        }
    }
    if (run_mode) {
        while (run_count >= 1 << ff_log2_run[run_index]) {
            run_count -= 1 << ff_log2_run[run_index];
            run_index++;
            put_bits(&s->pb, 1, 1);
        }

        if (run_count)
            put_bits(&s->pb, 1, 1);
    }
    s->run_index = run_index;

    return 0;
}

static int RENAME(encode_rgb_frame)(FFV1Context *s, const uint8_t *src[4],
                                    int w, int h, const int stride[4])
{
    int x, y, p, i;
    const int ring_size = s->context_model ? 3 : 2;
    TYPE *sample[4][3];
    int lbd    = s->bits_per_raw_sample <= 8;
    int packed = !src[1];
    int bits   = s->bits_per_raw_sample > 0 ? s->bits_per_raw_sample : 8;
    int offset = 1 << bits;
    int transparency = s->transparency;
    int packed_size = (3 + transparency)*2;

    s->run_index = 0;

    memset(RENAME(s->sample_buffer), 0, ring_size * MAX_PLANES *
           (w + 6) * sizeof(*RENAME(s->sample_buffer)));

    for (y = 0; y < h; y++) {
        for (i = 0; i < ring_size; i++)
            for (p = 0; p < MAX_PLANES; p++)
                sample[p][i]= RENAME(s->sample_buffer) + p*ring_size*(w+6) + ((h+i-y)%ring_size)*(w+6) + 3;

        for (x = 0; x < w; x++) {
            int b, g, r, av_uninit(a);
            if (lbd) {
                unsigned v = *((const uint32_t*)(src[0] + x*4 + stride[0]*y));
                b =  v        & 0xFF;
                g = (v >>  8) & 0xFF;
                r = (v >> 16) & 0xFF;
                a =  v >> 24;
            } else if (packed) {
                const uint16_t *p = ((const uint16_t*)(src[0] + x*packed_size + stride[0]*y));
                r = p[0];
                g = p[1];
                b = p[2];
                if (transparency)
                  a = p[3];
            } else if (sizeof(TYPE) == 4 || transparency) {
                g = *((const uint16_t *)(src[0] + x*2 + stride[0]*y));
                b = *((const uint16_t *)(src[1] + x*2 + stride[1]*y));
                r = *((const uint16_t *)(src[2] + x*2 + stride[2]*y));
                if (transparency)
                    a = *((const uint16_t *)(src[3] + x*2 + stride[3]*y));
            } else {
                b = *((const uint16_t *)(src[0] + x*2 + stride[0]*y));
                g = *((const uint16_t *)(src[1] + x*2 + stride[1]*y));
                r = *((const uint16_t *)(src[2] + x*2 + stride[2]*y));
            }

            if (s->slice_coding_mode != 1) {
                b -= g;
                r -= g;
                g += (b * s->slice_rct_by_coef + r * s->slice_rct_ry_coef) >> 2;
                b += offset;
                r += offset;
            }

            sample[0][0][x] = g;
            sample[1][0][x] = b;
            sample[2][0][x] = r;
            sample[3][0][x] = a;
        }
        for (p = 0; p < 3 + transparency; p++) {
            int ret;
            sample[p][0][-1] = sample[p][1][0  ];
            sample[p][1][ w] = sample[p][1][w-1];
            if (lbd && s->slice_coding_mode == 0)
                ret = RENAME(encode_line)(s, w, sample[p], (p + 1) / 2, 9);
            else
                ret = RENAME(encode_line)(s, w, sample[p], (p + 1) / 2, bits + (s->slice_coding_mode != 1));
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

