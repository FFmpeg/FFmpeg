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

#include "avformat.h"
#include "libavcodec/avcodec.h"
#include <stdint.h>

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

enum MXFFrameLayout {
    FullFrame = 0,
    SeparateFields,
    OneField,
    MixedFields,
    SegmentedFrame,
};

typedef struct KLVPacket {
    UID key;
    int64_t offset;
    uint64_t length;
} KLVPacket;

typedef struct MXFCodecUL {
    UID uid;
    unsigned matching_len;
    int id;
} MXFCodecUL;

typedef struct {
    struct AVRational time_base;
    int samples_per_frame[6];
} MXFSamplesPerFrame;

extern const MXFCodecUL ff_mxf_data_definition_uls[];
extern const MXFCodecUL ff_mxf_codec_uls[];
extern const MXFCodecUL ff_mxf_pixel_format_uls[];

int ff_mxf_decode_pixel_layout(const char pixel_layout[16], enum AVPixelFormat *pix_fmt);
const MXFSamplesPerFrame *ff_mxf_get_samples_per_frame(AVFormatContext *s, AVRational time_base);

#define PRIxUID                             \
    "%02x.%02x.%02x.%02x."                  \
    "%02x.%02x.%02x.%02x."                  \
    "%02x.%02x.%02x.%02x."                  \
    "%02x.%02x.%02x.%02x"

#define UID_ARG(x) \
    (x)[0],  (x)[1],  (x)[2],  (x)[3],      \
    (x)[4],  (x)[5],  (x)[6],  (x)[7],      \
    (x)[8],  (x)[9],  (x)[10], (x)[11],     \
    (x)[12], (x)[13], (x)[14], (x)[15]      \

#ifdef DEBUG
#define PRINT_KEY(pc, s, x)                         \
    av_log(pc, AV_LOG_VERBOSE,                      \
           "%s "                                    \
           "0x%02x,0x%02x,0x%02x,0x%02x,"           \
           "0x%02x,0x%02x,0x%02x,0x%02x,"           \
           "0x%02x,0x%02x,0x%02x,0x%02x,"           \
           "0x%02x,0x%02x,0x%02x,0x%02x ",          \
            s,                                      \
            (x)[0],  (x)[1],  (x)[2],  (x)[3],      \
            (x)[4],  (x)[5],  (x)[6],  (x)[7],      \
            (x)[8],  (x)[9],  (x)[10], (x)[11],     \
            (x)[12], (x)[13], (x)[14], (x)[15]);    \
    av_log(pc, AV_LOG_INFO,                         \
           "%s "                                    \
           "%02x.%02x.%02x.%02x."                   \
           "%02x.%02x.%02x.%02x."                   \
           "%02x.%02x.%02x.%02x."                   \
           "%02x.%02x.%02x.%02x\n",                 \
            s,                                      \
            (x)[0],  (x)[1],  (x)[2],  (x)[3],      \
            (x)[4],  (x)[5],  (x)[6],  (x)[7],      \
            (x)[8],  (x)[9],  (x)[10], (x)[11],     \
            (x)[12], (x)[13], (x)[14], (x)[15])
#else
#define PRINT_KEY(pc, s, x)
#endif

#endif /* AVFORMAT_MXF_H */
