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
 * 32 bits  atom size
 * 32 bits  tag                  ("alac")
 * 32 bits  tag version          (0)
 * 32 bits  samples per frame    (used when not set explicitly in the frames)
 *  8 bits  compatible version   (0)
 *  8 bits  sample size
 *  8 bits  history mult         (40)
 *  8 bits  initial history      (10)
 *  8 bits  rice param limit     (14)
 *  8 bits  channels
 * 16 bits  maxRun               (255)
 * 32 bits  max coded frame size (0 means unknown)
 * 32 bits  average bitrate      (0 means unknown)
 * 32 bits  samplerate
 */

#include <inttypes.h>

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "internal.h"
#include "thread.h"
#include "unary.h"
#include "mathops.h"
#include "alac_data.h"
#include "alacdsp.h"

#define ALAC_EXTRADATA_SIZE 36

typedef struct ALACContext {
    AVClass *class;
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
    int      sample_rate;

    int extra_bits;     /**< number of extra bits beyond 16-bit */
    int nb_samples;     /**< number of samples in the current frame */

    int direct_output;
    int extra_bit_bug;

    ALACDSPContext dsp;
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
            return AVERROR_INVALIDDATA;

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

static void lpc_prediction(int32_t *error_buffer, uint32_t *buffer_out,
                           int nb_samples, int bps, int16_t *lpc_coefs,
                           int lpc_order, int lpc_quant)
{
    int i;
    uint32_t *pred = buffer_out;

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
        unsigned error_val = error_buffer[i];
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
            for (j = 0; j < lpc_order && (int)error_val * error_sign > 0; j++) {
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
    if (bps > 32) {
        avpriv_report_missing_feature(avctx, "bps %d", bps);
        return AVERROR_PATCHWELCOME;
    }
    if (bps < 1)
        return AVERROR_INVALIDDATA;

    /* whether the frame is compressed */
    is_compressed = !get_bits1(&alac->gb);

    if (has_size)
        output_samples = get_bits_long(&alac->gb, 32);
    else
        output_samples = alac->max_samples_per_frame;
    if (!output_samples || output_samples > alac->max_samples_per_frame) {
        av_log(avctx, AV_LOG_ERROR, "invalid samples per frame: %"PRIu32"\n",
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
        av_log(avctx, AV_LOG_ERROR, "sample count mismatch: %"PRIu32" != %d\n",
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

        if (!alac->rice_limit) {
            avpriv_request_sample(alac->avctx,
                                  "Compression with rice limit 0");
            return AVERROR(ENOSYS);
        }

        decorr_shift       = get_bits(&alac->gb, 8);
        decorr_left_weight = get_bits(&alac->gb, 8);

        for (ch = 0; ch < channels; ch++) {
            prediction_type[ch]   = get_bits(&alac->gb, 4);
            lpc_quant[ch]         = get_bits(&alac->gb, 4);
            rice_history_mult[ch] = get_bits(&alac->gb, 3);
            lpc_order[ch]         = get_bits(&alac->gb, 5);

            if (lpc_order[ch] >= alac->max_samples_per_frame || !lpc_quant[ch])
                return AVERROR_INVALIDDATA;

            /* read the predictor table */
            for (i = lpc_order[ch] - 1; i >= 0; i--)
                lpc_coefs[ch][i] = get_sbits(&alac->gb, 16);
        }

        if (alac->extra_bits) {
            for (i = 0; i < alac->nb_samples; i++) {
                if(get_bits_left(&alac->gb) <= 0)
                    return AVERROR_INVALIDDATA;
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
                return AVERROR_INVALIDDATA;
            for (ch = 0; ch < channels; ch++) {
                alac->output_samples_buffer[ch][i] =
                         get_sbits_long(&alac->gb, alac->sample_size);
            }
        }
        alac->extra_bits   = 0;
        decorr_shift       = 0;
        decorr_left_weight = 0;
    }

    if (channels == 2) {
        if (alac->extra_bits && alac->extra_bit_bug) {
            alac->dsp.append_extra_bits[1](alac->output_samples_buffer, alac->extra_bits_buffer,
                                           alac->extra_bits, channels, alac->nb_samples);
        }

        if (decorr_left_weight) {
            alac->dsp.decorrelate_stereo(alac->output_samples_buffer, alac->nb_samples,
                                         decorr_shift, decorr_left_weight);
        }

        if (alac->extra_bits && !alac->extra_bit_bug) {
            alac->dsp.append_extra_bits[1](alac->output_samples_buffer, alac->extra_bits_buffer,
                                           alac->extra_bits, channels, alac->nb_samples);
        }
    } else if (alac->extra_bits) {
        alac->dsp.append_extra_bits[0](alac->output_samples_buffer, alac->extra_bits_buffer,
                                       alac->extra_bits, channels, alac->nb_samples);
    }

    switch(alac->sample_size) {
    case 16: {
        for (ch = 0; ch < channels; ch++) {
            int16_t *outbuffer = (int16_t *)frame->extended_data[ch_index + ch];
            for (i = 0; i < alac->nb_samples; i++)
                *outbuffer++ = alac->output_samples_buffer[ch][i];
        }}
        break;
    case 20: {
        for (ch = 0; ch < channels; ch++) {
            for (i = 0; i < alac->nb_samples; i++)
                alac->output_samples_buffer[ch][i] <<= 12;
        }}
        break;
    case 24: {
        for (ch = 0; ch < channels; ch++) {
            for (i = 0; i < alac->nb_samples; i++)
                alac->output_samples_buffer[ch][i] <<= 8;
        }}
        break;
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
            avpriv_report_missing_feature(avctx, "Syntax element %d", element);
            return AVERROR_PATCHWELCOME;
        }

        channels = (element == TYPE_CPE) ? 2 : 1;
        if (ch + channels > alac->channels ||
            ff_alac_channel_layout_offsets[alac->channels - 1][ch] + channels > alac->channels) {
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

    if (alac->channels == ch && alac->nb_samples)
        *got_frame_ptr = 1;
    else
        av_log(avctx, AV_LOG_WARNING, "Failed to decode all channels\n");

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
    unsigned buf_size = alac->max_samples_per_frame * sizeof(int32_t);

    for (ch = 0; ch < 2; ch++) {
        alac->predict_error_buffer[ch]  = NULL;
        alac->output_samples_buffer[ch] = NULL;
        alac->extra_bits_buffer[ch]     = NULL;
    }

    for (ch = 0; ch < FFMIN(alac->channels, 2); ch++) {
        FF_ALLOC_OR_GOTO(alac->avctx, alac->predict_error_buffer[ch],
                         buf_size, buf_alloc_fail);

        alac->direct_output = alac->sample_size > 16;
        if (!alac->direct_output) {
            FF_ALLOC_OR_GOTO(alac->avctx, alac->output_samples_buffer[ch],
                             buf_size + AV_INPUT_BUFFER_PADDING_SIZE, buf_alloc_fail);
        }

        FF_ALLOC_OR_GOTO(alac->avctx, alac->extra_bits_buffer[ch],
                         buf_size + AV_INPUT_BUFFER_PADDING_SIZE, buf_alloc_fail);
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
    if (!alac->max_samples_per_frame ||
        alac->max_samples_per_frame > 4096 * 4096) {
        av_log(alac->avctx, AV_LOG_ERROR,
               "max samples per frame invalid: %"PRIu32"\n",
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
    alac->sample_rate          = bytestream2_get_be32u(&gb);

    return 0;
}

static av_cold int alac_decode_init(AVCodecContext * avctx)
{
    int ret;
    ALACContext *alac = avctx->priv_data;
    alac->avctx = avctx;

    /* initialize from the extradata */
    if (alac->avctx->extradata_size < ALAC_EXTRADATA_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "extradata is too small\n");
        return AVERROR_INVALIDDATA;
    }
    if ((ret = alac_set_info(alac)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "set_info failed\n");
        return ret;
    }

    switch (alac->sample_size) {
    case 16: avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
             break;
    case 20:
    case 24:
    case 32: avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
             break;
    default: avpriv_request_sample(avctx, "Sample depth %d", alac->sample_size);
             return AVERROR_PATCHWELCOME;
    }
    avctx->bits_per_raw_sample = alac->sample_size;
    avctx->sample_rate         = alac->sample_rate;

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
        avpriv_report_missing_feature(avctx, "Channel count %d",
                                      avctx->channels);
        return AVERROR_PATCHWELCOME;
    }
    avctx->channel_layout = ff_alac_channel_layouts[alac->channels - 1];

    if ((ret = allocate_buffers(alac)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating buffers\n");
        return ret;
    }

    ff_alacdsp_init(&alac->dsp);

    return 0;
}

#if HAVE_THREADS
static int init_thread_copy(AVCodecContext *avctx)
{
    ALACContext *alac = avctx->priv_data;
    alac->avctx = avctx;
    return allocate_buffers(alac);
}
#endif

static const AVOption options[] = {
    { "extra_bits_bug", "Force non-standard decoding process",
      offsetof(ALACContext, extra_bit_bug), AV_OPT_TYPE_BOOL, { .i64 = 0 },
      0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass alac_class = {
    .class_name = "alac",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_alac_decoder = {
    .name           = "alac",
    .long_name      = NULL_IF_CONFIG_SMALL("ALAC (Apple Lossless Audio Codec)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ALAC,
    .priv_data_size = sizeof(ALACContext),
    .init           = alac_decode_init,
    .close          = alac_decode_close,
    .decode         = alac_decode_frame,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(init_thread_copy),
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .priv_class     = &alac_class
};
