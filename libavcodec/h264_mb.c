/*
 * H.26L/H.264/AVC/JVT/14496-10/... decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG-4 part10 macroblock decoding
 */

#include <stdint.h>

#include "config.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "h264dec.h"
#include "h264_ps.h"
#include "qpeldsp.h"
#include "rectangle.h"
#include "threadframe.h"

static inline int get_lowest_part_list_y(H264SliceContext *sl,
                                         int n, int height, int y_offset, int list)
{
    int raw_my             = sl->mv_cache[list][scan8[n]][1];
    int filter_height_down = (raw_my & 3) ? 3 : 0;
    int full_my            = (raw_my >> 2) + y_offset;
    int bottom             = full_my + filter_height_down + height;

    av_assert2(height >= 0);

    return FFMAX(0, bottom);
}

static inline void get_lowest_part_y(const H264Context *h, H264SliceContext *sl,
                                     int16_t refs[2][48], int n,
                                     int height, int y_offset, int list0,
                                     int list1, int *nrefs)
{
    int my;

    y_offset += 16 * (sl->mb_y >> MB_FIELD(sl));

    if (list0) {
        int ref_n = sl->ref_cache[0][scan8[n]];
        H264Ref *ref = &sl->ref_list[0][ref_n];

        // Error resilience puts the current picture in the ref list.
        // Don't try to wait on these as it will cause a deadlock.
        // Fields can wait on each other, though.
        if (ref->parent->tf.progress != h->cur_pic.tf.progress ||
            (ref->reference & 3) != h->picture_structure) {
            my = get_lowest_part_list_y(sl, n, height, y_offset, 0);
            if (refs[0][ref_n] < 0)
                nrefs[0] += 1;
            refs[0][ref_n] = FFMAX(refs[0][ref_n], my);
        }
    }

    if (list1) {
        int ref_n    = sl->ref_cache[1][scan8[n]];
        H264Ref *ref = &sl->ref_list[1][ref_n];

        if (ref->parent->tf.progress != h->cur_pic.tf.progress ||
            (ref->reference & 3) != h->picture_structure) {
            my = get_lowest_part_list_y(sl, n, height, y_offset, 1);
            if (refs[1][ref_n] < 0)
                nrefs[1] += 1;
            refs[1][ref_n] = FFMAX(refs[1][ref_n], my);
        }
    }
}

/**
 * Wait until all reference frames are available for MC operations.
 *
 * @param h the H.264 context
 */
static void await_references(const H264Context *h, H264SliceContext *sl)
{
    const int mb_xy   = sl->mb_xy;
    const int mb_type = h->cur_pic.mb_type[mb_xy];
    int16_t refs[2][48];
    int nrefs[2] = { 0 };
    int ref, list;

    memset(refs, -1, sizeof(refs));

    if (IS_16X16(mb_type)) {
        get_lowest_part_y(h, sl, refs, 0, 16, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
    } else if (IS_16X8(mb_type)) {
        get_lowest_part_y(h, sl, refs, 0, 8, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, sl, refs, 8, 8, 8,
                          IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    } else if (IS_8X16(mb_type)) {
        get_lowest_part_y(h, sl, refs, 0, 16, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, sl, refs, 4, 16, 0,
                          IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    } else {
        int i;

        av_assert2(IS_8X8(mb_type));

        for (i = 0; i < 4; i++) {
            const int sub_mb_type = sl->sub_mb_type[i];
            const int n           = 4 * i;
            int y_offset          = (i & 2) << 2;

            if (IS_SUB_8X8(sub_mb_type)) {
                get_lowest_part_y(h, sl, refs, n, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else if (IS_SUB_8X4(sub_mb_type)) {
                get_lowest_part_y(h, sl, refs, n, 4, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
                get_lowest_part_y(h, sl, refs, n + 2, 4, y_offset + 4,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else if (IS_SUB_4X8(sub_mb_type)) {
                get_lowest_part_y(h, sl, refs, n, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
                get_lowest_part_y(h, sl, refs, n + 1, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else {
                int j;
                av_assert2(IS_SUB_4X4(sub_mb_type));
                for (j = 0; j < 4; j++) {
                    int sub_y_offset = y_offset + 2 * (j & 2);
                    get_lowest_part_y(h, sl, refs, n + j, 4, sub_y_offset,
                                      IS_DIR(sub_mb_type, 0, 0),
                                      IS_DIR(sub_mb_type, 0, 1),
                                      nrefs);
                }
            }
        }
    }

    for (list = sl->list_count - 1; list >= 0; list--)
        for (ref = 0; ref < 48 && nrefs[list]; ref++) {
            int row = refs[list][ref];
            if (row >= 0) {
                H264Ref *ref_pic  = &sl->ref_list[list][ref];
                int ref_field         = ref_pic->reference - 1;
                int ref_field_picture = ref_pic->parent->field_picture;
                int pic_height        = 16 * h->mb_height >> ref_field_picture;

                row <<= MB_MBAFF(sl);
                nrefs[list]--;

                if (!FIELD_PICTURE(h) && ref_field_picture) { // frame referencing two fields
                    av_assert2((ref_pic->parent->reference & 3) == 3);
                    ff_thread_await_progress(&ref_pic->parent->tf,
                                             FFMIN((row >> 1) - !(row & 1),
                                                   pic_height - 1),
                                             1);
                    ff_thread_await_progress(&ref_pic->parent->tf,
                                             FFMIN((row >> 1), pic_height - 1),
                                             0);
                } else if (FIELD_PICTURE(h) && !ref_field_picture) { // field referencing one field of a frame
                    ff_thread_await_progress(&ref_pic->parent->tf,
                                             FFMIN(row * 2 + ref_field,
                                                   pic_height - 1),
                                             0);
                } else if (FIELD_PICTURE(h)) {
                    ff_thread_await_progress(&ref_pic->parent->tf,
                                             FFMIN(row, pic_height - 1),
                                             ref_field);
                } else {
                    ff_thread_await_progress(&ref_pic->parent->tf,
                                             FFMIN(row, pic_height - 1),
                                             0);
                }
            }
        }
}

static av_always_inline void mc_dir_part(const H264Context *h, H264SliceContext *sl,
                                         H264Ref *pic,
                                         int n, int square, int height,
                                         int delta, int list,
                                         uint8_t *dest_y, uint8_t *dest_cb,
                                         uint8_t *dest_cr,
                                         int src_x_offset, int src_y_offset,
                                         const qpel_mc_func *qpix_op,
                                         h264_chroma_mc_func chroma_op,
                                         int pixel_shift, int chroma_idc)
{
    const int mx      = sl->mv_cache[list][scan8[n]][0] + src_x_offset * 8;
    int my            = sl->mv_cache[list][scan8[n]][1] + src_y_offset * 8;
    const int luma_xy = (mx & 3) + ((my & 3) << 2);
    ptrdiff_t offset  = (mx >> 2) * (1 << pixel_shift) + (my >> 2) * sl->mb_linesize;
    uint8_t *src_y    = pic->data[0] + offset;
    uint8_t *src_cb, *src_cr;
    int extra_width  = 0;
    int extra_height = 0;
    int emu = 0;
    const int full_mx    = mx >> 2;
    const int full_my    = my >> 2;
    const int pic_width  = 16 * h->mb_width;
    const int pic_height = 16 * h->mb_height >> MB_FIELD(sl);
    int ysh;

    if (mx & 7)
        extra_width -= 3;
    if (my & 7)
        extra_height -= 3;

    if (full_mx                <          0 - extra_width  ||
        full_my                <          0 - extra_height ||
        full_mx + 16 /*FIXME*/ > pic_width  + extra_width  ||
        full_my + 16 /*FIXME*/ > pic_height + extra_height) {
        h->vdsp.emulated_edge_mc(sl->edge_emu_buffer,
                                 src_y - (2 << pixel_shift) - 2 * sl->mb_linesize,
                                 sl->mb_linesize, sl->mb_linesize,
                                 16 + 5, 16 + 5 /*FIXME*/, full_mx - 2,
                                 full_my - 2, pic_width, pic_height);
        src_y = sl->edge_emu_buffer + (2 << pixel_shift) + 2 * sl->mb_linesize;
        emu   = 1;
    }

    qpix_op[luma_xy](dest_y, src_y, sl->mb_linesize); // FIXME try variable height perhaps?
    if (!square)
        qpix_op[luma_xy](dest_y + delta, src_y + delta, sl->mb_linesize);

    if (CONFIG_GRAY && h->flags & AV_CODEC_FLAG_GRAY)
        return;

    if (chroma_idc == 3 /* yuv444 */) {
        src_cb = pic->data[1] + offset;
        if (emu) {
            h->vdsp.emulated_edge_mc(sl->edge_emu_buffer,
                                     src_cb - (2 << pixel_shift) - 2 * sl->mb_linesize,
                                     sl->mb_linesize, sl->mb_linesize,
                                     16 + 5, 16 + 5 /*FIXME*/,
                                     full_mx - 2, full_my - 2,
                                     pic_width, pic_height);
            src_cb = sl->edge_emu_buffer + (2 << pixel_shift) + 2 * sl->mb_linesize;
        }
        qpix_op[luma_xy](dest_cb, src_cb, sl->mb_linesize); // FIXME try variable height perhaps?
        if (!square)
            qpix_op[luma_xy](dest_cb + delta, src_cb + delta, sl->mb_linesize);

        src_cr = pic->data[2] + offset;
        if (emu) {
            h->vdsp.emulated_edge_mc(sl->edge_emu_buffer,
                                     src_cr - (2 << pixel_shift) - 2 * sl->mb_linesize,
                                     sl->mb_linesize, sl->mb_linesize,
                                     16 + 5, 16 + 5 /*FIXME*/,
                                     full_mx - 2, full_my - 2,
                                     pic_width, pic_height);
            src_cr = sl->edge_emu_buffer + (2 << pixel_shift) + 2 * sl->mb_linesize;
        }
        qpix_op[luma_xy](dest_cr, src_cr, sl->mb_linesize); // FIXME try variable height perhaps?
        if (!square)
            qpix_op[luma_xy](dest_cr + delta, src_cr + delta, sl->mb_linesize);
        return;
    }

    ysh = 3 - (chroma_idc == 2 /* yuv422 */);
    if (chroma_idc == 1 /* yuv420 */ && MB_FIELD(sl)) {
        // chroma offset when predicting from a field of opposite parity
        my  += 2 * ((sl->mb_y & 1) - (pic->reference - 1));
        emu |= (my >> 3) < 0 || (my >> 3) + 8 >= (pic_height >> 1);
    }

    src_cb = pic->data[1] + ((mx >> 3) * (1 << pixel_shift)) +
             (my >> ysh) * sl->mb_uvlinesize;
    src_cr = pic->data[2] + ((mx >> 3) * (1 << pixel_shift)) +
             (my >> ysh) * sl->mb_uvlinesize;

    if (emu) {
        h->vdsp.emulated_edge_mc(sl->edge_emu_buffer, src_cb,
                                 sl->mb_uvlinesize, sl->mb_uvlinesize,
                                 9, 8 * chroma_idc + 1, (mx >> 3), (my >> ysh),
                                 pic_width >> 1, pic_height >> (chroma_idc == 1 /* yuv420 */));
        src_cb = sl->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, sl->mb_uvlinesize,
              height >> (chroma_idc == 1 /* yuv420 */),
              mx & 7, ((unsigned)my << (chroma_idc == 2 /* yuv422 */)) & 7);

    if (emu) {
        h->vdsp.emulated_edge_mc(sl->edge_emu_buffer, src_cr,
                                 sl->mb_uvlinesize, sl->mb_uvlinesize,
                                 9, 8 * chroma_idc + 1, (mx >> 3), (my >> ysh),
                                 pic_width >> 1, pic_height >> (chroma_idc == 1 /* yuv420 */));
        src_cr = sl->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, sl->mb_uvlinesize, height >> (chroma_idc == 1 /* yuv420 */),
              mx & 7, ((unsigned)my << (chroma_idc == 2 /* yuv422 */)) & 7);
}

static av_always_inline void mc_part_std(const H264Context *h, H264SliceContext *sl,
                                         int n, int square,
                                         int height, int delta,
                                         uint8_t *dest_y, uint8_t *dest_cb,
                                         uint8_t *dest_cr,
                                         int x_offset, int y_offset,
                                         const qpel_mc_func *qpix_put,
                                         h264_chroma_mc_func chroma_put,
                                         const qpel_mc_func *qpix_avg,
                                         h264_chroma_mc_func chroma_avg,
                                         int list0, int list1,
                                         int pixel_shift, int chroma_idc)
{
    const qpel_mc_func *qpix_op   = qpix_put;
    h264_chroma_mc_func chroma_op = chroma_put;

    dest_y += (2 * x_offset << pixel_shift) + 2 * y_offset * sl->mb_linesize;
    if (chroma_idc == 3 /* yuv444 */) {
        dest_cb += (2 * x_offset << pixel_shift) + 2 * y_offset * sl->mb_linesize;
        dest_cr += (2 * x_offset << pixel_shift) + 2 * y_offset * sl->mb_linesize;
    } else if (chroma_idc == 2 /* yuv422 */) {
        dest_cb += (x_offset << pixel_shift) + 2 * y_offset * sl->mb_uvlinesize;
        dest_cr += (x_offset << pixel_shift) + 2 * y_offset * sl->mb_uvlinesize;
    } else { /* yuv420 */
        dest_cb += (x_offset << pixel_shift) + y_offset * sl->mb_uvlinesize;
        dest_cr += (x_offset << pixel_shift) + y_offset * sl->mb_uvlinesize;
    }
    x_offset += 8 * sl->mb_x;
    y_offset += 8 * (sl->mb_y >> MB_FIELD(sl));

    if (list0) {
        H264Ref *ref = &sl->ref_list[0][sl->ref_cache[0][scan8[n]]];
        mc_dir_part(h, sl, ref, n, square, height, delta, 0,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, pixel_shift, chroma_idc);

        qpix_op   = qpix_avg;
        chroma_op = chroma_avg;
    }

    if (list1) {
        H264Ref *ref = &sl->ref_list[1][sl->ref_cache[1][scan8[n]]];
        mc_dir_part(h, sl, ref, n, square, height, delta, 1,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, pixel_shift, chroma_idc);
    }
}

static av_always_inline void mc_part_weighted(const H264Context *h, H264SliceContext *sl,
                                              int n, int square,
                                              int height, int delta,
                                              uint8_t *dest_y, uint8_t *dest_cb,
                                              uint8_t *dest_cr,
                                              int x_offset, int y_offset,
                                              const qpel_mc_func *qpix_put,
                                              h264_chroma_mc_func chroma_put,
                                              h264_weight_func luma_weight_op,
                                              h264_weight_func chroma_weight_op,
                                              h264_biweight_func luma_weight_avg,
                                              h264_biweight_func chroma_weight_avg,
                                              int list0, int list1,
                                              int pixel_shift, int chroma_idc)
{
    int chroma_height;

    dest_y += (2 * x_offset << pixel_shift) + 2 * y_offset * sl->mb_linesize;
    if (chroma_idc == 3 /* yuv444 */) {
        chroma_height     = height;
        chroma_weight_avg = luma_weight_avg;
        chroma_weight_op  = luma_weight_op;
        dest_cb += (2 * x_offset << pixel_shift) + 2 * y_offset * sl->mb_linesize;
        dest_cr += (2 * x_offset << pixel_shift) + 2 * y_offset * sl->mb_linesize;
    } else if (chroma_idc == 2 /* yuv422 */) {
        chroma_height = height;
        dest_cb      += (x_offset << pixel_shift) + 2 * y_offset * sl->mb_uvlinesize;
        dest_cr      += (x_offset << pixel_shift) + 2 * y_offset * sl->mb_uvlinesize;
    } else { /* yuv420 */
        chroma_height = height >> 1;
        dest_cb      += (x_offset << pixel_shift) + y_offset * sl->mb_uvlinesize;
        dest_cr      += (x_offset << pixel_shift) + y_offset * sl->mb_uvlinesize;
    }
    x_offset += 8 * sl->mb_x;
    y_offset += 8 * (sl->mb_y >> MB_FIELD(sl));

    if (list0 && list1) {
        /* don't optimize for luma-only case, since B-frames usually
         * use implicit weights => chroma too. */
        uint8_t *tmp_cb = sl->bipred_scratchpad;
        uint8_t *tmp_cr = sl->bipred_scratchpad + (8 << pixel_shift + (chroma_idc == 3));
        uint8_t *tmp_y  = sl->bipred_scratchpad + 16 * sl->mb_uvlinesize;
        int refn0       = sl->ref_cache[0][scan8[n]];
        int refn1       = sl->ref_cache[1][scan8[n]];

        mc_dir_part(h, sl, &sl->ref_list[0][refn0], n, square, height, delta, 0,
                    dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put,
                    pixel_shift, chroma_idc);
        mc_dir_part(h, sl, &sl->ref_list[1][refn1], n, square, height, delta, 1,
                    tmp_y, tmp_cb, tmp_cr,
                    x_offset, y_offset, qpix_put, chroma_put,
                    pixel_shift, chroma_idc);

        if (sl->pwt.use_weight == 2) {
            int weight0 = sl->pwt.implicit_weight[refn0][refn1][sl->mb_y & 1];
            int weight1 = 64 - weight0;
            luma_weight_avg(dest_y, tmp_y, sl->mb_linesize,
                            height, 5, weight0, weight1, 0);
            if (!CONFIG_GRAY || !(h->flags & AV_CODEC_FLAG_GRAY)) {
                chroma_weight_avg(dest_cb, tmp_cb, sl->mb_uvlinesize,
                                  chroma_height, 5, weight0, weight1, 0);
                chroma_weight_avg(dest_cr, tmp_cr, sl->mb_uvlinesize,
                                  chroma_height, 5, weight0, weight1, 0);
            }
        } else {
            luma_weight_avg(dest_y, tmp_y, sl->mb_linesize, height,
                            sl->pwt.luma_log2_weight_denom,
                            sl->pwt.luma_weight[refn0][0][0],
                            sl->pwt.luma_weight[refn1][1][0],
                            sl->pwt.luma_weight[refn0][0][1] +
                            sl->pwt.luma_weight[refn1][1][1]);
            if (!CONFIG_GRAY || !(h->flags & AV_CODEC_FLAG_GRAY)) {
                chroma_weight_avg(dest_cb, tmp_cb, sl->mb_uvlinesize, chroma_height,
                                  sl->pwt.chroma_log2_weight_denom,
                                  sl->pwt.chroma_weight[refn0][0][0][0],
                                  sl->pwt.chroma_weight[refn1][1][0][0],
                                  sl->pwt.chroma_weight[refn0][0][0][1] +
                                  sl->pwt.chroma_weight[refn1][1][0][1]);
                chroma_weight_avg(dest_cr, tmp_cr, sl->mb_uvlinesize, chroma_height,
                                  sl->pwt.chroma_log2_weight_denom,
                                  sl->pwt.chroma_weight[refn0][0][1][0],
                                  sl->pwt.chroma_weight[refn1][1][1][0],
                                  sl->pwt.chroma_weight[refn0][0][1][1] +
                                  sl->pwt.chroma_weight[refn1][1][1][1]);
            }
        }
    } else {
        int list     = list1 ? 1 : 0;
        int refn     = sl->ref_cache[list][scan8[n]];
        H264Ref *ref = &sl->ref_list[list][refn];
        mc_dir_part(h, sl, ref, n, square, height, delta, list,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put, chroma_put, pixel_shift, chroma_idc);

        luma_weight_op(dest_y, sl->mb_linesize, height,
                       sl->pwt.luma_log2_weight_denom,
                       sl->pwt.luma_weight[refn][list][0],
                       sl->pwt.luma_weight[refn][list][1]);
        if (!CONFIG_GRAY || !(h->flags & AV_CODEC_FLAG_GRAY)) {
            if (sl->pwt.use_weight_chroma) {
                chroma_weight_op(dest_cb, sl->mb_uvlinesize, chroma_height,
                                 sl->pwt.chroma_log2_weight_denom,
                                 sl->pwt.chroma_weight[refn][list][0][0],
                                 sl->pwt.chroma_weight[refn][list][0][1]);
                chroma_weight_op(dest_cr, sl->mb_uvlinesize, chroma_height,
                                 sl->pwt.chroma_log2_weight_denom,
                                 sl->pwt.chroma_weight[refn][list][1][0],
                                 sl->pwt.chroma_weight[refn][list][1][1]);
            }
        }
    }
}

static av_always_inline void prefetch_motion(const H264Context *h, H264SliceContext *sl,
                                             int list, int pixel_shift,
                                             int chroma_idc)
{
    /* fetch pixels for estimated mv 4 macroblocks ahead
     * optimized for 64byte cache lines */
    const int refn = sl->ref_cache[list][scan8[0]];
    if (refn >= 0) {
        const int mx  = (sl->mv_cache[list][scan8[0]][0] >> 2) + 16 * sl->mb_x + 8;
        const int my  = (sl->mv_cache[list][scan8[0]][1] >> 2) + 16 * sl->mb_y;
        uint8_t **src = sl->ref_list[list][refn].data;
        int off       =  mx * (1<< pixel_shift) +
                        (my + (sl->mb_x & 3) * 4) * sl->mb_linesize +
                        (64 << pixel_shift);
        h->vdsp.prefetch(src[0] + off, sl->linesize, 4);
        if (chroma_idc == 3 /* yuv444 */) {
            h->vdsp.prefetch(src[1] + off, sl->linesize, 4);
            h->vdsp.prefetch(src[2] + off, sl->linesize, 4);
        } else {
            off= ((mx>>1)+64) * (1<<pixel_shift) + ((my>>1) + (sl->mb_x&7))*sl->uvlinesize;
            h->vdsp.prefetch(src[1] + off, src[2] - src[1], 2);
        }
    }
}

static av_always_inline void xchg_mb_border(const H264Context *h, H264SliceContext *sl,
                                            uint8_t *src_y,
                                            uint8_t *src_cb, uint8_t *src_cr,
                                            int linesize, int uvlinesize,
                                            int xchg, int chroma444,
                                            int simple, int pixel_shift)
{
    int deblock_topleft;
    int deblock_top;
    int top_idx = 1;
    uint8_t *top_border_m1;
    uint8_t *top_border;

    if (!simple && FRAME_MBAFF(h)) {
        if (sl->mb_y & 1) {
            if (!MB_MBAFF(sl))
                return;
        } else {
            top_idx = MB_MBAFF(sl) ? 0 : 1;
        }
    }

    if (sl->deblocking_filter == 2) {
        deblock_topleft = h->slice_table[sl->mb_xy - 1 - (h->mb_stride << MB_FIELD(sl))] == sl->slice_num;
        deblock_top     = sl->top_type;
    } else {
        deblock_topleft = (sl->mb_x > 0);
        deblock_top     = (sl->mb_y > !!MB_FIELD(sl));
    }

    src_y  -= linesize   + 1 + pixel_shift;
    src_cb -= uvlinesize + 1 + pixel_shift;
    src_cr -= uvlinesize + 1 + pixel_shift;

    top_border_m1 = sl->top_borders[top_idx][sl->mb_x - 1];
    top_border    = sl->top_borders[top_idx][sl->mb_x];

#define XCHG(a, b, xchg)                        \
    if (pixel_shift) {                          \
        if (xchg) {                             \
            AV_SWAP64(b + 0, a + 0);            \
            AV_SWAP64(b + 8, a + 8);            \
        } else {                                \
            AV_COPY128(b, a);                   \
        }                                       \
    } else if (xchg)                            \
        AV_SWAP64(b, a);                        \
    else                                        \
        AV_COPY64(b, a);

    if (deblock_top) {
        if (deblock_topleft) {
            XCHG(top_border_m1 + (8 << pixel_shift),
                 src_y - (7 << pixel_shift), 1);
        }
        XCHG(top_border + (0 << pixel_shift), src_y + (1 << pixel_shift), xchg);
        XCHG(top_border + (8 << pixel_shift), src_y + (9 << pixel_shift), 1);
        if (sl->mb_x + 1 < h->mb_width) {
            XCHG(sl->top_borders[top_idx][sl->mb_x + 1],
                 src_y + (17 << pixel_shift), 1);
        }
        if (simple || !CONFIG_GRAY || !(h->flags & AV_CODEC_FLAG_GRAY)) {
            if (chroma444) {
                if (deblock_topleft) {
                    XCHG(top_border_m1 + (24 << pixel_shift), src_cb - (7 << pixel_shift), 1);
                    XCHG(top_border_m1 + (40 << pixel_shift), src_cr - (7 << pixel_shift), 1);
                }
                XCHG(top_border + (16 << pixel_shift), src_cb + (1 << pixel_shift), xchg);
                XCHG(top_border + (24 << pixel_shift), src_cb + (9 << pixel_shift), 1);
                XCHG(top_border + (32 << pixel_shift), src_cr + (1 << pixel_shift), xchg);
                XCHG(top_border + (40 << pixel_shift), src_cr + (9 << pixel_shift), 1);
                if (sl->mb_x + 1 < h->mb_width) {
                    XCHG(sl->top_borders[top_idx][sl->mb_x + 1] + (16 << pixel_shift), src_cb + (17 << pixel_shift), 1);
                    XCHG(sl->top_borders[top_idx][sl->mb_x + 1] + (32 << pixel_shift), src_cr + (17 << pixel_shift), 1);
                }
            } else {
                if (deblock_topleft) {
                    XCHG(top_border_m1 + (16 << pixel_shift), src_cb - (7 << pixel_shift), 1);
                    XCHG(top_border_m1 + (24 << pixel_shift), src_cr - (7 << pixel_shift), 1);
                }
                XCHG(top_border + (16 << pixel_shift), src_cb + 1 + pixel_shift, 1);
                XCHG(top_border + (24 << pixel_shift), src_cr + 1 + pixel_shift, 1);
            }
        }
    }
}

static av_always_inline int dctcoef_get(int16_t *mb, int high_bit_depth,
                                        int index)
{
    if (high_bit_depth) {
        return AV_RN32A(((int32_t *)mb) + index);
    } else
        return AV_RN16A(mb + index);
}

static av_always_inline void dctcoef_set(int16_t *mb, int high_bit_depth,
                                         int index, int value)
{
    if (high_bit_depth) {
        AV_WN32A(((int32_t *)mb) + index, value);
    } else
        AV_WN16A(mb + index, value);
}

static av_always_inline void hl_decode_mb_predict_luma(const H264Context *h,
                                                       H264SliceContext *sl,
                                                       int mb_type, int simple,
                                                       int transform_bypass,
                                                       int pixel_shift,
                                                       const int *block_offset,
                                                       int linesize,
                                                       uint8_t *dest_y, int p)
{
    void (*idct_add)(uint8_t *dst, int16_t *block, int stride);
    void (*idct_dc_add)(uint8_t *dst, int16_t *block, int stride);
    int i;
    int qscale = p == 0 ? sl->qscale : sl->chroma_qp[p - 1];
    block_offset += 16 * p;
    if (IS_INTRA4x4(mb_type)) {
        if (IS_8x8DCT(mb_type)) {
            if (transform_bypass) {
                idct_dc_add =
                idct_add    = h->h264dsp.h264_add_pixels8_clear;
            } else {
                idct_dc_add = h->h264dsp.h264_idct8_dc_add;
                idct_add    = h->h264dsp.h264_idct8_add;
            }
            for (i = 0; i < 16; i += 4) {
                uint8_t *const ptr = dest_y + block_offset[i];
                const int dir      = sl->intra4x4_pred_mode_cache[scan8[i]];
                if (transform_bypass && h->ps.sps->profile_idc == 244 && dir <= 1) {
                    if (h->x264_build < 151U) {
                        h->hpc.pred8x8l_add[dir](ptr, sl->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                    } else
                        h->hpc.pred8x8l_filter_add[dir](ptr, sl->mb + (i * 16 + p * 256 << pixel_shift),
                                                        (sl-> topleft_samples_available << i) & 0x8000,
                                                        (sl->topright_samples_available << i) & 0x4000, linesize);
                } else {
                    const int nnz = sl->non_zero_count_cache[scan8[i + p * 16]];
                    h->hpc.pred8x8l[dir](ptr, (sl->topleft_samples_available << i) & 0x8000,
                                         (sl->topright_samples_available << i) & 0x4000, linesize);
                    if (nnz) {
                        if (nnz == 1 && dctcoef_get(sl->mb, pixel_shift, i * 16 + p * 256))
                            idct_dc_add(ptr, sl->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                        else
                            idct_add(ptr, sl->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                    }
                }
            }
        } else {
            if (transform_bypass) {
                idct_dc_add  =
                idct_add     = h->h264dsp.h264_add_pixels4_clear;
            } else {
                idct_dc_add = h->h264dsp.h264_idct_dc_add;
                idct_add    = h->h264dsp.h264_idct_add;
            }
            for (i = 0; i < 16; i++) {
                uint8_t *const ptr = dest_y + block_offset[i];
                const int dir      = sl->intra4x4_pred_mode_cache[scan8[i]];

                if (transform_bypass && h->ps.sps->profile_idc == 244 && dir <= 1) {
                    h->hpc.pred4x4_add[dir](ptr, sl->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                } else {
                    uint8_t *topright;
                    int nnz, tr;
                    uint64_t tr_high;
                    if (dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED) {
                        const int topright_avail = (sl->topright_samples_available << i) & 0x8000;
                        av_assert2(sl->mb_y || linesize <= block_offset[i]);
                        if (!topright_avail) {
                            if (pixel_shift) {
                                tr_high  = ((uint16_t *)ptr)[3 - linesize / 2] * 0x0001000100010001ULL;
                                topright = (uint8_t *)&tr_high;
                            } else {
                                tr       = ptr[3 - linesize] * 0x01010101u;
                                topright = (uint8_t *)&tr;
                            }
                        } else
                            topright = ptr + (4 << pixel_shift) - linesize;
                    } else
                        topright = NULL;

                    h->hpc.pred4x4[dir](ptr, topright, linesize);
                    nnz = sl->non_zero_count_cache[scan8[i + p * 16]];
                    if (nnz) {
                        if (nnz == 1 && dctcoef_get(sl->mb, pixel_shift, i * 16 + p * 256))
                            idct_dc_add(ptr, sl->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                        else
                            idct_add(ptr, sl->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                    }
                }
            }
        }
    } else {
        h->hpc.pred16x16[sl->intra16x16_pred_mode](dest_y, linesize);
        if (sl->non_zero_count_cache[scan8[LUMA_DC_BLOCK_INDEX + p]]) {
            if (!transform_bypass)
                h->h264dsp.h264_luma_dc_dequant_idct(sl->mb + (p * 256 << pixel_shift),
                                                     sl->mb_luma_dc[p],
                                                     h->ps.pps->dequant4_coeff[p][qscale][0]);
            else {
                static const uint8_t dc_mapping[16] = {
                     0 * 16,  1 * 16,  4 * 16,  5 * 16,
                     2 * 16,  3 * 16,  6 * 16,  7 * 16,
                     8 * 16,  9 * 16, 12 * 16, 13 * 16,
                    10 * 16, 11 * 16, 14 * 16, 15 * 16
                };
                for (i = 0; i < 16; i++)
                    dctcoef_set(sl->mb + (p * 256 << pixel_shift),
                                pixel_shift, dc_mapping[i],
                                dctcoef_get(sl->mb_luma_dc[p],
                                            pixel_shift, i));
            }
        }
    }
}

static av_always_inline void hl_decode_mb_idct_luma(const H264Context *h, H264SliceContext *sl,
                                                    int mb_type, int simple,
                                                    int transform_bypass,
                                                    int pixel_shift,
                                                    const int *block_offset,
                                                    int linesize,
                                                    uint8_t *dest_y, int p)
{
    void (*idct_add)(uint8_t *dst, int16_t *block, int stride);
    int i;
    block_offset += 16 * p;
    if (!IS_INTRA4x4(mb_type)) {
        if (IS_INTRA16x16(mb_type)) {
            if (transform_bypass) {
                if (h->ps.sps->profile_idc == 244 &&
                    (sl->intra16x16_pred_mode == VERT_PRED8x8 ||
                     sl->intra16x16_pred_mode == HOR_PRED8x8)) {
                    h->hpc.pred16x16_add[sl->intra16x16_pred_mode](dest_y, block_offset,
                                                                   sl->mb + (p * 256 << pixel_shift),
                                                                   linesize);
                } else {
                    for (i = 0; i < 16; i++)
                        if (sl->non_zero_count_cache[scan8[i + p * 16]] ||
                            dctcoef_get(sl->mb, pixel_shift, i * 16 + p * 256))
                            h->h264dsp.h264_add_pixels4_clear(dest_y + block_offset[i],
                                                              sl->mb + (i * 16 + p * 256 << pixel_shift),
                                                              linesize);
                }
            } else {
                h->h264dsp.h264_idct_add16intra(dest_y, block_offset,
                                                sl->mb + (p * 256 << pixel_shift),
                                                linesize,
                                                sl->non_zero_count_cache + p * 5 * 8);
            }
        } else if (sl->cbp & 15) {
            if (transform_bypass) {
                const int di = IS_8x8DCT(mb_type) ? 4 : 1;
                idct_add = IS_8x8DCT(mb_type) ? h->h264dsp.h264_add_pixels8_clear
                    : h->h264dsp.h264_add_pixels4_clear;
                for (i = 0; i < 16; i += di)
                    if (sl->non_zero_count_cache[scan8[i + p * 16]])
                        idct_add(dest_y + block_offset[i],
                                 sl->mb + (i * 16 + p * 256 << pixel_shift),
                                 linesize);
            } else {
                if (IS_8x8DCT(mb_type))
                    h->h264dsp.h264_idct8_add4(dest_y, block_offset,
                                               sl->mb + (p * 256 << pixel_shift),
                                               linesize,
                                               sl->non_zero_count_cache + p * 5 * 8);
                else
                    h->h264dsp.h264_idct_add16(dest_y, block_offset,
                                               sl->mb + (p * 256 << pixel_shift),
                                               linesize,
                                               sl->non_zero_count_cache + p * 5 * 8);
            }
        }
    }
}

#define BITS   8
#define SIMPLE 1
#include "h264_mb_template.c"

#undef  BITS
#define BITS   16
#include "h264_mb_template.c"

#undef  SIMPLE
#define SIMPLE 0
#include "h264_mb_template.c"

void ff_h264_hl_decode_mb(const H264Context *h, H264SliceContext *sl)
{
    const int mb_xy   = sl->mb_xy;
    const int mb_type = h->cur_pic.mb_type[mb_xy];
    int is_complex    = CONFIG_SMALL || sl->is_complex ||
                        IS_INTRA_PCM(mb_type) || sl->qscale == 0;

    if (CHROMA444(h)) {
        if (is_complex || h->pixel_shift)
            hl_decode_mb_444_complex(h, sl);
        else
            hl_decode_mb_444_simple_8(h, sl);
    } else if (is_complex) {
        hl_decode_mb_complex(h, sl);
    } else if (h->pixel_shift) {
        hl_decode_mb_simple_16(h, sl);
    } else
        hl_decode_mb_simple_8(h, sl);
}
