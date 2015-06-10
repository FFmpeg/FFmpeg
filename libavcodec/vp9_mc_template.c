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

#define ROUNDED_DIV_MVx2(a, b) \
    (VP56mv) { .x = ROUNDED_DIV(a.x + b.x, 2), .y = ROUNDED_DIV(a.y + b.y, 2) }
#define ROUNDED_DIV_MVx4(a, b, c, d) \
    (VP56mv) { .x = ROUNDED_DIV(a.x + b.x + c.x + d.x, 4), \
               .y = ROUNDED_DIV(a.y + b.y + c.y + d.y, 4) }

static void FN(inter_pred)(AVCodecContext *ctx)
{
    static const uint8_t bwlog_tab[2][N_BS_SIZES] = {
        { 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4 },
        { 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4 },
    };
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col;
    ThreadFrame *tref1 = &s->refs[s->refidx[b->ref[0]]], *tref2;
    AVFrame *ref1 = tref1->f, *ref2;
    int w1 = ref1->width, h1 = ref1->height, w2, h2;
    ptrdiff_t ls_y = s->y_stride, ls_uv = s->uv_stride;
    int bytesperpixel = BYTES_PER_PIXEL;

    if (b->comp) {
        tref2 = &s->refs[s->refidx[b->ref[1]]];
        ref2 = tref2->f;
        w2 = ref2->width;
        h2 = ref2->height;
    }

    // y inter pred
    if (b->bs > BS_8x8) {
        VP56mv uvmv;

#if SCALED == 0
        if (b->bs == BS_8x4) {
            mc_luma_dir(s, mc[3][b->filter][0], s->dst[0], ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, col << 3, &b->mv[0][0],,,,, 8, 4, w1, h1, 0);
            mc_luma_dir(s, mc[3][b->filter][0],
                        s->dst[0] + 4 * ls_y, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        (row << 3) + 4, col << 3, &b->mv[2][0],,,,, 8, 4, w1, h1, 0);
            w1 = (w1 + s->ss_h) >> s->ss_h;
            if (s->ss_v) {
                h1 = (h1 + 1) >> 1;
                uvmv = ROUNDED_DIV_MVx2(b->mv[0][0], b->mv[2][0]);
                mc_chroma_dir(s, mc[3 + s->ss_h][b->filter][0],
                              s->dst[1], s->dst[2], ls_uv,
                              ref1->data[1], ref1->linesize[1],
                              ref1->data[2], ref1->linesize[2], tref1,
                              row << 2, col << (3 - s->ss_h),
                              &uvmv,,,,, 8 >> s->ss_h, 4, w1, h1, 0);
            } else {
                mc_chroma_dir(s, mc[3 + s->ss_h][b->filter][0],
                              s->dst[1], s->dst[2], ls_uv,
                              ref1->data[1], ref1->linesize[1],
                              ref1->data[2], ref1->linesize[2], tref1,
                              row << 3, col << (3 - s->ss_h),
                              &b->mv[0][0],,,,, 8 >> s->ss_h, 4, w1, h1, 0);
                // BUG for 4:2:2 bs=8x4, libvpx uses the wrong block index
                // to get the motion vector for the bottom 4x4 block
                // https://code.google.com/p/webm/issues/detail?id=993
                if (s->ss_h == 0) {
                    uvmv = b->mv[2][0];
                } else {
                    uvmv = ROUNDED_DIV_MVx2(b->mv[0][0], b->mv[2][0]);
                }
                mc_chroma_dir(s, mc[3 + s->ss_h][b->filter][0],
                              s->dst[1] + 4 * ls_uv, s->dst[2] + 4 * ls_uv, ls_uv,
                              ref1->data[1], ref1->linesize[1],
                              ref1->data[2], ref1->linesize[2], tref1,
                              (row << 3) + 4, col << (3 - s->ss_h),
                              &uvmv,,,,, 8 >> s->ss_h, 4, w1, h1, 0);
            }

            if (b->comp) {
                mc_luma_dir(s, mc[3][b->filter][1], s->dst[0], ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, col << 3, &b->mv[0][1],,,,, 8, 4, w2, h2, 1);
                mc_luma_dir(s, mc[3][b->filter][1],
                            s->dst[0] + 4 * ls_y, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            (row << 3) + 4, col << 3, &b->mv[2][1],,,,, 8, 4, w2, h2, 1);
                w2 = (w2 + s->ss_h) >> s->ss_h;
                if (s->ss_v) {
                    h2 = (h2 + 1) >> 1;
                    uvmv = ROUNDED_DIV_MVx2(b->mv[0][1], b->mv[2][1]);
                    mc_chroma_dir(s, mc[3 + s->ss_h][b->filter][1],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref2->data[1], ref2->linesize[1],
                                  ref2->data[2], ref2->linesize[2], tref2,
                                  row << 2, col << (3 - s->ss_h),
                                  &uvmv,,,,, 8 >> s->ss_h, 4, w2, h2, 1);
                } else {
                    mc_chroma_dir(s, mc[3 + s->ss_h][b->filter][1],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref2->data[1], ref2->linesize[1],
                                  ref2->data[2], ref2->linesize[2], tref2,
                                  row << 3, col << (3 - s->ss_h),
                                  &b->mv[0][1],,,,, 8 >> s->ss_h, 4, w2, h2, 1);
                    // BUG for 4:2:2 bs=8x4, libvpx uses the wrong block index
                    // to get the motion vector for the bottom 4x4 block
                    // https://code.google.com/p/webm/issues/detail?id=993
                    if (s->ss_h == 0) {
                        uvmv = b->mv[2][1];
                    } else {
                        uvmv = ROUNDED_DIV_MVx2(b->mv[0][1], b->mv[2][1]);
                    }
                    mc_chroma_dir(s, mc[3 + s->ss_h][b->filter][1],
                                  s->dst[1] + 4 * ls_uv, s->dst[2] + 4 * ls_uv, ls_uv,
                                  ref2->data[1], ref2->linesize[1],
                                  ref2->data[2], ref2->linesize[2], tref2,
                                  (row << 3) + 4, col << (3 - s->ss_h),
                                  &uvmv,,,,, 8 >> s->ss_h, 4, w2, h2, 1);
                }
            }
        } else if (b->bs == BS_4x8) {
            mc_luma_dir(s, mc[4][b->filter][0], s->dst[0], ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, col << 3, &b->mv[0][0],,,,, 4, 8, w1, h1, 0);
            mc_luma_dir(s, mc[4][b->filter][0], s->dst[0] + 4 * bytesperpixel, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, (col << 3) + 4, &b->mv[1][0],,,,, 4, 8, w1, h1, 0);
            h1 = (h1 + s->ss_v) >> s->ss_v;
            if (s->ss_h) {
                w1 = (w1 + 1) >> 1;
                uvmv = ROUNDED_DIV_MVx2(b->mv[0][0], b->mv[1][0]);
                mc_chroma_dir(s, mc[4][b->filter][0],
                              s->dst[1], s->dst[2], ls_uv,
                              ref1->data[1], ref1->linesize[1],
                              ref1->data[2], ref1->linesize[2], tref1,
                              row << (3 - s->ss_v), col << 2,
                              &uvmv,,,,, 4, 8 >> s->ss_v, w1, h1, 0);
            } else {
                mc_chroma_dir(s, mc[4][b->filter][0],
                              s->dst[1], s->dst[2], ls_uv,
                              ref1->data[1], ref1->linesize[1],
                              ref1->data[2], ref1->linesize[2], tref1,
                              row << (3 - s->ss_v), col << 3,
                              &b->mv[0][0],,,,, 4, 8 >> s->ss_v, w1, h1, 0);
                mc_chroma_dir(s, mc[4][b->filter][0],
                              s->dst[1] + 4 * bytesperpixel,
                              s->dst[2] + 4 * bytesperpixel, ls_uv,
                              ref1->data[1], ref1->linesize[1],
                              ref1->data[2], ref1->linesize[2], tref1,
                              row << (3 - s->ss_v), (col << 3) + 4,
                              &b->mv[1][0],,,,, 4, 8 >> s->ss_v, w1, h1, 0);
            }

            if (b->comp) {
                mc_luma_dir(s, mc[4][b->filter][1], s->dst[0], ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, col << 3, &b->mv[0][1],,,,, 4, 8, w2, h2, 1);
                mc_luma_dir(s, mc[4][b->filter][1], s->dst[0] + 4 * bytesperpixel, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, (col << 3) + 4, &b->mv[1][1],,,,, 4, 8, w2, h2, 1);
                h2 = (h2 + s->ss_v) >> s->ss_v;
                if (s->ss_h) {
                    w2 = (w2 + 1) >> 1;
                    uvmv = ROUNDED_DIV_MVx2(b->mv[0][1], b->mv[1][1]);
                    mc_chroma_dir(s, mc[4][b->filter][1],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref2->data[1], ref2->linesize[1],
                                  ref2->data[2], ref2->linesize[2], tref2,
                                  row << (3 - s->ss_v), col << 2,
                                  &uvmv,,,,, 4, 8 >> s->ss_v, w2, h2, 1);
                } else {
                    mc_chroma_dir(s, mc[4][b->filter][1],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref2->data[1], ref2->linesize[1],
                                  ref2->data[2], ref2->linesize[2], tref2,
                                  row << (3 - s->ss_v), col << 3,
                                  &b->mv[0][1],,,,, 4, 8 >> s->ss_v, w2, h2, 1);
                    mc_chroma_dir(s, mc[4][b->filter][1],
                                  s->dst[1] + 4 * bytesperpixel,
                                  s->dst[2] + 4 * bytesperpixel, ls_uv,
                                  ref2->data[1], ref2->linesize[1],
                                  ref2->data[2], ref2->linesize[2], tref2,
                                  row << (3 - s->ss_v), (col << 3) + 4,
                                  &b->mv[1][1],,,,, 4, 8 >> s->ss_v, w2, h2, 1);
                }
            }
        } else
#endif
        {
            av_assert2(b->bs == BS_4x4);

            // FIXME if two horizontally adjacent blocks have the same MV,
            // do a w8 instead of a w4 call
            mc_luma_dir(s, mc[4][b->filter][0], s->dst[0], ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, col << 3, &b->mv[0][0],
                        0, 0, 8, 8, 4, 4, w1, h1, 0);
            mc_luma_dir(s, mc[4][b->filter][0], s->dst[0] + 4 * bytesperpixel, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, (col << 3) + 4, &b->mv[1][0],
                        4, 0, 8, 8, 4, 4, w1, h1, 0);
            mc_luma_dir(s, mc[4][b->filter][0],
                        s->dst[0] + 4 * ls_y, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        (row << 3) + 4, col << 3, &b->mv[2][0],
                        0, 4, 8, 8, 4, 4, w1, h1, 0);
            mc_luma_dir(s, mc[4][b->filter][0],
                        s->dst[0] + 4 * ls_y + 4 * bytesperpixel, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        (row << 3) + 4, (col << 3) + 4, &b->mv[3][0],
                        4, 4, 8, 8, 4, 4, w1, h1, 0);
            if (s->ss_v) {
                h1 = (h1 + 1) >> 1;
                if (s->ss_h) {
                    w1 = (w1 + 1) >> 1;
                    uvmv = ROUNDED_DIV_MVx4(b->mv[0][0], b->mv[1][0],
                                            b->mv[2][0], b->mv[3][0]);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  row << 2, col << 2,
                                  &uvmv, 0, 0, 4, 4, 4, 4, w1, h1, 0);
                } else {
                    uvmv = ROUNDED_DIV_MVx2(b->mv[0][0], b->mv[2][0]);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  row << 2, col << 3,
                                  &uvmv, 0, 0, 8, 4, 4, 4, w1, h1, 0);
                    uvmv = ROUNDED_DIV_MVx2(b->mv[1][0], b->mv[3][0]);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1] + 4 * bytesperpixel,
                                  s->dst[2] + 4 * bytesperpixel, ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  row << 2, (col << 3) + 4,
                                  &uvmv, 4, 0, 8, 4, 4, 4, w1, h1, 0);
                }
            } else {
                if (s->ss_h) {
                    w1 = (w1 + 1) >> 1;
                    uvmv = ROUNDED_DIV_MVx2(b->mv[0][0], b->mv[1][0]);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  row << 3, col << 2,
                                  &uvmv, 0, 0, 4, 8, 4, 4, w1, h1, 0);
                    // BUG libvpx uses wrong block index for 4:2:2 bs=4x4
                    // bottom block
                    // https://code.google.com/p/webm/issues/detail?id=993
                    uvmv = ROUNDED_DIV_MVx2(b->mv[1][0], b->mv[2][0]);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1] + 4 * ls_uv, s->dst[2] + 4 * ls_uv, ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  (row << 3) + 4, col << 2,
                                  &uvmv, 0, 4, 4, 8, 4, 4, w1, h1, 0);
                } else {
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1], s->dst[2], ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  row << 3, col << 3,
                                  &b->mv[0][0], 0, 0, 8, 8, 4, 4, w1, h1, 0);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1] + 4 * bytesperpixel,
                                  s->dst[2] + 4 * bytesperpixel, ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  row << 3, (col << 3) + 4,
                                  &b->mv[1][0], 4, 0, 8, 8, 4, 4, w1, h1, 0);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1] + 4 * ls_uv, s->dst[2] + 4 * ls_uv, ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  (row << 3) + 4, col << 3,
                                  &b->mv[2][0], 0, 4, 8, 8, 4, 4, w1, h1, 0);
                    mc_chroma_dir(s, mc[4][b->filter][0],
                                  s->dst[1] + 4 * ls_uv + 4 * bytesperpixel,
                                  s->dst[2] + 4 * ls_uv + 4 * bytesperpixel, ls_uv,
                                  ref1->data[1], ref1->linesize[1],
                                  ref1->data[2], ref1->linesize[2], tref1,
                                  (row << 3) + 4, (col << 3) + 4,
                                  &b->mv[3][0], 4, 4, 8, 8, 4, 4, w1, h1, 0);
                }
            }

            if (b->comp) {
                mc_luma_dir(s, mc[4][b->filter][1], s->dst[0], ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, col << 3, &b->mv[0][1], 0, 0, 8, 8, 4, 4, w2, h2, 1);
                mc_luma_dir(s, mc[4][b->filter][1], s->dst[0] + 4 * bytesperpixel, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, (col << 3) + 4, &b->mv[1][1], 4, 0, 8, 8, 4, 4, w2, h2, 1);
                mc_luma_dir(s, mc[4][b->filter][1],
                            s->dst[0] + 4 * ls_y, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            (row << 3) + 4, col << 3, &b->mv[2][1], 0, 4, 8, 8, 4, 4, w2, h2, 1);
                mc_luma_dir(s, mc[4][b->filter][1],
                            s->dst[0] + 4 * ls_y + 4 * bytesperpixel, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            (row << 3) + 4, (col << 3) + 4, &b->mv[3][1], 4, 4, 8, 8, 4, 4, w2, h2, 1);
                if (s->ss_v) {
                    h2 = (h2 + 1) >> 1;
                    if (s->ss_h) {
                        w2 = (w2 + 1) >> 1;
                        uvmv = ROUNDED_DIV_MVx4(b->mv[0][1], b->mv[1][1],
                                                b->mv[2][1], b->mv[3][1]);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1], s->dst[2], ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      row << 2, col << 2,
                                      &uvmv, 0, 0, 4, 4, 4, 4, w2, h2, 1);
                    } else {
                        uvmv = ROUNDED_DIV_MVx2(b->mv[0][1], b->mv[2][1]);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1], s->dst[2], ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      row << 2, col << 3,
                                      &uvmv, 0, 0, 8, 4, 4, 4, w2, h2, 1);
                        uvmv = ROUNDED_DIV_MVx2(b->mv[1][1], b->mv[3][1]);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1] + 4 * bytesperpixel,
                                      s->dst[2] + 4 * bytesperpixel, ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      row << 2, (col << 3) + 4,
                                      &uvmv, 4, 0, 8, 4, 4, 4, w2, h2, 1);
                    }
                } else {
                    if (s->ss_h) {
                        w2 = (w2 + 1) >> 1;
                        uvmv = ROUNDED_DIV_MVx2(b->mv[0][1], b->mv[1][1]);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1], s->dst[2], ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      row << 3, col << 2,
                                      &uvmv, 0, 0, 4, 8, 4, 4, w2, h2, 1);
                        // BUG libvpx uses wrong block index for 4:2:2 bs=4x4
                        // bottom block
                        // https://code.google.com/p/webm/issues/detail?id=993
                        uvmv = ROUNDED_DIV_MVx2(b->mv[1][1], b->mv[2][1]);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1] + 4 * ls_uv, s->dst[2] + 4 * ls_uv, ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      (row << 3) + 4, col << 2,
                                      &uvmv, 0, 4, 4, 8, 4, 4, w2, h2, 1);
                    } else {
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1], s->dst[2], ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      row << 3, col << 3,
                                      &b->mv[0][1], 0, 0, 8, 8, 4, 4, w2, h2, 1);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1] + 4 * bytesperpixel,
                                      s->dst[2] + 4 * bytesperpixel, ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      row << 3, (col << 3) + 4,
                                      &b->mv[1][1], 4, 0, 8, 8, 4, 4, w2, h2, 1);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1] + 4 * ls_uv, s->dst[2] + 4 * ls_uv, ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      (row << 3) + 4, col << 3,
                                      &b->mv[2][1], 0, 4, 8, 8, 4, 4, w2, h2, 1);
                        mc_chroma_dir(s, mc[4][b->filter][1],
                                      s->dst[1] + 4 * ls_uv + 4 * bytesperpixel,
                                      s->dst[2] + 4 * ls_uv + 4 * bytesperpixel, ls_uv,
                                      ref2->data[1], ref2->linesize[1],
                                      ref2->data[2], ref2->linesize[2], tref2,
                                      (row << 3) + 4, (col << 3) + 4,
                                      &b->mv[3][1], 4, 4, 8, 8, 4, 4, w2, h2, 1);
                    }
                }
            }
        }
    } else {
        int bwl = bwlog_tab[0][b->bs];
        int bw = bwh_tab[0][b->bs][0] * 4, bh = bwh_tab[0][b->bs][1] * 4;
        int uvbw = bwh_tab[s->ss_h][b->bs][0] * 4, uvbh = bwh_tab[s->ss_v][b->bs][1] * 4;

        mc_luma_dir(s, mc[bwl][b->filter][0], s->dst[0], ls_y,
                    ref1->data[0], ref1->linesize[0], tref1,
                    row << 3, col << 3, &b->mv[0][0], 0, 0, bw, bh, bw, bh, w1, h1, 0);
        w1 = (w1 + s->ss_h) >> s->ss_h;
        h1 = (h1 + s->ss_v) >> s->ss_v;
        mc_chroma_dir(s, mc[bwl + s->ss_h][b->filter][0],
                      s->dst[1], s->dst[2], ls_uv,
                      ref1->data[1], ref1->linesize[1],
                      ref1->data[2], ref1->linesize[2], tref1,
                      row << (3 - s->ss_v), col << (3 - s->ss_h),
                      &b->mv[0][0], 0, 0, uvbw, uvbh, uvbw, uvbh, w1, h1, 0);

        if (b->comp) {
            mc_luma_dir(s, mc[bwl][b->filter][1], s->dst[0], ls_y,
                        ref2->data[0], ref2->linesize[0], tref2,
                        row << 3, col << 3, &b->mv[0][1], 0, 0, bw, bh, bw, bh, w2, h2, 1);
            w2 = (w2 + s->ss_h) >> s->ss_h;
            h2 = (h2 + s->ss_v) >> s->ss_v;
            mc_chroma_dir(s, mc[bwl + s->ss_h][b->filter][1],
                          s->dst[1], s->dst[2], ls_uv,
                          ref2->data[1], ref2->linesize[1],
                          ref2->data[2], ref2->linesize[2], tref2,
                          row << (3 - s->ss_v), col << (3 - s->ss_h),
                          &b->mv[0][1], 0, 0, uvbw, uvbh, uvbw, uvbh, w2, h2, 1);
        }
    }
}
