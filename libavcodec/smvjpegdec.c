/*
 * SMV JPEG decoder
 * Copyright (c) 2013 Ash Hughes
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
 * SMV JPEG decoder.
 */

// #define DEBUG
#include "avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "mjpegdec.h"
#include "internal.h"

typedef struct SMVJpegDecodeContext {
    MJpegDecodeContext jpg;
    AVFrame *picture[2]; /* pictures array */
    AVCodecContext* avctx;
    int frames_per_jpeg;
    int mjpeg_data_size;
} SMVJpegDecodeContext;

static inline void smv_img_pnt_plane(uint8_t      **dst, uint8_t *src,
                                     int src_linesize, int height, int nlines)
{
    if (!dst || !src)
        return;
    src += (nlines) * src_linesize * height;
    *dst = src;
}

static inline void smv_img_pnt(uint8_t *dst_data[4], uint8_t *src_data[4],
                               const int src_linesizes[4],
                               enum AVPixelFormat pix_fmt, int width, int height,
                               int nlines)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int i, planes_nb = 0;

    if (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return;

    for (i = 0; i < desc->nb_components; i++)
        planes_nb = FFMAX(planes_nb, desc->comp[i].plane + 1);

    for (i = 0; i < planes_nb; i++) {
        int h = height;
        if (i == 1 || i == 2) {
            h = AV_CEIL_RSHIFT(height, desc->log2_chroma_h);
        }
        smv_img_pnt_plane(&dst_data[i], src_data[i],
            src_linesizes[i], h, nlines);
    }
    if (desc->flags & AV_PIX_FMT_FLAG_PAL ||
        desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
        dst_data[1] = src_data[1];
}

static av_cold int smvjpeg_decode_end(AVCodecContext *avctx)
{
    SMVJpegDecodeContext *s = avctx->priv_data;
    MJpegDecodeContext *jpg = &s->jpg;
    int ret;

    jpg->picture_ptr = NULL;
    av_frame_free(&s->picture[0]);
    av_frame_free(&s->picture[1]);
    ret = avcodec_close(s->avctx);
    av_freep(&s->avctx);
    return ret;
}

static av_cold int smvjpeg_decode_init(AVCodecContext *avctx)
{
    SMVJpegDecodeContext *s = avctx->priv_data;
    AVCodec *codec;
    AVDictionary *thread_opt = NULL;
    int ret = 0, r;

    s->frames_per_jpeg = 0;

    s->picture[0] = av_frame_alloc();
    if (!s->picture[0])
        return AVERROR(ENOMEM);

    s->picture[1] = av_frame_alloc();
    if (!s->picture[1]) {
        av_frame_free(&s->picture[0]);
        return AVERROR(ENOMEM);
    }

    s->jpg.picture_ptr      = s->picture[0];

    if (avctx->extradata_size >= 4)
        s->frames_per_jpeg = AV_RL32(avctx->extradata);

    if (s->frames_per_jpeg <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of frames per jpeg.\n");
        ret = AVERROR_INVALIDDATA;
    }

    codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        av_log(avctx, AV_LOG_ERROR, "MJPEG codec not found\n");
        smvjpeg_decode_end(avctx);
        return AVERROR_DECODER_NOT_FOUND;
    }

    s->avctx = avcodec_alloc_context3(codec);

    av_dict_set(&thread_opt, "threads", "1", 0);
    s->avctx->refcounted_frames = 1;
    s->avctx->flags = avctx->flags;
    s->avctx->idct_algo = avctx->idct_algo;
    if ((r = ff_codec_open2_recursive(s->avctx, codec, &thread_opt)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "MJPEG codec failed to open\n");
        ret = r;
    }
    av_dict_free(&thread_opt);

    if (ret < 0)
        smvjpeg_decode_end(avctx);
    return ret;
}

static int smvjpeg_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt)
{
    const AVPixFmtDescriptor *desc;
    SMVJpegDecodeContext *s = avctx->priv_data;
    AVFrame* mjpeg_data = s->picture[0];
    int i, cur_frame = 0, ret = 0;

    cur_frame = avpkt->pts % s->frames_per_jpeg;

    /* cur_frame is later used to calculate the buffer offset, so it mustn't be negative */
    if (cur_frame < 0)
        cur_frame += s->frames_per_jpeg;

    /* Are we at the start of a block? */
    if (!cur_frame) {
        av_frame_unref(mjpeg_data);
        ret = avcodec_decode_video2(s->avctx, mjpeg_data, &s->mjpeg_data_size, avpkt);
        if (ret < 0) {
            s->mjpeg_data_size = 0;
            return ret;
        }
    } else if (!s->mjpeg_data_size)
        return AVERROR(EINVAL);

    desc = av_pix_fmt_desc_get(s->avctx->pix_fmt);
    av_assert0(desc);

    if (mjpeg_data->height % (s->frames_per_jpeg << desc->log2_chroma_h)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid height\n");
        return AVERROR_INVALIDDATA;
    }

    /*use the last lot... */
    *data_size = s->mjpeg_data_size;

    avctx->pix_fmt = s->avctx->pix_fmt;

    /* We shouldn't get here if frames_per_jpeg <= 0 because this was rejected
       in init */
    ret = ff_set_dimensions(avctx, mjpeg_data->width, mjpeg_data->height / s->frames_per_jpeg);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to set dimensions\n");
        return ret;
    }

    if (*data_size) {
        s->picture[1]->extended_data = NULL;
        s->picture[1]->width         = avctx->width;
        s->picture[1]->height        = avctx->height;
        s->picture[1]->format        = avctx->pix_fmt;
        /* ff_init_buffer_info(avctx, &s->picture[1]); */
        smv_img_pnt(s->picture[1]->data, mjpeg_data->data, mjpeg_data->linesize,
                    avctx->pix_fmt, avctx->width, avctx->height, cur_frame);
        for (i = 0; i < AV_NUM_DATA_POINTERS; i++)
            s->picture[1]->linesize[i] = mjpeg_data->linesize[i];

        ret = av_frame_ref(data, s->picture[1]);
        if (ret < 0)
            return ret;
    }

    return avpkt->size;
}

static const AVClass smvjpegdec_class = {
    .class_name = "SMVJPEG decoder",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_smvjpeg_decoder = {
    .name           = "smvjpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("SMV JPEG"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SMVJPEG,
    .priv_data_size = sizeof(SMVJpegDecodeContext),
    .init           = smvjpeg_decode_init,
    .close          = smvjpeg_decode_end,
    .decode         = smvjpeg_decode_frame,
    .priv_class     = &smvjpegdec_class,
};
