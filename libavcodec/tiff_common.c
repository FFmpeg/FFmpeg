/*
 * TIFF Common Routines
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
 * TIFF Common Routines
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include "tiff_common.h"


int ff_tis_ifd(unsigned tag)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(ifd_tags); i++) {
        if (ifd_tags[i] == tag) {
            return i + 1;
        }
    }
    return 0;
}


unsigned ff_tget_short(GetByteContext *gb, int le)
{
    unsigned v = le ? bytestream2_get_le16(gb) : bytestream2_get_be16(gb);
    return v;
}


unsigned ff_tget_long(GetByteContext *gb, int le)
{
    unsigned v = le ? bytestream2_get_le32(gb) : bytestream2_get_be32(gb);
    return v;
}


double ff_tget_double(GetByteContext *gb, int le)
{
    av_alias64 i = { .u64 = le ? bytestream2_get_le64(gb) : bytestream2_get_be64(gb)};
    return i.f64;
}


unsigned ff_tget(GetByteContext *gb, int type, int le)
{
    switch (type) {
    case TIFF_BYTE:
        return bytestream2_get_byte(gb);
    case TIFF_SHORT:
        return ff_tget_short(gb, le);
    case TIFF_LONG:
        return ff_tget_long(gb, le);
    default:
        return UINT_MAX;
    }
}

static const char *auto_sep(int count, const char *sep, int i, int columns)
{
    if (sep)
        return i ? sep : "";
    if (i && i%columns) {
        return ", ";
    } else
        return columns < count ? "\n" : "";
}

int ff_tadd_rational_metadata(int count, const char *name, const char *sep,
                              GetByteContext *gb, int le, AVDictionary **metadata)
{
    AVBPrint bp;
    char *ap;
    int32_t nom, denom;
    int i;

    if (count >= INT_MAX / sizeof(int64_t) || count <= 0)
        return AVERROR_INVALIDDATA;
    if (bytestream2_get_bytes_left(gb) < count * sizeof(int64_t))
        return AVERROR_INVALIDDATA;

    av_bprint_init(&bp, 10 * count, AV_BPRINT_SIZE_UNLIMITED);

    for (i = 0; i < count; i++) {
        nom   = ff_tget_long(gb, le);
        denom = ff_tget_long(gb, le);
        av_bprintf(&bp, "%s%7i:%-7i", auto_sep(count, sep, i, 4), nom, denom);
    }

    if ((i = av_bprint_finalize(&bp, &ap))) {
        return i;
    }
    if (!ap) {
        return AVERROR(ENOMEM);
    }

    av_dict_set(metadata, name, ap, AV_DICT_DONT_STRDUP_VAL);

    return 0;
}


int ff_tadd_long_metadata(int count, const char *name, const char *sep,
                          GetByteContext *gb, int le, AVDictionary **metadata)
{
    AVBPrint bp;
    char *ap;
    int i;

    if (count >= INT_MAX / sizeof(int32_t) || count <= 0)
        return AVERROR_INVALIDDATA;
    if (bytestream2_get_bytes_left(gb) < count * sizeof(int32_t))
        return AVERROR_INVALIDDATA;

    av_bprint_init(&bp, 10 * count, AV_BPRINT_SIZE_UNLIMITED);

    for (i = 0; i < count; i++) {
        av_bprintf(&bp, "%s%7i", auto_sep(count, sep, i, 8), ff_tget_long(gb, le));
    }

    if ((i = av_bprint_finalize(&bp, &ap))) {
        return i;
    }
    if (!ap) {
        return AVERROR(ENOMEM);
    }

    av_dict_set(metadata, name, ap, AV_DICT_DONT_STRDUP_VAL);

    return 0;
}


int ff_tadd_doubles_metadata(int count, const char *name, const char *sep,
                             GetByteContext *gb, int le, AVDictionary **metadata)
{
    AVBPrint bp;
    char *ap;
    int i;

    if (count >= INT_MAX / sizeof(int64_t) || count <= 0)
        return AVERROR_INVALIDDATA;
    if (bytestream2_get_bytes_left(gb) < count * sizeof(int64_t))
        return AVERROR_INVALIDDATA;

    av_bprint_init(&bp, 10 * count, 100 * count);

    for (i = 0; i < count; i++) {
        av_bprintf(&bp, "%s%.15g", auto_sep(count, sep, i, 4), ff_tget_double(gb, le));
    }

    if ((i = av_bprint_finalize(&bp, &ap))) {
        return i;
    }
    if (!ap) {
        return AVERROR(ENOMEM);
    }

    av_dict_set(metadata, name, ap, AV_DICT_DONT_STRDUP_VAL);

    return 0;
}


int ff_tadd_shorts_metadata(int count, const char *name, const char *sep,
                            GetByteContext *gb, int le, AVDictionary **metadata)
{
    AVBPrint bp;
    char *ap;
    int i;

    if (count >= INT_MAX / sizeof(int16_t) || count <= 0)
        return AVERROR_INVALIDDATA;
    if (bytestream2_get_bytes_left(gb) < count * sizeof(int16_t))
        return AVERROR_INVALIDDATA;

    av_bprint_init(&bp, 10 * count, AV_BPRINT_SIZE_UNLIMITED);

    for (i = 0; i < count; i++) {
        av_bprintf(&bp, "%s%5i", auto_sep(count, sep, i, 8), ff_tget_short(gb, le));
    }

    if ((i = av_bprint_finalize(&bp, &ap))) {
        return i;
    }
    if (!ap) {
        return AVERROR(ENOMEM);
    }

    av_dict_set(metadata, name, ap, AV_DICT_DONT_STRDUP_VAL);

    return 0;
}


int ff_tadd_bytes_metadata(int count, const char *name, const char *sep,
                           GetByteContext *gb, int le, AVDictionary **metadata)
{
    AVBPrint bp;
    char *ap;
    int i;

    if (count >= INT_MAX / sizeof(int8_t) || count < 0)
        return AVERROR_INVALIDDATA;
    if (bytestream2_get_bytes_left(gb) < count * sizeof(int8_t))
        return AVERROR_INVALIDDATA;

    av_bprint_init(&bp, 10 * count, AV_BPRINT_SIZE_UNLIMITED);

    for (i = 0; i < count; i++) {
        av_bprintf(&bp, "%s%3i", auto_sep(count, sep, i, 16), bytestream2_get_byte(gb));
    }

    if ((i = av_bprint_finalize(&bp, &ap))) {
        return i;
    }
    if (!ap) {
        return AVERROR(ENOMEM);
    }

    av_dict_set(metadata, name, ap, AV_DICT_DONT_STRDUP_VAL);

    return 0;
}

int ff_tadd_string_metadata(int count, const char *name,
                            GetByteContext *gb, int le, AVDictionary **metadata)
{
    char *value;

    if (bytestream2_get_bytes_left(gb) < count || count < 0)
        return AVERROR_INVALIDDATA;

    value = av_malloc(count + 1);
    if (!value)
        return AVERROR(ENOMEM);

    bytestream2_get_bufferu(gb, value, count);
    value[count] = 0;

    av_dict_set(metadata, name, value, AV_DICT_DONT_STRDUP_VAL);
    return 0;
}


int ff_tdecode_header(GetByteContext *gb, int *le, int *ifd_offset)
{
    if (bytestream2_get_bytes_left(gb) < 8) {
        return AVERROR_INVALIDDATA;
    }

    *le = bytestream2_get_le16u(gb);
    if (*le == AV_RB16("II")) {
        *le = 1;
    } else if (*le == AV_RB16("MM")) {
        *le = 0;
    } else {
        return AVERROR_INVALIDDATA;
    }

    if (ff_tget_short(gb, *le) != 42) {
        return AVERROR_INVALIDDATA;
    }

    *ifd_offset = ff_tget_long(gb, *le);

    return 0;
}


int ff_tread_tag(GetByteContext *gb, int le, unsigned *tag, unsigned *type,
                 unsigned *count, int *next)
{
    int ifd_tag;
    int valid_type;

    *tag    = ff_tget_short(gb, le);
    *type   = ff_tget_short(gb, le);
    *count  = ff_tget_long (gb, le);

    ifd_tag    = ff_tis_ifd(*tag);
    valid_type = *type != 0 && *type < FF_ARRAY_ELEMS(type_sizes);

    *next = bytestream2_tell(gb) + 4;

    // check for valid type
    if (!valid_type) {
        return AVERROR_INVALIDDATA;
    }

    // seek to offset if this is an IFD-tag or
    // if count values do not fit into the offset value
    if (ifd_tag || (*count > 4 || !(type_sizes[*type] * (*count) <= 4 || *type == TIFF_STRING))) {
        bytestream2_seek(gb, ff_tget_long (gb, le), SEEK_SET);
    }

    return 0;
}
