/*
 * Raw FLAC picture parser
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/png.h"
#include "avformat.h"
#include "flac_picture.h"
#include "id3v2.h"
#include "internal.h"
#include "avio_internal.h"

int ff_flac_parse_picture(AVFormatContext *s, uint8_t *buf, int buf_size)
{
    const CodecMime *mime = ff_id3v2_mime_tags;
    enum AVCodecID id = AV_CODEC_ID_NONE;
    AVBufferRef *data = NULL;
    uint8_t mimetype[64], *desc = NULL;
    AVIOContext pb0, *pb = &pb0;
    AVStream *st;
    int width, height, ret = 0;
    int len;
    unsigned int type;

    ffio_init_context(pb, buf, buf_size, 0, NULL, NULL, NULL, NULL);

    /* read the picture type */
    type = avio_rb32(pb);
    if (type >= FF_ARRAY_ELEMS(ff_id3v2_picture_types)) {
        av_log(s, AV_LOG_ERROR, "Invalid picture type: %d.\n", type);
        if (s->error_recognition & AV_EF_EXPLODE) {
            RETURN_ERROR(AVERROR_INVALIDDATA);
        }
        type = 0;
    }

    /* picture mimetype */
    len = avio_rb32(pb);
    if (len <= 0 || len >= 64 ||
        avio_read(pb, mimetype, FFMIN(len, sizeof(mimetype) - 1)) != len) {
        av_log(s, AV_LOG_ERROR, "Could not read mimetype from an attached "
               "picture.\n");
        if (s->error_recognition & AV_EF_EXPLODE)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    av_assert0(len < sizeof(mimetype));
    mimetype[len] = 0;

    while (mime->id != AV_CODEC_ID_NONE) {
        if (!strncmp(mime->str, mimetype, sizeof(mimetype))) {
            id = mime->id;
            break;
        }
        mime++;
    }
    if (id == AV_CODEC_ID_NONE) {
        av_log(s, AV_LOG_ERROR, "Unknown attached picture mimetype: %s.\n",
               mimetype);
        if (s->error_recognition & AV_EF_EXPLODE)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    /* picture description */
    len = avio_rb32(pb);
    if (len > 0) {
        if (!(desc = av_malloc(len + 1))) {
            RETURN_ERROR(AVERROR(ENOMEM));
        }

        if (avio_read(pb, desc, len) != len) {
            av_log(s, AV_LOG_ERROR, "Error reading attached picture description.\n");
            if (s->error_recognition & AV_EF_EXPLODE)
                ret = AVERROR(EIO);
            goto fail;
        }
        desc[len] = 0;
    }

    /* picture metadata */
    width  = avio_rb32(pb);
    height = avio_rb32(pb);
    avio_skip(pb, 8);

    /* picture data */
    len = avio_rb32(pb);
    if (len <= 0) {
        av_log(s, AV_LOG_ERROR, "Invalid attached picture size: %d.\n", len);
        if (s->error_recognition & AV_EF_EXPLODE)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    if (!(data = av_buffer_alloc(len + AV_INPUT_BUFFER_PADDING_SIZE))) {
        RETURN_ERROR(AVERROR(ENOMEM));
    }
    memset(data->data + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    if (avio_read(pb, data->data, len) != len) {
        av_log(s, AV_LOG_ERROR, "Error reading attached picture data.\n");
        if (s->error_recognition & AV_EF_EXPLODE)
            ret = AVERROR(EIO);
        goto fail;
    }

    if (AV_RB64(data->data) == PNGSIG)
        id = AV_CODEC_ID_PNG;

    st = avformat_new_stream(s, NULL);
    if (!st) {
        RETURN_ERROR(AVERROR(ENOMEM));
    }

    av_init_packet(&st->attached_pic);
    st->attached_pic.buf          = data;
    st->attached_pic.data         = data->data;
    st->attached_pic.size         = len;
    st->attached_pic.stream_index = st->index;
    st->attached_pic.flags       |= AV_PKT_FLAG_KEY;

    st->disposition      |= AV_DISPOSITION_ATTACHED_PIC;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = id;
    st->codecpar->width      = width;
    st->codecpar->height     = height;
    av_dict_set(&st->metadata, "comment", ff_id3v2_picture_types[type], 0);
    if (desc)
        av_dict_set(&st->metadata, "title", desc, AV_DICT_DONT_STRDUP_VAL);

    return 0;

fail:
    av_buffer_unref(&data);
    av_freep(&desc);

    return ret;
}
