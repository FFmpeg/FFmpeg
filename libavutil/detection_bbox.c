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
#include "mem.h"

AVDetectionBBoxHeader *av_detection_bbox_alloc(uint32_t nb_bboxes, size_t *out_size)
{
    size_t size;
    struct BBoxContext {
        AVDetectionBBoxHeader header;
        AVDetectionBBox boxes;
    };
    const size_t bboxes_offset = offsetof(struct BBoxContext, boxes);
    const size_t bbox_size = sizeof(AVDetectionBBox);
    AVDetectionBBoxHeader *header;

    size = bboxes_offset;
    if (nb_bboxes > (SIZE_MAX - size) / bbox_size)
        return NULL;
    size += bbox_size * nb_bboxes;

    header = av_mallocz(size);
    if (!header)
        return NULL;

    header->nb_bboxes     = nb_bboxes;
    header->bbox_size     = bbox_size;
    header->bboxes_offset = bboxes_offset;

    if (out_size)
        *out_size = size;

    return header;
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
