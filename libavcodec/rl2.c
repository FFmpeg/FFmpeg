/*
 * RL2 Video Decoder
 * Copyright (C) 2008 Sascha Sommer (saschasommer@freenet.de)
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
 * RL2 Video Decoder
 * @author Sascha Sommer (saschasommer@freenet.de)
 * @see http://wiki.multimedia.cx/index.php?title=RL2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"


#define EXTRADATA1_SIZE (6 + 256 * 3) ///< video base, clr count, palette

typedef struct Rl2Context {
    AVCodecContext *avctx;
    AVFrame frame;

    unsigned short video_base; ///< initial drawing offset
    unsigned int clr_count;    ///< number of used colors (currently unused)
    unsigned char* back_frame; ///< background frame
    unsigned int palette[AVPALETTE_COUNT];
} Rl2Context;

/**
 * Run Length Decode a single 320x200 frame
 * @param s rl2 context
 * @param in input buffer
 * @param size input buffer size
 * @param out ouput buffer
 * @param stride stride of the output buffer
 * @param video_base offset of the rle data inside the frame
 */
static void rl2_rle_decode(Rl2Context *s,const unsigned char* in,int size,
                               unsigned char* out,int stride,int video_base){
    int base_x = video_base % s->avctx->width;
    int base_y = video_base / s->avctx->width;
    int stride_adj = stride - s->avctx->width;
    int i;
    const unsigned char* back_frame = s->back_frame;
    const unsigned char* in_end = in + size;
    const unsigned char* out_end = out + stride * s->avctx->height;
    unsigned char* line_end = out + s->avctx->width;

    /** copy start of the background frame */
    for(i=0;i<=base_y;i++){
        if(s->back_frame)
            memcpy(out,back_frame,s->avctx->width);
        out += stride;
        back_frame += s->avctx->width;
    }
    back_frame += base_x - s->avctx->width;
    line_end = out - stride_adj;
    out += base_x - stride;

    /** decode the variable part of the frame */
    while(in < in_end){
        unsigned char val = *in++;
        int len = 1;
        if(val >= 0x80){
            if(in >= in_end)
                break;
            len = *in++;
            if(!len)
                break;
        }

        if(len >= out_end - out)
            break;

        if(s->back_frame)
            val |= 0x80;
        else
            val &= ~0x80;

        while(len--){
            *out++ = (val == 0x80)? *back_frame:val;
            back_frame++;
            if(out == line_end){
                 out += stride_adj;
                 line_end += stride;
                 if(len >= out_end - out)
                     break;
            }
        }
    }

    /** copy the rest from the background frame */
    if(s->back_frame){
        while(out < out_end){
            memcpy(out, back_frame, line_end - out);
            back_frame += line_end - out;
            out = line_end + stride_adj;
            line_end += stride;
        }
    }
}


/**
 * Initialize the decoder
 * @param avctx decoder context
 * @return 0 success, -1 on error
 */
static av_cold int rl2_decode_init(AVCodecContext *avctx)
{
    Rl2Context *s = avctx->priv_data;
    int back_size;
    int i;
    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;
    avcodec_get_frame_defaults(&s->frame);

    /** parse extra data */
    if(!avctx->extradata || avctx->extradata_size < EXTRADATA1_SIZE){
        av_log(avctx, AV_LOG_ERROR, "invalid extradata size\n");
        return -1;
    }

    /** get frame_offset */
    s->video_base = AV_RL16(&avctx->extradata[0]);
    s->clr_count = AV_RL32(&avctx->extradata[2]);

    if(s->video_base >= avctx->width * avctx->height){
        av_log(avctx, AV_LOG_ERROR, "invalid video_base\n");
        return -1;
    }

    /** initialize palette */
    for(i=0;i<AVPALETTE_COUNT;i++)
        s->palette[i] = 0xFF << 24 | AV_RB24(&avctx->extradata[6 + i * 3]);

    /** decode background frame if present */
    back_size = avctx->extradata_size - EXTRADATA1_SIZE;

    if(back_size > 0){
        unsigned char* back_frame = av_mallocz(avctx->width*avctx->height);
        if(!back_frame)
            return -1;
        rl2_rle_decode(s,avctx->extradata + EXTRADATA1_SIZE,back_size,
                           back_frame,avctx->width,0);
        s->back_frame = back_frame;
    }
    return 0;
}


static int rl2_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    Rl2Context *s = avctx->priv_data;

    if(s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    /** get buffer */
    s->frame.reference= 0;
    if(avctx->get_buffer(avctx, &s->frame)) {
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    /** run length decode */
    rl2_rle_decode(s,buf,buf_size,s->frame.data[0],s->frame.linesize[0],s->video_base);

    /** make the palette available on the way out */
    memcpy(s->frame.data[1], s->palette, AVPALETTE_SIZE);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /** report that the buffer was completely consumed */
    return buf_size;
}


/**
 * Uninit decoder
 * @param avctx decoder context
 * @return 0 success, -1 on error
 */
static av_cold int rl2_decode_end(AVCodecContext *avctx)
{
    Rl2Context *s = avctx->priv_data;

    if(s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    av_free(s->back_frame);

    return 0;
}


AVCodec ff_rl2_decoder = {
    .name           = "rl2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_RL2,
    .priv_data_size = sizeof(Rl2Context),
    .init           = rl2_decode_init,
    .close          = rl2_decode_end,
    .decode         = rl2_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("RL2 video"),
};

