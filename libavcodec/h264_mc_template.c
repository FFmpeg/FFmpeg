/*
 * H.26L/H.264/AVC/JVT/14496-10/... decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

#undef MCFUNC

#if   CHROMA_IDC == 1
#   define MCFUNC(n) FUNC(n ## _420)
#elif CHROMA_IDC == 2
#   define MCFUNC(n) FUNC(n ## _422)
#elif CHROMA_IDC == 3
#   define MCFUNC(n) FUNC(n ## _444)
#endif

#undef  mc_part
#define mc_part MCFUNC(mc_part)

static void mc_part(H264Context *h, int n, int square,
                    int height, int delta,
                    uint8_t *dest_y, uint8_t *dest_cb,
                    uint8_t *dest_cr,
                    int x_offset, int y_offset,
                    qpel_mc_func *qpix_put,
                    h264_chroma_mc_func chroma_put,
                    qpel_mc_func *qpix_avg,
                    h264_chroma_mc_func chroma_avg,
                    h264_weight_func *weight_op,
                    h264_biweight_func *weight_avg,
                    int list0, int list1)
{
    if ((h->use_weight == 2 && list0 && list1 &&
         (h->implicit_weight[h->ref_cache[0][scan8[n]]][h->ref_cache[1][scan8[n]]][h->s.mb_y & 1] != 32)) ||
        h->use_weight == 1)
        mc_part_weighted(h, n, square, height, delta, dest_y, dest_cb, dest_cr,
                         x_offset, y_offset, qpix_put, chroma_put,
                         weight_op[0], weight_op[1], weight_avg[0],
                         weight_avg[1], list0, list1, PIXEL_SHIFT, CHROMA_IDC);
    else
        mc_part_std(h, n, square, height, delta, dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put, qpix_avg,
                    chroma_avg, list0, list1, PIXEL_SHIFT, CHROMA_IDC);
}

static void MCFUNC(hl_motion)(H264Context *h, uint8_t *dest_y,
                              uint8_t *dest_cb, uint8_t *dest_cr,
                              qpel_mc_func(*qpix_put)[16],
                              h264_chroma_mc_func(*chroma_put),
                              qpel_mc_func(*qpix_avg)[16],
                              h264_chroma_mc_func(*chroma_avg),
                              h264_weight_func *weight_op,
                              h264_biweight_func *weight_avg)
{
    MpegEncContext *const s = &h->s;
    const int mb_xy   = h->mb_xy;
    const int mb_type = s->current_picture.f.mb_type[mb_xy];

    assert(IS_INTER(mb_type));

    if (HAVE_THREADS && (s->avctx->active_thread_type & FF_THREAD_FRAME))
        await_references(h);
    prefetch_motion(h, 0, PIXEL_SHIFT, CHROMA_IDC);

    if (IS_16X16(mb_type)) {
        mc_part(h, 0, 1, 16, 0, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[0], chroma_put[0], qpix_avg[0], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
    } else if (IS_16X8(mb_type)) {
        mc_part(h, 0, 0, 8, 8 << PIXEL_SHIFT, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 8, 0, 8, 8 << PIXEL_SHIFT, dest_y, dest_cb, dest_cr, 0, 4,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    } else if (IS_8X16(mb_type)) {
        mc_part(h, 0, 0, 16, 8 * h->mb_linesize, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 4, 0, 16, 8 * h->mb_linesize, dest_y, dest_cb, dest_cr, 4, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    } else {
        int i;

        assert(IS_8X8(mb_type));

        for (i = 0; i < 4; i++) {
            const int sub_mb_type = h->sub_mb_type[i];
            const int n  = 4 * i;
            int x_offset = (i & 1) << 2;
            int y_offset = (i & 2) << 1;

            if (IS_SUB_8X8(sub_mb_type)) {
                mc_part(h, n, 1, 8, 0, dest_y, dest_cb, dest_cr,
                        x_offset, y_offset,
                        qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                        &weight_op[1], &weight_avg[1],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            } else if (IS_SUB_8X4(sub_mb_type)) {
                mc_part(h, n, 0, 4, 4 << PIXEL_SHIFT, dest_y, dest_cb, dest_cr,
                        x_offset, y_offset,
                        qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                        &weight_op[1], &weight_avg[1],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n + 2, 0, 4, 4 << PIXEL_SHIFT,
                        dest_y, dest_cb, dest_cr, x_offset, y_offset + 2,
                        qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                        &weight_op[1], &weight_avg[1],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            } else if (IS_SUB_4X8(sub_mb_type)) {
                mc_part(h, n, 0, 8, 4 * h->mb_linesize,
                        dest_y, dest_cb, dest_cr, x_offset, y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        &weight_op[2], &weight_avg[2],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n + 1, 0, 8, 4 * h->mb_linesize,
                        dest_y, dest_cb, dest_cr, x_offset + 2, y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        &weight_op[2], &weight_avg[2],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            } else {
                int j;
                assert(IS_SUB_4X4(sub_mb_type));
                for (j = 0; j < 4; j++) {
                    int sub_x_offset = x_offset + 2 * (j & 1);
                    int sub_y_offset = y_offset + (j & 2);
                    mc_part(h, n + j, 1, 4, 0,
                            dest_y, dest_cb, dest_cr, sub_x_offset, sub_y_offset,
                            qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                            &weight_op[2], &weight_avg[2],
                            IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                }
            }
        }
    }

    prefetch_motion(h, 1, PIXEL_SHIFT, CHROMA_IDC);
}

