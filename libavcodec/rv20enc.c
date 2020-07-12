/*
 * RV20 encoder
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
 * RV20 encoder
 */

#include "mpegvideo.h"
#include "mpegvideodata.h"
#include "h263.h"
#include "h263data.h"
#include "put_bits.h"
#include "rv10.h"

void ff_rv20_encode_picture_header(MpegEncContext *s, int picture_number){
    put_bits(&s->pb, 2, s->pict_type); //I 0 vs. 1 ?
    put_bits(&s->pb, 1, 0);     /* unknown bit */
    put_bits(&s->pb, 5, s->qscale);

    put_sbits(&s->pb, 8, picture_number); //FIXME wrong, but correct is not known
    s->mb_x= s->mb_y= 0;
    ff_h263_encode_mba(s);

    put_bits(&s->pb, 1, s->no_rounding);

    av_assert0(s->f_code == 1);
    av_assert0(s->unrestricted_mv == 0);
    av_assert0(s->alt_inter_vlc == 0);
    av_assert0(s->umvplus == 0);
    av_assert0(s->modified_quant==1);
    av_assert0(s->loop_filter==1);

    s->h263_aic= s->pict_type == AV_PICTURE_TYPE_I;
    if(s->h263_aic){
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_aic_dc_scale_table;
    }else{
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }
}

static const AVClass rv20_class = {
    .class_name = "rv20 encoder",
    .item_name  = av_default_item_name,
    .option     = ff_mpv_generic_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_rv20_encoder = {
    .name           = "rv20",
    .long_name      = NULL_IF_CONFIG_SMALL("RealVideo 2.0"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RV20,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .priv_class     = &rv20_class,
};
