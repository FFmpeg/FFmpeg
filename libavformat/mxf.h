/*
 * MXF
 * Copyright (c) 2006 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
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
#ifndef AVFORMAT_MXF_H
#define AVFORMAT_MXF_H

#include <string.h>
#include "avformat.h"
#include "libavcodec/bytestream.h"

typedef uint8_t UID[16];

enum MXFMetadataSetType {
    AnyType,
    MaterialPackage,
    SourcePackage,
    SourceClip,
    TimecodeComponent,
    Sequence,
    MultipleDescriptor,
    Descriptor,
    Track,
    CryptoContext,
    Preface,
    Identification,
    ContentStorage,
    SubDescriptor,
    IndexTableSegment,
    EssenceContainerData,
    TypeBottom,// add metadata type before this
};

typedef struct {
    UID key;
    int64_t offset;
    uint64_t length;
} KLVPacket;

typedef struct {
    UID uid;
    unsigned matching_len;
    enum CodecID id;
} MXFCodecUL;

typedef struct {
    UID uid;
    enum CodecType type;
} MXFDataDefinitionUL;

extern const MXFDataDefinitionUL ff_mxf_data_definition_uls[];
extern const MXFCodecUL ff_mxf_codec_uls[];

#ifdef DEBUG
#define PRINT_KEY(pc, s, x) dprintf(pc, "%s %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", s, \
                             (x)[0], (x)[1], (x)[2], (x)[3], (x)[4], (x)[5], (x)[6], (x)[7], (x)[8], (x)[9], (x)[10], (x)[11], (x)[12], (x)[13], (x)[14], (x)[15])
#else
#define PRINT_KEY(pc, s, x)
#endif

#endif /* AVFORMAT_MXF_H */
