/*
 * APE tag handling
 * Copyright (c) 2007 Benjamin Zores <ben@geexbox.org>
 *  based upon libdemac from Dave Chapman.
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

#include <inttypes.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "avformat.h"
#include "avio_internal.h"
#include "apetag.h"
#include "internal.h"

#define APE_TAG_FLAG_CONTAINS_HEADER  (1 << 31)
#define APE_TAG_FLAG_CONTAINS_FOOTER  (1 << 30)
#define APE_TAG_FLAG_IS_HEADER        (1 << 29)
#define APE_TAG_FLAG_IS_BINARY        (1 << 1)

static int ape_tag_read_field(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    uint8_t key[1024], *value;
    int64_t size, flags;
    int i, c;

    size = avio_rl32(pb);  /* field size */
    flags = avio_rl32(pb); /* field flags */
    for (i = 0; i < sizeof(key) - 1; i++) {
        c = avio_r8(pb);
        if (c < 0x20 || c > 0x7E)
            break;
        else
            key[i] = c;
    }
    key[i] = 0;
    if (c != 0) {
        av_log(s, AV_LOG_WARNING, "Invalid APE tag key '%s'.\n", key);
        return -1;
    }
    if (size > INT32_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        av_log(s, AV_LOG_ERROR, "APE tag size too large.\n");
        return AVERROR_INVALIDDATA;
    }
    if (flags & APE_TAG_FLAG_IS_BINARY) {
        uint8_t filename[1024];
        enum AVCodecID id;
        int ret;
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        ret = avio_get_str(pb, size, filename, sizeof(filename));
        if (ret < 0)
            return ret;
        if (size <= ret) {
            av_log(s, AV_LOG_WARNING, "Skipping binary tag '%s'.\n", key);
            return 0;
        }
        size -= ret;

        av_dict_set(&st->metadata, key, filename, 0);

        if ((id = ff_guess_image2_codec(filename)) != AV_CODEC_ID_NONE) {
            AVPacket pkt;
            int ret;

            ret = av_get_packet(s->pb, &pkt, size);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Error reading cover art.\n");
                return ret;
            }

            st->disposition      |= AV_DISPOSITION_ATTACHED_PIC;
            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codecpar->codec_id   = id;

            st->attached_pic              = pkt;
            st->attached_pic.stream_index = st->index;
            st->attached_pic.flags       |= AV_PKT_FLAG_KEY;
        } else {
            if (ff_get_extradata(s, st->codecpar, s->pb, size) < 0)
                return AVERROR(ENOMEM);
            st->codecpar->codec_type = AVMEDIA_TYPE_ATTACHMENT;
        }
    } else {
        value = av_malloc(size+1);
        if (!value)
            return AVERROR(ENOMEM);
        c = avio_read(pb, value, size);
        if (c < 0) {
            av_free(value);
            return c;
        }
        value[c] = 0;
        av_dict_set(&s->metadata, key, value, AV_DICT_DONT_STRDUP_VAL);
    }
    return 0;
}

int64_t ff_ape_parse_tag(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int64_t file_size = avio_size(pb);
    uint32_t val, fields, tag_bytes;
    uint8_t buf[8];
    int64_t tag_start;
    int i;

    if (file_size < APE_TAG_FOOTER_BYTES)
        return 0;

    avio_seek(pb, file_size - APE_TAG_FOOTER_BYTES, SEEK_SET);

    avio_read(pb, buf, 8);     /* APETAGEX */
    if (strncmp(buf, APE_TAG_PREAMBLE, 8)) {
        return 0;
    }

    val = avio_rl32(pb);       /* APE tag version */
    if (val > APE_TAG_VERSION) {
        av_log(s, AV_LOG_ERROR, "Unsupported tag version. (>=%d)\n", APE_TAG_VERSION);
        return 0;
    }

    tag_bytes = avio_rl32(pb); /* tag size */
    if (tag_bytes - APE_TAG_FOOTER_BYTES > (1024 * 1024 * 16)) {
        av_log(s, AV_LOG_ERROR, "Tag size is way too big\n");
        return 0;
    }

    if (tag_bytes > file_size - APE_TAG_FOOTER_BYTES) {
        av_log(s, AV_LOG_ERROR, "Invalid tag size %"PRIu32".\n", tag_bytes);
        return 0;
    }
    tag_start = file_size - tag_bytes - APE_TAG_FOOTER_BYTES;

    fields = avio_rl32(pb);    /* number of fields */
    if (fields > 65536) {
        av_log(s, AV_LOG_ERROR, "Too many tag fields (%"PRIu32")\n", fields);
        return 0;
    }

    val = avio_rl32(pb);       /* flags */
    if (val & APE_TAG_FLAG_IS_HEADER) {
        av_log(s, AV_LOG_ERROR, "APE Tag is a header\n");
        return 0;
    }

    avio_seek(pb, file_size - tag_bytes, SEEK_SET);

    for (i=0; i<fields; i++)
        if (ape_tag_read_field(s) < 0) break;

    return tag_start;
}

static int string_is_ascii(const uint8_t *str)
{
    while (*str && *str >= 0x20 && *str <= 0x7e ) str++;
    return !*str;
}

int ff_ape_write_tag(AVFormatContext *s)
{
    AVDictionaryEntry *e = NULL;
    int size, ret, count = 0;
    AVIOContext *dyn_bc = NULL;
    uint8_t *dyn_buf = NULL;

    if ((ret = avio_open_dyn_buf(&dyn_bc)) < 0)
        goto end;

    // flags
    avio_wl32(dyn_bc, APE_TAG_FLAG_CONTAINS_HEADER | APE_TAG_FLAG_CONTAINS_FOOTER |
                     APE_TAG_FLAG_IS_HEADER);
    ffio_fill(dyn_bc, 0, 8);             // reserved

    ff_standardize_creation_time(s);
    while ((e = av_dict_get(s->metadata, "", e, AV_DICT_IGNORE_SUFFIX))) {
        int val_len;

        if (!string_is_ascii(e->key)) {
            av_log(s, AV_LOG_WARNING, "Non ASCII keys are not allowed\n");
            continue;
        }

        val_len = strlen(e->value);
        avio_wl32(dyn_bc, val_len);            // value length
        avio_wl32(dyn_bc, 0);                  // item flags
        avio_put_str(dyn_bc, e->key);          // key
        avio_write(dyn_bc, e->value, val_len); // value
        count++;
    }
    if (!count)
        goto end;

    size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    if (size <= 0)
        goto end;
    size += 20;

    // header
    avio_write(s->pb, "APETAGEX", 8);   // id
    avio_wl32(s->pb, APE_TAG_VERSION);  // version
    avio_wl32(s->pb, size);
    avio_wl32(s->pb, count);

    avio_write(s->pb, dyn_buf, size - 20);

    // footer
    avio_write(s->pb, "APETAGEX", 8);   // id
    avio_wl32(s->pb, APE_TAG_VERSION);  // version
    avio_wl32(s->pb, size);             // size
    avio_wl32(s->pb, count);            // tag count

    // flags
    avio_wl32(s->pb, APE_TAG_FLAG_CONTAINS_HEADER | APE_TAG_FLAG_CONTAINS_FOOTER);
    ffio_fill(s->pb, 0, 8);             // reserved

end:
    if (dyn_bc && !dyn_buf)
        avio_close_dyn_buf(dyn_bc, &dyn_buf);
    av_freep(&dyn_buf);

    return ret;
}
