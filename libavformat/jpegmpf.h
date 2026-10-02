/*
 * JPEG Multi-Picture Format common definitions
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
 * Shared definitions for JPEG Multi-Picture Format (CIPA DC-007).
 */

#ifndef AVFORMAT_JPEGMPF_H
#define AVFORMAT_JPEGMPF_H

/* Individual Image Attribute bits, from CIPA DC-007 Figure 8 */
enum MPFAttribute {
    MPF_DEPENDENT_PARENT = 0x80000000,
    MPF_DEPENDENT_CHILD  = 0x40000000,
    MPF_FORMAT_JPEG      = 0x00000000,
};

/* MP Type Codes, from CIPA DC-007 Table 4 */
enum MPFType {
    MPF_TYPE_UNDEFINED = 0x000000,
    MPF_TYPE_PRIMARY   = 0x030000, /* Baseline MP Primary Image */
    MPF_TYPE_GAIN_MAP  = 0x050000,
};

/* MP Index IFD tags, from CIPA DC-007 Table 3 */
enum MPFTag {
    MPF_TAG_MPFVersion      = 0xB000,
    MPF_TAG_NumberOfImages  = 0xB001,
    MPF_TAG_MPEntry         = 0xB002,
};

/* APP2 extension segments */
typedef enum MPFExt {
    MPF_EXT_XMP,        /* GContainer directory, First Individual Image only */
    MPF_EXT_INDEX,      /* MP Index IFD, First Individual Image only */
    MPF_EXT_ISO21496,   /* ISO 21496-1 version fields (in the base image) */
    MPF_EXT_NB,
} MPFExt;

/* Namespace URIs of the GContainer XMP extension */
#define XMP_CONTAINER_NS "http://ns.google.com/photos/1.0/container/"
#define XMP_ITEM_NS      "http://ns.google.com/photos/1.0/container/item/"

#endif /* AVFORMAT_JPEGMPF_H */
