/*
 * Jpeg XL header verification
 * Copyright (c) 2022 Leo Izen <leo.izen@gmail.com>
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

#include "jpegxl_probe.h"

#define BITSTREAM_READER_LE
#include "libavcodec/get_bits.h"

enum JpegXLExtraChannelType {
    FF_JPEGXL_CT_ALPHA = 0,
    FF_JPEGXL_CT_DEPTH,
    FF_JPEGXL_CT_SPOT_COLOR,
    FF_JPEGXL_CT_SELECTION_MASK,
    FF_JPEGXL_CT_BLACK,
    FF_JPEGXL_CT_CFA,
    FF_JPEGXL_CT_THERMAL,
    FF_JPEGXL_CT_NON_OPTIONAL = 15,
    FF_JPEGXL_CT_OPTIONAL
};

enum JpegXLColorSpace {
    FF_JPEGXL_CS_RGB = 0,
    FF_JPEGXL_CS_GRAY,
    FF_JPEGXL_CS_XYB,
    FF_JPEGXL_CS_UNKNOWN
};

enum JpegXLWhitePoint {
    FF_JPEGXL_WP_D65 = 1,
    FF_JPEGXL_WP_CUSTOM,
    FF_JPEGXL_WP_E = 10,
    FF_JPEGXL_WP_DCI = 11
};

enum JpegXLPrimaries {
    FF_JPEGXL_PR_SRGB = 1,
    FF_JPEGXL_PR_CUSTOM,
    FF_JPEGXL_PR_2100 = 9,
    FF_JPEGXL_PR_P3 = 11,
};

#define jxl_bits(n) get_bits_long(gb, (n))
#define jxl_bits_skip(n) skip_bits_long(gb, (n))
#define jxl_u32(c0, c1, c2, c3, u0, u1, u2, u3) jpegxl_u32(gb, \
    (const uint32_t[]){c0, c1, c2, c3}, (const uint32_t[]){u0, u1, u2, u3})
#define jxl_u64() jpegxl_u64(gb)
#define jxl_enum() jxl_u32(0, 1, 2, 18, 0, 0, 4, 6)

/* read a U32(c_i + u(u_i)) */
static uint32_t jpegxl_u32(GetBitContext *gb,
                           const uint32_t constants[4], const uint32_t ubits[4])
{
    uint32_t ret, choice = jxl_bits(2);

    ret = constants[choice];
    if (ubits[choice])
        ret += jxl_bits(ubits[choice]);

    return ret;
}

/* read a U64() */
static uint64_t jpegxl_u64(GetBitContext *gb)
{
    uint64_t shift = 12, ret;

    switch (jxl_bits(2)) {
    case 0:
        ret = 0;
        break;
    case 1:
        ret = 1 + jxl_bits(4);
        break;
    case 2:
        ret = 17 + jxl_bits(8);
        break;
    case 3:
        ret = jxl_bits(12);
        while (jxl_bits(1)) {
            if (shift < 60) {
                ret |= (uint64_t)jxl_bits(8) << shift;
                shift += 8;
            } else {
                ret |= (uint64_t)jxl_bits(4) << shift;
                break;
            }
        }
        break;
    }

    return ret;
}

static uint32_t jpegxl_width_from_ratio(uint32_t height, int ratio)
{
    uint64_t height64 = height; /* avoid integer overflow */
    switch (ratio) {
    case 1:
        return height;
    case 2:
        return (uint32_t)((height64 * 12) / 10);
    case 3:
        return (uint32_t)((height64 * 4) / 3);
    case 4:
        return (uint32_t)((height64 * 3) / 2);
    case 5:
        return (uint32_t)((height64 * 16) / 9);
    case 6:
        return (uint32_t)((height64 * 5) / 4);
    case 7:
        return (uint32_t)(height64 * 2);
    default:
        break;
    }

    return 0; /* manual width */
}

/**
 * validate a Jpeg XL Size Header
 * @return >= 0 upon valid size, < 0 upon invalid size found
 */
static int jpegxl_read_size_header(GetBitContext *gb)
{
    uint32_t width, height;

    if (jxl_bits(1)) {
        /* small size header */
        height = (jxl_bits(5) + 1) << 3;
        width = jpegxl_width_from_ratio(height, jxl_bits(3));
        if (!width)
            width = (jxl_bits(5) + 1) << 3;
    } else {
        /* large size header */
        height = 1 + jxl_u32(0, 0, 0, 0, 9, 13, 18, 30);
        width = jpegxl_width_from_ratio(height, jxl_bits(3));
        if (!width)
            width = 1 + jxl_u32(0, 0, 0, 0, 9, 13, 18, 30);
    }
    if (width > (1 << 18) || height > (1 << 18)
        || (width >> 4) * (height >> 4) > (1 << 20))
        return -1;

    return 0;
}

/**
 * validate a Jpeg XL Preview Header
 * @return >= 0 upon valid size, < 0 upon invalid size found
 */
static int jpegxl_read_preview_header(GetBitContext *gb)
{
    uint32_t width, height;

    if (jxl_bits(1)) {
        /* coded height and width divided by eight */
        height = jxl_u32(16, 32, 1, 33, 0, 0, 5, 9) << 3;
        width = jpegxl_width_from_ratio(height, jxl_bits(3));
        if (!width)
            width = jxl_u32(16, 32, 1, 33, 0, 0, 5, 9) << 3;
    } else {
        /* full height and width coded */
        height = jxl_u32(1, 65, 321, 1345, 6, 8, 10, 12);
        width = jpegxl_width_from_ratio(height, jxl_bits(3));
        if (!width)
            width = jxl_u32(1, 65, 321, 1345, 6, 8, 10, 12);
    }
    if (width > 4096 || height > 4096)
        return -1;

    return 0;
}

/**
 * skip a Jpeg XL BitDepth Header. These cannot be invalid.
 */
static void jpegxl_skip_bit_depth(GetBitContext *gb)
{
    if (jxl_bits(1)) {
        /* float samples */
        jxl_u32(32, 16, 24, 1, 0, 0, 0, 6); /* mantissa */
        jxl_bits_skip(4); /* exponent */
    } else {
        /* integer samples */
        jxl_u32(8, 10, 12, 1, 0, 0, 0, 6);
    }
}

/**
 * validate a Jpeg XL Extra Channel Info bundle
 * @return >= 0 upon valid, < 0 upon invalid
 */
static int jpegxl_read_extra_channel_info(GetBitContext *gb)
{
    int all_default = jxl_bits(1);
    uint32_t type, name_len = 0;

    if (!all_default) {
        type = jxl_enum();
        if (type > 63)
            return -1; /* enum types cannot be 64+ */
        if (type == FF_JPEGXL_CT_BLACK)
            return -1;
        jpegxl_skip_bit_depth(gb);
        jxl_u32(0, 3, 4, 1, 0, 0, 0, 3); /* dim-shift */
        /* max of name_len is 1071 = 48 + 2^10 - 1 */
        name_len = jxl_u32(0, 0, 16, 48, 0, 4, 5, 10);
    } else {
        type = FF_JPEGXL_CT_ALPHA;
    }

    /* skip over the name */
    jxl_bits_skip(8 * name_len);

    if (!all_default && type == FF_JPEGXL_CT_ALPHA)
        jxl_bits_skip(1);

    if (type == FF_JPEGXL_CT_SPOT_COLOR)
        jxl_bits_skip(16 * 4);

    if (type == FF_JPEGXL_CT_CFA)
        jxl_u32(1, 0, 3, 19, 0, 2, 4, 8);

    return 0;
}

/* verify that a codestream header is valid */
int ff_jpegxl_verify_codestream_header(const uint8_t *buf, int buflen)
{
    GetBitContext gbi, *gb = &gbi;
    int all_default, extra_fields = 0;
    int xyb_encoded = 1, have_icc_profile = 0;
    uint32_t num_extra_channels;
    uint64_t extensions;
    int ret;

    ret = init_get_bits8(gb, buf, buflen);
    if (ret < 0)
        return ret;

    if (jxl_bits(16) != FF_JPEGXL_CODESTREAM_SIGNATURE_LE)
        return -1;

    if (jpegxl_read_size_header(gb) < 0)
        return -1;

    all_default = jxl_bits(1);
    if (!all_default)
        extra_fields = jxl_bits(1);

    if (extra_fields) {
        jxl_bits_skip(3); /* orientation */

        /*
         * intrinstic size
         * any size header here is valid, but as it
         * is variable length we have to read it
         */
        if (jxl_bits(1))
            jpegxl_read_size_header(gb);

        /* preview header */
        if (jxl_bits(1)) {
            if (jpegxl_read_preview_header(gb) < 0)
                return -1;
        }

        /* animation header */
        if (jxl_bits(1)) {
            jxl_u32(100, 1000, 1, 1, 0, 0, 10, 30);
            jxl_u32(1, 1001, 1, 1, 0, 0, 8, 10);
            jxl_u32(0, 0, 0, 0, 0, 3, 16, 32);
            jxl_bits_skip(1);
        }
    }

    if (!all_default) {
        jpegxl_skip_bit_depth(gb);

        /* modular_16bit_buffers must equal 1 */
        if (!jxl_bits(1))
            return -1;

        num_extra_channels = jxl_u32(0, 1, 2, 1, 0, 0, 4, 12);
        if (num_extra_channels > 4)
            return -1;
        for (uint32_t i = 0; i < num_extra_channels; i++) {
            if (jpegxl_read_extra_channel_info(gb) < 0)
                return -1;
        }

        xyb_encoded = jxl_bits(1);

        /* color encoding bundle */
        if (!jxl_bits(1)) {
            uint32_t color_space;
            have_icc_profile = jxl_bits(1);
            color_space = jxl_enum();
            if (color_space > 63)
                return -1;

            if (!have_icc_profile) {
                if (color_space != FF_JPEGXL_CS_XYB) {
                    uint32_t white_point = jxl_enum();
                    if (white_point > 63)
                        return -1;
                    if (white_point == FF_JPEGXL_WP_CUSTOM) {
                        /* ux and uy values */
                        jxl_u32(0, 524288, 1048576, 2097152, 19, 19, 20, 21);
                        jxl_u32(0, 524288, 1048576, 2097152, 19, 19, 20, 21);
                    }
                    if (color_space != FF_JPEGXL_CS_GRAY) {
                        /* primaries */
                        uint32_t primaries = jxl_enum();
                        if (primaries > 63)
                            return -1;
                        if (primaries == FF_JPEGXL_PR_CUSTOM) {
                            /* ux/uy values for r,g,b */
                            for (int i = 0; i < 6; i++)
                                jxl_u32(0, 524288, 1048576, 2097152, 19, 19, 20, 21);
                        }
                    }
                }

                /* transfer characteristics */
                if (jxl_bits(1)) {
                    /* gamma */
                    jxl_bits_skip(24);
                } else {
                    /* transfer function */
                    if (jxl_enum() > 63)
                        return -1;
                }

                /* rendering intent */
                if (jxl_enum() > 63)
                    return -1;
            }
        }

        /* tone mapping bundle */
        if (extra_fields && !jxl_bits(1))
            jxl_bits_skip(16 + 16 + 1 + 16);

        extensions = jxl_u64();
        if (extensions) {
            for (int i = 0; i < 64; i++) {
                if (extensions & (UINT64_C(1) << i))
                    jxl_u64();
            }
        }
    }

    /* default transform */
    if (!jxl_bits(1)) {
        /* opsin inverse matrix */
        if (xyb_encoded && !jxl_bits(1))
            jxl_bits_skip(16 * 16);
        /* cw_mask and default weights */
        if (jxl_bits(1))
            jxl_bits_skip(16 * 15);
        if (jxl_bits(1))
            jxl_bits_skip(16 * 55);
        if (jxl_bits(1))
            jxl_bits_skip(16 * 210);
    }

    if (!have_icc_profile) {
        int bits_remaining = 7 - (get_bits_count(gb) - 1) % 8;
        if (bits_remaining && jxl_bits(bits_remaining))
            return -1;
    }

    if (get_bits_left(gb) < 0)
        return -1;

    return 0;
}
