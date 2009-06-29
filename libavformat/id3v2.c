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
    int len, dstlen = sizeof(dst) - 1;
    unsigned genre;

    dst[0] = 0;
    if (taglen < 1)
        return;

    taglen--; /* account for encoding type byte */

    switch (get_byte(s->pb)) { /* encoding type */

    case 0:  /* ISO-8859-1 (0 - 255 maps directly into unicode) */
        q = dst;
        while (taglen--) {
            uint8_t tmp;
            PUT_UTF8(get_byte(s->pb), tmp, if (q - dst < dstlen - 1) *q++ = tmp;)
        }
        *q = '\0';
        break;

    case 3:  /* UTF-8 */
        len = FFMIN(taglen, dstlen - 1);
        get_buffer(s->pb, dst, len);
        dst[len] = 0;
        break;
    }

    if (!strcmp(key, "genre")
        && (sscanf(dst, "(%d)", &genre) == 1 || sscanf(dst, "%d", &genre) == 1)
        && genre <= ID3v1_GENRE_MAX)
        av_strlcpy(dst, ff_id3v1_genre_str[genre], sizeof(dst));

    if (*dst)
        av_metadata_set(&s->metadata, key, dst);
}

void ff_id3v2_parse(AVFormatContext *s, int len, uint8_t version, uint8_t flags)
{
    int isv34, tlen;
    uint32_t tag;
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
            tag  = get_be32(s->pb);
            if(version==3){
                tlen = get_be32(s->pb);
            }else
                tlen = get_size(s->pb, 4);
            get_be16(s->pb); /* flags */
        } else {
            tag  = get_be24(s->pb);
            tlen = get_be24(s->pb);
        }
        len -= taghdrlen + tlen;

        if (len < 0)
            break;

        next = url_ftell(s->pb) + tlen;

        switch (tag) {
        case MKBETAG('T', 'I', 'T', '2'):
        case MKBETAG(0,   'T', 'T', '2'):
            read_ttag(s, tlen, "title");
            break;
        case MKBETAG('T', 'P', 'E', '1'):
        case MKBETAG(0,   'T', 'P', '1'):
            read_ttag(s, tlen, "author");
            break;
        case MKBETAG('T', 'A', 'L', 'B'):
        case MKBETAG(0,   'T', 'A', 'L'):
            read_ttag(s, tlen, "album");
            break;
        case MKBETAG('T', 'C', 'O', 'N'):
        case MKBETAG(0,   'T', 'C', 'O'):
            read_ttag(s, tlen, "genre");
            break;
        case MKBETAG('T', 'C', 'O', 'P'):
        case MKBETAG(0,   'T', 'C', 'R'):
            read_ttag(s, tlen, "copyright");
            break;
        case MKBETAG('T', 'R', 'C', 'K'):
        case MKBETAG(0,   'T', 'R', 'K'):
            read_ttag(s, tlen, "track");
            break;
        case 0:
            /* padding, skip to end */
            url_fskip(s->pb, len);
            len = 0;
            continue;
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
