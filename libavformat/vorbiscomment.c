/*
 * VorbisComment writer
 * Copyright (c) 2009 James Darnley
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

#include "avformat.h"
#include "metadata.h"
#include "vorbiscomment.h"
#include "libavcodec/bytestream.h"
#include "libavutil/dict.h"

/**
 * VorbisComment metadata conversion mapping.
 * from Ogg Vorbis I format specification: comment field and header specification
 * http://xiph.org/vorbis/doc/v-comment.html
 */
const AVMetadataConv ff_vorbiscomment_metadata_conv[] = {
    { "ALBUMARTIST", "album_artist"},
    { "TRACKNUMBER", "track"  },
    { "DISCNUMBER",  "disc"   },
    { "DESCRIPTION", "comment" },
    { 0 }
};

int64_t ff_vorbiscomment_length(AVDictionary *m, const char *vendor_string)
{
    int64_t len = 8;
    len += strlen(vendor_string);
    if (m) {
        AVDictionaryEntry *tag = NULL;
        while ((tag = av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            len += 4 +strlen(tag->key) + 1 + strlen(tag->value);
        }
    }
    return len;
}

int ff_vorbiscomment_write(uint8_t **p, AVDictionary **m,
                           const char *vendor_string)
{
    bytestream_put_le32(p, strlen(vendor_string));
    bytestream_put_buffer(p, vendor_string, strlen(vendor_string));
    if (*m) {
        int count = av_dict_count(*m);
        AVDictionaryEntry *tag = NULL;
        bytestream_put_le32(p, count);
        while ((tag = av_dict_get(*m, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            int64_t len1 = strlen(tag->key);
            int64_t len2 = strlen(tag->value);
            if (len1+1+len2 > UINT32_MAX)
                return AVERROR(EINVAL);
            bytestream_put_le32(p, len1+1+len2);
            bytestream_put_buffer(p, tag->key, len1);
            bytestream_put_byte(p, '=');
            bytestream_put_buffer(p, tag->value, len2);
        }
    } else
        bytestream_put_le32(p, 0);
    return 0;
}
