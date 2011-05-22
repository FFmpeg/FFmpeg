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

#include <strings.h>
#include "avformat.h"
#include "id3v1.h"
#include "id3v2.h"
#include "rawenc.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
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
    avio_w8(s->pb, size >> 21 & 0x7f);
    avio_w8(s->pb, size >> 14 & 0x7f);
    avio_w8(s->pb, size >> 7  & 0x7f);
    avio_w8(s->pb, size       & 0x7f);
}

static int string_is_ascii(const uint8_t *str)
{
    while (*str && *str < 128) str++;
    return !*str;
}

/**
 * Write a text frame with one (normal frames) or two (TXXX frames) strings
 * according to encoding (only UTF-8 or UTF-16+BOM supported).
 * @return number of bytes written or a negative error code.
 */
static int id3v2_put_ttag(AVFormatContext *s, const char *str1, const char *str2,
                          uint32_t tag, enum ID3v2Encoding enc)
{
    int len;
    uint8_t *pb;
    int (*put)(AVIOContext*, const char*);
    AVIOContext *dyn_buf;
    if (avio_open_dyn_buf(&dyn_buf) < 0)
        return AVERROR(ENOMEM);

    /* check if the strings are ASCII-only and use UTF16 only if
     * they're not */
    if (enc == ID3v2_ENCODING_UTF16BOM && string_is_ascii(str1) &&
        (!str2 || string_is_ascii(str2)))
        enc = ID3v2_ENCODING_ISO8859;

    avio_w8(dyn_buf, enc);
    if (enc == ID3v2_ENCODING_UTF16BOM) {
        avio_wl16(dyn_buf, 0xFEFF);      /* BOM */
        put = avio_put_str16le;
    } else
        put = avio_put_str;

    put(dyn_buf, str1);
    if (str2)
        put(dyn_buf, str2);
    len = avio_close_dyn_buf(dyn_buf, &pb);

    avio_wb32(s->pb, tag);
    id3v2_put_size(s, len);
    avio_wb16(s->pb, 0);
    avio_write(s->pb, pb, len);

    av_freep(&pb);
    return len + ID3v2_HEADER_SIZE;
}

static int mp3_write_trailer(struct AVFormatContext *s)
{
    uint8_t buf[ID3v1_TAG_SIZE];

    /* write the id3v1 tag */
    if (id3v1_create_tag(s, buf) > 0) {
        avio_write(s->pb, buf, ID3v1_TAG_SIZE);
        avio_flush(s->pb);
    }
    return 0;
}

#if CONFIG_MP2_MUXER
AVOutputFormat ff_mp2_muxer = {
    "mp2",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 2"),
    "audio/x-mpeg",
    "mp2,m2a",
    0,
    CODEC_ID_MP2,
    CODEC_ID_NONE,
    NULL,
    ff_raw_write_packet,
    mp3_write_trailer,
};
#endif

#if CONFIG_MP3_MUXER
typedef struct MP3Context {
    const AVClass *class;
    int id3v2_version;
} MP3Context;

static const AVOption options[] = {
    { "id3v2_version", "Select ID3v2 version to write. Currently 3 and 4 are supported.",
      offsetof(MP3Context, id3v2_version), FF_OPT_TYPE_INT, {.dbl = 4}, 3, 4, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static const AVClass mp3_muxer_class = {
    .class_name     = "MP3 muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int id3v2_check_write_tag(AVFormatContext *s, AVDictionaryEntry *t, const char table[][4],
                                 enum ID3v2Encoding enc)
{
    uint32_t tag;
    int i;

    if (t->key[0] != 'T' || strlen(t->key) != 4)
        return -1;
    tag = AV_RB32(t->key);
    for (i = 0; *table[i]; i++)
        if (tag == AV_RB32(table[i]))
            return id3v2_put_ttag(s, t->value, NULL, tag, enc);
    return -1;
}

/**
 * Write an ID3v2 header at beginning of stream
 */

static int mp3_write_header(struct AVFormatContext *s)
{
    MP3Context  *mp3 = s->priv_data;
    AVDictionaryEntry *t = NULL;
    int totlen = 0, enc = mp3->id3v2_version == 3 ? ID3v2_ENCODING_UTF16BOM :
                                                    ID3v2_ENCODING_UTF8;
    int64_t size_pos, cur_pos;

    avio_wb32(s->pb, MKBETAG('I', 'D', '3', mp3->id3v2_version));
    avio_w8(s->pb, 0);
    avio_w8(s->pb, 0); /* flags */

    /* reserve space for size */
    size_pos = avio_tell(s->pb);
    avio_wb32(s->pb, 0);

    ff_metadata_conv(&s->metadata, ff_id3v2_34_metadata_conv, NULL);
    if (mp3->id3v2_version == 4)
        ff_metadata_conv(&s->metadata, ff_id3v2_4_metadata_conv, NULL);

    while ((t = av_dict_get(s->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        int ret;

        if ((ret = id3v2_check_write_tag(s, t, ff_id3v2_tags, enc)) > 0) {
            totlen += ret;
            continue;
        }
        if ((ret = id3v2_check_write_tag(s, t, mp3->id3v2_version == 3 ?
                                               ff_id3v2_3_tags : ff_id3v2_4_tags, enc)) > 0) {
            totlen += ret;
            continue;
        }

        /* unknown tag, write as TXXX frame */
        if ((ret = id3v2_put_ttag(s, t->key, t->value, MKBETAG('T', 'X', 'X', 'X'), enc)) < 0)
            return ret;
        totlen += ret;
    }

    cur_pos = avio_tell(s->pb);
    avio_seek(s->pb, size_pos, SEEK_SET);
    id3v2_put_size(s, totlen);
    avio_seek(s->pb, cur_pos, SEEK_SET);

    return 0;
}

AVOutputFormat ff_mp3_muxer = {
    "mp3",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 3"),
    "audio/x-mpeg",
    "mp3",
    sizeof(MP3Context),
    CODEC_ID_MP3,
    CODEC_ID_NONE,
    mp3_write_header,
    ff_raw_write_packet,
    mp3_write_trailer,
    AVFMT_NOTIMESTAMPS,
    .priv_class = &mp3_muxer_class,
};
#endif
