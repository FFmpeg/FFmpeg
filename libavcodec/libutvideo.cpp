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

#include <stdlib.h>
#include <utvideo/utvideo.h>
#include <utvideo/Codec.h>

#include "get_bits.h"

typedef struct {
    uint32_t version;
    uint32_t original_format;
    uint32_t stripes;
    uint32_t flags;
} UtVideoExtra;

typedef struct {
    CCodec *codec;
    unsigned int buf_size;
    uint8_t *output;
} UtVideoContext;

static av_cold int utvideo_decode_init(AVCodecContext *avctx)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;
    UtVideoExtra info;
    int defined_fourcc = 0;

    if(avctx->extradata_size != 4*4)
    {
        av_log(avctx, AV_LOG_ERROR, "Extradata size mismatch.\n");
        return -1;
    }

    /* Read extradata */
    info.version = AV_RL32(avctx->extradata);
    info.original_format = AV_RL32(avctx->extradata + 4);
    info.stripes = AV_RL32(avctx->extradata + 8);
    info.flags = AV_RL32(avctx->extradata + 12);

    /* Try to output the original format */
    switch(UNFCC(info.original_format))
    {
        case UTVF_YV12:
            avctx->pix_fmt = PIX_FMT_YUV420P;
            break;
        case UTVF_YUY2:
        case UTVF_YUYV:
        case UTVF_YUNV:
            avctx->pix_fmt = PIX_FMT_YUYV422;
            break;
        case UTVF_UYVY:
        case UTVF_UYNV:
            avctx->pix_fmt = PIX_FMT_UYVY422;
            break;
        case UTVF_RGB24_WIN:
            avctx->pix_fmt = PIX_FMT_BGR24;
            break;
        case UTVF_RGB32_WIN:
            avctx->pix_fmt = PIX_FMT_RGB32;
            break;
        case UTVF_ARGB32_WIN:
            avctx->pix_fmt = PIX_FMT_ARGB;
            break;
        case 0:
            /* Fall back on FOURCC */
            switch(UNFCC(avctx->codec_tag))
            {
                case UTVF_ULY0:
                    avctx->pix_fmt = PIX_FMT_YUV420P;
                    defined_fourcc = UTVF_YV12;
                    break;
                case UTVF_ULY2:
                    avctx->pix_fmt = PIX_FMT_YUYV422;
                    defined_fourcc = UTVF_YUY2;
                    break;
                case UTVF_ULRG:
                    avctx->pix_fmt = PIX_FMT_BGR24;
                    defined_fourcc = UTVF_RGB24_WIN;
                    break;
                case UTVF_ULRA:
                    avctx->pix_fmt = PIX_FMT_RGB32;
                    defined_fourcc = UTVF_RGB32_WIN;
                    break;
            }
            break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                  "Codec ExtraData is Corrupt or Invalid: %X\n", info.original_format);
            return -1;
    }

    /* Only allocate the buffer once */
    utv->buf_size = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
    utv->output = (uint8_t *)av_malloc(utv->buf_size * sizeof(uint8_t));

    if(utv->output == NULL)
    {
        av_log(avctx, AV_LOG_ERROR, "Unable to allocate output buffer.\n");
        return -1;
    }

    /* Allocate the output frame  */
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
    utv->codec->DecodeBegin(defined_fourcc ? defined_fourcc : UNFCC(info.original_format),
                            avctx->width, avctx->height, CBGROSSWIDTH_WINDOWS, &info,
                            sizeof(UtVideoExtra));

    return 0;
}

static int utvideo_decode_frame(AVCodecContext *avctx, void *data,
                                int *data_size, AVPacket *avpkt)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;
    AVFrame *pic = avctx->coded_frame;
    unsigned int w = avctx->width, h = avctx->height;

    /* Set flags */
    pic->reference = 0;
    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;

    /* Decode the frame */
    utv->codec->DecodeFrame(utv->output, avpkt->data, true);

    /* Set the output data depending on the colorspace */
    switch(avctx->pix_fmt)
    {
        case PIX_FMT_YUV420P:
            pic->linesize[0] = w;
            pic->linesize[1] = pic->linesize[2] = w / 2;
            pic->data[0] = utv->output;
            pic->data[2] = utv->output + (w * h);
            pic->data[1] = pic->data[2] + (w * h / 4);
            break;
        case PIX_FMT_YUYV422:
        case PIX_FMT_UYVY422:
            pic->linesize[0] = w * 2;
            pic->data[0] = utv->output;
            break;
        case PIX_FMT_BGR24:
        case PIX_FMT_RGB32:
            /* Make the linesize negative, since Ut Video uses bottom-up BGR */
            pic->linesize[0] = -1 * w * (avctx->pix_fmt == PIX_FMT_BGR24 ? 3 : 4);
            pic->data[0] = utv->output + utv->buf_size + pic->linesize[0];
            break;
     }

    *data_size = sizeof(AVFrame);
    *(AVFrame *)data = *pic;

    return avpkt->size;
}

static av_cold int utvideo_decode_close(AVCodecContext *avctx)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;

    /* Free output */
    av_freep(&avctx->coded_frame);
    av_freep(&utv->output);

    /* Finish decoding and clean up the instance */
    utv->codec->DecodeEnd();
    CCodec::DeleteInstance(utv->codec);

    return 0;
}

AVCodec ff_libutvideo_decoder = {
    "utvideo",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_UTVIDEO,
    sizeof(UtVideoContext),
    utvideo_decode_init,
    NULL,
    utvideo_decode_close,
    utvideo_decode_frame,
    CODEC_CAP_LOSSLESS,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL_IF_CONFIG_SMALL("Ut Video"),
};
