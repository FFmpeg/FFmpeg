/*
 * ID3v2 header parser
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

#include "id3v2.h"
#include "id3v1.h"
#include "libavutil/avstring.h"

int ff_id3v2_match(const uint8_t *buf)
{
    return  buf[0]         ==  'I' &&
            buf[1]         ==  'D' &&
            buf[2]         ==  '3' &&
            buf[3]         != 0xff &&
            buf[4]         != 0xff &&
           (buf[6] & 0x80) ==    0 &&
           (buf[7] & 0x80) ==    0 &&
           (buf[8] & 0x80) ==    0 &&
           (buf[9] & 0x80) ==    0;
}

int ff_id3v2_tag_len(const uint8_t * buf)
{
    int len = ((buf[6] & 0x7f) << 21) +
              ((buf[7] & 0x7f) << 14) +
              ((buf[8] & 0x7f) << 7) +
               (buf[9] & 0x7f) +
              ID3v2_HEADER_SIZE;
    if (buf[5] & 0x10)
        len += ID3v2_HEADER_SIZE;
    return len;
}

void ff_id3v2_read(AVFormatContext *s)
{
    int len, ret;
    uint8_t buf[ID3v2_HEADER_SIZE];

    ret = get_buffer(s->pb, buf, ID3v2_HEADER_SIZE);
    if (ret != ID3v2_HEADER_SIZE)
        return;
    if (ff_id3v2_match(buf)) {
        /* parse ID3v2 header */
        len = ((buf[6] & 0x7f) << 21) |
            ((buf[7] & 0x7f) << 14) |
            ((buf[8] & 0x7f) << 7) |
            (buf[9] & 0x7f);
        ff_id3v2_parse(s, len, buf[3], buf[5]);
    } else {
        url_fseek(s->pb, 0, SEEK_SET);
    }
}

static unsigned int get_size(ByteIOContext *s, int len)
{
    int v = 0;
    while (len--)
        v = (v << 7) + (get_byte(s) & 0x7F);
    return v;
}

static void read_ttag(AVFormatContext *s, int taglen, const char *key)
{
    char *q, dst[512];
    const char *val = NULL;
    int len, dstlen = sizeof(dst) - 1;
    unsigned genre;
    unsigned int (*get)(ByteIOContext*) = get_be16;

    dst[0] = 0;
    if (taglen < 1)
        return;

    taglen--; /* account for encoding type byte */

    switch (get_byte(s->pb)) { /* encoding type */

    case 0:  /* ISO-8859-1 (0 - 255 maps directly into unicode) */
        q = dst;
        while (taglen-- && q - dst < dstlen - 7) {
            uint8_t tmp;
            PUT_UTF8(get_byte(s->pb), tmp, *q++ = tmp;)
        }
        *q = 0;
        break;

    case 1:  /* UTF-16 with BOM */
        taglen -= 2;
        switch (get_be16(s->pb)) {
        case 0xfffe:
            get = get_le16;
        case 0xfeff:
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Incorrect BOM value in tag %s.\n", key);
            return;
        }
        // fall-through

    case 2:  /* UTF-16BE without BOM */
        q = dst;
        while (taglen > 1 && q - dst < dstlen - 7) {
            uint32_t ch;
            uint8_t tmp;

            GET_UTF16(ch, ((taglen -= 2) >= 0 ? get(s->pb) : 0), break;)
            PUT_UTF8(ch, tmp, *q++ = tmp;)
        }
        *q = 0;
        break;

    case 3:  /* UTF-8 */
        len = FFMIN(taglen, dstlen);
        get_buffer(s->pb, dst, len);
        dst[len] = 0;
        break;
    default:
        av_log(s, AV_LOG_WARNING, "Unknown encoding in tag %s\n.", key);
    }

    if (!(strcmp(key, "TCON") && strcmp(key, "TCO"))
        && (sscanf(dst, "(%d)", &genre) == 1 || sscanf(dst, "%d", &genre) == 1)
        && genre <= ID3v1_GENRE_MAX)
        val = ff_id3v1_genre_str[genre];
    else if (!(strcmp(key, "TXXX") && strcmp(key, "TXX"))) {
        /* dst now contains two 0-terminated strings */
        dst[dstlen] = 0;
        len = strlen(dst);
        key = dst;
        val = dst + FFMIN(len + 1, dstlen);
    }
    else if (*dst)
        val = dst;

    if (val)
        av_metadata_set2(&s->metadata, key, val, 0);
}

void ff_id3v2_parse(AVFormatContext *s, int len, uint8_t version, uint8_t flags)
{
    int isv34, tlen;
    char tag[5];
    int64_t next;
    int taghdrlen;
    const char *reason;

    switch (version) {
    case 2:
        if (flags & 0x40) {
            reason = "compression";
            goto error;
        }
        isv34 = 0;
        taghdrlen = 6;
        break;

    case 3:
    case 4:
        isv34 = 1;
        taghdrlen = 10;
        break;

    default:
        reason = "version";
        goto error;
    }

    if (flags & 0x80) {
        reason = "unsynchronization";
        goto error;
    }

    if (isv34 && flags & 0x40) /* Extended header present, just skip over it */
        url_fskip(s->pb, get_size(s->pb, 4));

    while (len >= taghdrlen) {
        if (isv34) {
            get_buffer(s->pb, tag, 4);
            tag[4] = 0;
            if(version==3){
                tlen = get_be32(s->pb);
            }else
                tlen = get_size(s->pb, 4);
            get_be16(s->pb); /* flags */
        } else {
            get_buffer(s->pb, tag, 3);
            tag[3] = 0;
            tlen = get_be24(s->pb);
        }
        len -= taghdrlen + tlen;

        if (len < 0)
            break;

        next = url_ftell(s->pb) + tlen;

        if (tag[0] == 'T')
            read_ttag(s, tlen, tag);
        else if (!tag[0]) {
            if (tag[1])
                av_log(s, AV_LOG_WARNING, "invalid frame id, assuming padding");
            url_fskip(s->pb, len);
            break;
        }
        /* Skip to end of tag */
        url_fseek(s->pb, next, SEEK_SET);
    }

    if (version == 4 && flags & 0x10) /* Footer preset, always 10 bytes, skip over it */
        url_fskip(s->pb, 10);
    return;

  error:
    av_log(s, AV_LOG_INFO, "ID3v2.%d tag skipped, cannot handle %s\n", version, reason);
    url_fskip(s->pb, len);
}

const AVMetadataConv ff_id3v2_metadata_conv[] = {
    { "TALB", "album"},
    { "TAL",  "album"},
    { "TCOM", "composer"},
    { "TCON", "genre"},
    { "TCO",  "genre"},
    { "TCOP", "copyright"},
    { "TDRL", "date"},
    { "TDRC", "date"},
    { "TENC", "encoded_by"},
    { "TEN",  "encoded_by"},
    { "TIT2", "title"},
    { "TT2",  "title"},
    { "TLAN", "language"},
    { "TPE1", "artist"},
    { "TP1",  "artist"},
    { "TPE2", "album_artist"},
    { "TP2",  "album_artist"},
    { "TPE3", "performer"},
    { "TP3",  "performer"},
    { "TPOS", "disc"},
    { "TPUB", "publisher"},
    { "TRCK", "track"},
    { "TRK",  "track"},
    { "TSOA", "album-sort"},
    { "TSOP", "artist-sort"},
    { "TSOT", "title-sort"},
    { "TSSE", "encoder"},
    { 0 }
};

const char ff_id3v2_tags[][4] = {
   "TALB", "TBPM", "TCOM", "TCON", "TCOP", "TDEN", "TDLY", "TDOR", "TDRC",
   "TDRL", "TDTG", "TENC", "TEXT", "TFLT", "TIPL", "TIT1", "TIT2", "TIT3",
   "TKEY", "TLAN", "TLEN", "TMCL", "TMED", "TMOO", "TOAL", "TOFN", "TOLY",
   "TOPE", "TOWN", "TPE1", "TPE2", "TPE3", "TPE4", "TPOS", "TPRO", "TPUB",
   "TRCK", "TRSN", "TRSO", "TSOA", "TSOP", "TSOT", "TSRC", "TSSE", "TSST",
   { 0 },
};
