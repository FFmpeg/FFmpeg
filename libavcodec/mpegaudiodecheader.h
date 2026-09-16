/*
 * MPEG Audio header decoder
 * Copyright (c) 2001, 2002 Fabrice Bellard
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
 * MPEG Audio header decoder.
 */

#ifndef AVCODEC_MPEGAUDIODECHEADER_H
#define AVCODEC_MPEGAUDIODECHEADER_H

#include <stdint.h>

#include "libavutil/attributes.h"

#include "codec_id.h"
#include "version_major.h"

#define MP3_MASK 0xFFFE0CCF

#define FF_MPEGAUDIO_LEGACY_DECODE_HEADER (LIBAVCODEC_VERSION_MAJOR < 64)

#if FF_MPEGAUDIO_LEGACY_DECODE_HEADER
#define MPA_DECODE_HEADER \
    int frame_size; \
    int error_protection; \
    int layer; \
    int sample_rate; \
    int sample_rate_index; /* between 0 and 8 */ \
    int bit_rate; \
    int nb_channels; \
    int mode; \
    int mode_ext; \
    int lsf;

typedef struct MPADecodeHeader {
  MPA_DECODE_HEADER
} MPADecodeHeader;

/* header decoding. MUST check the header before because no
   consistency check is done there. Return 1 if free format found and
   that the frame size must be computed externally */
int avpriv_mpegaudio_decode_header(MPADecodeHeader *s, uint32_t header);
#endif

/**
 * Same as MPADecodeHeader, plus the header fields that carry no decoding
 * information. Its size is not part of the libavcodec ABI: it is only ever
 * allocated by avpriv_mpegaudio_decode_header2().
 */
typedef struct MPADecodeHeader2 {
    int frame_size;
    int error_protection;
    int layer;
    int sample_rate;
    int sample_rate_index; /* between 0 and 8 */
    int bit_rate;
    int nb_channels;
    int mode;
    int mode_ext;
    int lsf;
    int copyright;
    int original;
    int emphasis;
    int bitrate_index;
    int padding;
    int private_bit;
} MPADecodeHeader2;

/**
 * Decode an MPEG audio header into *phdr, which is allocated if it is NULL.
 *
 * The header must have been checked beforehand, as no consistency check is
 * done here.
 *
 * @return 1 if free format was found and the frame size must be computed
 *         externally, 0 on success, a negative AVERROR code on failure
 */
int avpriv_mpegaudio_decode_header2(MPADecodeHeader2 **phdr, uint32_t header);

/* same as avpriv_mpegaudio_decode_header2(), for callers within libavcodec,
   which are built against this very header and may allocate it themselves */
int ff_mpegaudio_decode_header(MPADecodeHeader2 *s, uint32_t header);

/* useful helper to get MPEG audio stream info. Return -1 if error in
   header, otherwise the coded frame size in bytes */
int ff_mpa_decode_header(uint32_t head, int *sample_rate,
                         int *channels, int *frame_size, int *bitrate, enum AVCodecID *codec_id);

/* fast header check for resync */
static inline int ff_mpa_check_header(uint32_t header){
    /* header */
    if ((header & 0xffe00000) != 0xffe00000)
        return -1;
    /* version check */
    if ((header & (3<<19)) == 1<<19)
        return -1;
    /* layer check */
    if ((header & (3<<17)) == 0)
        return -1;
    /* bit rate */
    if ((header & (0xf<<12)) == 0xf<<12)
        return -1;
    /* frequency */
    if ((header & (3<<10)) == 3<<10)
        return -1;
    return 0;
}

static inline uint32_t ff_mpa_encode_header(const MPADecodeHeader2 *s)
{
    int version = s->sample_rate_index < 6 ? 3 - s->sample_rate_index / 3 : 0;

    return 0xffeU                   << 20 |
           version                  << 19 |
           (4 - s->layer)           << 17 |
           !s->error_protection     << 16 |
           s->bitrate_index         << 12 |
           s->sample_rate_index % 3 << 10 |
           s->padding               <<  9 |
           s->private_bit           <<  8 |
           s->mode                  <<  6 |
           s->mode_ext              <<  4 |
           s->copyright             <<  3 |
           s->original              <<  2 |
           s->emphasis;
}

#endif /* AVCODEC_MPEGAUDIODECHEADER_H */
