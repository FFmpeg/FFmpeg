/*
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
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
 *  8bit  initial history      (14)
 *  8bit  rice param limit     (10)
 *  8bit  channels
 * 16bit  maxRun               (255)
 * 32bit  max coded frame size (0 means unknown)
 * 32bit  average bitrate      (0 means unknown)
 * 32bit  samplerate
 */


#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "unary.h"
#include "mathops.h"

#define ALAC_EXTRADATA_SIZE 36
#define MAX_CHANNELS 2

typedef struct {

    AVCodecContext *avctx;
    AVFrame frame;
    GetBitContext gb;

    int channels;

    /* buffers */
    int32_t *predict_error_buffer[MAX_CHANNELS];
    int32_t *output_samples_buffer[MAX_CHANNELS];
    int32_t *extra_bits_buffer[MAX_CHANNELS];

    uint32_t max_samples_per_frame;
    uint8_t  sample_size;
    uint8_t  rice_history_mult;
    uint8_t  rice_initial_history;
    uint8_t  rice_limit;

    int extra_bits;                         /**< number of extra bits beyond 16-bit */
    int nb_samples;                         /**< number of samples in the current frame */
} ALACContext;

static inline int decode_scalar(GetBitContext *gb, int k, int readsamplesize)
{
    int x = get_unary_0_9(gb);

    if (x > 8) { /* RICE THRESHOLD */
        /* use alternative encoding */
        x = get_bits(gb, readsamplesize);
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

static void bastardized_rice_decompress(ALACContext *alac,
                                        int32_t *output_buffer,
                                        int output_size,
                                        int readsamplesize,
                                        int rice_history_mult)
{
    int output_count;
    unsigned int history = alac->rice_initial_history;
    int sign_modifier = 0;

    for (output_count = 0; output_count < output_size; output_count++) {
        int x, k;

        /* read k, that is bits as is */
        k = av_log2((history >> 9) + 3);
        k = FFMIN(k, alac->rice_limit);
        x = decode_scalar(&alac->gb, k, readsamplesize);
        x += sign_modifier;
        sign_modifier = 0;

        output_buffer[output_count] = (x >> 1) ^ -(x & 1);

        /* now update the history */
        if (x > 0xffff)
            history = 0xffff;
        else
            history +=         x * rice_history_mult -
                       ((history * rice_history_mult) >> 9);

        /* special case: there may be compressed blocks of 0 */
        if ((history < 128) && (output_count+1 < output_size)) {
            int block_size;

            k = 7 - av_log2(history) + ((history + 16) >> 6 /* / 64 */);
            k = FFMIN(k, alac->rice_limit);

            block_size = decode_scalar(&alac->gb, k, 16);

            if (block_size > 0) {
                if(block_size >= output_size - output_count){
                    av_log(alac->avctx, AV_LOG_ERROR, "invalid zero block size of %d %d %d\n", block_size, output_size, output_count);
                    block_size= output_size - output_count - 1;
                }
                memset(&output_buffer[output_count + 1], 0,
                       block_size * sizeof(*output_buffer));
                output_count += block_size;
            }

            if (block_size <= 0xffff)
                sign_modifier = 1;

            history = 0;
        }
    }
}

static inline int sign_only(int v)
{
    return v ? FFSIGN(v) : 0;
}

static void predictor_decompress_fir_adapt(int32_t *error_buffer,
                                           int32_t *buffer_out,
                                           int output_size,
                                           int readsamplesize,
                                           int16_t *predictor_coef_table,
                                           int predictor_coef_num,
                                           int predictor_quantitization)
{
    int i;

    /* first sample always copies */
    *buffer_out = *error_buffer;

    if (output_size <= 1)
        return;

    if (!predictor_coef_num) {
        memcpy(&buffer_out[1], &error_buffer[1],
               (output_size - 1) * sizeof(*buffer_out));
        return;
    }

    if (predictor_coef_num == 31) {
        /* simple 1st-order prediction */
        for (i = 1; i < output_size; i++) {
            buffer_out[i] = sign_extend(buffer_out[i - 1] + error_buffer[i],
                                        readsamplesize);
        }
        return;
    }

    /* read warm-up samples */
    for (i = 0; i < predictor_coef_num; i++) {
        buffer_out[i + 1] = sign_extend(buffer_out[i] + error_buffer[i + 1],
                                        readsamplesize);
    }

    /* NOTE: 4 and 8 are very common cases that could be optimized. */

    /* general case */
    for (i = predictor_coef_num; i < output_size - 1; i++) {
        int j;
        int val = 0;
        int error_val = error_buffer[i + 1];
        int error_sign;
        int d = buffer_out[i - predictor_coef_num];

        for (j = 0; j < predictor_coef_num; j++) {
            val += (buffer_out[i - j] - d) *
                   predictor_coef_table[j];
        }

        val = (val + (1 << (predictor_quantitization - 1))) >>
              predictor_quantitization;
        val += d + error_val;

        buffer_out[i + 1] = sign_extend(val, readsamplesize);

        /* adapt LPC coefficients */
        error_sign = sign_only(error_val);
        if (error_sign) {
            for (j = predictor_coef_num - 1; j >= 0 && error_val * error_sign > 0; j--) {
                int sign;
                val  = d - buffer_out[i - j];
                sign = sign_only(val) * error_sign;
                predictor_coef_table[j] -= sign;
                val *= sign;
                error_val -= ((val >> predictor_quantitization) *
                              (predictor_coef_num - j));
            }
        }
    }
}

static void decorrelate_stereo(int32_t *buffer[MAX_CHANNELS],
                               int numsamples, uint8_t interlacing_shift,
                               uint8_t interlacing_leftweight)
{
    int i;

    for (i = 0; i < numsamples; i++) {
        int32_t a, b;

        a = buffer[0][i];
        b = buffer[1][i];

        a -= (b * interlacing_leftweight) >> interlacing_shift;
        b += a;

        buffer[0][i] = b;
        buffer[1][i] = a;
    }
}

static void append_extra_bits(int32_t *buffer[MAX_CHANNELS],
                              int32_t *extra_bits_buffer[MAX_CHANNELS],
                              int extra_bits, int numchannels, int numsamples)
{
    int i, ch;

    for (ch = 0; ch < numchannels; ch++)
        for (i = 0; i < numsamples; i++)
            buffer[ch][i] = (buffer[ch][i] << extra_bits) | extra_bits_buffer[ch][i];
}

static int alac_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    ALACContext *alac = avctx->priv_data;

    int channels;
    int hassize;
    unsigned int readsamplesize;
    int is_compressed;
    uint8_t interlacing_shift;
    uint8_t interlacing_leftweight;
    int i, ch, ret;

    init_get_bits(&alac->gb, avpkt->data, avpkt->size * 8);

    channels = get_bits(&alac->gb, 3) + 1;
    if (channels != avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "frame header channel count mismatch\n");
        return AVERROR_INVALIDDATA;
    }

    skip_bits(&alac->gb, 4);  /* element instance tag */
    skip_bits(&alac->gb, 12); /* unused header bits */

    /* the number of output samples is stored in the frame */
    hassize = get_bits1(&alac->gb);

    alac->extra_bits = get_bits(&alac->gb, 2) << 3;

    /* whether the frame is compressed */
    is_compressed = !get_bits1(&alac->gb);

    if (hassize) {
        /* now read the number of samples as a 32bit integer */
        uint32_t output_samples = get_bits_long(&alac->gb, 32);
        if (!output_samples || output_samples > alac->max_samples_per_frame) {
            av_log(avctx, AV_LOG_ERROR, "invalid samples per frame: %d\n",
                   output_samples);
            return AVERROR_INVALIDDATA;
        }
        alac->nb_samples = output_samples;
    } else
        alac->nb_samples = alac->max_samples_per_frame;

    /* get output buffer */
    alac->frame.nb_samples = alac->nb_samples;
    if ((ret = avctx->get_buffer(avctx, &alac->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    readsamplesize = alac->sample_size - alac->extra_bits + channels - 1;
    if (readsamplesize > MIN_CACHE_BITS) {
        av_log(avctx, AV_LOG_ERROR, "readsamplesize too big (%d)\n", readsamplesize);
        return -1;
    }

    if (is_compressed) {
        int16_t predictor_coef_table[MAX_CHANNELS][32];
        int predictor_coef_num[MAX_CHANNELS];
        int prediction_type[MAX_CHANNELS];
        int prediction_quantitization[MAX_CHANNELS];
        int ricemodifier[MAX_CHANNELS];

        interlacing_shift = get_bits(&alac->gb, 8);
        interlacing_leftweight = get_bits(&alac->gb, 8);

        for (ch = 0; ch < channels; ch++) {
            prediction_type[ch] = get_bits(&alac->gb, 4);
            prediction_quantitization[ch] = get_bits(&alac->gb, 4);

            ricemodifier[ch] = get_bits(&alac->gb, 3);
            predictor_coef_num[ch] = get_bits(&alac->gb, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num[ch]; i++)
                predictor_coef_table[ch][i] = get_sbits(&alac->gb, 16);
        }

        if (alac->extra_bits) {
            for (i = 0; i < alac->nb_samples; i++) {
                for (ch = 0; ch < channels; ch++)
                    alac->extra_bits_buffer[ch][i] = get_bits(&alac->gb, alac->extra_bits);
            }
        }
        for (ch = 0; ch < channels; ch++) {
            bastardized_rice_decompress(alac,
                                        alac->predict_error_buffer[ch],
                                        alac->nb_samples,
                                        readsamplesize,
                                        ricemodifier[ch] * alac->rice_history_mult / 4);

            /* adaptive FIR filter */
            if (prediction_type[ch] == 15) {
                /* Prediction type 15 runs the adaptive FIR twice.
                 * The first pass uses the special-case coef_num = 31, while
                 * the second pass uses the coefs from the bitstream.
                 *
                 * However, this prediction type is not currently used by the
                 * reference encoder.
                 */
                predictor_decompress_fir_adapt(alac->predict_error_buffer[ch],
                                               alac->predict_error_buffer[ch],
                                               alac->nb_samples, readsamplesize,
                                               NULL, 31, 0);
            } else if (prediction_type[ch] > 0) {
                av_log(avctx, AV_LOG_WARNING, "unknown prediction type: %i\n",
                       prediction_type[ch]);
            }
            predictor_decompress_fir_adapt(alac->predict_error_buffer[ch],
                                           alac->output_samples_buffer[ch],
                                           alac->nb_samples, readsamplesize,
                                           predictor_coef_table[ch],
                                           predictor_coef_num[ch],
                                           prediction_quantitization[ch]);
        }
    } else {
        /* not compressed, easy case */
        for (i = 0; i < alac->nb_samples; i++) {
            for (ch = 0; ch < channels; ch++) {
                alac->output_samples_buffer[ch][i] = get_sbits_long(&alac->gb,
                                                                    alac->sample_size);
            }
        }
        alac->extra_bits = 0;
        interlacing_shift = 0;
        interlacing_leftweight = 0;
    }
    if (get_bits(&alac->gb, 3) != 7)
        av_log(avctx, AV_LOG_ERROR, "Error : Wrong End Of Frame\n");

    if (channels == 2 && interlacing_leftweight) {
        decorrelate_stereo(alac->output_samples_buffer, alac->nb_samples,
                           interlacing_shift, interlacing_leftweight);
    }

    if (alac->extra_bits) {
        append_extra_bits(alac->output_samples_buffer, alac->extra_bits_buffer,
                          alac->extra_bits, alac->channels, alac->nb_samples);
    }

    switch(alac->sample_size) {
    case 16: {
        int16_t *outbuffer = (int16_t *)alac->frame.data[0];
        for (i = 0; i < alac->nb_samples; i++) {
            *outbuffer++ = alac->output_samples_buffer[0][i];
            if (channels == 2)
                *outbuffer++ = alac->output_samples_buffer[1][i];
        }}
        break;
    case 24: {
        int32_t *outbuffer = (int32_t *)alac->frame.data[0];
        for (i = 0; i < alac->nb_samples; i++) {
            *outbuffer++ = alac->output_samples_buffer[0][i] << 8;
            if (channels == 2)
                *outbuffer++ = alac->output_samples_buffer[1][i] << 8;
        }}
        break;
    }

    if (avpkt->size * 8 - get_bits_count(&alac->gb) > 8)
        av_log(avctx, AV_LOG_ERROR, "Error : %d bits left\n",
               avpkt->size * 8 - get_bits_count(&alac->gb));

    *got_frame_ptr   = 1;
    *(AVFrame *)data = alac->frame;

    return avpkt->size;
}

static av_cold int alac_decode_close(AVCodecContext *avctx)
{
    ALACContext *alac = avctx->priv_data;

    int ch;
    for (ch = 0; ch < alac->channels; ch++) {
        av_freep(&alac->predict_error_buffer[ch]);
        av_freep(&alac->output_samples_buffer[ch]);
        av_freep(&alac->extra_bits_buffer[ch]);
    }

    return 0;
}

static int allocate_buffers(ALACContext *alac)
{
    int ch;
    for (ch = 0; ch < alac->channels; ch++) {
        int buf_size = alac->max_samples_per_frame * sizeof(int32_t);

        FF_ALLOC_OR_GOTO(alac->avctx, alac->predict_error_buffer[ch],
                         buf_size, buf_alloc_fail);

        FF_ALLOC_OR_GOTO(alac->avctx, alac->output_samples_buffer[ch],
                         buf_size, buf_alloc_fail);

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
    ALACContext *alac = avctx->priv_data;
    alac->avctx = avctx;

    /* initialize from the extradata */
    if (alac->avctx->extradata_size != ALAC_EXTRADATA_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "alac: expected %d extradata bytes\n",
            ALAC_EXTRADATA_SIZE);
        return -1;
    }
    if (alac_set_info(alac)) {
        av_log(avctx, AV_LOG_ERROR, "alac: set_info failed\n");
        return -1;
    }

    switch (alac->sample_size) {
    case 16: avctx->sample_fmt    = AV_SAMPLE_FMT_S16;
             break;
    case 24: avctx->sample_fmt    = AV_SAMPLE_FMT_S32;
             break;
    default: av_log_ask_for_sample(avctx, "Sample depth %d is not supported.\n",
                                   alac->sample_size);
             return AVERROR_PATCHWELCOME;
    }

    if (alac->channels < 1) {
        av_log(avctx, AV_LOG_WARNING, "Invalid channel count\n");
        alac->channels = avctx->channels;
    } else {
        if (alac->channels > MAX_CHANNELS)
            alac->channels = avctx->channels;
        else
            avctx->channels = alac->channels;
    }
    if (avctx->channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported channel count: %d\n",
               avctx->channels);
        return AVERROR_PATCHWELCOME;
    }

    if ((ret = allocate_buffers(alac)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating buffers\n");
        return ret;
    }

    avcodec_get_frame_defaults(&alac->frame);
    avctx->coded_frame = &alac->frame;

    return 0;
}

AVCodec ff_alac_decoder = {
    .name           = "alac",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_ALAC,
    .priv_data_size = sizeof(ALACContext),
    .init           = alac_decode_init,
    .close          = alac_decode_close,
    .decode         = alac_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("ALAC (Apple Lossless Audio Codec)"),
};
