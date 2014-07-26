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

#include "libavutil/buffer.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixfmt.h"
#include "avcodec.h"
#include "config.h"

#define FF_SANE_NB_CHANNELS 63U

#if HAVE_AVX
#   define STRIDE_ALIGN 32
#elif HAVE_SIMD_ALIGN_16
#   define STRIDE_ALIGN 16
#else
#   define STRIDE_ALIGN 8
#endif

typedef struct FramePool {
    /**
     * Pools for each data plane. For audio all the planes have the same size,
     * so only pools[0] is used.
     */
    AVBufferPool *pools[4];

    /*
     * Pool parameters
     */
    int format;
    int width, height;
    int stride_align[AV_NUM_DATA_POINTERS];
    int linesize[4];
    int planes;
    int channels;
    int samples;
} FramePool;

typedef struct AVCodecInternal {
    /**
     * Whether the parent AVCodecContext is a copy of the context which had
     * init() called on it.
     * This is used by multithreading - shared tables and picture pointers
     * should be freed from the original context only.
     */
    int is_copy;

    /**
     * Whether to allocate progress for frame threading.
     *
     * The codec must set it to 1 if it uses ff_thread_await/report_progress(),
     * then progress will be allocated in ff_thread_get_buffer(). The frames
     * then MUST be freed with ff_thread_release_buffer().
     *
     * If the codec does not need to call the progress functions (there are no
     * dependencies between the frames), it should leave this at 0. Then it can
     * decode straight to the user-provided frames (which the user will then
     * free with av_frame_unref()), there is no need to call
     * ff_thread_release_buffer().
     */
    int allocate_progress;

#if FF_API_OLD_ENCODE_AUDIO
    /**
     * Internal sample count used by avcodec_encode_audio() to fabricate pts.
     * Can be removed along with avcodec_encode_audio().
     */
    int64_t sample_count;
#endif

    /**
     * An audio frame with less than required samples has been submitted and
     * padded with silence. Reject all subsequent frames.
     */
    int last_audio_frame;

    AVFrame *to_free;

    FramePool *pool;

    void *thread_ctx;

    /**
     * Current packet as passed into the decoder, to avoid having to pass the
     * packet into every function.
     */
    AVPacket *pkt;

    /**
     * temporary buffer used for encoders to store their bitstream
     */
    uint8_t *byte_buffer;
    unsigned int byte_buffer_size;

    void *frame_thread_encoder;

    /**
     * Number of audio samples to skip at the start of the next decoded frame
     */
    int skip_samples;

    /**
     * hwaccel-specific private data
     */
    void *hwaccel_priv_data;
} AVCodecInternal;

struct AVCodecDefault {
    const uint8_t *key;
    const uint8_t *value;
};

extern const uint8_t ff_log2_run[41];

/**
 * Return the index into tab at which {a,b} match elements {[0],[1]} of tab.
 * If there is no such matching pair then size is returned.
 */
int ff_match_2uint16(const uint16_t (*tab)[2], int size, int a, int b);

unsigned int avpriv_toupper4(unsigned int x);

/**
 * does needed setup of pkt_pts/pos and such for (re)get_buffer();
 */
int ff_init_buffer_info(AVCodecContext *s, AVFrame *frame);


void avpriv_color_frame(AVFrame *frame, const int color[4]);

extern volatile int ff_avcodec_locked;
int ff_lock_avcodec(AVCodecContext *log_ctx);
int ff_unlock_avcodec(void);

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
int ff_alloc_packet2(AVCodecContext *avctx, AVPacket *avpkt, int64_t size);

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

/**
 * Get a buffer for a frame. This is a wrapper around
 * AVCodecContext.get_buffer() and should be used instead calling get_buffer()
 * directly.
 */
int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags);

/**
 * Identical in function to av_frame_make_writable(), except it uses
 * ff_get_buffer() to allocate the buffer when needed.
 */
int ff_reget_buffer(AVCodecContext *avctx, AVFrame *frame);

int ff_thread_can_start_frame(AVCodecContext *avctx);

int avpriv_h264_has_num_reorder_frames(AVCodecContext *avctx);

/**
 * Call avcodec_open2 recursively by decrementing counter, unlocking mutex,
 * calling the function and then restoring again. Assumes the mutex is
 * already locked
 */
int ff_codec_open2_recursive(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options);

/**
 * Finalize buf into extradata and set its size appropriately.
 */
int avpriv_bprint_to_extradata(AVCodecContext *avctx, struct AVBPrint *buf);

const uint8_t *avpriv_find_start_code(const uint8_t *p,
                                      const uint8_t *end,
                                      uint32_t *state);

/**
 * Check that the provided frame dimensions are valid and set them on the codec
 * context.
 */
int ff_set_dimensions(AVCodecContext *s, int width, int height);

/**
 * Check that the provided sample aspect ratio is valid and set it on the codec
 * context.
 */
int ff_set_sar(AVCodecContext *avctx, AVRational sar);

/**
 * Add or update AV_FRAME_DATA_MATRIXENCODING side data.
 */
int ff_side_data_update_matrix_encoding(AVFrame *frame,
                                        enum AVMatrixEncoding matrix_encoding);

/**
 * Select the (possibly hardware accelerated) pixel format.
 * This is a wrapper around AVCodecContext.get_format() and should be used
 * instead of calling get_format() directly.
 */
int ff_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt);

/**
 * Set various frame properties from the codec context / packet data.
 */
int ff_decode_frame_props(AVCodecContext *avctx, AVFrame *frame);

#endif /* AVCODEC_INTERNAL_H */
