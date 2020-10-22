/*
 * WMA compatible decoder
 * Copyright (c) 2002 The FFmpeg Project
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
 * WMA compatible decoder.
 * This decoder handles Microsoft Windows Media Audio data, versions 1 & 2.
 * WMA v1 is identified by audio format 0x160 in Microsoft media files
 * (ASF/AVI/WAV). WMA v2 is identified by audio format 0x161.
 *
 * To use this decoder, a calling application must supply the extra data
 * bytes provided with the WMA data. These are the extra, codec-specific
 * bytes at the end of a WAVEFORMATEX data structure. Transmit these bytes
 * to the decoder using the extradata[_size] fields in AVCodecContext. There
 * should be 4 extra bytes for v1 data and 6 extra bytes for v2 data.
 */

#include "libavutil/attributes.h"
#include "libavutil/ffmath.h"

#include "avcodec.h"
#include "internal.h"
#include "wma.h"

#define EXPVLCBITS 8
#define EXPMAX     ((19 + EXPVLCBITS - 1) / EXPVLCBITS)

#define HGAINVLCBITS 9
#define HGAINMAX     ((13 + HGAINVLCBITS - 1) / HGAINVLCBITS)

static void wma_lsp_to_curve_init(WMACodecContext *s, int frame_len);

#ifdef TRACE
static void dump_floats(WMACodecContext *s, const char *name,
                        int prec, const float *tab, int n)
{
    int i;

    ff_tlog(s->avctx, "%s[%d]:\n", name, n);
    for (i = 0; i < n; i++) {
        if ((i & 7) == 0)
            ff_tlog(s->avctx, "%4d: ", i);
        ff_tlog(s->avctx, " %8.*f", prec, tab[i]);
        if ((i & 7) == 7)
            ff_tlog(s->avctx, "\n");
    }
    if ((i & 7) != 0)
        ff_tlog(s->avctx, "\n");
}
#endif /* TRACE */

static av_cold int wma_decode_init(AVCodecContext *avctx)
{
    WMACodecContext *s = avctx->priv_data;
    int i, flags2;
    uint8_t *extradata;

    if (!avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR, "block_align is not set\n");
        return AVERROR(EINVAL);
    }

    s->avctx = avctx;

    /* extract flag info */
    flags2    = 0;
    extradata = avctx->extradata;
    if (avctx->codec->id == AV_CODEC_ID_WMAV1 && avctx->extradata_size >= 4)
        flags2 = AV_RL16(extradata + 2);
    else if (avctx->codec->id == AV_CODEC_ID_WMAV2 && avctx->extradata_size >= 6)
        flags2 = AV_RL16(extradata + 4);

    s->use_exp_vlc            = flags2 & 0x0001;
    s->use_bit_reservoir      = flags2 & 0x0002;
    s->use_variable_block_len = flags2 & 0x0004;

    if (avctx->codec->id == AV_CODEC_ID_WMAV2 && avctx->extradata_size >= 8){
        if (AV_RL16(extradata+4)==0xd && s->use_variable_block_len){
            av_log(avctx, AV_LOG_WARNING, "Disabling use_variable_block_len, if this fails contact the ffmpeg developers and send us the file\n");
            s->use_variable_block_len= 0; // this fixes issue1503
        }
    }

    for (i=0; i<MAX_CHANNELS; i++)
        s->max_exponent[i] = 1.0;

    if (ff_wma_init(avctx, flags2) < 0)
        return -1;

    /* init MDCT */
    for (i = 0; i < s->nb_block_sizes; i++)
        ff_mdct_init(&s->mdct_ctx[i], s->frame_len_bits - i + 1, 1, 1.0 / 32768.0);

    if (s->use_noise_coding) {
        init_vlc(&s->hgain_vlc, HGAINVLCBITS, sizeof(ff_wma_hgain_huffbits),
                 ff_wma_hgain_huffbits, 1, 1,
                 ff_wma_hgain_huffcodes, 2, 2, 0);
    }

    if (s->use_exp_vlc)
        init_vlc(&s->exp_vlc, EXPVLCBITS, sizeof(ff_aac_scalefactor_bits), // FIXME move out of context
                 ff_aac_scalefactor_bits, 1, 1,
                 ff_aac_scalefactor_code, 4, 4, 0);
    else
        wma_lsp_to_curve_init(s, s->frame_len);

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    return 0;
}

/**
 * compute x^-0.25 with an exponent and mantissa table. We use linear
 * interpolation to reduce the mantissa table size at a small speed
 * expense (linear interpolation approximately doubles the number of
 * bits of precision).
 */
static inline float pow_m1_4(WMACodecContext *s, float x)
{
    union {
        float f;
        unsigned int v;
    } u, t;
    unsigned int e, m;
    float a, b;

    u.f = x;
    e   =  u.v >>  23;
    m   = (u.v >> (23 - LSP_POW_BITS)) & ((1 << LSP_POW_BITS) - 1);
    /* build interpolation scale: 1 <= t < 2. */
    t.v = ((u.v << LSP_POW_BITS) & ((1 << 23) - 1)) | (127 << 23);
    a   = s->lsp_pow_m_table1[m];
    b   = s->lsp_pow_m_table2[m];
    return s->lsp_pow_e_table[e] * (a + b * t.f);
}

static av_cold void wma_lsp_to_curve_init(WMACodecContext *s, int frame_len)
{
    float wdel, a, b;
    int i, e, m;

    wdel = M_PI / frame_len;
    for (i = 0; i < frame_len; i++)
        s->lsp_cos_table[i] = 2.0f * cos(wdel * i);

    /* tables for x^-0.25 computation */
    for (i = 0; i < 256; i++) {
        e                     = i - 126;
        s->lsp_pow_e_table[i] = exp2f(e * -0.25);
    }

    /* NOTE: these two tables are needed to avoid two operations in
     * pow_m1_4 */
    b = 1.0;
    for (i = (1 << LSP_POW_BITS) - 1; i >= 0; i--) {
        m                      = (1 << LSP_POW_BITS) + i;
        a                      = (float) m * (0.5 / (1 << LSP_POW_BITS));
        a                      = 1/sqrt(sqrt(a));
        s->lsp_pow_m_table1[i] = 2 * a - b;
        s->lsp_pow_m_table2[i] = b - a;
        b                      = a;
    }
}

/**
 * NOTE: We use the same code as Vorbis here
 * @todo optimize it further with SSE/3Dnow
 */
static void wma_lsp_to_curve(WMACodecContext *s, float *out, float *val_max_ptr,
                             int n, float *lsp)
{
    int i, j;
    float p, q, w, v, val_max;

    val_max = 0;
    for (i = 0; i < n; i++) {
        p = 0.5f;
        q = 0.5f;
        w = s->lsp_cos_table[i];
        for (j = 1; j < NB_LSP_COEFS; j += 2) {
            q *= w - lsp[j - 1];
            p *= w - lsp[j];
        }
        p *= p * (2.0f - w);
        q *= q * (2.0f + w);
        v  = p + q;
        v  = pow_m1_4(s, v);
        if (v > val_max)
            val_max = v;
        out[i] = v;
    }
    *val_max_ptr = val_max;
}

/**
 * decode exponents coded with LSP coefficients (same idea as Vorbis)
 */
static void decode_exp_lsp(WMACodecContext *s, int ch)
{
    float lsp_coefs[NB_LSP_COEFS];
    int val, i;

    for (i = 0; i < NB_LSP_COEFS; i++) {
        if (i == 0 || i >= 8)
            val = get_bits(&s->gb, 3);
        else
            val = get_bits(&s->gb, 4);
        lsp_coefs[i] = ff_wma_lsp_codebook[i][val];
    }

    wma_lsp_to_curve(s, s->exponents[ch], &s->max_exponent[ch],
                     s->block_len, lsp_coefs);
}

/** pow(10, i / 16.0) for i in -60..95 */
static const float pow_tab[] = {
    1.7782794100389e-04, 2.0535250264571e-04,
    2.3713737056617e-04, 2.7384196342644e-04,
    3.1622776601684e-04, 3.6517412725484e-04,
    4.2169650342858e-04, 4.8696752516586e-04,
    5.6234132519035e-04, 6.4938163157621e-04,
    7.4989420933246e-04, 8.6596432336006e-04,
    1.0000000000000e-03, 1.1547819846895e-03,
    1.3335214321633e-03, 1.5399265260595e-03,
    1.7782794100389e-03, 2.0535250264571e-03,
    2.3713737056617e-03, 2.7384196342644e-03,
    3.1622776601684e-03, 3.6517412725484e-03,
    4.2169650342858e-03, 4.8696752516586e-03,
    5.6234132519035e-03, 6.4938163157621e-03,
    7.4989420933246e-03, 8.6596432336006e-03,
    1.0000000000000e-02, 1.1547819846895e-02,
    1.3335214321633e-02, 1.5399265260595e-02,
    1.7782794100389e-02, 2.0535250264571e-02,
    2.3713737056617e-02, 2.7384196342644e-02,
    3.1622776601684e-02, 3.6517412725484e-02,
    4.2169650342858e-02, 4.8696752516586e-02,
    5.6234132519035e-02, 6.4938163157621e-02,
    7.4989420933246e-02, 8.6596432336007e-02,
    1.0000000000000e-01, 1.1547819846895e-01,
    1.3335214321633e-01, 1.5399265260595e-01,
    1.7782794100389e-01, 2.0535250264571e-01,
    2.3713737056617e-01, 2.7384196342644e-01,
    3.1622776601684e-01, 3.6517412725484e-01,
    4.2169650342858e-01, 4.8696752516586e-01,
    5.6234132519035e-01, 6.4938163157621e-01,
    7.4989420933246e-01, 8.6596432336007e-01,
    1.0000000000000e+00, 1.1547819846895e+00,
    1.3335214321633e+00, 1.5399265260595e+00,
    1.7782794100389e+00, 2.0535250264571e+00,
    2.3713737056617e+00, 2.7384196342644e+00,
    3.1622776601684e+00, 3.6517412725484e+00,
    4.2169650342858e+00, 4.8696752516586e+00,
    5.6234132519035e+00, 6.4938163157621e+00,
    7.4989420933246e+00, 8.6596432336007e+00,
    1.0000000000000e+01, 1.1547819846895e+01,
    1.3335214321633e+01, 1.5399265260595e+01,
    1.7782794100389e+01, 2.0535250264571e+01,
    2.3713737056617e+01, 2.7384196342644e+01,
    3.1622776601684e+01, 3.6517412725484e+01,
    4.2169650342858e+01, 4.8696752516586e+01,
    5.6234132519035e+01, 6.4938163157621e+01,
    7.4989420933246e+01, 8.6596432336007e+01,
    1.0000000000000e+02, 1.1547819846895e+02,
    1.3335214321633e+02, 1.5399265260595e+02,
    1.7782794100389e+02, 2.0535250264571e+02,
    2.3713737056617e+02, 2.7384196342644e+02,
    3.1622776601684e+02, 3.6517412725484e+02,
    4.2169650342858e+02, 4.8696752516586e+02,
    5.6234132519035e+02, 6.4938163157621e+02,
    7.4989420933246e+02, 8.6596432336007e+02,
    1.0000000000000e+03, 1.1547819846895e+03,
    1.3335214321633e+03, 1.5399265260595e+03,
    1.7782794100389e+03, 2.0535250264571e+03,
    2.3713737056617e+03, 2.7384196342644e+03,
    3.1622776601684e+03, 3.6517412725484e+03,
    4.2169650342858e+03, 4.8696752516586e+03,
    5.6234132519035e+03, 6.4938163157621e+03,
    7.4989420933246e+03, 8.6596432336007e+03,
    1.0000000000000e+04, 1.1547819846895e+04,
    1.3335214321633e+04, 1.5399265260595e+04,
    1.7782794100389e+04, 2.0535250264571e+04,
    2.3713737056617e+04, 2.7384196342644e+04,
    3.1622776601684e+04, 3.6517412725484e+04,
    4.2169650342858e+04, 4.8696752516586e+04,
    5.6234132519035e+04, 6.4938163157621e+04,
    7.4989420933246e+04, 8.6596432336007e+04,
    1.0000000000000e+05, 1.1547819846895e+05,
    1.3335214321633e+05, 1.5399265260595e+05,
    1.7782794100389e+05, 2.0535250264571e+05,
    2.3713737056617e+05, 2.7384196342644e+05,
    3.1622776601684e+05, 3.6517412725484e+05,
    4.2169650342858e+05, 4.8696752516586e+05,
    5.6234132519035e+05, 6.4938163157621e+05,
    7.4989420933246e+05, 8.6596432336007e+05,
};

/**
 * decode exponents coded with VLC codes
 */
static int decode_exp_vlc(WMACodecContext *s, int ch)
{
    int last_exp, n, code;
    const uint16_t *ptr;
    float v, max_scale;
    uint32_t *q, *q_end, iv;
    const float *ptab = pow_tab + 60;
    const uint32_t *iptab = (const uint32_t *) ptab;

    ptr       = s->exponent_bands[s->frame_len_bits - s->block_len_bits];
    q         = (uint32_t *) s->exponents[ch];
    q_end     = q + s->block_len;
    max_scale = 0;
    if (s->version == 1) {
        last_exp  = get_bits(&s->gb, 5) + 10;
        v         = ptab[last_exp];
        iv        = iptab[last_exp];
        max_scale = v;
        n         = *ptr++;
        switch (n & 3) do {
        case 0: *q++ = iv;
        case 3: *q++ = iv;
        case 2: *q++ = iv;
        case 1: *q++ = iv;
        } while ((n -= 4) > 0);
    } else
        last_exp = 36;

    while (q < q_end) {
        code = get_vlc2(&s->gb, s->exp_vlc.table, EXPVLCBITS, EXPMAX);
        /* NOTE: this offset is the same as MPEG-4 AAC! */
        last_exp += code - 60;
        if ((unsigned) last_exp + 60 >= FF_ARRAY_ELEMS(pow_tab)) {
            av_log(s->avctx, AV_LOG_ERROR, "Exponent out of range: %d\n",
                   last_exp);
            return -1;
        }
        v  = ptab[last_exp];
        iv = iptab[last_exp];
        if (v > max_scale)
            max_scale = v;
        n = *ptr++;
        switch (n & 3) do {
        case 0: *q++ = iv;
        case 3: *q++ = iv;
        case 2: *q++ = iv;
        case 1: *q++ = iv;
        } while ((n -= 4) > 0);
    }
    s->max_exponent[ch] = max_scale;
    return 0;
}

/**
 * Apply MDCT window and add into output.
 *
 * We ensure that when the windows overlap their squared sum
 * is always 1 (MDCT reconstruction rule).
 */
static void wma_window(WMACodecContext *s, float *out)
{
    float *in = s->output;
    int block_len, bsize, n;

    /* left part */
    if (s->block_len_bits <= s->prev_block_len_bits) {
        block_len = s->block_len;
        bsize     = s->frame_len_bits - s->block_len_bits;

        s->fdsp->vector_fmul_add(out, in, s->windows[bsize],
                                out, block_len);
    } else {
        block_len = 1 << s->prev_block_len_bits;
        n         = (s->block_len - block_len) / 2;
        bsize     = s->frame_len_bits - s->prev_block_len_bits;

        s->fdsp->vector_fmul_add(out + n, in + n, s->windows[bsize],
                                out + n, block_len);

        memcpy(out + n + block_len, in + n + block_len, n * sizeof(float));
    }

    out += s->block_len;
    in  += s->block_len;

    /* right part */
    if (s->block_len_bits <= s->next_block_len_bits) {
        block_len = s->block_len;
        bsize     = s->frame_len_bits - s->block_len_bits;

        s->fdsp->vector_fmul_reverse(out, in, s->windows[bsize], block_len);
    } else {
        block_len = 1 << s->next_block_len_bits;
        n         = (s->block_len - block_len) / 2;
        bsize     = s->frame_len_bits - s->next_block_len_bits;

        memcpy(out, in, n * sizeof(float));

        s->fdsp->vector_fmul_reverse(out + n, in + n, s->windows[bsize],
                                    block_len);

        memset(out + n + block_len, 0, n * sizeof(float));
    }
}

/**
 * @return 0 if OK. 1 if last block of frame. return -1 if
 * unrecoverable error.
 */
static int wma_decode_block(WMACodecContext *s)
{
    int n, v, a, ch, bsize;
    int coef_nb_bits, total_gain;
    int nb_coefs[MAX_CHANNELS];
    float mdct_norm;
    FFTContext *mdct;

#ifdef TRACE
    ff_tlog(s->avctx, "***decode_block: %d:%d\n",
            s->frame_count - 1, s->block_num);
#endif /* TRACE */

    /* compute current block length */
    if (s->use_variable_block_len) {
        n = av_log2(s->nb_block_sizes - 1) + 1;

        if (s->reset_block_lengths) {
            s->reset_block_lengths = 0;
            v                      = get_bits(&s->gb, n);
            if (v >= s->nb_block_sizes) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "prev_block_len_bits %d out of range\n",
                       s->frame_len_bits - v);
                return -1;
            }
            s->prev_block_len_bits = s->frame_len_bits - v;
            v                      = get_bits(&s->gb, n);
            if (v >= s->nb_block_sizes) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "block_len_bits %d out of range\n",
                       s->frame_len_bits - v);
                return -1;
            }
            s->block_len_bits = s->frame_len_bits - v;
        } else {
            /* update block lengths */
            s->prev_block_len_bits = s->block_len_bits;
            s->block_len_bits      = s->next_block_len_bits;
        }
        v = get_bits(&s->gb, n);
        if (v >= s->nb_block_sizes) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "next_block_len_bits %d out of range\n",
                   s->frame_len_bits - v);
            return -1;
        }
        s->next_block_len_bits = s->frame_len_bits - v;
    } else {
        /* fixed block len */
        s->next_block_len_bits = s->frame_len_bits;
        s->prev_block_len_bits = s->frame_len_bits;
        s->block_len_bits      = s->frame_len_bits;
    }

    if (s->frame_len_bits - s->block_len_bits >= s->nb_block_sizes){
        av_log(s->avctx, AV_LOG_ERROR, "block_len_bits not initialized to a valid value\n");
        return -1;
    }

    /* now check if the block length is coherent with the frame length */
    s->block_len = 1 << s->block_len_bits;
    if ((s->block_pos + s->block_len) > s->frame_len) {
        av_log(s->avctx, AV_LOG_ERROR, "frame_len overflow\n");
        return -1;
    }

    if (s->avctx->channels == 2)
        s->ms_stereo = get_bits1(&s->gb);
    v = 0;
    for (ch = 0; ch < s->avctx->channels; ch++) {
        a                    = get_bits1(&s->gb);
        s->channel_coded[ch] = a;
        v                   |= a;
    }

    bsize = s->frame_len_bits - s->block_len_bits;

    /* if no channel coded, no need to go further */
    /* XXX: fix potential framing problems */
    if (!v)
        goto next;

    /* read total gain and extract corresponding number of bits for
     * coef escape coding */
    total_gain = 1;
    for (;;) {
        if (get_bits_left(&s->gb) < 7) {
            av_log(s->avctx, AV_LOG_ERROR, "total_gain overread\n");
            return AVERROR_INVALIDDATA;
        }
        a           = get_bits(&s->gb, 7);
        total_gain += a;
        if (a != 127)
            break;
    }

    coef_nb_bits = ff_wma_total_gain_to_bits(total_gain);

    /* compute number of coefficients */
    n = s->coefs_end[bsize] - s->coefs_start;
    for (ch = 0; ch < s->avctx->channels; ch++)
        nb_coefs[ch] = n;

    /* complex coding */
    if (s->use_noise_coding) {
        for (ch = 0; ch < s->avctx->channels; ch++) {
            if (s->channel_coded[ch]) {
                int i, n, a;
                n = s->exponent_high_sizes[bsize];
                for (i = 0; i < n; i++) {
                    a                         = get_bits1(&s->gb);
                    s->high_band_coded[ch][i] = a;
                    /* if noise coding, the coefficients are not transmitted */
                    if (a)
                        nb_coefs[ch] -= s->exponent_high_bands[bsize][i];
                }
            }
        }
        for (ch = 0; ch < s->avctx->channels; ch++) {
            if (s->channel_coded[ch]) {
                int i, n, val, code;

                n   = s->exponent_high_sizes[bsize];
                val = (int) 0x80000000;
                for (i = 0; i < n; i++) {
                    if (s->high_band_coded[ch][i]) {
                        if (val == (int) 0x80000000) {
                            val = get_bits(&s->gb, 7) - 19;
                        } else {
                            code = get_vlc2(&s->gb, s->hgain_vlc.table,
                                            HGAINVLCBITS, HGAINMAX);
                            val += code - 18;
                        }
                        s->high_band_values[ch][i] = val;
                    }
                }
            }
        }
    }

    /* exponents can be reused in short blocks. */
    if ((s->block_len_bits == s->frame_len_bits) || get_bits1(&s->gb)) {
        for (ch = 0; ch < s->avctx->channels; ch++) {
            if (s->channel_coded[ch]) {
                if (s->use_exp_vlc) {
                    if (decode_exp_vlc(s, ch) < 0)
                        return -1;
                } else {
                    decode_exp_lsp(s, ch);
                }
                s->exponents_bsize[ch] = bsize;
                s->exponents_initialized[ch] = 1;
            }
        }
    }

    for (ch = 0; ch < s->avctx->channels; ch++) {
        if (s->channel_coded[ch] && !s->exponents_initialized[ch])
            return AVERROR_INVALIDDATA;
    }

    /* parse spectral coefficients : just RLE encoding */
    for (ch = 0; ch < s->avctx->channels; ch++) {
        if (s->channel_coded[ch]) {
            int tindex;
            WMACoef *ptr = &s->coefs1[ch][0];

            /* special VLC tables are used for ms stereo because
             * there is potentially less energy there */
            tindex = (ch == 1 && s->ms_stereo);
            memset(ptr, 0, s->block_len * sizeof(WMACoef));
            ff_wma_run_level_decode(s->avctx, &s->gb, &s->coef_vlc[tindex],
                                    s->level_table[tindex], s->run_table[tindex],
                                    0, ptr, 0, nb_coefs[ch],
                                    s->block_len, s->frame_len_bits, coef_nb_bits);
        }
        if (s->version == 1 && s->avctx->channels >= 2)
            align_get_bits(&s->gb);
    }

    /* normalize */
    {
        int n4 = s->block_len / 2;
        mdct_norm = 1.0 / (float) n4;
        if (s->version == 1)
            mdct_norm *= sqrt(n4);
    }

    /* finally compute the MDCT coefficients */
    for (ch = 0; ch < s->avctx->channels; ch++) {
        if (s->channel_coded[ch]) {
            WMACoef *coefs1;
            float *coefs, *exponents, mult, mult1, noise;
            int i, j, n, n1, last_high_band, esize;
            float exp_power[HIGH_BAND_MAX_SIZE];

            coefs1    = s->coefs1[ch];
            exponents = s->exponents[ch];
            esize     = s->exponents_bsize[ch];
            mult      = ff_exp10(total_gain * 0.05) / s->max_exponent[ch];
            mult     *= mdct_norm;
            coefs     = s->coefs[ch];
            if (s->use_noise_coding) {
                mult1 = mult;
                /* very low freqs : noise */
                for (i = 0; i < s->coefs_start; i++) {
                    *coefs++ = s->noise_table[s->noise_index] *
                               exponents[i << bsize >> esize] * mult1;
                    s->noise_index = (s->noise_index + 1) &
                                     (NOISE_TAB_SIZE - 1);
                }

                n1 = s->exponent_high_sizes[bsize];

                /* compute power of high bands */
                exponents = s->exponents[ch] +
                            (s->high_band_start[bsize] << bsize >> esize);
                last_high_band = 0; /* avoid warning */
                for (j = 0; j < n1; j++) {
                    n = s->exponent_high_bands[s->frame_len_bits -
                                               s->block_len_bits][j];
                    if (s->high_band_coded[ch][j]) {
                        float e2, v;
                        e2 = 0;
                        for (i = 0; i < n; i++) {
                            v   = exponents[i << bsize >> esize];
                            e2 += v * v;
                        }
                        exp_power[j]   = e2 / n;
                        last_high_band = j;
                        ff_tlog(s->avctx, "%d: power=%f (%d)\n", j, exp_power[j], n);
                    }
                    exponents += n << bsize >> esize;
                }

                /* main freqs and high freqs */
                exponents = s->exponents[ch] + (s->coefs_start << bsize >> esize);
                for (j = -1; j < n1; j++) {
                    if (j < 0)
                        n = s->high_band_start[bsize] - s->coefs_start;
                    else
                        n = s->exponent_high_bands[s->frame_len_bits -
                                                   s->block_len_bits][j];
                    if (j >= 0 && s->high_band_coded[ch][j]) {
                        /* use noise with specified power */
                        mult1 = sqrt(exp_power[j] / exp_power[last_high_band]);
                        /* XXX: use a table */
                        mult1  = mult1 * ff_exp10(s->high_band_values[ch][j] * 0.05);
                        mult1  = mult1 / (s->max_exponent[ch] * s->noise_mult);
                        mult1 *= mdct_norm;
                        for (i = 0; i < n; i++) {
                            noise          = s->noise_table[s->noise_index];
                            s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
                            *coefs++       = noise * exponents[i << bsize >> esize] * mult1;
                        }
                        exponents += n << bsize >> esize;
                    } else {
                        /* coded values + small noise */
                        for (i = 0; i < n; i++) {
                            noise          = s->noise_table[s->noise_index];
                            s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
                            *coefs++       = ((*coefs1++) + noise) *
                                             exponents[i << bsize >> esize] * mult;
                        }
                        exponents += n << bsize >> esize;
                    }
                }

                /* very high freqs : noise */
                n     = s->block_len - s->coefs_end[bsize];
                mult1 = mult * exponents[(-(1 << bsize)) >> esize];
                for (i = 0; i < n; i++) {
                    *coefs++       = s->noise_table[s->noise_index] * mult1;
                    s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
                }
            } else {
                /* XXX: optimize more */
                for (i = 0; i < s->coefs_start; i++)
                    *coefs++ = 0.0;
                n = nb_coefs[ch];
                for (i = 0; i < n; i++)
                    *coefs++ = coefs1[i] * exponents[i << bsize >> esize] * mult;
                n = s->block_len - s->coefs_end[bsize];
                for (i = 0; i < n; i++)
                    *coefs++ = 0.0;
            }
        }
    }

#ifdef TRACE
    for (ch = 0; ch < s->avctx->channels; ch++) {
        if (s->channel_coded[ch]) {
            dump_floats(s, "exponents", 3, s->exponents[ch], s->block_len);
            dump_floats(s, "coefs", 1, s->coefs[ch], s->block_len);
        }
    }
#endif /* TRACE */

    if (s->ms_stereo && s->channel_coded[1]) {
        /* nominal case for ms stereo: we do it before mdct */
        /* no need to optimize this case because it should almost
         * never happen */
        if (!s->channel_coded[0]) {
            ff_tlog(s->avctx, "rare ms-stereo case happened\n");
            memset(s->coefs[0], 0, sizeof(float) * s->block_len);
            s->channel_coded[0] = 1;
        }

        s->fdsp->butterflies_float(s->coefs[0], s->coefs[1], s->block_len);
    }

next:
    mdct = &s->mdct_ctx[bsize];

    for (ch = 0; ch < s->avctx->channels; ch++) {
        int n4, index;

        n4 = s->block_len / 2;
        if (s->channel_coded[ch])
            mdct->imdct_calc(mdct, s->output, s->coefs[ch]);
        else if (!(s->ms_stereo && ch == 1))
            memset(s->output, 0, sizeof(s->output));

        /* multiply by the window and add in the frame */
        index = (s->frame_len / 2) + s->block_pos - n4;
        wma_window(s, &s->frame_out[ch][index]);
    }

    /* update block number */
    s->block_num++;
    s->block_pos += s->block_len;
    if (s->block_pos >= s->frame_len)
        return 1;
    else
        return 0;
}

/* decode a frame of frame_len samples */
static int wma_decode_frame(WMACodecContext *s, float **samples,
                            int samples_offset)
{
    int ret, ch;

#ifdef TRACE
    ff_tlog(s->avctx, "***decode_frame: %d size=%d\n",
            s->frame_count++, s->frame_len);
#endif /* TRACE */

    /* read each block */
    s->block_num = 0;
    s->block_pos = 0;
    for (;;) {
        ret = wma_decode_block(s);
        if (ret < 0)
            return -1;
        if (ret)
            break;
    }

    for (ch = 0; ch < s->avctx->channels; ch++) {
        /* copy current block to output */
        memcpy(samples[ch] + samples_offset, s->frame_out[ch],
               s->frame_len * sizeof(*s->frame_out[ch]));
        /* prepare for next block */
        memmove(&s->frame_out[ch][0], &s->frame_out[ch][s->frame_len],
                s->frame_len * sizeof(*s->frame_out[ch]));

#ifdef TRACE
        dump_floats(s, "samples", 6, samples[ch] + samples_offset,
                    s->frame_len);
#endif /* TRACE */
    }

    return 0;
}

static int wma_decode_superframe(AVCodecContext *avctx, void *data,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    WMACodecContext *s = avctx->priv_data;
    int nb_frames, bit_offset, i, pos, len, ret;
    uint8_t *q;
    float **samples;
    int samples_offset;

    ff_tlog(avctx, "***decode_superframe:\n");

    if (buf_size == 0) {
        s->last_superframe_len = 0;
        return 0;
    }
    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR,
               "Input packet size too small (%d < %d)\n",
               buf_size, avctx->block_align);
        return AVERROR_INVALIDDATA;
    }
    if (avctx->block_align)
        buf_size = avctx->block_align;

    init_get_bits(&s->gb, buf, buf_size * 8);

    if (s->use_bit_reservoir) {
        /* read super frame header */
        skip_bits(&s->gb, 4); /* super frame index */
        nb_frames = get_bits(&s->gb, 4) - (s->last_superframe_len <= 0);
        if (nb_frames <= 0) {
            int is_error = nb_frames < 0 || get_bits_left(&s->gb) <= 8;
            av_log(avctx, is_error ? AV_LOG_ERROR : AV_LOG_WARNING,
                   "nb_frames is %d bits left %d\n",
                   nb_frames, get_bits_left(&s->gb));
            if (is_error)
                return AVERROR_INVALIDDATA;

            if ((s->last_superframe_len + buf_size - 1) >
                MAX_CODED_SUPERFRAME_SIZE)
                goto fail;

            q   = s->last_superframe + s->last_superframe_len;
            len = buf_size - 1;
            while (len > 0) {
                *q++ = get_bits (&s->gb, 8);
                len --;
            }
            memset(q, 0, AV_INPUT_BUFFER_PADDING_SIZE);

            s->last_superframe_len += 8*buf_size - 8;
//             s->reset_block_lengths = 1; //XXX is this needed ?
            *got_frame_ptr = 0;
            return buf_size;
        }
    } else
        nb_frames = 1;

    /* get output buffer */
    frame->nb_samples = nb_frames * s->frame_len;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples        = (float **) frame->extended_data;
    samples_offset = 0;

    if (s->use_bit_reservoir) {
        bit_offset = get_bits(&s->gb, s->byte_offset_bits + 3);
        if (bit_offset > get_bits_left(&s->gb)) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid last frame bit offset %d > buf size %d (%d)\n",
                   bit_offset, get_bits_left(&s->gb), buf_size);
            goto fail;
        }

        if (s->last_superframe_len > 0) {
            /* add bit_offset bits to last frame */
            if ((s->last_superframe_len + ((bit_offset + 7) >> 3)) >
                MAX_CODED_SUPERFRAME_SIZE)
                goto fail;
            q   = s->last_superframe + s->last_superframe_len;
            len = bit_offset;
            while (len > 7) {
                *q++ = get_bits(&s->gb, 8);
                len -= 8;
            }
            if (len > 0)
                *q++ = get_bits(&s->gb, len) << (8 - len);
            memset(q, 0, AV_INPUT_BUFFER_PADDING_SIZE);

            /* XXX: bit_offset bits into last frame */
            init_get_bits(&s->gb, s->last_superframe,
                          s->last_superframe_len * 8 + bit_offset);
            /* skip unused bits */
            if (s->last_bitoffset > 0)
                skip_bits(&s->gb, s->last_bitoffset);
            /* this frame is stored in the last superframe and in the
             * current one */
            if (wma_decode_frame(s, samples, samples_offset) < 0)
                goto fail;
            samples_offset += s->frame_len;
            nb_frames--;
        }

        /* read each frame starting from bit_offset */
        pos = bit_offset + 4 + 4 + s->byte_offset_bits + 3;
        if (pos >= MAX_CODED_SUPERFRAME_SIZE * 8 || pos > buf_size * 8)
            return AVERROR_INVALIDDATA;
        init_get_bits(&s->gb, buf + (pos >> 3), (buf_size - (pos >> 3)) * 8);
        len = pos & 7;
        if (len > 0)
            skip_bits(&s->gb, len);

        s->reset_block_lengths = 1;
        for (i = 0; i < nb_frames; i++) {
            if (wma_decode_frame(s, samples, samples_offset) < 0)
                goto fail;
            samples_offset += s->frame_len;
        }

        /* we copy the end of the frame in the last frame buffer */
        pos               = get_bits_count(&s->gb) +
                            ((bit_offset + 4 + 4 + s->byte_offset_bits + 3) & ~7);
        s->last_bitoffset = pos & 7;
        pos             >>= 3;
        len               = buf_size - pos;
        if (len > MAX_CODED_SUPERFRAME_SIZE || len < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "len %d invalid\n", len);
            goto fail;
        }
        s->last_superframe_len = len;
        memcpy(s->last_superframe, buf + pos, len);
    } else {
        /* single frame decode */
        if (wma_decode_frame(s, samples, samples_offset) < 0)
            goto fail;
        samples_offset += s->frame_len;
    }

    ff_dlog(s->avctx, "%d %d %d %d outbytes:%"PTRDIFF_SPECIFIER" eaten:%d\n",
            s->frame_len_bits, s->block_len_bits, s->frame_len, s->block_len,
            (int8_t *) samples - (int8_t *) data, avctx->block_align);

    *got_frame_ptr = 1;

    return buf_size;

fail:
    /* when error, we reset the bit reservoir */
    s->last_superframe_len = 0;
    return -1;
}

static av_cold void flush(AVCodecContext *avctx)
{
    WMACodecContext *s = avctx->priv_data;

    s->last_bitoffset      =
    s->last_superframe_len = 0;
}

#if CONFIG_WMAV1_DECODER
AVCodec ff_wmav1_decoder = {
    .name           = "wmav1",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Audio 1"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_WMAV1,
    .priv_data_size = sizeof(WMACodecContext),
    .init           = wma_decode_init,
    .close          = ff_wma_end,
    .decode         = wma_decode_superframe,
    .flush          = flush,
    .capabilities   = AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_WMAV2_DECODER
AVCodec ff_wmav2_decoder = {
    .name           = "wmav2",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Audio 2"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_WMAV2,
    .priv_data_size = sizeof(WMACodecContext),
    .init           = wma_decode_init,
    .close          = ff_wma_end,
    .decode         = wma_decode_superframe,
    .flush          = flush,
    .capabilities   = AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
