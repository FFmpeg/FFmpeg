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

#ifndef AVCODEC_VT_INTERNAL_H
#define AVCODEC_VT_INTERNAL_H

typedef struct VTContext {
    // The current bitstream buffer.
    uint8_t                     *bitstream;

    // The current size of the bitstream.
    int                         bitstream_size;

    // The reference size used for fast reallocation.
    int                         allocated_size;

    // The core video buffer
    CVImageBufferRef            frame;

    // Current dummy frames context (depends on exact CVImageBufferRef params).
    struct AVBufferRef         *cached_hw_frames_ctx;

    // Non-NULL if the new hwaccel API is used. This is only a separate struct
    // to ease compatibility with the old API.
    struct AVVideotoolboxContext *vt_ctx;

    // Current H264 parameters (used to trigger decoder restart on SPS changes).
    uint8_t                     sps[3];
    bool                        reconfig_needed;
} VTContext;

int ff_videotoolbox_alloc_frame(AVCodecContext *avctx, AVFrame *frame);
int ff_videotoolbox_uninit(AVCodecContext *avctx);
int ff_videotoolbox_h264_start_frame(AVCodecContext *avctx,
                                     const uint8_t *buffer,
                                     uint32_t size);
int ff_videotoolbox_h264_decode_slice(AVCodecContext *avctx,
                                      const uint8_t *buffer,
                                      uint32_t size);
CFDataRef ff_videotoolbox_avcc_extradata_create(AVCodecContext *avctx);
CFDataRef ff_videotoolbox_hvcc_extradata_create(AVCodecContext *avctx);

#endif /* AVCODEC_VT_INTERNAL_H */
