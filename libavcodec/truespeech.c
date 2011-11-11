/*
 * DSP Group TrueSpeech compatible decoder
 * Copyright (c) 2005 Konstantin Shishkov
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"

#include "truespeech_data.h"
/**
 * @file
 * TrueSpeech decoder.
 */

/**
 * TrueSpeech decoder context
 */
typedef struct {
    DSPContext dsp;
    /* input data */
    uint8_t buffer[32];
    int16_t vector[8];  ///< input vector: 5/5/4/4/4/3/3/3
    int offset1[2];     ///< 8-bit value, used in one copying offset
    int offset2[4];     ///< 7-bit value, encodes offsets for copying and for two-point filter
    int pulseoff[4];    ///< 4-bit offset of pulse values block
    int pulsepos[4];    ///< 27-bit variable, encodes 7 pulse positions
    int pulseval[4];    ///< 7x2-bit pulse values
    int flag;           ///< 1-bit flag, shows how to choose filters
    /* temporary data */
    int filtbuf[146];   // some big vector used for storing filters
    int prevfilt[8];    // filter from previous frame
    int16_t tmp1[8];    // coefficients for adding to out
    int16_t tmp2[8];    // coefficients for adding to out
    int16_t tmp3[8];    // coefficients for adding to out
    int16_t cvector[8]; // correlated input vector
    int filtval;        // gain value for one function
    int16_t newvec[60]; // tmp vector
    int16_t filters[32]; // filters for every subframe
} TSContext;

static av_cold int truespeech_decode_init(AVCodecContext * avctx)
{
    TSContext *c = avctx->priv_data;

    if (avctx->channels != 1) {
        av_log_ask_for_sample(avctx, "Unsupported channel count: %d\n", avctx->channels);
        return AVERROR(EINVAL);
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    dsputil_init(&c->dsp, avctx);

    return 0;
}

static void truespeech_read_frame(TSContext *dec, const uint8_t *input)
{
    GetBitContext gb;

    dec->dsp.bswap_buf((uint32_t *)dec->buffer, (const uint32_t *)input, 8);
    init_get_bits(&gb, dec->buffer, 32 * 8);

    dec->vector[7] = ts_codebook[7][get_bits(&gb, 3)];
    dec->vector[6] = ts_codebook[6][get_bits(&gb, 3)];
    dec->vector[5] = ts_codebook[5][get_bits(&gb, 3)];
    dec->vector[4] = ts_codebook[4][get_bits(&gb, 4)];
    dec->vector[3] = ts_codebook[3][get_bits(&gb, 4)];
    dec->vector[2] = ts_codebook[2][get_bits(&gb, 4)];
    dec->vector[1] = ts_codebook[1][get_bits(&gb, 5)];
    dec->vector[0] = ts_codebook[0][get_bits(&gb, 5)];
    dec->flag      = get_bits1(&gb);

    dec->offset1[0] = get_bits(&gb, 4) << 4;
    dec->offset2[3] = get_bits(&gb, 7);
    dec->offset2[2] = get_bits(&gb, 7);
    dec->offset2[1] = get_bits(&gb, 7);
    dec->offset2[0] = get_bits(&gb, 7);

    dec->offset1[1]  = get_bits(&gb, 4);
    dec->pulseval[1] = get_bits(&gb, 14);
    dec->pulseval[0] = get_bits(&gb, 14);

    dec->offset1[1] |= get_bits(&gb, 4) << 4;
    dec->pulseval[3] = get_bits(&gb, 14);
    dec->pulseval[2] = get_bits(&gb, 14);

    dec->offset1[0] |= get_bits1(&gb);
    dec->pulsepos[0] = get_bits_long(&gb, 27);
    dec->pulseoff[0] = get_bits(&gb, 4);

    dec->offset1[0] |= get_bits1(&gb) << 1;
    dec->pulsepos[1] = get_bits_long(&gb, 27);
    dec->pulseoff[1] = get_bits(&gb, 4);

    dec->offset1[0] |= get_bits1(&gb) << 2;
    dec->pulsepos[2] = get_bits_long(&gb, 27);
    dec->pulseoff[2] = get_bits(&gb, 4);

    dec->offset1[0] |= get_bits1(&gb) << 3;
    dec->pulsepos[3] = get_bits_long(&gb, 27);
    dec->pulseoff[3] = get_bits(&gb, 4);
}

static void truespeech_correlate_filter(TSContext *dec)
{
    int16_t tmp[8];
    int i, j;

    for(i = 0; i < 8; i++){
        if(i > 0){
            memcpy(tmp, dec->cvector, i * sizeof(*tmp));
            for(j = 0; j < i; j++)
                dec->cvector[j] = ((tmp[i - j - 1] * dec->vector[i]) +
                                   (dec->cvector[j] << 15) + 0x4000) >> 15;
        }
        dec->cvector[i] = (8 - dec->vector[i]) >> 3;
    }
    for(i = 0; i < 8; i++)
        dec->cvector[i] = (dec->cvector[i] * ts_decay_994_1000[i]) >> 15;

    dec->filtval = dec->vector[0];
}

static void truespeech_filters_merge(TSContext *dec)
{
    int i;

    if(!dec->flag){
        for(i = 0; i < 8; i++){
            dec->filters[i + 0] = dec->prevfilt[i];
            dec->filters[i + 8] = dec->prevfilt[i];
        }
    }else{
        for(i = 0; i < 8; i++){
            dec->filters[i + 0]=(dec->cvector[i] * 21846 + dec->prevfilt[i] * 10923 + 16384) >> 15;
            dec->filters[i + 8]=(dec->cvector[i] * 10923 + dec->prevfilt[i] * 21846 + 16384) >> 15;
        }
    }
    for(i = 0; i < 8; i++){
        dec->filters[i + 16] = dec->cvector[i];
        dec->filters[i + 24] = dec->cvector[i];
    }
}

static void truespeech_apply_twopoint_filter(TSContext *dec, int quart)
{
    int16_t tmp[146 + 60], *ptr0, *ptr1;
    const int16_t *filter;
    int i, t, off;

    t = dec->offset2[quart];
    if(t == 127){
        memset(dec->newvec, 0, 60 * sizeof(*dec->newvec));
        return;
    }
    for(i = 0; i < 146; i++)
        tmp[i] = dec->filtbuf[i];
    off = (t / 25) + dec->offset1[quart >> 1] + 18;
    ptr0 = tmp + 145 - off;
    ptr1 = tmp + 146;
    filter = (const int16_t*)ts_order2_coeffs + (t % 25) * 2;
    for(i = 0; i < 60; i++){
        t = (ptr0[0] * filter[0] + ptr0[1] * filter[1] + 0x2000) >> 14;
        ptr0++;
        dec->newvec[i] = t;
        ptr1[i] = t;
    }
}

static void truespeech_place_pulses(TSContext *dec, int16_t *out, int quart)
{
    int16_t tmp[7];
    int i, j, t;
    const int16_t *ptr1;
    int16_t *ptr2;
    int coef;

    memset(out, 0, 60 * sizeof(*out));
    for(i = 0; i < 7; i++) {
        t = dec->pulseval[quart] & 3;
        dec->pulseval[quart] >>= 2;
        tmp[6 - i] = ts_pulse_scales[dec->pulseoff[quart] * 4 + t];
    }

    coef = dec->pulsepos[quart] >> 15;
    ptr1 = (const int16_t*)ts_pulse_values + 30;
    ptr2 = tmp;
    for(i = 0, j = 3; (i < 30) && (j > 0); i++){
        t = *ptr1++;
        if(coef >= t)
            coef -= t;
        else{
            out[i] = *ptr2++;
            ptr1 += 30;
            j--;
        }
    }
    coef = dec->pulsepos[quart] & 0x7FFF;
    ptr1 = (const int16_t*)ts_pulse_values;
    for(i = 30, j = 4; (i < 60) && (j > 0); i++){
        t = *ptr1++;
        if(coef >= t)
            coef -= t;
        else{
            out[i] = *ptr2++;
            ptr1 += 30;
            j--;
        }
    }

}

static void truespeech_update_filters(TSContext *dec, int16_t *out, int quart)
{
    int i;

    memmove(dec->filtbuf, &dec->filtbuf[60], 86 * sizeof(*dec->filtbuf));
    for(i = 0; i < 60; i++){
        dec->filtbuf[i + 86] = out[i] + dec->newvec[i] - (dec->newvec[i] >> 3);
        out[i] += dec->newvec[i];
    }
}

static void truespeech_synth(TSContext *dec, int16_t *out, int quart)
{
    int i,k;
    int t[8];
    int16_t *ptr0, *ptr1;

    ptr0 = dec->tmp1;
    ptr1 = dec->filters + quart * 8;
    for(i = 0; i < 60; i++){
        int sum = 0;
        for(k = 0; k < 8; k++)
            sum += ptr0[k] * ptr1[k];
        sum = (sum + (out[i] << 12) + 0x800) >> 12;
        out[i] = av_clip(sum, -0x7FFE, 0x7FFE);
        for(k = 7; k > 0; k--)
            ptr0[k] = ptr0[k - 1];
        ptr0[0] = out[i];
    }

    for(i = 0; i < 8; i++)
        t[i] = (ts_decay_35_64[i] * ptr1[i]) >> 15;

    ptr0 = dec->tmp2;
    for(i = 0; i < 60; i++){
        int sum = 0;
        for(k = 0; k < 8; k++)
            sum += ptr0[k] * t[k];
        for(k = 7; k > 0; k--)
            ptr0[k] = ptr0[k - 1];
        ptr0[0] = out[i];
        out[i] = ((out[i] << 12) - sum) >> 12;
    }

    for(i = 0; i < 8; i++)
        t[i] = (ts_decay_3_4[i] * ptr1[i]) >> 15;

    ptr0 = dec->tmp3;
    for(i = 0; i < 60; i++){
        int sum = out[i] << 12;
        for(k = 0; k < 8; k++)
            sum += ptr0[k] * t[k];
        for(k = 7; k > 0; k--)
            ptr0[k] = ptr0[k - 1];
        ptr0[0] = av_clip((sum + 0x800) >> 12, -0x7FFE, 0x7FFE);

        sum = ((ptr0[1] * (dec->filtval - (dec->filtval >> 2))) >> 4) + sum;
        sum = sum - (sum >> 3);
        out[i] = av_clip((sum + 0x800) >> 12, -0x7FFE, 0x7FFE);
    }
}

static void truespeech_save_prevvec(TSContext *c)
{
    int i;

    for(i = 0; i < 8; i++)
        c->prevfilt[i] = c->cvector[i];
}

static int truespeech_decode_frame(AVCodecContext *avctx,
                void *data, int *data_size,
                AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TSContext *c = avctx->priv_data;

    int i, j;
    short *samples = data;
    int iterations, out_size;

    iterations = buf_size / 32;

    if (!iterations) {
        av_log(avctx, AV_LOG_ERROR,
               "Too small input buffer (%d bytes), need at least 32 bytes\n", buf_size);
        return -1;
    }

    out_size = iterations * 240 * av_get_bytes_per_sample(avctx->sample_fmt);
    if (*data_size < out_size) {
        av_log(avctx, AV_LOG_ERROR, "Output buffer is too small\n");
        return AVERROR(EINVAL);
    }

    memset(samples, 0, out_size);

    for(j = 0; j < iterations; j++) {
        truespeech_read_frame(c, buf);
        buf += 32;

        truespeech_correlate_filter(c);
        truespeech_filters_merge(c);

        for(i = 0; i < 4; i++) {
            truespeech_apply_twopoint_filter(c, i);
            truespeech_place_pulses  (c, samples, i);
            truespeech_update_filters(c, samples, i);
            truespeech_synth         (c, samples, i);
            samples += 60;
        }

        truespeech_save_prevvec(c);
    }

    *data_size = out_size;

    return buf_size;
}

AVCodec ff_truespeech_decoder = {
    .name           = "truespeech",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_TRUESPEECH,
    .priv_data_size = sizeof(TSContext),
    .init           = truespeech_decode_init,
    .decode         = truespeech_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("DSP Group TrueSpeech"),
};
