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

#include <stddef.h>
#include <stdint.h>

#include "atsc_a53.h"
#include "get_bits.h"

int ff_parse_a53_cc(AVBufferRef **pbuf, const uint8_t *data, int size)
{
    AVBufferRef *buf = *pbuf;
    GetBitContext gb;
    size_t new_size, old_size = buf ? buf->size : 0;
    int ret, cc_count;

    if (size < 3)
        return AVERROR(EINVAL);

    ret = init_get_bits8(&gb, data, size);
    if (ret < 0)
        return ret;

    if (get_bits(&gb, 8) != 0x3) // user_data_type_code
        return 0;

    skip_bits(&gb, 1); // reserved
    if (!get_bits(&gb, 1)) // process_cc_data_flag
        return 0;

    skip_bits(&gb, 1); // zero bit
    cc_count = get_bits(&gb, 5);
    if (!cc_count)
        return 0;

    skip_bits(&gb, 8); // reserved

    /* 3 bytes per CC plus one byte marker_bits at the end */
    if (cc_count * 3 >= (get_bits_left(&gb) >> 3))
        return AVERROR(EINVAL);

    new_size = (old_size + cc_count * 3);

    if (new_size > INT_MAX)
        return AVERROR(EINVAL);

    /* Allow merging of the cc data from two fields. */
    ret = av_buffer_realloc(pbuf, new_size);
    if (ret < 0)
        return ret;

    buf = *pbuf;
    /* Use of av_buffer_realloc assumes buffer is writeable */
    for (int i = 0; i < cc_count; i++) {
        buf->data[old_size++] = get_bits(&gb, 8);
        buf->data[old_size++] = get_bits(&gb, 8);
        buf->data[old_size++] = get_bits(&gb, 8);
    }

    return cc_count;
}
