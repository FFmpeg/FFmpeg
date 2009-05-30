/*
 * SoX native format muxer
 * Copyright (c) 2009 Daniel Verkamp <daniel@drv.nu>
 *
 * Based on libSoX sox-fmt.c
 * Copyright (c) 2008 robs@users.sourceforge.net
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
 * SoX native format muxer
 * @file libavformat/soxenc.c
 * @author Daniel Verkamp
 * @sa http://wiki.multimedia.cx/index.php?title=SoX_native_intermediate_format
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "sox.h"

typedef struct {
    int64_t header_size;
} SoXContext;

static int sox_write_header(AVFormatContext *s)
{
    SoXContext *sox = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    AVMetadataTag *comment;
    size_t comment_len = 0, comment_size;

    comment = av_metadata_get(s->metadata, "comment", NULL, 0);
    if (comment)
        comment_len = strlen(comment->value);
    comment_size = (comment_len + 7) & ~7;

    sox->header_size = SOX_FIXED_HDR + comment_size;

    if (enc->codec_id == CODEC_ID_PCM_S32LE) {
        put_tag(pb, ".SoX");
        put_le32(pb, sox->header_size);
        put_le64(pb, 0); /* number of samples */
        put_le64(pb, av_dbl2int(enc->sample_rate));
        put_le32(pb, enc->channels);
        put_le32(pb, comment_size);
    } else if (enc->codec_id == CODEC_ID_PCM_S32BE) {
        put_tag(pb, "XoS.");
        put_be32(pb, sox->header_size);
        put_be64(pb, 0); /* number of samples */
        put_be64(pb, av_dbl2int(enc->sample_rate));
        put_be32(pb, enc->channels);
        put_be32(pb, comment_size);
    } else {
        av_log(s, AV_LOG_ERROR, "invalid codec; use pcm_s32le or pcm_s32be\n");
        return -1;
    }

    if (comment_len)
        put_buffer(pb, comment->value, comment_len);

    for ( ; comment_size > comment_len; comment_len++)
        put_byte(pb, 0);

    put_flush_packet(pb);

    return 0;
}

static int sox_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int sox_write_trailer(AVFormatContext *s)
{
    SoXContext *sox = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;

    if (!url_is_streamed(s->pb)) {
        /* update number of samples */
        int64_t file_size = url_ftell(pb);
        int64_t num_samples = (file_size - sox->header_size - 4LL) >> 2LL;
        url_fseek(pb, 8, SEEK_SET);
        if (enc->codec_id == CODEC_ID_PCM_S32LE) {
            put_le64(pb, num_samples);
        } else
            put_be64(pb, num_samples);
        url_fseek(pb, file_size, SEEK_SET);

        put_flush_packet(pb);
    }

    return 0;
}

AVOutputFormat sox_muxer = {
    "sox",
    NULL_IF_CONFIG_SMALL("SoX native format"),
    NULL,
    "sox",
    sizeof(SoXContext),
    CODEC_ID_PCM_S32LE,
    CODEC_ID_NONE,
    sox_write_header,
    sox_write_packet,
    sox_write_trailer,
};
