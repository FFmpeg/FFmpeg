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
#include "libavutil/mem.h"
#include "avformat.h"
#include "avlanguage.h"
#include "avio.h"
#include "avio_internal.h"
#include "id3v2.h"
#include "mux.h"

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

static int id3v2_put_frame(ID3v2EncContext *id3, AVIOContext *avioc, AVIOContext *dyn_buf,
                           const uint32_t tag, const uint8_t flags)
{
    int len;
    uint8_t *pb;
    len = avio_get_dyn_buf(dyn_buf, &pb);

    avio_wb32(avioc, tag);
    /* ID3v2.3 frame size is not sync-safe */
    if (id3->version == 3)
        avio_wb32(avioc, len);
    else
        id3v2_put_size(avioc, len);
    avio_wb16(avioc, flags);
    avio_write(avioc, pb, len);

    id3->len += len + ID3v2_HEADER_SIZE;

    ffio_free_dyn_buf(&dyn_buf);
    return 1;
}

/**
 * Write a frame containing lang, descr and text such as COMM and USLT
 * according to encoding (only UTF-8 or UTF-16+BOM supported).
 * @return number of bytes written or a negative error code.
 */
static int id3v2_put_lang_descr_tag(
    ID3v2EncContext *id3, AVIOContext *avioc,
    const uint32_t tag, const uint8_t flags,
    const char *lang, const char *descr,
    const char *comment, enum ID3v2Encoding enc)
{
    int ret;
    AVIOContext *dyn_buf;
    if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
        return ret;

    /* check if the strings are ASCII-only and use UTF16 only if
     * they're not */
    if (enc == ID3v2_ENCODING_UTF16BOM && string_is_ascii(comment) &&
        (!descr || string_is_ascii(descr)))
        enc = ID3v2_ENCODING_ISO8859;

    avio_w8(dyn_buf, enc);
    avio_write(dyn_buf, lang, 3);
    // avio_put_str can handle NULL pointer but avio_put_str16le cannot.
    id3v2_encode_string(dyn_buf, descr ? descr : "", enc);
    id3v2_encode_string(dyn_buf, comment, enc);

    return id3v2_put_frame(id3, avioc, dyn_buf, tag, flags);
}

/**
 * Write a text frame with multiple text string according to encoding (only
 * UTF-8 or UTF-16+BOM supported).
 * @return number of bytes written or a negative error code.
 */
static int id3v2_put_text_tag(
    ID3v2EncContext *id3, AVIOContext *avioc,
    const uint32_t tag, const uint8_t flags,
    const char **strings, int len,
    enum ID3v2Encoding enc)
{
    int i, ret;
    AVIOContext *dyn_buf;
    if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
        return ret;

    /* check if the strings are ASCII-only and use UTF16 only if
     * they're not */
    if (enc == ID3v2_ENCODING_UTF16BOM) {
        enc = ID3v2_ENCODING_ISO8859;
        for (i = 0; i < len; i++) {
            if (!string_is_ascii((const uint8_t *)strings[i])) {
                enc = ID3v2_ENCODING_UTF16BOM;
                break;
            }
        }
    }

    avio_w8(dyn_buf, enc);

    for (i = 0; i < len; i++)
        id3v2_encode_string(dyn_buf, strings[i], enc);

    return id3v2_put_frame(id3, avioc, dyn_buf, tag, flags);
}

/**
 * Write a priv frame with owner and data. 'key' is the owner prepended with
 * ID3v2_PRIV_METADATA_PREFIX. 'data' is provided as a string. Any \xXX
 * (where 'X' is a valid hex digit) will be unescaped to the byte value.
 */
static int id3v2_put_priv(
    ID3v2EncContext *id3, AVIOContext *avioc,
    const uint8_t flags, const char *key,
    const char *data)
{
    int ret;
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

    return id3v2_put_frame(id3, avioc, dyn_buf, MKBETAG('P', 'R', 'I', 'V'),
                           flags);
}

struct LangDescrTagMap {
    const char * const key;
    uint32_t tag;
};

static const struct LangDescrTagMap id3v2_lang_descr_tags[] = {
    {.key = "comment", .tag = MKBETAG('C', 'O', 'M', 'M')},
    {.key = "lyrics", .tag = MKBETAG('U', 'S', 'L', 'T')},
    {.key = NULL, .tag = 0}
};

static int is_valid_lang(const char *s)
{
    return strlen(s) == 3 && ff_convert_lang_to(s, AV_LANG_ISO639_2_BIBL);
}

static int id3v2_check_write_lang_descr_tag(
    ID3v2EncContext *id3, AVIOContext *pb, const AVDictionaryEntry *t,
    enum ID3v2Encoding enc)
{
    int i, key_len;
    const char *key, *after_dash, *last_dash;
    uint32_t tag, ff_tag = 0;
    char lang[4] = "und";

    /* Raw 4CC key (e.g. "COMM"): only exact match, no suffix modifiers. */
    if (strlen(t->key) == 4)
        ff_tag = AV_RB32(t->key);

    for (i = 0; id3v2_lang_descr_tags[i].key; i++) {
        tag = id3v2_lang_descr_tags[i].tag;
        key = id3v2_lang_descr_tags[i].key;
        key_len = strlen(key);

        if (ff_tag == tag)
            return id3v2_put_lang_descr_tag(id3, pb, tag, 0,
                                            lang, NULL, t->value, enc);

        /* Generic key (e.g. "comment"): require exact match or '-' separator. */
        if (strncmp(t->key, key, key_len) ||
            (t->key[key_len] != '\0' && t->key[key_len] != '-'))
            continue;

        if (t->key[key_len] == '\0')
            return id3v2_put_lang_descr_tag(id3, pb, tag, 0,
                                            lang, NULL, t->value, enc);

        /* Has suffix(es) after '-'. */
        after_dash = t->key + key_len + 1;
        last_dash  = strrchr(after_dash, '-');
        const char *suffix = NULL;
        const char *middle = NULL;

        if (last_dash) {
            middle = after_dash;
            suffix = last_dash + 1;
        } else {
            suffix = after_dash;
        }

        if (!middle) {
            /* <tag>-<suffix>: valid lang → lang only; otherwise →
             * descriptor only. */
            const char *descr = NULL;
            if (is_valid_lang(suffix))
                memcpy(lang, suffix, 3);
            else
                descr = suffix;
            return id3v2_put_lang_descr_tag(id3, pb, tag, 0, lang,
                                            descr, t->value, enc);
        } else {
            /* <tag>-<middle>-<suffix>: valid lang suffix → lang+descriptor;
             * otherwise → full suffix after first '-' is the descriptor. */
            if (is_valid_lang(suffix) || *suffix == '\0') {
                /* Valid lang suffix or trailing dash (explicit empty lang):
                 * descriptor = everything before the last '-'. */
                size_t descr_len = last_dash - after_dash;
                char *descr = av_strndup(middle, descr_len);
                int ret;
                if (!descr)
                    return AVERROR(ENOMEM);
                if (*suffix)
                    memcpy(lang, suffix, 3);
                ret = id3v2_put_lang_descr_tag(id3, pb, tag, 0,
                                               lang, descr, t->value, enc);
                av_freep(&descr);
                return ret;
            }
            return id3v2_put_lang_descr_tag(id3, pb, tag, 0,
                                            lang, after_dash, t->value, enc);
        }
    }

    return 0;
}

static int id3v2_check_write_tag(ID3v2EncContext *id3, AVIOContext *pb, const AVDictionaryEntry *t,
                                 const char table[][4], enum ID3v2Encoding enc)
{
    uint32_t tag;
    int i;

    if (t->key[0] != 'T' || strlen(t->key) != 4)
        return 0;

    tag = AV_RB32(t->key);
    for (i = 0; *table[i]; i++)
        if (tag == AV_RB32(table[i])) {
            const char *strings[] = {t->value};
            return id3v2_put_text_tag(id3, pb, tag, 0, strings, 1, enc);
        }

    return 0;
}

static void id3v2_3_metadata_split_date(AVDictionary **pm)
{
    const AVDictionaryEntry *mtag = NULL;
    AVDictionary *dst = NULL;
    const char *key, *value;
    char year[5] = {0}, day_month[5] = {0};
    int i;

    while ((mtag = av_dict_iterate(*pm, mtag))) {
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
    const AVDictionaryEntry *t = NULL;
    int ret;

    ff_metadata_conv(metadata, ff_id3v2_34_metadata_conv, NULL);
    if (id3->version == 3)
        id3v2_3_metadata_split_date(metadata);
    else if (id3->version == 4)
        ff_metadata_conv(metadata, ff_id3v2_4_metadata_conv, NULL);

    while ((t = av_dict_iterate(*metadata, t))) {
        if ((ret = id3v2_check_write_lang_descr_tag(id3, pb, t, enc)) > 0)
            continue;

        if (ret < 0)
            return ret;

        if ((ret = id3v2_check_write_tag(id3, pb, t, ff_id3v2_tags, enc)) > 0)
            continue;

        if (ret < 0)
            return ret;

        if ((ret = id3v2_check_write_tag(id3, pb, t, id3->version == 3 ?
                                         ff_id3v2_3_tags : ff_id3v2_4_tags, enc)) > 0)
            continue;

        if (ret < 0)
            return ret;

        if ((ret = id3v2_put_priv(id3, pb, 0, t->key, t->value)) > 0)
            continue;

        if (ret < 0)
            return ret;

        /* unknown tag, write as TXXX frame */
        const char *strings[] = {t->key, t->value};
        if ((ret = id3v2_put_text_tag(id3, pb, MKBETAG('T', 'X', 'X', 'X'),
                                      0, strings, 2, enc)) < 0)
            return ret;
    }

    return 0;
}

static int write_ctoc(AVFormatContext *s, AVIOContext *pb,
                      ID3v2EncContext *id3, int enc)
{
    AVIOContext *dyn_bc;
    char name[123];
    int ret;

    if (s->nb_chapters == 0)
        return 0;

    if ((ret = avio_open_dyn_buf(&dyn_bc)) < 0)
        return ret;

    avio_put_str(dyn_bc, "toc");
    avio_w8(dyn_bc, 0x03);
    avio_w8(dyn_bc, s->nb_chapters);
    for (int i = 0; i < s->nb_chapters; i++) {
        snprintf(name, 122, "ch%d", i);
        avio_put_str(dyn_bc, name);
    }

    return id3v2_put_frame(id3, pb, dyn_bc,
                           MKBETAG('C', 'T', 'O', 'C'), 0);
}

static int write_chapter(AVFormatContext *s, AVIOContext *pb,
                         ID3v2EncContext *id3, int id, int enc)
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

    avio_wb32(pb, MKBETAG('C', 'H', 'A', 'P'));
    avio_wb32(pb, len);
    avio_wb16(pb, 0);
    avio_write(pb, dyn_buf, len);

fail:
    ffio_free_dyn_buf(&dyn_bc);

    return ret;
}

int ff_id3v2_write_metadata(AVFormatContext *s, AVIOContext *pb,
                            ID3v2EncContext *id3)
{
    int enc = id3->version == 3 ? ID3v2_ENCODING_UTF16BOM :
                                  ID3v2_ENCODING_UTF8;
    int i, ret;

    ff_standardize_creation_time(s);
    if ((ret = write_metadata(pb, &s->metadata, id3, enc)) < 0)
        return ret;

    if ((ret = write_ctoc(s, pb, id3, enc)) < 0)
        return ret;

    for (i = 0; i < s->nb_chapters; i++) {
        if ((ret = write_chapter(s, pb, id3, i, enc)) < 0)
            return ret;
    }

    return 0;
}

int ff_id3v2_write_apic(AVFormatContext *s, AVIOContext *pb,
                        ID3v2EncContext *id3, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    AVDictionaryEntry *e;

    AVIOContext *dyn_buf;
    const CodecMime *mime = ff_id3v2_mime_tags;
    const char  *mimetype = NULL, *desc = "";
    int enc = id3->version == 3 ? ID3v2_ENCODING_UTF16BOM :
                                  ID3v2_ENCODING_UTF8;
    int i, type = 0, ret;

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

    return id3v2_put_frame(id3, pb, dyn_buf,
                           MKBETAG('A', 'P', 'I', 'C'), 0);
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
    if ((ret = ff_id3v2_write_metadata(s, s->pb, &id3)) < 0)
        return ret;
    ff_id3v2_finish(&id3, s->pb, s->metadata_header_padding);

    return 0;
}
