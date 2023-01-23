/*
 * Mpeg video formats-related defines and utility functions
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

#include <stdint.h>

#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/pixdesc.h"
#include "libavutil/motion_vector.h"
#include "libavutil/avassert.h"

#include "avcodec.h"
#include "mpegutils.h"

static int add_mb(AVMotionVector *mb, uint32_t mb_type,
                  int dst_x, int dst_y,
                  int motion_x, int motion_y, int motion_scale,
                  int direction)
{
    mb->w = IS_8X8(mb_type) || IS_8X16(mb_type) ? 8 : 16;
    mb->h = IS_8X8(mb_type) || IS_16X8(mb_type) ? 8 : 16;
    mb->motion_x = motion_x;
    mb->motion_y = motion_y;
    mb->motion_scale = motion_scale;
    mb->dst_x = dst_x;
    mb->dst_y = dst_y;
    mb->src_x = dst_x + motion_x / motion_scale;
    mb->src_y = dst_y + motion_y / motion_scale;
    mb->source = direction ? 1 : -1;
    mb->flags = 0; // XXX: does mb_type contain extra information that could be exported here?
    return 1;
}

void ff_draw_horiz_band(AVCodecContext *avctx,
                        const AVFrame *cur, const AVFrame *last,
                        int y, int h, int picture_structure,
                        int first_field, int low_delay)
{
    const int field_pic = picture_structure != PICT_FRAME;
    const AVFrame *src;
    int offset[AV_NUM_DATA_POINTERS];

    if (!avctx->draw_horiz_band)
        return;

    if (field_pic) {
        h <<= 1;
        y <<= 1;
    }

    h = FFMIN(h, avctx->height - y);

    if (field_pic && first_field &&
        !(avctx->slice_flags & SLICE_FLAG_ALLOW_FIELD))
        return;

    if (cur->pict_type == AV_PICTURE_TYPE_B || low_delay ||
        (avctx->slice_flags & SLICE_FLAG_CODED_ORDER))
        src = cur;
    else if (last)
        src = last;
    else
        return;

    if (cur->pict_type == AV_PICTURE_TYPE_B &&
        picture_structure == PICT_FRAME &&
        avctx->codec_id != AV_CODEC_ID_SVQ3) {
        for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
            offset[i] = 0;
    } else {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
        int vshift = desc->log2_chroma_h;

        offset[0] = y * src->linesize[0];
        offset[1] =
        offset[2] = (y >> vshift) * src->linesize[1];
        for (int i = 3; i < AV_NUM_DATA_POINTERS; i++)
            offset[i] = 0;
    }

    emms_c();

    avctx->draw_horiz_band(avctx, src, offset,
                            y, picture_structure, h);
}

static char get_type_mv_char(int mb_type)
{
    // Type & MV direction
    if (IS_PCM(mb_type))
        return 'P';
    else if (IS_INTRA(mb_type) && IS_ACPRED(mb_type))
        return 'A';
    else if (IS_INTRA4x4(mb_type))
        return 'i';
    else if (IS_INTRA16x16(mb_type))
        return 'I';
    else if (IS_DIRECT(mb_type) && IS_SKIP(mb_type))
        return 'd';
    else if (IS_DIRECT(mb_type))
        return 'D';
    else if (IS_GMC(mb_type) && IS_SKIP(mb_type))
        return 'g';
    else if (IS_GMC(mb_type))
        return 'G';
    else if (IS_SKIP(mb_type))
        return 'S';
    else if (!USES_LIST(mb_type, 1))
        return '>';
    else if (!USES_LIST(mb_type, 0))
        return '<';
    else {
        av_assert2(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
        return 'X';
    }
}

static char get_segmentation_char(int mb_type)
{
    if (IS_8X8(mb_type))
        return '+';
    else if (IS_16X8(mb_type))
        return '-';
    else if (IS_8X16(mb_type))
        return '|';
    else if (IS_INTRA(mb_type) || IS_16X16(mb_type))
        return ' ';

    return '?';
}

static char get_interlacement_char(int mb_type)
{
    if (IS_INTERLACED(mb_type))
        return '=';
    else
        return ' ';
}

void ff_print_debug_info2(AVCodecContext *avctx, AVFrame *pict,
                          const uint8_t *mbskip_table, const uint32_t *mbtype_table,
                          const int8_t *qscale_table, int16_t (*const motion_val[2])[2],
                          int mb_width, int mb_height, int mb_stride, int quarter_sample)
{
    if ((avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS) && mbtype_table && motion_val[0]) {
        const int shift = 1 + quarter_sample;
        const int scale = 1 << shift;
        const int mv_sample_log2 = avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_SVQ3 ? 2 : 1;
        const int mv_stride      = (mb_width << mv_sample_log2) +
                                   (avctx->codec->id == AV_CODEC_ID_H264 ? 0 : 1);
        int mb_x, mb_y, mbcount = 0;

        /* size is width * height * 2 * 4 where 2 is for directions and 4 is
         * for the maximum number of MB (4 MB in case of IS_8x8) */
        AVMotionVector *mvs = av_malloc_array(mb_width * mb_height, 2 * 4 * sizeof(AVMotionVector));
        if (!mvs)
            return;

        for (mb_y = 0; mb_y < mb_height; mb_y++) {
            for (mb_x = 0; mb_x < mb_width; mb_x++) {
                int i, direction, mb_type = mbtype_table[mb_x + mb_y * mb_stride];
                for (direction = 0; direction < 2; direction++) {
                    if (!USES_LIST(mb_type, direction))
                        continue;
                    if (IS_8X8(mb_type)) {
                        for (i = 0; i < 4; i++) {
                            int sx = mb_x * 16 + 4 + 8 * (i & 1);
                            int sy = mb_y * 16 + 4 + 8 * (i >> 1);
                            int xy = (mb_x * 2 + (i & 1) +
                                      (mb_y * 2 + (i >> 1)) * mv_stride) << (mv_sample_log2 - 1);
                            int mx = motion_val[direction][xy][0];
                            int my = motion_val[direction][xy][1];
                            mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                        }
                    } else if (IS_16X8(mb_type)) {
                        for (i = 0; i < 2; i++) {
                            int sx = mb_x * 16 + 8;
                            int sy = mb_y * 16 + 4 + 8 * i;
                            int xy = (mb_x * 2 + (mb_y * 2 + i) * mv_stride) << (mv_sample_log2 - 1);
                            int mx = motion_val[direction][xy][0];
                            int my = motion_val[direction][xy][1];

                            if (IS_INTERLACED(mb_type))
                                my *= 2;

                            mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                        }
                    } else if (IS_8X16(mb_type)) {
                        for (i = 0; i < 2; i++) {
                            int sx = mb_x * 16 + 4 + 8 * i;
                            int sy = mb_y * 16 + 8;
                            int xy = (mb_x * 2 + i + mb_y * 2 * mv_stride) << (mv_sample_log2 - 1);
                            int mx = motion_val[direction][xy][0];
                            int my = motion_val[direction][xy][1];

                            if (IS_INTERLACED(mb_type))
                                my *= 2;

                            mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                        }
                    } else {
                          int sx = mb_x * 16 + 8;
                          int sy = mb_y * 16 + 8;
                          int xy = (mb_x + mb_y * mv_stride) << mv_sample_log2;
                          int mx = motion_val[direction][xy][0];
                          int my = motion_val[direction][xy][1];
                          mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                    }
                }
            }
        }

        if (mbcount) {
            AVFrameSideData *sd;

            av_log(avctx, AV_LOG_DEBUG, "Adding %d MVs info to frame %"PRId64"\n", mbcount, avctx->frame_num);
            sd = av_frame_new_side_data(pict, AV_FRAME_DATA_MOTION_VECTORS, mbcount * sizeof(AVMotionVector));
            if (!sd) {
                av_freep(&mvs);
                return;
            }
            memcpy(sd->data, mvs, mbcount * sizeof(AVMotionVector));
        }

        av_freep(&mvs);
    }

    /* TODO: export all the following to make them accessible for users (and filters) */
    if (avctx->hwaccel || !mbtype_table)
        return;


    if (avctx->debug & (FF_DEBUG_SKIP | FF_DEBUG_QP | FF_DEBUG_MB_TYPE)) {
        int x,y;

        av_log(avctx, AV_LOG_DEBUG, "New frame, type: %c\n",
               av_get_picture_type_char(pict->pict_type));
        for (y = 0; y < mb_height; y++) {
            for (x = 0; x < mb_width; x++) {
                if (avctx->debug & FF_DEBUG_SKIP) {
                    int count = mbskip_table ? mbskip_table[x + y * mb_stride] : 0;
                    if (count > 9)
                        count = 9;
                    av_log(avctx, AV_LOG_DEBUG, "%1d", count);
                }
                if (avctx->debug & FF_DEBUG_QP) {
                    av_log(avctx, AV_LOG_DEBUG, "%2d",
                           qscale_table[x + y * mb_stride]);
                }
                if (avctx->debug & FF_DEBUG_MB_TYPE) {
                    int mb_type = mbtype_table[x + y * mb_stride];

                    av_log(avctx, AV_LOG_DEBUG, "%c%c%c",
                           get_type_mv_char(mb_type),
                           get_segmentation_char(mb_type),
                           get_interlacement_char(mb_type));
                }
            }
            av_log(avctx, AV_LOG_DEBUG, "\n");
        }
    }
}
