/*
 * Electronic Arts CMV Video Decoder
 * Copyright (c) 2007-2008 Peter Ross
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file
 * Electronic Arts CMV Video Decoder
 * by Peter Ross (pross@xvid.org)
 *
 * Technical details here:
 * http://wiki.multimedia.cx/index.php?title=Electronic_Arts_CMV
 */

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct CmvContext {
    AVCodecContext *avctx;
    AVFrame *last_frame;   ///< last
    AVFrame *last2_frame;  ///< second-last
    int width, height;
    unsigned int palette[AVPALETTE_COUNT];
} CmvContext;

static av_cold int cmv_decode_init(AVCodecContext *avctx){
    CmvContext *s = avctx->priv_data;

    s->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    s->last_frame  = av_frame_alloc();
    s->last2_frame = av_frame_alloc();
    if (!s->last_frame || !s->last2_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void cmv_decode_intra(CmvContext * s, AVFrame *frame,
                             const uint8_t *buf, const uint8_t *buf_end)
{
    unsigned char *dst = frame->data[0];
    int i;

    for (i=0; i < s->avctx->height && buf_end - buf >= s->avctx->width; i++) {
        memcpy(dst, buf, s->avctx->width);
        dst += frame->linesize[0];
        buf += s->avctx->width;
    }
}

static void cmv_motcomp(unsigned char *dst, ptrdiff_t dst_stride,
                        const unsigned char *src, ptrdiff_t src_stride,
                        int x, int y,
                        int xoffset, int yoffset,
                        int width, int height){
    int i,j;

    for(j=y;j<y+4;j++)
    for(i=x;i<x+4;i++)
    {
        if (i+xoffset>=0 && i+xoffset<width &&
            j+yoffset>=0 && j+yoffset<height) {
            dst[j*dst_stride + i] = src[(j+yoffset)*src_stride + i+xoffset];
        }else{
            dst[j*dst_stride + i] = 0;
        }
    }
}

static void cmv_decode_inter(CmvContext *s, AVFrame *frame, const uint8_t *buf,
                             const uint8_t *buf_end)
{
    const uint8_t *raw = buf + (s->avctx->width*s->avctx->height/16);
    int x,y,i;

    i = 0;
    for(y=0; y<s->avctx->height/4; y++)
    for(x=0; x<s->avctx->width/4 && buf_end - buf > i; x++) {
        if (buf[i]==0xFF) {
            unsigned char *dst = frame->data[0] + (y*4)*frame->linesize[0] + x*4;
            if (raw+16<buf_end && *raw==0xFF) { /* intra */
                raw++;
                memcpy(dst, raw, 4);
                memcpy(dst +     frame->linesize[0], raw+4, 4);
                memcpy(dst + 2 * frame->linesize[0], raw+8, 4);
                memcpy(dst + 3 * frame->linesize[0], raw+12, 4);
                raw+=16;
            }else if(raw<buf_end) {  /* inter using second-last frame as reference */
                int xoffset = (*raw & 0xF) - 7;
                int yoffset = ((*raw >> 4)) - 7;
                if (s->last2_frame->data[0])
                    cmv_motcomp(frame->data[0], frame->linesize[0],
                                s->last2_frame->data[0], s->last2_frame->linesize[0],
                                x*4, y*4, xoffset, yoffset, s->avctx->width, s->avctx->height);
                raw++;
            }
        }else{  /* inter using last frame as reference */
            int xoffset = (buf[i] & 0xF) - 7;
            int yoffset = ((buf[i] >> 4)) - 7;
            if (s->last_frame->data[0])
                cmv_motcomp(frame->data[0], frame->linesize[0],
                            s->last_frame->data[0], s->last_frame->linesize[0],
                            x*4, y*4, xoffset, yoffset, s->avctx->width, s->avctx->height);
        }
        i++;
    }
}

static int cmv_process_header(CmvContext *s, const uint8_t *buf, const uint8_t *buf_end)
{
    int pal_start, pal_count, i, ret, fps;

    if(buf_end - buf < 16) {
        av_log(s->avctx, AV_LOG_WARNING, "truncated header\n");
        return AVERROR_INVALIDDATA;
    }

    s->width  = AV_RL16(&buf[4]);
    s->height = AV_RL16(&buf[6]);

    if (s->width  != s->avctx->width ||
        s->height != s->avctx->height) {
        av_frame_unref(s->last_frame);
        av_frame_unref(s->last2_frame);
    }

    ret = ff_set_dimensions(s->avctx, s->width, s->height);
    if (ret < 0)
        return ret;

    fps = AV_RL16(&buf[10]);
    if (fps > 0)
        s->avctx->framerate = (AVRational){ fps, 1 };

    pal_start = AV_RL16(&buf[12]);
    pal_count = AV_RL16(&buf[14]);

    buf += 16;
    for (i=pal_start; i<pal_start+pal_count && i<AVPALETTE_COUNT && buf_end - buf >= 3; i++) {
        s->palette[i] = 0xFFU << 24 | AV_RB24(buf);
        buf += 3;
    }

    return 0;
}

#define EA_PREAMBLE_SIZE 8
#define MVIh_TAG MKTAG('M', 'V', 'I', 'h')

static int cmv_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CmvContext *s = avctx->priv_data;
    const uint8_t *buf_end = buf + buf_size;
    int ret;

    if (buf_end - buf < EA_PREAMBLE_SIZE)
        return AVERROR_INVALIDDATA;

    if (AV_RL32(buf)==MVIh_TAG||AV_RB32(buf)==MVIh_TAG) {
        unsigned size = AV_RL32(buf + 4);
        ret = cmv_process_header(s, buf+EA_PREAMBLE_SIZE, buf_end);
        if (ret < 0)
            return ret;
        if (size > buf_end - buf - EA_PREAMBLE_SIZE)
            return AVERROR_INVALIDDATA;
        buf += size;
    }

    if ((ret = av_image_check_size(s->width, s->height, 0, s->avctx)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    memcpy(frame->data[1], s->palette, AVPALETTE_SIZE);

    buf += EA_PREAMBLE_SIZE;
    if ((buf[0]&1)) {  // subtype
        cmv_decode_inter(s, frame, buf+2, buf_end);
        frame->key_frame = 0;
        frame->pict_type = AV_PICTURE_TYPE_P;
    }else{
        frame->key_frame = 1;
        frame->pict_type = AV_PICTURE_TYPE_I;
        cmv_decode_intra(s, frame, buf+2, buf_end);
    }

    av_frame_unref(s->last2_frame);
    av_frame_move_ref(s->last2_frame, s->last_frame);
    if ((ret = av_frame_ref(s->last_frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    return buf_size;
}

static av_cold int cmv_decode_end(AVCodecContext *avctx){
    CmvContext *s = avctx->priv_data;

    av_frame_free(&s->last_frame);
    av_frame_free(&s->last2_frame);

    return 0;
}

const FFCodec ff_eacmv_decoder = {
    .p.name         = "eacmv",
    CODEC_LONG_NAME("Electronic Arts CMV video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_CMV,
    .priv_data_size = sizeof(CmvContext),
    .init           = cmv_decode_init,
    .close          = cmv_decode_end,
    FF_CODEC_DECODE_CB(cmv_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
