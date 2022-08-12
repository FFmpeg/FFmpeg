/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "avcodec.h"
#include "vp9dec.h"

static av_always_inline void filter_plane_cols(VP9Context *s, int col, int ss_h, int ss_v,
                                               uint8_t *lvl, uint8_t (*mask)[4],
                                               uint8_t *dst, ptrdiff_t ls)
{
    int y, x, bytesperpixel = s->bytesperpixel;

    // filter edges between columns (e.g. block1 | block2)
    for (y = 0; y < 8; y += 2 << ss_v, dst += 16 * ls, lvl += 16 << ss_v) {
        uint8_t *ptr = dst, *l = lvl, *hmask1 = mask[y], *hmask2 = mask[y + 1 + ss_v];
        unsigned hm1 = hmask1[0] | hmask1[1] | hmask1[2], hm13 = hmask1[3];
        unsigned hm2 = hmask2[1] | hmask2[2], hm23 = hmask2[3];
        unsigned hm = hm1 | hm2 | hm13 | hm23;

        for (x = 1; hm & ~(x - 1); x <<= 1, ptr += 8 * bytesperpixel >> ss_h) {
            if (col || x > 1) {
                if (hm1 & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (hmask1[0] & x) {
                        if (hmask2[0] & x) {
                            av_assert2(l[8 << ss_v] == L);
                            s->dsp.loop_filter_16[0](ptr, ls, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[2][0](ptr, ls, E, I, H);
                        }
                    } else if (hm2 & x) {
                        L = l[8 << ss_v];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[!!(hmask1[1] & x)]
                                               [!!(hmask2[1] & x)]
                                               [0](ptr, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[!!(hmask1[1] & x)]
                                            [0](ptr, ls, E, I, H);
                    }
                } else if (hm2 & x) {
                    int L = l[8 << ss_v], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[!!(hmask2[1] & x)]
                                        [0](ptr + 8 * ls, ls, E, I, H);
                }
            }
            if (ss_h) {
                if (x & 0xAA)
                    l += 2;
            } else {
                if (hm13 & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (hm23 & x) {
                        L = l[8 << ss_v];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[0][0][0](ptr + 4 * bytesperpixel, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[0][0](ptr + 4 * bytesperpixel, ls, E, I, H);
                    }
                } else if (hm23 & x) {
                    int L = l[8 << ss_v], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[0][0](ptr + 8 * ls + 4 * bytesperpixel, ls, E, I, H);
                }
                l++;
            }
        }
    }
}

static av_always_inline void filter_plane_rows(VP9Context *s, int row, int ss_h, int ss_v,
                                               uint8_t *lvl, uint8_t (*mask)[4],
                                               uint8_t *dst, ptrdiff_t ls)
{
    int y, x, bytesperpixel = s->bytesperpixel;

    //                                 block1
    // filter edges between rows (e.g. ------)
    //                                 block2
    for (y = 0; y < 8; y++, dst += 8 * ls >> ss_v) {
        uint8_t *ptr = dst, *l = lvl, *vmask = mask[y];
        unsigned vm = vmask[0] | vmask[1] | vmask[2], vm3 = vmask[3];

        for (x = 1; vm & ~(x - 1); x <<= (2 << ss_h), ptr += 16 * bytesperpixel, l += 2 << ss_h) {
            if (row || y) {
                if (vm & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (vmask[0] & x) {
                        if (vmask[0] & (x << (1 + ss_h))) {
                            av_assert2(l[1 + ss_h] == L);
                            s->dsp.loop_filter_16[1](ptr, ls, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[2][1](ptr, ls, E, I, H);
                        }
                    } else if (vm & (x << (1 + ss_h))) {
                        L = l[1 + ss_h];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[!!(vmask[1] &  x)]
                                               [!!(vmask[1] & (x << (1 + ss_h)))]
                                               [1](ptr, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[!!(vmask[1] & x)]
                                            [1](ptr, ls, E, I, H);
                    }
                } else if (vm & (x << (1 + ss_h))) {
                    int L = l[1 + ss_h], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[!!(vmask[1] & (x << (1 + ss_h)))]
                                        [1](ptr + 8 * bytesperpixel, ls, E, I, H);
                }
            }
            if (!ss_v) {
                if (vm3 & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (vm3 & (x << (1 + ss_h))) {
                        L = l[1 + ss_h];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[0][0][1](ptr + ls * 4, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[0][1](ptr + ls * 4, ls, E, I, H);
                    }
                } else if (vm3 & (x << (1 + ss_h))) {
                    int L = l[1 + ss_h], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[0][1](ptr + ls * 4 + 8 * bytesperpixel, ls, E, I, H);
                }
            }
        }
        if (ss_v) {
            if (y & 1)
                lvl += 16;
        } else {
            lvl += 8;
        }
    }
}

void ff_vp9_loopfilter_sb(AVCodecContext *avctx, VP9Filter *lflvl,
                          int row, int col, ptrdiff_t yoff, ptrdiff_t uvoff)
{
    VP9Context *s = avctx->priv_data;
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;
    uint8_t *dst = f->data[0] + yoff;
    ptrdiff_t ls_y = f->linesize[0], ls_uv = f->linesize[1];
    uint8_t (*uv_masks)[8][4] = lflvl->mask[s->ss_h | s->ss_v];
    int p;

    /* FIXME: In how far can we interleave the v/h loopfilter calls? E.g.
     * if you think of them as acting on a 8x8 block max, we can interleave
     * each v/h within the single x loop, but that only works if we work on
     * 8 pixel blocks, and we won't always do that (we want at least 16px
     * to use SSE2 optimizations, perhaps 32 for AVX2) */

    filter_plane_cols(s, col, 0, 0, lflvl->level, lflvl->mask[0][0], dst, ls_y);
    filter_plane_rows(s, row, 0, 0, lflvl->level, lflvl->mask[0][1], dst, ls_y);

    for (p = 0; p < 2; p++) {
        dst = f->data[1 + p] + uvoff;
        filter_plane_cols(s, col, s->ss_h, s->ss_v, lflvl->level, uv_masks[0], dst, ls_uv);
        filter_plane_rows(s, row, s->ss_h, s->ss_v, lflvl->level, uv_masks[1], dst, ls_uv);
    }
}
