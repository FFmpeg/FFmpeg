/*
 * MxPEG decoder
 * Copyright (c) 2011 Anatoly Nenashev
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
 * MxPEG decoder
 */

#include "mjpeg.h"
#include "mjpegdec.h"

typedef struct MXpegDecodeContext {
    MJpegDecodeContext jpg;
    AVFrame picture[2]; /* pictures array */
    int picture_index; /* index of current picture */
    int got_sof_data; /* true if SOF data successfully parsed */
    int got_mxm_bitmask; /* true if MXM bitmask available */
    uint8_t *mxm_bitmask; /* bitmask buffer */
    unsigned bitmask_size; /* size of bitmask */
    int has_complete_frame; /* true if has complete frame */
    uint8_t *completion_bitmask; /* completion bitmask of macroblocks */
    unsigned mb_width, mb_height; /* size of picture in MB's from MXM header */
} MXpegDecodeContext;

static av_cold int mxpeg_decode_init(AVCodecContext *avctx)
{
    MXpegDecodeContext *s = avctx->priv_data;

    s->picture[0].reference = s->picture[1].reference = 3;
    s->jpg.picture_ptr      = &s->picture[0];
    return ff_mjpeg_decode_init(avctx);
}

static int mxpeg_decode_app(MXpegDecodeContext *s,
                            const uint8_t *buf_ptr, int buf_size)
{
    int len;
    if (buf_size < 2)
        return 0;
    len = AV_RB16(buf_ptr);
    skip_bits(&s->jpg.gb, 8*FFMIN(len,buf_size));

    return 0;
}

static int mxpeg_decode_mxm(MXpegDecodeContext *s,
                            const uint8_t *buf_ptr, int buf_size)
{
    unsigned bitmask_size, mb_count;
    int i;

    s->mb_width  = AV_RL16(buf_ptr+4);
    s->mb_height = AV_RL16(buf_ptr+6);
    mb_count = s->mb_width * s->mb_height;

    bitmask_size = (mb_count + 7) >> 3;
    if (bitmask_size > buf_size - 12) {
        av_log(s->jpg.avctx, AV_LOG_ERROR,
               "MXM bitmask is not complete\n");
        return AVERROR(EINVAL);
    }

    if (s->bitmask_size != bitmask_size) {
        s->bitmask_size = 0;
        av_freep(&s->mxm_bitmask);
        s->mxm_bitmask = av_malloc(bitmask_size);
        if (!s->mxm_bitmask) {
            av_log(s->jpg.avctx, AV_LOG_ERROR,
                   "MXM bitmask memory allocation error\n");
            return AVERROR(ENOMEM);
        }

        av_freep(&s->completion_bitmask);
        s->completion_bitmask = av_mallocz(bitmask_size);
        if (!s->completion_bitmask) {
            av_log(s->jpg.avctx, AV_LOG_ERROR,
                   "Completion bitmask memory allocation error\n");
            return AVERROR(ENOMEM);
        }

        s->bitmask_size = bitmask_size;
    }

    memcpy(s->mxm_bitmask, buf_ptr + 12, bitmask_size);
    s->got_mxm_bitmask = 1;

    if (!s->has_complete_frame) {
        uint8_t completion_check = 0xFF;
        for (i = 0; i < bitmask_size; ++i) {
            s->completion_bitmask[i] |= s->mxm_bitmask[i];
            completion_check &= s->completion_bitmask[i];
        }
        s->has_complete_frame = !(completion_check ^ 0xFF);
    }

    return 0;
}

static int mxpeg_decode_com(MXpegDecodeContext *s,
                            const uint8_t *buf_ptr, int buf_size)
{
    int len, ret = 0;
    if (buf_size < 2)
        return 0;
    len = AV_RB16(buf_ptr);
    if (len > 14 && len <= buf_size && !strncmp(buf_ptr + 2, "MXM", 3)) {
        ret = mxpeg_decode_mxm(s, buf_ptr + 2, len - 2);
    }
    skip_bits(&s->jpg.gb, 8*FFMIN(len,buf_size));

    return ret;
}

static int mxpeg_check_dimensions(MXpegDecodeContext *s, MJpegDecodeContext *jpg,
                                  AVFrame *reference_ptr)
{
    if ((jpg->width + 0x0F)>>4 != s->mb_width ||
        (jpg->height + 0x0F)>>4 != s->mb_height) {
        av_log(jpg->avctx, AV_LOG_ERROR,
               "Picture dimensions stored in SOF and MXM mismatch\n");
        return AVERROR(EINVAL);
    }

    if (reference_ptr->data[0]) {
        int i;
        for (i = 0; i < MAX_COMPONENTS; ++i) {
            if ( (!reference_ptr->data[i] ^ !jpg->picture_ptr->data[i]) ||
                 reference_ptr->linesize[i] != jpg->picture_ptr->linesize[i]) {
                av_log(jpg->avctx, AV_LOG_ERROR,
                       "Dimensions of current and reference picture mismatch\n");
                return AVERROR(EINVAL);
            }
        }
    }

    return 0;
}

static int mxpeg_decode_frame(AVCodecContext *avctx,
                          void *data, int *data_size,
                          AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MXpegDecodeContext *s = avctx->priv_data;
    MJpegDecodeContext *jpg = &s->jpg;
    const uint8_t *buf_end, *buf_ptr;
    const uint8_t *unescaped_buf_ptr;
    int unescaped_buf_size;
    int start_code;
    AVFrame *picture = data;
    int ret;

    buf_ptr = buf;
    buf_end = buf + buf_size;
    jpg->got_picture = 0;
    s->got_mxm_bitmask = 0;
    while (buf_ptr < buf_end) {
        start_code = ff_mjpeg_find_marker(jpg, &buf_ptr, buf_end,
                                          &unescaped_buf_ptr, &unescaped_buf_size);
        if (start_code < 0)
            goto the_end;
        {
            init_get_bits(&jpg->gb, unescaped_buf_ptr, unescaped_buf_size*8);

            if (start_code >= APP0 && start_code <= APP15) {
                mxpeg_decode_app(s, unescaped_buf_ptr, unescaped_buf_size);
            }

            switch (start_code) {
            case SOI:
                if (jpg->got_picture) //emulating EOI
                    goto the_end;
                break;
            case EOI:
                goto the_end;
            case DQT:
                ret = ff_mjpeg_decode_dqt(jpg);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "quantization table decode error\n");
                    return ret;
                }
                break;
            case DHT:
                ret = ff_mjpeg_decode_dht(jpg);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "huffman table decode error\n");
                    return ret;
                }
                break;
            case COM:
                ret = mxpeg_decode_com(s, unescaped_buf_ptr,
                                       unescaped_buf_size);
                if (ret < 0)
                    return ret;
                break;
            case SOF0:
                s->got_sof_data = 0;
                ret = ff_mjpeg_decode_sof(jpg);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "SOF data decode error\n");
                    return ret;
                }
                if (jpg->interlaced) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Interlaced mode not supported in MxPEG\n");
                    return AVERROR(EINVAL);
                }
                s->got_sof_data = 1;
                break;
            case SOS:
                if (!s->got_sof_data) {
                    av_log(avctx, AV_LOG_WARNING,
                           "Can not process SOS without SOF data, skipping\n");
                    break;
                }
                if (!jpg->got_picture) {
                    if (jpg->first_picture) {
                        av_log(avctx, AV_LOG_WARNING,
                               "First picture has no SOF, skipping\n");
                        break;
                    }
                    if (!s->got_mxm_bitmask){
                        av_log(avctx, AV_LOG_WARNING,
                               "Non-key frame has no MXM, skipping\n");
                        break;
                    }
                    /* use stored SOF data to allocate current picture */
                    if (jpg->picture_ptr->data[0])
                        avctx->release_buffer(avctx, jpg->picture_ptr);
                    if (avctx->get_buffer(avctx, jpg->picture_ptr) < 0) {
                        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
                        return AVERROR(ENOMEM);
                    }
                    jpg->picture_ptr->pict_type = AV_PICTURE_TYPE_P;
                    jpg->picture_ptr->key_frame = 0;
                    jpg->got_picture = 1;
                } else {
                    jpg->picture_ptr->pict_type = AV_PICTURE_TYPE_I;
                    jpg->picture_ptr->key_frame = 1;
                }

                if (s->got_mxm_bitmask) {
                    AVFrame *reference_ptr = &s->picture[s->picture_index ^ 1];
                    if (mxpeg_check_dimensions(s, jpg, reference_ptr) < 0)
                        break;

                    /* allocate dummy reference picture if needed */
                    if (!reference_ptr->data[0] &&
                        avctx->get_buffer(avctx, reference_ptr) < 0) {
                        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
                        return AVERROR(ENOMEM);
                    }

                    ret = ff_mjpeg_decode_sos(jpg, s->mxm_bitmask, reference_ptr);
                    if (ret < 0 && (avctx->err_recognition & AV_EF_EXPLODE))
                        return ret;
                } else {
                    ret = ff_mjpeg_decode_sos(jpg, NULL, NULL);
                    if (ret < 0 && (avctx->err_recognition & AV_EF_EXPLODE))
                        return ret;
                }

                break;
            }

            buf_ptr += (get_bits_count(&jpg->gb)+7) >> 3;
        }

    }

the_end:
    if (jpg->got_picture) {
        *data_size = sizeof(AVFrame);
        *picture = *jpg->picture_ptr;
        s->picture_index ^= 1;
        jpg->picture_ptr = &s->picture[s->picture_index];

        if (!s->has_complete_frame) {
            if (!s->got_mxm_bitmask)
                s->has_complete_frame = 1;
            else
                *data_size = 0;
        }
    }

    return buf_ptr - buf;
}

static av_cold int mxpeg_decode_end(AVCodecContext *avctx)
{
    MXpegDecodeContext *s = avctx->priv_data;
    MJpegDecodeContext *jpg = &s->jpg;
    int i;

    jpg->picture_ptr = NULL;
    ff_mjpeg_decode_end(avctx);

    for (i = 0; i < 2; ++i) {
        if (s->picture[i].data[0])
            avctx->release_buffer(avctx, &s->picture[i]);
    }

    av_freep(&s->mxm_bitmask);
    av_freep(&s->completion_bitmask);

    return 0;
}

AVCodec ff_mxpeg_decoder = {
    .name           = "mxpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("Mobotix MxPEG video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_MXPEG,
    .priv_data_size = sizeof(MXpegDecodeContext),
    .init           = mxpeg_decode_init,
    .close          = mxpeg_decode_end,
    .decode         = mxpeg_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .max_lowres     = 3,
};
