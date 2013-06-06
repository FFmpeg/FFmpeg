/*
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
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

/**
 * @file
 * ALAC (Apple Lossless Audio Codec) decoder
 * @author 2005 David Hammerton
 * @see http://crazney.net/programs/itunes/alac.html
 *
 * Note: This decoder expects a 36-byte QuickTime atom to be
 * passed through the extradata[_size] fields. This atom is tacked onto
 * the end of an 'alac' stsd atom and has the following format:
 *
 * 32bit  atom size
 * 32bit  tag                  ("alac")
 * 32bit  tag version          (0)
 * 32bit  samples per frame    (used when not set explicitly in the frames)
 *  8bit  compatible version   (0)
 *  8bit  sample size
 *  8bit  history mult         (40)
 *  8bit  initial history      (10)
 *  8bit  rice param limit     (14)
 *  8bit  channels
 * 16bit  maxRun               (255)
 * 32bit  max coded frame size (0 means unknown)
 * 32bit  average bitrate      (0 means unknown)
 * 32bit  samplerate
 */

#include "libavutil/channel_layout.h"
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "internal.h"
#include "thread.h"
#include "unary.h"
#include "mathops.h"
#include "alac_data.h"

#define ALAC_EXTRADATA_SIZE 36

typedef struct {
    AVCodecContext *avctx;
    GetBitContext gb;
    int channels;

    int32_t *predict_error_buffer[2];
    int32_t *output_samples_buffer[2];
    int32_t *extra_bits_buffer[2];

    uint32_t max_samples_per_frame;
    uint8_t  sample_size;
    uint8_t  rice_history_mult;
    uint8_t  rice_initial_history;
    uint8_t  rice_limit;

    int extra_bits;     /**< number of extra bits beyond 16-bit */
    int nb_samples;     /**< number of samples in the current frame */

    int direct_output;
} ALACContext;

static inline unsigned int decode_scalar(GetBitContext *gb, int k, int bps)
{
    unsigned int x = get_unary_0_9(gb);

    if (x > 8) { /* RICE THRESHOLD */
        /* use alternative encoding */
        x = get_bits_long(gb, bps);
    } else if (k != 1) {
        int extrabits = show_bits(gb, k);

        /* multiply x by 2^k - 1, as part of their strange algorithm */
        x = (x << k) - x;

        if (extrabits > 1) {
            x += extrabits - 1;
            skip_bits(gb, k);
        } else
            skip_bits(gb, k - 1);
    }
    return x;
}

static int rice_decompress(ALACContext *alac, int32_t *output_buffer,
                            int nb_samples, int bps, int rice_history_mult)
{
    int i;
    unsigned int history = alac->rice_initial_history;
    int sign_modifier = 0;

    for (i = 0; i < nb_samples; i++) {
        int k;
        unsigned int x;

        if(get_bits_left(&alac->gb) <= 0)
            return -1;

        /* calculate rice param and decode next value */
        k = av_log2((history >> 9) + 3);
        k = FFMIN(k, alac->rice_limit);
        x = decode_scalar(&alac->gb, k, bps);
        x += sign_modifier;
        sign_modifier = 0;
        output_buffer[i] = (x >> 1) ^ -(x & 1);

        /* update the history */
        if (x > 0xffff)
            history = 0xffff;
        else
            history +=         x * rice_history_mult -
                       ((history * rice_history_mult) >> 9);

        /* special case: there may be compressed blocks of 0 */
        if ((history < 128) && (i + 1 < nb_samples)) {
            int block_size;

            /* calculate rice param and decode block size */
            k = 7 - av_log2(history) + ((history + 16) >> 6);
            k = FFMIN(k, alac->rice_limit);
            block_size = decode_scalar(&alac->gb, k, 16);

            if (block_size > 0) {
                if (block_size >= nb_samples - i) {
                    av_log(alac->avctx, AV_LOG_ERROR,
                           "invalid zero block size of %d %d %d\n", block_size,
                           nb_samples, i);
                    block_size = nb_samples - i - 1;
                }
                memset(&output_buffer[i + 1], 0,
                       block_size * sizeof(*output_buffer));
                i += block_size;
            }
            if (block_size <= 0xffff)
                sign_modifier = 1;
            history = 0;
        }
    }
    return 0;
}

static inline int sign_only(int v)
{
    return v ? FFSIGN(v) : 0;
}

static void lpc_prediction(int32_t *error_buffer, int32_t *buffer_out,
                           int nb_samples, int bps, int16_t *lpc_coefs,
                           int lpc_order, int lpc_quant)
{
    int i;
    int32_t *pred = buffer_out;

    /* first sample always copies */
    *buffer_out = *error_buffer;

    if (nb_samples <= 1)
        return;

    if (!lpc_order) {
        memcpy(&buffer_out[1], &error_buffer[1],
               (nb_samples - 1) * sizeof(*buffer_out));
        return;
    }

    if (lpc_order == 31) {
        /* simple 1st-order prediction */
        for (i = 1; i < nb_samples; i++) {
            buffer_out[i] = sign_extend(buffer_out[i - 1] + error_buffer[i],
                                        bps);
        }
        return;
    }

    /* read warm-up samples */
    for (i = 1; i <= lpc_order && i < nb_samples; i++)
        buffer_out[i] = sign_extend(buffer_out[i - 1] + error_buffer[i], bps);

    /* NOTE: 4 and 8 are very common cases that could be optimized. */

    for (; i < nb_samples; i++) {
        int j;
        int val = 0;
        int error_val = error_buffer[i];
        int error_sign;
        int d = *pred++;

        /* LPC prediction */
        for (j = 0; j < lpc_order; j++)
            val += (pred[j] - d) * lpc_coefs[j];
        val = (val + (1 << (lpc_quant - 1))) >> lpc_quant;
        val += d + error_val;
        buffer_out[i] = sign_extend(val, bps);

        /* adapt LPC coefficients */
        error_sign = sign_only(error_val);
        if (error_sign) {
            for (j = 0; j < lpc_order && error_val * error_sign > 0; j++) {
                int sign;
                val  = d - pred[j];
                sign = sign_only(val) * error_sign;
                lpc_coefs[j] -= sign;
                val *= sign;
                error_val -= (val >> lpc_quant) * (j + 1);
            }
        }
    }
}

static void decorrelate_stereo(int32_t *buffer[2], int nb_samples,
                               int decorr_shift, int decorr_left_weight)
{
    int i;

    for (i = 0; i < nb_samples; i++) {
        int32_t a, b;

        a = buffer[0][i];
        b = buffer[1][i];

        a -= (b * decorr_left_weight) >> decorr_shift;
        b += a;

        buffer[0][i] = b;
        buffer[1][i] = a;
    }
}

static void append_extra_bits(int32_t *buffer[2], int32_t *extra_bits_buffer[2],
                              int extra_bits, int channels, int nb_samples)
{
    int i, ch;

    for (ch = 0; ch < channels; ch++)
        for (i = 0; i < nb_samples; i++)
            buffer[ch][i] = (buffer[ch][i] << extra_bits) | extra_bits_buffer[ch][i];
}

static int decode_element(AVCodecContext *avctx, AVFrame *frame, int ch_index,
                          int channels)
{
    ALACContext *alac = avctx->priv_data;
    int has_size, bps, is_compressed, decorr_shift, decorr_left_weight, ret;
    uint32_t output_samples;
    int i, ch;

    skip_bits(&alac->gb, 4);  /* element instance tag */
    skip_bits(&alac->gb, 12); /* unused header bits */

    /* the number of output samples is stored in the frame */
    has_size = get_bits1(&alac->gb);

    alac->extra_bits = get_bits(&alac->gb, 2) << 3;
    bps = alac->sample_size - alac->extra_bits + channels - 1;
    if (bps > 32U) {
        av_log(avctx, AV_LOG_ERROR, "bps is unsupported: %d\n", bps);
        return AVERROR_PATCHWELCOME;
    }

    /* whether the frame is compressed */
    is_compressed = !get_bits1(&alac->gb);

    if (has_size)
        output_samples = get_bits_long(&alac->gb, 32);
    else
        output_samples = alac->max_samples_per_frame;
    if (!output_samples || output_samples > alac->max_samples_per_frame) {
        av_log(avctx, AV_LOG_ERROR, "invalid samples per frame: %d\n",
               output_samples);
        return AVERROR_INVALIDDATA;
    }
    if (!alac->nb_samples) {
        ThreadFrame tframe = { .f = frame };
        /* get output buffer */
        frame->nb_samples = output_samples;
        if ((ret = ff_thread_get_buffer(avctx, &tframe, 0)) < 0)
            return ret;
    } else if (output_samples != alac->nb_samples) {
        av_log(avctx, AV_LOG_ERROR, "sample count mismatch: %u != %d\n",
               output_samples, alac->nb_samples);
        return AVERROR_INVALIDDATA;
    }
    alac->nb_samples = output_samples;
    if (alac->direct_output) {
        for (ch = 0; ch < channels; ch++)
            alac->output_samples_buffer[ch] = (int32_t *)frame->extended_data[ch_index + ch];
    }

    if (is_compressed) {
        int16_t lpc_coefs[2][32];
        int lpc_order[2];
        int prediction_type[2];
        int lpc_quant[2];
        int rice_history_mult[2];

        decorr_shift       = get_bits(&alac->gb, 8);
        decorr_left_weight = get_bits(&alac->gb, 8);

        for (ch = 0; ch < channels; ch++) {
            prediction_type[ch]   = get_bits(&alac->gb, 4);
            lpc_quant[ch]         = get_bits(&alac->gb, 4);
            rice_history_mult[ch] = get_bits(&alac->gb, 3);
            lpc_order[ch]         = get_bits(&alac->gb, 5);

            /* read the predictor table */
            for (i = lpc_order[ch] - 1; i >= 0; i--)
                lpc_coefs[ch][i] = get_sbits(&alac->gb, 16);
        }

        if (alac->extra_bits) {
            for (i = 0; i < alac->nb_samples; i++) {
                if(get_bits_left(&alac->gb) <= 0)
                    return -1;
                for (ch = 0; ch < channels; ch++)
                    alac->extra_bits_buffer[ch][i] = get_bits(&alac->gb, alac->extra_bits);
            }
        }
        for (ch = 0; ch < channels; ch++) {
            int ret=rice_decompress(alac, alac->predict_error_buffer[ch],
                            alac->nb_samples, bps,
                            rice_history_mult[ch] * alac->rice_history_mult / 4);
            if(ret<0)
                return ret;

            /* adaptive FIR filter */
            if (prediction_type[ch] == 15) {
                /* Prediction type 15 runs the adaptive FIR twice.
                 * The first pass uses the special-case coef_num = 31, while
                 * the second pass uses the coefs from the bitstream.
                 *
                 * However, this prediction type is not currently used by the
                 * reference encoder.
                 */
                lpc_prediction(alac->predict_error_buffer[ch],
                               alac->predict_error_buffer[ch],
                               alac->nb_samples, bps, NULL, 31, 0);
            } else if (prediction_type[ch] > 0) {
                av_log(avctx, AV_LOG_WARNING, "unknown prediction type: %i\n",
                       prediction_type[ch]);
            }
            lpc_prediction(alac->predict_error_buffer[ch],
                           alac->output_samples_buffer[ch], alac->nb_samples,
                           bps, lpc_coefs[ch], lpc_order[ch], lpc_quant[ch]);
        }
    } else {
        /* not compressed, easy case */
        for (i = 0; i < alac->nb_samples; i++) {
            if(get_bits_left(&alac->gb) <= 0)
                return -1;
            for (ch = 0; ch < channels; ch++) {
                alac->output_samples_buffer[ch][i] =
                         get_sbits_long(&alac->gb, alac->sample_size);
            }
        }
        alac->extra_bits   = 0;
        decorr_shift       = 0;
        decorr_left_weight = 0;
    }

    if (channels == 2 && decorr_left_weight) {
        decorrelate_stereo(alac->output_samples_buffer, alac->nb_samples,
                           decorr_shift, decorr_left_weight);
    }

    if (alac->extra_bits) {
        append_extra_bits(alac->output_samples_buffer, alac->extra_bits_buffer,
                          alac->extra_bits, channels, alac->nb_samples);
    }

    if(av_sample_fmt_is_planar(avctx->sample_fmt)) {
    switch(alac->sample_size) {
    case 16: {
        for (ch = 0; ch < channels; ch++) {
            int16_t *outbuffer = (int16_t *)frame->extended_data[ch_index + ch];
            for (i = 0; i < alac->nb_samples; i++)
                *outbuffer++ = alac->output_samples_buffer[ch][i];
        }}
        break;
    case 24: {
        for (ch = 0; ch < channels; ch++) {
            for (i = 0; i < alac->nb_samples; i++)
                alac->output_samples_buffer[ch][i] <<= 8;
        }}
        break;
    }
    }else{
        switch(alac->sample_size) {
        case 16: {
            int16_t *outbuffer = ((int16_t *)frame->extended_data[0]) + ch_index;
            for (i = 0; i < alac->nb_samples; i++) {
                for (ch = 0; ch < channels; ch++)
                    *outbuffer++ = alac->output_samples_buffer[ch][i];
                outbuffer += alac->channels - channels;
            }
            }
            break;
        case 24: {
            int32_t *outbuffer = ((int32_t *)frame->extended_data[0]) + ch_index;
            for (i = 0; i < alac->nb_samples; i++) {
                for (ch = 0; ch < channels; ch++)
                    *outbuffer++ = alac->output_samples_buffer[ch][i] << 8;
                outbuffer += alac->channels - channels;
            }
            }
            break;
        case 32: {
            int32_t *outbuffer = ((int32_t *)frame->extended_data[0]) + ch_index;
            for (i = 0; i < alac->nb_samples; i++) {
                for (ch = 0; ch < channels; ch++)
                    *outbuffer++ = alac->output_samples_buffer[ch][i];
                outbuffer += alac->channels - channels;
            }
            }
            break;
        }
    }

    return 0;
}

static int alac_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    ALACContext *alac = avctx->priv_data;
    AVFrame *frame    = data;
    enum AlacRawDataBlockType element;
    int channels;
    int ch, ret, got_end;

    if ((ret = init_get_bits8(&alac->gb, avpkt->data, avpkt->size)) < 0)
        return ret;

    got_end = 0;
    alac->nb_samples = 0;
    ch = 0;
    while (get_bits_left(&alac->gb) >= 3) {
        element = get_bits(&alac->gb, 3);
        if (element == TYPE_END) {
            got_end = 1;
            break;
        }
        if (element > TYPE_CPE && element != TYPE_LFE) {
            av_log(avctx, AV_LOG_ERROR, "syntax element unsupported: %d\n", element);
            return AVERROR_PATCHWELCOME;
        }

        channels = (element == TYPE_CPE) ? 2 : 1;
        if (   ch + channels > alac->channels
            || ff_alac_channel_layout_offsets[alac->channels - 1][ch] + channels > alac->channels
        ) {
            av_log(avctx, AV_LOG_ERROR, "invalid element channel count\n");
            return AVERROR_INVALIDDATA;
        }

        ret = decode_element(avctx, frame,
                             ff_alac_channel_layout_offsets[alac->channels - 1][ch],
                             channels);
        if (ret < 0 && get_bits_left(&alac->gb))
            return ret;

        ch += channels;
    }
    if (!got_end) {
        av_log(avctx, AV_LOG_ERROR, "no end tag found. incomplete packet.\n");
        return AVERROR_INVALIDDATA;
    }

    if (avpkt->size * 8 - get_bits_count(&alac->gb) > 8) {
        av_log(avctx, AV_LOG_ERROR, "Error : %d bits left\n",
               avpkt->size * 8 - get_bits_count(&alac->gb));
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold int alac_decode_close(AVCodecContext *avctx)
{
    ALACContext *alac = avctx->priv_data;

    int ch;
    for (ch = 0; ch < FFMIN(alac->channels, 2); ch++) {
        av_freep(&alac->predict_error_buffer[ch]);
        if (!alac->direct_output)
            av_freep(&alac->output_samples_buffer[ch]);
        av_freep(&alac->extra_bits_buffer[ch]);
    }

    return 0;
}

static int allocate_buffers(ALACContext *alac)
{
    int ch;
    int buf_size;

    if (alac->max_samples_per_frame > INT_MAX / sizeof(int32_t))
        goto buf_alloc_fail;
    buf_size = alac->max_samples_per_frame * sizeof(int32_t);

    for (ch = 0; ch < FFMIN(alac->channels, 2); ch++) {
        FF_ALLOC_OR_GOTO(alac->avctx, alac->predict_error_buffer[ch],
                         buf_size, buf_alloc_fail);

        alac->direct_output = alac->sample_size > 16 && av_sample_fmt_is_planar(alac->avctx->sample_fmt);
        if (!alac->direct_output) {
            FF_ALLOC_OR_GOTO(alac->avctx, alac->output_samples_buffer[ch],
                             buf_size, buf_alloc_fail);
        }

        FF_ALLOC_OR_GOTO(alac->avctx, alac->extra_bits_buffer[ch],
                         buf_size, buf_alloc_fail);
    }
    return 0;
buf_alloc_fail:
    alac_decode_close(alac->avctx);
    return AVERROR(ENOMEM);
}

static int alac_set_info(ALACContext *alac)
{
    GetByteContext gb;

    bytestream2_init(&gb, alac->avctx->extradata,
                     alac->avctx->extradata_size);

    bytestream2_skipu(&gb, 12); // size:4, alac:4, version:4

    alac->max_samples_per_frame = bytestream2_get_be32u(&gb);
    if (!alac->max_samples_per_frame || alac->max_samples_per_frame > INT_MAX) {
        av_log(alac->avctx, AV_LOG_ERROR, "max samples per frame invalid: %u\n",
               alac->max_samples_per_frame);
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skipu(&gb, 1);  // compatible version
    alac->sample_size          = bytestream2_get_byteu(&gb);
    alac->rice_history_mult    = bytestream2_get_byteu(&gb);
    alac->rice_initial_history = bytestream2_get_byteu(&gb);
    alac->rice_limit           = bytestream2_get_byteu(&gb);
    alac->channels             = bytestream2_get_byteu(&gb);
    bytestream2_get_be16u(&gb); // maxRun
    bytestream2_get_be32u(&gb); // max coded frame size
    bytestream2_get_be32u(&gb); // average bitrate
    bytestream2_get_be32u(&gb); // samplerate

    return 0;
}

static av_cold int alac_decode_init(AVCodecContext * avctx)
{
    int ret;
    int req_packed;
    ALACContext *alac = avctx->priv_data;
    alac->avctx = avctx;

    /* initialize from the extradata */
    if (alac->avctx->extradata_size < ALAC_EXTRADATA_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "extradata is too small\n");
        return AVERROR_INVALIDDATA;
    }
    if (alac_set_info(alac)) {
        av_log(avctx, AV_LOG_ERROR, "set_info failed\n");
        return -1;
    }

    req_packed = LIBAVCODEC_VERSION_MAJOR < 55 && !av_sample_fmt_is_planar(avctx->request_sample_fmt);
    switch (alac->sample_size) {
    case 16: avctx->sample_fmt = req_packed ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S16P;
             break;
    case 24:
    case 32: avctx->sample_fmt = req_packed ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S32P;
             break;
    default: avpriv_request_sample(avctx, "Sample depth %d", alac->sample_size);
             return AVERROR_PATCHWELCOME;
    }
    avctx->bits_per_raw_sample = alac->sample_size;

    if (alac->channels < 1) {
        av_log(avctx, AV_LOG_WARNING, "Invalid channel count\n");
        alac->channels = avctx->channels;
    } else {
        if (alac->channels > ALAC_MAX_CHANNELS)
            alac->channels = avctx->channels;
        else
            avctx->channels = alac->channels;
    }
    if (avctx->channels > ALAC_MAX_CHANNELS || avctx->channels <= 0 ) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported channel count: %d\n",
               avctx->channels);
        return AVERROR_PATCHWELCOME;
    }
    avctx->channel_layout = ff_alac_channel_layouts[alac->channels - 1];

    if ((ret = allocate_buffers(alac)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating buffers\n");
        return ret;
    }

    return 0;
}

static int init_thread_copy(AVCodecContext *avctx)
{
    ALACContext *alac = avctx->priv_data;
    return allocate_buffers(alac);
}

AVCodec ff_alac_decoder = {
    .name           = "alac",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ALAC,
    .priv_data_size = sizeof(ALACContext),
    .init           = alac_decode_init,
    .close          = alac_decode_close,
    .decode         = alac_decode_frame,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(init_thread_copy),
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("ALAC (Apple Lossless Audio Codec)"),
};
