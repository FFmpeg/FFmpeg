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

int ff_alloc_a53_sei(const AVFrame *frame, size_t prefix_len,
                     void **data, size_t *sei_size)
{
    AVFrameSideData *side_data = NULL;
    uint8_t *sei_data;

    if (frame)
        side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_A53_CC);

    if (!side_data) {
        *data = NULL;
        return 0;
    }

    *sei_size = side_data->size + 11;
    *data = av_mallocz(*sei_size + prefix_len);
    if (!*data)
        return AVERROR(ENOMEM);
    sei_data = (uint8_t*)*data + prefix_len;

    // country code
    sei_data[0] = 181;
    sei_data[1] = 0;
    sei_data[2] = 49;

    /**
     * 'GA94' is standard in North America for ATSC, but hard coding
     * this style may not be the right thing to do -- other formats
     * do exist. This information is not available in the side_data
     * so we are going with this right now.
     */
    AV_WL32(sei_data + 3, MKTAG('G', 'A', '9', '4'));
    sei_data[7] = 3;
    sei_data[8] = ((side_data->size/3) & 0x1f) | 0x40;
    sei_data[9] = 0;

    memcpy(sei_data + 10, side_data->data, side_data->size);

    sei_data[side_data->size+10] = 255;

    return 0;
}

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
