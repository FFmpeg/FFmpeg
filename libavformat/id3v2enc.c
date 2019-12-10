/*
 * ID3v2 header writer
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

#include <stdint.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "id3v2.h"

static void id3v2_put_size(AVIOContext *pb, int size)
{
    avio_w8(pb, size >> 21 & 0x7f);
    avio_w8(pb, size >> 14 & 0x7f);
    avio_w8(pb, size >> 7  & 0x7f);
    avio_w8(pb, size       & 0x7f);
}

static int string_is_ascii(const uint8_t *str)
{
    while (*str && *str < 128) str++;
    return !*str;
}

static void id3v2_encode_string(AVIOContext *pb, const uint8_t *str,
                               enum ID3v2Encoding enc)
{
    int (*put)(AVIOContext*, const char*);

    if (enc == ID3v2_ENCODING_UTF16BOM) {
        avio_wl16(pb, 0xFEFF);      /* BOM */
        put = avio_put_str16le;
    } else
        put = avio_put_str;

    put(pb, str);
}

/**
 * Write a text frame with one (normal frames) or two (TXXX frames) strings
 * according to encoding (only UTF-8 or UTF-16+BOM supported).
 * @return number of bytes written or a negative error code.
 */
static int id3v2_put_ttag(ID3v2EncContext *id3, AVIOContext *avioc, const char *str1, const char *str2,
                          uint32_t tag, enum ID3v2Encoding enc)
{
    int len, ret;
    uint8_t *pb;
    AVIOContext *dyn_buf;
    if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
        return ret;

    /* check if the strings are ASCII-only and use UTF16 only if
     * they're not */
    if (enc == ID3v2_ENCODING_UTF16BOM && string_is_ascii(str1) &&
        (!str2 || string_is_ascii(str2)))
        enc = ID3v2_ENCODING_ISO8859;

    avio_w8(dyn_buf, enc);
    id3v2_encode_string(dyn_buf, str1, enc);
    if (str2)
        id3v2_encode_string(dyn_buf, str2, enc);
    len = avio_get_dyn_buf(dyn_buf, &pb);

    avio_wb32(avioc, tag);
    /* ID3v2.3 frame size is not sync-safe */
    if (id3->version == 3)
        avio_wb32(avioc, len);
    else
        id3v2_put_size(avioc, len);
    avio_wb16(avioc, 0);
    avio_write(avioc, pb, len);

    ffio_free_dyn_buf(&dyn_buf);
    return len + ID3v2_HEADER_SIZE;
}

/**
 * Write a priv frame with owner and data. 'key' is the owner prepended with
 * ID3v2_PRIV_METADATA_PREFIX. 'data' is provided as a string. Any \xXX
 * (where 'X' is a valid hex digit) will be unescaped to the byte value.
 */
static int id3v2_put_priv(ID3v2EncContext *id3, AVIOContext *avioc, const char *key, const char *data)
{
    int len, ret;
    uint8_t *pb;
    AVIOContext *dyn_buf;

    if (!av_strstart(key, ID3v2_PRIV_METADATA_PREFIX, &key)) {
        return 0;
    }

    if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
        return ret;

    // owner + null byte.
    avio_write(dyn_buf, key, strlen(key) + 1);

    while (*data) {
        if (av_strstart(data, "\\x", &data)) {
            if (data[0] && data[1] && av_isxdigit(data[0]) && av_isxdigit(data[1])) {
                char digits[] = {data[0], data[1], 0};
                avio_w8(dyn_buf, strtol(digits, NULL, 16));
                data += 2;
            } else {
                ffio_free_dyn_buf(&dyn_buf);
                av_log(avioc, AV_LOG_ERROR, "Invalid escape '\\x%.2s' in metadata tag '"
                       ID3v2_PRIV_METADATA_PREFIX "%s'.\n", data, key);
                return AVERROR(EINVAL);
            }
        } else {
            avio_write(dyn_buf, data++, 1);
        }
    }

    len = avio_get_dyn_buf(dyn_buf, &pb);

    avio_wb32(avioc, MKBETAG('P', 'R', 'I', 'V'));
    if (id3->version == 3)
        avio_wb32(avioc, len);
    else
        id3v2_put_size(avioc, len);
    avio_wb16(avioc, 0);
    avio_write(avioc, pb, len);

    ffio_free_dyn_buf(&dyn_buf);

    return len + ID3v2_HEADER_SIZE;
}

static int id3v2_check_write_tag(ID3v2EncContext *id3, AVIOContext *pb, AVDictionaryEntry *t,
                                 const char table[][4], enum ID3v2Encoding enc)
{
    uint32_t tag;
    int i;

    if (t->key[0] != 'T' || strlen(t->key) != 4)
        return -1;
    tag = AV_RB32(t->key);
    for (i = 0; *table[i]; i++)
        if (tag == AV_RB32(table[i]))
            return id3v2_put_ttag(id3, pb, t->value, NULL, tag, enc);
    return -1;
}

static void id3v2_3_metadata_split_date(AVDictionary **pm)
{
    AVDictionaryEntry *mtag = NULL;
    AVDictionary *dst = NULL;
    const char *key, *value;
    char year[5] = {0}, day_month[5] = {0};
    int i;

    while ((mtag = av_dict_get(*pm, "", mtag, AV_DICT_IGNORE_SUFFIX))) {
        key = mtag->key;
        if (!av_strcasecmp(key, "date")) {
            /* split date tag using "YYYY-MM-DD" format into year and month/day segments */
            value = mtag->value;
            i = 0;
            while (value[i] >= '0' && value[i] <= '9') i++;
            if (value[i] == '\0' || value[i] == '-') {
                av_strlcpy(year, value, sizeof(year));
                av_dict_set(&dst, "TYER", year, 0);

                if (value[i] == '-' &&
                    value[i+1] >= '0' && value[i+1] <= '1' &&
                    value[i+2] >= '0' && value[i+2] <= '9' &&
                    value[i+3] == '-' &&
                    value[i+4] >= '0' && value[i+4] <= '3' &&
                    value[i+5] >= '0' && value[i+5] <= '9' &&
                    (value[i+6] == '\0' || value[i+6] == ' ')) {
                    snprintf(day_month, sizeof(day_month), "%.2s%.2s", value + i + 4, value + i + 1);
                    av_dict_set(&dst, "TDAT", day_month, 0);
                }
            } else
                av_dict_set(&dst, key, value, 0);
        } else
            av_dict_set(&dst, key, mtag->value, 0);
    }
    av_dict_free(pm);
    *pm = dst;
}

void ff_id3v2_start(ID3v2EncContext *id3, AVIOContext *pb, int id3v2_version,
                    const char *magic)
{
    id3->version = id3v2_version;

    avio_wb32(pb, MKBETAG(magic[0], magic[1], magic[2], id3v2_version));
    avio_w8(pb, 0);
    avio_w8(pb, 0); /* flags */

    /* reserve space for size */
    id3->size_pos = avio_tell(pb);
    avio_wb32(pb, 0);
}

static int write_metadata(AVIOContext *pb, AVDictionary **metadata,
                          ID3v2EncContext *id3, int enc)
{
    AVDictionaryEntry *t = NULL;
    int ret;

    ff_metadata_conv(metadata, ff_id3v2_34_metadata_conv, NULL);
    if (id3->version == 3)
        id3v2_3_metadata_split_date(metadata);
    else if (id3->version == 4)
        ff_metadata_conv(metadata, ff_id3v2_4_metadata_conv, NULL);

    while ((t = av_dict_get(*metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        if ((ret = id3v2_check_write_tag(id3, pb, t, ff_id3v2_tags, enc)) > 0) {
            id3->len += ret;
            continue;
        }
        if ((ret = id3v2_check_write_tag(id3, pb, t, id3->version == 3 ?
                                         ff_id3v2_3_tags : ff_id3v2_4_tags, enc)) > 0) {
            id3->len += ret;
            continue;
        }

        if ((ret = id3v2_put_priv(id3, pb, t->key, t->value)) > 0) {
            id3->len += ret;
            continue;
        } else if (ret < 0) {
            return ret;
        }

        /* unknown tag, write as TXXX frame */
        if ((ret = id3v2_put_ttag(id3, pb, t->key, t->value, MKBETAG('T', 'X', 'X', 'X'), enc)) < 0)
            return ret;
        id3->len += ret;
    }

    return 0;
}

static int write_ctoc(AVFormatContext *s, ID3v2EncContext *id3, int enc)
{
    uint8_t *dyn_buf;
    AVIOContext *dyn_bc;
    char name[123];
    int len, ret;

    if (s->nb_chapters == 0)
        return 0;

    if ((ret = avio_open_dyn_buf(&dyn_bc)) < 0)
        return ret;

    id3->len += avio_put_str(dyn_bc, "toc");
    avio_w8(dyn_bc, 0x03);
    avio_w8(dyn_bc, s->nb_chapters);
    for (int i = 0; i < s->nb_chapters; i++) {
        snprintf(name, 122, "ch%d", i);
        id3->len += avio_put_str(dyn_bc, name);
    }
    len = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    id3->len += 16 + ID3v2_HEADER_SIZE;

    avio_wb32(s->pb, MKBETAG('C', 'T', 'O', 'C'));
    avio_wb32(s->pb, len);
    avio_wb16(s->pb, 0);
    avio_write(s->pb, dyn_buf, len);

    ffio_free_dyn_buf(&dyn_bc);

    return ret;
}

static int write_chapter(AVFormatContext *s, ID3v2EncContext *id3, int id, int enc)
{
    const AVRational time_base = {1, 1000};
    AVChapter *ch = s->chapters[id];
    uint8_t *dyn_buf;
    AVIOContext *dyn_bc;
    char name[123];
    int len, start, end, ret;

    if ((ret = avio_open_dyn_buf(&dyn_bc)) < 0)
        return ret;

    start = av_rescale_q(ch->start, ch->time_base, time_base);
    end   = av_rescale_q(ch->end,   ch->time_base, time_base);

    snprintf(name, 122, "ch%d", id);
    id3->len += avio_put_str(dyn_bc, name);
    avio_wb32(dyn_bc, start);
    avio_wb32(dyn_bc, end);
    avio_wb32(dyn_bc, 0xFFFFFFFFu);
    avio_wb32(dyn_bc, 0xFFFFFFFFu);

    if ((ret = write_metadata(dyn_bc, &ch->metadata, id3, enc)) < 0)
        goto fail;

    len = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    id3->len += 16 + ID3v2_HEADER_SIZE;

    avio_wb32(s->pb, MKBETAG('C', 'H', 'A', 'P'));
    avio_wb32(s->pb, len);
    avio_wb16(s->pb, 0);
    avio_write(s->pb, dyn_buf, len);

fail:
    ffio_free_dyn_buf(&dyn_bc);

    return ret;
}

int ff_id3v2_write_metadata(AVFormatContext *s, ID3v2EncContext *id3)
{
    int enc = id3->version == 3 ? ID3v2_ENCODING_UTF16BOM :
                                  ID3v2_ENCODING_UTF8;
    int i, ret;

    ff_standardize_creation_time(s);
    if ((ret = write_metadata(s->pb, &s->metadata, id3, enc)) < 0)
        return ret;

    if ((ret = write_ctoc(s, id3, enc)) < 0)
        return ret;

    for (i = 0; i < s->nb_chapters; i++) {
        if ((ret = write_chapter(s, id3, i, enc)) < 0)
            return ret;
    }

    return 0;
}

int ff_id3v2_write_apic(AVFormatContext *s, ID3v2EncContext *id3, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    AVDictionaryEntry *e;

    AVIOContext *dyn_buf;
    uint8_t     *buf;
    const CodecMime *mime = ff_id3v2_mime_tags;
    const char  *mimetype = NULL, *desc = "";
    int enc = id3->version == 3 ? ID3v2_ENCODING_UTF16BOM :
                                  ID3v2_ENCODING_UTF8;
    int i, len, type = 0, ret;

    /* get the mimetype*/
    while (mime->id != AV_CODEC_ID_NONE) {
        if (mime->id == st->codecpar->codec_id) {
            mimetype = mime->str;
            break;
        }
        mime++;
    }
    if (!mimetype) {
        av_log(s, AV_LOG_ERROR, "No mimetype is known for stream %d, cannot "
               "write an attached picture.\n", st->index);
        return AVERROR(EINVAL);
    }

    /* get the picture type */
    e = av_dict_get(st->metadata, "comment", NULL, 0);
    for (i = 0; e && i < FF_ARRAY_ELEMS(ff_id3v2_picture_types); i++) {
        if (!av_strcasecmp(e->value, ff_id3v2_picture_types[i])) {
            type = i;
            break;
        }
    }

    /* get the description */
    if ((e = av_dict_get(st->metadata, "title", NULL, 0)))
        desc = e->value;

    /* use UTF16 only for non-ASCII strings */
    if (enc == ID3v2_ENCODING_UTF16BOM && string_is_ascii(desc))
        enc = ID3v2_ENCODING_ISO8859;

    /* start writing */
    if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
        return ret;

    avio_w8(dyn_buf, enc);
    avio_put_str(dyn_buf, mimetype);
    avio_w8(dyn_buf, type);
    id3v2_encode_string(dyn_buf, desc, enc);
    avio_write(dyn_buf, pkt->data, pkt->size);
    len = avio_get_dyn_buf(dyn_buf, &buf);

    avio_wb32(s->pb, MKBETAG('A', 'P', 'I', 'C'));
    if (id3->version == 3)
        avio_wb32(s->pb, len);
    else
        id3v2_put_size(s->pb, len);
    avio_wb16(s->pb, 0);
    avio_write(s->pb, buf, len);
    ffio_free_dyn_buf(&dyn_buf);

    id3->len += len + ID3v2_HEADER_SIZE;

    return 0;
}

void ff_id3v2_finish(ID3v2EncContext *id3, AVIOContext *pb,
                     int padding_bytes)
{
    int64_t cur_pos;

    if (padding_bytes < 0)
        padding_bytes = 10;

    /* The ID3v2.3 specification states that 28 bits are used to represent the
     * size of the whole tag.  Therefore the current size of the tag needs to be
     * subtracted from the upper limit of 2^28-1 to clip the value correctly. */
    /* The minimum of 10 is an arbitrary amount of padding at the end of the tag
     * to fix cover art display with some software such as iTunes, Traktor,
     * Serato, Torq. */
    padding_bytes = av_clip(padding_bytes, 10, 268435455 - id3->len);
    ffio_fill(pb, 0, padding_bytes);
    id3->len += padding_bytes;

    cur_pos = avio_tell(pb);
    avio_seek(pb, id3->size_pos, SEEK_SET);
    id3v2_put_size(pb, id3->len);
    avio_seek(pb, cur_pos, SEEK_SET);
}

int ff_id3v2_write_simple(struct AVFormatContext *s, int id3v2_version,
                          const char *magic)
{
    ID3v2EncContext id3 = { 0 };
    int ret;

    ff_id3v2_start(&id3, s->pb, id3v2_version, magic);
    if ((ret = ff_id3v2_write_metadata(s, &id3)) < 0)
        return ret;
    ff_id3v2_finish(&id3, s->pb, s->metadata_header_padding);

    return 0;
}
