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

#ifndef AVCODEC_VDA_VT_INTERNAL_H
#define AVCODEC_VDA_VT_INTERNAL_H

void ff_vda_output_callback(void *vda_hw_ctx,
                            CFDictionaryRef user_info,
                            OSStatus status,
                            uint32_t infoFlags,
                            CVImageBufferRef image_buffer);

int ff_vda_default_init(AVCodecContext *avctx);
void ff_vda_default_free(AVCodecContext *avctx);

typedef struct VTContext {
    // The current bitstream buffer.
    uint8_t                     *bitstream;

    // The current size of the bitstream.
    int                         bitstream_size;

    // The reference size used for fast reallocation.
    int                         allocated_size;

    // The core video buffer
    CVImageBufferRef            frame;
} VTContext;

int ff_videotoolbox_alloc_frame(AVCodecContext *avctx, AVFrame *frame);
int ff_videotoolbox_uninit(AVCodecContext *avctx);
int ff_videotoolbox_buffer_create(VTContext *vtctx, AVFrame *frame);
int ff_videotoolbox_h264_start_frame(AVCodecContext *avctx,
                                     const uint8_t *buffer,
                                     uint32_t size);
int ff_videotoolbox_h264_decode_slice(AVCodecContext *avctx,
                                      const uint8_t *buffer,
                                      uint32_t size);
CFDataRef ff_videotoolbox_avcc_extradata_create(AVCodecContext *avctx);
#endif /* AVCODEC_VDA_VT_INTERNAL_H */
