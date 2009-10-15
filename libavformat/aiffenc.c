/*
 * AIFF/AIFF-C muxer
 * Copyright (c) 2006  Patrick Guimond
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

#include "avformat.h"
#include "aiff.h"

typedef struct {
    int64_t form;
    int64_t frames;
    int64_t ssnd;
} AIFFOutputContext;

static int aiff_write_header(AVFormatContext *s)
{
    AIFFOutputContext *aiff = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    AVExtFloat sample_rate;
    int aifc = 0;

    /* First verify if format is ok */
    if (!enc->codec_tag)
        return -1;
    if (enc->codec_tag != MKTAG('N','O','N','E'))
        aifc = 1;

    /* FORM AIFF header */
    put_tag(pb, "FORM");
    aiff->form = url_ftell(pb);
    put_be32(pb, 0);                    /* file length */
    put_tag(pb, aifc ? "AIFC" : "AIFF");

    if (aifc) { // compressed audio
        enc->bits_per_coded_sample = 16;
        if (!enc->block_align) {
            av_log(s, AV_LOG_ERROR, "block align not set\n");
            return -1;
        }
        /* Version chunk */
        put_tag(pb, "FVER");
        put_be32(pb, 4);
        put_be32(pb, 0xA2805140);
    }

    /* Common chunk */
    put_tag(pb, "COMM");
    put_be32(pb, aifc ? 24 : 18); /* size */
    put_be16(pb, enc->channels);  /* Number of channels */

    aiff->frames = url_ftell(pb);
    put_be32(pb, 0);              /* Number of frames */

    if (!enc->bits_per_coded_sample)
        enc->bits_per_coded_sample = av_get_bits_per_sample(enc->codec_id);
    if (!enc->bits_per_coded_sample) {
        av_log(s, AV_LOG_ERROR, "could not compute bits per sample\n");
        return -1;
    }
    if (!enc->block_align)
        enc->block_align = (enc->bits_per_coded_sample * enc->channels) >> 3;

    put_be16(pb, enc->bits_per_coded_sample); /* Sample size */

    sample_rate = av_dbl2ext((double)enc->sample_rate);
    put_buffer(pb, (uint8_t*)&sample_rate, sizeof(sample_rate));

    if (aifc) {
        put_le32(pb, enc->codec_tag);
        put_be16(pb, 0);
    }

    /* Sound data chunk */
    put_tag(pb, "SSND");
    aiff->ssnd = url_ftell(pb);         /* Sound chunk size */
    put_be32(pb, 0);                    /* Sound samples data size */
    put_be32(pb, 0);                    /* Data offset */
    put_be32(pb, 0);                    /* Block-size (block align) */

    av_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);

    /* Data is starting here */
    put_flush_packet(pb);

    return 0;
}

static int aiff_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int aiff_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    AIFFOutputContext *aiff = s->priv_data;
    AVCodecContext *enc = s->streams[0]->codec;

    /* Chunks sizes must be even */
    int64_t file_size, end_size;
    end_size = file_size = url_ftell(pb);
    if (file_size & 1) {
        put_byte(pb, 0);
        end_size++;
    }

    if (!url_is_streamed(s->pb)) {
        /* File length */
        url_fseek(pb, aiff->form, SEEK_SET);
        put_be32(pb, file_size - aiff->form - 4);

        /* Number of sample frames */
        url_fseek(pb, aiff->frames, SEEK_SET);
        put_be32(pb, (file_size-aiff->ssnd-12)/enc->block_align);

        /* Sound Data chunk size */
        url_fseek(pb, aiff->ssnd, SEEK_SET);
        put_be32(pb, file_size - aiff->ssnd - 4);

        /* return to the end */
        url_fseek(pb, end_size, SEEK_SET);

        put_flush_packet(pb);
    }

    return 0;
}

AVOutputFormat aiff_muxer = {
    "aiff",
    NULL_IF_CONFIG_SMALL("Audio IFF"),
    "audio/aiff",
    "aif,aiff,afc,aifc",
    sizeof(AIFFOutputContext),
    CODEC_ID_PCM_S16BE,
    CODEC_ID_NONE,
    aiff_write_header,
    aiff_write_packet,
    aiff_write_trailer,
    .codec_tag= (const AVCodecTag* const []){ff_codec_aiff_tags, 0},
};
