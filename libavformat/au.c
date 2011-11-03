/*
 * AU muxer and demuxer
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

/*
 * First version by Francois Revol revol@free.fr
 *
 * Reference documents:
 * http://www.opengroup.org/public/pubs/external/auformat.html
 * http://www.goice.co.jp/member/mo/formats/au.html
 */

#include "avformat.h"
#include "avio_internal.h"
#include "pcm.h"
#include "riff.h"

/* if we don't know the size in advance */
#define AU_UNKNOWN_SIZE ((uint32_t)(~0))

/* The libavcodec codecs we support, and the IDs they have in the file */
static const AVCodecTag codec_au_tags[] = {
    { CODEC_ID_PCM_MULAW, 1 },
    { CODEC_ID_PCM_S8, 2 },
    { CODEC_ID_PCM_S16BE, 3 },
    { CODEC_ID_PCM_S24BE, 4 },
    { CODEC_ID_PCM_S32BE, 5 },
    { CODEC_ID_PCM_F32BE, 6 },
    { CODEC_ID_PCM_F64BE, 7 },
    { CODEC_ID_PCM_ALAW, 27 },
    { CODEC_ID_NONE, 0 },
};

#if CONFIG_AU_MUXER
/* AUDIO_FILE header */
static int put_au_header(AVIOContext *pb, AVCodecContext *enc)
{
    if(!enc->codec_tag)
        return -1;
    ffio_wfourcc(pb, ".snd");    /* magic number */
    avio_wb32(pb, 24);           /* header size */
    avio_wb32(pb, AU_UNKNOWN_SIZE); /* data size */
    avio_wb32(pb, (uint32_t)enc->codec_tag);     /* codec ID */
    avio_wb32(pb, enc->sample_rate);
    avio_wb32(pb, (uint32_t)enc->channels);
    return 0;
}

static int au_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;

    s->priv_data = NULL;

    /* format header */
    if (put_au_header(pb, s->streams[0]->codec) < 0) {
        return -1;
    }

    avio_flush(pb);

    return 0;
}

static int au_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    avio_write(pb, pkt->data, pkt->size);
    return 0;
}

static int au_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int64_t file_size;

    if (s->pb->seekable) {

        /* update file size */
        file_size = avio_tell(pb);
        avio_seek(pb, 8, SEEK_SET);
        avio_wb32(pb, (uint32_t)(file_size - 24));
        avio_seek(pb, file_size, SEEK_SET);

        avio_flush(pb);
    }

    return 0;
}
#endif /* CONFIG_AU_MUXER */

static int au_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf[0] == '.' && p->buf[1] == 's' &&
        p->buf[2] == 'n' && p->buf[3] == 'd')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* au input */
static int au_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    int size, bps, data_size = 0;
    unsigned int tag;
    AVIOContext *pb = s->pb;
    unsigned int id, channels, rate;
    enum CodecID codec;
    AVStream *st;

    /* check ".snd" header */
    tag = avio_rl32(pb);
    if (tag != MKTAG('.', 's', 'n', 'd'))
        return -1;
    size = avio_rb32(pb); /* header size */
    data_size = avio_rb32(pb); /* data size in bytes */

    if (data_size < 0 && data_size != AU_UNKNOWN_SIZE) {
        av_log(s, AV_LOG_ERROR, "Invalid negative data size '%d' found\n", data_size);
        return AVERROR_INVALIDDATA;
    }

    id = avio_rb32(pb);
    rate = avio_rb32(pb);
    channels = avio_rb32(pb);

    codec = ff_codec_get_id(codec_au_tags, id);

    if (!(bps = av_get_bits_per_sample(codec))) {
        av_log_ask_for_sample(s, "could not determine bits per sample\n");
        return AVERROR_INVALIDDATA;
    }

    if (size >= 24) {
        /* skip unused data */
        avio_skip(pb, size - 24);
    }

    /* now we are ready: build format streams */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return -1;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_tag = id;
    st->codec->codec_id = codec;
    st->codec->channels = channels;
    st->codec->sample_rate = rate;
    if (data_size != AU_UNKNOWN_SIZE)
    st->duration = (((int64_t)data_size)<<3) / (st->codec->channels * bps);
    av_set_pts_info(st, 64, 1, rate);
    return 0;
}

#define BLOCK_SIZE 1024

static int au_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    int ret;

    ret= av_get_packet(s->pb, pkt, BLOCK_SIZE *
                       s->streams[0]->codec->channels *
                       av_get_bits_per_sample(s->streams[0]->codec->codec_id) >> 3);
    if (ret < 0)
        return ret;
    pkt->stream_index = 0;

    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return 0;
}

#if CONFIG_AU_DEMUXER
AVInputFormat ff_au_demuxer = {
    .name           = "au",
    .long_name      = NULL_IF_CONFIG_SMALL("SUN AU format"),
    .read_probe     = au_probe,
    .read_header    = au_read_header,
    .read_packet    = au_read_packet,
    .read_seek      = pcm_read_seek,
    .codec_tag= (const AVCodecTag* const []){codec_au_tags, 0},
};
#endif

#if CONFIG_AU_MUXER
AVOutputFormat ff_au_muxer = {
    .name              = "au",
    .long_name         = NULL_IF_CONFIG_SMALL("SUN AU format"),
    .mime_type         = "audio/basic",
    .extensions        = "au",
    .audio_codec       = CODEC_ID_PCM_S16BE,
    .video_codec       = CODEC_ID_NONE,
    .write_header      = au_write_header,
    .write_packet      = au_write_packet,
    .write_trailer     = au_write_trailer,
    .codec_tag= (const AVCodecTag* const []){codec_au_tags, 0},
};
#endif //CONFIG_AU_MUXER
