/*
 * Bink Audio decoder
 * Copyright (c) 2007-2011 Peter Ross (pross@xvid.org)
 * Copyright (c) 2009 Daniel Verkamp (daniel@drv.nu)
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
 * Bink Audio decoder
 *
 * Technical details here:
 *  http://wiki.multimedia.cx/index.php?title=Bink_Audio
 */

#include "avcodec.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "dsputil.h"
#include "dct.h"
#include "rdft.h"
#include "fmtconvert.h"
#include "libavutil/intfloat.h"

extern const uint16_t ff_wma_critical_freqs[25];

static float quant_table[96];

#define MAX_CHANNELS 2
#define BINK_BLOCK_MAX_SIZE (MAX_CHANNELS << 11)

typedef struct {
    AVFrame frame;
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
    DECLARE_ALIGNED(16, int16_t, previous)[BINK_BLOCK_MAX_SIZE / 16];  ///< coeffs from previous audio block
    DECLARE_ALIGNED(16, int16_t, current)[BINK_BLOCK_MAX_SIZE / 16];
    float *coeffs_ptr[MAX_CHANNELS]; ///< pointers to the coeffs arrays for float_to_int16_interleave
    float *prev_ptr[MAX_CHANNELS];   ///< pointers to the overlap points in the coeffs array
    uint8_t *packet_buffer;
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

    ff_dsputil_init(&s->dsp, avctx);
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

    s->version_b = avctx->extradata && avctx->extradata[3] == 'b';

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
    for (i = 0; i < 96; i++) {
        /* constant is result of 0.066399999/log10(M_E) */
        quant_table[i] = expf(i * 0.15289164787221953823f) * s->root;
    }

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

    for (i = 0; i < s->channels; i++) {
        s->coeffs_ptr[i] = s->coeffs + i * s->frame_len;
        s->prev_ptr[i]   = s->coeffs_ptr[i] + s->frame_len - s->overlap_len;
    }

    if (CONFIG_BINKAUDIO_RDFT_DECODER && avctx->codec->id == CODEC_ID_BINKAUDIO_RDFT)
        ff_rdft_init(&s->trans.rdft, frame_len_bits, DFT_C2R);
    else if (CONFIG_BINKAUDIO_DCT_DECODER)
        ff_dct_init(&s->trans.dct, frame_len_bits, DCT_III);
    else
        return -1;

    avcodec_get_frame_defaults(&s->frame);
    avctx->coded_frame = &s->frame;

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

#define GET_BITS_SAFE(out, nbits) do {  \
    if (get_bits_left(gb) < nbits)      \
        return AVERROR_INVALIDDATA;     \
    out = get_bits(gb, nbits);          \
} while (0)

/**
 * Decode Bink Audio block
 * @param[out] out Output buffer (must contain s->block_size elements)
 * @return 0 on success, negative error code on failure
 */
static int decode_block(BinkAudioContext *s, int16_t *out, int use_dct)
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
            quant[i]  = quant_table[FFMIN(value, 95)];
        }

        k = 0;
        q = quant[0];

        // parse coefficients
        i = 2;
        while (i < s->frame_len) {
            if (s->version_b) {
                j = i + 16;
            } else {
                int v;
                GET_BITS_SAFE(v, 1);
                if (v) {
                    GET_BITS_SAFE(v, 4);
                    j = i + rle_length_tab[v] * 8;
                } else {
                    j = i + 8;
                }
            }

            j = FFMIN(j, s->frame_len);

            GET_BITS_SAFE(width, 4);
            if (width == 0) {
                memset(coeffs + i, 0, (j - i) * sizeof(*coeffs));
                i = j;
                while (s->bands[k] < i)
                    q = quant[k++];
            } else {
                while (i < j) {
                    if (s->bands[k] == i)
                        q = quant[k++];
                    GET_BITS_SAFE(coeff, width);
                    if (coeff) {
                        int v;
                        GET_BITS_SAFE(v, 1);
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
            s->dsp.vector_fmul_scalar(coeffs, coeffs, s->frame_len / 2, s->frame_len);
        }
        else if (CONFIG_BINKAUDIO_RDFT_DECODER)
            s->trans.rdft.rdft_calc(&s->trans.rdft, coeffs);
    }

    s->fmt_conv.float_to_int16_interleave(s->current,
                                          (const float **)s->prev_ptr,
                                          s->overlap_len, s->channels);
    s->fmt_conv.float_to_int16_interleave(out, (const float **)s->coeffs_ptr,
                                          s->frame_len - s->overlap_len,
                                          s->channels);

    if (!s->first) {
        int count = s->overlap_len * s->channels;
        int shift = av_log2(count);
        for (i = 0; i < count; i++) {
            out[i] = (s->previous[i] * (count - i) + out[i] * i) >> shift;
        }
    }

    memcpy(s->previous, s->current,
           s->overlap_len * s->channels * sizeof(*s->previous));

    s->first = 0;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    BinkAudioContext * s = avctx->priv_data;
    av_freep(&s->bands);
    av_freep(&s->packet_buffer);
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

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame_ptr, AVPacket *avpkt)
{
    BinkAudioContext *s = avctx->priv_data;
    int16_t *samples;
    GetBitContext *gb = &s->gb;
    int ret, consumed = 0;

    if (!get_bits_left(gb)) {
        uint8_t *buf;
        /* handle end-of-stream */
        if (!avpkt->size) {
            *got_frame_ptr = 0;
            return 0;
        }
        if (avpkt->size < 4) {
            av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
            return AVERROR_INVALIDDATA;
        }
        buf = av_realloc(s->packet_buffer, avpkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!buf)
            return AVERROR(ENOMEM);
        s->packet_buffer = buf;
        memcpy(s->packet_buffer, avpkt->data, avpkt->size);
        init_get_bits(gb, s->packet_buffer, avpkt->size * 8);
        consumed = avpkt->size;

        /* skip reported size */
        skip_bits_long(gb, 32);
    }

    /* get output buffer */
    s->frame.nb_samples = s->block_size / avctx->channels;
    if ((ret = avctx->get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    samples = (int16_t *)s->frame.data[0];

    if (decode_block(s, samples, avctx->codec->id == CODEC_ID_BINKAUDIO_DCT)) {
        av_log(avctx, AV_LOG_ERROR, "Incomplete packet\n");
        return AVERROR_INVALIDDATA;
    }
    get_bits_align32(gb);

    *got_frame_ptr   = 1;
    *(AVFrame *)data = s->frame;

    return consumed;
}

AVCodec ff_binkaudio_rdft_decoder = {
    .name           = "binkaudio_rdft",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_BINKAUDIO_RDFT,
    .priv_data_size = sizeof(BinkAudioContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DELAY | CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Bink Audio (RDFT)")
};

AVCodec ff_binkaudio_dct_decoder = {
    .name           = "binkaudio_dct",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_BINKAUDIO_DCT,
    .priv_data_size = sizeof(BinkAudioContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DELAY | CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Bink Audio (DCT)")
};
