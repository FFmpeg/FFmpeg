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

/**
 * The codec does not modify any global variables in the init function,
 * allowing to call the init function without locking any global mutexes.
 */
#define FF_CODEC_CAP_INIT_THREADSAFE        (1 << 0)
/**
 * The codec allows calling the close function for deallocation even if
 * the init function returned a failure. Without this capability flag, a
 * codec does such cleanup internally when returning failures from the
 * init function and does not expect the close function to be called at
 * all.
 */
#define FF_CODEC_CAP_INIT_CLEANUP           (1 << 1)
/**
 * Decoders marked with FF_CODEC_CAP_SETS_PKT_DTS want to set
 * AVFrame.pkt_dts manually. If the flag is set, utils.c won't overwrite
 * this field. If it's unset, utils.c tries to guess the pkt_dts field
 * from the input AVPacket.
 */
#define FF_CODEC_CAP_SETS_PKT_DTS           (1 << 2)
/**
 * The decoder extracts and fills its parameters even if the frame is
 * skipped due to the skip_frame setting.
 */
#define FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM  (1 << 3)

#ifdef TRACE
#   define ff_tlog(ctx, ...) av_log(ctx, AV_LOG_TRACE, __VA_ARGS__)
#else
#   define ff_tlog(ctx, ...) do { } while(0)
#endif


#if !FF_API_QUANT_BIAS
#define FF_DEFAULT_QUANT_BIAS 999999
#endif

#define FF_SANE_NB_CHANNELS 64U

#define FF_SIGNBIT(x) ((x) >> CHAR_BIT * sizeof(x) - 1)

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


void ff_color_frame(AVFrame *frame, const int color[4]);

extern volatile int ff_avcodec_locked;
int ff_lock_avcodec(AVCodecContext *log_ctx, const AVCodec *codec);
int ff_unlock_avcodec(const AVCodec *codec);

int avpriv_lock_avformat(void);
int avpriv_unlock_avformat(void);

/**
 * Maximum size in bytes of extradata.
 * This value was chosen such that every bit of the buffer is
 * addressable by a 32-bit signed integer as used by get_bits.
 */
#define FF_MAX_EXTRADATA_SIZE ((1 << 28) - AV_INPUT_BUFFER_PADDING_SIZE)

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
 * @param min_size This is a hint to the allocation algorithm, which indicates
 *                to what minimal size the caller might later shrink the packet
 *                to. Encoders often allocate packets which are larger than the
 *                amount of data that is written into them as the exact amount is
 *                not known at the time of allocation. min_size represents the
 *                size a packet might be shrunk to by the caller. Can be set to
 *                0. setting this roughly correctly allows the allocation code
 *                to choose between several allocation strategies to improve
 *                speed slightly.
 * @return        non negative on success, negative error code on failure
 */
int ff_alloc_packet2(AVCodecContext *avctx, AVPacket *avpkt, int64_t size, int64_t min_size);

attribute_deprecated int ff_alloc_packet(AVPacket *avpkt, int size);

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
 * 2^(x) for integer x
 * @return correctly rounded float
 */
static av_always_inline float ff_exp2fi(int x) {
    /* Normal range */
    if (-126 <= x && x <= 128)
        return av_int2float(x+127 << 23);
    /* Too large */
    else if (x > 128)
        return INFINITY;
    /* Subnormal numbers */
    else if (x > -150)
        return av_int2float(1 << (x+149));
    /* Negligibly small */
    else
        return 0;
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

int avpriv_codec_get_cap_skip_frame_fill_param(const AVCodec *codec);

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

/**
 * Add a CPB properties side data to an encoding context.
 */
AVCPBProperties *ff_add_cpb_side_data(AVCodecContext *avctx);

int ff_side_data_set_encoder_stats(AVPacket *pkt, int quality, int64_t *error, int error_count, int pict_type);

#endif /* AVCODEC_INTERNAL_H */
