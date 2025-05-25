/*
 * FLV Encoding specific code.
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

#include "codec_internal.h"
#include "flvenc.h"
#include "mpegvideo.h"
#include "mpegvideoenc.h"
#include "put_bits.h"

int ff_flv_encode_picture_header(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    int format;

    put_bits_assume_flushed(&s->pb);

    put_bits(&s->pb, 17, 1);
    /* 0: H.263 escape codes 1: 11-bit escape codes */
    put_bits(&s->pb, 5, (s->c.h263_flv - 1));
    put_bits(&s->pb, 8,
             (((int64_t) s->c.picture_number * 30 * s->c.avctx->time_base.num) /   // FIXME use timestamp
              s->c.avctx->time_base.den) & 0xff);   /* TemporalReference */
    if (s->c.width == 352 && s->c.height == 288)
        format = 2;
    else if (s->c.width == 176 && s->c.height == 144)
        format = 3;
    else if (s->c.width == 128 && s->c.height == 96)
        format = 4;
    else if (s->c.width == 320 && s->c.height == 240)
        format = 5;
    else if (s->c.width == 160 && s->c.height == 120)
        format = 6;
    else if (s->c.width <= 255 && s->c.height <= 255)
        format = 0;   /* use 1 byte width & height */
    else
        format = 1;   /* use 2 bytes width & height */
    put_bits(&s->pb, 3, format);   /* PictureSize */
    if (format == 0) {
        put_bits(&s->pb, 8, s->c.width);
        put_bits(&s->pb, 8, s->c.height);
    } else if (format == 1) {
        put_bits(&s->pb, 16, s->c.width);
        put_bits(&s->pb, 16, s->c.height);
    }
    put_bits(&s->pb, 2, s->c.pict_type == AV_PICTURE_TYPE_P);   /* PictureType */
    put_bits(&s->pb, 1, 1);   /* DeblockingFlag: on */
    put_bits(&s->pb, 5, s->c.qscale);   /* Quantizer */
    put_bits(&s->pb, 1, 0);   /* ExtraInformation */

    return 0;
}

const FFCodec ff_flv_encoder = {
    .p.name         = "flv",
    CODEC_LONG_NAME("FLV / Sorenson Spark / Sorenson H.263 (Flash Video)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FLV1,
    .p.priv_class   = &ff_mpv_enc_class,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(MPVMainEncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P),
    .color_ranges   = AVCOL_RANGE_MPEG,
};
