/*
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

/**
 * @file
 * ID3v2 header parser
 *
 * Specifications available at:
 * http://id3.org/Developer_Information
 */

#include "id3v2.h"
#include "id3v1.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "avio_internal.h"

const AVMetadataConv ff_id3v2_34_metadata_conv[] = {
    { "TALB", "album"},
    { "TCOM", "composer"},
    { "TCON", "genre"},
    { "TCOP", "copyright"},
    { "TENC", "encoded_by"},
    { "TIT2", "title"},
    { "TLAN", "language"},
    { "TPE1", "artist"},
    { "TPE2", "album_artist"},
    { "TPE3", "performer"},
    { "TPOS", "disc"},
    { "TPUB", "publisher"},
    { "TRCK", "track"},
    { "TSSE", "encoder"},
    { 0 }
};

const AVMetadataConv ff_id3v2_4_metadata_conv[] = {
    { "TDRL", "date"},
    { "TDRC", "date"},
    { "TDEN", "creation_time"},
    { "TSOA", "album-sort"},
    { "TSOP", "artist-sort"},
    { "TSOT", "title-sort"},
    { 0 }
};

static const AVMetadataConv id3v2_2_metadata_conv[] = {
    { "TAL",  "album"},
    { "TCO",  "genre"},
    { "TT2",  "title"},
    { "TEN",  "encoded_by"},
    { "TP1",  "artist"},
    { "TP2",  "album_artist"},
    { "TP3",  "performer"},
    { "TRK",  "track"},
    { 0 }
};


const char ff_id3v2_tags[][4] = {
   "TALB", "TBPM", "TCOM", "TCON", "TCOP", "TDLY", "TENC", "TEXT",
   "TFLT", "TIT1", "TIT2", "TIT3", "TKEY", "TLAN", "TLEN", "TMED",
   "TOAL", "TOFN", "TOLY", "TOPE", "TOWN", "TPE1", "TPE2", "TPE3",
   "TPE4", "TPOS", "TPUB", "TRCK", "TRSN", "TRSO", "TSRC", "TSSE",
   { 0 },
};

const char ff_id3v2_4_tags[][4] = {
   "TDEN", "TDOR", "TDRC", "TDRL", "TDTG", "TIPL", "TMCL", "TMOO",
   "TPRO", "TSOA", "TSOP", "TSOT", "TSST",
   { 0 },
};

const char ff_id3v2_3_tags[][4] = {
   "TDAT", "TIME", "TORY", "TRDA", "TSIZ", "TYER",
   { 0 },
};

int ff_id3v2_match(const uint8_t *buf, const char * magic)
{
    return  buf[0]         == magic[0] &&
            buf[1]         == magic[1] &&
            buf[2]         == magic[2] &&
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

static unsigned int get_size(AVIOContext *s, int len)
{
    int v = 0;
    while (len--)
        v = (v << 7) + (avio_r8(s) & 0x7F);
    return v;
}

/**
 * Free GEOB type extra metadata.
 */
static void free_geobtag(void *obj)
{
    ID3v2ExtraMetaGEOB *geob = obj;
    av_free(geob->mime_type);
    av_free(geob->file_name);
    av_free(geob->description);
    av_free(geob->data);
    av_free(geob);
}

/**
 * Decode characters to UTF-8 according to encoding type. The decoded buffer is
 * always null terminated. Stop reading when either *maxread bytes are read from
 * pb or U+0000 character is found.
 *
 * @param dst Pointer where the address of the buffer with the decoded bytes is
 * stored. Buffer must be freed by caller.
 * @param maxread Pointer to maximum number of characters to read from the
 * AVIOContext. After execution the value is decremented by the number of bytes
 * actually read.
 * @returns 0 if no error occured, dst is uninitialized on error
 */
static int decode_str(AVFormatContext *s, AVIOContext *pb, int encoding,
                      uint8_t **dst, int *maxread)
{
    int ret;
    uint8_t tmp;
    uint32_t ch = 1;
    int left = *maxread;
    unsigned int (*get)(AVIOContext*) = avio_rb16;
    AVIOContext *dynbuf;

    if ((ret = avio_open_dyn_buf(&dynbuf)) < 0) {
        av_log(s, AV_LOG_ERROR, "Error opening memory stream\n");
        return ret;
    }

    switch (encoding) {

    case ID3v2_ENCODING_ISO8859:
        while (left && ch) {
            ch = avio_r8(pb);
            PUT_UTF8(ch, tmp, avio_w8(dynbuf, tmp);)
            left--;
        }
        break;

    case ID3v2_ENCODING_UTF16BOM:
        if ((left -= 2) < 0) {
            av_log(s, AV_LOG_ERROR, "Cannot read BOM value, input too short\n");
            avio_close_dyn_buf(dynbuf, dst);
            av_freep(dst);
            return AVERROR_INVALIDDATA;
        }
        switch (avio_rb16(pb)) {
        case 0xfffe:
            get = avio_rl16;
        case 0xfeff:
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Incorrect BOM value\n");
            avio_close_dyn_buf(dynbuf, dst);
            av_freep(dst);
            *maxread = left;
            return AVERROR_INVALIDDATA;
        }
        // fall-through

    case ID3v2_ENCODING_UTF16BE:
        while ((left > 1) && ch) {
            GET_UTF16(ch, ((left -= 2) >= 0 ? get(pb) : 0), break;)
            PUT_UTF8(ch, tmp, avio_w8(dynbuf, tmp);)
        }
        if (left < 0)
            left += 2; /* did not read last char from pb */
        break;

    case ID3v2_ENCODING_UTF8:
        while (left && ch) {
            ch = avio_r8(pb);
            avio_w8(dynbuf, ch);
            left--;
        }
        break;
    default:
        av_log(s, AV_LOG_WARNING, "Unknown encoding\n");
    }

    if (ch)
        avio_w8(dynbuf, 0);

    avio_close_dyn_buf(dynbuf, dst);
    *maxread = left;

    return 0;
}

/**
 * Parse a text tag.
 */
static void read_ttag(AVFormatContext *s, AVIOContext *pb, int taglen, const char *key)
{
    uint8_t *dst;
    int encoding, dict_flags = AV_DICT_DONT_OVERWRITE;
    unsigned genre;

    if (taglen < 1)
        return;

    encoding = avio_r8(pb);
    taglen--; /* account for encoding type byte */

    if (decode_str(s, pb, encoding, &dst, &taglen) < 0) {
        av_log(s, AV_LOG_ERROR, "Error reading frame %s, skipped\n", key);
        return;
    }

    if (!(strcmp(key, "TCON") && strcmp(key, "TCO"))
        && (sscanf(dst, "(%d)", &genre) == 1 || sscanf(dst, "%d", &genre) == 1)
        && genre <= ID3v1_GENRE_MAX) {
        av_freep(&dst);
        dst = ff_id3v1_genre_str[genre];
    } else if (!(strcmp(key, "TXXX") && strcmp(key, "TXX"))) {
        /* dst now contains the key, need to get value */
        key = dst;
        if (decode_str(s, pb, encoding, &dst, &taglen) < 0) {
            av_log(s, AV_LOG_ERROR, "Error reading frame %s, skipped\n", key);
            av_freep(&key);
            return;
        }
        dict_flags |= AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_STRDUP_KEY;
    }
    else if (*dst)
        dict_flags |= AV_DICT_DONT_STRDUP_VAL;

    if (dst)
        av_dict_set(&s->metadata, key, dst, dict_flags);
}

/**
 * Parse GEOB tag into a ID3v2ExtraMetaGEOB struct.
 */
static void read_geobtag(AVFormatContext *s, AVIOContext *pb, int taglen, char *tag, ID3v2ExtraMeta **extra_meta)
{
    ID3v2ExtraMetaGEOB *geob_data = NULL;
    ID3v2ExtraMeta *new_extra = NULL;
    char encoding;
    unsigned int len;

    if (taglen < 1)
        return;

    geob_data = av_mallocz(sizeof(ID3v2ExtraMetaGEOB));
    if (!geob_data) {
        av_log(s, AV_LOG_ERROR, "Failed to alloc %zu bytes\n", sizeof(ID3v2ExtraMetaGEOB));
        return;
    }

    new_extra = av_mallocz(sizeof(ID3v2ExtraMeta));
    if (!new_extra) {
        av_log(s, AV_LOG_ERROR, "Failed to alloc %zu bytes\n", sizeof(ID3v2ExtraMeta));
        goto fail;
    }

    /* read encoding type byte */
    encoding = avio_r8(pb);
    taglen--;

    /* read MIME type (always ISO-8859) */
    if (decode_str(s, pb, ID3v2_ENCODING_ISO8859, &geob_data->mime_type, &taglen) < 0
        || taglen <= 0)
        goto fail;

    /* read file name */
    if (decode_str(s, pb, encoding, &geob_data->file_name, &taglen) < 0
        || taglen <= 0)
        goto fail;

    /* read content description */
    if (decode_str(s, pb, encoding, &geob_data->description, &taglen) < 0
        || taglen < 0)
        goto fail;

    if (taglen) {
        /* save encapsulated binary data */
        geob_data->data = av_malloc(taglen);
        if (!geob_data->data) {
            av_log(s, AV_LOG_ERROR, "Failed to alloc %d bytes\n", taglen);
            goto fail;
        }
        if ((len = avio_read(pb, geob_data->data, taglen)) < taglen)
            av_log(s, AV_LOG_WARNING, "Error reading GEOB frame, data truncated.\n");
        geob_data->datasize = len;
    } else {
        geob_data->data = NULL;
        geob_data->datasize = 0;
    }

    /* add data to the list */
    new_extra->tag = "GEOB";
    new_extra->data = geob_data;
    new_extra->next = *extra_meta;
    *extra_meta = new_extra;

    return;

fail:
    av_log(s, AV_LOG_ERROR, "Error reading frame %s, skipped\n", tag);
    free_geobtag(geob_data);
    av_free(new_extra);
    return;
}

static int is_number(const char *str)
{
    while (*str >= '0' && *str <= '9') str++;
    return !*str;
}

static AVDictionaryEntry* get_date_tag(AVDictionary *m, const char *tag)
{
    AVDictionaryEntry *t;
    if ((t = av_dict_get(m, tag, NULL, AV_DICT_MATCH_CASE)) &&
        strlen(t->value) == 4 && is_number(t->value))
        return t;
    return NULL;
}

static void merge_date(AVDictionary **m)
{
    AVDictionaryEntry *t;
    char date[17] = {0};      // YYYY-MM-DD hh:mm

    if (!(t = get_date_tag(*m, "TYER")) &&
        !(t = get_date_tag(*m, "TYE")))
        return;
    av_strlcpy(date, t->value, 5);
    av_dict_set(m, "TYER", NULL, 0);
    av_dict_set(m, "TYE",  NULL, 0);

    if (!(t = get_date_tag(*m, "TDAT")) &&
        !(t = get_date_tag(*m, "TDA")))
        goto finish;
    snprintf(date + 4, sizeof(date) - 4, "-%.2s-%.2s", t->value + 2, t->value);
    av_dict_set(m, "TDAT", NULL, 0);
    av_dict_set(m, "TDA",  NULL, 0);

    if (!(t = get_date_tag(*m, "TIME")) &&
        !(t = get_date_tag(*m, "TIM")))
        goto finish;
    snprintf(date + 10, sizeof(date) - 10, " %.2s:%.2s", t->value, t->value + 2);
    av_dict_set(m, "TIME", NULL, 0);
    av_dict_set(m, "TIM",  NULL, 0);

finish:
    if (date[0])
        av_dict_set(m, "date", date, 0);
}

typedef struct ID3v2EMFunc {
    const char *tag3;
    const char *tag4;
    void (*read)(AVFormatContext*, AVIOContext*, int, char*, ID3v2ExtraMeta **);
    void (*free)(void *obj);
} ID3v2EMFunc;

static const ID3v2EMFunc id3v2_extra_meta_funcs[] = {
    { "GEO", "GEOB", read_geobtag, free_geobtag },
    { NULL }
};

/**
 * Get the corresponding ID3v2EMFunc struct for a tag.
 * @param isv34 Determines if v2.2 or v2.3/4 strings are used
 * @return A pointer to the ID3v2EMFunc struct if found, NULL otherwise.
 */
static const ID3v2EMFunc *get_extra_meta_func(const char *tag, int isv34)
{
    int i = 0;
    while (id3v2_extra_meta_funcs[i].tag3) {
        if (!memcmp(tag,
                    (isv34 ? id3v2_extra_meta_funcs[i].tag4 :
                             id3v2_extra_meta_funcs[i].tag3),
                    (isv34 ? 4 : 3)))
            return &id3v2_extra_meta_funcs[i];
        i++;
    }
    return &id3v2_extra_meta_funcs[i];
}

static void ff_id3v2_parse(AVFormatContext *s, int len, uint8_t version, uint8_t flags, ID3v2ExtraMeta **extra_meta)
{
    int isv34, unsync;
    unsigned tlen;
    char tag[5];
    int64_t next, end = avio_tell(s->pb) + len;
    int taghdrlen;
    const char *reason = NULL;
    AVIOContext pb;
    AVIOContext *pbx;
    unsigned char *buffer = NULL;
    int buffer_size = 0;
    const ID3v2EMFunc *extra_func;

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

    unsync = flags & 0x80;

    if (isv34 && flags & 0x40) /* Extended header present, just skip over it */
        avio_skip(s->pb, get_size(s->pb, 4));

    while (len >= taghdrlen) {
        unsigned int tflags = 0;
        int tunsync = 0;

        if (isv34) {
            avio_read(s->pb, tag, 4);
            tag[4] = 0;
            if(version==3){
                tlen = avio_rb32(s->pb);
            }else
                tlen = get_size(s->pb, 4);
            tflags = avio_rb16(s->pb);
            tunsync = tflags & ID3v2_FLAG_UNSYNCH;
        } else {
            avio_read(s->pb, tag, 3);
            tag[3] = 0;
            tlen = avio_rb24(s->pb);
        }
        if (tlen > (1<<28))
            break;
        len -= taghdrlen + tlen;

        if (len < 0)
            break;

        next = avio_tell(s->pb) + tlen;

        if (!tlen) {
            if (tag[0])
                av_log(s, AV_LOG_DEBUG, "Invalid empty frame %s, skipping.\n", tag);
            continue;
        }

        if (tflags & ID3v2_FLAG_DATALEN) {
            if (tlen < 4)
                break;
            avio_rb32(s->pb);
            tlen -= 4;
        }

        if (tflags & (ID3v2_FLAG_ENCRYPTION | ID3v2_FLAG_COMPRESSION)) {
            av_log(s, AV_LOG_WARNING, "Skipping encrypted/compressed ID3v2 frame %s.\n", tag);
            avio_skip(s->pb, tlen);
        /* check for text tag or supported special meta tag */
        } else if (tag[0] == 'T' || (extra_meta && (extra_func = get_extra_meta_func(tag, isv34)))) {
            if (unsync || tunsync) {
                int i, j;
                av_fast_malloc(&buffer, &buffer_size, tlen);
                if (!buffer) {
                    av_log(s, AV_LOG_ERROR, "Failed to alloc %d bytes\n", tlen);
                    goto seek;
                }
                for (i = 0, j = 0; i < tlen; i++, j++) {
                    buffer[j] = avio_r8(s->pb);
                    if (j > 0 && !buffer[j] && buffer[j - 1] == 0xff) {
                        /* Unsynchronised byte, skip it */
                        j--;
                    }
                }
                ffio_init_context(&pb, buffer, j, 0, NULL, NULL, NULL, NULL);
                tlen = j;
                pbx = &pb; // read from sync buffer
            } else {
                pbx = s->pb; // read straight from input
            }
            if (tag[0] == 'T')
                /* parse text tag */
                read_ttag(s, pbx, tlen, tag);
            else
                /* parse special meta tag */
                extra_func->read(s, pbx, tlen, tag, extra_meta);
        }
        else if (!tag[0]) {
            if (tag[1])
                av_log(s, AV_LOG_WARNING, "invalid frame id, assuming padding");
            avio_skip(s->pb, tlen);
            break;
        }
        /* Skip to end of tag */
seek:
        avio_seek(s->pb, next, SEEK_SET);
    }

    if (version == 4 && flags & 0x10) /* Footer preset, always 10 bytes, skip over it */
        end += 10;

  error:
    if (reason)
        av_log(s, AV_LOG_INFO, "ID3v2.%d tag skipped, cannot handle %s\n", version, reason);
    avio_seek(s->pb, end, SEEK_SET);
    av_free(buffer);
    return;
}

void ff_id3v2_read_all(AVFormatContext *s, const char *magic, ID3v2ExtraMeta **extra_meta)
{
    int len, ret;
    uint8_t buf[ID3v2_HEADER_SIZE];
    int     found_header;
    int64_t off;

    do {
        /* save the current offset in case there's nothing to read/skip */
        off = avio_tell(s->pb);
        ret = avio_read(s->pb, buf, ID3v2_HEADER_SIZE);
        if (ret != ID3v2_HEADER_SIZE)
            break;
            found_header = ff_id3v2_match(buf, magic);
            if (found_header) {
            /* parse ID3v2 header */
            len = ((buf[6] & 0x7f) << 21) |
                  ((buf[7] & 0x7f) << 14) |
                  ((buf[8] & 0x7f) << 7) |
                   (buf[9] & 0x7f);
            ff_id3v2_parse(s, len, buf[3], buf[5], extra_meta);
        } else {
            avio_seek(s->pb, off, SEEK_SET);
        }
    } while (found_header);
    ff_metadata_conv(&s->metadata, NULL, ff_id3v2_34_metadata_conv);
    ff_metadata_conv(&s->metadata, NULL, id3v2_2_metadata_conv);
    ff_metadata_conv(&s->metadata, NULL, ff_id3v2_4_metadata_conv);
    merge_date(&s->metadata);
}

void ff_id3v2_read(AVFormatContext *s, const char *magic)
{
    ff_id3v2_read_all(s, magic, NULL);
}

void ff_id3v2_free_extra_meta(ID3v2ExtraMeta **extra_meta)
{
    ID3v2ExtraMeta *current = *extra_meta, *next;
    const ID3v2EMFunc *extra_func;

    while (current) {
        if ((extra_func = get_extra_meta_func(current->tag, 1)))
            extra_func->free(current->data);
        next = current->next;
        av_freep(&current);
        current = next;
    }
}
