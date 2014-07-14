/*
 * MP3 muxer
 * Copyright (c) 2003 Fabrice Bellard
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
    /* we knowingly overspecify each tag length by one byte to compensate for the mandatory null byte added by av_strlcpy */
    count += id3v1_set_string(s, "TIT2",    buf +  3, 30 + 1);       //title
    count += id3v1_set_string(s, "TPE1",    buf + 33, 30 + 1);       //author|artist
    count += id3v1_set_string(s, "TALB",    buf + 63, 30 + 1);       //album
    count += id3v1_set_string(s, "TDRL",    buf + 93,  4 + 1);       //date
    count += id3v1_set_string(s, "comment", buf + 97, 30 + 1);
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

#define XING_NUM_BAGS 400
#define XING_TOC_SIZE 100
// maximum size of the xing frame: offset/Xing/flags/frames/size/TOC
#define XING_MAX_SIZE (32 + 4 + 4 + 4 + 4 + XING_TOC_SIZE)

typedef struct MP3Context {
    const AVClass *class;
    ID3v2EncContext id3;
    int id3v2_version;
    int write_id3v1;
    int write_xing;

    /* xing header */
    int64_t xing_offset;
    int32_t frames;
    int32_t size;
    uint32_t want;
    uint32_t seen;
    uint32_t pos;
    uint64_t bag[XING_NUM_BAGS];
    int initial_bitrate;
    int has_variable_bitrate;

    /* index of the audio stream */
    int audio_stream_idx;
    /* number of attached pictures we still need to write */
    int pics_to_write;

    /* audio packets are queued here until we get all the attached pictures */
    AVPacketList *queue, *queue_end;
} MP3Context;

static const uint8_t xing_offtbl[2][2] = {{32, 17}, {17, 9}};

/*
 * Write an empty XING header and initialize respective data.
 */
static int mp3_write_xing(AVFormatContext *s)
{
    MP3Context       *mp3 = s->priv_data;
    AVCodecContext *codec = s->streams[mp3->audio_stream_idx]->codec;
    int32_t          header;
    MPADecodeHeader  mpah;
    int srate_idx, i, channels;
    int bitrate_idx;
    int best_bitrate_idx = -1;
    int best_bitrate_error = INT_MAX;
    int xing_offset;
    int ver = 0;
    int bytes_needed;
    const char *vendor = (codec->flags & CODEC_FLAG_BITEXACT) ? "Lavf" : LIBAVFORMAT_IDENT;

    if (!s->pb->seekable || !mp3->write_xing)
        return 0;

    for (i = 0; i < FF_ARRAY_ELEMS(avpriv_mpa_freq_tab); i++) {
        const uint16_t base_freq = avpriv_mpa_freq_tab[i];

        if      (codec->sample_rate == base_freq)     ver = 0x3; // MPEG 1
        else if (codec->sample_rate == base_freq / 2) ver = 0x2; // MPEG 2
        else if (codec->sample_rate == base_freq / 4) ver = 0x0; // MPEG 2.5
        else continue;

        srate_idx = i;
        break;
    }
    if (i == FF_ARRAY_ELEMS(avpriv_mpa_freq_tab)) {
        av_log(s, AV_LOG_WARNING, "Unsupported sample rate, not writing Xing header.\n");
        return -1;
    }

    switch (codec->channels) {
    case 1:  channels = MPA_MONO;                                          break;
    case 2:  channels = MPA_STEREO;                                        break;
    default: av_log(s, AV_LOG_WARNING, "Unsupported number of channels, "
                    "not writing Xing header.\n");
             return -1;
    }

    /* dummy MPEG audio header */
    header  =  0xffU                                 << 24; // sync
    header |= (0x7 << 5 | ver << 3 | 0x1 << 1 | 0x1) << 16; // sync/audio-version/layer 3/no crc*/
    header |= (srate_idx << 2) << 8;
    header |= channels << 6;

    for (bitrate_idx = 1; bitrate_idx < 15; bitrate_idx++) {
        int bit_rate = 1000 * avpriv_mpa_bitrate_tab[ver != 3][3 - 1][bitrate_idx];
        int error    = FFABS(bit_rate - codec->bit_rate);

        if (error < best_bitrate_error) {
            best_bitrate_error = error;
            best_bitrate_idx   = bitrate_idx;
        }
    }
    av_assert0(best_bitrate_idx >= 0);

    for (bitrate_idx = best_bitrate_idx; ; bitrate_idx++) {
        int32_t mask = bitrate_idx << (4 + 8);
        if (15 == bitrate_idx)
            return -1;
        header |= mask;

        avpriv_mpegaudio_decode_header(&mpah, header);
        xing_offset=xing_offtbl[mpah.lsf == 1][mpah.nb_channels == 1];
        bytes_needed = 4              // header
               + xing_offset
               + 4              // xing tag
               + 4              // frames/size/toc flags
               + 4              // frames
               + 4              // size
               + XING_TOC_SIZE   // toc
               + 24
               ;

        if (bytes_needed <= mpah.frame_size)
            break;

        header &= ~mask;
    }

    avio_wb32(s->pb, header);

    ffio_fill(s->pb, 0, xing_offset);
    mp3->xing_offset = avio_tell(s->pb);
    ffio_wfourcc(s->pb, "Xing");
    avio_wb32(s->pb, 0x01 | 0x02 | 0x04);  // frames / size / TOC

    mp3->size = mpah.frame_size;
    mp3->want=1;
    mp3->seen=0;
    mp3->pos=0;

    avio_wb32(s->pb, 0);  // frames
    avio_wb32(s->pb, 0);  // size

    // toc
    for (i = 0; i < XING_TOC_SIZE; ++i)
        avio_w8(s->pb, (uint8_t)(255 * i / XING_TOC_SIZE));

    for (i = 0; i < strlen(vendor); ++i)
        avio_w8(s->pb, vendor[i]);
    for (; i < 21; ++i)
        avio_w8(s->pb, 0);
    avio_wb24(s->pb, FFMAX(codec->delay - 528 - 1, 0)<<12);

    ffio_fill(s->pb, 0, mpah.frame_size - bytes_needed);

    return 0;
}

/*
 * Add a frame to XING data.
 * Following lame's "VbrTag.c".
 */
static void mp3_xing_add_frame(MP3Context *mp3, AVPacket *pkt)
{
    int i;

    mp3->frames++;
    mp3->seen++;
    mp3->size += pkt->size;

    if (mp3->want == mp3->seen) {
        mp3->bag[mp3->pos] = mp3->size;

        if (XING_NUM_BAGS == ++mp3->pos) {
            /* shrink table to half size by throwing away each second bag. */
            for (i = 1; i < XING_NUM_BAGS; i += 2)
                mp3->bag[i >> 1] = mp3->bag[i];

            /* double wanted amount per bag. */
            mp3->want *= 2;
            /* adjust current position to half of table size. */
            mp3->pos = XING_NUM_BAGS / 2;
        }

        mp3->seen = 0;
    }
}

static int mp3_write_audio_packet(AVFormatContext *s, AVPacket *pkt)
{
    MP3Context  *mp3 = s->priv_data;

    if (pkt->data && pkt->size >= 4) {
        MPADecodeHeader mpah;
        int av_unused base;
        uint32_t h;

        h = AV_RB32(pkt->data);
        if (ff_mpa_check_header(h) == 0) {
            avpriv_mpegaudio_decode_header(&mpah, h);
            if (!mp3->initial_bitrate)
                mp3->initial_bitrate = mpah.bit_rate;
            if ((mpah.bit_rate == 0) || (mp3->initial_bitrate != mpah.bit_rate))
                mp3->has_variable_bitrate = 1;
        } else {
            av_log(s, AV_LOG_WARNING, "Audio packet of size %d (starting with %08X...) "
                   "is invalid, writing it anyway.\n", pkt->size, h);
        }

#ifdef FILTER_VBR_HEADERS
        /* filter out XING and INFO headers. */
        base = 4 + xing_offtbl[mpah.lsf == 1][mpah.nb_channels == 1];

        if (base + 4 <= pkt->size) {
            uint32_t v = AV_RB32(pkt->data + base);

            if (MKBETAG('X','i','n','g') == v || MKBETAG('I','n','f','o') == v)
                return 0;
        }

        /* filter out VBRI headers. */
        base = 4 + 32;

        if (base + 4 <= pkt->size && MKBETAG('V','B','R','I') == AV_RB32(pkt->data + base))
            return 0;
#endif

        if (mp3->xing_offset)
            mp3_xing_add_frame(mp3, pkt);
    }

    return ff_raw_write_packet(s, pkt);
}

static int mp3_queue_flush(AVFormatContext *s)
{
    MP3Context *mp3 = s->priv_data;
    AVPacketList *pktl;
    int ret = 0, write = 1;

    ff_id3v2_finish(&mp3->id3, s->pb, s->metadata_header_padding);
    mp3_write_xing(s);

    while ((pktl = mp3->queue)) {
        if (write && (ret = mp3_write_audio_packet(s, &pktl->pkt)) < 0)
            write = 0;
        av_free_packet(&pktl->pkt);
        mp3->queue = pktl->next;
        av_freep(&pktl);
    }
    mp3->queue_end = NULL;
    return ret;
}

static void mp3_update_xing(AVFormatContext *s)
{
    MP3Context  *mp3 = s->priv_data;
    int i;

    /* replace "Xing" identification string with "Info" for CBR files. */
    if (!mp3->has_variable_bitrate) {
        avio_seek(s->pb, mp3->xing_offset, SEEK_SET);
        ffio_wfourcc(s->pb, "Info");
    }

    avio_seek(s->pb, mp3->xing_offset + 8, SEEK_SET);
    avio_wb32(s->pb, mp3->frames);
    avio_wb32(s->pb, mp3->size);

    avio_w8(s->pb, 0);  // first toc entry has to be zero.

    for (i = 1; i < XING_TOC_SIZE; ++i) {
        int j = i * mp3->pos / XING_TOC_SIZE;
        int seek_point = 256LL * mp3->bag[j] / mp3->size;
        avio_w8(s->pb, FFMIN(seek_point, 255));
    }

    avio_seek(s->pb, 0, SEEK_END);
}

static int mp3_write_trailer(struct AVFormatContext *s)
{
    uint8_t buf[ID3v1_TAG_SIZE];
    MP3Context *mp3 = s->priv_data;

    if (mp3->pics_to_write) {
        av_log(s, AV_LOG_WARNING, "No packets were sent for some of the "
               "attached pictures.\n");
        mp3_queue_flush(s);
    }

    /* write the id3v1 tag */
    if (mp3->write_id3v1 && id3v1_create_tag(s, buf) > 0) {
        avio_write(s->pb, buf, ID3v1_TAG_SIZE);
    }

    if (mp3->xing_offset)
        mp3_update_xing(s);

    return 0;
}

static int query_codec(enum AVCodecID id, int std_compliance)
{
    const CodecMime *cm= ff_id3v2_mime_tags;
    while(cm->id != AV_CODEC_ID_NONE) {
        if(id == cm->id)
            return MKTAG('A', 'P', 'I', 'C');
        cm++;
    }
    return -1;
}

#if CONFIG_MP2_MUXER
AVOutputFormat ff_mp2_muxer = {
    .name              = "mp2",
    .long_name         = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
    .mime_type         = "audio/x-mpeg",
    .extensions        = "mp2,m2a,mpa",
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MP3_MUXER

static const AVOption options[] = {
    { "id3v2_version", "Select ID3v2 version to write. Currently 3 and 4 are supported.",
      offsetof(MP3Context, id3v2_version), AV_OPT_TYPE_INT, {.i64 = 4}, 0, 4, AV_OPT_FLAG_ENCODING_PARAM},
    { "write_id3v1", "Enable ID3v1 writing. ID3v1 tags are written in UTF-8 which may not be supported by most software.",
      offsetof(MP3Context, write_id3v1), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { "write_xing",  "Write the Xing header containing file duration.",
      offsetof(MP3Context, write_xing),  AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
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
            int ret;
            if (!pktl)
                return AVERROR(ENOMEM);

            ret = av_copy_packet(&pktl->pkt, pkt);
            if (ret < 0) {
                av_freep(&pktl);
                return ret;
            }

            if (mp3->queue_end)
                mp3->queue_end->next = pktl;
            else
                mp3->queue = pktl;
            mp3->queue_end = pktl;
        } else
            return mp3_write_audio_packet(s, pkt);
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

    if (mp3->id3v2_version      &&
        mp3->id3v2_version != 3 &&
        mp3->id3v2_version != 4) {
        av_log(s, AV_LOG_ERROR, "Invalid ID3v2 version requested: %d. Only "
               "3, 4 or 0 (disabled) are allowed.\n", mp3->id3v2_version);
        return AVERROR(EINVAL);
    }

    /* check the streams -- we want exactly one audio and arbitrary number of
     * video (attached pictures) */
    mp3->audio_stream_idx = -1;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (mp3->audio_stream_idx >= 0 || st->codec->codec_id != AV_CODEC_ID_MP3) {
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

    if (mp3->pics_to_write && !mp3->id3v2_version) {
        av_log(s, AV_LOG_ERROR, "Attached pictures were requested, but the "
               "ID3v2 header is disabled.\n");
        return AVERROR(EINVAL);
    }

    if (mp3->id3v2_version) {
        ff_id3v2_start(&mp3->id3, s->pb, mp3->id3v2_version, ID3v2_DEFAULT_MAGIC);
        ret = ff_id3v2_write_metadata(s, &mp3->id3);
        if (ret < 0)
            return ret;
    }

    if (!mp3->pics_to_write) {
        if (mp3->id3v2_version)
            ff_id3v2_finish(&mp3->id3, s->pb, s->metadata_header_padding);
        mp3_write_xing(s);
    }

    return 0;
}

AVOutputFormat ff_mp3_muxer = {
    .name              = "mp3",
    .long_name         = NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
    .mime_type         = "audio/x-mpeg",
    .extensions        = "mp3",
    .priv_data_size    = sizeof(MP3Context),
    .audio_codec       = AV_CODEC_ID_MP3,
    .video_codec       = AV_CODEC_ID_PNG,
    .write_header      = mp3_write_header,
    .write_packet      = mp3_write_packet,
    .write_trailer     = mp3_write_trailer,
    .query_codec       = query_codec,
    .flags             = AVFMT_NOTIMESTAMPS,
    .priv_class        = &mp3_muxer_class,
};
#endif
