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
 * AVFrame.pkt_dts manually. If the flag is set, decode.c won't overwrite
 * this field. If it's unset, decode.c tries to guess the pkt_dts field
 * from the input AVPacket.
 */
#define FF_CODEC_CAP_SETS_PKT_DTS           (1 << 2)
/**
 * The decoder extracts and fills its parameters even if the frame is
 * skipped due to the skip_frame setting.
 */
#define FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM  (1 << 3)
/**
 * The decoder sets the cropping fields in the output frames manually.
 * If this cap is set, the generic code will initialize output frame
 * dimensions to coded rather than display values.
 */
#define FF_CODEC_CAP_EXPORTS_CROPPING       (1 << 4)
/**
 * Codec initializes slice-based threading with a main function
 */
#define FF_CODEC_CAP_SLICE_THREAD_HAS_MF    (1 << 5)
/*
 * The codec supports frame threading and has inter-frame dependencies, so it
 * uses ff_thread_report/await_progress().
 */
#define FF_CODEC_CAP_ALLOCATE_PROGRESS      (1 << 6)

/**
 * AVCodec.codec_tags termination value
 */
#define FF_CODEC_TAGS_END -1


#ifdef TRACE
#   define ff_tlog(ctx, ...) av_log(ctx, AV_LOG_TRACE, __VA_ARGS__)
#else
#   define ff_tlog(ctx, ...) do { } while(0)
#endif


#define FF_DEFAULT_QUANT_BIAS 999999

#define FF_QSCALE_TYPE_MPEG1 0
#define FF_QSCALE_TYPE_MPEG2 1
#define FF_QSCALE_TYPE_H264  2
#define FF_QSCALE_TYPE_VP56  3

#define FF_SANE_NB_CHANNELS 512U

#define FF_SIGNBIT(x) ((x) >> CHAR_BIT * sizeof(x) - 1)

#if HAVE_SIMD_ALIGN_64
#   define STRIDE_ALIGN 64 /* AVX-512 */
#elif HAVE_SIMD_ALIGN_32
#   define STRIDE_ALIGN 32
#elif HAVE_SIMD_ALIGN_16
#   define STRIDE_ALIGN 16
#else
#   define STRIDE_ALIGN 8
#endif

typedef struct DecodeSimpleContext {
    AVPacket *in_pkt;
    AVFrame  *out_frame;
} DecodeSimpleContext;

typedef struct AVCodecInternal {
    /**
     * Whether the parent AVCodecContext is a copy of the context which had
     * init() called on it.
     * This is used by multithreading - shared tables and picture pointers
     * should be freed from the original context only.
     */
    int is_copy;

    /**
     * An audio frame with less than required samples has been submitted and
     * padded with silence. Reject all subsequent frames.
     */
    int last_audio_frame;

    AVFrame *to_free;

    AVBufferRef *pool;

    void *thread_ctx;

    DecodeSimpleContext ds;
    AVBSFContext *bsf;

    /**
     * Properties (timestamps+side data) extracted from the last packet passed
     * for decoding.
     */
    AVPacket *last_pkt_props;

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

    /**
     * checks API usage: after codec draining, flush is required to resume operation
     */
    int draining;

    /**
     * buffers for using new encode/decode API through legacy API
     */
    AVPacket *buffer_pkt;
    int buffer_pkt_valid; // encoding: packet without data can be valid
    AVFrame *buffer_frame;
    int draining_done;
    int compat_decode_warned;
    /* this variable is set by the decoder internals to signal to the old
     * API compat wrappers the amount of data consumed from the last packet */
    size_t compat_decode_consumed;
    /* when a partial packet has been consumed, this stores the remaining size
     * of the packet (that should be submitted in the next decode call */
    size_t compat_decode_partial_size;
    AVFrame *compat_decode_frame;

    int showed_multi_packet_warning;

    int skip_samples_multiplier;

    /* to prevent infinite loop on errors when draining */
    int nb_draining_errors;

    /* used when avctx flag AV_CODEC_FLAG_DROPCHANGED is set */
    int changed_frames_dropped;
    int initial_format;
    int initial_width, initial_height;
    int initial_sample_rate;
    int initial_channels;
    uint64_t initial_channel_layout;
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

void ff_color_frame(AVFrame *frame, const int color[4]);

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
        return av_int2float((x+127) << 23);
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

#define FF_REGET_BUFFER_FLAG_READONLY 1 ///< the returned buffer does not need to be writable
/**
 * Identical in function to ff_get_buffer(), except it reuses the existing buffer
 * if available.
 */
int ff_reget_buffer(AVCodecContext *avctx, AVFrame *frame, int flags);

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
 *
 * The list of pixel formats must contain at least one valid entry, and is
 * terminated with AV_PIX_FMT_NONE.  If it is possible to decode to software,
 * the last entry in the list must be the most accurate software format.
 * If it is not possible to decode to software, AVCodecContext.sw_pix_fmt
 * must be set before calling this function.
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

/**
 * Check AVFrame for A53 side data and allocate and fill SEI message with A53 info
 *
 * @param frame      Raw frame to get A53 side data from
 * @param prefix_len Number of bytes to allocate before SEI message
 * @param data       Pointer to a variable to store allocated memory
 *                   Upon return the variable will hold NULL on error or if frame has no A53 info.
 *                   Otherwise it will point to prefix_len uninitialized bytes followed by
 *                   *sei_size SEI message
 * @param sei_size   Pointer to a variable to store generated SEI message length
 * @return           Zero on success, negative error code on failure
 */
int ff_alloc_a53_sei(const AVFrame *frame, size_t prefix_len,
                     void **data, size_t *sei_size);

/**
 * Get an estimated video bitrate based on frame size, frame rate and coded
 * bits per pixel.
 */
int64_t ff_guess_coded_bitrate(AVCodecContext *avctx);

/**
 * Check if a value is in the list. If not, return the default value
 *
 * @param ctx                Context for the log msg
 * @param val_name           Name of the checked value, for log msg
 * @param array_valid_values Array of valid int, ended with INT_MAX
 * @param default_value      Value return if checked value is not in the array
 * @return                   Value or default_value.
 */
int ff_int_from_list_or_default(void *ctx, const char * val_name, int val,
                                const int * array_valid_values, int default_value);

void ff_dvdsub_parse_palette(uint32_t *palette, const char *p);

#if defined(_WIN32) && CONFIG_SHARED && !defined(BUILDING_avcodec)
#    define av_export_avcodec __declspec(dllimport)
#else
#    define av_export_avcodec
#endif

#endif /* AVCODEC_INTERNAL_H */
