/*
 * TTA (The Lossless True Audio) decoder
 * Copyright (c) 2006 Alex Beregszaszi
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
 * TTA (The Lossless True Audio) decoder
 * @see http://www.true-audio.com/
 * @see http://tta.corecodec.org/
 * @author Alex Beregszaszi
 */

#define BITSTREAM_READER_LE
#include <limits.h>
#include "ttadata.h"
#include "ttadsp.h"
#include "avcodec.h"
#include "get_bits.h"
#include "thread.h"
#include "unary.h"
#include "internal.h"
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#define FORMAT_SIMPLE    1
#define FORMAT_ENCRYPTED 2

typedef struct TTAContext {
    AVClass *class;
    AVCodecContext *avctx;
    const AVCRC *crc_table;

    int format, channels, bps;
    unsigned data_length;
    int frame_length, last_frame_length;

    int32_t *decode_buffer;

    uint8_t crc_pass[8];
    uint8_t *pass;
    TTAChannel *ch_ctx;
    TTADSPContext dsp;
} TTAContext;

static const int64_t tta_channel_layouts[7] = {
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_STEREO|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_QUAD,
    0,
    AV_CH_LAYOUT_5POINT1_BACK,
    AV_CH_LAYOUT_5POINT1_BACK|AV_CH_BACK_CENTER,
    AV_CH_LAYOUT_7POINT1_WIDE
};

static int tta_check_crc(TTAContext *s, const uint8_t *buf, int buf_size)
{
    uint32_t crc, CRC;

    CRC = AV_RL32(buf + buf_size);
    crc = av_crc(s->crc_table, 0xFFFFFFFFU, buf, buf_size);
    if (CRC != (crc ^ 0xFFFFFFFFU)) {
        av_log(s->avctx, AV_LOG_ERROR, "CRC error\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static uint64_t tta_check_crc64(uint8_t *pass)
{
    uint64_t crc = UINT64_MAX, poly = 0x42F0E1EBA9EA3693U;
    uint8_t *end = pass + strlen(pass);
    int i;

    while (pass < end) {
        crc ^= (uint64_t)*pass++ << 56;
        for (i = 0; i < 8; i++)
            crc = (crc << 1) ^ (poly & (((int64_t) crc) >> 63));
    }

    return crc ^ UINT64_MAX;
}

static int allocate_buffers(AVCodecContext *avctx)
{
    TTAContext *s = avctx->priv_data;

    if (s->bps < 3) {
        s->decode_buffer = av_mallocz_array(sizeof(int32_t)*s->frame_length, s->channels);
        if (!s->decode_buffer)
            return AVERROR(ENOMEM);
    } else
        s->decode_buffer = NULL;
    s->ch_ctx = av_malloc_array(avctx->channels, sizeof(*s->ch_ctx));
    if (!s->ch_ctx) {
        av_freep(&s->decode_buffer);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold int tta_decode_init(AVCodecContext * avctx)
{
    TTAContext *s = avctx->priv_data;
    GetBitContext gb;
    int total_frames;

    s->avctx = avctx;

    // 30bytes includes TTA1 header
    if (avctx->extradata_size < 22)
        return AVERROR_INVALIDDATA;

    s->crc_table = av_crc_get_table(AV_CRC_32_IEEE_LE);
    init_get_bits8(&gb, avctx->extradata, avctx->extradata_size);
    if (show_bits_long(&gb, 32) == AV_RL32("TTA1")) {
        /* signature */
        skip_bits_long(&gb, 32);

        s->format = get_bits(&gb, 16);
        if (s->format > 2) {
            av_log(avctx, AV_LOG_ERROR, "Invalid format\n");
            return AVERROR_INVALIDDATA;
        }
        if (s->format == FORMAT_ENCRYPTED) {
            if (!s->pass) {
                av_log(avctx, AV_LOG_ERROR, "Missing password for encrypted stream. Please use the -password option\n");
                return AVERROR(EINVAL);
            }
            AV_WL64(s->crc_pass, tta_check_crc64(s->pass));
        }
        avctx->channels = s->channels = get_bits(&gb, 16);
        if (s->channels > 1 && s->channels < 9)
            avctx->channel_layout = tta_channel_layouts[s->channels-2];
        avctx->bits_per_raw_sample = get_bits(&gb, 16);
        s->bps = (avctx->bits_per_raw_sample + 7) / 8;
        avctx->sample_rate = get_bits_long(&gb, 32);
        s->data_length = get_bits_long(&gb, 32);
        skip_bits_long(&gb, 32); // CRC32 of header

        if (s->channels == 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid number of channels\n");
            return AVERROR_INVALIDDATA;
        } else if (avctx->sample_rate == 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid samplerate\n");
            return AVERROR_INVALIDDATA;
        }

        switch(s->bps) {
        case 1: avctx->sample_fmt = AV_SAMPLE_FMT_U8; break;
        case 2:
            avctx->sample_fmt = AV_SAMPLE_FMT_S16;
            break;
        case 3:
            avctx->sample_fmt = AV_SAMPLE_FMT_S32;
            break;
        //case 4: avctx->sample_fmt = AV_SAMPLE_FMT_S32; break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Invalid/unsupported sample format.\n");
            return AVERROR_INVALIDDATA;
        }

        // prevent overflow
        if (avctx->sample_rate > 0x7FFFFFu) {
            av_log(avctx, AV_LOG_ERROR, "sample_rate too large\n");
            return AVERROR(EINVAL);
        }
        s->frame_length = 256 * avctx->sample_rate / 245;

        s->last_frame_length = s->data_length % s->frame_length;
        total_frames = s->data_length / s->frame_length +
                       (s->last_frame_length ? 1 : 0);

        av_log(avctx, AV_LOG_DEBUG, "format: %d chans: %d bps: %d rate: %d block: %d\n",
            s->format, avctx->channels, avctx->bits_per_coded_sample, avctx->sample_rate,
            avctx->block_align);
        av_log(avctx, AV_LOG_DEBUG, "data_length: %d frame_length: %d last: %d total: %d\n",
            s->data_length, s->frame_length, s->last_frame_length, total_frames);

        if(s->frame_length >= UINT_MAX / (s->channels * sizeof(int32_t))){
            av_log(avctx, AV_LOG_ERROR, "frame_length too large\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "Wrong extradata present\n");
        return AVERROR_INVALIDDATA;
    }

    ff_ttadsp_init(&s->dsp);

    return allocate_buffers(avctx);
}

static int tta_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    ThreadFrame tframe = { .f = data };
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TTAContext *s = avctx->priv_data;
    GetBitContext gb;
    int i, ret;
    int cur_chan = 0, framelen = s->frame_length;
    int32_t *p;

    if (avctx->err_recognition & AV_EF_CRCCHECK) {
        if (buf_size < 4 ||
            (tta_check_crc(s, buf, buf_size - 4) && avctx->err_recognition & AV_EF_EXPLODE))
            return AVERROR_INVALIDDATA;
    }

    if ((ret = init_get_bits8(&gb, avpkt->data, avpkt->size)) < 0)
        return ret;

    /* get output buffer */
    frame->nb_samples = framelen;
    if ((ret = ff_thread_get_buffer(avctx, &tframe, 0)) < 0)
        return ret;

    // decode directly to output buffer for 24-bit sample format
    if (s->bps == 3)
        s->decode_buffer = (int32_t *)frame->data[0];

    // init per channel states
    for (i = 0; i < s->channels; i++) {
        TTAFilter *filter = &s->ch_ctx[i].filter;
        s->ch_ctx[i].predictor = 0;
        ff_tta_filter_init(filter, ff_tta_filter_configs[s->bps-1]);
        if (s->format == FORMAT_ENCRYPTED) {
            int i;
            for (i = 0; i < 8; i++)
                filter->qm[i] = sign_extend(s->crc_pass[i], 8);
        }
        ff_tta_rice_init(&s->ch_ctx[i].rice, 10, 10);
    }

    i = 0;
    for (p = s->decode_buffer; p < s->decode_buffer + (framelen * s->channels); p++) {
        int32_t *predictor = &s->ch_ctx[cur_chan].predictor;
        TTAFilter *filter = &s->ch_ctx[cur_chan].filter;
        TTARice *rice = &s->ch_ctx[cur_chan].rice;
        uint32_t unary, depth, k;
        int32_t value;

        unary = get_unary(&gb, 0, get_bits_left(&gb));

        if (unary == 0) {
            depth = 0;
            k = rice->k0;
        } else {
            depth = 1;
            k = rice->k1;
            unary--;
        }

        if (get_bits_left(&gb) < k) {
            ret = AVERROR_INVALIDDATA;
            goto error;
        }

        if (k) {
            if (k > MIN_CACHE_BITS) {
                ret = AVERROR_INVALIDDATA;
                goto error;
            }
            value = (unary << k) + get_bits(&gb, k);
        } else
            value = unary;

        // FIXME: copy paste from original
        switch (depth) {
        case 1:
            rice->sum1 += value - (rice->sum1 >> 4);
            if (rice->k1 > 0 && rice->sum1 < ff_tta_shift_16[rice->k1])
                rice->k1--;
            else if(rice->sum1 > ff_tta_shift_16[rice->k1 + 1])
                rice->k1++;
            value += ff_tta_shift_1[rice->k0];
        default:
            rice->sum0 += value - (rice->sum0 >> 4);
            if (rice->k0 > 0 && rice->sum0 < ff_tta_shift_16[rice->k0])
                rice->k0--;
            else if(rice->sum0 > ff_tta_shift_16[rice->k0 + 1])
                rice->k0++;
        }

        // extract coded value
        *p = 1 + ((value >> 1) ^ ((value & 1) - 1));

        // run hybrid filter
        s->dsp.ttafilter_process_dec(filter->qm, filter->dx, filter->dl, &filter->error, p,
                                     filter->shift, filter->round);

        // fixed order prediction
#define PRED(x, k) (int32_t)((((uint64_t)(x) << (k)) - (x)) >> (k))
        switch (s->bps) {
        case 1: *p += PRED(*predictor, 4); break;
        case 2:
        case 3: *p += PRED(*predictor, 5); break;
        case 4: *p +=      *predictor;     break;
        }
        *predictor = *p;

        // flip channels
        if (cur_chan < (s->channels-1))
            cur_chan++;
        else {
            // decorrelate in case of multiple channels
            if (s->channels > 1) {
                int32_t *r = p - 1;
                for (*p += *r / 2; r > p - s->channels; r--)
                    *r = *(r + 1) - *r;
            }
            cur_chan = 0;
            i++;
            // check for last frame
            if (i == s->last_frame_length && get_bits_left(&gb) / 8 == 4) {
                frame->nb_samples = framelen = s->last_frame_length;
                break;
            }
        }
    }

    align_get_bits(&gb);
    if (get_bits_left(&gb) < 32) {
        ret = AVERROR_INVALIDDATA;
        goto error;
    }
    skip_bits_long(&gb, 32); // frame crc

    // convert to output buffer
    switch (s->bps) {
    case 1: {
        uint8_t *samples = (uint8_t *)frame->data[0];
        for (p = s->decode_buffer; p < s->decode_buffer + (framelen * s->channels); p++)
            *samples++ = *p + 0x80;
        break;
        }
    case 2: {
        int16_t *samples = (int16_t *)frame->data[0];
        for (p = s->decode_buffer; p < s->decode_buffer + (framelen * s->channels); p++)
            *samples++ = *p;
        break;
        }
    case 3: {
        // shift samples for 24-bit sample format
        int32_t *samples = (int32_t *)frame->data[0];
        for (i = 0; i < framelen * s->channels; i++)
            *samples++ <<= 8;
        // reset decode buffer
        s->decode_buffer = NULL;
        break;
        }
    }

    *got_frame_ptr = 1;

    return buf_size;
error:
    // reset decode buffer
    if (s->bps == 3)
        s->decode_buffer = NULL;
    return ret;
}

static int init_thread_copy(AVCodecContext *avctx)
{
    TTAContext *s = avctx->priv_data;
    s->avctx = avctx;
    return allocate_buffers(avctx);
}

static av_cold int tta_decode_close(AVCodecContext *avctx) {
    TTAContext *s = avctx->priv_data;

    if (s->bps < 3)
        av_free(s->decode_buffer);
    s->decode_buffer = NULL;
    av_freep(&s->ch_ctx);

    return 0;
}

#define OFFSET(x) offsetof(TTAContext, x)
#define DEC (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM)
static const AVOption options[] = {
    { "password", "Set decoding password", OFFSET(pass), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, DEC },
    { NULL },
};

static const AVClass tta_decoder_class = {
    .class_name = "TTA Decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_tta_decoder = {
    .name           = "tta",
    .long_name      = NULL_IF_CONFIG_SMALL("TTA (True Audio)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_TTA,
    .priv_data_size = sizeof(TTAContext),
    .init           = tta_decode_init,
    .close          = tta_decode_close,
    .decode         = tta_decode_frame,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(init_thread_copy),
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS,
    .priv_class     = &tta_decoder_class,
};
