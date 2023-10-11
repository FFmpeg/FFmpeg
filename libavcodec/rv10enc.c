/*
 * RV10 encoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
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
 * RV10 encoder
 */

#include "codec_internal.h"
#include "mpegvideo.h"
#include "mpegvideoenc.h"
#include "put_bits.h"
#include "rv10enc.h"

int ff_rv10_encode_picture_header(MpegEncContext *s)
{
    int full_frame= 0;

    align_put_bits(&s->pb);

    put_bits(&s->pb, 1, 1);     /* marker */

    put_bits(&s->pb, 1, (s->pict_type == AV_PICTURE_TYPE_P));

    put_bits(&s->pb, 1, 0);     /* not PB-mframe */

    put_bits(&s->pb, 5, s->qscale);

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        /* specific MPEG like DC coding not used */
    }
    /* if multiple packets per frame are sent, the position at which
       to display the macroblocks is coded here */
    if(!full_frame){
        if (s->mb_width * s->mb_height >= (1U << 12)) {
            avpriv_report_missing_feature(s->avctx, "Encoding frames with %d (>= 4096) macroblocks",
                                          s->mb_width * s->mb_height);
            return AVERROR(ENOSYS);
        }
        put_bits(&s->pb, 6, 0); /* mb_x */
        put_bits(&s->pb, 6, 0); /* mb_y */
        put_bits(&s->pb, 12, s->mb_width * s->mb_height);
    }

    put_bits(&s->pb, 3, 0);     /* ignored */
    return 0;
}

const FFCodec ff_rv10_encoder = {
    .p.name         = "rv10",
    CODEC_LONG_NAME("RealVideo 1.0"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RV10,
    .p.priv_class   = &ff_mpv_enc_class,
    .p.capabilities = AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .color_ranges   = AVCOL_RANGE_MPEG,
};
