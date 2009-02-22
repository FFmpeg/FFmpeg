/*
 * The simplest AC-3 encoder
 * Copyright (c) 2000 Fabrice Bellard
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
 * @file libavcodec/ac3enc.c
 * The simplest AC-3 encoder.
 */
//#define DEBUG
//#define DEBUG_BITALLOC
#include "libavutil/crc.h"
#include "avcodec.h"
#include "bitstream.h"
#include "ac3.h"

typedef struct AC3EncodeContext {
    PutBitContext pb;
    int nb_channels;
    int nb_all_channels;
    int lfe_channel;
    int bit_rate;
    unsigned int sample_rate;
    unsigned int bitstream_id;
    unsigned int frame_size_min; /* minimum frame size in case rounding is necessary */
    unsigned int frame_size; /* current frame size in words */
    unsigned int bits_written;
    unsigned int samples_written;
    int sr_shift;
    unsigned int frame_size_code;
    unsigned int sr_code; /* frequency */
    unsigned int channel_mode;
    int lfe;
    unsigned int bitstream_mode;
    short last_samples[AC3_MAX_CHANNELS][256];
    unsigned int chbwcod[AC3_MAX_CHANNELS];
    int nb_coefs[AC3_MAX_CHANNELS];

    /* bitrate allocation control */
    int slow_gain_code, slow_decay_code, fast_decay_code, db_per_bit_code, floor_code;
    AC3BitAllocParameters bit_alloc;
    int coarse_snr_offset;
    int fast_gain_code[AC3_MAX_CHANNELS];
    int fine_snr_offset[AC3_MAX_CHANNELS];
    /* mantissa encoding */
    int mant1_cnt, mant2_cnt, mant4_cnt;
} AC3EncodeContext;

static int16_t costab[64];
static int16_t sintab[64];
static int16_t xcos1[128];
static int16_t xsin1[128];

#define MDCT_NBITS 9
#define N         (1 << MDCT_NBITS)

/* new exponents are sent if their Norm 1 exceed this number */
#define EXP_DIFF_THRESHOLD 1000

static inline int16_t fix15(float a)
{
    int v;
    v = (int)(a * (float)(1 << 15));
    if (v < -32767)
        v = -32767;
    else if (v > 32767)
        v = 32767;
    return v;
}

typedef struct IComplex {
    short re,im;
} IComplex;

static av_cold void fft_init(int ln)
{
    int i, n;
    float alpha;

    n = 1 << ln;

    for(i=0;i<(n/2);i++) {
        alpha = 2 * M_PI * (float)i / (float)n;
        costab[i] = fix15(cos(alpha));
        sintab[i] = fix15(sin(alpha));
    }
}

/* butter fly op */
#define BF(pre, pim, qre, qim, pre1, pim1, qre1, qim1) \
{\
  int ax, ay, bx, by;\
  bx=pre1;\
  by=pim1;\
  ax=qre1;\
  ay=qim1;\
  pre = (bx + ax) >> 1;\
  pim = (by + ay) >> 1;\
  qre = (bx - ax) >> 1;\
  qim = (by - ay) >> 1;\
}

#define MUL16(a,b) ((a) * (b))

#define CMUL(pre, pim, are, aim, bre, bim) \
{\
   pre = (MUL16(are, bre) - MUL16(aim, bim)) >> 15;\
   pim = (MUL16(are, bim) + MUL16(bre, aim)) >> 15;\
}


/* do a 2^n point complex fft on 2^ln points. */
static void fft(IComplex *z, int ln)
{
    int        j, l, np, np2;
    int        nblocks, nloops;
    register IComplex *p,*q;
    int tmp_re, tmp_im;

    np = 1 << ln;

    /* reverse */
    for(j=0;j<np;j++) {
        int k = ff_reverse[j] >> (8 - ln);
        if (k < j)
            FFSWAP(IComplex, z[k], z[j]);
    }

    /* pass 0 */

    p=&z[0];
    j=(np >> 1);
    do {
        BF(p[0].re, p[0].im, p[1].re, p[1].im,
           p[0].re, p[0].im, p[1].re, p[1].im);
        p+=2;
    } while (--j != 0);

    /* pass 1 */

    p=&z[0];
    j=np >> 2;
    do {
        BF(p[0].re, p[0].im, p[2].re, p[2].im,
           p[0].re, p[0].im, p[2].re, p[2].im);
        BF(p[1].re, p[1].im, p[3].re, p[3].im,
           p[1].re, p[1].im, p[3].im, -p[3].re);
        p+=4;
    } while (--j != 0);

    /* pass 2 .. ln-1 */

    nblocks = np >> 3;
    nloops = 1 << 2;
    np2 = np >> 1;
    do {
        p = z;
        q = z + nloops;
        for (j = 0; j < nblocks; ++j) {

            BF(p->re, p->im, q->re, q->im,
               p->re, p->im, q->re, q->im);

            p++;
            q++;
            for(l = nblocks; l < np2; l += nblocks) {
                CMUL(tmp_re, tmp_im, costab[l], -sintab[l], q->re, q->im);
                BF(p->re, p->im, q->re, q->im,
                   p->re, p->im, tmp_re, tmp_im);
                p++;
                q++;
            }
            p += nloops;
            q += nloops;
        }
        nblocks = nblocks >> 1;
        nloops = nloops << 1;
    } while (nblocks != 0);
}

/* do a 512 point mdct */
static void mdct512(int32_t *out, int16_t *in)
{
    int i, re, im, re1, im1;
    int16_t rot[N];
    IComplex x[N/4];

    /* shift to simplify computations */
    for(i=0;i<N/4;i++)
        rot[i] = -in[i + 3*N/4];
    for(i=N/4;i<N;i++)
        rot[i] = in[i - N/4];

    /* pre rotation */
    for(i=0;i<N/4;i++) {
        re = ((int)rot[2*i] - (int)rot[N-1-2*i]) >> 1;
        im = -((int)rot[N/2+2*i] - (int)rot[N/2-1-2*i]) >> 1;
        CMUL(x[i].re, x[i].im, re, im, -xcos1[i], xsin1[i]);
    }

    fft(x, MDCT_NBITS - 2);

    /* post rotation */
    for(i=0;i<N/4;i++) {
        re = x[i].re;
        im = x[i].im;
        CMUL(re1, im1, re, im, xsin1[i], xcos1[i]);
        out[2*i] = im1;
        out[N/2-1-2*i] = re1;
    }
}

/* XXX: use another norm ? */
static int calc_exp_diff(uint8_t *exp1, uint8_t *exp2, int n)
{
    int sum, i;
    sum = 0;
    for(i=0;i<n;i++) {
        sum += abs(exp1[i] - exp2[i]);
    }
    return sum;
}

static void compute_exp_strategy(uint8_t exp_strategy[NB_BLOCKS][AC3_MAX_CHANNELS],
                                 uint8_t exp[NB_BLOCKS][AC3_MAX_CHANNELS][N/2],
                                 int ch, int is_lfe)
{
    int i, j;
    int exp_diff;

    /* estimate if the exponent variation & decide if they should be
       reused in the next frame */
    exp_strategy[0][ch] = EXP_NEW;
    for(i=1;i<NB_BLOCKS;i++) {
        exp_diff = calc_exp_diff(exp[i][ch], exp[i-1][ch], N/2);
#ifdef DEBUG
        av_log(NULL, AV_LOG_DEBUG, "exp_diff=%d\n", exp_diff);
#endif
        if (exp_diff > EXP_DIFF_THRESHOLD)
            exp_strategy[i][ch] = EXP_NEW;
        else
            exp_strategy[i][ch] = EXP_REUSE;
    }
    if (is_lfe)
        return;

    /* now select the encoding strategy type : if exponents are often
       recoded, we use a coarse encoding */
    i = 0;
    while (i < NB_BLOCKS) {
        j = i + 1;
        while (j < NB_BLOCKS && exp_strategy[j][ch] == EXP_REUSE)
            j++;
        switch(j - i) {
        case 1:
            exp_strategy[i][ch] = EXP_D45;
            break;
        case 2:
        case 3:
            exp_strategy[i][ch] = EXP_D25;
            break;
        default:
            exp_strategy[i][ch] = EXP_D15;
            break;
        }
        i = j;
    }
}

/* set exp[i] to min(exp[i], exp1[i]) */
static void exponent_min(uint8_t exp[N/2], uint8_t exp1[N/2], int n)
{
    int i;

    for(i=0;i<n;i++) {
        if (exp1[i] < exp[i])
            exp[i] = exp1[i];
    }
}

/* update the exponents so that they are the ones the decoder will
   decode. Return the number of bits used to code the exponents */
static int encode_exp(uint8_t encoded_exp[N/2],
                      uint8_t exp[N/2],
                      int nb_exps,
                      int exp_strategy)
{
    int group_size, nb_groups, i, j, k, exp_min;
    uint8_t exp1[N/2];

    switch(exp_strategy) {
    case EXP_D15:
        group_size = 1;
        break;
    case EXP_D25:
        group_size = 2;
        break;
    default:
    case EXP_D45:
        group_size = 4;
        break;
    }
    nb_groups = ((nb_exps + (group_size * 3) - 4) / (3 * group_size)) * 3;

    /* for each group, compute the minimum exponent */
    exp1[0] = exp[0]; /* DC exponent is handled separately */
    k = 1;
    for(i=1;i<=nb_groups;i++) {
        exp_min = exp[k];
        assert(exp_min >= 0 && exp_min <= 24);
        for(j=1;j<group_size;j++) {
            if (exp[k+j] < exp_min)
                exp_min = exp[k+j];
        }
        exp1[i] = exp_min;
        k += group_size;
    }

    /* constraint for DC exponent */
    if (exp1[0] > 15)
        exp1[0] = 15;

    /* Decrease the delta between each groups to within 2
     * so that they can be differentially encoded */
    for (i=1;i<=nb_groups;i++)
        exp1[i] = FFMIN(exp1[i], exp1[i-1] + 2);
    for (i=nb_groups-1;i>=0;i--)
        exp1[i] = FFMIN(exp1[i], exp1[i+1] + 2);

    /* now we have the exponent values the decoder will see */
    encoded_exp[0] = exp1[0];
    k = 1;
    for(i=1;i<=nb_groups;i++) {
        for(j=0;j<group_size;j++) {
            encoded_exp[k+j] = exp1[i];
        }
        k += group_size;
    }

#if defined(DEBUG)
    av_log(NULL, AV_LOG_DEBUG, "exponents: strategy=%d\n", exp_strategy);
    for(i=0;i<=nb_groups * group_size;i++) {
        av_log(NULL, AV_LOG_DEBUG, "%d ", encoded_exp[i]);
    }
    av_log(NULL, AV_LOG_DEBUG, "\n");
#endif

    return 4 + (nb_groups / 3) * 7;
}

/* return the size in bits taken by the mantissa */
static int compute_mantissa_size(AC3EncodeContext *s, uint8_t *m, int nb_coefs)
{
    int bits, mant, i;

    bits = 0;
    for(i=0;i<nb_coefs;i++) {
        mant = m[i];
        switch(mant) {
        case 0:
            /* nothing */
            break;
        case 1:
            /* 3 mantissa in 5 bits */
            if (s->mant1_cnt == 0)
                bits += 5;
            if (++s->mant1_cnt == 3)
                s->mant1_cnt = 0;
            break;
        case 2:
            /* 3 mantissa in 7 bits */
            if (s->mant2_cnt == 0)
                bits += 7;
            if (++s->mant2_cnt == 3)
                s->mant2_cnt = 0;
            break;
        case 3:
            bits += 3;
            break;
        case 4:
            /* 2 mantissa in 7 bits */
            if (s->mant4_cnt == 0)
                bits += 7;
            if (++s->mant4_cnt == 2)
                s->mant4_cnt = 0;
            break;
        case 14:
            bits += 14;
            break;
        case 15:
            bits += 16;
            break;
        default:
            bits += mant - 1;
            break;
        }
    }
    return bits;
}


static void bit_alloc_masking(AC3EncodeContext *s,
                              uint8_t encoded_exp[NB_BLOCKS][AC3_MAX_CHANNELS][N/2],
                              uint8_t exp_strategy[NB_BLOCKS][AC3_MAX_CHANNELS],
                              int16_t psd[NB_BLOCKS][AC3_MAX_CHANNELS][N/2],
                              int16_t mask[NB_BLOCKS][AC3_MAX_CHANNELS][50])
{
    int blk, ch;
    int16_t band_psd[NB_BLOCKS][AC3_MAX_CHANNELS][50];

    for(blk=0; blk<NB_BLOCKS; blk++) {
        for(ch=0;ch<s->nb_all_channels;ch++) {
            if(exp_strategy[blk][ch] == EXP_REUSE) {
                memcpy(psd[blk][ch], psd[blk-1][ch], (N/2)*sizeof(int16_t));
                memcpy(mask[blk][ch], mask[blk-1][ch], 50*sizeof(int16_t));
            } else {
                ff_ac3_bit_alloc_calc_psd(encoded_exp[blk][ch], 0,
                                          s->nb_coefs[ch],
                                          psd[blk][ch], band_psd[blk][ch]);
                ff_ac3_bit_alloc_calc_mask(&s->bit_alloc, band_psd[blk][ch],
                                           0, s->nb_coefs[ch],
                                           ff_ac3_fast_gain_tab[s->fast_gain_code[ch]],
                                           ch == s->lfe_channel,
                                           DBA_NONE, 0, NULL, NULL, NULL,
                                           mask[blk][ch]);
            }
        }
    }
}

static int bit_alloc(AC3EncodeContext *s,
                     int16_t mask[NB_BLOCKS][AC3_MAX_CHANNELS][50],
                     int16_t psd[NB_BLOCKS][AC3_MAX_CHANNELS][N/2],
                     uint8_t bap[NB_BLOCKS][AC3_MAX_CHANNELS][N/2],
                     int frame_bits, int coarse_snr_offset, int fine_snr_offset)
{
    int i, ch;
    int snr_offset;

    snr_offset = (((coarse_snr_offset - 15) << 4) + fine_snr_offset) << 2;

    /* compute size */
    for(i=0;i<NB_BLOCKS;i++) {
        s->mant1_cnt = 0;
        s->mant2_cnt = 0;
        s->mant4_cnt = 0;
        for(ch=0;ch<s->nb_all_channels;ch++) {
            ff_ac3_bit_alloc_calc_bap(mask[i][ch], psd[i][ch], 0,
                                      s->nb_coefs[ch], snr_offset,
                                      s->bit_alloc.floor, ff_ac3_bap_tab,
                                      bap[i][ch]);
            frame_bits += compute_mantissa_size(s, bap[i][ch],
                                                 s->nb_coefs[ch]);
        }
    }
#if 0
    printf("csnr=%d fsnr=%d frame_bits=%d diff=%d\n",
           coarse_snr_offset, fine_snr_offset, frame_bits,
           16 * s->frame_size - ((frame_bits + 7) & ~7));
#endif
    return 16 * s->frame_size - frame_bits;
}

#define SNR_INC1 4

static int compute_bit_allocation(AC3EncodeContext *s,
                                  uint8_t bap[NB_BLOCKS][AC3_MAX_CHANNELS][N/2],
                                  uint8_t encoded_exp[NB_BLOCKS][AC3_MAX_CHANNELS][N/2],
                                  uint8_t exp_strategy[NB_BLOCKS][AC3_MAX_CHANNELS],
                                  int frame_bits)
{
    int i, ch;
    int coarse_snr_offset, fine_snr_offset;
    uint8_t bap1[NB_BLOCKS][AC3_MAX_CHANNELS][N/2];
    int16_t psd[NB_BLOCKS][AC3_MAX_CHANNELS][N/2];
    int16_t mask[NB_BLOCKS][AC3_MAX_CHANNELS][50];
    static const int frame_bits_inc[8] = { 0, 0, 2, 2, 2, 4, 2, 4 };

    /* init default parameters */
    s->slow_decay_code = 2;
    s->fast_decay_code = 1;
    s->slow_gain_code = 1;
    s->db_per_bit_code = 2;
    s->floor_code = 4;
    for(ch=0;ch<s->nb_all_channels;ch++)
        s->fast_gain_code[ch] = 4;

    /* compute real values */
    s->bit_alloc.sr_code = s->sr_code;
    s->bit_alloc.sr_shift = s->sr_shift;
    s->bit_alloc.slow_decay = ff_ac3_slow_decay_tab[s->slow_decay_code] >> s->sr_shift;
    s->bit_alloc.fast_decay = ff_ac3_fast_decay_tab[s->fast_decay_code] >> s->sr_shift;
    s->bit_alloc.slow_gain = ff_ac3_slow_gain_tab[s->slow_gain_code];
    s->bit_alloc.db_per_bit = ff_ac3_db_per_bit_tab[s->db_per_bit_code];
    s->bit_alloc.floor = ff_ac3_floor_tab[s->floor_code];

    /* header size */
    frame_bits += 65;
    // if (s->channel_mode == 2)
    //    frame_bits += 2;
    frame_bits += frame_bits_inc[s->channel_mode];

    /* audio blocks */
    for(i=0;i<NB_BLOCKS;i++) {
        frame_bits += s->nb_channels * 2 + 2; /* blksw * c, dithflag * c, dynrnge, cplstre */
        if (s->channel_mode == AC3_CHMODE_STEREO) {
            frame_bits++; /* rematstr */
            if(i==0) frame_bits += 4;
        }
        frame_bits += 2 * s->nb_channels; /* chexpstr[2] * c */
        if (s->lfe)
            frame_bits++; /* lfeexpstr */
        for(ch=0;ch<s->nb_channels;ch++) {
            if (exp_strategy[i][ch] != EXP_REUSE)
                frame_bits += 6 + 2; /* chbwcod[6], gainrng[2] */
        }
        frame_bits++; /* baie */
        frame_bits++; /* snr */
        frame_bits += 2; /* delta / skip */
    }
    frame_bits++; /* cplinu for block 0 */
    /* bit alloc info */
    /* sdcycod[2], fdcycod[2], sgaincod[2], dbpbcod[2], floorcod[3] */
    /* csnroffset[6] */
    /* (fsnoffset[4] + fgaincod[4]) * c */
    frame_bits += 2*4 + 3 + 6 + s->nb_all_channels * (4 + 3);

    /* auxdatae, crcrsv */
    frame_bits += 2;

    /* CRC */
    frame_bits += 16;

    /* calculate psd and masking curve before doing bit allocation */
    bit_alloc_masking(s, encoded_exp, exp_strategy, psd, mask);

    /* now the big work begins : do the bit allocation. Modify the snr
       offset until we can pack everything in the requested frame size */

    coarse_snr_offset = s->coarse_snr_offset;
    while (coarse_snr_offset >= 0 &&
           bit_alloc(s, mask, psd, bap, frame_bits, coarse_snr_offset, 0) < 0)
        coarse_snr_offset -= SNR_INC1;
    if (coarse_snr_offset < 0) {
        av_log(NULL, AV_LOG_ERROR, "Bit allocation failed. Try increasing the bitrate.\n");
        return -1;
    }
    while ((coarse_snr_offset + SNR_INC1) <= 63 &&
           bit_alloc(s, mask, psd, bap1, frame_bits,
                     coarse_snr_offset + SNR_INC1, 0) >= 0) {
        coarse_snr_offset += SNR_INC1;
        memcpy(bap, bap1, sizeof(bap1));
    }
    while ((coarse_snr_offset + 1) <= 63 &&
           bit_alloc(s, mask, psd, bap1, frame_bits, coarse_snr_offset + 1, 0) >= 0) {
        coarse_snr_offset++;
        memcpy(bap, bap1, sizeof(bap1));
    }

    fine_snr_offset = 0;
    while ((fine_snr_offset + SNR_INC1) <= 15 &&
           bit_alloc(s, mask, psd, bap1, frame_bits,
                     coarse_snr_offset, fine_snr_offset + SNR_INC1) >= 0) {
        fine_snr_offset += SNR_INC1;
        memcpy(bap, bap1, sizeof(bap1));
    }
    while ((fine_snr_offset + 1) <= 15 &&
           bit_alloc(s, mask, psd, bap1, frame_bits,
                     coarse_snr_offset, fine_snr_offset + 1) >= 0) {
        fine_snr_offset++;
        memcpy(bap, bap1, sizeof(bap1));
    }

    s->coarse_snr_offset = coarse_snr_offset;
    for(ch=0;ch<s->nb_all_channels;ch++)
        s->fine_snr_offset[ch] = fine_snr_offset;
#if defined(DEBUG_BITALLOC)
    {
        int j;

        for(i=0;i<6;i++) {
            for(ch=0;ch<s->nb_all_channels;ch++) {
                printf("Block #%d Ch%d:\n", i, ch);
                printf("bap=");
                for(j=0;j<s->nb_coefs[ch];j++) {
                    printf("%d ",bap[i][ch][j]);
                }
                printf("\n");
            }
        }
    }
#endif
    return 0;
}

static av_cold int AC3_encode_init(AVCodecContext *avctx)
{
    int freq = avctx->sample_rate;
    int bitrate = avctx->bit_rate;
    int channels = avctx->channels;
    AC3EncodeContext *s = avctx->priv_data;
    int i, j, ch;
    float alpha;
    int bw_code;
    static const uint8_t channel_mode_defs[6] = {
        0x01, /* C */
        0x02, /* L R */
        0x03, /* L C R */
        0x06, /* L R SL SR */
        0x07, /* L C R SL SR */
        0x07, /* L C R SL SR (+LFE) */
    };

    avctx->frame_size = AC3_FRAME_SIZE;

    ac3_common_init();

    /* number of channels */
    if (channels < 1 || channels > 6)
        return -1;
    s->channel_mode = channel_mode_defs[channels - 1];
    s->lfe = (channels == 6) ? 1 : 0;
    s->nb_all_channels = channels;
    s->nb_channels = channels > 5 ? 5 : channels;
    s->lfe_channel = s->lfe ? 5 : -1;

    /* frequency */
    for(i=0;i<3;i++) {
        for(j=0;j<3;j++)
            if ((ff_ac3_sample_rate_tab[j] >> i) == freq)
                goto found;
    }
    return -1;
 found:
    s->sample_rate = freq;
    s->sr_shift = i;
    s->sr_code = j;
    s->bitstream_id = 8 + s->sr_shift;
    s->bitstream_mode = 0; /* complete main audio service */

    /* bitrate & frame size */
    for(i=0;i<19;i++) {
        if ((ff_ac3_bitrate_tab[i] >> s->sr_shift)*1000 == bitrate)
            break;
    }
    if (i == 19)
        return -1;
    s->bit_rate = bitrate;
    s->frame_size_code = i << 1;
    s->frame_size_min = ff_ac3_frame_size_tab[s->frame_size_code][s->sr_code];
    s->bits_written = 0;
    s->samples_written = 0;
    s->frame_size = s->frame_size_min;

    /* bit allocation init */
    if(avctx->cutoff) {
        /* calculate bandwidth based on user-specified cutoff frequency */
        int cutoff = av_clip(avctx->cutoff, 1, s->sample_rate >> 1);
        int fbw_coeffs = cutoff * 512 / s->sample_rate;
        bw_code = av_clip((fbw_coeffs - 73) / 3, 0, 60);
    } else {
        /* use default bandwidth setting */
        /* XXX: should compute the bandwidth according to the frame
           size, so that we avoid annoying high frequency artifacts */
        bw_code = 50;
    }
    for(ch=0;ch<s->nb_channels;ch++) {
        /* bandwidth for each channel */
        s->chbwcod[ch] = bw_code;
        s->nb_coefs[ch] = bw_code * 3 + 73;
    }
    if (s->lfe) {
        s->nb_coefs[s->lfe_channel] = 7; /* fixed */
    }
    /* initial snr offset */
    s->coarse_snr_offset = 40;

    /* mdct init */
    fft_init(MDCT_NBITS - 2);
    for(i=0;i<N/4;i++) {
        alpha = 2 * M_PI * (i + 1.0 / 8.0) / (float)N;
        xcos1[i] = fix15(-cos(alpha));
        xsin1[i] = fix15(-sin(alpha));
    }

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

/* output the AC-3 frame header */
static void output_frame_header(AC3EncodeContext *s, unsigned char *frame)
{
    init_put_bits(&s->pb, frame, AC3_MAX_CODED_FRAME_SIZE);

    put_bits(&s->pb, 16, 0x0b77); /* frame header */
    put_bits(&s->pb, 16, 0); /* crc1: will be filled later */
    put_bits(&s->pb, 2, s->sr_code);
    put_bits(&s->pb, 6, s->frame_size_code + (s->frame_size - s->frame_size_min));
    put_bits(&s->pb, 5, s->bitstream_id);
    put_bits(&s->pb, 3, s->bitstream_mode);
    put_bits(&s->pb, 3, s->channel_mode);
    if ((s->channel_mode & 0x01) && s->channel_mode != AC3_CHMODE_MONO)
        put_bits(&s->pb, 2, 1); /* XXX -4.5 dB */
    if (s->channel_mode & 0x04)
        put_bits(&s->pb, 2, 1); /* XXX -6 dB */
    if (s->channel_mode == AC3_CHMODE_STEREO)
        put_bits(&s->pb, 2, 0); /* surround not indicated */
    put_bits(&s->pb, 1, s->lfe); /* LFE */
    put_bits(&s->pb, 5, 31); /* dialog norm: -31 db */
    put_bits(&s->pb, 1, 0); /* no compression control word */
    put_bits(&s->pb, 1, 0); /* no lang code */
    put_bits(&s->pb, 1, 0); /* no audio production info */
    put_bits(&s->pb, 1, 0); /* no copyright */
    put_bits(&s->pb, 1, 1); /* original bitstream */
    put_bits(&s->pb, 1, 0); /* no time code 1 */
    put_bits(&s->pb, 1, 0); /* no time code 2 */
    put_bits(&s->pb, 1, 0); /* no additional bit stream info */
}

/* symetric quantization on 'levels' levels */
static inline int sym_quant(int c, int e, int levels)
{
    int v;

    if (c >= 0) {
        v = (levels * (c << e)) >> 24;
        v = (v + 1) >> 1;
        v = (levels >> 1) + v;
    } else {
        v = (levels * ((-c) << e)) >> 24;
        v = (v + 1) >> 1;
        v = (levels >> 1) - v;
    }
    assert (v >= 0 && v < levels);
    return v;
}

/* asymetric quantization on 2^qbits levels */
static inline int asym_quant(int c, int e, int qbits)
{
    int lshift, m, v;

    lshift = e + qbits - 24;
    if (lshift >= 0)
        v = c << lshift;
    else
        v = c >> (-lshift);
    /* rounding */
    v = (v + 1) >> 1;
    m = (1 << (qbits-1));
    if (v >= m)
        v = m - 1;
    assert(v >= -m);
    return v & ((1 << qbits)-1);
}

/* Output one audio block. There are NB_BLOCKS audio blocks in one AC-3
   frame */
static void output_audio_block(AC3EncodeContext *s,
                               uint8_t exp_strategy[AC3_MAX_CHANNELS],
                               uint8_t encoded_exp[AC3_MAX_CHANNELS][N/2],
                               uint8_t bap[AC3_MAX_CHANNELS][N/2],
                               int32_t mdct_coefs[AC3_MAX_CHANNELS][N/2],
                               int8_t global_exp[AC3_MAX_CHANNELS],
                               int block_num)
{
    int ch, nb_groups, group_size, i, baie, rbnd;
    uint8_t *p;
    uint16_t qmant[AC3_MAX_CHANNELS][N/2];
    int exp0, exp1;
    int mant1_cnt, mant2_cnt, mant4_cnt;
    uint16_t *qmant1_ptr, *qmant2_ptr, *qmant4_ptr;
    int delta0, delta1, delta2;

    for(ch=0;ch<s->nb_channels;ch++)
        put_bits(&s->pb, 1, 0); /* 512 point MDCT */
    for(ch=0;ch<s->nb_channels;ch++)
        put_bits(&s->pb, 1, 1); /* no dither */
    put_bits(&s->pb, 1, 0); /* no dynamic range */
    if (block_num == 0) {
        /* for block 0, even if no coupling, we must say it. This is a
           waste of bit :-) */
        put_bits(&s->pb, 1, 1); /* coupling strategy present */
        put_bits(&s->pb, 1, 0); /* no coupling strategy */
    } else {
        put_bits(&s->pb, 1, 0); /* no new coupling strategy */
    }

    if (s->channel_mode == AC3_CHMODE_STEREO)
      {
        if(block_num==0)
          {
            /* first block must define rematrixing (rematstr)  */
            put_bits(&s->pb, 1, 1);

            /* dummy rematrixing rematflg(1:4)=0 */
            for (rbnd=0;rbnd<4;rbnd++)
              put_bits(&s->pb, 1, 0);
          }
        else
          {
            /* no matrixing (but should be used in the future) */
            put_bits(&s->pb, 1, 0);
          }
      }

#if defined(DEBUG)
    {
      static int count = 0;
      av_log(NULL, AV_LOG_DEBUG, "Block #%d (%d)\n", block_num, count++);
    }
#endif
    /* exponent strategy */
    for(ch=0;ch<s->nb_channels;ch++) {
        put_bits(&s->pb, 2, exp_strategy[ch]);
    }

    if (s->lfe) {
        put_bits(&s->pb, 1, exp_strategy[s->lfe_channel]);
    }

    for(ch=0;ch<s->nb_channels;ch++) {
        if (exp_strategy[ch] != EXP_REUSE)
            put_bits(&s->pb, 6, s->chbwcod[ch]);
    }

    /* exponents */
    for (ch = 0; ch < s->nb_all_channels; ch++) {
        switch(exp_strategy[ch]) {
        case EXP_REUSE:
            continue;
        case EXP_D15:
            group_size = 1;
            break;
        case EXP_D25:
            group_size = 2;
            break;
        default:
        case EXP_D45:
            group_size = 4;
            break;
        }
        nb_groups = (s->nb_coefs[ch] + (group_size * 3) - 4) / (3 * group_size);
        p = encoded_exp[ch];

        /* first exponent */
        exp1 = *p++;
        put_bits(&s->pb, 4, exp1);

        /* next ones are delta encoded */
        for(i=0;i<nb_groups;i++) {
            /* merge three delta in one code */
            exp0 = exp1;
            exp1 = p[0];
            p += group_size;
            delta0 = exp1 - exp0 + 2;

            exp0 = exp1;
            exp1 = p[0];
            p += group_size;
            delta1 = exp1 - exp0 + 2;

            exp0 = exp1;
            exp1 = p[0];
            p += group_size;
            delta2 = exp1 - exp0 + 2;

            put_bits(&s->pb, 7, ((delta0 * 5 + delta1) * 5) + delta2);
        }

        if (ch != s->lfe_channel)
            put_bits(&s->pb, 2, 0); /* no gain range info */
    }

    /* bit allocation info */
    baie = (block_num == 0);
    put_bits(&s->pb, 1, baie);
    if (baie) {
        put_bits(&s->pb, 2, s->slow_decay_code);
        put_bits(&s->pb, 2, s->fast_decay_code);
        put_bits(&s->pb, 2, s->slow_gain_code);
        put_bits(&s->pb, 2, s->db_per_bit_code);
        put_bits(&s->pb, 3, s->floor_code);
    }

    /* snr offset */
    put_bits(&s->pb, 1, baie); /* always present with bai */
    if (baie) {
        put_bits(&s->pb, 6, s->coarse_snr_offset);
        for(ch=0;ch<s->nb_all_channels;ch++) {
            put_bits(&s->pb, 4, s->fine_snr_offset[ch]);
            put_bits(&s->pb, 3, s->fast_gain_code[ch]);
        }
    }

    put_bits(&s->pb, 1, 0); /* no delta bit allocation */
    put_bits(&s->pb, 1, 0); /* no data to skip */

    /* mantissa encoding : we use two passes to handle the grouping. A
       one pass method may be faster, but it would necessitate to
       modify the output stream. */

    /* first pass: quantize */
    mant1_cnt = mant2_cnt = mant4_cnt = 0;
    qmant1_ptr = qmant2_ptr = qmant4_ptr = NULL;

    for (ch = 0; ch < s->nb_all_channels; ch++) {
        int b, c, e, v;

        for(i=0;i<s->nb_coefs[ch];i++) {
            c = mdct_coefs[ch][i];
            e = encoded_exp[ch][i] - global_exp[ch];
            b = bap[ch][i];
            switch(b) {
            case 0:
                v = 0;
                break;
            case 1:
                v = sym_quant(c, e, 3);
                switch(mant1_cnt) {
                case 0:
                    qmant1_ptr = &qmant[ch][i];
                    v = 9 * v;
                    mant1_cnt = 1;
                    break;
                case 1:
                    *qmant1_ptr += 3 * v;
                    mant1_cnt = 2;
                    v = 128;
                    break;
                default:
                    *qmant1_ptr += v;
                    mant1_cnt = 0;
                    v = 128;
                    break;
                }
                break;
            case 2:
                v = sym_quant(c, e, 5);
                switch(mant2_cnt) {
                case 0:
                    qmant2_ptr = &qmant[ch][i];
                    v = 25 * v;
                    mant2_cnt = 1;
                    break;
                case 1:
                    *qmant2_ptr += 5 * v;
                    mant2_cnt = 2;
                    v = 128;
                    break;
                default:
                    *qmant2_ptr += v;
                    mant2_cnt = 0;
                    v = 128;
                    break;
                }
                break;
            case 3:
                v = sym_quant(c, e, 7);
                break;
            case 4:
                v = sym_quant(c, e, 11);
                switch(mant4_cnt) {
                case 0:
                    qmant4_ptr = &qmant[ch][i];
                    v = 11 * v;
                    mant4_cnt = 1;
                    break;
                default:
                    *qmant4_ptr += v;
                    mant4_cnt = 0;
                    v = 128;
                    break;
                }
                break;
            case 5:
                v = sym_quant(c, e, 15);
                break;
            case 14:
                v = asym_quant(c, e, 14);
                break;
            case 15:
                v = asym_quant(c, e, 16);
                break;
            default:
                v = asym_quant(c, e, b - 1);
                break;
            }
            qmant[ch][i] = v;
        }
    }

    /* second pass : output the values */
    for (ch = 0; ch < s->nb_all_channels; ch++) {
        int b, q;

        for(i=0;i<s->nb_coefs[ch];i++) {
            q = qmant[ch][i];
            b = bap[ch][i];
            switch(b) {
            case 0:
                break;
            case 1:
                if (q != 128)
                    put_bits(&s->pb, 5, q);
                break;
            case 2:
                if (q != 128)
                    put_bits(&s->pb, 7, q);
                break;
            case 3:
                put_bits(&s->pb, 3, q);
                break;
            case 4:
                if (q != 128)
                    put_bits(&s->pb, 7, q);
                break;
            case 14:
                put_bits(&s->pb, 14, q);
                break;
            case 15:
                put_bits(&s->pb, 16, q);
                break;
            default:
                put_bits(&s->pb, b - 1, q);
                break;
            }
        }
    }
}

#define CRC16_POLY ((1 << 0) | (1 << 2) | (1 << 15) | (1 << 16))

static unsigned int mul_poly(unsigned int a, unsigned int b, unsigned int poly)
{
    unsigned int c;

    c = 0;
    while (a) {
        if (a & 1)
            c ^= b;
        a = a >> 1;
        b = b << 1;
        if (b & (1 << 16))
            b ^= poly;
    }
    return c;
}

static unsigned int pow_poly(unsigned int a, unsigned int n, unsigned int poly)
{
    unsigned int r;
    r = 1;
    while (n) {
        if (n & 1)
            r = mul_poly(r, a, poly);
        a = mul_poly(a, a, poly);
        n >>= 1;
    }
    return r;
}


/* compute log2(max(abs(tab[]))) */
static int log2_tab(int16_t *tab, int n)
{
    int i, v;

    v = 0;
    for(i=0;i<n;i++) {
        v |= abs(tab[i]);
    }
    return av_log2(v);
}

static void lshift_tab(int16_t *tab, int n, int lshift)
{
    int i;

    if (lshift > 0) {
        for(i=0;i<n;i++) {
            tab[i] <<= lshift;
        }
    } else if (lshift < 0) {
        lshift = -lshift;
        for(i=0;i<n;i++) {
            tab[i] >>= lshift;
        }
    }
}

/* fill the end of the frame and compute the two crcs */
static int output_frame_end(AC3EncodeContext *s)
{
    int frame_size, frame_size_58, n, crc1, crc2, crc_inv;
    uint8_t *frame;

    frame_size = s->frame_size; /* frame size in words */
    /* align to 8 bits */
    flush_put_bits(&s->pb);
    /* add zero bytes to reach the frame size */
    frame = s->pb.buf;
    n = 2 * s->frame_size - (pbBufPtr(&s->pb) - frame) - 2;
    assert(n >= 0);
    if(n>0)
      memset(pbBufPtr(&s->pb), 0, n);

    /* Now we must compute both crcs : this is not so easy for crc1
       because it is at the beginning of the data... */
    frame_size_58 = (frame_size >> 1) + (frame_size >> 3);
    crc1 = bswap_16(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0,
                           frame + 4, 2 * frame_size_58 - 4));
    /* XXX: could precompute crc_inv */
    crc_inv = pow_poly((CRC16_POLY >> 1), (16 * frame_size_58) - 16, CRC16_POLY);
    crc1 = mul_poly(crc_inv, crc1, CRC16_POLY);
    AV_WB16(frame+2,crc1);

    crc2 = bswap_16(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0,
                           frame + 2 * frame_size_58,
                           (frame_size - frame_size_58) * 2 - 2));
    AV_WB16(frame+2*frame_size-2,crc2);

    //    printf("n=%d frame_size=%d\n", n, frame_size);
    return frame_size * 2;
}

static int AC3_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame, int buf_size, void *data)
{
    AC3EncodeContext *s = avctx->priv_data;
    int16_t *samples = data;
    int i, j, k, v, ch;
    int16_t input_samples[N];
    int32_t mdct_coef[NB_BLOCKS][AC3_MAX_CHANNELS][N/2];
    uint8_t exp[NB_BLOCKS][AC3_MAX_CHANNELS][N/2];
    uint8_t exp_strategy[NB_BLOCKS][AC3_MAX_CHANNELS];
    uint8_t encoded_exp[NB_BLOCKS][AC3_MAX_CHANNELS][N/2];
    uint8_t bap[NB_BLOCKS][AC3_MAX_CHANNELS][N/2];
    int8_t exp_samples[NB_BLOCKS][AC3_MAX_CHANNELS];
    int frame_bits;

    frame_bits = 0;
    for(ch=0;ch<s->nb_all_channels;ch++) {
        /* fixed mdct to the six sub blocks & exponent computation */
        for(i=0;i<NB_BLOCKS;i++) {
            int16_t *sptr;
            int sinc;

            /* compute input samples */
            memcpy(input_samples, s->last_samples[ch], N/2 * sizeof(int16_t));
            sinc = s->nb_all_channels;
            sptr = samples + (sinc * (N/2) * i) + ch;
            for(j=0;j<N/2;j++) {
                v = *sptr;
                input_samples[j + N/2] = v;
                s->last_samples[ch][j] = v;
                sptr += sinc;
            }

            /* apply the MDCT window */
            for(j=0;j<N/2;j++) {
                input_samples[j] = MUL16(input_samples[j],
                                         ff_ac3_window[j]) >> 15;
                input_samples[N-j-1] = MUL16(input_samples[N-j-1],
                                             ff_ac3_window[j]) >> 15;
            }

            /* Normalize the samples to use the maximum available
               precision */
            v = 14 - log2_tab(input_samples, N);
            if (v < 0)
                v = 0;
            exp_samples[i][ch] = v - 9;
            lshift_tab(input_samples, N, v);

            /* do the MDCT */
            mdct512(mdct_coef[i][ch], input_samples);

            /* compute "exponents". We take into account the
               normalization there */
            for(j=0;j<N/2;j++) {
                int e;
                v = abs(mdct_coef[i][ch][j]);
                if (v == 0)
                    e = 24;
                else {
                    e = 23 - av_log2(v) + exp_samples[i][ch];
                    if (e >= 24) {
                        e = 24;
                        mdct_coef[i][ch][j] = 0;
                    }
                }
                exp[i][ch][j] = e;
            }
        }

        compute_exp_strategy(exp_strategy, exp, ch, ch == s->lfe_channel);

        /* compute the exponents as the decoder will see them. The
           EXP_REUSE case must be handled carefully : we select the
           min of the exponents */
        i = 0;
        while (i < NB_BLOCKS) {
            j = i + 1;
            while (j < NB_BLOCKS && exp_strategy[j][ch] == EXP_REUSE) {
                exponent_min(exp[i][ch], exp[j][ch], s->nb_coefs[ch]);
                j++;
            }
            frame_bits += encode_exp(encoded_exp[i][ch],
                                     exp[i][ch], s->nb_coefs[ch],
                                     exp_strategy[i][ch]);
            /* copy encoded exponents for reuse case */
            for(k=i+1;k<j;k++) {
                memcpy(encoded_exp[k][ch], encoded_exp[i][ch],
                       s->nb_coefs[ch] * sizeof(uint8_t));
            }
            i = j;
        }
    }

    /* adjust for fractional frame sizes */
    while(s->bits_written >= s->bit_rate && s->samples_written >= s->sample_rate) {
        s->bits_written -= s->bit_rate;
        s->samples_written -= s->sample_rate;
    }
    s->frame_size = s->frame_size_min + (s->bits_written * s->sample_rate < s->samples_written * s->bit_rate);
    s->bits_written += s->frame_size * 16;
    s->samples_written += AC3_FRAME_SIZE;

    compute_bit_allocation(s, bap, encoded_exp, exp_strategy, frame_bits);
    /* everything is known... let's output the frame */
    output_frame_header(s, frame);

    for(i=0;i<NB_BLOCKS;i++) {
        output_audio_block(s, exp_strategy[i], encoded_exp[i],
                           bap[i], mdct_coef[i], exp_samples[i], i);
    }
    return output_frame_end(s);
}

static av_cold int AC3_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);
    return 0;
}

#if 0
/*************************************************************************/
/* TEST */

#undef random
#define FN (N/4)

void fft_test(void)
{
    IComplex in[FN], in1[FN];
    int k, n, i;
    float sum_re, sum_im, a;

    /* FFT test */

    for(i=0;i<FN;i++) {
        in[i].re = random() % 65535 - 32767;
        in[i].im = random() % 65535 - 32767;
        in1[i] = in[i];
    }
    fft(in, 7);

    /* do it by hand */
    for(k=0;k<FN;k++) {
        sum_re = 0;
        sum_im = 0;
        for(n=0;n<FN;n++) {
            a = -2 * M_PI * (n * k) / FN;
            sum_re += in1[n].re * cos(a) - in1[n].im * sin(a);
            sum_im += in1[n].re * sin(a) + in1[n].im * cos(a);
        }
        printf("%3d: %6d,%6d %6.0f,%6.0f\n",
               k, in[k].re, in[k].im, sum_re / FN, sum_im / FN);
    }
}

void mdct_test(void)
{
    int16_t input[N];
    int32_t output[N/2];
    float input1[N];
    float output1[N/2];
    float s, a, err, e, emax;
    int i, k, n;

    for(i=0;i<N;i++) {
        input[i] = (random() % 65535 - 32767) * 9 / 10;
        input1[i] = input[i];
    }

    mdct512(output, input);

    /* do it by hand */
    for(k=0;k<N/2;k++) {
        s = 0;
        for(n=0;n<N;n++) {
            a = (2*M_PI*(2*n+1+N/2)*(2*k+1) / (4 * N));
            s += input1[n] * cos(a);
        }
        output1[k] = -2 * s / N;
    }

    err = 0;
    emax = 0;
    for(i=0;i<N/2;i++) {
        printf("%3d: %7d %7.0f\n", i, output[i], output1[i]);
        e = output[i] - output1[i];
        if (e > emax)
            emax = e;
        err += e * e;
    }
    printf("err2=%f emax=%f\n", err / (N/2), emax);
}

void test_ac3(void)
{
    AC3EncodeContext ctx;
    unsigned char frame[AC3_MAX_CODED_FRAME_SIZE];
    short samples[AC3_FRAME_SIZE];
    int ret, i;

    AC3_encode_init(&ctx, 44100, 64000, 1);

    fft_test();
    mdct_test();

    for(i=0;i<AC3_FRAME_SIZE;i++)
        samples[i] = (int)(sin(2*M_PI*i*1000.0/44100) * 10000);
    ret = AC3_encode_frame(&ctx, frame, samples);
    printf("ret=%d\n", ret);
}
#endif

AVCodec ac3_encoder = {
    "ac3",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof(AC3EncodeContext),
    AC3_encode_init,
    AC3_encode_frame,
    AC3_encode_close,
    NULL,
    .sample_fmts = (enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
};
