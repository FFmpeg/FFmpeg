/*
 * EXIF metadata parser
 * Copyright (c) 2013 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 * EXIF metadata parser
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include "exif.h"
#include "tiff_common.h"


static const char *exif_get_tag_name(uint16_t id)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(tag_list); i++) {
        if (tag_list[i].id == id)
            return tag_list[i].name;
    }

    return NULL;
}


static int exif_add_metadata(void *logctx, int count, int type,
                             const char *name, const char *sep,
                             GetByteContext *gb, int le,
                             AVDictionary **metadata)
{
    switch(type) {
    case 0:
        av_log(logctx, AV_LOG_WARNING,
               "Invalid TIFF tag type 0 found for %s with size %d\n",
               name, count);
        return 0;
    case TIFF_DOUBLE   : return ff_tadd_doubles_metadata(count, name, sep, gb, le, metadata);
    case TIFF_SSHORT   : return ff_tadd_shorts_metadata(count, name, sep, gb, le, 1, metadata);
    case TIFF_SHORT    : return ff_tadd_shorts_metadata(count, name, sep, gb, le, 0, metadata);
    case TIFF_SBYTE    : return ff_tadd_bytes_metadata(count, name, sep, gb, le, 1, metadata);
    case TIFF_BYTE     :
    case TIFF_UNDEFINED: return ff_tadd_bytes_metadata(count, name, sep, gb, le, 0, metadata);
    case TIFF_STRING   : return ff_tadd_string_metadata(count, name, gb, le, metadata);
    case TIFF_SRATIONAL:
    case TIFF_RATIONAL : return ff_tadd_rational_metadata(count, name, sep, gb, le, metadata);
    case TIFF_SLONG    :
    case TIFF_LONG     : return ff_tadd_long_metadata(count, name, sep, gb, le, metadata);
    default:
        avpriv_request_sample(logctx, "TIFF tag type (%u)", type);
        return 0;
    };
}


static int exif_decode_tag(void *logctx, GetByteContext *gbytes, int le,
                           int depth, AVDictionary **metadata)
{
    int ret, cur_pos;
    unsigned id, count;
    enum TiffTypes type;

    if (depth > 2) {
        return 0;
    }

    ff_tread_tag(gbytes, le, &id, &type, &count, &cur_pos);

    if (!bytestream2_tell(gbytes)) {
        bytestream2_seek(gbytes, cur_pos, SEEK_SET);
        return 0;
    }

    // read count values and add it metadata
    // store metadata or proceed with next IFD
    ret = ff_tis_ifd(id);
    if (ret) {
        ret = ff_exif_decode_ifd(logctx, gbytes, le, depth + 1, metadata);
    } else {
        const char *name = exif_get_tag_name(id);
        char buf[7];

        if (!name) {
            name = buf;
            snprintf(buf, sizeof(buf), "0x%04X", id);
        }

        ret = exif_add_metadata(logctx, count, type, name, NULL,
                                gbytes, le, metadata);
    }

    bytestream2_seek(gbytes, cur_pos, SEEK_SET);

    return ret;
}


int ff_exif_decode_ifd(void *logctx, GetByteContext *gbytes,
                       int le, int depth, AVDictionary **metadata)
{
    int i, ret;
    int entries;

    entries = ff_tget_short(gbytes, le);

    if (bytestream2_get_bytes_left(gbytes) < entries * 12) {
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < entries; i++) {
        if ((ret = exif_decode_tag(logctx, gbytes, le, depth, metadata)) < 0) {
            return ret;
        }
    }

    // return next IDF offset or 0x000000000 or a value < 0 for failure
    return ff_tget_long(gbytes, le);
}

int avpriv_exif_decode_ifd(void *logctx, const uint8_t *buf, int size,
                           int le, int depth, AVDictionary **metadata)
{
    GetByteContext gb;

    bytestream2_init(&gb, buf, size);

    return ff_exif_decode_ifd(logctx, &gb, le, depth, metadata);
}
