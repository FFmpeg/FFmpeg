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

#define FF_SANE_NB_CHANNELS 512U

#if HAVE_SIMD_ALIGN_64
#   define STRIDE_ALIGN 64 /* AVX-512 */
#elif HAVE_SIMD_ALIGN_32
#   define STRIDE_ALIGN 32
#elif HAVE_SIMD_ALIGN_16
#   define STRIDE_ALIGN 16
#else
#   define STRIDE_ALIGN 8
#endif

typedef struct AVCodecInternal {
    /**
     * When using frame-threaded decoding, this field is set for the first
     * worker thread (e.g. to decode extradata just once).
     */
    int is_copy;

    /**
     * An audio frame with less than required samples has been submitted and
     * padded with silence. Reject all subsequent frames.
     */
    int last_audio_frame;

    AVBufferRef *pool;

    void *thread_ctx;

    /**
     * This packet is used to hold the packet given to decoders
     * implementing the .decode API; it is unused by the generic
     * code for decoders implementing the .receive_frame API and
     * may be freely used (but not freed) by them with the caveat
     * that the packet will be unreferenced generically in
     * avcodec_flush_buffers().
     */
    AVPacket *in_pkt;
    struct AVBSFContext *bsf;

    /**
     * Properties (timestamps+side data) extracted from the last packet passed
     * for decoding.
     */
    AVPacket *last_pkt_props;
    struct AVFifo *pkt_props;

    /**
     * temporary buffer used for encoders to store their bitstream
     */
    uint8_t *byte_buffer;
    unsigned int byte_buffer_size;

    /**
     * This is set to AV_PKT_FLAG_KEY for encoders that encode intra-only
     * formats (i.e. whose codec descriptor has AV_CODEC_PROP_INTRA_ONLY set).
     * This is used to set said flag generically for said encoders.
     */
    int intra_only_flag;

    void *frame_thread_encoder;

    /**
     * The input frame is stored here for encoders implementing the simple
     * encode API.
     *
     * Not allocated in other cases.
     */
    AVFrame *in_frame;

    /**
     * If this is set, then FFCodec->close (if existing) needs to be called
     * for the parent AVCodecContext.
     */
    int needs_close;

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
    AVFrame *buffer_frame;
    int draining_done;

    int showed_multi_packet_warning;

    int skip_samples_multiplier;

    /* to prevent infinite loop on errors when draining */
    int nb_draining_errors;

    /* used when avctx flag AV_CODEC_FLAG_DROPCHANGED is set */
    int changed_frames_dropped;
    int initial_format;
    int initial_width, initial_height;
    int initial_sample_rate;
#if FF_API_OLD_CHANNEL_LAYOUT
    int initial_channels;
    uint64_t initial_channel_layout;
#endif
    AVChannelLayout initial_ch_layout;
} AVCodecInternal;

/**
 * Return the index into tab at which {a,b} match elements {[0],[1]} of tab.
 * If there is no such matching pair then size is returned.
 */
int ff_match_2uint16(const uint16_t (*tab)[2], int size, int a, int b);

unsigned int ff_toupper4(unsigned int x);

void ff_color_frame(AVFrame *frame, const int color[4]);

/**
 * Maximum size in bytes of extradata.
 * This value was chosen such that every bit of the buffer is
 * addressable by a 32-bit signed integer as used by get_bits.
 */
#define FF_MAX_EXTRADATA_SIZE ((1 << 28) - AV_INPUT_BUFFER_PADDING_SIZE)

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
 * Add a CPB properties side data to an encoding context.
 */
AVCPBProperties *ff_add_cpb_side_data(AVCodecContext *avctx);

/**
 * Check AVFrame for S12M timecode side data and allocate and fill TC SEI message with timecode info
 *
 * @param frame      Raw frame to get S12M timecode side data from
 * @param rate       The frame rate
 * @param prefix_len Number of bytes to allocate before SEI message
 * @param data       Pointer to a variable to store allocated memory
 *                   Upon return the variable will hold NULL on error or if frame has no S12M timecode info.
 *                   Otherwise it will point to prefix_len uninitialized bytes followed by
 *                   *sei_size SEI message
 * @param sei_size   Pointer to a variable to store generated SEI message length
 * @return           Zero on success, negative error code on failure
 */
int ff_alloc_timecode_sei(const AVFrame *frame, AVRational rate, size_t prefix_len,
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

#endif /* AVCODEC_INTERNAL_H */
