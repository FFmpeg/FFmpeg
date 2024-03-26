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
#include "libavutil/mem_internal.h"
#include "libavutil/tx.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "hca_data.h"

#define HCA_MASK 0x7f7f7f7f
#define MAX_CHANNELS 16

typedef struct ChannelContext {
    DECLARE_ALIGNED(32, float, base)[128];
    DECLARE_ALIGNED(32, float, factors)[128];
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
    const AVCRC *crc_table;

    ChannelContext ch[MAX_CHANNELS];

    uint8_t ath[128];
    uint8_t cipher[256];
    uint64_t key;
    uint16_t subkey;

    int     ath_type;
    int     ciph_type;
    unsigned hfr_group_count;
    uint8_t track_count;
    uint8_t channel_config;
    uint8_t total_band_count;
    uint8_t base_band_count;
    uint8_t stereo_band_count;
    uint8_t bands_per_hfr_group;

    // Set during init() and freed on close(). Untouched on init_flush()
    av_tx_fn           tx_fn;
    AVTXContext       *tx_ctx;
    AVFloatDSPContext *fdsp;
} HCAContext;

static void cipher_init56_create_table(uint8_t *r, uint8_t key)
{
    const int mul = ((key & 1) << 3) | 5;
    const int add = (key & 0xE) | 1;

    key >>= 4;
    for (int i = 0; i < 16; i++) {
        key = (key * mul + add) & 0xF;
        r[i] = key;
    }
}

static void cipher_init56(uint8_t *cipher, uint64_t keycode)
{
    uint8_t base[256], base_r[16], base_c[16], kc[8], seed[16];

    /* 56bit keycode encryption (given as a uint64_t number, but upper 8b aren't used) */
    /* keycode = keycode - 1 */
    if (keycode != 0)
        keycode--;

    /* init keycode table */
    for (int r = 0; r < (8-1); r++) {
        kc[r] = keycode & 0xFF;
        keycode = keycode >> 8;
    }

    /* init seed table */
    seed[ 0] = kc[1];
    seed[ 1] = kc[1] ^ kc[6];
    seed[ 2] = kc[2] ^ kc[3];
    seed[ 3] = kc[2];
    seed[ 4] = kc[2] ^ kc[1];
    seed[ 5] = kc[3] ^ kc[4];
    seed[ 6] = kc[3];
    seed[ 7] = kc[3] ^ kc[2];
    seed[ 8] = kc[4] ^ kc[5];
    seed[ 9] = kc[4];
    seed[10] = kc[4] ^ kc[3];
    seed[11] = kc[5] ^ kc[6];
    seed[12] = kc[5];
    seed[13] = kc[5] ^ kc[4];
    seed[14] = kc[6] ^ kc[1];
    seed[15] = kc[6];

    /* init base table */
    cipher_init56_create_table(base_r, kc[0]);
    for (int r = 0; r < 16; r++) {
        uint8_t nb;
        cipher_init56_create_table(base_c, seed[r]);
        nb = base_r[r] << 4;
        for (int c = 0; c < 16; c++)
            base[r*16 + c] = nb | base_c[c]; /* combine nibbles */
    }

    /* final shuffle table */
    {
        unsigned x = 0;
        unsigned pos = 1;

        for (int i = 0; i < 256; i++) {
            x = (x + 17) & 0xFF;
            if (base[x] != 0 && base[x] != 0xFF)
                cipher[pos++] = base[x];
        }
        cipher[0] = 0;
        cipher[0xFF] = 0xFF;
    }
}

static void cipher_init(uint8_t *cipher, int type, uint64_t keycode, uint16_t subkey)
{
    switch (type) {
    case 56:
        if (keycode) {
            if (subkey)
                keycode = keycode * (((uint64_t)subkey<<16u)|((uint16_t)~subkey+2u));
            cipher_init56(cipher, keycode);
        }
        break;
    case 0:
        for (int i = 0; i < 256; i++)
            cipher[i] = i;
        break;
    }
}

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

static av_cold void init_flush(AVCodecContext *avctx)
{
    HCAContext *c = avctx->priv_data;

    memset(c, 0, offsetof(HCAContext, tx_fn));
}

static int init_hca(AVCodecContext *avctx, const uint8_t *extradata,
                    const int extradata_size)
{
    HCAContext *c = avctx->priv_data;
    GetByteContext gb0, *const gb = &gb0;
    int8_t r[16] = { 0 };
    unsigned b, chunk;
    int version, ret;
    unsigned hfr_group_count;

    init_flush(avctx);

    if (extradata_size < 36)
        return AVERROR_INVALIDDATA;

    bytestream2_init(gb, extradata, extradata_size);

    bytestream2_skipu(gb, 4);
    version = bytestream2_get_be16(gb);
    bytestream2_skipu(gb, 2);

    c->ath_type = version >= 0x200 ? 0 : 1;

    if ((bytestream2_get_be32u(gb) & HCA_MASK) != MKBETAG('f', 'm', 't', 0))
        return AVERROR_INVALIDDATA;
    bytestream2_skipu(gb, 4);
    bytestream2_skipu(gb, 4);
    bytestream2_skipu(gb, 4);

    chunk = bytestream2_get_be32u(gb) & HCA_MASK;
    if (chunk == MKBETAG('c', 'o', 'm', 'p')) {
        bytestream2_skipu(gb, 2);
        bytestream2_skipu(gb, 1);
        bytestream2_skipu(gb, 1);
        c->track_count         = bytestream2_get_byteu(gb);
        c->channel_config      = bytestream2_get_byteu(gb);
        c->total_band_count    = bytestream2_get_byteu(gb);
        c->base_band_count     = bytestream2_get_byteu(gb);
        c->stereo_band_count   = bytestream2_get_byte (gb);
        c->bands_per_hfr_group = bytestream2_get_byte (gb);
    } else if (chunk == MKBETAG('d', 'e', 'c', 0)) {
        bytestream2_skipu(gb, 2);
        bytestream2_skipu(gb, 1);
        bytestream2_skipu(gb, 1);
        c->total_band_count = bytestream2_get_byteu(gb) + 1;
        c->base_band_count  = bytestream2_get_byteu(gb) + 1;
        c->track_count      = bytestream2_peek_byteu(gb) >> 4;
        c->channel_config   = bytestream2_get_byteu(gb) & 0xF;
        if (!bytestream2_get_byteu(gb))
            c->base_band_count = c->total_band_count;
        c->stereo_band_count = c->total_band_count - c->base_band_count;
        c->bands_per_hfr_group = 0;
    } else
        return AVERROR_INVALIDDATA;

    if (c->total_band_count > FF_ARRAY_ELEMS(c->ch->imdct_in))
        return AVERROR_INVALIDDATA;

    while (bytestream2_get_bytes_left(gb) >= 4) {
        chunk = bytestream2_get_be32u(gb) & HCA_MASK;
        if (chunk == MKBETAG('v', 'b', 'r', 0)) {
            bytestream2_skip(gb, 2 + 2);
        } else if (chunk == MKBETAG('a', 't', 'h', 0)) {
            c->ath_type = bytestream2_get_be16(gb);
        } else if (chunk == MKBETAG('r', 'v', 'a', 0)) {
            bytestream2_skip(gb, 4);
        } else if (chunk == MKBETAG('c', 'o', 'm', 'm')) {
            bytestream2_skip(gb, bytestream2_get_byte(gb) * 8);
        } else if (chunk == MKBETAG('c', 'i', 'p', 'h')) {
            c->ciph_type = bytestream2_get_be16(gb);
        } else if (chunk == MKBETAG('l', 'o', 'o', 'p')) {
            bytestream2_skip(gb, 4 + 4 + 2 + 2);
        } else if (chunk == MKBETAG('p', 'a', 'd', 0)) {
            break;
        } else {
            break;
        }
    }

    if (bytestream2_get_bytes_left(gb) >= 10) {
        bytestream2_skip(gb, bytestream2_get_bytes_left(gb) - 10);
        c->key = bytestream2_get_be64u(gb);
        c->subkey = bytestream2_get_be16u(gb);
    }

    cipher_init(c->cipher, c->ciph_type, c->key, c->subkey);

    ret = ath_init(c->ath, c->ath_type, avctx->sample_rate);
    if (ret < 0)
        return ret;

    if (!c->track_count)
        c->track_count = 1;

    b = avctx->ch_layout.nb_channels / c->track_count;
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

    hfr_group_count = ceil2(c->total_band_count - (c->base_band_count + c->stereo_band_count),
                               c->bands_per_hfr_group);

    if (c->base_band_count + c->stereo_band_count + (uint64_t)hfr_group_count > 128ULL)
        return AVERROR_INVALIDDATA;
    c->hfr_group_count = hfr_group_count;

    for (int i = 0; i < avctx->ch_layout.nb_channels; i++) {
        c->ch[i].chan_type = r[i];
        c->ch[i].count     = c->base_band_count + ((r[i] != 2) ? c->stereo_band_count : 0);
        c->ch[i].hfr_scale = &c->ch[i].scale_factors[c->base_band_count + c->stereo_band_count];
        if (c->ch[i].count > 128)
            return AVERROR_INVALIDDATA;
    }

    // Done last to signal init() finished
    c->crc_table = av_crc_get_table(AV_CRC_16_ANSI);

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    HCAContext *c = avctx->priv_data;
    float scale = 1.f / 8.f;
    int ret;

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    if (avctx->ch_layout.nb_channels <= 0 || avctx->ch_layout.nb_channels > FF_ARRAY_ELEMS(c->ch))
        return AVERROR(EINVAL);

    c->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!c->fdsp)
        return AVERROR(ENOMEM);

    ret = av_tx_init(&c->tx_ctx, &c->tx_fn, AV_TX_FLOAT_MDCT, 1, 128, &scale, 0);
    if (ret < 0)
        return ret;

    if (avctx->extradata_size != 0 && avctx->extradata_size < 36)
        return AVERROR_INVALIDDATA;

    if (!avctx->extradata_size)
        return 0;

    return init_hca(avctx, avctx->extradata, avctx->extradata_size);
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
        c2[i]  = c1[i] * ratio_r;
        c1[i] *= ratio_l;
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

static void dequantize_coefficients(HCAContext *c, ChannelContext *ch,
                                    GetBitContext *gb)
{
    const float *base = ch->base;
    float *factors = ch->factors;
    float *out = ch->imdct_in;

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
        factors[i] = factor;
    }

    memset(factors + ch->count, 0, 512 - ch->count * sizeof(*factors));
    c->fdsp->vector_fmul(out, factors, base, 128);
}

static void unpack(HCAContext *c, ChannelContext *ch,
                   GetBitContext *gb,
                   unsigned hfr_group_count,
                   int packed_noise_level,
                   const uint8_t *ath)
{
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

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame_ptr, AVPacket *avpkt)
{
    HCAContext *c = avctx->priv_data;
    int ch, offset = 0, ret, packed_noise_level;
    GetBitContext gb0, *const gb = &gb0;
    float **samples;

    if (avpkt->size <= 8)
        return AVERROR_INVALIDDATA;

    if (AV_RN16(avpkt->data) != 0xFFFF) {
        if ((AV_RL32(avpkt->data)) != MKTAG('H','C','A',0)) {
            return AVERROR_INVALIDDATA;
        } else if (AV_RB16(avpkt->data + 6) <= avpkt->size) {
            ret = init_hca(avctx, avpkt->data, AV_RB16(avpkt->data + 6));
            if (ret < 0) {
                c->crc_table = NULL; // signal that init has not finished
                return ret;
            }
            offset = AV_RB16(avpkt->data + 6);
            if (offset == avpkt->size)
                return avpkt->size;
        } else {
            return AVERROR_INVALIDDATA;
        }
    }

    if (!c->crc_table)
        return AVERROR_INVALIDDATA;

    if (c->key || c->subkey) {
        uint8_t *data, *cipher = c->cipher;

        if ((ret = av_packet_make_writable(avpkt)) < 0)
            return ret;
        data = avpkt->data;
        for (int n = 0; n < avpkt->size; n++)
            data[n] = cipher[data[n]];
    }

    if (avctx->err_recognition & AV_EF_CRCCHECK) {
        if (av_crc(c->crc_table, 0, avpkt->data + offset, avpkt->size - offset))
            return AVERROR_INVALIDDATA;
    }

    if ((ret = init_get_bits8(gb, avpkt->data + offset, avpkt->size - offset)) < 0)
        return ret;

    if (get_bits(gb, 16) != 0xFFFF)
        return AVERROR_INVALIDDATA;

    frame->nb_samples = 1024;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (float **)frame->extended_data;

    packed_noise_level = (get_bits(gb, 9) << 8) - get_bits(gb, 7);

    for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
        unpack(c, &c->ch[ch], gb, c->hfr_group_count, packed_noise_level, c->ath);

    for (int i = 0; i < 8; i++) {
        for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
            dequantize_coefficients(c, &c->ch[ch], gb);
        for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
            reconstruct_hfr(c, &c->ch[ch], c->hfr_group_count, c->bands_per_hfr_group,
                            c->stereo_band_count + c->base_band_count, c->total_band_count);
        for (ch = 0; ch < avctx->ch_layout.nb_channels - 1; ch++)
            apply_intensity_stereo(c, &c->ch[ch], &c->ch[ch+1], i,
                                   c->total_band_count - c->base_band_count,
                                   c->base_band_count, c->stereo_band_count);
        for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
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

static av_cold void decode_flush(AVCodecContext *avctx)
{
    HCAContext *c = avctx->priv_data;

    for (int ch = 0; ch < MAX_CHANNELS; ch++)
        memset(c->ch[ch].imdct_prev, 0, sizeof(c->ch[ch].imdct_prev));
}

const FFCodec ff_hca_decoder = {
    .p.name         = "hca",
    CODEC_LONG_NAME("CRI HCA"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_HCA,
    .priv_data_size = sizeof(HCAContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .flush          = decode_flush,
    .close          = decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
