/*
 * APE tag writer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/dict.h"
#include "avio_internal.h"
#include "avformat.h"
#include "apetag.h"

static int string_is_ascii(const uint8_t *str)
{
    while (*str && *str >= 0x20 && *str <= 0x7e ) str++;
    return !*str;
}

void ff_ape_write(AVFormatContext *s)
{
    int64_t tag_bytes;
    AVDictionaryEntry *t = NULL;
    AVIOContext *pb = s->pb;
    int tags = 0, vlen;

    tag_bytes = avio_tell(s->pb);
    while ((t = av_dict_get(s->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        if (!string_is_ascii(t->key)) {
            av_log(s, AV_LOG_WARNING, "Non ASCII keys are not allowed\n");
            continue;
        }

        vlen = strlen(t->value);
        avio_wl32(pb, vlen + 1);
        avio_wl32(pb, 0); // flags
        avio_put_str(pb, t->key);
        avio_put_str(pb, t->value);
        tags++;
    }
    tag_bytes = avio_tell(s->pb) - tag_bytes;

    if (!tags)
        return;

    avio_write(pb, APE_TAG_PREAMBLE, 8);
    avio_wl32(pb, APE_TAG_VERSION);
    avio_wl32(pb, tag_bytes + APE_TAG_FOOTER_BYTES);
    avio_wl32(pb, tags); // item count
    avio_wl32(pb, 0); // global flags
    ffio_fill(pb, 0, 8); // reserved
}
