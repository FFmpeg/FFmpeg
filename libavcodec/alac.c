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
 *  8bit  initial history      (14)
 *  8bit  kmodifier            (10)
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

    int numchannels;

    /* buffers */
    int32_t *predicterror_buffer[MAX_CHANNELS];

    int32_t *outputsamples_buffer[MAX_CHANNELS];

    int32_t *extra_bits_buffer[MAX_CHANNELS];

    /* stuff from setinfo */
    uint32_t setinfo_max_samples_per_frame; /* 0x1000 = 4096 */    /* max samples per frame? */
    uint8_t setinfo_sample_size; /* 0x10 */
    uint8_t setinfo_rice_historymult; /* 0x28 */
    uint8_t setinfo_rice_initialhistory; /* 0x0a */
    uint8_t setinfo_rice_kmodifier; /* 0x0e */
    /* end setinfo stuff */

    int extra_bits;                         /**< number of extra bits beyond 16-bit */
} ALACContext;

static inline int decode_scalar(GetBitContext *gb, int k, int limit, int readsamplesize){
    /* read x - number of 1s before 0 represent the rice */
    int x = get_unary_0_9(gb);

    if (x > 8) { /* RICE THRESHOLD */
        /* use alternative encoding */
        x = get_bits(gb, readsamplesize);
    } else {
        if (k >= limit)
            k = limit;

        if (k != 1) {
            int extrabits = show_bits(gb, k);

            /* multiply x by 2^k - 1, as part of their strange algorithm */
            x = (x << k) - x;

            if (extrabits > 1) {
                x += extrabits - 1;
                skip_bits(gb, k);
            } else
                skip_bits(gb, k - 1);
        }
    }
    return x;
}

static int bastardized_rice_decompress(ALACContext *alac,
                                 int32_t *output_buffer,
                                 int output_size,
                                 int readsamplesize, /* arg_10 */
                                 int rice_initialhistory, /* arg424->b */
                                 int rice_kmodifier, /* arg424->d */
                                 int rice_historymult, /* arg424->c */
                                 int rice_kmodifier_mask /* arg424->e */
        )
{
    int output_count;
    unsigned int history = rice_initialhistory;
    int sign_modifier = 0;

    for (output_count = 0; output_count < output_size; output_count++) {
        int32_t x;
        int32_t x_modified;
        int32_t final_val;

        /* standard rice encoding */
        int k; /* size of extra bits */

        if(get_bits_left(&alac->gb) <= 0)
            return -1;

        /* read k, that is bits as is */
        k = av_log2((history >> 9) + 3);
        x= decode_scalar(&alac->gb, k, rice_kmodifier, readsamplesize);

        x_modified = sign_modifier + x;
        final_val = (x_modified + 1) / 2;
        if (x_modified & 1) final_val *= -1;

        output_buffer[output_count] = final_val;

        sign_modifier = 0;

        /* now update the history */
        history += x_modified * rice_historymult
                   - ((history * rice_historymult) >> 9);

        if (x_modified > 0xffff)
            history = 0xffff;

        /* special case: there may be compressed blocks of 0 */
        if ((history < 128) && (output_count+1 < output_size)) {
            int k;
            unsigned int block_size;

            sign_modifier = 1;

            k = 7 - av_log2(history) + ((history + 16) >> 6 /* / 64 */);

            block_size= decode_scalar(&alac->gb, k, rice_kmodifier, 16);

            if (block_size > 0) {
                if(block_size >= output_size - output_count){
                    av_log(alac->avctx, AV_LOG_ERROR, "invalid zero block size of %d %d %d\n", block_size, output_size, output_count);
                    block_size= output_size - output_count - 1;
                }
                memset(&output_buffer[output_count+1], 0, block_size * 4);
                output_count += block_size;
            }

            if (block_size > 0xffff)
                sign_modifier = 0;

            history = 0;
        }
    }
    return 0;
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

    if (!predictor_coef_num) {
        if (output_size <= 1)
            return;

        memcpy(buffer_out+1, error_buffer+1, (output_size-1) * 4);
        return;
    }

    if (predictor_coef_num == 0x1f) { /* 11111 - max value of predictor_coef_num */
      /* second-best case scenario for fir decompression,
       * error describes a small difference from the previous sample only
       */
        if (output_size <= 1)
            return;
        for (i = 0; i < output_size - 1; i++) {
            int32_t prev_value;
            int32_t error_value;

            prev_value = buffer_out[i];
            error_value = error_buffer[i+1];
            buffer_out[i+1] =
                sign_extend((prev_value + error_value), readsamplesize);
        }
        return;
    }

    /* read warm-up samples */
    if (predictor_coef_num > 0)
        for (i = 0; i < predictor_coef_num; i++) {
            int32_t val;

            val = buffer_out[i] + error_buffer[i+1];
            val = sign_extend(val, readsamplesize);
            buffer_out[i+1] = val;
        }

    /* 4 and 8 are very common cases (the only ones i've seen). these
     * should be unrolled and optimized
     */

    /* general case */
    if (predictor_coef_num > 0) {
        for (i = predictor_coef_num + 1; i < output_size; i++) {
            int j;
            int sum = 0;
            int outval;
            int error_val = error_buffer[i];

            for (j = 0; j < predictor_coef_num; j++) {
                sum += (buffer_out[predictor_coef_num-j] - buffer_out[0]) *
                       predictor_coef_table[j];
            }

            outval = (1 << (predictor_quantitization-1)) + sum;
            outval = outval >> predictor_quantitization;
            outval = outval + buffer_out[0] + error_val;
            outval = sign_extend(outval, readsamplesize);

            buffer_out[predictor_coef_num+1] = outval;

            if (error_val > 0) {
                int predictor_num = predictor_coef_num - 1;

                while (predictor_num >= 0 && error_val > 0) {
                    int val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
                    int sign = sign_only(val);

                    predictor_coef_table[predictor_num] -= sign;

                    val *= sign; /* absolute value */

                    error_val -= ((val >> predictor_quantitization) *
                                  (predictor_coef_num - predictor_num));

                    predictor_num--;
                }
            } else if (error_val < 0) {
                int predictor_num = predictor_coef_num - 1;

                while (predictor_num >= 0 && error_val < 0) {
                    int val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
                    int sign = - sign_only(val);

                    predictor_coef_table[predictor_num] -= sign;

                    val *= sign; /* neg value */

                    error_val -= ((val >> predictor_quantitization) *
                                  (predictor_coef_num - predictor_num));

                    predictor_num--;
                }
            }

            buffer_out++;
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

static void interleave_stereo_16(int32_t *buffer[MAX_CHANNELS],
                                 int16_t *buffer_out, int numsamples)
{
    int i;

    for (i = 0; i < numsamples; i++) {
        *buffer_out++ = buffer[0][i];
        *buffer_out++ = buffer[1][i];
    }
}

static void interleave_stereo_24(int32_t *buffer[MAX_CHANNELS],
                                 int32_t *buffer_out, int numsamples)
{
    int i;

    for (i = 0; i < numsamples; i++) {
        *buffer_out++ = buffer[0][i] << 8;
        *buffer_out++ = buffer[1][i] << 8;
    }
}

static void interleave_stereo_32(int32_t *buffer[MAX_CHANNELS],
                                 int32_t *buffer_out, int numsamples)
{
    int i;

    for (i = 0; i < numsamples; i++) {
        *buffer_out++ = buffer[0][i];
        *buffer_out++ = buffer[1][i];
    }
}

static int alac_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *inbuffer = avpkt->data;
    int input_buffer_size = avpkt->size;
    ALACContext *alac = avctx->priv_data;

    int channels;
    unsigned int outputsamples;
    int hassize;
    unsigned int readsamplesize;
    int isnotcompressed;
    uint8_t interlacing_shift;
    uint8_t interlacing_leftweight;
    int i, ch, ret;

    init_get_bits(&alac->gb, inbuffer, input_buffer_size * 8);

    channels = get_bits(&alac->gb, 3) + 1;
    if (channels != avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "frame header channel count mismatch\n");
        return AVERROR_INVALIDDATA;
    }

    /* 2^result = something to do with output waiting.
     * perhaps matters if we read > 1 frame in a pass?
     */
    skip_bits(&alac->gb, 4);

    skip_bits(&alac->gb, 12); /* unknown, skip 12 bits */

    /* the output sample size is stored soon */
    hassize = get_bits1(&alac->gb);

    alac->extra_bits = get_bits(&alac->gb, 2) << 3;

    /* whether the frame is compressed */
    isnotcompressed = get_bits1(&alac->gb);

    if (hassize) {
        /* now read the number of samples as a 32bit integer */
        outputsamples = get_bits_long(&alac->gb, 32);
        if(outputsamples > alac->setinfo_max_samples_per_frame){
            av_log(avctx, AV_LOG_ERROR, "outputsamples %d > %d\n", outputsamples, alac->setinfo_max_samples_per_frame);
            return -1;
        }
    } else
        outputsamples = alac->setinfo_max_samples_per_frame;

    /* get output buffer */
    if (outputsamples > INT32_MAX) {
        av_log(avctx, AV_LOG_ERROR, "unsupported block size: %u\n", outputsamples);
        return AVERROR_INVALIDDATA;
    }
    alac->frame.nb_samples = outputsamples;
    if ((ret = avctx->get_buffer(avctx, &alac->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    readsamplesize = alac->setinfo_sample_size - alac->extra_bits + channels - 1;
    if (readsamplesize > MIN_CACHE_BITS) {
        av_log(avctx, AV_LOG_ERROR, "readsamplesize too big (%d)\n", readsamplesize);
        return -1;
    }

    if (!isnotcompressed) {
        /* so it is compressed */
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
                predictor_coef_table[ch][i] = (int16_t)get_bits(&alac->gb, 16);
        }

        if (alac->extra_bits) {
            for (i = 0; i < outputsamples; i++) {
                if(get_bits_left(&alac->gb) <= 0)
                    return -1;
                for (ch = 0; ch < channels; ch++)
                    alac->extra_bits_buffer[ch][i] = get_bits(&alac->gb, alac->extra_bits);
            }
        }
        for (ch = 0; ch < channels; ch++) {
            int ret = bastardized_rice_decompress(alac,
                                        alac->predicterror_buffer[ch],
                                        outputsamples,
                                        readsamplesize,
                                        alac->setinfo_rice_initialhistory,
                                        alac->setinfo_rice_kmodifier,
                                        ricemodifier[ch] * alac->setinfo_rice_historymult / 4,
                                        (1 << alac->setinfo_rice_kmodifier) - 1);
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
                predictor_decompress_fir_adapt(alac->predicterror_buffer[ch],
                                               alac->predicterror_buffer[ch],
                                               outputsamples, readsamplesize,
                                               NULL, 31, 0);
            } else if (prediction_type[ch] > 0) {
                av_log(avctx, AV_LOG_WARNING, "unknown prediction type: %i\n",
                       prediction_type[ch]);
            }
            predictor_decompress_fir_adapt(alac->predicterror_buffer[ch],
                                           alac->outputsamples_buffer[ch],
                                           outputsamples, readsamplesize,
                                           predictor_coef_table[ch],
                                           predictor_coef_num[ch],
                                           prediction_quantitization[ch]);
        }
    } else {
        /* not compressed, easy case */
        for (i = 0; i < outputsamples; i++) {
            if(get_bits_left(&alac->gb) <= 0)
                return -1;
            for (ch = 0; ch < channels; ch++) {
                alac->outputsamples_buffer[ch][i] = get_sbits_long(&alac->gb,
                                                                   alac->setinfo_sample_size);
            }
        }
        alac->extra_bits = 0;
        interlacing_shift = 0;
        interlacing_leftweight = 0;
    }
    if (get_bits(&alac->gb, 3) != 7)
        av_log(avctx, AV_LOG_ERROR, "Error : Wrong End Of Frame\n");

    if (channels == 2 && interlacing_leftweight) {
        decorrelate_stereo(alac->outputsamples_buffer, outputsamples,
                           interlacing_shift, interlacing_leftweight);
    }

    if (alac->extra_bits) {
        append_extra_bits(alac->outputsamples_buffer, alac->extra_bits_buffer,
                          alac->extra_bits, alac->numchannels, outputsamples);
    }

    switch(alac->setinfo_sample_size) {
    case 16:
        if (channels == 2) {
            interleave_stereo_16(alac->outputsamples_buffer,
                                 (int16_t *)alac->frame.data[0], outputsamples);
        } else {
            int16_t *outbuffer = (int16_t *)alac->frame.data[0];
            for (i = 0; i < outputsamples; i++) {
                outbuffer[i] = alac->outputsamples_buffer[0][i];
            }
        }
        break;
    case 24:
        if (channels == 2) {
            interleave_stereo_24(alac->outputsamples_buffer,
                                 (int32_t *)alac->frame.data[0], outputsamples);
        } else {
            int32_t *outbuffer = (int32_t *)alac->frame.data[0];
            for (i = 0; i < outputsamples; i++)
                outbuffer[i] = alac->outputsamples_buffer[0][i] << 8;
        }
        break;
    case 32:
        if (channels == 2) {
            interleave_stereo_32(alac->outputsamples_buffer,
                                 (int32_t *)alac->frame.data[0], outputsamples);
        } else {
            int32_t *outbuffer = (int32_t *)alac->frame.data[0];
            for (i = 0; i < outputsamples; i++)
                outbuffer[i] = alac->outputsamples_buffer[0][i];
        }
        break;
    }

    if (input_buffer_size * 8 - get_bits_count(&alac->gb) > 8)
        av_log(avctx, AV_LOG_ERROR, "Error : %d bits left\n", input_buffer_size * 8 - get_bits_count(&alac->gb));

    *got_frame_ptr   = 1;
    *(AVFrame *)data = alac->frame;

    return input_buffer_size;
}

static av_cold int alac_decode_close(AVCodecContext *avctx)
{
    ALACContext *alac = avctx->priv_data;

    int ch;
    for (ch = 0; ch < alac->numchannels; ch++) {
        av_freep(&alac->predicterror_buffer[ch]);
        av_freep(&alac->outputsamples_buffer[ch]);
        av_freep(&alac->extra_bits_buffer[ch]);
    }

    return 0;
}

static int allocate_buffers(ALACContext *alac)
{
    int ch;
    for (ch = 0; ch < alac->numchannels; ch++) {
        int buf_size = alac->setinfo_max_samples_per_frame * sizeof(int32_t);

        FF_ALLOC_OR_GOTO(alac->avctx, alac->predicterror_buffer[ch],
                         buf_size, buf_alloc_fail);

        FF_ALLOC_OR_GOTO(alac->avctx, alac->outputsamples_buffer[ch],
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

    /* buffer size / 2 ? */
    alac->setinfo_max_samples_per_frame = bytestream2_get_be32u(&gb);
    if (alac->setinfo_max_samples_per_frame >= UINT_MAX/4){
        av_log(alac->avctx, AV_LOG_ERROR,
               "setinfo_max_samples_per_frame too large\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skipu(&gb, 1);  // compatible version
    alac->setinfo_sample_size           = bytestream2_get_byteu(&gb);
    alac->setinfo_rice_historymult      = bytestream2_get_byteu(&gb);
    alac->setinfo_rice_initialhistory   = bytestream2_get_byteu(&gb);
    alac->setinfo_rice_kmodifier        = bytestream2_get_byteu(&gb);
    alac->numchannels                   = bytestream2_get_byteu(&gb);
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

    switch (alac->setinfo_sample_size) {
    case 16: avctx->sample_fmt    = AV_SAMPLE_FMT_S16;
             break;
    case 32:
    case 24: avctx->sample_fmt    = AV_SAMPLE_FMT_S32;
             break;
    default: av_log_ask_for_sample(avctx, "Sample depth %d is not supported.\n",
                                   alac->setinfo_sample_size);
             return AVERROR_PATCHWELCOME;
    }

    if (alac->numchannels < 1) {
        av_log(avctx, AV_LOG_WARNING, "Invalid channel count\n");
        alac->numchannels = avctx->channels;
    } else {
        if (alac->numchannels > MAX_CHANNELS)
            alac->numchannels = avctx->channels;
        else
            avctx->channels = alac->numchannels;
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
