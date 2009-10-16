/*
 * RIFF codec tags
 * copyright (c) 2000 Fabrice Bellard
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
 * @file libavformat/riff.h
 * internal header for RIFF based (de)muxers
 * do NOT include this in end user applications
 */

#ifndef AVFORMAT_RIFF_H
#define AVFORMAT_RIFF_H

#include "libavcodec/avcodec.h"
#include "avio.h"

int64_t ff_start_tag(ByteIOContext *pb, const char *tag);
void ff_end_tag(ByteIOContext *pb, int64_t start);

typedef struct AVCodecTag {
    enum CodecID id;
    unsigned int tag;
} AVCodecTag;

void ff_put_bmp_header(ByteIOContext *pb, AVCodecContext *enc, const AVCodecTag *tags, int for_asf);
int ff_put_wav_header(ByteIOContext *pb, AVCodecContext *enc);
enum CodecID ff_wav_codec_get_id(unsigned int tag, int bps);
void ff_get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size);

extern const AVCodecTag ff_codec_bmp_tags[];
extern const AVCodecTag ff_codec_wav_tags[];

unsigned int ff_codec_get_tag(const AVCodecTag *tags, int id);
enum CodecID ff_codec_get_id(const AVCodecTag *tags, unsigned int tag);
void ff_parse_specific_params(AVCodecContext *stream, int *au_rate, int *au_ssize, int *au_scale);

#endif /* AVFORMAT_RIFF_H */
