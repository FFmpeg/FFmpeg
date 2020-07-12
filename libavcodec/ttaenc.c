/*
 * TTA (The Lossless True Audio) encoder
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

#define BITSTREAM_WRITER_LE
#include "ttadata.h"
#include "ttaencdsp.h"
#include "avcodec.h"
#include "put_bits.h"
#include "internal.h"
#include "libavutil/crc.h"

typedef struct TTAEncContext {
    const AVCRC *crc_table;
    int bps;
    TTAChannel *ch_ctx;
    TTAEncDSPContext dsp;
} TTAEncContext;

static av_cold int tta_encode_init(AVCodecContext *avctx)
{
    TTAEncContext *s = avctx->priv_data;

    s->crc_table = av_crc_get_table(AV_CRC_32_IEEE_LE);

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_U8:
        avctx->bits_per_raw_sample = 8;
        break;
    case AV_SAMPLE_FMT_S16:
        avctx->bits_per_raw_sample = 16;
        break;
    case AV_SAMPLE_FMT_S32:
        if (avctx->bits_per_raw_sample > 24)
            av_log(avctx, AV_LOG_WARNING, "encoding as 24 bits-per-sample\n");
        avctx->bits_per_raw_sample = 24;
    }

    s->bps = avctx->bits_per_raw_sample >> 3;
    avctx->frame_size = 256 * avctx->sample_rate / 245;

    s->ch_ctx = av_malloc_array(avctx->channels, sizeof(*s->ch_ctx));
    if (!s->ch_ctx)
        return AVERROR(ENOMEM);

    ff_ttaencdsp_init(&s->dsp);

    return 0;
}

static int32_t get_sample(const AVFrame *frame, int sample,
                          enum AVSampleFormat format)
{
    int32_t ret;

    if (format == AV_SAMPLE_FMT_U8) {
        ret = frame->data[0][sample] - 0x80;
    } else if (format == AV_SAMPLE_FMT_S16) {
        const int16_t *ptr = (const int16_t *)frame->data[0];
        ret = ptr[sample];
    } else {
        const int32_t *ptr = (const int32_t *)frame->data[0];
        ret = ptr[sample] >> 8;
    }

    return ret;
}

static int tta_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    TTAEncContext *s = avctx->priv_data;
    PutBitContext pb;
    int ret, i, out_bytes, cur_chan, res, samples;
    int64_t pkt_size =  frame->nb_samples * 2LL * avctx->channels * s->bps;

pkt_alloc:
    cur_chan = 0, res = 0, samples = 0;
    if ((ret = ff_alloc_packet2(avctx, avpkt, pkt_size, 0)) < 0)
        return ret;
    init_put_bits(&pb, avpkt->data, avpkt->size);

    // init per channel states
    for (i = 0; i < avctx->channels; i++) {
        s->ch_ctx[i].predictor = 0;
        ff_tta_filter_init(&s->ch_ctx[i].filter, ff_tta_filter_configs[s->bps - 1]);
        ff_tta_rice_init(&s->ch_ctx[i].rice, 10, 10);
    }

    for (i = 0; i < frame->nb_samples * avctx->channels; i++) {
        TTAChannel *c = &s->ch_ctx[cur_chan];
        TTAFilter *filter = &c->filter;
        TTARice *rice = &c->rice;
        uint32_t k, unary, outval;
        int32_t value, temp;

        value = get_sample(frame, samples++, avctx->sample_fmt);

        if (avctx->channels > 1) {
            if (cur_chan < avctx->channels - 1)
                value  = res = get_sample(frame, samples, avctx->sample_fmt) - value;
            else
                value -= res / 2;
        }

        temp = value;
#define PRED(x, k) (int32_t)((((uint64_t)(x) << (k)) - (x)) >> (k))
        switch (s->bps) {
        case 1: value -= PRED(c->predictor, 4); break;
        case 2:
        case 3: value -= PRED(c->predictor, 5); break;
        }
        c->predictor = temp;

        s->dsp.filter_process(filter->qm, filter->dx, filter->dl, &filter->error, &value,
                              filter->shift, filter->round);
        outval = (value > 0) ? (value << 1) - 1: -value << 1;

        k = rice->k0;

        rice->sum0 += outval - (rice->sum0 >> 4);
        if (rice->k0 > 0 && rice->sum0 < ff_tta_shift_16[rice->k0])
            rice->k0--;
        else if (rice->sum0 > ff_tta_shift_16[rice->k0 + 1])
            rice->k0++;

        if (outval >= ff_tta_shift_1[k]) {
            outval -= ff_tta_shift_1[k];
            k = rice->k1;

            rice->sum1 += outval - (rice->sum1 >> 4);
            if (rice->k1 > 0 && rice->sum1 < ff_tta_shift_16[rice->k1])
                rice->k1--;
            else if (rice->sum1 > ff_tta_shift_16[rice->k1 + 1])
                rice->k1++;

            unary = 1 + (outval >> k);
            if (unary + 100LL > put_bits_left(&pb)) {
                if (pkt_size < INT_MAX/2) {
                    pkt_size *= 2;
                    av_packet_unref(avpkt);
                    goto pkt_alloc;
                } else
                    return AVERROR(ENOMEM);
            }
            do {
                if (unary > 31) {
                    put_bits(&pb, 31, 0x7FFFFFFF);
                    unary -= 31;
                } else {
                    put_bits(&pb, unary, (1U << unary) - 1);
                    unary = 0;
                }
            } while (unary);
        }

        put_bits(&pb, 1, 0);

        if (k)
            put_bits(&pb, k, outval & (ff_tta_shift_1[k] - 1));

        if (cur_chan < avctx->channels - 1)
            cur_chan++;
        else
            cur_chan = 0;
    }

    flush_put_bits(&pb);
    out_bytes = put_bits_count(&pb) >> 3;
    put_bits32(&pb, av_crc(s->crc_table, UINT32_MAX, avpkt->data, out_bytes) ^ UINT32_MAX);
    flush_put_bits(&pb);

    avpkt->pts      = frame->pts;
    avpkt->size     = out_bytes + 4;
    avpkt->duration = ff_samples_to_time_base(avctx, frame->nb_samples);
    *got_packet_ptr = 1;
    return 0;
}

static av_cold int tta_encode_close(AVCodecContext *avctx)
{
    TTAEncContext *s = avctx->priv_data;
    av_freep(&s->ch_ctx);
    return 0;
}

AVCodec ff_tta_encoder = {
    .name           = "tta",
    .long_name      = NULL_IF_CONFIG_SMALL("TTA (True Audio)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_TTA,
    .priv_data_size = sizeof(TTAEncContext),
    .init           = tta_encode_init,
    .close          = tta_encode_close,
    .encode2        = tta_encode_frame,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_U8,
                                                     AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_S32,
                                                     AV_SAMPLE_FMT_NONE },
};
