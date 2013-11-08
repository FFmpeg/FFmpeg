/*
 * Xiph CELT decoder using libcelt
 * Copyright (c) 2011 Nicolas George
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

#include <celt/celt.h>
#include <celt/celt_header.h>
#include "avcodec.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"

struct libcelt_context {
    CELTMode *mode;
    CELTDecoder *dec;
    int discard;
};

static int ff_celt_error_to_averror(int err)
{
    switch (err) {
        case CELT_BAD_ARG:          return AVERROR(EINVAL);
#ifdef CELT_BUFFER_TOO_SMALL
        case CELT_BUFFER_TOO_SMALL: return AVERROR(ENOBUFS);
#endif
        case CELT_INTERNAL_ERROR:   return AVERROR(EFAULT);
        case CELT_CORRUPTED_DATA:   return AVERROR_INVALIDDATA;
        case CELT_UNIMPLEMENTED:    return AVERROR(ENOSYS);
#ifdef ENOTRECOVERABLE
        case CELT_INVALID_STATE:    return AVERROR(ENOTRECOVERABLE);
#endif
        case CELT_ALLOC_FAIL:       return AVERROR(ENOMEM);
        default:                    return AVERROR(EINVAL);
    }
}

static int ff_celt_bitstream_version_hack(CELTMode *mode)
{
    CELTHeader header = { .version_id = 0 };
    celt_header_init(&header, mode, 960, 2);
    return header.version_id;
}

static av_cold int libcelt_dec_init(AVCodecContext *c)
{
    struct libcelt_context *celt = c->priv_data;
    int err;

    if (!c->channels || !c->frame_size ||
        c->frame_size > INT_MAX / sizeof(int16_t) / c->channels)
        return AVERROR(EINVAL);
    celt->mode = celt_mode_create(c->sample_rate, c->frame_size, &err);
    if (!celt->mode)
        return ff_celt_error_to_averror(err);
    celt->dec = celt_decoder_create_custom(celt->mode, c->channels, &err);
    if (!celt->dec) {
        celt_mode_destroy(celt->mode);
        return ff_celt_error_to_averror(err);
    }
    if (c->extradata_size >= 4) {
        celt->discard = AV_RL32(c->extradata);
        if (celt->discard < 0 || celt->discard >= c->frame_size) {
            av_log(c, AV_LOG_WARNING,
                   "Invalid overlap (%d), ignored.\n", celt->discard);
            celt->discard = 0;
        }
    }
    if (c->extradata_size >= 8) {
        unsigned version = AV_RL32(c->extradata + 4);
        unsigned lib_version = ff_celt_bitstream_version_hack(celt->mode);
        if (version != lib_version)
            av_log(c, AV_LOG_WARNING,
                   "CELT bitstream version 0x%x may be "
                   "improperly decoded by libcelt for version 0x%x.\n",
                   version, lib_version);
    }
    c->sample_fmt = AV_SAMPLE_FMT_S16;
    return 0;
}

static av_cold int libcelt_dec_close(AVCodecContext *c)
{
    struct libcelt_context *celt = c->priv_data;

    celt_decoder_destroy(celt->dec);
    celt_mode_destroy(celt->mode);
    return 0;
}

static int libcelt_dec_decode(AVCodecContext *c, void *data,
                              int *got_frame_ptr, AVPacket *pkt)
{
    struct libcelt_context *celt = c->priv_data;
    AVFrame *frame = data;
    int err;
    int16_t *pcm;

    frame->nb_samples = c->frame_size;
    if ((err = ff_get_buffer(c, frame, 0)) < 0)
        return err;
    pcm = (int16_t *)frame->data[0];
    err = celt_decode(celt->dec, pkt->data, pkt->size, pcm, c->frame_size);
    if (err < 0)
        return ff_celt_error_to_averror(err);
    if (celt->discard) {
        frame->nb_samples -= celt->discard;
        memmove(pcm, pcm + celt->discard * c->channels,
                frame->nb_samples * c->channels * sizeof(int16_t));
        celt->discard = 0;
    }
    *got_frame_ptr = 1;
    return pkt->size;
}

AVCodec ff_libcelt_decoder = {
    .name           = "libcelt",
    .long_name      = NULL_IF_CONFIG_SMALL("Xiph CELT decoder using libcelt"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_CELT,
    .priv_data_size = sizeof(struct libcelt_context),
    .init           = libcelt_dec_init,
    .close          = libcelt_dec_close,
    .decode         = libcelt_dec_decode,
    .capabilities   = CODEC_CAP_DR1,
};
