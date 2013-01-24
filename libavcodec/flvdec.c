/*
 * FLV decoding.
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

#include "mpegvideo.h"
#include "h263.h"
#include "flv.h"
#include "libavutil/imgutils.h"

void ff_flv2_decode_ac_esc(GetBitContext *gb, int *level, int *run, int *last){
    int is11 = get_bits1(gb);
    *last = get_bits1(gb);
    *run = get_bits(gb, 6);
    if(is11){
        *level = get_sbits(gb, 11);
    } else {
        *level = get_sbits(gb, 7);
    }
}

int ff_flv_decode_picture_header(MpegEncContext *s)
{
    int format, width, height;

    /* picture header */
    if (get_bits_long(&s->gb, 17) != 1) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return AVERROR_INVALIDDATA;
    }
    format = get_bits(&s->gb, 5);
    if (format != 0 && format != 1) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture format\n");
        return AVERROR_INVALIDDATA;
    }
    s->h263_flv = format+1;
    s->picture_number = get_bits(&s->gb, 8); /* picture timestamp */
    format = get_bits(&s->gb, 3);
    switch (format) {
    case 0:
        width = get_bits(&s->gb, 8);
        height = get_bits(&s->gb, 8);
        break;
    case 1:
        width = get_bits(&s->gb, 16);
        height = get_bits(&s->gb, 16);
        break;
    case 2:
        width = 352;
        height = 288;
        break;
    case 3:
        width = 176;
        height = 144;
        break;
    case 4:
        width = 128;
        height = 96;
        break;
    case 5:
        width = 320;
        height = 240;
        break;
    case 6:
        width = 160;
        height = 120;
        break;
    default:
        width = height = 0;
        break;
    }
    if(av_image_check_size(width, height, 0, s->avctx))
        return AVERROR(EINVAL);
    s->width = width;
    s->height = height;

    s->pict_type = AV_PICTURE_TYPE_I + get_bits(&s->gb, 2);
    s->droppable = s->pict_type > AV_PICTURE_TYPE_P;
    if (s->droppable)
        s->pict_type = AV_PICTURE_TYPE_P;

    skip_bits1(&s->gb); /* deblocking flag */
    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);

    s->h263_plus = 0;

    s->unrestricted_mv = 1;
    s->h263_long_vectors = 0;

    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;

    if(s->avctx->debug & FF_DEBUG_PICT_INFO){
        av_log(s->avctx, AV_LOG_DEBUG, "%c esc_type:%d, qp:%d num:%d\n",
               s->droppable ? 'D' : av_get_picture_type_char(s->pict_type),
               s->h263_flv - 1, s->qscale, s->picture_number);
    }

    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;

    return 0;
}

AVCodec ff_flv_decoder = {
    .name           = "flv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FLV1,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_h263_decode_init,
    .close          = ff_h263_decode_end,
    .decode         = ff_h263_decode_frame,
    .capabilities   = CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
    .max_lowres     = 3,
    .long_name      = NULL_IF_CONFIG_SMALL("FLV / Sorenson Spark / Sorenson H.263 (Flash Video)"),
    .pix_fmts       = ff_pixfmt_list_420,
};
