/*
 * Copyright (c) 2020 Jun Zhao<barryjzhao@tencent.com>
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

#include "dovi_meta.h"
#include "mem.h"

AVDOVIDecoderConfigurationRecord *av_dovi_alloc(size_t *size)
{
    AVDOVIDecoderConfigurationRecord *dovi =
        av_mallocz(sizeof(AVDOVIDecoderConfigurationRecord));
    if (!dovi)
        return NULL;

     if (size)
        *size = sizeof(*dovi);

    return dovi;
}

typedef struct AVDOVIMetadataInternal {
    AVDOVIMetadata metadata;
    AVDOVIRpuDataHeader header;
    AVDOVIDataMapping mapping;
    AVDOVIColorMetadata color;
} AVDOVIMetadataInternal;

AVDOVIMetadata *av_dovi_metadata_alloc(size_t *size)
{
    AVDOVIMetadataInternal *dovi = av_mallocz(sizeof(AVDOVIMetadataInternal));
    if (!dovi)
        return NULL;

    if (size)
        *size = sizeof(*dovi);

    dovi->metadata = (struct AVDOVIMetadata) {
        .header_offset  = offsetof(AVDOVIMetadataInternal, header),
        .mapping_offset = offsetof(AVDOVIMetadataInternal, mapping),
        .color_offset   = offsetof(AVDOVIMetadataInternal, color),
    };

    return &dovi->metadata;
}
