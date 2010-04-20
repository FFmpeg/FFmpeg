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
 *
 * For more information on the ALAC format, visit:
 *  http://crazney.net/programs/itunes/alac.html
 *
 * Note: This decoder expects a 36- (0x24-)byte QuickTime atom to be
 * passed through the extradata[_size] fields. This atom is tacked onto
 * the end of an 'alac' stsd atom and has the following format:
 *  bytes 0-3   atom size (0x24), big-endian
 *  bytes 4-7   atom type ('alac', not the 'alac' tag from start of stsd)
 *  bytes 8-35  data bytes needed by decoder
 *
 * Extradata:
 * 32bit  size
 * 32bit  tag (=alac)
 * 32bit  zero?
 * 32bit  max sample per frame
 *  8bit  ?? (zero?)
 *  8bit  sample size
 *  8bit  history mult
 *  8bit  initial history
 *  8bit  kmodifier
 *  8bit  channels?
 * 16bit  ??
 * 32bit  max coded frame size
 * 32bit  bitrate?
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
    GetBitContext gb;
    /* init to 0; first frame decode should initialize from extradata and
     * set this to 1 */
    int context_initialized;

    int numchannels;
    int bytespersample;

    /* buffers */
    int32_t *predicterror_buffer[MAX_CHANNELS];

    int32_t *outputsamples_buffer[MAX_CHANNELS];

    int32_t *wasted_bits_buffer[MAX_CHANNELS];

    /* stuff from setinfo */
    uint32_t setinfo_max_samples_per_frame; /* 0x1000 = 4096 */    /* max samples per frame? */
    uint8_t setinfo_sample_size; /* 0x10 */
    uint8_t setinfo_rice_historymult; /* 0x28 */
    uint8_t setinfo_rice_initialhistory; /* 0x0a */
    uint8_t setinfo_rice_kmodifier; /* 0x0e */
    /* end setinfo stuff */

    int wasted_bits;
} ALACContext;

static void allocate_buffers(ALACContext *alac)
{
    int chan;
    for (chan = 0; chan < MAX_CHANNELS; chan++) {
        alac->predicterror_buffer[chan] =
            av_malloc(alac->setinfo_max_samples_per_frame * 4);

        alac->outputsamples_buffer[chan] =
            av_malloc(alac->setinfo_max_samples_per_frame * 4);

        alac->wasted_bits_buffer[chan] = av_malloc(alac->setinfo_max_samples_per_frame * 4);
    }
}

static int alac_set_info(ALACContext *alac)
{
    const unsigned char *ptr = alac->avctx->extradata;

    ptr += 4; /* size */
    ptr += 4; /* alac */
    ptr += 4; /* 0 ? */

    if(AV_RB32(ptr) >= UINT_MAX/4){
        av_log(alac->avctx, AV_LOG_ERROR, "setinfo_max_samples_per_frame too large\n");
        return -1;
    }

    /* buffer size / 2 ? */
    alac->setinfo_max_samples_per_frame = bytestream_get_be32(&ptr);
    ptr++;                          /* ??? */
    alac->setinfo_sample_size           = *ptr++;
    if (alac->setinfo_sample_size > 32) {
        av_log(alac->avctx, AV_LOG_ERROR, "setinfo_sample_size too large\n");
        return -1;
    }
    alac->setinfo_rice_historymult      = *ptr++;
    alac->setinfo_rice_initialhistory   = *ptr++;
    alac->setinfo_rice_kmodifier        = *ptr++;
    ptr++;                         /* channels? */
    bytestream_get_be16(&ptr);      /* ??? */
    bytestream_get_be32(&ptr);      /* max coded frame size */
    bytestream_get_be32(&ptr);      /* bitrate ? */
    bytestream_get_be32(&ptr);      /* samplerate */

    allocate_buffers(alac);

    return 0;
}

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

static void bastardized_rice_decompress(ALACContext *alac,
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

#if 0
    /* 4 and 8 are very common cases (the only ones i've seen). these
     * should be unrolled and optimized
     */
    if (predictor_coef_num == 4) {
        /* FIXME: optimized general case */
        return;
    }

    if (predictor_coef_table == 8) {
        /* FIXME: optimized general case */
        return;
    }
#endif

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

static void reconstruct_stereo_16(int32_t *buffer[MAX_CHANNELS],
                                  int16_t *buffer_out,
                                  int numchannels, int numsamples,
                                  uint8_t interlacing_shift,
                                  uint8_t interlacing_leftweight)
{
    int i;
    if (numsamples <= 0)
        return;

    /* weighted interlacing */
    if (interlacing_leftweight) {
        for (i = 0; i < numsamples; i++) {
            int32_t a, b;

            a = buffer[0][i];
            b = buffer[1][i];

            a -= (b * interlacing_leftweight) >> interlacing_shift;
            b += a;

            buffer_out[i*numchannels] = b;
            buffer_out[i*numchannels + 1] = a;
        }

        return;
    }

    /* otherwise basic interlacing took place */
    for (i = 0; i < numsamples; i++) {
        int16_t left, right;

        left = buffer[0][i];
        right = buffer[1][i];

        buffer_out[i*numchannels] = left;
        buffer_out[i*numchannels + 1] = right;
    }
}

static void decorrelate_stereo_24(int32_t *buffer[MAX_CHANNELS],
                                  int32_t *buffer_out,
                                  int32_t *wasted_bits_buffer[MAX_CHANNELS],
                                  int wasted_bits,
                                  int numchannels, int numsamples,
                                  uint8_t interlacing_shift,
                                  uint8_t interlacing_leftweight)
{
    int i;

    if (numsamples <= 0)
        return;

    /* weighted interlacing */
    if (interlacing_leftweight) {
        for (i = 0; i < numsamples; i++) {
            int32_t a, b;

            a = buffer[0][i];
            b = buffer[1][i];

            a -= (b * interlacing_leftweight) >> interlacing_shift;
            b += a;

            if (wasted_bits) {
                b  = (b  << wasted_bits) | wasted_bits_buffer[0][i];
                a  = (a  << wasted_bits) | wasted_bits_buffer[1][i];
            }

            buffer_out[i * numchannels]     = b << 8;
            buffer_out[i * numchannels + 1] = a << 8;
        }
    } else {
        for (i = 0; i < numsamples; i++) {
            int32_t left, right;

            left  = buffer[0][i];
            right = buffer[1][i];

            if (wasted_bits) {
                left   = (left   << wasted_bits) | wasted_bits_buffer[0][i];
                right  = (right  << wasted_bits) | wasted_bits_buffer[1][i];
            }

            buffer_out[i * numchannels]     = left  << 8;
            buffer_out[i * numchannels + 1] = right << 8;
        }
    }
}

static int alac_decode_frame(AVCodecContext *avctx,
                             void *outbuffer, int *outputsize,
                             AVPacket *avpkt)
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

    /* short-circuit null buffers */
    if (!inbuffer || !input_buffer_size)
        return input_buffer_size;

    /* initialize from the extradata */
    if (!alac->context_initialized) {
        if (alac->avctx->extradata_size != ALAC_EXTRADATA_SIZE) {
            av_log(avctx, AV_LOG_ERROR, "alac: expected %d extradata bytes\n",
                ALAC_EXTRADATA_SIZE);
            return input_buffer_size;
        }
        if (alac_set_info(alac)) {
            av_log(avctx, AV_LOG_ERROR, "alac: set_info failed\n");
            return input_buffer_size;
        }
        alac->context_initialized = 1;
    }

    init_get_bits(&alac->gb, inbuffer, input_buffer_size * 8);

    channels = get_bits(&alac->gb, 3) + 1;
    if (channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "channels > %d not supported\n",
               MAX_CHANNELS);
        return input_buffer_size;
    }

    /* 2^result = something to do with output waiting.
     * perhaps matters if we read > 1 frame in a pass?
     */
    skip_bits(&alac->gb, 4);

    skip_bits(&alac->gb, 12); /* unknown, skip 12 bits */

    /* the output sample size is stored soon */
    hassize = get_bits1(&alac->gb);

    alac->wasted_bits = get_bits(&alac->gb, 2) << 3;

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

    switch (alac->setinfo_sample_size) {
    case 16: avctx->sample_fmt    = SAMPLE_FMT_S16;
             alac->bytespersample = channels << 1;
             break;
    case 24: avctx->sample_fmt    = SAMPLE_FMT_S32;
             alac->bytespersample = channels << 2;
             break;
    default: av_log(avctx, AV_LOG_ERROR, "Sample depth %d is not supported.\n",
                    alac->setinfo_sample_size);
             return -1;
    }

    if(outputsamples > *outputsize / alac->bytespersample){
        av_log(avctx, AV_LOG_ERROR, "sample buffer too small\n");
        return -1;
    }

    *outputsize = outputsamples * alac->bytespersample;
    readsamplesize = alac->setinfo_sample_size - (alac->wasted_bits) + channels - 1;
    if (readsamplesize > MIN_CACHE_BITS) {
        av_log(avctx, AV_LOG_ERROR, "readsamplesize too big (%d)\n", readsamplesize);
        return -1;
    }

    if (!isnotcompressed) {
        /* so it is compressed */
        int16_t predictor_coef_table[channels][32];
        int predictor_coef_num[channels];
        int prediction_type[channels];
        int prediction_quantitization[channels];
        int ricemodifier[channels];
        int i, chan;

        interlacing_shift = get_bits(&alac->gb, 8);
        interlacing_leftweight = get_bits(&alac->gb, 8);

        for (chan = 0; chan < channels; chan++) {
            prediction_type[chan] = get_bits(&alac->gb, 4);
            prediction_quantitization[chan] = get_bits(&alac->gb, 4);

            ricemodifier[chan] = get_bits(&alac->gb, 3);
            predictor_coef_num[chan] = get_bits(&alac->gb, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num[chan]; i++)
                predictor_coef_table[chan][i] = (int16_t)get_bits(&alac->gb, 16);
        }

        if (alac->wasted_bits) {
            int i, ch;
            for (i = 0; i < outputsamples; i++) {
                for (ch = 0; ch < channels; ch++)
                    alac->wasted_bits_buffer[ch][i] = get_bits(&alac->gb, alac->wasted_bits);
            }
        }
        for (chan = 0; chan < channels; chan++) {
            bastardized_rice_decompress(alac,
                                        alac->predicterror_buffer[chan],
                                        outputsamples,
                                        readsamplesize,
                                        alac->setinfo_rice_initialhistory,
                                        alac->setinfo_rice_kmodifier,
                                        ricemodifier[chan] * alac->setinfo_rice_historymult / 4,
                                        (1 << alac->setinfo_rice_kmodifier) - 1);

            if (prediction_type[chan] == 0) {
                /* adaptive fir */
                predictor_decompress_fir_adapt(alac->predicterror_buffer[chan],
                                               alac->outputsamples_buffer[chan],
                                               outputsamples,
                                               readsamplesize,
                                               predictor_coef_table[chan],
                                               predictor_coef_num[chan],
                                               prediction_quantitization[chan]);
            } else {
                av_log(avctx, AV_LOG_ERROR, "FIXME: unhandled prediction type: %i\n", prediction_type[chan]);
                /* I think the only other prediction type (or perhaps this is
                 * just a boolean?) runs adaptive fir twice.. like:
                 * predictor_decompress_fir_adapt(predictor_error, tempout, ...)
                 * predictor_decompress_fir_adapt(predictor_error, outputsamples ...)
                 * little strange..
                 */
            }
        }
    } else {
        /* not compressed, easy case */
        int i, chan;
        if (alac->setinfo_sample_size <= 16) {
        for (i = 0; i < outputsamples; i++)
            for (chan = 0; chan < channels; chan++) {
                int32_t audiobits;

                audiobits = get_sbits_long(&alac->gb, alac->setinfo_sample_size);

                alac->outputsamples_buffer[chan][i] = audiobits;
            }
        } else {
            for (i = 0; i < outputsamples; i++) {
                for (chan = 0; chan < channels; chan++) {
                    alac->outputsamples_buffer[chan][i] = get_bits(&alac->gb,
                                                          alac->setinfo_sample_size);
                    alac->outputsamples_buffer[chan][i] = sign_extend(alac->outputsamples_buffer[chan][i],
                                                                      alac->setinfo_sample_size);
                }
            }
        }
        alac->wasted_bits = 0;
        interlacing_shift = 0;
        interlacing_leftweight = 0;
    }
    if (get_bits(&alac->gb, 3) != 7)
        av_log(avctx, AV_LOG_ERROR, "Error : Wrong End Of Frame\n");

    switch(alac->setinfo_sample_size) {
    case 16:
        if (channels == 2) {
            reconstruct_stereo_16(alac->outputsamples_buffer,
                                  (int16_t*)outbuffer,
                                  alac->numchannels,
                                  outputsamples,
                                  interlacing_shift,
                                  interlacing_leftweight);
        } else {
            int i;
            for (i = 0; i < outputsamples; i++) {
                ((int16_t*)outbuffer)[i] = alac->outputsamples_buffer[0][i];
            }
        }
        break;
    case 24:
        if (channels == 2) {
            decorrelate_stereo_24(alac->outputsamples_buffer,
                                  outbuffer,
                                  alac->wasted_bits_buffer,
                                  alac->wasted_bits,
                                  alac->numchannels,
                                  outputsamples,
                                  interlacing_shift,
                                  interlacing_leftweight);
        } else {
            int i;
            for (i = 0; i < outputsamples; i++)
                ((int32_t *)outbuffer)[i] = alac->outputsamples_buffer[0][i] << 8;
        }
        break;
    }

    if (input_buffer_size * 8 - get_bits_count(&alac->gb) > 8)
        av_log(avctx, AV_LOG_ERROR, "Error : %d bits left\n", input_buffer_size * 8 - get_bits_count(&alac->gb));

    return input_buffer_size;
}

static av_cold int alac_decode_init(AVCodecContext * avctx)
{
    ALACContext *alac = avctx->priv_data;
    alac->avctx = avctx;
    alac->context_initialized = 0;

    alac->numchannels = alac->avctx->channels;

    return 0;
}

static av_cold int alac_decode_close(AVCodecContext *avctx)
{
    ALACContext *alac = avctx->priv_data;

    int chan;
    for (chan = 0; chan < MAX_CHANNELS; chan++) {
        av_freep(&alac->predicterror_buffer[chan]);
        av_freep(&alac->outputsamples_buffer[chan]);
        av_freep(&alac->wasted_bits_buffer[chan]);
    }

    return 0;
}

AVCodec alac_decoder = {
    "alac",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_ALAC,
    sizeof(ALACContext),
    alac_decode_init,
    NULL,
    alac_decode_close,
    alac_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("ALAC (Apple Lossless Audio Codec)"),
};
