/*
 * Interplay MVE Video Decoder
 * Copyright (C) 2003 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/**
 * @file roqvideo.c
 * Interplay MVE Video Decoder by Mike Melanson
 * For more information about the Interplay MVE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

typedef struct IpvideoContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame last_frame;
    AVFrame current_frame;
    int first_frame;
    int receiving_decoding_map;
    unsigned char *decoding_map;
    int decoding_map_size;

    unsigned char *buf;
    int size;

} IpvideoContext;

static int ipvideo_decode_init(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_YUV444P;
    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    s->first_frame = 1;
    s->receiving_decoding_map = 1;  /* decoding map will be received first */

    /* decoding map contains 4 bits of information per 8x8 block */
    s->decoding_map_size = avctx->width * avctx->height / (8 * 8 * 2);
    s->decoding_map = av_malloc(s->decoding_map_size);

    return 0;
}

static int ipvideo_decode_frame(AVCodecContext *avctx,
                                void *data, int *data_size,
                                uint8_t *buf, int buf_size)
{
    IpvideoContext *s = avctx->priv_data;

    if (s->receiving_decoding_map) {
        /* receiving the decoding map on this iteration */
        s->receiving_decoding_map = 0;

        if (buf_size != s->decoding_map_size)
            printf (" Interplay video: buf_size != decoding_map_size (%d != %d)\n",
                buf_size, s->decoding_map_size);
        else
            memcpy(s->decoding_map, buf, buf_size);

        *data_size = 0;
        *(AVFrame*)data = s->last_frame;
    } else {
        /* receiving the compressed video data on this iteration */
        s->receiving_decoding_map = 1;
        s->buf = buf;
        s->size = buf_size;

        if (avctx->get_buffer(avctx, &s->current_frame)) {
            printf ("  Interplay Video: get_buffer() failed\n");
            return -1;
        }

//        ipvideo_decode_frame(s);
memset(s->current_frame.data[0], 0x80, s->current_frame.linesize[0] * avctx->height);
memset(s->current_frame.data[1], 0x80, s->current_frame.linesize[1] * avctx->height / 4);
memset(s->current_frame.data[2], 0x80, s->current_frame.linesize[2] * avctx->height / 4);

        /* release the last frame if it is allocated */
        if (s->first_frame)
            s->first_frame = 0;
        else
            avctx->release_buffer(avctx, &s->last_frame);

        /* shuffle frames */
        s->last_frame = s->current_frame;

        *data_size = sizeof(AVFrame);
        *(AVFrame*)data = s->current_frame;
    }

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static int ipvideo_decode_end(AVCodecContext *avctx)
{
    IpvideoContext *s = avctx->priv_data;

    /* release the last frame */
    avctx->release_buffer(avctx, &s->last_frame);

    av_free(s->decoding_map);

    return 0;
}

AVCodec interplay_video_decoder = {
    "interplayvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_INTERPLAY_VIDEO,
    sizeof(IpvideoContext),
    ipvideo_decode_init,
    NULL,
    ipvideo_decode_end,
    ipvideo_decode_frame,
    CODEC_CAP_DR1,
};
