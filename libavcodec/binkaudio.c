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

#include "avcodec.h"
#define ALT_BITSTREAM_READER_LE
#include "get_bits.h"
#include "dsputil.h"
#include "dct.h"
#include "rdft.h"
#include "fmtconvert.h"
#include "libavutil/intfloat_readwrite.h"

extern const uint16_t ff_wma_critical_freqs[25];

#define MAX_CHANNELS 2
#define BINK_BLOCK_MAX_SIZE (MAX_CHANNELS << 11)

typedef struct {
    GetBitContext gb;
    DSPContext dsp;
    FmtConvertContext fmt_conv;
    int version_b;          ///< Bink version 'b'
    int first;
    int channels;
    int frame_len;          ///< transform size (samples)
    int overlap_len;        ///< overlap size (samples)
    int block_size;
    int num_bands;
    unsigned int *bands;
    float root;
    DECLARE_ALIGNED(32, FFTSample, coeffs)[BINK_BLOCK_MAX_SIZE];
    DECLARE_ALIGNED(16, short, previous)[BINK_BLOCK_MAX_SIZE / 16];  ///< coeffs from previous audio block
    float *coeffs_ptr[MAX_CHANNELS]; ///< pointers to the coeffs arrays for float_to_int16_interleave
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
    int i;
    int frame_len_bits;

    dsputil_init(&s->dsp, avctx);
    ff_fmt_convert_init(&s->fmt_conv, avctx);

    /* determine frame length */
    if (avctx->sample_rate < 22050) {
        frame_len_bits = 9;
    } else if (avctx->sample_rate < 44100) {
        frame_len_bits = 10;
    } else {
        frame_len_bits = 11;
    }

    if (avctx->channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "too many channels: %d\n", avctx->channels);
        return -1;
    }

    if (avctx->extradata && avctx->extradata_size > 0)
        s->version_b = avctx->extradata[0];

    if (avctx->codec->id == CODEC_ID_BINKAUDIO_RDFT) {
        // audio is already interleaved for the RDFT format variant
        sample_rate  *= avctx->channels;
        s->channels = 1;
        if (!s->version_b)
            frame_len_bits += av_log2(avctx->channels);
    } else {
        s->channels = avctx->channels;
    }

    s->frame_len     = 1 << frame_len_bits;
    s->overlap_len   = s->frame_len / 16;
    s->block_size    = (s->frame_len - s->overlap_len) * s->channels;
    sample_rate_half = (sample_rate + 1) / 2;
    s->root          = 2.0 / sqrt(s->frame_len);

    /* calculate number of bands */
    for (s->num_bands = 1; s->num_bands < 25; s->num_bands++)
        if (sample_rate_half <= ff_wma_critical_freqs[s->num_bands - 1])
            break;

    s->bands = av_malloc((s->num_bands + 1) * sizeof(*s->bands));
    if (!s->bands)
        return AVERROR(ENOMEM);

    /* populate bands data */
    s->bands[0] = 2;
    for (i = 1; i < s->num_bands; i++)
        s->bands[i] = (ff_wma_critical_freqs[i - 1] * s->frame_len / sample_rate_half) & ~1;
    s->bands[s->num_bands] = s->frame_len;

    s->first = 1;
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    for (i = 0; i < s->channels; i++)
        s->coeffs_ptr[i] = s->coeffs + i * s->frame_len;

    if (CONFIG_BINKAUDIO_RDFT_DECODER && avctx->codec->id == CODEC_ID_BINKAUDIO_RDFT)
        ff_rdft_init(&s->trans.rdft, frame_len_bits, DFT_C2R);
    else if (CONFIG_BINKAUDIO_DCT_DECODER)
        ff_dct_init(&s->trans.dct, frame_len_bits, DCT_III);
    else
        return -1;

    return 0;
}

static float get_float(GetBitContext *gb)
{
    int power = get_bits(gb, 5);
    float f = ldexpf(get_bits_long(gb, 23), power - 23);
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
 */
static void decode_block(BinkAudioContext *s, short *out, int use_dct)
{
    int ch, i, j, k;
    float q, quant[25];
    int width, coeff;
    GetBitContext *gb = &s->gb;

    if (use_dct)
        skip_bits(gb, 2);

    for (ch = 0; ch < s->channels; ch++) {
        FFTSample *coeffs = s->coeffs_ptr[ch];
        if (s->version_b) {
            coeffs[0] = av_int2flt(get_bits(gb, 32)) * s->root;
            coeffs[1] = av_int2flt(get_bits(gb, 32)) * s->root;
        } else {
            coeffs[0] = get_float(gb) * s->root;
            coeffs[1] = get_float(gb) * s->root;
        }

        for (i = 0; i < s->num_bands; i++) {
            /* constant is result of 0.066399999/log10(M_E) */
            int value = get_bits(gb, 8);
            quant[i] = expf(FFMIN(value, 95) * 0.15289164787221953823f) * s->root;
        }

        k = 0;
        q = quant[0];

        // parse coefficients
        i = 2;
        while (i < s->frame_len) {
            if (s->version_b) {
                j = i + 16;
            } else if (get_bits1(gb)) {
                j = i + rle_length_tab[get_bits(gb, 4)] * 8;
            } else {
                j = i + 8;
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
                        if (get_bits1(gb))
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
            s->dsp.vector_fmul_scalar(coeffs, coeffs, s->frame_len / 2, s->frame_len);
        }
        else if (CONFIG_BINKAUDIO_RDFT_DECODER)
            s->trans.rdft.rdft_calc(&s->trans.rdft, coeffs);
    }

    s->fmt_conv.float_to_int16_interleave(out, (const float **)s->coeffs_ptr,
                                          s->frame_len, s->channels);

    if (!s->first) {
        int count = s->overlap_len * s->channels;
        int shift = av_log2(count);
        for (i = 0; i < count; i++) {
            out[i] = (s->previous[i] * (count - i) + out[i] * i) >> shift;
        }
    }

    memcpy(s->previous, out + s->block_size,
           s->overlap_len * s->channels * sizeof(*out));

    s->first = 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    BinkAudioContext * s = avctx->priv_data;
    av_freep(&s->bands);
    if (CONFIG_BINKAUDIO_RDFT_DECODER && avctx->codec->id == CODEC_ID_BINKAUDIO_RDFT)
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

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    BinkAudioContext *s = avctx->priv_data;
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    short *samples      = data;
    short *samples_end  = (short*)((uint8_t*)data + *data_size);
    int reported_size;
    GetBitContext *gb = &s->gb;

    init_get_bits(gb, buf, buf_size * 8);

    reported_size = get_bits_long(gb, 32);
    while (get_bits_count(gb) / 8 < buf_size &&
           samples + s->block_size <= samples_end) {
        decode_block(s, samples, avctx->codec->id == CODEC_ID_BINKAUDIO_DCT);
        samples += s->block_size;
        get_bits_align32(gb);
    }

    *data_size = FFMIN(reported_size, (uint8_t*)samples - (uint8_t*)data);
    return buf_size;
}

AVCodec ff_binkaudio_rdft_decoder = {
    "binkaudio_rdft",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_BINKAUDIO_RDFT,
    sizeof(BinkAudioContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Bink Audio (RDFT)")
};

AVCodec ff_binkaudio_dct_decoder = {
    "binkaudio_dct",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_BINKAUDIO_DCT,
    sizeof(BinkAudioContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Bink Audio (DCT)")
};
