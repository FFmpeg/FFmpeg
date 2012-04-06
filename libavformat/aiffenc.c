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

#include "libavutil/intfloat.h"
#include "avformat.h"
#include "internal.h"
#include "aiff.h"
#include "avio_internal.h"
#include "isom.h"

typedef struct {
    int64_t form;
    int64_t frames;
    int64_t ssnd;
} AIFFOutputContext;

static int aiff_write_header(AVFormatContext *s)
{
    AIFFOutputContext *aiff = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    uint64_t sample_rate;
    int aifc = 0;

    /* First verify if format is ok */
    if (!enc->codec_tag)
        return -1;
    if (enc->codec_tag != MKTAG('N','O','N','E'))
        aifc = 1;

    /* FORM AIFF header */
    ffio_wfourcc(pb, "FORM");
    aiff->form = avio_tell(pb);
    avio_wb32(pb, 0);                    /* file length */
    ffio_wfourcc(pb, aifc ? "AIFC" : "AIFF");

    if (aifc) { // compressed audio
        enc->bits_per_coded_sample = 16;
        if (!enc->block_align) {
            av_log(s, AV_LOG_ERROR, "block align not set\n");
            return -1;
        }
        /* Version chunk */
        ffio_wfourcc(pb, "FVER");
        avio_wb32(pb, 4);
        avio_wb32(pb, 0xA2805140);
    }

    if (enc->channels > 2 && enc->channel_layout) {
        ffio_wfourcc(pb, "CHAN");
        avio_wb32(pb, 12);
        ff_mov_write_chan(pb, enc->channel_layout);
    }

    /* Common chunk */
    ffio_wfourcc(pb, "COMM");
    avio_wb32(pb, aifc ? 24 : 18); /* size */
    avio_wb16(pb, enc->channels);  /* Number of channels */

    aiff->frames = avio_tell(pb);
    avio_wb32(pb, 0);              /* Number of frames */

    if (!enc->bits_per_coded_sample)
        enc->bits_per_coded_sample = av_get_bits_per_sample(enc->codec_id);
    if (!enc->bits_per_coded_sample) {
        av_log(s, AV_LOG_ERROR, "could not compute bits per sample\n");
        return -1;
    }
    if (!enc->block_align)
        enc->block_align = (enc->bits_per_coded_sample * enc->channels) >> 3;

    avio_wb16(pb, enc->bits_per_coded_sample); /* Sample size */

    sample_rate = av_double2int(enc->sample_rate);
    avio_wb16(pb, (sample_rate >> 52) + (16383 - 1023));
    avio_wb64(pb, UINT64_C(1) << 63 | sample_rate << 11);

    if (aifc) {
        avio_wl32(pb, enc->codec_tag);
        avio_wb16(pb, 0);
    }

    /* Sound data chunk */
    ffio_wfourcc(pb, "SSND");
    aiff->ssnd = avio_tell(pb);         /* Sound chunk size */
    avio_wb32(pb, 0);                    /* Sound samples data size */
    avio_wb32(pb, 0);                    /* Data offset */
    avio_wb32(pb, 0);                    /* Block-size (block align) */

    avpriv_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);

    /* Data is starting here */
    avio_flush(pb);

    return 0;
}

static int aiff_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    avio_write(pb, pkt->data, pkt->size);
    return 0;
}

static int aiff_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AIFFOutputContext *aiff = s->priv_data;
    AVCodecContext *enc = s->streams[0]->codec;

    /* Chunks sizes must be even */
    int64_t file_size, end_size;
    end_size = file_size = avio_tell(pb);
    if (file_size & 1) {
        avio_w8(pb, 0);
        end_size++;
    }

    if (s->pb->seekable) {
        /* File length */
        avio_seek(pb, aiff->form, SEEK_SET);
        avio_wb32(pb, file_size - aiff->form - 4);

        /* Number of sample frames */
        avio_seek(pb, aiff->frames, SEEK_SET);
        avio_wb32(pb, (file_size-aiff->ssnd-12)/enc->block_align);

        /* Sound Data chunk size */
        avio_seek(pb, aiff->ssnd, SEEK_SET);
        avio_wb32(pb, file_size - aiff->ssnd - 4);

        /* return to the end */
        avio_seek(pb, end_size, SEEK_SET);

        avio_flush(pb);
    }

    return 0;
}

AVOutputFormat ff_aiff_muxer = {
    .name              = "aiff",
    .long_name         = NULL_IF_CONFIG_SMALL("Audio IFF"),
    .mime_type         = "audio/aiff",
    .extensions        = "aif,aiff,afc,aifc",
    .priv_data_size    = sizeof(AIFFOutputContext),
    .audio_codec       = CODEC_ID_PCM_S16BE,
    .video_codec       = CODEC_ID_NONE,
    .write_header      = aiff_write_header,
    .write_packet      = aiff_write_packet,
    .write_trailer     = aiff_write_trailer,
    .codec_tag         = (const AVCodecTag* const []){ ff_codec_aiff_tags, 0 },
};
