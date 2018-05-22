/**
 * Copyright (c) 2018 Patrick Keroulas <patrick.keroulas@savoirfairelinux.com>
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

#ifndef AVUTIL_ANCILLARY_DATA_H
#define AVUTIL_ANCILLARY_DATA_H

#include <stdint.h>

enum AVAncillaryDataFields {
    AV_ANCILLARY_DATA_FIELD_NONE         = 0x00,
    AV_ANCILLARY_DATA_FIELD_TOP_FIELD    = 0x01,
    AV_ANCILLARY_DATA_FIELD_BOTTOM_FIELD = 0x02,
};

/**
 * Ancillary data carries various side data that can't be transmitted in
 * AV bit streams for codecs like bitpacked or v210.
 *
 * To be used as payload of a AVPacketSideData.
 *
 */
typedef struct AVAncillaryData {
    /**
     * Flags to determine if the AVPacket holds a top/bottom field in
     * case of interlaced format.
     */
    uint8_t field;
} AVAncillaryData;

/**
 * Allocate an AVAncillaryData structure and set its fields to
 * default values. The resulting struct can be freed using av_freep().
 *
 * @return An AVAncillaryData filled with default values or NULL
 *         on failure.
 */
AVAncillaryData *av_ancillary_data_alloc(void);

#endif /* AVUTIL_ANCILLARY_DATA_H */
