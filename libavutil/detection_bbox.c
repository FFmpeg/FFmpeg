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

#include "detection_bbox.h"

AVDetectionBBoxHeader *av_detection_bbox_alloc(uint32_t nb_bboxes, size_t *out_size)
{
    size_t size;
    struct {
        AVDetectionBBoxHeader header;
        AVDetectionBBox boxes[1];
    } *ret;

    size = sizeof(*ret);
    if (nb_bboxes - 1 > (SIZE_MAX - size) / sizeof(*ret->boxes))
        return NULL;
    size += sizeof(*ret->boxes) * (nb_bboxes - 1);

    ret = av_mallocz(size);
    if (!ret)
        return NULL;

    ret->header.nb_bboxes = nb_bboxes;
    ret->header.bbox_size = sizeof(*ret->boxes);
    ret->header.bboxes_offset = (char *)&ret->boxes - (char *)&ret->header;

    if (out_size)
        *out_size = size;

    return &ret->header;
}

AVDetectionBBoxHeader *av_detection_bbox_create_side_data(AVFrame *frame, uint32_t nb_bboxes)
{
    AVBufferRef         *buf;
    AVDetectionBBoxHeader *header;
    size_t size;

    header = av_detection_bbox_alloc(nb_bboxes, &size);
    if (!header)
        return NULL;
    buf = av_buffer_create((uint8_t *)header, size, NULL, NULL, 0);
    if (!buf) {
        av_freep(&header);
        return NULL;
    }

    if (!av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_DETECTION_BBOXES, buf)) {
        av_buffer_unref(&buf);
        return NULL;
    }

    return header;
}
