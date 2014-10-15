/*
 * Video Decode and Presentation API for UNIX (VDPAU) is used for
 * HW decode acceleration for MPEG-1/2, H.264 and VC-1.
 *
 * Copyright (C) 2008 NVIDIA
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

#ifndef AVCODEC_VDPAU_INTERNAL_H
#define AVCODEC_VDPAU_INTERNAL_H

#include "config.h"
#include <stdint.h>
#if CONFIG_VDPAU
#include <vdpau/vdpau.h>
#endif
#include "h264.h"

#include "avcodec.h"
#include "mpeg4video.h"
#include "mpegvideo.h"
#include "version.h"

/** Extract VdpVideoSurface from an AVFrame */
static inline uintptr_t ff_vdpau_get_surface_id(AVFrame *pic)
{
    return (uintptr_t)pic->data[3];
}

struct vdpau_picture_context;
#if CONFIG_VDPAU
#if !FF_API_BUFS_VDPAU
union AVVDPAUPictureInfo {
    VdpPictureInfoH264        h264;
    VdpPictureInfoMPEG1Or2    mpeg;
    VdpPictureInfoVC1          vc1;
    VdpPictureInfoMPEG4Part2 mpeg4;
};
#else
#include "vdpau.h"
#endif

typedef struct VDPAUHWContext {
    AVVDPAUContext context;
    VdpDevice device;
    VdpGetProcAddress *get_proc_address;
    char reset;
    unsigned char flags;
} VDPAUHWContext;

typedef struct VDPAUContext {
    /**
     * VDPAU device handle
     */
    VdpDevice device;

    /**
     * VDPAU decoder handle
     */
    VdpDecoder decoder;

    /**
     * VDPAU device driver
     */
    VdpGetProcAddress *get_proc_address;

    /**
     * VDPAU decoder render callback
     */
    VdpDecoderRender *render;

    uint32_t width;
    uint32_t height;
} VDPAUContext;

struct vdpau_picture_context {
    /**
     * VDPAU picture information.
     */
    union AVVDPAUPictureInfo info;

    /**
     * Allocated size of the bitstream_buffers table.
     */
    int bitstream_buffers_allocated;

    /**
     * Useful bitstream buffers in the bitstream buffers table.
     */
    int bitstream_buffers_used;

   /**
     * Table of bitstream buffers.
     */
    VdpBitstreamBuffer *bitstream_buffers;
};

int ff_vdpau_common_init(AVCodecContext *avctx, VdpDecoderProfile profile,
                         int level);
#endif //CONFIG_VDPAU

int ff_vdpau_common_uninit(AVCodecContext *avctx);

int ff_vdpau_common_start_frame(struct vdpau_picture_context *pic,
                                const uint8_t *buffer, uint32_t size);
int ff_vdpau_common_end_frame(AVCodecContext *avctx, AVFrame *frame,
                              struct vdpau_picture_context *pic);
int ff_vdpau_mpeg_end_frame(AVCodecContext *avctx);
int ff_vdpau_add_buffer(struct vdpau_picture_context *pic, const uint8_t *buf,
                        uint32_t buf_size);


void ff_vdpau_add_data_chunk(uint8_t *data, const uint8_t *buf,
                             int buf_size);

void ff_vdpau_mpeg_picture_complete(MpegEncContext *s, const uint8_t *buf,
                                    int buf_size, int slice_count);

void ff_vdpau_h264_picture_start(H264Context *h);
void ff_vdpau_h264_set_reference_frames(H264Context *h);
void ff_vdpau_h264_picture_complete(H264Context *h);

void ff_vdpau_vc1_decode_picture(MpegEncContext *s, const uint8_t *buf,
                                 int buf_size);

void ff_vdpau_mpeg4_decode_picture(Mpeg4DecContext *s, const uint8_t *buf,
                                   int buf_size);

#endif /* AVCODEC_VDPAU_INTERNAL_H */
