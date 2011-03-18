/*
 * SVQ1 decoder
 * ported to MPlayer by Arpi <arpi@thot.banki.hu>
 * ported to libavcodec by Nick Kurshev <nickols_k@mail.ru>
 *
 * Copyright (C) 2002 the xine project
 * Copyright (C) 2002 the ffmpeg project
 *
 * SVQ1 Encoder (c) 2004 Mike Melanson <melanson@pcisys.net>
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

/**
 * @file
 * Sorenson Vector Quantizer #1 (SVQ1) video codec.
 * For more information of the SVQ1 algorithm, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#ifndef AVCODEC_SVQ1_H
#define AVCODEC_SVQ1_H

#include <stdint.h>

#define SVQ1_BLOCK_SKIP         0
#define SVQ1_BLOCK_INTER        1
#define SVQ1_BLOCK_INTER_4V     2
#define SVQ1_BLOCK_INTRA        3

struct svq1_frame_size {
    uint16_t width;
    uint16_t height;
};

uint16_t ff_svq1_packet_checksum (const uint8_t *data, const int length,
                                  int value);

extern const int8_t* const ff_svq1_inter_codebooks[6];
extern const int8_t* const ff_svq1_intra_codebooks[6];

extern const uint8_t ff_svq1_block_type_vlc[4][2];
extern const uint8_t ff_svq1_intra_multistage_vlc[6][8][2];
extern const uint8_t ff_svq1_inter_multistage_vlc[6][8][2];
extern const uint16_t ff_svq1_intra_mean_vlc[256][2];
extern const uint16_t ff_svq1_inter_mean_vlc[512][2];

extern const struct svq1_frame_size ff_svq1_frame_size_table[7];

#endif /* AVCODEC_SVQ1_H */
