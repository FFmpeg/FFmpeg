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

#include <strings.h>
#include "avformat.h"
#include "id3v1.h"
#include "id3v2.h"
#include "libavutil/intreadwrite.h"

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

    ff_metadata_conv(&s->metadata, ff_id3v2_metadata_conv, NULL);
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
};
#endif
