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

/**
 * @file
 * common internal api header.
 */

#ifndef AVCODEC_INTERNAL_H
#define AVCODEC_INTERNAL_H

#include <stdint.h>

#include "libavutil/pixfmt.h"
#include "avcodec.h"

typedef struct InternalBuffer {
    int last_pic_num;
    uint8_t *base[4];
    uint8_t *data[4];
    int linesize[4];
    int width;
    int height;
    enum PixelFormat pix_fmt;
} InternalBuffer;

typedef struct AVCodecInternal {
    /**
     * internal buffer count
     * used by default get/release/reget_buffer().
     */
    int buffer_count;

    /**
     * internal buffers
     * used by default get/release/reget_buffer().
     */
    InternalBuffer *buffer;

    /**
     * Whether the parent AVCodecContext is a copy of the context which had
     * init() called on it.
     * This is used by multithreading - shared tables and picture pointers
     * should be freed from the original context only.
     */
    int is_copy;
} AVCodecInternal;

struct AVCodecDefault {
    const uint8_t *key;
    const uint8_t *value;
};

/**
 * Determine whether pix_fmt is a hardware accelerated format.
 */
int ff_is_hwaccel_pix_fmt(enum PixelFormat pix_fmt);

/**
 * Return the hardware accelerated codec for codec codec_id and
 * pixel format pix_fmt.
 *
 * @param codec_id the codec to match
 * @param pix_fmt the pixel format to match
 * @return the hardware accelerated codec, or NULL if none was found.
 */
AVHWAccel *ff_find_hwaccel(enum CodecID codec_id, enum PixelFormat pix_fmt);

/**
 * Return the index into tab at which {a,b} match elements {[0],[1]} of tab.
 * If there is no such matching pair then size is returned.
 */
int ff_match_2uint16(const uint16_t (*tab)[2], int size, int a, int b);

unsigned int avpriv_toupper4(unsigned int x);

/**
 * does needed setup of pkt_pts/pos and such for (re)get_buffer();
 */
void ff_init_buffer_info(AVCodecContext *s, AVFrame *pic);

int avpriv_lock_avformat(void);
int avpriv_unlock_avformat(void);

#endif /* AVCODEC_INTERNAL_H */
