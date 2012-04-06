/*
 * MP3 muxer
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avformat.h"
#include "avio_internal.h"
#include "id3v1.h"
#include "id3v2.h"
#include "rawenc.h"
#include "libavutil/avstring.h"
#include "libavcodec/mpegaudio.h"
#include "libavcodec/mpegaudiodata.h"
#include "libavcodec/mpegaudiodecheader.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/avassert.h"

static int id3v1_set_string(AVFormatContext *s, const char *key,
                            uint8_t *buf, int buf_size)
{
    AVDictionaryEntry *tag;
    if ((tag = av_dict_get(s->metadata, key, NULL, 0)))
        av_strlcpy(buf, tag->value, buf_size);
    return !!tag;
}

static int id3v1_create_tag(AVFormatContext *s, uint8_t *buf)
{
    AVDictionaryEntry *tag;
    int i, count = 0;

    memset(buf, 0, ID3v1_TAG_SIZE); /* fail safe */
    buf[0] = 'T';
    buf[1] = 'A';
    buf[2] = 'G';
    count += id3v1_set_string(s, "TIT2",    buf +  3, 30);       //title
    count += id3v1_set_string(s, "TPE1",    buf + 33, 30);       //author|artist
    count += id3v1_set_string(s, "TALB",    buf + 63, 30);       //album
    count += id3v1_set_string(s, "TDRL",    buf + 93,  4);       //date
    count += id3v1_set_string(s, "comment", buf + 97, 30);
    if ((tag = av_dict_get(s->metadata, "TRCK", NULL, 0))) { //track
        buf[125] = 0;
        buf[126] = atoi(tag->value);
        count++;
    }
    buf[127] = 0xFF; /* default to unknown genre */
    if ((tag = av_dict_get(s->metadata, "TCON", NULL, 0))) { //genre
        for(i = 0; i <= ID3v1_GENRE_MAX; i++) {
            if (!av_strcasecmp(tag->value, ff_id3v1_genre_str[i])) {
                buf[127] = i;
                count++;
                break;
            }
        }
    }
    return count;
}

typedef struct MP3Context {
    const AVClass *class;
    ID3v2EncContext id3;
    int id3v2_version;
    int write_id3v1;
    int64_t nb_frames_offset;

    /* index of the audio stream */
    int audio_stream_idx;
    /* number of attached pictures we still need to write */
    int pics_to_write;

    /* audio packets are queued here until we get all the attached pictures */
    AVPacketList *queue, *queue_end;
} MP3Context;

/* insert a dummy frame containing number of frames */
static void mp3_write_xing(AVFormatContext *s)
{
    MP3Context       *mp3 = s->priv_data;
    AVCodecContext *codec = s->streams[mp3->audio_stream_idx]->codec;
    int       bitrate_idx = 1;    // 32 kbps
    int64_t   xing_offset = (codec->channels == 2) ? 32 : 17;
    int32_t        header;
    MPADecodeHeader  mpah;
    int srate_idx, i, channels;

    if (!s->pb->seekable)
        return;

    for (i = 0; i < FF_ARRAY_ELEMS(avpriv_mpa_freq_tab); i++)
        if (avpriv_mpa_freq_tab[i] == codec->sample_rate) {
            srate_idx = i;
            break;
        }
    if (i == FF_ARRAY_ELEMS(avpriv_mpa_freq_tab)) {
        av_log(s, AV_LOG_ERROR, "Unsupported sample rate.\n");
        return;
    }

    switch (codec->channels) {
    case 1:  channels = MPA_MONO;                                          break;
    case 2:  channels = MPA_STEREO;                                        break;
    default: av_log(s, AV_LOG_ERROR, "Unsupported number of channels.\n"); return;
    }

    /* dummy MPEG audio header */
    header  =  0xff                                  << 24; // sync
    header |= (0x7 << 5 | 0x3 << 3 | 0x1 << 1 | 0x1) << 16; // sync/mpeg-1/layer 3/no crc*/
    header |= (bitrate_idx << 4 | srate_idx << 2)    <<  8;
    header |= channels << 6;
    avio_wb32(s->pb, header);

    avpriv_mpegaudio_decode_header(&mpah, header);

    ffio_fill(s->pb, 0, xing_offset);
    ffio_wfourcc(s->pb, "Xing");
    avio_wb32(s->pb, 0x1);    // only number of frames
    mp3->nb_frames_offset = avio_tell(s->pb);
    avio_wb32(s->pb, 0);

    mpah.frame_size -= 4 + xing_offset + 4 + 4 + 4;
    ffio_fill(s->pb, 0, mpah.frame_size);
}

static int mp3_queue_flush(AVFormatContext *s)
{
    MP3Context *mp3 = s->priv_data;
    AVPacketList *pktl;
    int ret = 0, write = 1;

    ff_id3v2_finish(&mp3->id3, s->pb);
    mp3_write_xing(s);

    while ((pktl = mp3->queue)) {
        if (write && (ret = ff_raw_write_packet(s, &pktl->pkt)) < 0)
            write = 0;
        av_free_packet(&pktl->pkt);
        mp3->queue = pktl->next;
        av_freep(&pktl);
    }
    mp3->queue_end = NULL;
    return ret;
}

static int mp3_write_trailer(struct AVFormatContext *s)
{
    uint8_t buf[ID3v1_TAG_SIZE];
    MP3Context *mp3 = s->priv_data;

    if (mp3 && mp3->pics_to_write) {
        av_log(s, AV_LOG_WARNING, "No packets were sent for some of the "
               "attached pictures.\n");
        mp3_queue_flush(s);
    }

    /* write the id3v1 tag */
    if (mp3 && mp3->write_id3v1 && id3v1_create_tag(s, buf) > 0) {
        avio_write(s->pb, buf, ID3v1_TAG_SIZE);
    }

    /* write number of frames */
    if (mp3 && mp3->nb_frames_offset) {
        avio_seek(s->pb, mp3->nb_frames_offset, SEEK_SET);
        avio_wb32(s->pb, s->streams[mp3->audio_stream_idx]->nb_frames);
        avio_seek(s->pb, 0, SEEK_END);
    }

    avio_flush(s->pb);

    return 0;
}

#if CONFIG_MP2_MUXER
AVOutputFormat ff_mp2_muxer = {
    .name              = "mp2",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG audio layer 2"),
    .mime_type         = "audio/x-mpeg",
    .extensions        = "mp2,m2a",
    .audio_codec       = CODEC_ID_MP2,
    .video_codec       = CODEC_ID_NONE,
    .write_packet      = ff_raw_write_packet,
    .write_trailer     = mp3_write_trailer,
};
#endif

#if CONFIG_MP3_MUXER

static const AVOption options[] = {
    { "id3v2_version", "Select ID3v2 version to write. Currently 3 and 4 are supported.",
      offsetof(MP3Context, id3v2_version), AV_OPT_TYPE_INT, {.dbl = 4}, 3, 4, AV_OPT_FLAG_ENCODING_PARAM},
    { "write_id3v1", "Enable ID3v1 writing. ID3v1 tags are written in UTF-8 which may not be supported by most software.",
      offsetof(MP3Context, write_id3v1), AV_OPT_TYPE_INT, {.dbl = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static const AVClass mp3_muxer_class = {
    .class_name     = "MP3 muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int mp3_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MP3Context *mp3 = s->priv_data;

    if (pkt->stream_index == mp3->audio_stream_idx) {
        if (mp3->pics_to_write) {
            /* buffer audio packets until we get all the pictures */
            AVPacketList *pktl = av_mallocz(sizeof(*pktl));
            if (!pktl)
                return AVERROR(ENOMEM);

            pktl->pkt     = *pkt;
            pkt->destruct = NULL;

            if (mp3->queue_end)
                mp3->queue_end->next = pktl;
            else
                mp3->queue = pktl;
            mp3->queue_end = pktl;
        } else
            return ff_raw_write_packet(s, pkt);
    } else {
        int ret;

        /* warn only once for each stream */
        if (s->streams[pkt->stream_index]->nb_frames == 1) {
            av_log(s, AV_LOG_WARNING, "Got more than one picture in stream %d,"
                   " ignoring.\n", pkt->stream_index);
        }
        if (!mp3->pics_to_write || s->streams[pkt->stream_index]->nb_frames >= 1)
            return 0;

        if ((ret = ff_id3v2_write_apic(s, &mp3->id3, pkt)) < 0)
            return ret;
        mp3->pics_to_write--;

        /* flush the buffered audio packets */
        if (!mp3->pics_to_write &&
            (ret = mp3_queue_flush(s)) < 0)
            return ret;
    }

    return 0;
}

/**
 * Write an ID3v2 header at beginning of stream
 */

static int mp3_write_header(struct AVFormatContext *s)
{
    MP3Context  *mp3 = s->priv_data;
    int ret, i;

    /* check the streams -- we want exactly one audio and arbitrary number of
     * video (attached pictures) */
    mp3->audio_stream_idx = -1;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (mp3->audio_stream_idx >= 0 || st->codec->codec_id != CODEC_ID_MP3) {
                av_log(s, AV_LOG_ERROR, "Invalid audio stream. Exactly one MP3 "
                       "audio stream is required.\n");
                return AVERROR(EINVAL);
            }
            mp3->audio_stream_idx = i;
        } else if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO) {
            av_log(s, AV_LOG_ERROR, "Only audio streams and pictures are allowed in MP3.\n");
            return AVERROR(EINVAL);
        }
    }
    if (mp3->audio_stream_idx < 0) {
        av_log(s, AV_LOG_ERROR, "No audio stream present.\n");
        return AVERROR(EINVAL);
    }
    mp3->pics_to_write = s->nb_streams - 1;

    ff_id3v2_start(&mp3->id3, s->pb, mp3->id3v2_version, ID3v2_DEFAULT_MAGIC);
    ret = ff_id3v2_write_metadata(s, &mp3->id3);
    if (ret < 0)
        return ret;

    if (!mp3->pics_to_write) {
        ff_id3v2_finish(&mp3->id3, s->pb);
        mp3_write_xing(s);
    }

    return 0;
}

AVOutputFormat ff_mp3_muxer = {
    .name              = "mp3",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG audio layer 3"),
    .mime_type         = "audio/x-mpeg",
    .extensions        = "mp3",
    .priv_data_size    = sizeof(MP3Context),
    .audio_codec       = CODEC_ID_MP3,
    .video_codec       = CODEC_ID_PNG,
    .write_header      = mp3_write_header,
    .write_packet      = mp3_write_packet,
    .write_trailer     = mp3_write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
    .priv_class        = &mp3_muxer_class,
};
#endif
