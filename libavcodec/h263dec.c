/*
 * H263 decoder
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"

//#define DEBUG

static int h263_decode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i;

    s->avctx = avctx;
    s->out_format = FMT_H263;

    s->width = avctx->width;
    s->height = avctx->height;

    /* select sub codec */
    switch(avctx->codec->id) {
    case CODEC_ID_H263:
        break;
    case CODEC_ID_MPEG4:
        s->time_increment_bits = 4; /* default value for broken headers */
        s->h263_pred = 1;
        break;
    case CODEC_ID_MSMPEG4:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        break;
    case CODEC_ID_H263I:
        s->h263_intel = 1;
        break;
    default:
        return -1;
    }

    /* for h263, we allocate the images after having read the header */
    if (MPV_common_init(s) < 0)
        return -1;

    /* XXX: suppress this matrix init, only needed because using mpeg1
       dequantize in mmx case */
    for(i=0;i<64;i++)
        s->non_intra_matrix[i] = default_non_intra_matrix[i];

    if (s->h263_msmpeg4)
        msmpeg4_decode_init_vlc(s);
    else
        h263_decode_init_vlc(s);
    
    return 0;
}

static int h263_decode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    MPV_common_end(s);
    return 0;
}

static int h263_decode_frame(AVCodecContext *avctx, 
                             void *data, int *data_size,
                             UINT8 *buf, int buf_size)
{
    MpegEncContext *s = avctx->priv_data;
    int ret;
    AVPicture *pict = data; 

#ifdef DEBUG
    printf("*****frame %d size=%d\n", avctx->frame_number, buf_size);
    printf("bytes=%x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
#endif

    /* no supplementary picture */
    if (buf_size == 0) {
        *data_size = 0;
        return 0;
    }

    init_get_bits(&s->gb, buf, buf_size);

    /* let's go :-) */
    if (s->h263_msmpeg4) {
        ret = msmpeg4_decode_picture_header(s);
    } else if (s->h263_pred) {
        ret = mpeg4_decode_picture_header(s);
    } else if (s->h263_intel) {
        ret = intel_h263_decode_picture_header(s);
    } else {
        ret = h263_decode_picture_header(s);
    }
    if (ret < 0)
        return -1;

    MPV_frame_start(s);

#ifdef DEBUG
    printf("qscale=%d\n", s->qscale);
#endif

    /* decode each macroblock */
    for(s->mb_y=0; s->mb_y < s->mb_height; s->mb_y++) {
        for(s->mb_x=0; s->mb_x < s->mb_width; s->mb_x++) {
#ifdef DEBUG
            printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
#endif
            /* DCT & quantize */
            if (s->h263_msmpeg4) {
                msmpeg4_dc_scale(s);
            } else if (s->h263_pred) {
                h263_dc_scale(s);
            } else {
                /* default quantization values */
                s->y_dc_scale = 8;
                s->c_dc_scale = 8;
            }

            memset(s->block, 0, sizeof(s->block));
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16; 
            if (s->h263_msmpeg4) {
                if (msmpeg4_decode_mb(s, s->block) < 0)
                    return -1;
            } else {
                if (h263_decode_mb(s, s->block) < 0)
                    return -1;
            }
            MPV_decode_mb(s, s->block);
        }
        if (avctx->draw_horiz_band) {
            UINT8 *src_ptr[3];
            int y, h, offset;
            y = s->mb_y * 16;
            h = s->height - y;
            if (h > 16)
                h = 16;
            offset = y * s->linesize;
            src_ptr[0] = s->current_picture[0] + offset;
            src_ptr[1] = s->current_picture[1] + (offset >> 2);
            src_ptr[2] = s->current_picture[2] + (offset >> 2);
            avctx->draw_horiz_band(avctx, src_ptr, s->linesize,
                                   y, s->width, h);
        }
    }

    MPV_frame_end(s);
    
    pict->data[0] = s->current_picture[0];
    pict->data[1] = s->current_picture[1];
    pict->data[2] = s->current_picture[2];
    pict->linesize[0] = s->linesize;
    pict->linesize[1] = s->linesize / 2;
    pict->linesize[2] = s->linesize / 2;

    avctx->quality = s->qscale;
    *data_size = sizeof(AVPicture);
    return buf_size;
}

AVCodec mpeg4_decoder = {
    "mpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG4,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND,
};

AVCodec h263_decoder = {
    "h263",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND,
};

AVCodec msmpeg4_decoder = {
    "msmpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND,
};

AVCodec h263i_decoder = {
    "h263i",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263I,
    sizeof(MpegEncContext),
    h263_decode_init,
    NULL,
    h263_decode_end,
    h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND,
};

