/*
 * MP3 muxer and demuxer
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

#include <strings.h>
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "id3v2.h"
#include "id3v1.h"

#if CONFIG_MP3_DEMUXER

#include "libavcodec/mpegaudio.h"
#include "libavcodec/mpegaudiodecheader.h"

/* mp3 read */

static int mp3_read_probe(AVProbeData *p)
{
    int max_frames, first_frames = 0;
    int fsize, frames, sample_rate;
    uint32_t header;
    uint8_t *buf, *buf0, *buf2, *end;
    AVCodecContext avctx;

    buf0 = p->buf;
    if(ff_id3v2_match(buf0, ID3v2_DEFAULT_MAGIC)) {
        buf0 += ff_id3v2_tag_len(buf0);
    }
    end = p->buf + p->buf_size - sizeof(uint32_t);
    while(buf0 < end && !*buf0)
        buf0++;

    max_frames = 0;
    buf = buf0;

    for(; buf < end; buf= buf2+1) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            header = AV_RB32(buf2);
            fsize = ff_mpa_decode_header(&avctx, header, &sample_rate, &sample_rate, &sample_rate, &sample_rate);
            if(fsize < 0)
                break;
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == buf0)
            first_frames= frames;
    }
    // keep this in sync with ac3 probe, both need to avoid
    // issues with MPEG-files!
    if   (first_frames>=4) return AVPROBE_SCORE_MAX/2+1;
    else if(max_frames>500)return AVPROBE_SCORE_MAX/2;
    else if(max_frames>=4) return AVPROBE_SCORE_MAX/4;
    else if(buf0!=p->buf)  return AVPROBE_SCORE_MAX/4-1;
    else if(max_frames>=1) return 1;
    else                   return 0;
//mpegps_mp3_unrecognized_format.mpg has max_frames=3
}

/**
 * Try to find Xing/Info/VBRI tags and compute duration from info therein
 */
static int mp3_parse_vbr_tags(AVFormatContext *s, AVStream *st, int64_t base)
{
    uint32_t v, spf;
    unsigned frames = 0; /* Total number of frames in file */
    unsigned size = 0; /* Total number of bytes in the stream */
    const int64_t xing_offtbl[2][2] = {{32, 17}, {17,9}};
    MPADecodeHeader c;
    int vbrtag_size = 0;

    v = get_be32(s->pb);
    if(ff_mpa_check_header(v) < 0)
      return -1;

    if (ff_mpegaudio_decode_header(&c, v) == 0)
        vbrtag_size = c.frame_size;
    if(c.layer != 3)
        return -1;

    /* Check for Xing / Info tag */
    url_fseek(s->pb, xing_offtbl[c.lsf == 1][c.nb_channels == 1], SEEK_CUR);
    v = get_be32(s->pb);
    if(v == MKBETAG('X', 'i', 'n', 'g') || v == MKBETAG('I', 'n', 'f', 'o')) {
        v = get_be32(s->pb);
        if(v & 0x1)
            frames = get_be32(s->pb);
        if(v & 0x2)
            size = get_be32(s->pb);
    }

    /* Check for VBRI tag (always 32 bytes after end of mpegaudio header) */
    url_fseek(s->pb, base + 4 + 32, SEEK_SET);
    v = get_be32(s->pb);
    if(v == MKBETAG('V', 'B', 'R', 'I')) {
        /* Check tag version */
        if(get_be16(s->pb) == 1) {
            /* skip delay and quality */
            url_fseek(s->pb, 4, SEEK_CUR);
            frames = get_be32(s->pb);
            size = get_be32(s->pb);
        }
    }

    if(!frames && !size)
        return -1;

    /* Skip the vbr tag frame */
    url_fseek(s->pb, base + vbrtag_size, SEEK_SET);

    spf = c.lsf ? 576 : 1152; /* Samples per frame, layer 3 */
    if(frames)
        st->duration = av_rescale_q(frames, (AVRational){spf, c.sample_rate},
                                    st->time_base);
    if(size)
        st->codec->bit_rate = av_rescale(size, 8 * c.sample_rate, frames * (int64_t)spf);

    return 0;
}

static int mp3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;
    int64_t off;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_MP3;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    st->start_time = 0;

    // lcm of all mp3 sample rates
    av_set_pts_info(st, 64, 1, 14112000);

    ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC);
    off = url_ftell(s->pb);

    if (!av_metadata_get(s->metadata, "", NULL, AV_METADATA_IGNORE_SUFFIX))
        ff_id3v1_read(s);

    if (mp3_parse_vbr_tags(s, st, off) < 0)
        url_fseek(s->pb, off, SEEK_SET);

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

#define MP3_PACKET_SIZE 1024

static int mp3_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;
    //    AVStream *st = s->streams[0];

    size= MP3_PACKET_SIZE;

    ret= av_get_packet(s->pb, pkt, size);

    pkt->stream_index = 0;
    if (ret <= 0) {
        return AVERROR(EIO);
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

AVInputFormat mp3_demuxer = {
    "mp3",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 2/3"),
    0,
    mp3_read_probe,
    mp3_read_header,
    mp3_read_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mp2,mp3,m2a", /* XXX: use probe */
    .metadata_conv = ff_id3v2_metadata_conv,
};
#endif

#if CONFIG_MP2_MUXER || CONFIG_MP3_MUXER
static int id3v1_set_string(AVFormatContext *s, const char *key,
                            uint8_t *buf, int buf_size)
{
    AVMetadataTag *tag;
    if ((tag = av_metadata_get(s->metadata, key, NULL, 0)))
        strncpy(buf, tag->value, buf_size);
    return !!tag;
}

static int id3v1_create_tag(AVFormatContext *s, uint8_t *buf)
{
    AVMetadataTag *tag;
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
    if ((tag = av_metadata_get(s->metadata, "TRCK", NULL, 0))) { //track
        buf[125] = 0;
        buf[126] = atoi(tag->value);
        count++;
    }
    buf[127] = 0xFF; /* default to unknown genre */
    if ((tag = av_metadata_get(s->metadata, "TCON", NULL, 0))) { //genre
        for(i = 0; i <= ID3v1_GENRE_MAX; i++) {
            if (!strcasecmp(tag->value, ff_id3v1_genre_str[i])) {
                buf[127] = i;
                count++;
                break;
            }
        }
    }
    return count;
}

/* simple formats */

static void id3v2_put_size(AVFormatContext *s, int size)
{
    put_byte(s->pb, size >> 21 & 0x7f);
    put_byte(s->pb, size >> 14 & 0x7f);
    put_byte(s->pb, size >> 7  & 0x7f);
    put_byte(s->pb, size       & 0x7f);
}

static void id3v2_put_ttag(AVFormatContext *s, const char *buf, int len,
                           uint32_t tag)
{
    put_be32(s->pb, tag);
    id3v2_put_size(s, len + 1);
    put_be16(s->pb, 0);
    put_byte(s->pb, 3); /* UTF-8 */
    put_buffer(s->pb, buf, len);
}


static int mp3_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(s->pb, pkt->data, pkt->size);
    put_flush_packet(s->pb);
    return 0;
}

static int mp3_write_trailer(struct AVFormatContext *s)
{
    uint8_t buf[ID3v1_TAG_SIZE];

    /* write the id3v1 tag */
    if (id3v1_create_tag(s, buf) > 0) {
        put_buffer(s->pb, buf, ID3v1_TAG_SIZE);
        put_flush_packet(s->pb);
    }
    return 0;
}
#endif /* CONFIG_MP2_MUXER || CONFIG_MP3_MUXER */

#if CONFIG_MP2_MUXER
AVOutputFormat mp2_muxer = {
    "mp2",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 2"),
    "audio/x-mpeg",
    "mp2,m2a",
    0,
    CODEC_ID_MP2,
    CODEC_ID_NONE,
    NULL,
    mp3_write_packet,
    mp3_write_trailer,
    .metadata_conv = ff_id3v2_metadata_conv,
};
#endif

#if CONFIG_MP3_MUXER
/**
 * Write an ID3v2.4 header at beginning of stream
 */

static int mp3_write_header(struct AVFormatContext *s)
{
    AVMetadataTag *t = NULL;
    int totlen = 0;
    int64_t size_pos, cur_pos;

    put_be32(s->pb, MKBETAG('I', 'D', '3', 0x04)); /* ID3v2.4 */
    put_byte(s->pb, 0);
    put_byte(s->pb, 0); /* flags */

    /* reserve space for size */
    size_pos = url_ftell(s->pb);
    put_be32(s->pb, 0);

    while ((t = av_metadata_get(s->metadata, "", t, AV_METADATA_IGNORE_SUFFIX))) {
        uint32_t tag = 0;

        if (t->key[0] == 'T' && strlen(t->key) == 4) {
            int i;
            for (i = 0; *ff_id3v2_tags[i]; i++)
                if (AV_RB32(t->key) == AV_RB32(ff_id3v2_tags[i])) {
                    int len = strlen(t->value);
                    tag = AV_RB32(t->key);
                    totlen += len + ID3v2_HEADER_SIZE + 2;
                    id3v2_put_ttag(s, t->value, len + 1, tag);
                    break;
                }
        }

        if (!tag) { /* unknown tag, write as TXXX frame */
            int   len = strlen(t->key), len1 = strlen(t->value);
            char *buf = av_malloc(len + len1 + 2);
            if (!buf)
                return AVERROR(ENOMEM);
            tag = MKBETAG('T', 'X', 'X', 'X');
            strcpy(buf,           t->key);
            strcpy(buf + len + 1, t->value);
            id3v2_put_ttag(s, buf, len + len1 + 2, tag);
            totlen += len + len1 + ID3v2_HEADER_SIZE + 3;
            av_free(buf);
        }
    }

    cur_pos = url_ftell(s->pb);
    url_fseek(s->pb, size_pos, SEEK_SET);
    id3v2_put_size(s, totlen);
    url_fseek(s->pb, cur_pos, SEEK_SET);

    return 0;
}

AVOutputFormat mp3_muxer = {
    "mp3",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 3"),
    "audio/x-mpeg",
    "mp3",
    0,
    CODEC_ID_MP3,
    CODEC_ID_NONE,
    mp3_write_header,
    mp3_write_packet,
    mp3_write_trailer,
    AVFMT_NOTIMESTAMPS,
    .metadata_conv = ff_id3v2_metadata_conv,
};
#endif
