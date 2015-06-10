/*
 * The simplest mpeg audio layer 2 encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
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
 * The simplest mpeg audio layer 2 encoder.
 */

#include "libavutil/channel_layout.h"

#include "avcodec.h"
#include "internal.h"
#include "put_bits.h"

#define FRAC_BITS   15   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  14   /* fractional bits for window */

#include "mpegaudio.h"
#include "mpegaudiodsp.h"
#include "mpegaudiodata.h"
#include "mpegaudiotab.h"

/* currently, cannot change these constants (need to modify
   quantization stage) */
#define MUL(a,b) (((int64_t)(a) * (int64_t)(b)) >> FRAC_BITS)

#define SAMPLES_BUF_SIZE 4096

typedef struct MpegAudioContext {
    PutBitContext pb;
    int nb_channels;
    int lsf;           /* 1 if mpeg2 low bitrate selected */
    int bitrate_index; /* bit rate */
    int freq_index;
    int frame_size; /* frame size, in bits, without padding */
    /* padding computation */
    int frame_frac, frame_frac_incr, do_padding;
    short samples_buf[MPA_MAX_CHANNELS][SAMPLES_BUF_SIZE]; /* buffer for filter */
    int samples_offset[MPA_MAX_CHANNELS];       /* offset in samples_buf */
    int sb_samples[MPA_MAX_CHANNELS][3][12][SBLIMIT];
    unsigned char scale_factors[MPA_MAX_CHANNELS][SBLIMIT][3]; /* scale factors */
    /* code to group 3 scale factors */
    unsigned char scale_code[MPA_MAX_CHANNELS][SBLIMIT];
    int sblimit; /* number of used subbands */
    const unsigned char *alloc_table;
    int16_t filter_bank[512];
    int scale_factor_table[64];
    unsigned char scale_diff_table[128];
#if USE_FLOATS
    float scale_factor_inv_table[64];
#else
    int8_t scale_factor_shift[64];
    unsigned short scale_factor_mult[64];
#endif
    unsigned short total_quant_bits[17]; /* total number of bits per allocation group */
} MpegAudioContext;

static av_cold int MPA_encode_init(AVCodecContext *avctx)
{
    MpegAudioContext *s = avctx->priv_data;
    int freq = avctx->sample_rate;
    int bitrate = avctx->bit_rate;
    int channels = avctx->channels;
    int i, v, table;
    float a;

    if (channels <= 0 || channels > 2){
        av_log(avctx, AV_LOG_ERROR, "encoding %d channel(s) is not allowed in mp2\n", channels);
        return AVERROR(EINVAL);
    }
    bitrate = bitrate / 1000;
    s->nb_channels = channels;
    avctx->frame_size = MPA_FRAME_SIZE;
    avctx->initial_padding = 512 - 32 + 1;

    /* encoding freq */
    s->lsf = 0;
    for(i=0;i<3;i++) {
        if (avpriv_mpa_freq_tab[i] == freq)
            break;
        if ((avpriv_mpa_freq_tab[i] / 2) == freq) {
            s->lsf = 1;
            break;
        }
    }
    if (i == 3){
        av_log(avctx, AV_LOG_ERROR, "Sampling rate %d is not allowed in mp2\n", freq);
        return AVERROR(EINVAL);
    }
    s->freq_index = i;

    /* encoding bitrate & frequency */
    for(i=1;i<15;i++) {
        if (avpriv_mpa_bitrate_tab[s->lsf][1][i] == bitrate)
            break;
    }
    if (i == 15 && !avctx->bit_rate) {
        i = 14;
        bitrate = avpriv_mpa_bitrate_tab[s->lsf][1][i];
        avctx->bit_rate = bitrate * 1000;
    }
    if (i == 15){
        av_log(avctx, AV_LOG_ERROR, "bitrate %d is not allowed in mp2\n", bitrate);
        return AVERROR(EINVAL);
    }
    s->bitrate_index = i;

    /* compute total header size & pad bit */

    a = (float)(bitrate * 1000 * MPA_FRAME_SIZE) / (freq * 8.0);
    s->frame_size = ((int)a) * 8;

    /* frame fractional size to compute padding */
    s->frame_frac = 0;
    s->frame_frac_incr = (int)((a - floor(a)) * 65536.0);

    /* select the right allocation table */
    table = ff_mpa_l2_select_table(bitrate, s->nb_channels, freq, s->lsf);

    /* number of used subbands */
    s->sblimit = ff_mpa_sblimit_table[table];
    s->alloc_table = ff_mpa_alloc_tables[table];

    ff_dlog(avctx, "%d kb/s, %d Hz, frame_size=%d bits, table=%d, padincr=%x\n",
            bitrate, freq, s->frame_size, table, s->frame_frac_incr);

    for(i=0;i<s->nb_channels;i++)
        s->samples_offset[i] = 0;

    for(i=0;i<257;i++) {
        int v;
        v = ff_mpa_enwindow[i];
#if WFRAC_BITS != 16
        v = (v + (1 << (16 - WFRAC_BITS - 1))) >> (16 - WFRAC_BITS);
#endif
        s->filter_bank[i] = v;
        if ((i & 63) != 0)
            v = -v;
        if (i != 0)
            s->filter_bank[512 - i] = v;
    }

    for(i=0;i<64;i++) {
        v = (int)(exp2((3 - i) / 3.0) * (1 << 20));
        if (v <= 0)
            v = 1;
        s->scale_factor_table[i] = v;
#if USE_FLOATS
        s->scale_factor_inv_table[i] = exp2(-(3 - i) / 3.0) / (float)(1 << 20);
#else
#define P 15
        s->scale_factor_shift[i] = 21 - P - (i / 3);
        s->scale_factor_mult[i] = (1 << P) * exp2((i % 3) / 3.0);
#endif
    }
    for(i=0;i<128;i++) {
        v = i - 64;
        if (v <= -3)
            v = 0;
        else if (v < 0)
            v = 1;
        else if (v == 0)
            v = 2;
        else if (v < 3)
            v = 3;
        else
            v = 4;
        s->scale_diff_table[i] = v;
    }

    for(i=0;i<17;i++) {
        v = ff_mpa_quant_bits[i];
        if (v < 0)
            v = -v;
        else
            v = v * 3;
        s->total_quant_bits[i] = 12 * v;
    }

    return 0;
}

/* 32 point floating point IDCT without 1/sqrt(2) coef zero scaling */
static void idct32(int *out, int *tab)
{
    int i, j;
    int *t, *t1, xr;
    const int *xp = costab32;

    for(j=31;j>=3;j-=2) tab[j] += tab[j - 2];

    t = tab + 30;
    t1 = tab + 2;
    do {
        t[0] += t[-4];
        t[1] += t[1 - 4];
        t -= 4;
    } while (t != t1);

    t = tab + 28;
    t1 = tab + 4;
    do {
        t[0] += t[-8];
        t[1] += t[1-8];
        t[2] += t[2-8];
        t[3] += t[3-8];
        t -= 8;
    } while (t != t1);

    t = tab;
    t1 = tab + 32;
    do {
        t[ 3] = -t[ 3];
        t[ 6] = -t[ 6];

        t[11] = -t[11];
        t[12] = -t[12];
        t[13] = -t[13];
        t[15] = -t[15];
        t += 16;
    } while (t != t1);


    t = tab;
    t1 = tab + 8;
    do {
        int x1, x2, x3, x4;

        x3 = MUL(t[16], FIX(SQRT2*0.5));
        x4 = t[0] - x3;
        x3 = t[0] + x3;

        x2 = MUL(-(t[24] + t[8]), FIX(SQRT2*0.5));
        x1 = MUL((t[8] - x2), xp[0]);
        x2 = MUL((t[8] + x2), xp[1]);

        t[ 0] = x3 + x1;
        t[ 8] = x4 - x2;
        t[16] = x4 + x2;
        t[24] = x3 - x1;
        t++;
    } while (t != t1);

    xp += 2;
    t = tab;
    t1 = tab + 4;
    do {
        xr = MUL(t[28],xp[0]);
        t[28] = (t[0] - xr);
        t[0] = (t[0] + xr);

        xr = MUL(t[4],xp[1]);
        t[ 4] = (t[24] - xr);
        t[24] = (t[24] + xr);

        xr = MUL(t[20],xp[2]);
        t[20] = (t[8] - xr);
        t[ 8] = (t[8] + xr);

        xr = MUL(t[12],xp[3]);
        t[12] = (t[16] - xr);
        t[16] = (t[16] + xr);
        t++;
    } while (t != t1);
    xp += 4;

    for (i = 0; i < 4; i++) {
        xr = MUL(tab[30-i*4],xp[0]);
        tab[30-i*4] = (tab[i*4] - xr);
        tab[   i*4] = (tab[i*4] + xr);

        xr = MUL(tab[ 2+i*4],xp[1]);
        tab[ 2+i*4] = (tab[28-i*4] - xr);
        tab[28-i*4] = (tab[28-i*4] + xr);

        xr = MUL(tab[31-i*4],xp[0]);
        tab[31-i*4] = (tab[1+i*4] - xr);
        tab[ 1+i*4] = (tab[1+i*4] + xr);

        xr = MUL(tab[ 3+i*4],xp[1]);
        tab[ 3+i*4] = (tab[29-i*4] - xr);
        tab[29-i*4] = (tab[29-i*4] + xr);

        xp += 2;
    }

    t = tab + 30;
    t1 = tab + 1;
    do {
        xr = MUL(t1[0], *xp);
        t1[0] = (t[0] - xr);
        t[0] = (t[0] + xr);
        t -= 2;
        t1 += 2;
        xp++;
    } while (t >= tab);

    for(i=0;i<32;i++) {
        out[i] = tab[bitinv32[i]];
    }
}

#define WSHIFT (WFRAC_BITS + 15 - FRAC_BITS)

static void filter(MpegAudioContext *s, int ch, const short *samples, int incr)
{
    short *p, *q;
    int sum, offset, i, j;
    int tmp[64];
    int tmp1[32];
    int *out;

    offset = s->samples_offset[ch];
    out = &s->sb_samples[ch][0][0][0];
    for(j=0;j<36;j++) {
        /* 32 samples at once */
        for(i=0;i<32;i++) {
            s->samples_buf[ch][offset + (31 - i)] = samples[0];
            samples += incr;
        }

        /* filter */
        p = s->samples_buf[ch] + offset;
        q = s->filter_bank;
        /* maxsum = 23169 */
        for(i=0;i<64;i++) {
            sum = p[0*64] * q[0*64];
            sum += p[1*64] * q[1*64];
            sum += p[2*64] * q[2*64];
            sum += p[3*64] * q[3*64];
            sum += p[4*64] * q[4*64];
            sum += p[5*64] * q[5*64];
            sum += p[6*64] * q[6*64];
            sum += p[7*64] * q[7*64];
            tmp[i] = sum;
            p++;
            q++;
        }
        tmp1[0] = tmp[16] >> WSHIFT;
        for( i=1; i<=16; i++ ) tmp1[i] = (tmp[i+16]+tmp[16-i]) >> WSHIFT;
        for( i=17; i<=31; i++ ) tmp1[i] = (tmp[i+16]-tmp[80-i]) >> WSHIFT;

        idct32(out, tmp1);

        /* advance of 32 samples */
        offset -= 32;
        out += 32;
        /* handle the wrap around */
        if (offset < 0) {
            memmove(s->samples_buf[ch] + SAMPLES_BUF_SIZE - (512 - 32),
                    s->samples_buf[ch], (512 - 32) * 2);
            offset = SAMPLES_BUF_SIZE - 512;
        }
    }
    s->samples_offset[ch] = offset;
}

static void compute_scale_factors(MpegAudioContext *s,
                                  unsigned char scale_code[SBLIMIT],
                                  unsigned char scale_factors[SBLIMIT][3],
                                  int sb_samples[3][12][SBLIMIT],
                                  int sblimit)
{
    int *p, vmax, v, n, i, j, k, code;
    int index, d1, d2;
    unsigned char *sf = &scale_factors[0][0];

    for(j=0;j<sblimit;j++) {
        for(i=0;i<3;i++) {
            /* find the max absolute value */
            p = &sb_samples[i][0][j];
            vmax = abs(*p);
            for(k=1;k<12;k++) {
                p += SBLIMIT;
                v = abs(*p);
                if (v > vmax)
                    vmax = v;
            }
            /* compute the scale factor index using log 2 computations */
            if (vmax > 1) {
                n = av_log2(vmax);
                /* n is the position of the MSB of vmax. now
                   use at most 2 compares to find the index */
                index = (21 - n) * 3 - 3;
                if (index >= 0) {
                    while (vmax <= s->scale_factor_table[index+1])
                        index++;
                } else {
                    index = 0; /* very unlikely case of overflow */
                }
            } else {
                index = 62; /* value 63 is not allowed */
            }

            ff_dlog(NULL, "%2d:%d in=%x %x %d\n",
                    j, i, vmax, s->scale_factor_table[index], index);
            /* store the scale factor */
            av_assert2(index >=0 && index <= 63);
            sf[i] = index;
        }

        /* compute the transmission factor : look if the scale factors
           are close enough to each other */
        d1 = s->scale_diff_table[sf[0] - sf[1] + 64];
        d2 = s->scale_diff_table[sf[1] - sf[2] + 64];

        /* handle the 25 cases */
        switch(d1 * 5 + d2) {
        case 0*5+0:
        case 0*5+4:
        case 3*5+4:
        case 4*5+0:
        case 4*5+4:
            code = 0;
            break;
        case 0*5+1:
        case 0*5+2:
        case 4*5+1:
        case 4*5+2:
            code = 3;
            sf[2] = sf[1];
            break;
        case 0*5+3:
        case 4*5+3:
            code = 3;
            sf[1] = sf[2];
            break;
        case 1*5+0:
        case 1*5+4:
        case 2*5+4:
            code = 1;
            sf[1] = sf[0];
            break;
        case 1*5+1:
        case 1*5+2:
        case 2*5+0:
        case 2*5+1:
        case 2*5+2:
            code = 2;
            sf[1] = sf[2] = sf[0];
            break;
        case 2*5+3:
        case 3*5+3:
            code = 2;
            sf[0] = sf[1] = sf[2];
            break;
        case 3*5+0:
        case 3*5+1:
        case 3*5+2:
            code = 2;
            sf[0] = sf[2] = sf[1];
            break;
        case 1*5+3:
            code = 2;
            if (sf[0] > sf[2])
              sf[0] = sf[2];
            sf[1] = sf[2] = sf[0];
            break;
        default:
            av_assert2(0); //cannot happen
            code = 0;           /* kill warning */
        }

        ff_dlog(NULL, "%d: %2d %2d %2d %d %d -> %d\n", j,
                sf[0], sf[1], sf[2], d1, d2, code);
        scale_code[j] = code;
        sf += 3;
    }
}

/* The most important function : psycho acoustic module. In this
   encoder there is basically none, so this is the worst you can do,
   but also this is the simpler. */
static void psycho_acoustic_model(MpegAudioContext *s, short smr[SBLIMIT])
{
    int i;

    for(i=0;i<s->sblimit;i++) {
        smr[i] = (int)(fixed_smr[i] * 10);
    }
}


#define SB_NOTALLOCATED  0
#define SB_ALLOCATED     1
#define SB_NOMORE        2

/* Try to maximize the smr while using a number of bits inferior to
   the frame size. I tried to make the code simpler, faster and
   smaller than other encoders :-) */
static void compute_bit_allocation(MpegAudioContext *s,
                                   short smr1[MPA_MAX_CHANNELS][SBLIMIT],
                                   unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT],
                                   int *padding)
{
    int i, ch, b, max_smr, max_ch, max_sb, current_frame_size, max_frame_size;
    int incr;
    short smr[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char subband_status[MPA_MAX_CHANNELS][SBLIMIT];
    const unsigned char *alloc;

    memcpy(smr, smr1, s->nb_channels * sizeof(short) * SBLIMIT);
    memset(subband_status, SB_NOTALLOCATED, s->nb_channels * SBLIMIT);
    memset(bit_alloc, 0, s->nb_channels * SBLIMIT);

    /* compute frame size and padding */
    max_frame_size = s->frame_size;
    s->frame_frac += s->frame_frac_incr;
    if (s->frame_frac >= 65536) {
        s->frame_frac -= 65536;
        s->do_padding = 1;
        max_frame_size += 8;
    } else {
        s->do_padding = 0;
    }

    /* compute the header + bit alloc size */
    current_frame_size = 32;
    alloc = s->alloc_table;
    for(i=0;i<s->sblimit;i++) {
        incr = alloc[0];
        current_frame_size += incr * s->nb_channels;
        alloc += 1 << incr;
    }
    for(;;) {
        /* look for the subband with the largest signal to mask ratio */
        max_sb = -1;
        max_ch = -1;
        max_smr = INT_MIN;
        for(ch=0;ch<s->nb_channels;ch++) {
            for(i=0;i<s->sblimit;i++) {
                if (smr[ch][i] > max_smr && subband_status[ch][i] != SB_NOMORE) {
                    max_smr = smr[ch][i];
                    max_sb = i;
                    max_ch = ch;
                }
            }
        }
        if (max_sb < 0)
            break;
        ff_dlog(NULL, "current=%d max=%d max_sb=%d max_ch=%d alloc=%d\n",
                current_frame_size, max_frame_size, max_sb, max_ch,
                bit_alloc[max_ch][max_sb]);

        /* find alloc table entry (XXX: not optimal, should use
           pointer table) */
        alloc = s->alloc_table;
        for(i=0;i<max_sb;i++) {
            alloc += 1 << alloc[0];
        }

        if (subband_status[max_ch][max_sb] == SB_NOTALLOCATED) {
            /* nothing was coded for this band: add the necessary bits */
            incr = 2 + nb_scale_factors[s->scale_code[max_ch][max_sb]] * 6;
            incr += s->total_quant_bits[alloc[1]];
        } else {
            /* increments bit allocation */
            b = bit_alloc[max_ch][max_sb];
            incr = s->total_quant_bits[alloc[b + 1]] -
                s->total_quant_bits[alloc[b]];
        }

        if (current_frame_size + incr <= max_frame_size) {
            /* can increase size */
            b = ++bit_alloc[max_ch][max_sb];
            current_frame_size += incr;
            /* decrease smr by the resolution we added */
            smr[max_ch][max_sb] = smr1[max_ch][max_sb] - quant_snr[alloc[b]];
            /* max allocation size reached ? */
            if (b == ((1 << alloc[0]) - 1))
                subband_status[max_ch][max_sb] = SB_NOMORE;
            else
                subband_status[max_ch][max_sb] = SB_ALLOCATED;
        } else {
            /* cannot increase the size of this subband */
            subband_status[max_ch][max_sb] = SB_NOMORE;
        }
    }
    *padding = max_frame_size - current_frame_size;
    av_assert0(*padding >= 0);
}

/*
 * Output the mpeg audio layer 2 frame. Note how the code is small
 * compared to other encoders :-)
 */
static void encode_frame(MpegAudioContext *s,
                         unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT],
                         int padding)
{
    int i, j, k, l, bit_alloc_bits, b, ch;
    unsigned char *sf;
    int q[3];
    PutBitContext *p = &s->pb;

    /* header */

    put_bits(p, 12, 0xfff);
    put_bits(p, 1, 1 - s->lsf); /* 1 = mpeg1 ID, 0 = mpeg2 lsf ID */
    put_bits(p, 2, 4-2);  /* layer 2 */
    put_bits(p, 1, 1); /* no error protection */
    put_bits(p, 4, s->bitrate_index);
    put_bits(p, 2, s->freq_index);
    put_bits(p, 1, s->do_padding); /* use padding */
    put_bits(p, 1, 0);             /* private_bit */
    put_bits(p, 2, s->nb_channels == 2 ? MPA_STEREO : MPA_MONO);
    put_bits(p, 2, 0); /* mode_ext */
    put_bits(p, 1, 0); /* no copyright */
    put_bits(p, 1, 1); /* original */
    put_bits(p, 2, 0); /* no emphasis */

    /* bit allocation */
    j = 0;
    for(i=0;i<s->sblimit;i++) {
        bit_alloc_bits = s->alloc_table[j];
        for(ch=0;ch<s->nb_channels;ch++) {
            put_bits(p, bit_alloc_bits, bit_alloc[ch][i]);
        }
        j += 1 << bit_alloc_bits;
    }

    /* scale codes */
    for(i=0;i<s->sblimit;i++) {
        for(ch=0;ch<s->nb_channels;ch++) {
            if (bit_alloc[ch][i])
                put_bits(p, 2, s->scale_code[ch][i]);
        }
    }

    /* scale factors */
    for(i=0;i<s->sblimit;i++) {
        for(ch=0;ch<s->nb_channels;ch++) {
            if (bit_alloc[ch][i]) {
                sf = &s->scale_factors[ch][i][0];
                switch(s->scale_code[ch][i]) {
                case 0:
                    put_bits(p, 6, sf[0]);
                    put_bits(p, 6, sf[1]);
                    put_bits(p, 6, sf[2]);
                    break;
                case 3:
                case 1:
                    put_bits(p, 6, sf[0]);
                    put_bits(p, 6, sf[2]);
                    break;
                case 2:
                    put_bits(p, 6, sf[0]);
                    break;
                }
            }
        }
    }

    /* quantization & write sub band samples */

    for(k=0;k<3;k++) {
        for(l=0;l<12;l+=3) {
            j = 0;
            for(i=0;i<s->sblimit;i++) {
                bit_alloc_bits = s->alloc_table[j];
                for(ch=0;ch<s->nb_channels;ch++) {
                    b = bit_alloc[ch][i];
                    if (b) {
                        int qindex, steps, m, sample, bits;
                        /* we encode 3 sub band samples of the same sub band at a time */
                        qindex = s->alloc_table[j+b];
                        steps = ff_mpa_quant_steps[qindex];
                        for(m=0;m<3;m++) {
                            sample = s->sb_samples[ch][k][l + m][i];
                            /* divide by scale factor */
#if USE_FLOATS
                            {
                                float a;
                                a = (float)sample * s->scale_factor_inv_table[s->scale_factors[ch][i][k]];
                                q[m] = (int)((a + 1.0) * steps * 0.5);
                            }
#else
                            {
                                int q1, e, shift, mult;
                                e = s->scale_factors[ch][i][k];
                                shift = s->scale_factor_shift[e];
                                mult = s->scale_factor_mult[e];

                                /* normalize to P bits */
                                if (shift < 0)
                                    q1 = sample << (-shift);
                                else
                                    q1 = sample >> shift;
                                q1 = (q1 * mult) >> P;
                                q1 += 1 << P;
                                if (q1 < 0)
                                    q1 = 0;
                                q[m] = (q1 * (unsigned)steps) >> (P + 1);
                            }
#endif
                            if (q[m] >= steps)
                                q[m] = steps - 1;
                            av_assert2(q[m] >= 0 && q[m] < steps);
                        }
                        bits = ff_mpa_quant_bits[qindex];
                        if (bits < 0) {
                            /* group the 3 values to save bits */
                            put_bits(p, -bits,
                                     q[0] + steps * (q[1] + steps * q[2]));
                        } else {
                            put_bits(p, bits, q[0]);
                            put_bits(p, bits, q[1]);
                            put_bits(p, bits, q[2]);
                        }
                    }
                }
                /* next subband in alloc table */
                j += 1 << bit_alloc_bits;
            }
        }
    }

    /* padding */
    for(i=0;i<padding;i++)
        put_bits(p, 1, 0);

    /* flush */
    flush_put_bits(p);
}

static int MPA_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    MpegAudioContext *s = avctx->priv_data;
    const int16_t *samples = (const int16_t *)frame->data[0];
    short smr[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT];
    int padding, i, ret;

    for(i=0;i<s->nb_channels;i++) {
        filter(s, i, samples + i, s->nb_channels);
    }

    for(i=0;i<s->nb_channels;i++) {
        compute_scale_factors(s, s->scale_code[i], s->scale_factors[i],
                              s->sb_samples[i], s->sblimit);
    }
    for(i=0;i<s->nb_channels;i++) {
        psycho_acoustic_model(s, smr[i]);
    }
    compute_bit_allocation(s, smr, bit_alloc, &padding);

    if ((ret = ff_alloc_packet2(avctx, avpkt, MPA_MAX_CODED_FRAME_SIZE)) < 0)
        return ret;

    init_put_bits(&s->pb, avpkt->data, avpkt->size);

    encode_frame(s, bit_alloc, padding);

    if (frame->pts != AV_NOPTS_VALUE)
        avpkt->pts = frame->pts - ff_samples_to_time_base(avctx, avctx->initial_padding);

    avpkt->size = put_bits_count(&s->pb) / 8;
    *got_packet_ptr = 1;
    return 0;
}

static const AVCodecDefault mp2_defaults[] = {
    { "b", "0" },
    { NULL },
};

