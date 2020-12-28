/*
 * QDMC compatible decoder
 * Copyright (c) 2017 Paul B Mahol
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

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define BITSTREAM_READER_LE

#include "libavutil/channel_layout.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "fft.h"

typedef struct QDMCTone {
    uint8_t mode;
    uint8_t phase;
    uint8_t offset;
    int16_t freq;
    int16_t amplitude;
} QDMCTone;

typedef struct QDMCContext {
    AVCodecContext *avctx;

    uint8_t frame_bits;
    int band_index;
    int frame_size;
    int subframe_size;
    int fft_offset;
    int buffer_offset;
    int nb_channels;
    int checksum_size;

    uint8_t noise[2][19][17];
    QDMCTone tones[5][8192];
    int nb_tones[5];
    int cur_tone[5];
    float alt_sin[5][31];
    float fft_buffer[4][8192 * 2];
    float noise2_buffer[4096 * 2];
    float noise_buffer[4096 * 2];
    float buffer[2 * 32768];
    float *buffer_ptr;
    int rndval;

    DECLARE_ALIGNED(32, FFTComplex, cmplx)[2][512];
    FFTContext fft_ctx;
} QDMCContext;

static float sin_table[512];
static VLC vtable[6];

static const unsigned code_prefix[] = {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x6, 0x8, 0xA,
    0xC, 0x10, 0x14, 0x18, 0x1C, 0x24, 0x2C, 0x34,
    0x3C, 0x4C, 0x5C, 0x6C, 0x7C, 0x9C, 0xBC, 0xDC,
    0xFC, 0x13C, 0x17C, 0x1BC, 0x1FC, 0x27C, 0x2FC, 0x37C,
    0x3FC, 0x4FC, 0x5FC, 0x6FC, 0x7FC, 0x9FC, 0xBFC, 0xDFC,
    0xFFC, 0x13FC, 0x17FC, 0x1BFC, 0x1FFC, 0x27FC, 0x2FFC, 0x37FC,
    0x3FFC, 0x4FFC, 0x5FFC, 0x6FFC, 0x7FFC, 0x9FFC, 0xBFFC, 0xDFFC,
    0xFFFC, 0x13FFC, 0x17FFC, 0x1BFFC, 0x1FFFC, 0x27FFC, 0x2FFFC, 0x37FFC,
    0x3FFFC
};

static const float amplitude_tab[64] = {
    1.18750000f, 1.68359380f, 2.37500000f, 3.36718750f, 4.75000000f,
    6.73437500f, 9.50000000f, 13.4687500f, 19.0000000f, 26.9375000f,
    38.0000000f, 53.8750000f, 76.0000000f, 107.750000f, 152.000000f,
    215.500000f, 304.000000f, 431.000000f, 608.000000f, 862.000000f,
    1216.00000f, 1724.00000f, 2432.00000f, 3448.00000f, 4864.00000f,
    6896.00000f, 9728.00000f, 13792.0000f, 19456.0000f, 27584.0000f,
    38912.0000f, 55168.0000f, 77824.0000f, 110336.000f, 155648.000f,
    220672.000f, 311296.000f, 441344.000f, 622592.000f, 882688.000f,
    1245184.00f, 1765376.00f, 2490368.00f, 3530752.00f, 4980736.00f,
    7061504.00f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint16_t qdmc_nodes[112] = {
    0, 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 56, 64,
    80, 96, 120, 144, 176, 208, 240, 256,
    0, 2, 4, 8, 16, 24, 32, 48, 56, 64, 80, 104,
    128, 160, 208, 256, 0, 0, 0, 0, 0,
    0, 2, 4, 8, 16, 32, 48, 64, 80, 112, 160, 208,
    256, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 4, 8, 16, 32, 48, 64, 96, 144, 208, 256,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 4, 16, 32, 64, 256, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t noise_bands_size[] = {
    19, 14, 11, 9, 4, 2, 0
};

static const uint8_t noise_bands_selector[] = {
    4, 3, 2, 1, 0, 0, 0,
};

static const uint8_t qdmc_hufftab[][2] = {
    /* Noise value - 27 entries */
    {  1,  2 }, { 10,  7 }, { 26,  9 }, { 22,  9 }, { 24,  9 }, { 14,  9 },
    {  8,  6 }, {  6,  5 }, {  7,  5 }, {  9,  7 }, { 30,  9 }, { 32, 10 },
    { 13, 10 }, { 20,  9 }, { 28,  9 }, { 12,  7 }, { 15, 11 }, { 36, 12 },
    {  0, 12 }, { 34, 10 }, { 18,  9 }, { 11,  9 }, { 16,  9 }, {  5,  3 },
    {  2,  3 }, {  4,  3 }, {  3,  2 },
    /* Noise segment length - 12 entries */
    {  1,  1 }, {  2,  2 }, {  3,  4 }, {  8,  9 }, {  9, 10 }, {  0, 10 },
    { 13,  8 }, {  7,  7 }, {  6,  6 }, { 17,  5 }, {  4,  4 }, {  5,  4 },
    /* Amplitude - 28 entries */
    { 18,  3 }, { 16,  3 }, { 22,  7 }, {  8, 10 }, {  4, 10 }, {  3,  9 },
    {  2,  8 }, { 23,  8 }, { 10,  8 }, { 11,  7 }, { 21,  5 }, { 20,  4 },
    {  1,  7 }, {  7, 10 }, {  5, 10 }, {  9,  9 }, {  6, 10 }, { 25, 11 },
    { 26, 12 }, { 27, 13 }, {  0, 13 }, { 24,  9 }, { 12,  6 }, { 13,  5 },
    { 14,  4 }, { 19,  3 }, { 15,  3 }, { 17,  2 },
    /* Frequency differences - 47 entries */
    {  2,  4 }, { 14,  6 }, { 26,  7 }, { 31,  8 }, { 32,  9 }, { 35,  9 },
    {  7,  5 }, { 10,  5 }, { 22,  7 }, { 27,  7 }, { 19,  7 }, { 20,  7 },
    {  4,  5 }, { 13,  5 }, { 17,  6 }, { 15,  6 }, {  8,  5 }, {  5,  4 },
    { 28,  7 }, { 33,  9 }, { 36, 11 }, { 38, 12 }, { 42, 14 }, { 45, 16 },
    { 44, 18 }, {  0, 18 }, { 46, 17 }, { 43, 15 }, { 40, 13 }, { 37, 11 },
    { 39, 12 }, { 41, 12 }, { 34,  8 }, { 16,  6 }, { 11,  5 }, {  9,  4 },
    {  1,  2 }, {  3,  4 }, { 30,  7 }, { 29,  7 }, { 23,  6 }, { 24,  6 },
    { 18,  6 }, {  6,  4 }, { 12,  5 }, { 21,  6 }, { 25,  6 },
    /* Amplitude differences - 9 entries */
    {  1,  2 }, {  3,  3 }, {  4,  4 }, {  5,  5 }, {  6,  6 }, {  7,  7 },
    {  8,  8 }, {  0,  8 }, {  2,  1 },
    /* Phase differences - 9 entries */
    {  2,  2 }, {  1,  2 }, {  3,  4 }, {  7,  4 }, {  6,  5 }, {  5,  6 },
    {  0,  6 }, {  4,  4 }, {  8,  2 },
};

static const uint8_t huff_sizes[] = {
    27, 12, 28, 47, 9, 9
};

static const uint8_t huff_bits[] = {
    12, 10, 12, 12, 8, 6
};

static av_cold void qdmc_init_static_data(void)
{
    const uint8_t (*hufftab)[2] = qdmc_hufftab;
    int i;

    for (unsigned i = 0, offset = 0; i < FF_ARRAY_ELEMS(vtable); i++) {
        static VLC_TYPE vlc_buffer[13698][2];
        vtable[i].table           = &vlc_buffer[offset];
        vtable[i].table_allocated = FF_ARRAY_ELEMS(vlc_buffer) - offset;
        ff_init_vlc_from_lengths(&vtable[i], huff_bits[i], huff_sizes[i],
                                 &hufftab[0][1], 2, &hufftab[0][0], 2, 1, -1,
                                 INIT_VLC_LE | INIT_VLC_STATIC_OVERLONG, NULL);
        hufftab += huff_sizes[i];
        offset  += vtable[i].table_size;
    }

    for (i = 0; i < 512; i++)
        sin_table[i] = sin(2.0f * i * M_PI * 0.001953125f);
}

static void make_noises(QDMCContext *s)
{
    int i, j, n0, n1, n2, diff;
    float *nptr;

    for (j = 0; j < noise_bands_size[s->band_index]; j++) {
        n0 = qdmc_nodes[j + 21 * s->band_index    ];
        n1 = qdmc_nodes[j + 21 * s->band_index + 1];
        n2 = qdmc_nodes[j + 21 * s->band_index + 2];
        nptr = s->noise_buffer + 256 * j;

        for (i = 0; i + n0 < n1; i++, nptr++)
            nptr[0] = i / (float)(n1 - n0);

        diff = n2 - n1;
        nptr = s->noise_buffer + (j << 8) + n1 - n0;

        for (i = n1; i < n2; i++, nptr++, diff--)
            nptr[0] = diff / (float)(n2 - n1);
    }
}

static av_cold int qdmc_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    QDMCContext *s = avctx->priv_data;
    int ret, fft_size, fft_order, size, g, j, x;
    GetByteContext b;

    ff_thread_once(&init_static_once, qdmc_init_static_data);

    if (!avctx->extradata || (avctx->extradata_size < 48)) {
        av_log(avctx, AV_LOG_ERROR, "extradata missing or truncated\n");
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&b, avctx->extradata, avctx->extradata_size);

    while (bytestream2_get_bytes_left(&b) > 8) {
        if (bytestream2_peek_be64(&b) == (((uint64_t)MKBETAG('f','r','m','a') << 32) |
                                           (uint64_t)MKBETAG('Q','D','M','C')))
            break;
        bytestream2_skipu(&b, 1);
    }
    bytestream2_skipu(&b, 8);

    if (bytestream2_get_bytes_left(&b) < 36) {
        av_log(avctx, AV_LOG_ERROR, "not enough extradata (%i)\n",
               bytestream2_get_bytes_left(&b));
        return AVERROR_INVALIDDATA;
    }

    size = bytestream2_get_be32u(&b);
    if (size > bytestream2_get_bytes_left(&b)) {
        av_log(avctx, AV_LOG_ERROR, "extradata size too small, %i < %i\n",
               bytestream2_get_bytes_left(&b), size);
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_be32u(&b) != MKBETAG('Q','D','C','A')) {
        av_log(avctx, AV_LOG_ERROR, "invalid extradata, expecting QDCA\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skipu(&b, 4);

    avctx->channels = s->nb_channels = bytestream2_get_be32u(&b);
    if (s->nb_channels <= 0 || s->nb_channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of channels\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->channel_layout = avctx->channels == 2 ? AV_CH_LAYOUT_STEREO :
                                                   AV_CH_LAYOUT_MONO;

    avctx->sample_rate = bytestream2_get_be32u(&b);
    avctx->bit_rate = bytestream2_get_be32u(&b);
    bytestream2_skipu(&b, 4);
    fft_size = bytestream2_get_be32u(&b);
    fft_order = av_log2(fft_size) + 1;
    s->checksum_size = bytestream2_get_be32u(&b);
    if (s->checksum_size >= 1U << 28) {
        av_log(avctx, AV_LOG_ERROR, "data block size too large (%u)\n", s->checksum_size);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->sample_rate >= 32000) {
        x = 28000;
        s->frame_bits = 13;
    } else if (avctx->sample_rate >= 16000) {
        x = 20000;
        s->frame_bits = 12;
    } else {
        x = 16000;
        s->frame_bits = 11;
    }
    s->frame_size = 1 << s->frame_bits;
    s->subframe_size = s->frame_size >> 5;

    if (avctx->channels == 2)
        x = 3 * x / 2;
    s->band_index = noise_bands_selector[FFMIN(6, llrint(floor(avctx->bit_rate * 3.0 / (double)x + 0.5)))];

    if ((fft_order < 7) || (fft_order > 9)) {
        avpriv_request_sample(avctx, "Unknown FFT order %d", fft_order);
        return AVERROR_PATCHWELCOME;
    }

    if (fft_size != (1 << (fft_order - 1))) {
        av_log(avctx, AV_LOG_ERROR, "FFT size %d not power of 2.\n", fft_size);
        return AVERROR_INVALIDDATA;
    }

    ret = ff_fft_init(&s->fft_ctx, fft_order, 1);
    if (ret < 0)
        return ret;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    for (g = 5; g > 0; g--) {
        for (j = 0; j < (1 << g) - 1; j++)
            s->alt_sin[5-g][j] = sin_table[(((j+1) << (8 - g)) & 0x1FF)];
    }

    make_noises(s);

    return 0;
}

static av_cold int qdmc_decode_close(AVCodecContext *avctx)
{
    QDMCContext *s = avctx->priv_data;

    ff_fft_end(&s->fft_ctx);

    return 0;
}

static int qdmc_get_vlc(GetBitContext *gb, VLC *table, int flag)
{
    int v;

    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
    v = get_vlc2(gb, table->table, table->bits, 2);
    if (v < 0)
        v = get_bits(gb, get_bits(gb, 3) + 1);

    if (flag) {
        if (v >= FF_ARRAY_ELEMS(code_prefix))
            return AVERROR_INVALIDDATA;

        v = code_prefix[v] + get_bitsz(gb, v >> 2);
    }

    return v;
}

static int skip_label(QDMCContext *s, GetBitContext *gb)
{
    uint32_t label = get_bits_long(gb, 32);
    uint16_t sum = 226, checksum = get_bits(gb, 16);
    const uint8_t *ptr = gb->buffer + 6;
    int i;

    if (label != MKTAG('Q', 'M', 'C', 1))
        return AVERROR_INVALIDDATA;

    for (i = 0; i < s->checksum_size - 6; i++)
        sum += ptr[i];

    return sum != checksum;
}

static int read_noise_data(QDMCContext *s, GetBitContext *gb)
{
    int ch, j, k, v, idx, band, lastval, newval, len;

    for (ch = 0; ch < s->nb_channels; ch++) {
        for (band = 0; band < noise_bands_size[s->band_index]; band++) {
            v = qdmc_get_vlc(gb, &vtable[0], 0);
            if (v < 0)
                return AVERROR_INVALIDDATA;

            if (v & 1)
                v = v + 1;
            else
                v = -v;

            lastval = v / 2;
            s->noise[ch][band][0] = lastval - 1;
            for (j = 0; j < 15;) {
                len = qdmc_get_vlc(gb, &vtable[1], 1);
                if (len < 0)
                    return AVERROR_INVALIDDATA;
                len += 1;

                v = qdmc_get_vlc(gb, &vtable[0], 0);
                if (v < 0)
                    return AVERROR_INVALIDDATA;

                if (v & 1)
                    newval = lastval + (v + 1) / 2;
                else
                    newval = lastval - v / 2;

                idx = j + 1;
                if (len + idx > 16)
                    return AVERROR_INVALIDDATA;

                for (k = 1; idx <= j + len; k++, idx++)
                    s->noise[ch][band][idx] = lastval + k * (newval - lastval) / len - 1;

                lastval = newval;
                j += len;
            }
        }
    }

    return 0;
}

static void add_tone(QDMCContext *s, int group, int offset, int freq, int stereo_mode, int amplitude, int phase)
{
    const int index = s->nb_tones[group];

    if (index >= FF_ARRAY_ELEMS(s->tones[group])) {
        av_log(s->avctx, AV_LOG_WARNING, "Too many tones already in buffer, ignoring tone!\n");
        return;
    }

    s->tones[group][index].offset    = offset;
    s->tones[group][index].freq      = freq;
    s->tones[group][index].mode      = stereo_mode;
    s->tones[group][index].amplitude = amplitude;
    s->tones[group][index].phase     = phase;
    s->nb_tones[group]++;
}

static int read_wave_data(QDMCContext *s, GetBitContext *gb)
{
    int amp, phase, stereo_mode = 0, i, group, freq, group_size, group_bits;
    int amp2, phase2, pos2, off;

    for (group = 0; group < 5; group++) {
        group_size = 1 << (s->frame_bits - group - 1);
        group_bits = 4 - group;
        pos2 = 0;
        off  = 0;

        for (i = 1; ; i = freq + 1) {
            int v;

            v = qdmc_get_vlc(gb, &vtable[3], 1);
            if (v < 0)
                return AVERROR_INVALIDDATA;

            freq = i + v;
            while (freq >= group_size - 1) {
                freq += 2 - group_size;
                pos2 += group_size;
                off  += 1 << group_bits;
            }

            if (pos2 >= s->frame_size)
                break;

            if (s->nb_channels > 1)
                stereo_mode = get_bits(gb, 2);

            amp   = qdmc_get_vlc(gb, &vtable[2], 0);
            if (amp < 0)
                return AVERROR_INVALIDDATA;
            phase = get_bits(gb, 3);

            if (stereo_mode > 1) {
                amp2   = qdmc_get_vlc(gb, &vtable[4], 0);
                if (amp2 < 0)
                    return AVERROR_INVALIDDATA;
                amp2   = amp - amp2;

                phase2 = qdmc_get_vlc(gb, &vtable[5], 0);
                if (phase2 < 0)
                    return AVERROR_INVALIDDATA;
                phase2 = phase - phase2;

                if (phase2 < 0)
                    phase2 += 8;
            }

            if ((freq >> group_bits) + 1 < s->subframe_size) {
                add_tone(s, group, off, freq, stereo_mode & 1, amp, phase);
                if (stereo_mode > 1)
                    add_tone(s, group, off, freq, ~stereo_mode & 1, amp2, phase2);
            }
        }
    }

    return 0;
}

static void lin_calc(QDMCContext *s, float amplitude, int node1, int node2, int index)
{
    int subframe_size, i, j, k, length;
    float scale, *noise_ptr;

    scale = 0.5 * amplitude;
    subframe_size = s->subframe_size;
    if (subframe_size >= node2)
        subframe_size = node2;
    length = (subframe_size - node1) & 0xFFFC;
    j = node1;
    noise_ptr = &s->noise_buffer[256 * index];

    for (i = 0; i < length; i += 4, j+= 4, noise_ptr += 4) {
        s->noise2_buffer[j    ] += scale * noise_ptr[0];
        s->noise2_buffer[j + 1] += scale * noise_ptr[1];
        s->noise2_buffer[j + 2] += scale * noise_ptr[2];
        s->noise2_buffer[j + 3] += scale * noise_ptr[3];
    }

    k = length + node1;
    noise_ptr = s->noise_buffer + length + (index << 8);
    for (i = length; i < subframe_size - node1; i++, k++, noise_ptr++)
        s->noise2_buffer[k] += scale * noise_ptr[0];
}

static void add_noise(QDMCContext *s, int ch, int current_subframe)
{
    int i, j, aindex;
    float amplitude;
    float *im = &s->fft_buffer[0 + ch][s->fft_offset + s->subframe_size * current_subframe];
    float *re = &s->fft_buffer[2 + ch][s->fft_offset + s->subframe_size * current_subframe];

    memset(s->noise2_buffer, 0, 4 * s->subframe_size);

    for (i = 0; i < noise_bands_size[s->band_index]; i++) {
        if (qdmc_nodes[i + 21 * s->band_index] > s->subframe_size - 1)
            break;

        aindex = s->noise[ch][i][current_subframe / 2];
        amplitude = aindex > 0 ? amplitude_tab[aindex & 0x3F] : 0.0f;

        lin_calc(s, amplitude, qdmc_nodes[21 * s->band_index + i],
                 qdmc_nodes[21 * s->band_index + i + 2], i);
    }

    for (j = 2; j < s->subframe_size - 1; j++) {
        float rnd_re, rnd_im;

        s->rndval = 214013U * s->rndval + 2531011;
        rnd_im = ((s->rndval & 0x7FFF) - 16384.0f) * 0.000030517578f * s->noise2_buffer[j];
        s->rndval = 214013U * s->rndval + 2531011;
        rnd_re = ((s->rndval & 0x7FFF) - 16384.0f) * 0.000030517578f * s->noise2_buffer[j];
        im[j  ] += rnd_im;
        re[j  ] += rnd_re;
        im[j+1] -= rnd_im;
        re[j+1] -= rnd_re;
    }
}

static void add_wave(QDMCContext *s, int offset, int freqs, int group, int stereo_mode, int amp, int phase)
{
    int j, group_bits, pos, pindex;
    float im, re, amplitude, level, *imptr, *reptr;

    if (s->nb_channels == 1)
        stereo_mode = 0;

    group_bits = 4 - group;
    pos = freqs >> (4 - group);
    amplitude = amplitude_tab[amp & 0x3F];
    imptr = &s->fft_buffer[    stereo_mode][s->fft_offset + s->subframe_size * offset + pos];
    reptr = &s->fft_buffer[2 + stereo_mode][s->fft_offset + s->subframe_size * offset + pos];
    pindex = (phase << 6) - ((2 * (freqs >> (4 - group)) + 1) << 7);
    for (j = 0; j < (1 << (group_bits + 1)) - 1; j++) {
        pindex += (2 * freqs + 1) << (7 - group_bits);
        level = amplitude * s->alt_sin[group][j];
        im = level * sin_table[ pindex        & 0x1FF];
        re = level * sin_table[(pindex + 128) & 0x1FF];
        imptr[0] += im;
        imptr[1] -= im;
        reptr[0] += re;
        reptr[1] -= re;
        imptr += s->subframe_size;
        reptr += s->subframe_size;
        if (imptr >= &s->fft_buffer[stereo_mode][2 * s->frame_size]) {
            imptr = &s->fft_buffer[0 + stereo_mode][pos];
            reptr = &s->fft_buffer[2 + stereo_mode][pos];
        }
    }
}

static void add_wave0(QDMCContext *s, int offset, int freqs, int stereo_mode, int amp, int phase)
{
    float level, im, re;
    int pos;

    if (s->nb_channels == 1)
        stereo_mode = 0;

    level = amplitude_tab[amp & 0x3F];
    im = level * sin_table[ (phase << 6)        & 0x1FF];
    re = level * sin_table[((phase << 6) + 128) & 0x1FF];
    pos = s->fft_offset + freqs + s->subframe_size * offset;
    s->fft_buffer[    stereo_mode][pos    ] += im;
    s->fft_buffer[2 + stereo_mode][pos    ] += re;
    s->fft_buffer[    stereo_mode][pos + 1] -= im;
    s->fft_buffer[2 + stereo_mode][pos + 1] -= re;
}

static void add_waves(QDMCContext *s, int current_subframe)
{
    int w, g;

    for (g = 0; g < 4; g++) {
        for (w = s->cur_tone[g]; w < s->nb_tones[g]; w++) {
            QDMCTone *t = &s->tones[g][w];

            if (current_subframe < t->offset)
                break;
            add_wave(s, t->offset, t->freq, g, t->mode, t->amplitude, t->phase);
        }
        s->cur_tone[g] = w;
    }
    for (w = s->cur_tone[4]; w < s->nb_tones[4]; w++) {
        QDMCTone *t = &s->tones[4][w];

        if (current_subframe < t->offset)
            break;
        add_wave0(s, t->offset, t->freq, t->mode, t->amplitude, t->phase);
    }
    s->cur_tone[4] = w;
}

static int decode_frame(QDMCContext *s, GetBitContext *gb, int16_t *out)
{
    int ret, ch, i, n;

    if (skip_label(s, gb))
        return AVERROR_INVALIDDATA;

    s->fft_offset = s->frame_size - s->fft_offset;
    s->buffer_ptr = &s->buffer[s->nb_channels * s->buffer_offset];

    ret = read_noise_data(s, gb);
    if (ret < 0)
        return ret;

    ret = read_wave_data(s, gb);
    if (ret < 0)
        return ret;

    for (n = 0; n < 32; n++) {
        float *r;

        for (ch = 0; ch < s->nb_channels; ch++)
            add_noise(s, ch, n);

        add_waves(s, n);

        for (ch = 0; ch < s->nb_channels; ch++) {
            for (i = 0; i < s->subframe_size; i++) {
                s->cmplx[ch][i].re = s->fft_buffer[ch + 2][s->fft_offset + n * s->subframe_size + i];
                s->cmplx[ch][i].im = s->fft_buffer[ch + 0][s->fft_offset + n * s->subframe_size + i];
                s->cmplx[ch][s->subframe_size + i].re = 0;
                s->cmplx[ch][s->subframe_size + i].im = 0;
            }
        }

        for (ch = 0; ch < s->nb_channels; ch++) {
            s->fft_ctx.fft_permute(&s->fft_ctx, s->cmplx[ch]);
            s->fft_ctx.fft_calc(&s->fft_ctx, s->cmplx[ch]);
        }

        r = &s->buffer_ptr[s->nb_channels * n * s->subframe_size];
        for (i = 0; i < 2 * s->subframe_size; i++) {
            for (ch = 0; ch < s->nb_channels; ch++) {
                *r++ += s->cmplx[ch][i].re;
            }
        }

        r = &s->buffer_ptr[n * s->subframe_size * s->nb_channels];
        for (i = 0; i < s->nb_channels * s->subframe_size; i++) {
            out[i] = av_clipf(r[i], INT16_MIN, INT16_MAX);
        }
        out += s->subframe_size * s->nb_channels;

        for (ch = 0; ch < s->nb_channels; ch++) {
            memset(s->fft_buffer[ch+0] + s->fft_offset + n * s->subframe_size, 0, 4 * s->subframe_size);
            memset(s->fft_buffer[ch+2] + s->fft_offset + n * s->subframe_size, 0, 4 * s->subframe_size);
        }
        memset(s->buffer + s->nb_channels * (n * s->subframe_size + s->frame_size + s->buffer_offset), 0, 4 * s->subframe_size * s->nb_channels);
    }

    s->buffer_offset += s->frame_size;
    if (s->buffer_offset >= 32768 - s->frame_size) {
        memcpy(s->buffer, &s->buffer[s->nb_channels * s->buffer_offset], 4 * s->frame_size * s->nb_channels);
        s->buffer_offset = 0;
    }

    return 0;
}

static av_cold void qdmc_flush(AVCodecContext *avctx)
{
    QDMCContext *s = avctx->priv_data;

    memset(s->buffer, 0, sizeof(s->buffer));
    memset(s->fft_buffer, 0, sizeof(s->fft_buffer));
    s->fft_offset = 0;
    s->buffer_offset = 0;
}

static int qdmc_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    QDMCContext *s = avctx->priv_data;
    AVFrame *frame = data;
    GetBitContext gb;
    int ret;

    if (!avpkt->data)
        return 0;
    if (avpkt->size < s->checksum_size)
        return AVERROR_INVALIDDATA;

    s->avctx = avctx;
    frame->nb_samples = s->frame_size;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if ((ret = init_get_bits8(&gb, avpkt->data, s->checksum_size)) < 0)
        return ret;

    memset(s->nb_tones, 0, sizeof(s->nb_tones));
    memset(s->cur_tone, 0, sizeof(s->cur_tone));

    ret = decode_frame(s, &gb, (int16_t *)frame->data[0]);
    if (ret >= 0) {
        *got_frame_ptr = 1;
        return s->checksum_size;
    }
    qdmc_flush(avctx);
    return ret;
}

AVCodec ff_qdmc_decoder = {
    .name             = "qdmc",
    .long_name        = NULL_IF_CONFIG_SMALL("QDesign Music Codec 1"),
    .type             = AVMEDIA_TYPE_AUDIO,
    .id               = AV_CODEC_ID_QDMC,
    .priv_data_size   = sizeof(QDMCContext),
    .init             = qdmc_decode_init,
    .close            = qdmc_decode_close,
    .decode           = qdmc_decode_frame,
    .flush            = qdmc_flush,
    .capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE,
};
