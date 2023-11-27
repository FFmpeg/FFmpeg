/*
 * JPEG XL Header Parser
 * Copyright (c) 2023 Leo Izen <leo.izen@gmail.com>
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

#include <stdint.h>

#include "bytestream.h"
#define UNCHECKED_BITSTREAM_READER 0
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "jpegxl.h"
#include "jpegxl_parse.h"

/* read a U32(c_i + u(u_i)) */
static av_always_inline uint32_t jxl_u32(GetBitContext *gb,
                        uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3,
                        uint32_t u0, uint32_t u1, uint32_t u2, uint32_t u3)
{
    const uint32_t constants[4] = {c0, c1, c2, c3};
    const uint32_t ubits    [4] = {u0, u1, u2, u3};
    uint32_t ret, choice = get_bits(gb, 2);

    ret = constants[choice];
    if (ubits[choice])
        ret += get_bits_long(gb, ubits[choice]);

    return ret;
}

static av_always_inline uint32_t jxl_enum(GetBitContext *gb)
{
    return jxl_u32(gb, 0, 1, 2, 18, 0, 0, 4, 6);
}

/* read a U64() */
static uint64_t jxl_u64(GetBitContext *gb)
{
    uint64_t shift = 12, ret;

    switch (get_bits(gb, 2)) {
    case 1:
        ret = 1 + get_bits(gb, 4);
        break;
    case 2:
        ret = 17 + get_bits(gb, 8);
        break;
    case 3:
        ret = get_bits(gb, 12);
        while (get_bits1(gb)) {
            if (shift < 60) {
                ret |= (uint64_t)get_bits(gb, 8) << shift;
                shift += 8;
            } else {
                ret |= (uint64_t)get_bits(gb, 4) << shift;
                break;
            }
        }
        break;
    default:
        ret = 0;
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
static int jpegxl_read_size_header(GetBitContext *gb, FFJXLMetadata *meta, int validate)
{
    uint32_t width, height;

    if (get_bits1(gb)) {
        /* small size header */
        height = (get_bits(gb, 5) + 1) << 3;
        width = jpegxl_width_from_ratio(height, get_bits(gb, 3));
        if (!width)
            width = (get_bits(gb, 5) + 1) << 3;
    } else {
        /* large size header */
        height = 1 + jxl_u32(gb, 0, 0, 0, 0, 9, 13, 18, 30);
        width = jpegxl_width_from_ratio(height, get_bits(gb, 3));
        if (!width)
            width = 1 + jxl_u32(gb, 0, 0, 0, 0, 9, 13, 18, 30);
    }
    if (validate && (width > (1 << 18) || height > (1 << 18)
        || (width >> 4) * (height >> 4) > (1 << 20)))
        return AVERROR_INVALIDDATA;

    if (meta) {
        meta->width = meta->coded_width = width;
        meta->height = meta->coded_height = height;
    }

    return 0;
}

/**
 * validate a Jpeg XL Preview Header
 * @return >= 0 upon valid size, < 0 upon invalid size found
 */
static int jpegxl_read_preview_header(GetBitContext *gb, int validate)
{
    uint32_t width, height;

    if (get_bits1(gb)) {
        /* coded height and width divided by eight */
        height = jxl_u32(gb, 16, 32, 1, 33, 0, 0, 5, 9) << 3;
        width = jpegxl_width_from_ratio(height, get_bits(gb, 3));
        if (!width)
            width = jxl_u32(gb, 16, 32, 1, 33, 0, 0, 5, 9) << 3;
    } else {
        /* full height and width coded */
        height = jxl_u32(gb, 1, 65, 321, 1345, 6, 8, 10, 12);
        width = jpegxl_width_from_ratio(height, get_bits(gb, 3));
        if (!width)
            width = jxl_u32(gb, 1, 65, 321, 1345, 6, 8, 10, 12);
    }
    if (validate && (width > 4096 || height > 4096))
        return AVERROR_INVALIDDATA;

    return 0;
}

/**
 * get a Jpeg XL BitDepth Header. These cannot be invalid.
 */
static void jpegxl_get_bit_depth(GetBitContext *gb, FFJXLMetadata *meta)
{
    int bit_depth;
    if (get_bits1(gb)) {
        /* float samples */
        bit_depth = jxl_u32(gb, 32, 16, 24, 1, 0, 0, 0, 6); /* mantissa */
        skip_bits_long(gb, 4); /* exponent */
    } else {
        /* integer samples */
        bit_depth = jxl_u32(gb, 8, 10, 12, 1, 0, 0, 0, 6);
    }
    if (meta)
        meta->bit_depth = bit_depth;
}

/**
 * validate a Jpeg XL Extra Channel Info bundle
 * @return >= 0 upon valid, < 0 upon invalid
 */
static int jpegxl_read_extra_channel_info(GetBitContext *gb, FFJXLMetadata *meta, int validate)
{
    int default_alpha = get_bits1(gb);
    uint32_t type, name_len = 0;

    if (!default_alpha) {
        type = jxl_enum(gb);
        if (validate && type > 63)
            return AVERROR_INVALIDDATA; /* enum types cannot be 64+ */
        if (validate && validate < 10 && type == JPEGXL_CT_BLACK)
            return AVERROR_INVALIDDATA;
        jpegxl_get_bit_depth(gb, NULL);
        jxl_u32(gb, 0, 3, 4, 1, 0, 0, 0, 3); /* dim-shift */
        /* max of name_len is 1071 = 48 + 2^10 - 1 */
        name_len = 8 * jxl_u32(gb, 0, 0, 16, 48, 0, 4, 5, 10);
    } else {
        type = JPEGXL_CT_ALPHA;
    }

    if (get_bits_left(gb) < name_len)
        return AVERROR_BUFFER_TOO_SMALL;

    /* skip over the name */
    skip_bits_long(gb, name_len);

    if (!default_alpha && type == JPEGXL_CT_ALPHA)
        skip_bits1(gb);

    if (type == JPEGXL_CT_SPOT_COLOR)
        skip_bits_long(gb, 16 * 4);

    if (type == JPEGXL_CT_CFA)
        jxl_u32(gb, 1, 0, 3, 19, 0, 2, 4, 8);

    if (meta && type == JPEGXL_CT_ALPHA)
        meta->have_alpha = 1;

    return 0;
}

static int jpegxl_skip_extensions(GetBitContext *gb)
{
    uint64_t extensions = jxl_u64(gb), extensions_len = 0;

    if (get_bits_left(gb) <= 0)
        return AVERROR_BUFFER_TOO_SMALL;

    if (!extensions)
        return 0;

    for (int i = 0; i < 64; i++) {
        if (extensions & (UINT64_C(1) << i))
            extensions_len += jxl_u64(gb);
        if (get_bits_left(gb) <= 0)
            return AVERROR_BUFFER_TOO_SMALL;
    }

    if (extensions_len > INT_MAX || get_bits_left(gb) <= extensions_len)
        return AVERROR_BUFFER_TOO_SMALL;

    skip_bits_long(gb, extensions_len);

    return 0;
}

int ff_jpegxl_parse_codestream_header(const uint8_t *buf, int buflen, FFJXLMetadata *meta, int validate)
{
    GetBitContext gbi, *gb = &gbi;

    int all_default, extra_fields = 0;
    int xyb_encoded = 1, have_icc_profile = 0;
    int animation_offset = 0, have_timecodes = 0;

    FFJXLPrimaries primaries = JPEGXL_PR_SRGB;
    FFJXLTransferCharacteristic trc = JPEGXL_TR_SRGB + (1U << 24);
    FFJXLWhitePoint white_point = JPEGXL_WP_D65;
    FFJXLColorSpace color_space = JPEGXL_CS_RGB;

    AVRational tb;
    uint32_t num_extra_channels = 0;
    int ret;

    ret = init_get_bits8(gb, buf, buflen);
    if (ret < 0)
        return ret;

    if (get_bits(gb, 16) != FF_JPEGXL_CODESTREAM_SIGNATURE_LE && validate)
        return AVERROR_INVALIDDATA;

    ret = jpegxl_read_size_header(gb, meta, validate);
    if (ret < 0)
        return ret;

    all_default = get_bits1(gb);
    if (!all_default)
        extra_fields = get_bits1(gb);

    if (extra_fields) {
        int orientation = get_bits(gb, 3);
        if (orientation > 3 && meta)
            FFSWAP(uint32_t, meta->width, meta->height);

        /*
         * intrinstic size
         * any size header here is valid, but as it
         * is variable length we have to read it
         */
        if (get_bits1(gb))
            jpegxl_read_size_header(gb, NULL, 0);

        /* preview header */
        if (get_bits1(gb)) {
            ret = jpegxl_read_preview_header(gb, 0);
            if (ret < 0)
                return ret;
        }

        /* animation header */
        if (get_bits1(gb)) {
            animation_offset = get_bits_count(gb);
            tb.den = jxl_u32(gb, 100, 1000, 1, 1, 0, 0, 10, 30);
            tb.num = jxl_u32(gb, 1, 1001, 1, 1, 0, 0, 8, 10);
            jxl_u32(gb, 0, 0, 0, 0, 0, 3, 16, 32);
            have_timecodes = get_bits1(gb);
        }
    }

    if (animation_offset && meta) {
        meta->animation_offset = animation_offset;
        meta->timebase = tb;
        meta->have_timecodes = have_timecodes;
    }

    if (get_bits_left(gb) <= 0)
        return AVERROR_BUFFER_TOO_SMALL;

    if (!all_default) {
        jpegxl_get_bit_depth(gb, meta);

        /* modular_16bit_buffers must equal 1 */
        if (!get_bits1(gb) && validate && validate < 10)
            return AVERROR_INVALIDDATA;

        num_extra_channels = jxl_u32(gb, 0, 1, 2, 1, 0, 0, 4, 12);
        if (num_extra_channels > 4 && validate && validate < 10)
            return AVERROR_INVALIDDATA;
        for (uint32_t i = 0; i < num_extra_channels; i++) {
            ret = jpegxl_read_extra_channel_info(gb, meta, validate);
            if (ret < 0)
                return ret;
            if (get_bits_left(gb) <= 0)
                return AVERROR_BUFFER_TOO_SMALL;
        }

        xyb_encoded = get_bits1(gb);

        /* color encoding bundle */
        if (!get_bits1(gb)) {
            have_icc_profile = get_bits1(gb);
            color_space = jxl_enum(gb);
            if (color_space > 63 && validate)
                return AVERROR_INVALIDDATA;
            if (!have_icc_profile) {
                if (color_space != JPEGXL_CS_XYB) {
                    white_point = jxl_enum(gb);
                    if (white_point > 63 && validate)
                        return AVERROR_INVALIDDATA;
                    if (white_point == JPEGXL_WP_CUSTOM) {
                        /* ux and uy values */
                        jxl_u32(gb, 0, 524288, 1048576, 2097152, 19, 19, 20, 21);
                        jxl_u32(gb, 0, 524288, 1048576, 2097152, 19, 19, 20, 21);
                    }
                    if (color_space != JPEGXL_CS_GRAY) {
                        /* primaries */
                        primaries = jxl_enum(gb);
                        if (primaries > 63 && validate)
                            return AVERROR_INVALIDDATA;
                        if (primaries == JPEGXL_PR_CUSTOM) {
                            /* ux/uy values for r,g,b */
                            for (int i = 0; i < 6; i++) {
                                jxl_u32(gb, 0, 524288, 1048576, 2097152, 19, 19, 20, 21);
                                if (get_bits_left(gb) <= 0)
                                    return AVERROR_BUFFER_TOO_SMALL;
                            }
                        }
                    }
                }

                /* transfer characteristics */
                if (get_bits1(gb)) {
                    /* gamma */
                    trc = get_bits(gb, 24);
                } else {
                    /* transfer function */
                    trc = jxl_enum(gb);
                    if (trc > 63 && validate)
                        return AVERROR_INVALIDDATA;
                    trc += (1U << 24);
                }

                /* rendering intent */
                if (jxl_enum(gb) > 63 && validate)
                    return AVERROR_INVALIDDATA;
            }
        }

        /* tone mapping bundle */
        if (extra_fields && !get_bits1(gb))
            skip_bits_long(gb, 16 + 16 + 1 + 16);

        ret = jpegxl_skip_extensions(gb);
        if (ret < 0)
            return ret;
    }

    if (meta) {
        meta->xyb_encoded = xyb_encoded;
        meta->have_icc_profile = have_icc_profile;
        meta->csp = color_space;
        meta->primaries = primaries;
        meta->wp = white_point;
        meta->trc = trc;
        if (!meta->bit_depth)
            meta->bit_depth = 8;
        meta->num_extra_channels = num_extra_channels;
    }

    /* default transform */
    if (!get_bits1(gb)) {
        /* opsin inverse matrix */
        if (xyb_encoded && !get_bits1(gb))
            skip_bits_long(gb, 16 * 16);
        /* cw_mask and default weights */
        if (get_bits1(gb))
            skip_bits_long(gb, 16 * 15);
        if (get_bits1(gb))
            skip_bits_long(gb, 16 * 55);
        if (get_bits1(gb))
            skip_bits_long(gb, 16 * 210);
    }

    if (!have_icc_profile) {
        int bits_remaining = 7 - ((get_bits_count(gb) - 1) & 0x7);
        if (bits_remaining && get_bits(gb, bits_remaining))
            return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(gb) < 0)
        return AVERROR_BUFFER_TOO_SMALL;

    return get_bits_count(gb);
}

/*
 * copies as much of the codestream into the buffer as possible
 * pass a shorter buflen to request less
 * returns the number of bytes consumed from input, may be greater than input_len
 * if the input doesn't end on an ISOBMFF-box boundary
 */
int ff_jpegxl_collect_codestream_header(const uint8_t *input_buffer, int input_len,
                                        uint8_t *buffer, int buflen, int *copied)
{
    GetByteContext gb;
    int pos = 0, last_box = 0;
    bytestream2_init(&gb, input_buffer, input_len);

    while (1) {
        uint64_t size;
        uint32_t tag;
        int head_size = 8;

        if (bytestream2_get_bytes_left(&gb) < 8)
            return AVERROR_BUFFER_TOO_SMALL;

        size = bytestream2_get_be32(&gb);
        tag = bytestream2_get_le32(&gb);

        if (size == 1) {
            if (bytestream2_get_bytes_left(&gb) < 8)
                return AVERROR_BUFFER_TOO_SMALL;
            size = bytestream2_get_be64(&gb);
            head_size = 16;
        }
        /* invalid ISOBMFF size */
        if (size && size <= head_size)
            return AVERROR_INVALIDDATA;
        if (size)
            size -= head_size;

        if (tag == MKTAG('j','x','l','p')) {
            uint32_t idx;
            if (bytestream2_get_bytes_left(&gb) < 4)
                return AVERROR_BUFFER_TOO_SMALL;
            idx = bytestream2_get_be32(&gb);
            if (idx >= UINT32_C(0x80000000))
                last_box = 1;
            if (size) {
                if (size <= 4)
                    return AVERROR_INVALIDDATA;
                size -= 4;
            }
        }
        if (tag == MKTAG('j','x','l','c'))
            last_box = 1;

        /*
         * size = 0 means "until EOF". this is legal but uncommon
         * here we just set it to the remaining size of the probe buffer
         */
        if (!size)
            size = bytestream2_get_bytes_left(&gb);
        else
            pos += size + head_size;

        if (tag == MKTAG('j','x','l','c') || tag == MKTAG('j','x','l','p')) {
            if (size > buflen - *copied)
                size = buflen - *copied;
            /*
             * arbitrary chunking of the payload makes this memcpy hard to avoid
             * in practice this will only be performed one or two times at most
             */
            *copied += bytestream2_get_buffer(&gb, buffer + *copied, size);
        } else {
            bytestream2_skip(&gb, size);
        }
        if (last_box || bytestream2_get_bytes_left(&gb) <= 0 || *copied >= buflen)
            break;
    }

    return pos;
}
