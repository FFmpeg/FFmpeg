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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "atsc_a53.h"

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
    size_t new_size, old_size = buf ? buf->size : 0;
    int ret, cc_count;

    if (size < 3)
        return AVERROR_INVALIDDATA;

    if (data[0] != 0x3) // user_data_type_code
        return 0;

    if (!(data[1] & 0x40)) // process_cc_data_flag
        return 0;

    cc_count = data[1] & 0x1F;
    if (!cc_count)
        return 0;

    // content of data[2] is reserved
    /* 3 bytes per CC plus one byte marker_bits at the end */
    if (cc_count * 3 >= size - 3)
        return AVERROR_INVALIDDATA;

    new_size = (old_size + cc_count * 3);

    if (new_size > INT_MAX)
        return AVERROR_INVALIDDATA;

    /* Allow merging of the cc data from two fields. */
    ret = av_buffer_realloc(pbuf, new_size);
    if (ret < 0)
        return ret;

    buf = *pbuf;
    /* Use of av_buffer_realloc assumes buffer is writeable */
    memcpy(buf->data + old_size, data + 3, cc_count * 3);

    return cc_count;
}
