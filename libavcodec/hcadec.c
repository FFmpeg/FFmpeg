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

#include "libavutil/crc.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/tx.h"

#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "hca_data.h"

typedef struct ChannelContext {
    float    base[128];
    DECLARE_ALIGNED(32, float, imdct_in)[128];
    DECLARE_ALIGNED(32, float, imdct_out)[128];
    DECLARE_ALIGNED(32, float, imdct_prev)[128];
    int8_t   scale_factors[128];
    uint8_t  scale[128];
    int8_t   intensity[8];
    int8_t  *hfr_scale;
    unsigned count;
    int      chan_type;
} ChannelContext;

typedef struct HCAContext {
    GetBitContext gb;

    const AVCRC *crc_table;

    ChannelContext ch[16];

    uint8_t ath[128];

    int     ath_type;
    unsigned hfr_group_count;
    uint8_t track_count;
    uint8_t channel_config;
    uint8_t total_band_count;
    uint8_t base_band_count;
    uint8_t stereo_band_count;
    uint8_t bands_per_hfr_group;

    av_tx_fn           tx_fn;
    AVTXContext       *tx_ctx;
    AVFloatDSPContext *fdsp;
} HCAContext;

static void ath_init1(uint8_t *ath, int sample_rate)
{
    unsigned int index;
    unsigned int acc = 0;

    for (int i = 0; i < 128; i++) {
        acc += sample_rate;
        index = acc >> 13;

        if (index >= 654) {
            memset(ath+i, 0xFF, (128 - i));
            break;
        }

        ath[i] = ath_base_curve[index];
    }
}

static int ath_init(uint8_t *ath, int type, int sample_rate)
{
    switch (type) {
    case 0:
        /* nothing to do */
        break;
    case 1:
        ath_init1(ath, sample_rate);
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static inline unsigned ceil2(unsigned a, unsigned b)
{
    return (b > 0) ? (a / b + ((a % b) ? 1 : 0)) : 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    HCAContext *c = avctx->priv_data;
    GetBitContext *gb = &c->gb;
    int8_t r[16] = { 0 };
    float scale = 1.f / 8.f;
    unsigned b, chunk;
    int version, ret;

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    c->crc_table = av_crc_get_table(AV_CRC_16_ANSI);

    if (avctx->channels <= 0 || avctx->channels > 16)
        return AVERROR(EINVAL);

    ret = init_get_bits8(gb, avctx->extradata, avctx->extradata_size);
    if (ret < 0)
        return ret;
    skip_bits_long(gb, 32);
    version = get_bits(gb, 16);
    skip_bits_long(gb, 16);

    c->ath_type = version >= 0x200 ? 0 : 1;

    if (get_bits_long(gb, 32) != MKBETAG('f', 'm', 't', 0))
        return AVERROR_INVALIDDATA;
    skip_bits_long(gb, 32);
    skip_bits_long(gb, 32);
    skip_bits_long(gb, 32);

    chunk = get_bits_long(gb, 32);
    if (chunk == MKBETAG('c', 'o', 'm', 'p')) {
        skip_bits_long(gb, 16);
        skip_bits_long(gb, 8);
        skip_bits_long(gb, 8);
        c->track_count = get_bits(gb, 8);
        c->channel_config = get_bits(gb, 8);
        c->total_band_count = get_bits(gb, 8);
        c->base_band_count = get_bits(gb, 8);
        c->stereo_band_count = get_bits(gb, 8);
        c->bands_per_hfr_group = get_bits(gb, 8);
    } else if (chunk == MKBETAG('d', 'e', 'c', 0)) {
        skip_bits_long(gb, 16);
        skip_bits_long(gb, 8);
        skip_bits_long(gb, 8);
        c->total_band_count = get_bits(gb, 8) + 1;
        c->base_band_count = get_bits(gb, 8) + 1;
        c->track_count = get_bits(gb, 4);
        c->channel_config = get_bits(gb, 4);
        if (!get_bits(gb, 8))
            c->base_band_count = c->total_band_count;
        c->stereo_band_count = c->total_band_count - c->base_band_count;
        c->bands_per_hfr_group = 0;
    } else
        return AVERROR_INVALIDDATA;

    if (c->total_band_count > FF_ARRAY_ELEMS(c->ch->imdct_in))
        return AVERROR_INVALIDDATA;


    while (get_bits_left(gb) >= 32) {
        chunk = get_bits_long(gb, 32);
        if (chunk == MKBETAG('v', 'b', 'r', 0)) {
            skip_bits_long(gb, 16);
            skip_bits_long(gb, 16);
        } else if (chunk == MKBETAG('a', 't', 'h', 0)) {
            c->ath_type = get_bits(gb, 16);
        } else if (chunk == MKBETAG('r', 'v', 'a', 0)) {
            skip_bits_long(gb, 32);
        } else if (chunk == MKBETAG('c', 'o', 'm', 'm')) {
            skip_bits_long(gb, get_bits(gb, 8) * 8);
        } else if (chunk == MKBETAG('c', 'i', 'p', 'h')) {
            skip_bits_long(gb, 16);
        } else if (chunk == MKBETAG('l', 'o', 'o', 'p')) {
            skip_bits_long(gb, 32);
            skip_bits_long(gb, 32);
            skip_bits_long(gb, 16);
            skip_bits_long(gb, 16);
        } else if (chunk == MKBETAG('p', 'a', 'd', 0)) {
            break;
        } else {
            break;
        }
    }

    ret = ath_init(c->ath, c->ath_type, avctx->sample_rate);
    if (ret < 0)
        return ret;

    if (!c->track_count)
        c->track_count = 1;

    b = avctx->channels / c->track_count;
    if (c->stereo_band_count && b > 1) {
        int8_t *x = r;

        for (int i = 0; i < c->track_count; i++, x+=b) {
            switch (b) {
            case 2:
            case 3:
                x[0] = 1;
                x[1] = 2;
                break;
            case 4:
                x[0]=1; x[1] = 2;
                if (c->channel_config == 0) {
                    x[2]=1;
                    x[3]=2;
                }
                break;
            case 5:
                x[0]=1; x[1] = 2;
                if (c->channel_config <= 2) {
                    x[3]=1;
                    x[4]=2;
                }
                break;
            case 6:
            case 7:
                x[0] = 1; x[1] = 2; x[4] = 1; x[5] = 2;
                break;
            case 8:
                x[0] = 1; x[1] = 2; x[4] = 1; x[5] = 2; x[6] = 1; x[7] = 2;
                break;
            }
        }
    }

    if (c->total_band_count < c->base_band_count)
        return AVERROR_INVALIDDATA;

    c->hfr_group_count = ceil2(c->total_band_count - (c->base_band_count + c->stereo_band_count),
                               c->bands_per_hfr_group);

    if (c->base_band_count + c->stereo_band_count + (unsigned long)c->hfr_group_count > 128ULL)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < avctx->channels; i++) {
        c->ch[i].chan_type = r[i];
        c->ch[i].count     = c->base_band_count + ((r[i] != 2) ? c->stereo_band_count : 0);
        c->ch[i].hfr_scale = &c->ch[i].scale_factors[c->base_band_count + c->stereo_band_count];
        if (c->ch[i].count > 128)
            return AVERROR_INVALIDDATA;
    }

    c->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!c->fdsp)
        return AVERROR(ENOMEM);

    return av_tx_init(&c->tx_ctx, &c->tx_fn, AV_TX_FLOAT_MDCT, 1, 128, &scale, 0);
}

static void run_imdct(HCAContext *c, ChannelContext *ch, int index, float *out)
{
    c->tx_fn(c->tx_ctx, ch->imdct_out, ch->imdct_in, sizeof(float));

    c->fdsp->vector_fmul_window(out, ch->imdct_prev + (128 >> 1),
                                ch->imdct_out, window, 128 >> 1);

    memcpy(ch->imdct_prev, ch->imdct_out, 128 * sizeof(float));
}

static void apply_intensity_stereo(HCAContext *s, ChannelContext *ch1, ChannelContext *ch2,
                                   int index, unsigned band_count, unsigned base_band_count,
                                   unsigned stereo_band_count)
{
    float ratio_l = intensity_ratio_table[ch2->intensity[index]];
    float ratio_r = ratio_l - 2.0f;
    float *c1 = &ch1->imdct_in[base_band_count];
    float *c2 = &ch2->imdct_in[base_band_count];

    if (ch1->chan_type != 1 || !stereo_band_count)
        return;

    for (int i = 0; i < band_count; i++) {
        *(c2++)  = *c1 * ratio_r;
        *(c1++) *= ratio_l;
    }
}

static void reconstruct_hfr(HCAContext *s, ChannelContext *ch,
                            unsigned hfr_group_count,
                            unsigned bands_per_hfr_group,
                            unsigned start_band, unsigned total_band_count)
{
    if (ch->chan_type == 2 || !bands_per_hfr_group)
        return;

    for (int i = 0, k = start_band, l = start_band - 1; i < hfr_group_count; i++){
        for (int j = 0; j < bands_per_hfr_group && k < total_band_count && l >= 0; j++, k++, l--){
            ch->imdct_in[k] = scale_conversion_table[ scale_conv_bias +
                av_clip_intp2(ch->hfr_scale[i] - ch->scale_factors[l], 6) ] * ch->imdct_in[l];
        }
    }

    ch->imdct_in[127] = 0;
}

static void dequantize_coefficients(HCAContext *c, ChannelContext *ch)
{
    GetBitContext *gb = &c->gb;

    for (int i = 0; i < ch->count; i++) {
        unsigned scale = ch->scale[i];
        int nb_bits = max_bits_table[scale];
        int value = get_bitsz(gb, nb_bits);
        float factor;

        if (scale > 7) {
            value = (1 - ((value & 1) << 1)) * (value >> 1);
            if (!value)
                skip_bits_long(gb, -1);
            factor = value;
        } else {
            value += scale << 4;
            skip_bits_long(gb, quant_spectrum_bits[value] - nb_bits);
            factor = quant_spectrum_value[value];
        }
        ch->imdct_in[i] = factor * ch->base[i];
    }

    memset(ch->imdct_in + ch->count, 0,  sizeof(ch->imdct_in) - ch->count * sizeof(ch->imdct_in[0]));
}

static void unpack(HCAContext *c, ChannelContext *ch,
                   unsigned hfr_group_count,
                   int packed_noise_level,
                   const uint8_t *ath)
{
    GetBitContext *gb = &c->gb;
    int delta_bits = get_bits(gb, 3);

    if (delta_bits > 5) {
        for (int i = 0; i < ch->count; i++)
            ch->scale_factors[i] = get_bits(gb, 6);
    } else if (delta_bits) {
        int factor = get_bits(gb, 6);
        int max_value = (1 << delta_bits) - 1;
        int half_max = max_value >> 1;

        ch->scale_factors[0] = factor;
        for (int i = 1; i < ch->count; i++){
            int delta = get_bits(gb, delta_bits);

            if (delta == max_value) {
                factor = get_bits(gb, 6);
            } else {
                factor += delta - half_max;
            }
            factor = av_clip_uintp2(factor, 6);

            ch->scale_factors[i] = factor;
        }
    } else {
        memset(ch->scale_factors, 0, 128);
    }

    if (ch->chan_type == 2){
        ch->intensity[0] = get_bits(gb, 4);
        if (ch->intensity[0] < 15) {
            for (int i = 1; i < 8; i++)
                ch->intensity[i] = get_bits(gb, 4);
        }
    } else {
        for (int i = 0; i < hfr_group_count; i++)
            ch->hfr_scale[i] = get_bits(gb, 6);
    }

    for (int i = 0; i < ch->count; i++) {
        int scale = ch->scale_factors[i];

        if (scale) {
            scale = c->ath[i] + ((packed_noise_level + i) >> 8) - ((scale * 5) >> 1) + 2;
            scale = scale_table[av_clip(scale, 0, 58)];
        }
        ch->scale[i] = scale;
    }

    memset(ch->scale + ch->count, 0, sizeof(ch->scale) - ch->count);

    for (int i = 0; i < ch->count; i++)
        ch->base[i] = dequantizer_scaling_table[ch->scale_factors[i]] * quant_step_size[ch->scale[i]];
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame = data;
    HCAContext *c = avctx->priv_data;
    int ch, ret, packed_noise_level;
    GetBitContext *gb = &c->gb;
    float **samples;

    if (avctx->err_recognition & AV_EF_CRCCHECK) {
        if (av_crc(c->crc_table, 0, avpkt->data, avpkt->size))
            return AVERROR_INVALIDDATA;
    }

    if ((ret = init_get_bits8(gb, avpkt->data, avpkt->size)) < 0)
        return ret;

    if (get_bits(gb, 16) != 0xFFFF)
        return AVERROR_INVALIDDATA;

    frame->nb_samples = 1024;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (float **)frame->extended_data;

    packed_noise_level = (get_bits(gb, 9) << 8) - get_bits(gb, 7);

    for (ch = 0; ch < avctx->channels; ch++)
        unpack(c, &c->ch[ch], c->hfr_group_count, packed_noise_level, c->ath);

    for (int i = 0; i < 8; i++) {
        for (ch = 0; ch < avctx->channels; ch++)
            dequantize_coefficients(c, &c->ch[ch]);
        for (ch = 0; ch < avctx->channels; ch++)
            reconstruct_hfr(c, &c->ch[ch], c->hfr_group_count, c->bands_per_hfr_group,
                            c->stereo_band_count + c->base_band_count, c->total_band_count);
        for (ch = 0; ch < avctx->channels - 1; ch++)
            apply_intensity_stereo(c, &c->ch[ch], &c->ch[ch+1], i,
                                   c->total_band_count - c->base_band_count,
                                   c->base_band_count, c->stereo_band_count);
        for (ch = 0; ch < avctx->channels; ch++)
            run_imdct(c, &c->ch[ch], i, samples[ch] + i * 128);
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    HCAContext *c = avctx->priv_data;

    av_freep(&c->fdsp);
    av_tx_uninit(&c->tx_ctx);

    return 0;
}

AVCodec ff_hca_decoder = {
    .name           = "hca",
    .long_name      = NULL_IF_CONFIG_SMALL("CRI HCA"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_HCA,
    .priv_data_size = sizeof(HCAContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .close          = decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
