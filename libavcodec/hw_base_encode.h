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

#ifndef AVCODEC_HW_BASE_ENCODE_H
#define AVCODEC_HW_BASE_ENCODE_H

#define MAX_DPB_SIZE 16
#define MAX_PICTURE_REFERENCES 2
#define MAX_REORDER_DELAY 16
#define MAX_ASYNC_DEPTH 64
#define MAX_REFERENCE_LIST_NUM 2

static inline const char *ff_hw_base_encode_get_pictype_name(const int type)
{
    const char * const picture_type_name[] = { "IDR", "I", "P", "B" };
    return picture_type_name[type];
}

enum {
    FF_HW_PICTURE_TYPE_IDR = 0,
    FF_HW_PICTURE_TYPE_I   = 1,
    FF_HW_PICTURE_TYPE_P   = 2,
    FF_HW_PICTURE_TYPE_B   = 3,
};

enum {
    // Codec supports controlling the subdivision of pictures into slices.
    FF_HW_FLAG_SLICE_CONTROL         = 1 << 0,
    // Codec only supports constant quality (no rate control).
    FF_HW_FLAG_CONSTANT_QUALITY_ONLY = 1 << 1,
    // Codec is intra-only.
    FF_HW_FLAG_INTRA_ONLY            = 1 << 2,
    // Codec supports B-pictures.
    FF_HW_FLAG_B_PICTURES            = 1 << 3,
    // Codec supports referencing B-pictures.
    FF_HW_FLAG_B_PICTURE_REFERENCES  = 1 << 4,
    // Codec supports non-IDR key pictures (that is, key pictures do
    // not necessarily empty the DPB).
    FF_HW_FLAG_NON_IDR_KEY_PICTURES  = 1 << 5,
};

typedef struct FFHWBaseEncodeContext {
    const AVClass *class;

    // Max number of frame buffered in encoder.
    int             async_depth;
} FFHWBaseEncodeContext;

#define HW_BASE_ENCODE_COMMON_OPTIONS \
    { "async_depth", "Maximum processing parallelism. " \
      "Increase this to improve single channel performance.", \
      OFFSET(common.base.async_depth), AV_OPT_TYPE_INT, \
      { .i64 = 2 }, 1, MAX_ASYNC_DEPTH, FLAGS }

#endif /* AVCODEC_HW_BASE_ENCODE_H */
