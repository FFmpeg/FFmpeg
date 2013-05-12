/*
 * FLV Encoding specific code.
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

void ff_flv_encode_picture_header(MpegEncContext * s, int picture_number)
{
      int format;

      avpriv_align_put_bits(&s->pb);

      put_bits(&s->pb, 17, 1);
      put_bits(&s->pb, 5, (s->h263_flv-1)); /* 0: h263 escape codes 1: 11-bit escape codes */
      put_bits(&s->pb, 8, (((int64_t)s->picture_number * 30 * s->avctx->time_base.num) / //FIXME use timestamp
                           s->avctx->time_base.den) & 0xff); /* TemporalReference */
      if (s->width == 352 && s->height == 288)
        format = 2;
      else if (s->width == 176 && s->height == 144)
        format = 3;
      else if (s->width == 128 && s->height == 96)
        format = 4;
      else if (s->width == 320 && s->height == 240)
        format = 5;
      else if (s->width == 160 && s->height == 120)
        format = 6;
      else if (s->width <= 255 && s->height <= 255)
        format = 0; /* use 1 byte width & height */
      else
        format = 1; /* use 2 bytes width & height */
      put_bits(&s->pb, 3, format); /* PictureSize */
      if (format == 0) {
        put_bits(&s->pb, 8, s->width);
        put_bits(&s->pb, 8, s->height);
      } else if (format == 1) {
        put_bits(&s->pb, 16, s->width);
        put_bits(&s->pb, 16, s->height);
      }
      put_bits(&s->pb, 2, s->pict_type == AV_PICTURE_TYPE_P); /* PictureType */
      put_bits(&s->pb, 1, 1); /* DeblockingFlag: on */
      put_bits(&s->pb, 5, s->qscale); /* Quantizer */
      put_bits(&s->pb, 1, 0); /* ExtraInformation */

      if(s->h263_aic){
        s->y_dc_scale_table=
          s->c_dc_scale_table= ff_aic_dc_scale_table;
      }else{
        s->y_dc_scale_table=
          s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
      }
}

void ff_flv2_encode_ac_esc(PutBitContext *pb, int slevel, int level, int run, int last){
    if(level < 64) { // 7-bit level
        put_bits(pb, 1, 0);
        put_bits(pb, 1, last);
        put_bits(pb, 6, run);

        put_sbits(pb, 7, slevel);
    } else {
        /* 11-bit level */
        put_bits(pb, 1, 1);
        put_bits(pb, 1, last);
        put_bits(pb, 6, run);

        put_sbits(pb, 11, slevel);
    }
}

FF_MPV_GENERIC_CLASS(flv)

AVCodec ff_flv_encoder = {
    .name           = "flv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FLV1,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_MPV_encode_init,
    .encode2        = ff_MPV_encode_picture,
    .close          = ff_MPV_encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("FLV / Sorenson Spark / Sorenson H.263 (Flash Video)"),
    .priv_class     = &flv_class,
};
