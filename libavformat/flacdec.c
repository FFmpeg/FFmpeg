/*
 * Raw FLAC demuxer
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

#include "libavcodec/flac.h"
#include "avformat.h"
#include "id3v2.h"
#include "internal.h"
#include "rawdec.h"
#include "oggdec.h"
#include "vorbiscomment.h"
#include "libavcodec/bytestream.h"

#define RETURN_ERROR(code) do { ret = (code); goto fail; } while (0)

static int parse_picture(AVFormatContext *s, uint8_t *buf, int buf_size)
{
    const CodecMime *mime = ff_id3v2_mime_tags;
    enum  AVCodecID      id = AV_CODEC_ID_NONE;
    AVBufferRef *data = NULL;
    uint8_t mimetype[64], *desc = NULL;
    AVIOContext *pb = NULL;
    AVStream *st;
    int type, width, height;
    int len, ret = 0;

    pb = avio_alloc_context(buf, buf_size, 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    /* read the picture type */
    type      = avio_rb32(pb);
    if (type >= FF_ARRAY_ELEMS(ff_id3v2_picture_types) || type < 0) {
        av_log(s, AV_LOG_ERROR, "Invalid picture type: %d.\n", type);
        if (s->error_recognition & AV_EF_EXPLODE) {
            RETURN_ERROR(AVERROR_INVALIDDATA);
        }
        type = 0;
    }

    /* picture mimetype */
    len  = avio_rb32(pb);
    if (len <= 0 ||
        avio_read(pb, mimetype, FFMIN(len, sizeof(mimetype) - 1)) != len) {
        av_log(s, AV_LOG_ERROR, "Could not read mimetype from an attached "
               "picture.\n");
        if (s->error_recognition & AV_EF_EXPLODE)
            ret = AVERROR_INVALIDDATA;
        goto fail;
    }
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
    if (!(data = av_buffer_alloc(len))) {
        RETURN_ERROR(AVERROR(ENOMEM));
    }
    if (avio_read(pb, data->data, len) != len) {
        av_log(s, AV_LOG_ERROR, "Error reading attached picture data.\n");
        if (s->error_recognition & AV_EF_EXPLODE)
            ret = AVERROR(EIO);
        goto fail;
    }

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
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = id;
    st->codec->width      = width;
    st->codec->height     = height;
    av_dict_set(&st->metadata, "comment", ff_id3v2_picture_types[type], 0);
    if (desc)
        av_dict_set(&st->metadata, "title",   desc, AV_DICT_DONT_STRDUP_VAL);

    av_freep(&pb);

    return 0;

fail:
    av_buffer_unref(&data);
    av_freep(&desc);
    av_freep(&pb);
    return ret;

}

static int flac_read_header(AVFormatContext *s)
{
    int ret, metadata_last=0, metadata_type, metadata_size, found_streaminfo=0;
    uint8_t header[4];
    uint8_t *buffer=NULL;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = AV_CODEC_ID_FLAC;
    st->need_parsing = AVSTREAM_PARSE_FULL_RAW;
    /* the parameters will be extracted from the compressed bitstream */

    /* if fLaC marker is not found, assume there is no header */
    if (avio_rl32(s->pb) != MKTAG('f','L','a','C')) {
        avio_seek(s->pb, -4, SEEK_CUR);
        return 0;
    }

    /* process metadata blocks */
    while (!url_feof(s->pb) && !metadata_last) {
        avio_read(s->pb, header, 4);
        avpriv_flac_parse_block_header(header, &metadata_last, &metadata_type,
                                   &metadata_size);
        switch (metadata_type) {
        /* allocate and read metadata block for supported types */
        case FLAC_METADATA_TYPE_STREAMINFO:
        case FLAC_METADATA_TYPE_CUESHEET:
        case FLAC_METADATA_TYPE_PICTURE:
        case FLAC_METADATA_TYPE_VORBIS_COMMENT:
            buffer = av_mallocz(metadata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!buffer) {
                return AVERROR(ENOMEM);
            }
            if (avio_read(s->pb, buffer, metadata_size) != metadata_size) {
                RETURN_ERROR(AVERROR(EIO));
            }
            break;
        /* skip metadata block for unsupported types */
        default:
            ret = avio_skip(s->pb, metadata_size);
            if (ret < 0)
                return ret;
        }

        if (metadata_type == FLAC_METADATA_TYPE_STREAMINFO) {
            FLACStreaminfo si;
            /* STREAMINFO can only occur once */
            if (found_streaminfo) {
                RETURN_ERROR(AVERROR_INVALIDDATA);
            }
            if (metadata_size != FLAC_STREAMINFO_SIZE) {
                RETURN_ERROR(AVERROR_INVALIDDATA);
            }
            found_streaminfo = 1;
            st->codec->extradata      = buffer;
            st->codec->extradata_size = metadata_size;
            buffer = NULL;

            /* get codec params from STREAMINFO header */
            avpriv_flac_parse_streaminfo(st->codec, &si, st->codec->extradata);

            /* set time base and duration */
            if (si.samplerate > 0) {
                avpriv_set_pts_info(st, 64, 1, si.samplerate);
                if (si.samples > 0)
                    st->duration = si.samples;
            }
        } else if (metadata_type == FLAC_METADATA_TYPE_CUESHEET) {
            uint8_t isrc[13];
            uint64_t start;
            const uint8_t *offset;
            int i, chapters, track, ti;
            if (metadata_size < 431)
                RETURN_ERROR(AVERROR_INVALIDDATA);
            offset = buffer + 395;
            chapters = bytestream_get_byte(&offset) - 1;
            if (chapters <= 0)
                RETURN_ERROR(AVERROR_INVALIDDATA);
            for (i = 0; i < chapters; i++) {
                if (offset + 36 - buffer > metadata_size)
                    RETURN_ERROR(AVERROR_INVALIDDATA);
                start = bytestream_get_be64(&offset);
                track = bytestream_get_byte(&offset);
                bytestream_get_buffer(&offset, isrc, 12);
                isrc[12] = 0;
                offset += 14;
                ti = bytestream_get_byte(&offset);
                if (ti <= 0) RETURN_ERROR(AVERROR_INVALIDDATA);
                offset += ti * 12;
                avpriv_new_chapter(s, track, st->time_base, start, AV_NOPTS_VALUE, isrc);
            }
            av_freep(&buffer);
        } else if (metadata_type == FLAC_METADATA_TYPE_PICTURE) {
            ret = parse_picture(s, buffer, metadata_size);
            av_freep(&buffer);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Error parsing attached picture.\n");
                return ret;
            }
        } else {
            /* STREAMINFO must be the first block */
            if (!found_streaminfo) {
                RETURN_ERROR(AVERROR_INVALIDDATA);
            }
            /* process supported blocks other than STREAMINFO */
            if (metadata_type == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
                if (ff_vorbis_comment(s, &s->metadata, buffer, metadata_size)) {
                    av_log(s, AV_LOG_WARNING, "error parsing VorbisComment metadata\n");
                }
            }
            av_freep(&buffer);
        }
    }

    return 0;

fail:
    av_free(buffer);
    return ret;
}

static int flac_probe(AVProbeData *p)
{
    if (p->buf_size < 4 || memcmp(p->buf, "fLaC", 4))
        return 0;
    return AVPROBE_SCORE_EXTENSION;
}

AVInputFormat ff_flac_demuxer = {
    .name           = "flac",
    .long_name      = NULL_IF_CONFIG_SMALL("raw FLAC"),
    .read_probe     = flac_probe,
    .read_header    = flac_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "flac",
    .raw_codec_id   = AV_CODEC_ID_FLAC,
};
