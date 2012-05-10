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

#include "libavutil/mathematics.h"
#include "libavutil/pixfmt.h"
#include "avcodec.h"

typedef struct InternalBuffer {
    uint8_t *base[AV_NUM_DATA_POINTERS];
    uint8_t *data[AV_NUM_DATA_POINTERS];
    int linesize[AV_NUM_DATA_POINTERS];
    int width;
    int height;
    enum PixelFormat pix_fmt;
    uint8_t **extended_data;
    int audio_data_size;
    int nb_channels;
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

#if FF_API_OLD_DECODE_AUDIO
    /**
     * Internal sample count used by avcodec_encode_audio() to fabricate pts.
     * Can be removed along with avcodec_encode_audio().
     */
    int sample_count;
#endif

    /**
     * An audio frame with less than required samples has been submitted and
     * padded with silence. Reject all subsequent frames.
     */
    int last_audio_frame;

    /**
     * temporary buffer used for encoders to store their bitstream
     */
    uint8_t *byte_buffer;
    unsigned int byte_buffer_size;
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

/**
 * Remove and free all side data from packet.
 */
void ff_packet_free_side_data(AVPacket *pkt);

int avpriv_lock_avformat(void);
int avpriv_unlock_avformat(void);

/**
 * Maximum size in bytes of extradata.
 * This value was chosen such that every bit of the buffer is
 * addressable by a 32-bit signed integer as used by get_bits.
 */
#define FF_MAX_EXTRADATA_SIZE ((1 << 28) - FF_INPUT_BUFFER_PADDING_SIZE)

/**
 * Check AVPacket size and/or allocate data.
 *
 * Encoders supporting AVCodec.encode2() can use this as a convenience to
 * ensure the output packet data is large enough, whether provided by the user
 * or allocated in this function.
 *
 * @param avctx   the AVCodecContext of the encoder
 * @param avpkt   the AVPacket
 *                If avpkt->data is already set, avpkt->size is checked
 *                to ensure it is large enough.
 *                If avpkt->data is NULL, a new buffer is allocated.
 *                avpkt->size is set to the specified size.
 *                All other AVPacket fields will be reset with av_init_packet().
 * @param size    the minimum required packet size
 * @return        0 on success, negative error code on failure
 */
int ff_alloc_packet2(AVCodecContext *avctx, AVPacket *avpkt, int size);

int ff_alloc_packet(AVPacket *avpkt, int size);

/**
 * Rescale from sample rate to AVCodecContext.time_base.
 */
static av_always_inline int64_t ff_samples_to_time_base(AVCodecContext *avctx,
                                                        int64_t samples)
{
    if(samples == AV_NOPTS_VALUE)
        return AV_NOPTS_VALUE;
    return av_rescale_q(samples, (AVRational){ 1, avctx->sample_rate },
                        avctx->time_base);
}

int ff_thread_can_start_frame(AVCodecContext *avctx);

#endif /* AVCODEC_INTERNAL_H */
