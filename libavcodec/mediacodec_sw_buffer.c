/*
 * Android MediaCodec software buffer copy functions
 *
 * Copyright (c) 2015-2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include <string.h>
#include <sys/types.h>

#include "libavutil/frame.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "mediacodec_wrapper.h"
#include "mediacodec_sw_buffer.h"
#include "mediacodecdec_common.h"

#define QCOM_TILE_WIDTH 64
#define QCOM_TILE_HEIGHT 32
#define QCOM_TILE_SIZE (QCOM_TILE_WIDTH * QCOM_TILE_HEIGHT)
#define QCOM_TILE_GROUP_SIZE (4 * QCOM_TILE_SIZE)

/**
 * The code handling the various YUV color formats is taken from the
 * GStreamer project.
 *
 * Gstreamer reference:
 * https://cgit.freedesktop.org/gstreamer/gst-plugins-bad/tree/sys/androidmedia/
 *
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * Copyright (C) 2012, Rafaël Carré <funman@videolanorg>
 *
 * Copyright (C) 2015, Sebastian Dröge <sebastian@centricular.com>
 *
 * Copyright (C) 2014-2015, Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@gcollabora.com>
 *
 * Copyright (C) 2015, Edward Hervey
 *   Author: Edward Hervey <bilboed@gmail.com>
 *
 * Copyright (C) 2015, Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */
void ff_mediacodec_sw_buffer_copy_yuv420_planar(AVCodecContext *avctx,
                                                MediaCodecDecContext *s,
                                                uint8_t *data,
                                                size_t size,
                                                FFAMediaCodecBufferInfo *info,
                                                AVFrame *frame)
{
    int i;
    uint8_t *src = NULL;

    for (i = 0; i < 3; i++) {
        int stride = s->stride;
        int height;

        src = data + info->offset;
        if (i == 0) {
            height = avctx->height;

            src += s->crop_top * s->stride;
            src += s->crop_left;
        } else {
            height = avctx->height / 2;
            stride = (s->stride + 1) / 2;

            src += s->slice_height * s->stride;

            if (i == 2) {
                src += ((s->slice_height + 1) / 2) * stride;
            }

            src += s->crop_top * stride;
            src += (s->crop_left / 2);
        }

        if (frame->linesize[i] == stride) {
            memcpy(frame->data[i], src, height * stride);
        } else {
            int j, width;
            uint8_t *dst = frame->data[i];

            if (i == 0) {
                width = avctx->width;
            } else if (i >= 1) {
                width = FFMIN(frame->linesize[i], FFALIGN(avctx->width, 2) / 2);
            }

            for (j = 0; j < height; j++) {
                memcpy(dst, src, width);
                src += stride;
                dst += frame->linesize[i];
            }
        }
    }
}

void ff_mediacodec_sw_buffer_copy_yuv420_semi_planar(AVCodecContext *avctx,
                                                     MediaCodecDecContext *s,
                                                     uint8_t *data,
                                                     size_t size,
                                                     FFAMediaCodecBufferInfo *info,
                                                     AVFrame *frame)
{
    int i;
    uint8_t *src = NULL;

    for (i = 0; i < 2; i++) {
        int height;

        src = data + info->offset;
        if (i == 0) {
            height = avctx->height;

            src += s->crop_top * s->stride;
            src += s->crop_left;
        } else if (i == 1) {
            height = avctx->height / 2;

            src += s->slice_height * s->stride;
            src += s->crop_top * s->stride;
            src += s->crop_left;
        }

        if (frame->linesize[i] == s->stride) {
            memcpy(frame->data[i], src, height * s->stride);
        } else {
            int j, width;
            uint8_t *dst = frame->data[i];

            if (i == 0) {
                width = avctx->width;
            } else if (i == 1) {
                width = FFMIN(frame->linesize[i], FFALIGN(avctx->width, 2));
            }

            for (j = 0; j < height; j++) {
                memcpy(dst, src, width);
                src += s->stride;
                dst += frame->linesize[i];
            }
        }
    }
}



void ff_mediacodec_sw_buffer_copy_yuv420_packed_semi_planar(AVCodecContext *avctx,
                                                            MediaCodecDecContext *s,
                                                            uint8_t *data,
                                                            size_t size,
                                                            FFAMediaCodecBufferInfo *info,
                                                            AVFrame *frame)
{
    int i;
    uint8_t *src = NULL;

    for (i = 0; i < 2; i++) {
        int height;

        src = data + info->offset;
        if (i == 0) {
            height = avctx->height;
        } else if (i == 1) {
            height = avctx->height / 2;

            src += (s->slice_height - s->crop_top / 2) * s->stride;

            src += s->crop_top * s->stride;
            src += s->crop_left;
        }

        if (frame->linesize[i] == s->stride) {
            memcpy(frame->data[i], src, height * s->stride);
        } else {
            int j, width;
            uint8_t *dst = frame->data[i];

            if (i == 0) {
                width = avctx->width;
            } else if (i == 1) {
                width = FFMIN(frame->linesize[i], FFALIGN(avctx->width, 2));
            }

            for (j = 0; j < height; j++) {
                memcpy(dst, src, width);
                src += s->stride;
                dst += frame->linesize[i];
            }
        }
    }
}

/**
 * The code handling the QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka
 * color format is taken from the VLC project.
 *
 * VLC reference:
 * http://git.videolan.org/?p=vlc.git;a=blob;f=modules/codec/omxil/qcom.c;hb=HEAD
 *
 * VLC copyright notice:
 *
 *****************************************************************************
 * qcom.c : pixel format translation for Qualcomm tiled nv12
 *****************************************************************************
 * Copyright © 2012 Rafaël Carré
 *
 * Authors: Rafaël Carré <funman@videolanorg>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *
 */

static size_t qcom_tile_pos(size_t x, size_t y, size_t w, size_t h)
{
  size_t flim = x + (y & ~1) * w;

  if (y & 1) {
    flim += (x & ~3) + 2;
  } else if ((h & 1) == 0 || y != (h - 1)) {
    flim += (x + 2) & ~3;
  }

  return flim;
}

void ff_mediacodec_sw_buffer_copy_yuv420_packed_semi_planar_64x32Tile2m8ka(AVCodecContext *avctx,
                                                                           MediaCodecDecContext *s,
                                                                           uint8_t *data,
                                                                           size_t size,
                                                                           FFAMediaCodecBufferInfo *info,
                                                                           AVFrame *frame)
{
    size_t width = frame->width;
    size_t linesize = frame->linesize[0];
    size_t height = frame->height;

    const size_t tile_w = (width - 1) / QCOM_TILE_WIDTH + 1;
    const size_t tile_w_align = (tile_w + 1) & ~1;
    const size_t tile_h_luma = (height - 1) / QCOM_TILE_HEIGHT + 1;
    const size_t tile_h_chroma = (height / 2 - 1) / QCOM_TILE_HEIGHT + 1;

    size_t luma_size = tile_w_align * tile_h_luma * QCOM_TILE_SIZE;
    if((luma_size % QCOM_TILE_GROUP_SIZE) != 0)
        luma_size = (((luma_size - 1) / QCOM_TILE_GROUP_SIZE) + 1) * QCOM_TILE_GROUP_SIZE;

    for(size_t y = 0; y < tile_h_luma; y++) {
        size_t row_width = width;
        for(size_t x = 0; x < tile_w; x++) {
            size_t tile_width = row_width;
            size_t tile_height = height;
            /* dest luma memory index for this tile */
            size_t luma_idx = y * QCOM_TILE_HEIGHT * linesize + x * QCOM_TILE_WIDTH;
            /* dest chroma memory index for this tile */
            /* XXX: remove divisions */
            size_t chroma_idx = (luma_idx / linesize) * linesize / 2 + (luma_idx % linesize);

            /* luma source pointer for this tile */
            const uint8_t *src_luma  = data
                + qcom_tile_pos(x, y,tile_w_align, tile_h_luma) * QCOM_TILE_SIZE;

            /* chroma source pointer for this tile */
            const uint8_t *src_chroma = data + luma_size
                + qcom_tile_pos(x, y/2, tile_w_align, tile_h_chroma) * QCOM_TILE_SIZE;
            if (y & 1)
                src_chroma += QCOM_TILE_SIZE/2;

            /* account for right columns */
            if (tile_width > QCOM_TILE_WIDTH)
                tile_width = QCOM_TILE_WIDTH;

            /* account for bottom rows */
            if (tile_height > QCOM_TILE_HEIGHT)
                tile_height = QCOM_TILE_HEIGHT;

            tile_height /= 2;
            while (tile_height--) {
                memcpy(frame->data[0] + luma_idx, src_luma, tile_width);
                src_luma += QCOM_TILE_WIDTH;
                luma_idx += linesize;

                memcpy(frame->data[0] + luma_idx, src_luma, tile_width);
                src_luma += QCOM_TILE_WIDTH;
                luma_idx += linesize;

                memcpy(frame->data[1] + chroma_idx, src_chroma, tile_width);
                src_chroma += QCOM_TILE_WIDTH;
                chroma_idx += linesize;
            }
            row_width -= QCOM_TILE_WIDTH;
        }
        height -= QCOM_TILE_HEIGHT;
    }
}
