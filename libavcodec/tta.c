/*
 * TTA (The Lossless True Audio) decoder
 * Copyright (c) 2006 Alex Beregszaszi
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
 * TTA (The Lossless True Audio) decoder
 * @see http://www.true-audio.com/
 * @see http://tta.corecodec.org/
 * @author Alex Beregszaszi
 */

#define BITSTREAM_READER_LE
#include <limits.h>
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "libavutil/crc.h"

#define FORMAT_SIMPLE    1
#define FORMAT_ENCRYPTED 2

#define MAX_ORDER 16
typedef struct TTAFilter {
    int32_t shift, round, error;
    int32_t qm[MAX_ORDER];
    int32_t dx[MAX_ORDER];
    int32_t dl[MAX_ORDER];
} TTAFilter;

typedef struct TTARice {
    uint32_t k0, k1, sum0, sum1;
} TTARice;

typedef struct TTAChannel {
    int32_t predictor;
    TTAFilter filter;
    TTARice rice;
} TTAChannel;

typedef struct TTAContext {
    AVCodecContext *avctx;
    GetBitContext gb;
    const AVCRC *crc_table;

    int format, channels, bps;
    unsigned data_length;
    int frame_length, last_frame_length;

    int32_t *decode_buffer;

    TTAChannel *ch_ctx;
} TTAContext;

static const uint32_t shift_1[] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00100000, 0x00200000, 0x00400000, 0x00800000,
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000,
    0x80000000, 0x80000000, 0x80000000, 0x80000000,
    0x80000000, 0x80000000, 0x80000000, 0x80000000
};

static const uint32_t * const shift_16 = shift_1 + 4;

static const int32_t ttafilter_configs[4] = {
    10,
    9,
    10,
    12
};

static void ttafilter_init(TTAFilter *c, int32_t shift) {
    memset(c, 0, sizeof(TTAFilter));
    c->shift = shift;
   c->round = shift_1[shift-1];
//    c->round = 1 << (shift - 1);
}

// FIXME: copy paste from original
static inline void memshl(register int32_t *a, register int32_t *b) {
    *a++ = *b++;
    *a++ = *b++;
    *a++ = *b++;
    *a++ = *b++;
    *a++ = *b++;
    *a++ = *b++;
    *a++ = *b++;
    *a = *b;
}

static inline void ttafilter_process(TTAFilter *c, int32_t *in)
{
    register int32_t *dl = c->dl, *qm = c->qm, *dx = c->dx, sum = c->round;

    if (!c->error) {
        sum += *dl++ * *qm, qm++;
        sum += *dl++ * *qm, qm++;
        sum += *dl++ * *qm, qm++;
        sum += *dl++ * *qm, qm++;
        sum += *dl++ * *qm, qm++;
        sum += *dl++ * *qm, qm++;
        sum += *dl++ * *qm, qm++;
        sum += *dl++ * *qm, qm++;
        dx += 8;
    } else if(c->error < 0) {
        sum += *dl++ * (*qm -= *dx++), qm++;
        sum += *dl++ * (*qm -= *dx++), qm++;
        sum += *dl++ * (*qm -= *dx++), qm++;
        sum += *dl++ * (*qm -= *dx++), qm++;
        sum += *dl++ * (*qm -= *dx++), qm++;
        sum += *dl++ * (*qm -= *dx++), qm++;
        sum += *dl++ * (*qm -= *dx++), qm++;
        sum += *dl++ * (*qm -= *dx++), qm++;
    } else {
        sum += *dl++ * (*qm += *dx++), qm++;
        sum += *dl++ * (*qm += *dx++), qm++;
        sum += *dl++ * (*qm += *dx++), qm++;
        sum += *dl++ * (*qm += *dx++), qm++;
        sum += *dl++ * (*qm += *dx++), qm++;
        sum += *dl++ * (*qm += *dx++), qm++;
        sum += *dl++ * (*qm += *dx++), qm++;
        sum += *dl++ * (*qm += *dx++), qm++;
    }

    *(dx-0) = ((*(dl-1) >> 30) | 1) << 2;
    *(dx-1) = ((*(dl-2) >> 30) | 1) << 1;
    *(dx-2) = ((*(dl-3) >> 30) | 1) << 1;
    *(dx-3) = ((*(dl-4) >> 30) | 1);

    c->error = *in;
    *in += (sum >> c->shift);
    *dl = *in;

    *(dl-1) = *dl - *(dl-1);
    *(dl-2) = *(dl-1) - *(dl-2);
    *(dl-3) = *(dl-2) - *(dl-3);

    memshl(c->dl, c->dl + 1);
    memshl(c->dx, c->dx + 1);
}

static void rice_init(TTARice *c, uint32_t k0, uint32_t k1)
{
    c->k0 = k0;
    c->k1 = k1;
    c->sum0 = shift_16[k0];
    c->sum1 = shift_16[k1];
}

static int tta_get_unary(GetBitContext *gb)
{
    int ret = 0;

    // count ones
    while (get_bits_left(gb) > 0 && get_bits1(gb))
        ret++;
    return ret;
}

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

static av_cold int tta_decode_init(AVCodecContext * avctx)
{
    TTAContext *s = avctx->priv_data;
    int total_frames;

    s->avctx = avctx;

    // 30bytes includes a seektable with one frame
    if (avctx->extradata_size < 30)
        return -1;

    init_get_bits(&s->gb, avctx->extradata, avctx->extradata_size * 8);
    if (show_bits_long(&s->gb, 32) == AV_RL32("TTA1"))
    {
        if (avctx->err_recognition & AV_EF_CRCCHECK) {
            s->crc_table = av_crc_get_table(AV_CRC_32_IEEE_LE);
            tta_check_crc(s, avctx->extradata, 18);
        }

        /* signature */
        skip_bits_long(&s->gb, 32);

        s->format = get_bits(&s->gb, 16);
        if (s->format > 2) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid format\n");
            return -1;
        }
        if (s->format == FORMAT_ENCRYPTED) {
            avpriv_report_missing_feature(s->avctx, "Encrypted TTA");
            return AVERROR_PATCHWELCOME;
        }
        avctx->channels = s->channels = get_bits(&s->gb, 16);
        avctx->bits_per_coded_sample = get_bits(&s->gb, 16);
        s->bps = (avctx->bits_per_coded_sample + 7) / 8;
        avctx->sample_rate = get_bits_long(&s->gb, 32);
        s->data_length = get_bits_long(&s->gb, 32);
        skip_bits_long(&s->gb, 32); // CRC32 of header

        if (s->channels == 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid number of channels\n");
            return AVERROR_INVALIDDATA;
        } else if (avctx->sample_rate == 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid samplerate\n");
            return AVERROR_INVALIDDATA;
        }

        switch(s->bps) {
        case 2:
            avctx->sample_fmt = AV_SAMPLE_FMT_S16;
            avctx->bits_per_raw_sample = 16;
            break;
        case 3:
            avctx->sample_fmt = AV_SAMPLE_FMT_S32;
            avctx->bits_per_raw_sample = 24;
            break;
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

        av_log(s->avctx, AV_LOG_DEBUG, "format: %d chans: %d bps: %d rate: %d block: %d\n",
            s->format, avctx->channels, avctx->bits_per_coded_sample, avctx->sample_rate,
            avctx->block_align);
        av_log(s->avctx, AV_LOG_DEBUG, "data_length: %d frame_length: %d last: %d total: %d\n",
            s->data_length, s->frame_length, s->last_frame_length, total_frames);

        // FIXME: seek table
        if (avctx->extradata_size <= 26 || total_frames > INT_MAX / 4 ||
            avctx->extradata_size - 26 < total_frames * 4)
            av_log(avctx, AV_LOG_WARNING, "Seek table missing or too small\n");
        else if (avctx->err_recognition & AV_EF_CRCCHECK) {
            int ret = tta_check_crc(s, avctx->extradata + 22, total_frames * 4);
            if (ret < 0 && avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
        }
        skip_bits_long(&s->gb, 32 * total_frames);
        skip_bits_long(&s->gb, 32); // CRC32 of seektable

        if(s->frame_length >= UINT_MAX / (s->channels * sizeof(int32_t))){
            av_log(avctx, AV_LOG_ERROR, "frame_length too large\n");
            return -1;
        }

        if (s->bps == 2) {
            s->decode_buffer = av_mallocz(sizeof(int32_t)*s->frame_length*s->channels);
            if (!s->decode_buffer)
                return AVERROR(ENOMEM);
        }
        s->ch_ctx = av_malloc(avctx->channels * sizeof(*s->ch_ctx));
        if (!s->ch_ctx) {
            av_freep(&s->decode_buffer);
            return AVERROR(ENOMEM);
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "Wrong extradata present\n");
        return -1;
    }

    return 0;
}

static int tta_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TTAContext *s = avctx->priv_data;
    int i, ret;
    int cur_chan = 0, framelen = s->frame_length;
    int32_t *p;

    if (avctx->err_recognition & AV_EF_CRCCHECK) {
        if (buf_size < 4 ||
            (tta_check_crc(s, buf, buf_size - 4) && avctx->err_recognition & AV_EF_EXPLODE))
            return AVERROR_INVALIDDATA;
    }

    init_get_bits(&s->gb, buf, buf_size*8);

    /* get output buffer */
    frame->nb_samples = framelen;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    // decode directly to output buffer for 24-bit sample format
    if (s->bps == 3)
        s->decode_buffer = (int32_t *)frame->data[0];

    // init per channel states
    for (i = 0; i < s->channels; i++) {
        s->ch_ctx[i].predictor = 0;
        ttafilter_init(&s->ch_ctx[i].filter, ttafilter_configs[s->bps-1]);
        rice_init(&s->ch_ctx[i].rice, 10, 10);
    }

    i = 0;
    for (p = s->decode_buffer; p < s->decode_buffer + (framelen * s->channels); p++) {
        int32_t *predictor = &s->ch_ctx[cur_chan].predictor;
        TTAFilter *filter = &s->ch_ctx[cur_chan].filter;
        TTARice *rice = &s->ch_ctx[cur_chan].rice;
        uint32_t unary, depth, k;
        int32_t value;

        unary = tta_get_unary(&s->gb);

        if (unary == 0) {
            depth = 0;
            k = rice->k0;
        } else {
            depth = 1;
            k = rice->k1;
            unary--;
        }

        if (get_bits_left(&s->gb) < k) {
            ret = AVERROR_INVALIDDATA;
            goto error;
        }

        if (k) {
            if (k > MIN_CACHE_BITS) {
                ret = AVERROR_INVALIDDATA;
                goto error;
            }
            value = (unary << k) + get_bits(&s->gb, k);
        } else
            value = unary;

        // FIXME: copy paste from original
        switch (depth) {
        case 1:
            rice->sum1 += value - (rice->sum1 >> 4);
            if (rice->k1 > 0 && rice->sum1 < shift_16[rice->k1])
                rice->k1--;
            else if(rice->sum1 > shift_16[rice->k1 + 1])
                rice->k1++;
            value += shift_1[rice->k0];
        default:
            rice->sum0 += value - (rice->sum0 >> 4);
            if (rice->k0 > 0 && rice->sum0 < shift_16[rice->k0])
                rice->k0--;
            else if(rice->sum0 > shift_16[rice->k0 + 1])
                rice->k0++;
        }

        // extract coded value
        *p = 1 + ((value >> 1) ^ ((value & 1) - 1));

        // run hybrid filter
        ttafilter_process(filter, p);

        // fixed order prediction
#define PRED(x, k) (int32_t)((((uint64_t)x << k) - x) >> k)
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
            if (i == s->last_frame_length && get_bits_left(&s->gb) / 8 == 4) {
                frame->nb_samples = framelen = s->last_frame_length;
                break;
            }
        }
    }

    align_get_bits(&s->gb);
    if (get_bits_left(&s->gb) < 32) {
        ret = AVERROR_INVALIDDATA;
        goto error;
    }
    skip_bits_long(&s->gb, 32); // frame crc

    // convert to output buffer
    if (s->bps == 2) {
        int16_t *samples = (int16_t *)frame->data[0];
        for (p = s->decode_buffer; p < s->decode_buffer + (framelen * s->channels); p++)
            *samples++ = *p;
    } else {
        // shift samples for 24-bit sample format
        int32_t *samples = (int32_t *)frame->data[0];
        for (i = 0; i < framelen * s->channels; i++)
            *samples++ <<= 8;
        // reset decode buffer
        s->decode_buffer = NULL;
    }

    *got_frame_ptr = 1;

    return buf_size;
error:
    // reset decode buffer
    if (s->bps == 3)
        s->decode_buffer = NULL;
    return ret;
}

static av_cold int tta_decode_close(AVCodecContext *avctx) {
    TTAContext *s = avctx->priv_data;

    av_free(s->decode_buffer);
    av_freep(&s->ch_ctx);

    return 0;
}

AVCodec ff_tta_decoder = {
    .name           = "tta",
    .long_name      = NULL_IF_CONFIG_SMALL("TTA (True Audio)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_TTA,
    .priv_data_size = sizeof(TTAContext),
    .init           = tta_decode_init,
    .close          = tta_decode_close,
    .decode         = tta_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
