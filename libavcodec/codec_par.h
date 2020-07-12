/*
 * Codec parameters public API
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

#ifndef AVCODEC_CODEC_PAR_H
#define AVCODEC_CODEC_PAR_H

#include <stdint.h>

#include "libavutil/avutil.h"
#include "libavutil/rational.h"
#include "libavutil/pixfmt.h"

#include "codec_id.h"

/**
 * @addtogroup lavc_core
 */

enum AVFieldOrder {
    AV_FIELD_UNKNOWN,
    AV_FIELD_PROGRESSIVE,
    AV_FIELD_TT,          //< Top coded_first, top displayed first
    AV_FIELD_BB,          //< Bottom coded first, bottom displayed first
    AV_FIELD_TB,          //< Top coded first, bottom displayed first
    AV_FIELD_BT,          //< Bottom coded first, top displayed first
};

/**
 * This struct describes the properties of an encoded stream.
 *
 * sizeof(AVCodecParameters) is not a part of the public ABI, this struct must
 * be allocated with avcodec_parameters_alloc() and freed with
 * avcodec_parameters_free().
 */
typedef struct AVCodecParameters {
    /**
     * General type of the encoded data.
     */
    enum AVMediaType codec_type;
    /**
     * Specific type of the encoded data (the codec used).
     */
    enum AVCodecID   codec_id;
    /**
     * Additional information about the codec (corresponds to the AVI FOURCC).
     */
    uint32_t         codec_tag;

    /**
     * Extra binary data needed for initializing the decoder, codec-dependent.
     *
     * Must be allocated with av_malloc() and will be freed by
     * avcodec_parameters_free(). The allocated size of extradata must be at
     * least extradata_size + AV_INPUT_BUFFER_PADDING_SIZE, with the padding
     * bytes zeroed.
     */
    uint8_t *extradata;
    /**
     * Size of the extradata content in bytes.
     */
    int      extradata_size;

    /**
     * - video: the pixel format, the value corresponds to enum AVPixelFormat.
     * - audio: the sample format, the value corresponds to enum AVSampleFormat.
     */
    int format;

    /**
     * The average bitrate of the encoded data (in bits per second).
     */
    int64_t bit_rate;

    /**
     * The number of bits per sample in the codedwords.
     *
     * This is basically the bitrate per sample. It is mandatory for a bunch of
     * formats to actually decode them. It's the number of bits for one sample in
     * the actual coded bitstream.
     *
     * This could be for example 4 for ADPCM
     * For PCM formats this matches bits_per_raw_sample
     * Can be 0
     */
    int bits_per_coded_sample;

    /**
     * This is the number of valid bits in each output sample. If the
     * sample format has more bits, the least significant bits are additional
     * padding bits, which are always 0. Use right shifts to reduce the sample
     * to its actual size. For example, audio formats with 24 bit samples will
     * have bits_per_raw_sample set to 24, and format set to AV_SAMPLE_FMT_S32.
     * To get the original sample use "(int32_t)sample >> 8"."
     *
     * For ADPCM this might be 12 or 16 or similar
     * Can be 0
     */
    int bits_per_raw_sample;

    /**
     * Codec-specific bitstream restrictions that the stream conforms to.
     */
    int profile;
    int level;

    /**
     * Video only. The dimensions of the video frame in pixels.
     */
    int width;
    int height;

    /**
     * Video only. The aspect ratio (width / height) which a single pixel
     * should have when displayed.
     *
     * When the aspect ratio is unknown / undefined, the numerator should be
     * set to 0 (the denominator may have any value).
     */
    AVRational sample_aspect_ratio;

    /**
     * Video only. The order of the fields in interlaced video.
     */
    enum AVFieldOrder                  field_order;

    /**
     * Video only. Additional colorspace characteristics.
     */
    enum AVColorRange                  color_range;
    enum AVColorPrimaries              color_primaries;
    enum AVColorTransferCharacteristic color_trc;
    enum AVColorSpace                  color_space;
    enum AVChromaLocation              chroma_location;

    /**
     * Video only. Number of delayed frames.
     */
    int video_delay;

    /**
     * Audio only. The channel layout bitmask. May be 0 if the channel layout is
     * unknown or unspecified, otherwise the number of bits set must be equal to
     * the channels field.
     */
    uint64_t channel_layout;
    /**
     * Audio only. The number of audio channels.
     */
    int      channels;
    /**
     * Audio only. The number of audio samples per second.
     */
    int      sample_rate;
    /**
     * Audio only. The number of bytes per coded audio frame, required by some
     * formats.
     *
     * Corresponds to nBlockAlign in WAVEFORMATEX.
     */
    int      block_align;
    /**
     * Audio only. Audio frame size, if known. Required by some formats to be static.
     */
    int      frame_size;

    /**
     * Audio only. The amount of padding (in samples) inserted by the encoder at
     * the beginning of the audio. I.e. this number of leading decoded samples
     * must be discarded by the caller to get the original audio without leading
     * padding.
     */
    int initial_padding;
    /**
     * Audio only. The amount of padding (in samples) appended by the encoder to
     * the end of the audio. I.e. this number of decoded samples must be
     * discarded by the caller from the end of the stream to get the original
     * audio without any trailing padding.
     */
    int trailing_padding;
    /**
     * Audio only. Number of samples to skip after a discontinuity.
     */
    int seek_preroll;
} AVCodecParameters;

/**
 * Allocate a new AVCodecParameters and set its fields to default values
 * (unknown/invalid/0). The returned struct must be freed with
 * avcodec_parameters_free().
 */
AVCodecParameters *avcodec_parameters_alloc(void);

/**
 * Free an AVCodecParameters instance and everything associated with it and
 * write NULL to the supplied pointer.
 */
void avcodec_parameters_free(AVCodecParameters **par);

/**
 * Copy the contents of src to dst. Any allocated fields in dst are freed and
 * replaced with newly allocated duplicates of the corresponding fields in src.
 *
 * @return >= 0 on success, a negative AVERROR code on failure.
 */
int avcodec_parameters_copy(AVCodecParameters *dst, const AVCodecParameters *src);


/**
 * @}
 */

#endif // AVCODEC_CODEC_PAR_H
