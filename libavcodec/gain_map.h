/*
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
 * Serialization of AVGainMapParams into the metadata blobs that image formats
 * carry it in, and back. Kept separate from the codecs so that an encoder
 * writing the blob and a bitstream filter splicing it into an existing
 * bitstream can share the same code.
 */

#ifndef AVCODEC_GAIN_MAP_H
#define AVCODEC_GAIN_MAP_H

#include "libavutil/gain_map.h"

/* May differ from AV_ISO21496_VERSION if libraries are not in sync */
#define FF_GAIN_MAP_VERSION          0
#define FF_GAIN_MAP_MAX_PAYLOAD_SIZE 141 /* = 5 + 2*8 + 3*5*8 */

/* For XMP-based encoding */
#define FF_GAIN_MAP_XMP_IDENT     "http://ns.adobe.com/xap/1.0/"
#define FF_GAIN_MAP_XMP_NAMESPACE "http://ns.adobe.com/hdr-gain-map/1.0/"
#define FF_GAIN_MAP_XMP_MAX_LEN   1024

/**
 * Parse the payload of an ISO 21496-1 gain map metadata blob.
 *
 * @param p   struct to fill; overwritten on success, untouched on failure
 * @param data payload, excluding container-specific identifiers
 * @param size size of `data` in bytes
 *
 * @return >= 0 on success, or a negative AVERROR on failure
 */
int ff_gain_map_params_from_iso21496(AVGainMapParams *p, const uint8_t *data,
                                     size_t size);

/**
 * Serialize an AVGainMapParams as an ISO 21496-1 gain map metadata blob.
 *
 * The output starts at the minimum_version field; the caller should prepend
 * whatever identifier its container requires.
 *
 * @param p   parameters to serialize
 * @param buf Byte buffer to fill with the serialized data. Must contain at
 *            least FF_GAIN_MAP_MAX_PAYLOAD_SIZE bytes.
 *
 * @return Number of bytes written on success, or a negative AVERROR on failure
 *
 * @note Always succeeds if av_gain_map_params_validate() returns >= 0, and
 *       p->version <= AV_ISO21496_VERSION.
 */
int ff_gain_map_params_to_iso21496(const AVGainMapParams *p, uint8_t *buf);

/**
 * Returns 1 if the gain map parameters are compatible with the XMP-based
 * encoding, or 0 otherwise.
 */
int ff_gain_map_params_check_xmp(const AVGainMapParams *p);

/**
 * Serialize an AVGainMapParams as an Ultra HDR XMP packet.
 *
 * @param p    parameters to serialize
 * @param buf  Byte buffer to fill with the serialized data. Must contain at
 *             least FF_GAIN_MAP_MAX_XMP_LEN bytes.
 *
 * @return Number of bytes written on success, or a negative AVERROR on failure
 *
 * @note Always succeeds if ff_gain_map_params_check_xmp() returns 1.
 */
int ff_gain_map_params_to_xmp(const AVGainMapParams *p, char *buf);

#endif /* AVCODEC_GAIN_MAP_H */
