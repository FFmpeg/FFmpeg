/*
 * Copyright (c) 2011 Derek Buitenhuis
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Known FOURCCs:
 *     'ULY0' (YCbCr 4:2:0), 'ULY2' (YCbCr 4:2:2), 'ULRG' (RGB), 'ULRA' (RGBA)
 */

extern "C" {
#include "avcodec.h"
}

#include "libutvideo.h"
#include "get_bits.h"

static av_cold int utvideo_decode_init(AVCodecContext *avctx)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;
    UtVideoExtra info;
    int format;
    int begin_ret;

    if (avctx->extradata_size != 4*4) {
        av_log(avctx, AV_LOG_ERROR, "Extradata size mismatch.\n");
        return -1;
    }

    /* Read extradata */
    info.version = AV_RL32(avctx->extradata);
    info.original_format = AV_RL32(avctx->extradata + 4);
    info.frameinfo_size = AV_RL32(avctx->extradata + 8);
    info.flags = AV_RL32(avctx->extradata + 12);

    /* Pick format based on FOURCC */
    switch (avctx->codec_tag) {
    case MKTAG('U', 'L', 'Y', '0'):
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        format = UTVF_YV12;
        break;
    case MKTAG('U', 'L', 'Y', '2'):
        avctx->pix_fmt = AV_PIX_FMT_YUYV422;
        format = UTVF_YUY2;
        break;
    case MKTAG('U', 'L', 'R', 'G'):
        avctx->pix_fmt = AV_PIX_FMT_BGR24;
        format = UTVF_RGB24_WIN;
        break;
    case MKTAG('U', 'L', 'R', 'A'):
        avctx->pix_fmt = AV_PIX_FMT_RGB32;
        format = UTVF_RGB32_WIN;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
              "Not a Ut Video FOURCC: %X\n", avctx->codec_tag);
        return -1;
    }

    /* Only allocate the buffer once */
    utv->buf_size = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
    utv->buffer = (uint8_t *)av_malloc(utv->buf_size * sizeof(uint8_t));

    if (utv->buffer == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Unable to allocate output buffer.\n");
        return -1;
    }

    /* Allocate the output frame */
    avctx->coded_frame = avcodec_alloc_frame();

    /* Ut Video only supports 8-bit */
    avctx->bits_per_raw_sample = 8;

    /* Is it interlaced? */
    avctx->coded_frame->interlaced_frame = info.flags & 0x800 ? 1 : 0;

    /* Apparently Ut Video doesn't store this info... */
    avctx->coded_frame->top_field_first = 1;

    /*
     * Create a Ut Video instance. Since the function wants
     * an "interface name" string, pass it the name of the lib.
     */
    utv->codec = CCodec::CreateInstance(UNFCC(avctx->codec_tag), "libavcodec");

    /* Initialize Decoding */
    begin_ret = utv->codec->DecodeBegin(format, avctx->width, avctx->height,
                            CBGROSSWIDTH_WINDOWS, &info, sizeof(UtVideoExtra));

    /* Check to see if the decoder initlized properly */
    if (begin_ret != 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Could not initialize decoder: %d\n", begin_ret);
        return -1;
    }

    return 0;
}

static int utvideo_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame, AVPacket *avpkt)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;
    AVFrame *pic = avctx->coded_frame;
    int w = avctx->width, h = avctx->height;

    /* Set flags */
    pic->reference = 0;
    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;

    /* Decode the frame */
    utv->codec->DecodeFrame(utv->buffer, avpkt->data, true);

    /* Set the output data depending on the colorspace */
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        pic->linesize[0] = w;
        pic->linesize[1] = pic->linesize[2] = w / 2;
        pic->data[0] = utv->buffer;
        pic->data[2] = utv->buffer + (w * h);
        pic->data[1] = pic->data[2] + (w * h / 4);
        break;
    case AV_PIX_FMT_YUYV422:
        pic->linesize[0] = w * 2;
        pic->data[0] = utv->buffer;
        break;
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB32:
        /* Make the linesize negative, since Ut Video uses bottom-up BGR */
        pic->linesize[0] = -1 * w * (avctx->pix_fmt == AV_PIX_FMT_BGR24 ? 3 : 4);
        pic->data[0] = utv->buffer + utv->buf_size + pic->linesize[0];
        break;
    }

    *got_frame = 1;
    *(AVFrame *)data = *pic;

    return avpkt->size;
}

static av_cold int utvideo_decode_close(AVCodecContext *avctx)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;

    /* Free output */
    av_freep(&avctx->coded_frame);
    av_freep(&utv->buffer);

    /* Finish decoding and clean up the instance */
    utv->codec->DecodeEnd();
    CCodec::DeleteInstance(utv->codec);

    return 0;
}

AVCodec ff_libutvideo_decoder = {
    "libutvideo",
    NULL_IF_CONFIG_SMALL("Ut Video"),
    AVMEDIA_TYPE_VIDEO,
    AV_CODEC_ID_UTVIDEO,
    0,    //capabilities
    NULL, //supported_framerates
    NULL, //pix_fmts
    NULL, //supported_samplerates
    NULL, //sample_fmts
    NULL, //channel_layouts
    0,    //max_lowres
    NULL, //priv_class
    NULL, //profiles
    sizeof(UtVideoContext),
    NULL, //next
    NULL, //init_thread_copy
    NULL, //update_thread_context
    NULL, //defaults
    NULL, //init_static_data
    utvideo_decode_init,
    NULL, //encode
    NULL, //encode2
    utvideo_decode_frame,
    utvideo_decode_close,
};
