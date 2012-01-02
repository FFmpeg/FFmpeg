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
#include "libavcodec/mpegaudio.h"
#include "libavcodec/mpegaudiodata.h"
#include "libavcodec/mpegaudiodecheader.h"
#include "libavformat/avio_internal.h"
#include "libavutil/dict.h"

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

#define VBR_NUM_BAGS 400
#define VBR_TOC_SIZE 100

typedef struct MP3Context {
    const AVClass *class;
    int id3v2_version;
    int write_id3v1;
    int64_t frames_offset;
    int32_t frames;
    int32_t size;
    uint32_t want;
    uint32_t seen;
    uint32_t pos;
    uint64_t bag[VBR_NUM_BAGS];
} MP3Context;

static int mp2_write_trailer(struct AVFormatContext *s)
{
    uint8_t buf[ID3v1_TAG_SIZE];
    MP3Context *mp3 = s->priv_data;

    /* write the id3v1 tag */
    if (mp3 && mp3->write_id3v1 && id3v1_create_tag(s, buf) > 0) {
        avio_write(s->pb, buf, ID3v1_TAG_SIZE);
    }

    /* write number of frames */
    if (mp3 && mp3->frames_offset) {
        avio_seek(s->pb, mp3->frames_offset, SEEK_SET);
        avio_wb32(s->pb, s->streams[0]->nb_frames);
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
    .write_trailer     = mp2_write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
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

static const int64_t xing_offtbl[2][2] = {{32, 17}, {17,9}};

/*
 * Write an empty XING header and initialize respective data.
 */
static int mp3_write_xing(AVFormatContext *s)
{
    AVCodecContext   *codec = s->streams[0]->codec;
    MP3Context       *mp3 = s->priv_data;
    int              bitrate_idx;
    int              best_bitrate_idx;
    int              best_bitrate_error= INT_MAX;
    int64_t          xing_offset;
    int32_t          header, mask;
    MPADecodeHeader  c;
    int              srate_idx, i, channels;
    int              needed;

    for (i = 0; i < FF_ARRAY_ELEMS(avpriv_mpa_freq_tab); i++)
        if (avpriv_mpa_freq_tab[i] == codec->sample_rate) {
            srate_idx = i;
            break;
        }
    if (i == FF_ARRAY_ELEMS(avpriv_mpa_freq_tab)) {
        av_log(s, AV_LOG_ERROR, "Unsupported sample rate.\n");
        return -1;
    }

    switch (codec->channels) {
    case 1:  channels = MPA_MONO;                                          break;
    case 2:  channels = MPA_STEREO;                                        break;
    default: av_log(s, AV_LOG_ERROR, "Unsupported number of channels.\n"); return -1;
    }

    /* dummy MPEG audio header */
    header  =  0xff                                  << 24; // sync
    header |= (0x7 << 5 | 0x3 << 3 | 0x1 << 1 | 0x1) << 16; // sync/mpeg-1/layer 3/no crc*/
    header |= (srate_idx << 2) <<  8;
    header |= channels << 6;

    for (bitrate_idx=1; bitrate_idx<15; bitrate_idx++) {
        int error;
        avpriv_mpegaudio_decode_header(&c, header | (bitrate_idx << (4+8)));
        error= FFABS(c.bit_rate - codec->bit_rate);
        if(error < best_bitrate_error){
            best_bitrate_error= error;
            best_bitrate_idx  = bitrate_idx;
        }
    }

    for (bitrate_idx= best_bitrate_idx;; bitrate_idx++) {
        if (15 == bitrate_idx)
            return -1;
        mask = bitrate_idx << (4+8);
        header |= mask;
        avpriv_mpegaudio_decode_header(&c, header);
        xing_offset=xing_offtbl[c.lsf == 1][c.nb_channels == 1];
        needed = 4              // header
               + xing_offset
               + 4              // xing tag
               + 4              // frames/size/toc flags
               + 4              // frames
               + 4              // size
               + VBR_TOC_SIZE;  // toc

        if (needed <= c.frame_size)
            break;
        header &= ~mask;
    }

    avio_wb32(s->pb, header);
    ffio_fill(s->pb, 0, xing_offset);
    avio_wb32(s->pb, MKBETAG('X', 'i', 'n', 'g'));
    avio_wb32(s->pb, 0x01 | 0x02 | 0x04);  // frames/size/toc

    mp3->frames_offset = avio_tell(s->pb);
    mp3->size = c.frame_size;
    mp3->want=1;
    mp3->seen=0;
    mp3->pos=0;

    avio_wb32(s->pb, 0);  // frames
    avio_wb32(s->pb, 0);  // size

    // toc
    for (i = 0; i < VBR_TOC_SIZE; ++i)
        avio_w8(s->pb, (uint8_t)(255 * i / VBR_TOC_SIZE));

    ffio_fill(s->pb, 0, c.frame_size - needed);
    avio_flush(s->pb);

    return 0;
}

/*
 * Add a frame to XING data.
 * Following lame's "VbrTag.c".
 */
static void mp3_xing_add_frame(AVFormatContext *s, AVPacket *pkt)
{
    MP3Context  *mp3 = s->priv_data;
    int i;

    ++mp3->frames;
    mp3->size += pkt->size;

    if (mp3->want == ++mp3->seen) {
        mp3->bag[mp3->pos] = mp3->size;

        if (VBR_NUM_BAGS == ++mp3->pos) {
            /* shrink table to half size by throwing away each second bag. */
            for (i = 1; i < VBR_NUM_BAGS; i += 2)
                mp3->bag[i >> 1] = mp3->bag[i];

            /* double wanted amount per bag. */
            mp3->want <<= 1;
            /* adjust current position to half of table size. */
            mp3->pos >>= 1;
        }

        mp3->seen = 0;
    }
}

static void mp3_fix_xing(AVFormatContext *s)
{
    MP3Context  *mp3 = s->priv_data;
    int i;

    avio_flush(s->pb);
    avio_seek(s->pb, mp3->frames_offset, SEEK_SET);
    avio_wb32(s->pb, mp3->frames);
    avio_wb32(s->pb, mp3->size);

    avio_w8(s->pb, 0);  // first toc entry has to be zero.

    for (i = 1; i < VBR_TOC_SIZE; ++i) {
        int j = i * mp3->pos / VBR_TOC_SIZE;
        int seek_point = 256LL * mp3->bag[j] / mp3->size;
        avio_w8(s->pb, FFMIN(seek_point, 255));
    }

    avio_flush(s->pb);
    avio_seek(s->pb, 0, SEEK_END);
}

/**
 * Write an ID3v2 header at beginning of stream
 */

static int mp3_write_header(struct AVFormatContext *s)
{
    MP3Context  *mp3 = s->priv_data;
    int ret;

    ret = ff_id3v2_write(s, mp3->id3v2_version, ID3v2_DEFAULT_MAGIC);
    if (ret < 0)
        return ret;

    if (s->pb->seekable)
        mp3_write_xing(s);

    return 0;
}

static int mp3_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (! pkt || ! pkt->data || pkt->size < 4)
        return ff_raw_write_packet(s, pkt);
    else {
        MP3Context  *mp3 = s->priv_data;
#ifdef FILTER_VBR_HEADERS
        MPADecodeHeader c;
        int base;

        ff_mpegaudio_decode_header(&c, AV_RB32(pkt->data));

        /* filter out XING and INFO headers. */
        base = 4 + xing_offtbl[c.lsf == 1][c.nb_channels == 1];

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

        if (mp3->frames_offset)
            mp3_xing_add_frame(s, pkt);

        return ff_raw_write_packet(s, pkt);
    }
}

static int mp3_write_trailer(AVFormatContext *s)
{
    MP3Context  *mp3 = s->priv_data;
    int ret=mp2_write_trailer(s);

    if (ret < 0)
        return ret;

    if (mp3->frames_offset)
        mp3_fix_xing(s);

    return 0;
}

AVOutputFormat ff_mp3_muxer = {
    .name              = "mp3",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG audio layer 3"),
    .mime_type         = "audio/x-mpeg",
    .extensions        = "mp3",
    .priv_data_size    = sizeof(MP3Context),
    .audio_codec       = CODEC_ID_MP3,
    .video_codec       = CODEC_ID_NONE,
    .write_header      = mp3_write_header,
    .write_packet      = mp3_write_packet,
    .write_trailer     = mp3_write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
    .priv_class = &mp3_muxer_class,
};
#endif
