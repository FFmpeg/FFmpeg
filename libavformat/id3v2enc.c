/*
 * ID3v2 header writer
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

#include <stdint.h>

#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio.h"
#include "id3v2.h"

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

int ff_id3v2_write(struct AVFormatContext *s, int id3v2_version,
                   const char *magic)
{
    int64_t size_pos, cur_pos;
    AVDictionaryEntry *t = NULL;

    int totlen = 0, enc = id3v2_version == 3 ? ID3v2_ENCODING_UTF16BOM :
                                               ID3v2_ENCODING_UTF8;


    avio_wb32(s->pb, MKBETAG(magic[0], magic[1], magic[2], id3v2_version));
    avio_w8(s->pb, 0);
    avio_w8(s->pb, 0); /* flags */

    /* reserve space for size */
    size_pos = avio_tell(s->pb);
    avio_wb32(s->pb, 0);

    ff_metadata_conv(&s->metadata, ff_id3v2_34_metadata_conv, NULL);
    if (id3v2_version == 4)
        ff_metadata_conv(&s->metadata, ff_id3v2_4_metadata_conv, NULL);

    while ((t = av_dict_get(s->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        int ret;

        if ((ret = id3v2_check_write_tag(s, t, ff_id3v2_tags, enc)) > 0) {
            totlen += ret;
            continue;
        }
        if ((ret = id3v2_check_write_tag(s, t, id3v2_version == 3 ?
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
