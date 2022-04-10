/*
 * Bink Audio decoder
 * Copyright (c) 2007-2011 Peter Ross (pross@xvid.org)
 * Copyright (c) 2009 Daniel Verkamp (daniel@drv.nu)
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
 * Bink Audio decoder
 *
 * Technical details here:
 *  http://wiki.multimedia.cx/index.php?title=Bink_Audio
 */

#include "config_components.h"

#include "libavutil/channel_layout.h"
#include "libavutil/intfloat.h"
#include "libavutil/mem_internal.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "dct.h"
#include "decode.h"
#include "get_bits.h"
#include "codec_internal.h"
#include "internal.h"
#include "rdft.h"
#include "wma_freqs.h"

#define MAX_DCT_CHANNELS 6
#define MAX_CHANNELS 2
#define BINK_BLOCK_MAX_SIZE (MAX_CHANNELS << 11)

typedef struct BinkAudioContext {
    GetBitContext gb;
    int version_b;          ///< Bink version 'b'
    int first;
    int channels;
    int ch_offset;
    int frame_len;          ///< transform size (samples)
    int overlap_len;        ///< overlap size (samples)
    int block_size;
    int num_bands;
    float root;
    unsigned int bands[26];
    float previous[MAX_DCT_CHANNELS][BINK_BLOCK_MAX_SIZE / 16];  ///< coeffs from previous audio block
    float quant_table[96];
    AVPacket *pkt;
    union {
        RDFTContext rdft;
        DCTContext dct;
    } trans;
} BinkAudioContext;


static av_cold int decode_init(AVCodecContext *avctx)
{
    BinkAudioContext *s = avctx->priv_data;
    int sample_rate = avctx->sample_rate;
    int sample_rate_half;
    int i, ret;
    int frame_len_bits;
    int max_channels = avctx->codec->id == AV_CODEC_ID_BINKAUDIO_RDFT ? MAX_CHANNELS : MAX_DCT_CHANNELS;
    int channels = avctx->ch_layout.nb_channels;

    /* determine frame length */
    if (avctx->sample_rate < 22050) {
        frame_len_bits = 9;
    } else if (avctx->sample_rate < 44100) {
        frame_len_bits = 10;
    } else {
        frame_len_bits = 11;
    }

    if (channels < 1 || channels > max_channels) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of channels: %d\n", channels);
        return AVERROR_INVALIDDATA;
    }
    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_default(&avctx->ch_layout, channels);

    s->version_b = avctx->extradata_size >= 4 && avctx->extradata[3] == 'b';

    if (avctx->codec->id == AV_CODEC_ID_BINKAUDIO_RDFT) {
        // audio is already interleaved for the RDFT format variant
        avctx->sample_fmt = AV_SAMPLE_FMT_FLT;
        if (sample_rate > INT_MAX / channels)
            return AVERROR_INVALIDDATA;
        sample_rate  *= channels;
        s->channels = 1;
        if (!s->version_b)
            frame_len_bits += av_log2(channels);
    } else {
        s->channels = channels;
        avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    }

    s->frame_len     = 1 << frame_len_bits;
    s->overlap_len   = s->frame_len / 16;
    s->block_size    = (s->frame_len - s->overlap_len) * FFMIN(MAX_CHANNELS, s->channels);
    sample_rate_half = (sample_rate + 1LL) / 2;
    if (avctx->codec->id == AV_CODEC_ID_BINKAUDIO_RDFT)
        s->root = 2.0 / (sqrt(s->frame_len) * 32768.0);
    else
        s->root = s->frame_len / (sqrt(s->frame_len) * 32768.0);
    for (i = 0; i < 96; i++) {
        /* constant is result of 0.066399999/log10(M_E) */
        s->quant_table[i] = expf(i * 0.15289164787221953823f) * s->root;
    }

    /* calculate number of bands */
    for (s->num_bands = 1; s->num_bands < 25; s->num_bands++)
        if (sample_rate_half <= ff_wma_critical_freqs[s->num_bands - 1])
            break;

    /* populate bands data */
    s->bands[0] = 2;
    for (i = 1; i < s->num_bands; i++)
        s->bands[i] = (ff_wma_critical_freqs[i - 1] * s->frame_len / sample_rate_half) & ~1;
    s->bands[s->num_bands] = s->frame_len;

    s->first = 1;

    if (CONFIG_BINKAUDIO_RDFT_DECODER && avctx->codec->id == AV_CODEC_ID_BINKAUDIO_RDFT)
        ret = ff_rdft_init(&s->trans.rdft, frame_len_bits, DFT_C2R);
    else if (CONFIG_BINKAUDIO_DCT_DECODER)
        ret = ff_dct_init(&s->trans.dct, frame_len_bits, DCT_III);
    else
        av_assert0(0);
    if (ret < 0)
        return ret;

    s->pkt = avctx->internal->in_pkt;

    return 0;
}

static float get_float(GetBitContext *gb)
{
    int power = get_bits(gb, 5);
    float f = ldexpf(get_bits(gb, 23), power - 23);
    if (get_bits1(gb))
        f = -f;
    return f;
}

static const uint8_t rle_length_tab[16] = {
    2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64
};

/**
 * Decode Bink Audio block
 * @param[out] out Output buffer (must contain s->block_size elements)
 * @return 0 on success, negative error code on failure
 */
static int decode_block(BinkAudioContext *s, float **out, int use_dct,
                        int channels, int ch_offset)
{
    int ch, i, j, k;
    float q, quant[25];
    int width, coeff;
    GetBitContext *gb = &s->gb;

    if (use_dct)
        skip_bits(gb, 2);

    for (ch = 0; ch < channels; ch++) {
        FFTSample *coeffs = out[ch + ch_offset];

        if (s->version_b) {
            if (get_bits_left(gb) < 64)
                return AVERROR_INVALIDDATA;
            coeffs[0] = av_int2float(get_bits_long(gb, 32)) * s->root;
            coeffs[1] = av_int2float(get_bits_long(gb, 32)) * s->root;
        } else {
            if (get_bits_left(gb) < 58)
                return AVERROR_INVALIDDATA;
            coeffs[0] = get_float(gb) * s->root;
            coeffs[1] = get_float(gb) * s->root;
        }

        if (get_bits_left(gb) < s->num_bands * 8)
            return AVERROR_INVALIDDATA;
        for (i = 0; i < s->num_bands; i++) {
            int value = get_bits(gb, 8);
            quant[i]  = s->quant_table[FFMIN(value, 95)];
        }

        k = 0;
        q = quant[0];

        // parse coefficients
        i = 2;
        while (i < s->frame_len) {
            if (s->version_b) {
                j = i + 16;
            } else {
                int v = get_bits1(gb);
                if (v) {
                    v = get_bits(gb, 4);
                    j = i + rle_length_tab[v] * 8;
                } else {
                    j = i + 8;
                }
            }

            j = FFMIN(j, s->frame_len);

            width = get_bits(gb, 4);
            if (width == 0) {
                memset(coeffs + i, 0, (j - i) * sizeof(*coeffs));
                i = j;
                while (s->bands[k] < i)
                    q = quant[k++];
            } else {
                while (i < j) {
                    if (s->bands[k] == i)
                        q = quant[k++];
                    coeff = get_bits(gb, width);
                    if (coeff) {
                        int v;
                        v = get_bits1(gb);
                        if (v)
                            coeffs[i] = -q * coeff;
                        else
                            coeffs[i] =  q * coeff;
                    } else {
                        coeffs[i] = 0.0f;
                    }
                    i++;
                }
            }
        }

        if (CONFIG_BINKAUDIO_DCT_DECODER && use_dct) {
            coeffs[0] /= 0.5;
            s->trans.dct.dct_calc(&s->trans.dct,  coeffs);
        }
        else if (CONFIG_BINKAUDIO_RDFT_DECODER)
            s->trans.rdft.rdft_calc(&s->trans.rdft, coeffs);
    }

    for (ch = 0; ch < channels; ch++) {
        int j;
        int count = s->overlap_len * channels;
        if (!s->first) {
            j = ch;
            for (i = 0; i < s->overlap_len; i++, j += channels)
                out[ch + ch_offset][i] = (s->previous[ch + ch_offset][i] * (count - j) +
                                                  out[ch + ch_offset][i] *          j) / count;
        }
        memcpy(s->previous[ch + ch_offset], &out[ch + ch_offset][s->frame_len - s->overlap_len],
               s->overlap_len * sizeof(*s->previous[ch + ch_offset]));
    }

    s->first = 0;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    BinkAudioContext * s = avctx->priv_data;
    if (CONFIG_BINKAUDIO_RDFT_DECODER && avctx->codec->id == AV_CODEC_ID_BINKAUDIO_RDFT)
        ff_rdft_end(&s->trans.rdft);
    else if (CONFIG_BINKAUDIO_DCT_DECODER)
        ff_dct_end(&s->trans.dct);

    return 0;
}

static void get_bits_align32(GetBitContext *s)
{
    int n = (-get_bits_count(s)) & 31;
    if (n) skip_bits(s, n);
}

static int binkaudio_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    BinkAudioContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int ret;

again:
    if (!s->pkt->data) {
        ret = ff_decode_get_packet(avctx, s->pkt);
        if (ret < 0)
            return ret;

        if (s->pkt->size < 4) {
            av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        ret = init_get_bits8(gb, s->pkt->data, s->pkt->size);
        if (ret < 0)
            goto fail;

        /* skip reported size */
        skip_bits_long(gb, 32);
    }

    /* get output buffer */
    if (s->ch_offset == 0) {
        frame->nb_samples = s->frame_len;
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;
    }

    if (decode_block(s, (float **)frame->extended_data,
                     avctx->codec->id == AV_CODEC_ID_BINKAUDIO_DCT,
                     FFMIN(MAX_CHANNELS, s->channels - s->ch_offset), s->ch_offset)) {
        av_log(avctx, AV_LOG_ERROR, "Incomplete packet\n");
        s->ch_offset = 0;
        return AVERROR_INVALIDDATA;
    }
    s->ch_offset += MAX_CHANNELS;
    get_bits_align32(gb);
    if (!get_bits_left(gb)) {
        memset(gb, 0, sizeof(*gb));
        av_packet_unref(s->pkt);
    }
    if (s->ch_offset >= s->channels) {
        s->ch_offset = 0;
    } else {
        goto again;
    }

    frame->nb_samples = s->block_size / FFMIN(avctx->ch_layout.nb_channels, MAX_CHANNELS);

    return 0;
fail:
    s->ch_offset = 0;
    av_packet_unref(s->pkt);
    return ret;
}

static void decode_flush(AVCodecContext *avctx)
{
    BinkAudioContext *const s = avctx->priv_data;

    /* s->pkt coincides with avctx->internal->in_pkt
     * and is unreferenced generically when flushing. */
    s->first = 1;
    s->ch_offset = 0;
}

const FFCodec ff_binkaudio_rdft_decoder = {
    .p.name         = "binkaudio_rdft",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Bink Audio (RDFT)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_BINKAUDIO_RDFT,
    .priv_data_size = sizeof(BinkAudioContext),
    .init           = decode_init,
    .flush          = decode_flush,
    .close          = decode_end,
    FF_CODEC_RECEIVE_FRAME_CB(binkaudio_receive_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};

const FFCodec ff_binkaudio_dct_decoder = {
    .p.name         = "binkaudio_dct",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Bink Audio (DCT)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_BINKAUDIO_DCT,
    .priv_data_size = sizeof(BinkAudioContext),
    .init           = decode_init,
    .flush          = decode_flush,
    .close          = decode_end,
    FF_CODEC_RECEIVE_FRAME_CB(binkaudio_receive_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
