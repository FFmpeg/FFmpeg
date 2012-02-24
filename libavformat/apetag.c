/*
 * APE tag handling
 * Copyright (c) 2007 Benjamin Zores <ben@geexbox.org>
 *  based upon libdemac from Dave Chapman.
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

#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "avformat.h"
#include "apetag.h"

#define APE_TAG_VERSION               2000
#define APE_TAG_FOOTER_BYTES          32
#define APE_TAG_FLAG_CONTAINS_HEADER  (1 << 31)
#define APE_TAG_FLAG_IS_HEADER        (1 << 29)
#define APE_TAG_FLAG_IS_BINARY        (1 << 1)

static int ape_tag_read_field(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    uint8_t key[1024], *value;
    uint32_t size, flags;
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
    if (size >= UINT_MAX)
        return -1;
    if (flags & APE_TAG_FLAG_IS_BINARY) {
        uint8_t filename[1024];
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        avio_get_str(pb, INT_MAX, filename, sizeof(filename));
        st->codec->extradata = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!st->codec->extradata)
            return AVERROR(ENOMEM);
        if (avio_read(pb, st->codec->extradata, size) != size) {
            av_freep(&st->codec->extradata);
            return AVERROR(EIO);
        }
        st->codec->extradata_size = size;
        av_dict_set(&st->metadata, key, filename, 0);
        st->codec->codec_type = AVMEDIA_TYPE_ATTACHMENT;
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

void ff_ape_parse_tag(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int file_size = avio_size(pb);
    uint32_t val, fields, tag_bytes;
    uint8_t buf[8];
    int i;

    if (file_size < APE_TAG_FOOTER_BYTES)
        return;

    avio_seek(pb, file_size - APE_TAG_FOOTER_BYTES, SEEK_SET);

    avio_read(pb, buf, 8);     /* APETAGEX */
    if (strncmp(buf, "APETAGEX", 8)) {
        return;
    }

    val = avio_rl32(pb);       /* APE tag version */
    if (val > APE_TAG_VERSION) {
        av_log(s, AV_LOG_ERROR, "Unsupported tag version. (>=%d)\n", APE_TAG_VERSION);
        return;
    }

    tag_bytes = avio_rl32(pb); /* tag size */
    if (tag_bytes - APE_TAG_FOOTER_BYTES > (1024 * 1024 * 16)) {
        av_log(s, AV_LOG_ERROR, "Tag size is way too big\n");
        return;
    }

    fields = avio_rl32(pb);    /* number of fields */
    if (fields > 65536) {
        av_log(s, AV_LOG_ERROR, "Too many tag fields (%d)\n", fields);
        return;
    }

    val = avio_rl32(pb);       /* flags */
    if (val & APE_TAG_FLAG_IS_HEADER) {
        av_log(s, AV_LOG_ERROR, "APE Tag is a header\n");
        return;
    }

    avio_seek(pb, file_size - tag_bytes, SEEK_SET);

    for (i=0; i<fields; i++)
        if (ape_tag_read_field(s) < 0) break;
}
