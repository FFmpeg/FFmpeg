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
 * @file
 * The simplest AC-3 encoder.
 */

//#define DEBUG

#include "libavcore/audioconvert.h"
#include "libavutil/crc.h"
#include "avcodec.h"
#include "put_bits.h"
#include "ac3.h"
#include "audioconvert.h"

#define SCALE_FLOAT(a, bits) lrintf((a) * (float)(1 << (bits)))

typedef struct AC3EncodeContext {
    PutBitContext pb;                       ///< bitstream writer context

    int bitstream_id;                       ///< bitstream id                           (bsid)
    int bitstream_mode;                     ///< bitstream mode                         (bsmod)

    int bit_rate;                           ///< target bit rate, in bits-per-second
    int sample_rate;                        ///< sampling frequency, in Hz

    int frame_size_min;                     ///< minimum frame size in case rounding is necessary
    int frame_size;                         ///< current frame size in words
    int frame_size_code;                    ///< frame size code                        (frmsizecod)
    int bits_written;                       ///< bit count    (used to avg. bitrate)
    int samples_written;                    ///< sample count (used to avg. bitrate)

    int fbw_channels;                       ///< number of full-bandwidth channels      (nfchans)
    int channels;                           ///< total number of channels               (nchans)
    int lfe_on;                             ///< indicates if there is an LFE channel   (lfeon)
    int lfe_channel;                        ///< channel index of the LFE channel
    int channel_mode;                       ///< channel mode                           (acmod)
    const uint8_t *channel_map;             ///< channel map used to reorder channels

    int bandwidth_code[AC3_MAX_CHANNELS];   ///< bandwidth code (0 to 60)               (chbwcod)
    int nb_coefs[AC3_MAX_CHANNELS];

    /* bitrate allocation control */
    int slow_gain_code;                     ///< slow gain code                         (sgaincod)
    int slow_decay_code;                    ///< slow decay code                        (sdcycod)
    int fast_decay_code;                    ///< fast decay code                        (fdcycod)
    int db_per_bit_code;                    ///< dB/bit code                            (dbpbcod)
    int floor_code;                         ///< floor code                             (floorcod)
    AC3BitAllocParameters bit_alloc;        ///< bit allocation parameters
    int coarse_snr_offset;                  ///< coarse SNR offsets                     (csnroffst)
    int fast_gain_code[AC3_MAX_CHANNELS];   ///< fast gain codes (signal-to-mask ratio) (fgaincod)
    int fine_snr_offset[AC3_MAX_CHANNELS];  ///< fine SNR offsets                       (fsnroffst)

    /* mantissa encoding */
    int mant1_cnt, mant2_cnt, mant4_cnt;    ///< mantissa counts for bap=1,2,4

    int16_t last_samples[AC3_MAX_CHANNELS][AC3_BLOCK_SIZE]; ///< last 256 samples from previous frame
} AC3EncodeContext;

static int16_t costab[64];
static int16_t sintab[64];
static int16_t xcos1[128];
static int16_t xsin1[128];

#define MDCT_NBITS 9
#define MDCT_SAMPLES (1 << MDCT_NBITS)

#define FIX15(a) av_clip_int16(SCALE_FLOAT(a, 15))

typedef struct IComplex {
    int16_t re,im;
} IComplex;

static av_cold void fft_init(int ln)
{
    int i, n, n2;
    float alpha;

    n  = 1 << ln;
    n2 = n >> 1;

    for (i = 0; i < n2; i++) {
        alpha     = 2.0 * M_PI * i / n;
        costab[i] = FIX15(cos(alpha));
        sintab[i] = FIX15(sin(alpha));
    }
}

static av_cold void mdct_init(int nbits)
{
    int i, n, n4;

    n  = 1 << nbits;
    n4 = n >> 2;

    fft_init(nbits - 2);

    for (i = 0; i < n4; i++) {
        float alpha = 2.0 * M_PI * (i + 1.0 / 8.0) / n;
        xcos1[i] = FIX15(-cos(alpha));
        xsin1[i] = FIX15(-sin(alpha));
    }
}

/* butter fly op */
#define BF(pre, pim, qre, qim, pre1, pim1, qre1, qim1)  \
{                                                       \
  int ax, ay, bx, by;                                   \
  bx  = pre1;                                           \
  by  = pim1;                                           \
  ax  = qre1;                                           \
  ay  = qim1;                                           \
  pre = (bx + ax) >> 1;                                 \
  pim = (by + ay) >> 1;                                 \
  qre = (bx - ax) >> 1;                                 \
  qim = (by - ay) >> 1;                                 \
}

#define CMUL(pre, pim, are, aim, bre, bim)              \
{                                                       \
   pre = (MUL16(are, bre) - MUL16(aim, bim)) >> 15;     \
   pim = (MUL16(are, bim) + MUL16(bre, aim)) >> 15;     \
}


/* do a 2^n point complex fft on 2^ln points. */
static void fft(IComplex *z, int ln)
{
    int j, l, np, np2;
    int nblocks, nloops;
    register IComplex *p,*q;
    int tmp_re, tmp_im;

    np = 1 << ln;

    /* reverse */
    for (j = 0; j < np; j++) {
        int k = av_reverse[j] >> (8 - ln);
        if (k < j)
            FFSWAP(IComplex, z[k], z[j]);
    }

    /* pass 0 */

    p = &z[0];
    j = np >> 1;
    do {
        BF(p[0].re, p[0].im, p[1].re, p[1].im,
           p[0].re, p[0].im, p[1].re, p[1].im);
        p += 2;
    } while (--j);

    /* pass 1 */

    p = &z[0];
    j = np >> 2;
    do {
        BF(p[0].re, p[0].im, p[2].re,  p[2].im,
           p[0].re, p[0].im, p[2].re,  p[2].im);
        BF(p[1].re, p[1].im, p[3].re,  p[3].im,
           p[1].re, p[1].im, p[3].im, -p[3].re);
        p+=4;
    } while (--j);

    /* pass 2 .. ln-1 */

    nblocks = np >> 3;
    nloops  =  1 << 2;
    np2     = np >> 1;
    do {
        p = z;
        q = z + nloops;
        for (j = 0; j < nblocks; j++) {
            BF(p->re, p->im, q->re, q->im,
               p->re, p->im, q->re, q->im);
            p++;
            q++;
            for(l = nblocks; l < np2; l += nblocks) {
                CMUL(tmp_re, tmp_im, costab[l], -sintab[l], q->re, q->im);
                BF(p->re, p->im, q->re,  q->im,
                   p->re, p->im, tmp_re, tmp_im);
                p++;
                q++;
            }
            p += nloops;
            q += nloops;
        }
        nblocks = nblocks >> 1;
        nloops  = nloops  << 1;
    } while (nblocks);
}

static void mdct512(int32_t *out, int16_t *in)
{
    int i, re, im, re1, im1;
    int16_t rot[MDCT_SAMPLES];
    IComplex x[MDCT_SAMPLES/4];

    /* shift to simplify computations */
    for (i = 0; i < MDCT_SAMPLES/4; i++)
        rot[i] = -in[i + 3*MDCT_SAMPLES/4];
    for (;i < MDCT_SAMPLES; i++)
        rot[i] =  in[i -   MDCT_SAMPLES/4];

    /* pre rotation */
    for (i = 0; i < MDCT_SAMPLES/4; i++) {
        re =  ((int)rot[               2*i] - (int)rot[MDCT_SAMPLES  -1-2*i]) >> 1;
        im = -((int)rot[MDCT_SAMPLES/2+2*i] - (int)rot[MDCT_SAMPLES/2-1-2*i]) >> 1;
        CMUL(x[i].re, x[i].im, re, im, -xcos1[i], xsin1[i]);
    }

    fft(x, MDCT_NBITS - 2);

    /* post rotation */
    for (i = 0; i < MDCT_SAMPLES/4; i++) {
        re = x[i].re;
        im = x[i].im;
        CMUL(re1, im1, re, im, xsin1[i], xcos1[i]);
        out[                 2*i] = im1;
        out[MDCT_SAMPLES/2-1-2*i] = re1;
    }
}

static int calc_exp_diff(uint8_t *exp1, uint8_t *exp2, int n)
{
    int sum, i;
    sum = 0;
    for (i = 0; i < n; i++)
        sum += abs(exp1[i] - exp2[i]);
    return sum;
}

/* new exponents are sent if their Norm 1 exceed this number */
#define EXP_DIFF_THRESHOLD 1000

static void compute_exp_strategy(uint8_t exp_strategy[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS],
                                 uint8_t exp[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                                 int ch, int is_lfe)
{
    int i, j;
    int exp_diff;

    /* estimate if the exponent variation & decide if they should be
       reused in the next frame */
    exp_strategy[0][ch] = EXP_NEW;
    for (i = 1; i < AC3_MAX_BLOCKS; i++) {
        exp_diff = calc_exp_diff(exp[i][ch], exp[i-1][ch], AC3_MAX_COEFS);
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
    while (i < AC3_MAX_BLOCKS) {
        j = i + 1;
        while (j < AC3_MAX_BLOCKS && exp_strategy[j][ch] == EXP_REUSE)
            j++;
        switch (j - i) {
        case 1:  exp_strategy[i][ch] = EXP_D45; break;
        case 2:
        case 3:  exp_strategy[i][ch] = EXP_D25; break;
        default: exp_strategy[i][ch] = EXP_D15; break;
        }
        i = j;
    }
}

/* set exp[i] to min(exp[i], exp1[i]) */
static void exponent_min(uint8_t exp[AC3_MAX_COEFS], uint8_t exp1[AC3_MAX_COEFS], int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (exp1[i] < exp[i])
            exp[i] = exp1[i];
    }
}

/* update the exponents so that they are the ones the decoder will
   decode. Return the number of bits used to code the exponents */
static int encode_exp(uint8_t encoded_exp[AC3_MAX_COEFS],
                      uint8_t exp[AC3_MAX_COEFS],
                      int nb_exps, int exp_strategy)
{
    int group_size, nb_groups, i, j, k, exp_min;
    uint8_t exp1[AC3_MAX_COEFS];

    switch (exp_strategy) {
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
    for (i = 1; i <= nb_groups; i++) {
        exp_min = exp[k];
        assert(exp_min >= 0 && exp_min <= 24);
        for (j = 1; j < group_size; j++) {
            if (exp[k+j] < exp_min)
                exp_min = exp[k+j];
        }
        exp1[i] = exp_min;
        k += group_size;
    }

    /* constraint for DC exponent */
    if (exp1[0] > 15)
        exp1[0] = 15;

    /* decrease the delta between each groups to within 2 so that they can be
       differentially encoded */
    for (i = 1; i <= nb_groups; i++)
        exp1[i] = FFMIN(exp1[i], exp1[i-1] + 2);
    for (i = nb_groups-1; i >= 0; i--)
        exp1[i] = FFMIN(exp1[i], exp1[i+1] + 2);

    /* now we have the exponent values the decoder will see */
    encoded_exp[0] = exp1[0];
    k = 1;
    for (i = 1; i <= nb_groups; i++) {
        for (j = 0; j < group_size; j++)
            encoded_exp[k+j] = exp1[i];
        k += group_size;
    }

    return 4 + (nb_groups / 3) * 7;
}

/* return the size in bits taken by the mantissa */
static int compute_mantissa_size(AC3EncodeContext *s, uint8_t *m, int nb_coefs)
{
    int bits, mant, i;

    bits = 0;
    for (i = 0; i < nb_coefs; i++) {
        mant = m[i];
        switch (mant) {
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
                              uint8_t encoded_exp[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                              uint8_t exp_strategy[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS],
                              int16_t psd[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                              int16_t mask[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][50])
{
    int blk, ch;
    int16_t band_psd[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][50];

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        for (ch = 0; ch < s->channels; ch++) {
            if(exp_strategy[blk][ch] == EXP_REUSE) {
                memcpy(psd[blk][ch], psd[blk-1][ch], AC3_MAX_COEFS*sizeof(int16_t));
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
                     int16_t mask[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][50],
                     int16_t psd[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                     uint8_t bap[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                     int frame_bits, int coarse_snr_offset, int fine_snr_offset)
{
    int i, ch;
    int snr_offset;

    snr_offset = (((coarse_snr_offset - 15) << 4) + fine_snr_offset) << 2;

    for (i = 0; i < AC3_MAX_BLOCKS; i++) {
        s->mant1_cnt = 0;
        s->mant2_cnt = 0;
        s->mant4_cnt = 0;
        for (ch = 0; ch < s->channels; ch++) {
            ff_ac3_bit_alloc_calc_bap(mask[i][ch], psd[i][ch], 0,
                                      s->nb_coefs[ch], snr_offset,
                                      s->bit_alloc.floor, ff_ac3_bap_tab,
                                      bap[i][ch]);
            frame_bits += compute_mantissa_size(s, bap[i][ch], s->nb_coefs[ch]);
        }
    }
    return 16 * s->frame_size - frame_bits;
}

#define SNR_INC1 4

static int compute_bit_allocation(AC3EncodeContext *s,
                                  uint8_t bap[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                                  uint8_t encoded_exp[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                                  uint8_t exp_strategy[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS],
                                  int frame_bits)
{
    int i, ch;
    int coarse_snr_offset, fine_snr_offset;
    uint8_t bap1[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS];
    int16_t psd[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS];
    int16_t mask[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][50];
    static const int frame_bits_inc[8] = { 0, 0, 2, 2, 2, 4, 2, 4 };

    /* init default parameters */
    s->slow_decay_code = 2;
    s->fast_decay_code = 1;
    s->slow_gain_code  = 1;
    s->db_per_bit_code = 2;
    s->floor_code      = 4;
    for (ch = 0; ch < s->channels; ch++)
        s->fast_gain_code[ch] = 4;

    /* compute real values */
    s->bit_alloc.slow_decay = ff_ac3_slow_decay_tab[s->slow_decay_code] >> s->bit_alloc.sr_shift;
    s->bit_alloc.fast_decay = ff_ac3_fast_decay_tab[s->fast_decay_code] >> s->bit_alloc.sr_shift;
    s->bit_alloc.slow_gain  = ff_ac3_slow_gain_tab[s->slow_gain_code];
    s->bit_alloc.db_per_bit = ff_ac3_db_per_bit_tab[s->db_per_bit_code];
    s->bit_alloc.floor      = ff_ac3_floor_tab[s->floor_code];

    /* header size */
    frame_bits += 65;
    // if (s->channel_mode == 2)
    //    frame_bits += 2;
    frame_bits += frame_bits_inc[s->channel_mode];

    /* audio blocks */
    for (i = 0; i < AC3_MAX_BLOCKS; i++) {
        frame_bits += s->fbw_channels * 2 + 2; /* blksw * c, dithflag * c, dynrnge, cplstre */
        if (s->channel_mode == AC3_CHMODE_STEREO) {
            frame_bits++; /* rematstr */
            if (!i)
                frame_bits += 4;
        }
        frame_bits += 2 * s->fbw_channels; /* chexpstr[2] * c */
        if (s->lfe_on)
            frame_bits++; /* lfeexpstr */
        for (ch = 0; ch < s->fbw_channels; ch++) {
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
    frame_bits += 2*4 + 3 + 6 + s->channels * (4 + 3);

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
    while (coarse_snr_offset + SNR_INC1 <= 63 &&
           bit_alloc(s, mask, psd, bap1, frame_bits,
                     coarse_snr_offset + SNR_INC1, 0) >= 0) {
        coarse_snr_offset += SNR_INC1;
        memcpy(bap, bap1, sizeof(bap1));
    }
    while (coarse_snr_offset + 1 <= 63 &&
           bit_alloc(s, mask, psd, bap1, frame_bits, coarse_snr_offset + 1, 0) >= 0) {
        coarse_snr_offset++;
        memcpy(bap, bap1, sizeof(bap1));
    }

    fine_snr_offset = 0;
    while (fine_snr_offset + SNR_INC1 <= 15 &&
           bit_alloc(s, mask, psd, bap1, frame_bits,
                     coarse_snr_offset, fine_snr_offset + SNR_INC1) >= 0) {
        fine_snr_offset += SNR_INC1;
        memcpy(bap, bap1, sizeof(bap1));
    }
    while (fine_snr_offset + 1 <= 15 &&
           bit_alloc(s, mask, psd, bap1, frame_bits,
                     coarse_snr_offset, fine_snr_offset + 1) >= 0) {
        fine_snr_offset++;
        memcpy(bap, bap1, sizeof(bap1));
    }

    s->coarse_snr_offset = coarse_snr_offset;
    for (ch = 0; ch < s->channels; ch++)
        s->fine_snr_offset[ch] = fine_snr_offset;

    return 0;
}

static av_cold int set_channel_info(AC3EncodeContext *s, int channels,
                                    int64_t *channel_layout)
{
    int ch_layout;

    if (channels < 1 || channels > AC3_MAX_CHANNELS)
        return -1;
    if ((uint64_t)*channel_layout > 0x7FF)
        return -1;
    ch_layout = *channel_layout;
    if (!ch_layout)
        ch_layout = avcodec_guess_channel_layout(channels, CODEC_ID_AC3, NULL);
    if (av_get_channel_layout_nb_channels(ch_layout) != channels)
        return -1;

    s->lfe_on       = !!(ch_layout & AV_CH_LOW_FREQUENCY);
    s->channels     = channels;
    s->fbw_channels = channels - s->lfe_on;
    s->lfe_channel  = s->lfe_on ? s->fbw_channels : -1;
    if (s->lfe_on)
        ch_layout -= AV_CH_LOW_FREQUENCY;

    switch (ch_layout) {
    case AV_CH_LAYOUT_MONO:           s->channel_mode = AC3_CHMODE_MONO;   break;
    case AV_CH_LAYOUT_STEREO:         s->channel_mode = AC3_CHMODE_STEREO; break;
    case AV_CH_LAYOUT_SURROUND:       s->channel_mode = AC3_CHMODE_3F;     break;
    case AV_CH_LAYOUT_2_1:            s->channel_mode = AC3_CHMODE_2F1R;   break;
    case AV_CH_LAYOUT_4POINT0:        s->channel_mode = AC3_CHMODE_3F1R;   break;
    case AV_CH_LAYOUT_QUAD:
    case AV_CH_LAYOUT_2_2:            s->channel_mode = AC3_CHMODE_2F2R;   break;
    case AV_CH_LAYOUT_5POINT0:
    case AV_CH_LAYOUT_5POINT0_BACK:   s->channel_mode = AC3_CHMODE_3F2R;   break;
    default:
        return -1;
    }

    s->channel_map  = ff_ac3_enc_channel_map[s->channel_mode][s->lfe_on];
    *channel_layout = ch_layout;
    if (s->lfe_on)
        *channel_layout |= AV_CH_LOW_FREQUENCY;

    return 0;
}

static av_cold int AC3_encode_init(AVCodecContext *avctx)
{
    int freq = avctx->sample_rate;
    int bitrate = avctx->bit_rate;
    AC3EncodeContext *s = avctx->priv_data;
    int i, j, ch;
    int bw_code;

    avctx->frame_size = AC3_FRAME_SIZE;

    ac3_common_init();

    if (!avctx->channel_layout) {
        av_log(avctx, AV_LOG_WARNING, "No channel layout specified. The "
                                      "encoder will guess the layout, but it "
                                      "might be incorrect.\n");
    }
    if (set_channel_info(s, avctx->channels, &avctx->channel_layout)) {
        av_log(avctx, AV_LOG_ERROR, "invalid channel layout\n");
        return -1;
    }

    /* frequency */
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++)
            if ((ff_ac3_sample_rate_tab[j] >> i) == freq)
                goto found;
    }
    return -1;
 found:
    s->sample_rate        = freq;
    s->bit_alloc.sr_shift = i;
    s->bit_alloc.sr_code  = j;
    s->bitstream_id       = 8 + s->bit_alloc.sr_shift;
    s->bitstream_mode     = 0; /* complete main audio service */

    /* bitrate & frame size */
    for (i = 0; i < 19; i++) {
        if ((ff_ac3_bitrate_tab[i] >> s->bit_alloc.sr_shift)*1000 == bitrate)
            break;
    }
    if (i == 19)
        return -1;
    s->bit_rate        = bitrate;
    s->frame_size_code = i << 1;
    s->frame_size_min  = ff_ac3_frame_size_tab[s->frame_size_code][s->bit_alloc.sr_code];
    s->bits_written    = 0;
    s->samples_written = 0;
    s->frame_size      = s->frame_size_min;

    /* set bandwidth */
    if(avctx->cutoff) {
        /* calculate bandwidth based on user-specified cutoff frequency */
        int cutoff     = av_clip(avctx->cutoff, 1, s->sample_rate >> 1);
        int fbw_coeffs = cutoff * 2 * AC3_MAX_COEFS / s->sample_rate;
        bw_code        = av_clip((fbw_coeffs - 73) / 3, 0, 60);
    } else {
        /* use default bandwidth setting */
        /* XXX: should compute the bandwidth according to the frame
           size, so that we avoid annoying high frequency artifacts */
        bw_code = 50;
    }
    for(ch=0;ch<s->fbw_channels;ch++) {
        /* bandwidth for each channel */
        s->bandwidth_code[ch] = bw_code;
        s->nb_coefs[ch]       = bw_code * 3 + 73;
    }
    if (s->lfe_on)
        s->nb_coefs[s->lfe_channel] = 7; /* LFE channel always has 7 coefs */

    /* initial snr offset */
    s->coarse_snr_offset = 40;

    mdct_init(9);

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

/* output the AC-3 frame header */
static void output_frame_header(AC3EncodeContext *s, unsigned char *frame)
{
    init_put_bits(&s->pb, frame, AC3_MAX_CODED_FRAME_SIZE);

    put_bits(&s->pb, 16, 0x0b77);   /* frame header */
    put_bits(&s->pb, 16, 0);        /* crc1: will be filled later */
    put_bits(&s->pb, 2,  s->bit_alloc.sr_code);
    put_bits(&s->pb, 6,  s->frame_size_code + (s->frame_size - s->frame_size_min));
    put_bits(&s->pb, 5,  s->bitstream_id);
    put_bits(&s->pb, 3,  s->bitstream_mode);
    put_bits(&s->pb, 3,  s->channel_mode);
    if ((s->channel_mode & 0x01) && s->channel_mode != AC3_CHMODE_MONO)
        put_bits(&s->pb, 2, 1);     /* XXX -4.5 dB */
    if (s->channel_mode & 0x04)
        put_bits(&s->pb, 2, 1);     /* XXX -6 dB */
    if (s->channel_mode == AC3_CHMODE_STEREO)
        put_bits(&s->pb, 2, 0);     /* surround not indicated */
    put_bits(&s->pb, 1, s->lfe_on); /* LFE */
    put_bits(&s->pb, 5, 31);        /* dialog norm: -31 db */
    put_bits(&s->pb, 1, 0);         /* no compression control word */
    put_bits(&s->pb, 1, 0);         /* no lang code */
    put_bits(&s->pb, 1, 0);         /* no audio production info */
    put_bits(&s->pb, 1, 0);         /* no copyright */
    put_bits(&s->pb, 1, 1);         /* original bitstream */
    put_bits(&s->pb, 1, 0);         /* no time code 1 */
    put_bits(&s->pb, 1, 0);         /* no time code 2 */
    put_bits(&s->pb, 1, 0);         /* no additional bit stream info */
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

/* Output one audio block. There are AC3_MAX_BLOCKS audio blocks in one AC-3
   frame */
static void output_audio_block(AC3EncodeContext *s,
                               uint8_t exp_strategy[AC3_MAX_CHANNELS],
                               uint8_t encoded_exp[AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                               uint8_t bap[AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                               int32_t mdct_coefs[AC3_MAX_CHANNELS][AC3_MAX_COEFS],
                               int8_t global_exp[AC3_MAX_CHANNELS],
                               int block_num)
{
    int ch, nb_groups, group_size, i, baie, rbnd;
    uint8_t *p;
    uint16_t qmant[AC3_MAX_CHANNELS][AC3_MAX_COEFS];
    int exp0, exp1;
    int mant1_cnt, mant2_cnt, mant4_cnt;
    uint16_t *qmant1_ptr, *qmant2_ptr, *qmant4_ptr;
    int delta0, delta1, delta2;

    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 1, 0); /* no block switching */
    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 1, 1); /* no dither */
    put_bits(&s->pb, 1, 0);     /* no dynamic range */
    if (!block_num) {
        put_bits(&s->pb, 1, 1); /* coupling strategy present */
        put_bits(&s->pb, 1, 0); /* no coupling strategy */
    } else {
        put_bits(&s->pb, 1, 0); /* no new coupling strategy */
    }

    if (s->channel_mode == AC3_CHMODE_STEREO) {
        if (!block_num) {
            /* first block must define rematrixing (rematstr) */
            put_bits(&s->pb, 1, 1);

            /* dummy rematrixing rematflg(1:4)=0 */
            for (rbnd = 0; rbnd < 4; rbnd++)
                put_bits(&s->pb, 1, 0);
        } else {
            /* no matrixing (but should be used in the future) */
            put_bits(&s->pb, 1, 0);
        }
    }

    /* exponent strategy */
    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 2, exp_strategy[ch]);

    if (s->lfe_on)
        put_bits(&s->pb, 1, exp_strategy[s->lfe_channel]);

    /* bandwidth */
    for (ch = 0; ch < s->fbw_channels; ch++) {
        if (exp_strategy[ch] != EXP_REUSE)
            put_bits(&s->pb, 6, s->bandwidth_code[ch]);
    }

    /* exponents */
    for (ch = 0; ch < s->channels; ch++) {
        switch (exp_strategy[ch]) {
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
        for (i = 0; i < nb_groups; i++) {
            /* merge three delta in one code */
            exp0   = exp1;
            exp1   = p[0];
            p     += group_size;
            delta0 = exp1 - exp0 + 2;

            exp0   = exp1;
            exp1   = p[0];
            p     += group_size;
            delta1 = exp1 - exp0 + 2;

            exp0   = exp1;
            exp1   = p[0];
            p     += group_size;
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
    put_bits(&s->pb, 1, baie);
    if (baie) {
        put_bits(&s->pb, 6, s->coarse_snr_offset);
        for (ch = 0; ch < s->channels; ch++) {
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

    for (ch = 0; ch < s->channels; ch++) {
        int b, c, e, v;

        for (i = 0; i < s->nb_coefs[ch]; i++) {
            c = mdct_coefs[ch][i];
            e = encoded_exp[ch][i] - global_exp[ch];
            b = bap[ch][i];
            switch (b) {
            case 0:
                v = 0;
                break;
            case 1:
                v = sym_quant(c, e, 3);
                switch (mant1_cnt) {
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
                switch (mant2_cnt) {
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
                switch (mant4_cnt) {
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
    for (ch = 0; ch < s->channels; ch++) {
        int b, q;

        for (i = 0; i < s->nb_coefs[ch]; i++) {
            q = qmant[ch][i];
            b = bap[ch][i];
            switch (b) {
            case 0:                                         break;
            case 1: if (q != 128) put_bits(&s->pb,   5, q); break;
            case 2: if (q != 128) put_bits(&s->pb,   7, q); break;
            case 3:               put_bits(&s->pb,   3, q); break;
            case 4: if (q != 128) put_bits(&s->pb,   7, q); break;
            case 14:              put_bits(&s->pb,  14, q); break;
            case 15:              put_bits(&s->pb,  16, q); break;
            default:              put_bits(&s->pb, b-1, q); break;
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
    for (i = 0; i < n; i++)
        v |= abs(tab[i]);

    return av_log2(v);
}

static void lshift_tab(int16_t *tab, int n, int lshift)
{
    int i;

    if (lshift > 0) {
        for(i = 0; i < n; i++)
            tab[i] <<= lshift;
    } else if (lshift < 0) {
        lshift = -lshift;
        for (i = 0; i < n; i++)
            tab[i] >>= lshift;
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
    n = 2 * s->frame_size - (put_bits_ptr(&s->pb) - frame) - 2;
    assert(n >= 0);
    if (n > 0)
        memset(put_bits_ptr(&s->pb), 0, n);

    /* Now we must compute both crcs : this is not so easy for crc1
       because it is at the beginning of the data... */
    frame_size_58 = (frame_size >> 1) + (frame_size >> 3);

    crc1 = av_bswap16(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0,
                             frame + 4, 2 * frame_size_58 - 4));

    /* XXX: could precompute crc_inv */
    crc_inv = pow_poly((CRC16_POLY >> 1), (16 * frame_size_58) - 16, CRC16_POLY);
    crc1    = mul_poly(crc_inv, crc1, CRC16_POLY);
    AV_WB16(frame + 2, crc1);

    crc2 = av_bswap16(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0,
                             frame + 2 * frame_size_58,
                             (frame_size - frame_size_58) * 2 - 2));
    AV_WB16(frame + 2*frame_size - 2, crc2);

    return frame_size * 2;
}

static int AC3_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame, int buf_size, void *data)
{
    AC3EncodeContext *s = avctx->priv_data;
    const int16_t *samples = data;
    int i, j, k, v, ch;
    int16_t input_samples[AC3_WINDOW_SIZE];
    int32_t mdct_coef[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS];
    uint8_t exp[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS];
    uint8_t exp_strategy[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS];
    uint8_t encoded_exp[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS];
    uint8_t bap[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][AC3_MAX_COEFS];
    int8_t exp_samples[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS];
    int frame_bits;

    frame_bits = 0;
    for (ch = 0; ch < s->channels; ch++) {
        int ich = s->channel_map[ch];
        /* fixed mdct to the six sub blocks & exponent computation */
        for (i = 0; i < AC3_MAX_BLOCKS; i++) {
            const int16_t *sptr;
            int sinc;

            /* compute input samples */
            memcpy(input_samples, s->last_samples[ich], AC3_BLOCK_SIZE * sizeof(int16_t));
            sinc = s->channels;
            sptr = samples + (sinc * AC3_BLOCK_SIZE * i) + ich;
            for (j = 0; j < AC3_BLOCK_SIZE; j++) {
                v = *sptr;
                input_samples[j + AC3_BLOCK_SIZE] = v;
                s->last_samples[ich][j] = v;
                sptr += sinc;
            }

            /* apply the MDCT window */
            for (j = 0; j < AC3_BLOCK_SIZE; j++) {
                input_samples[j]                   = MUL16(input_samples[j],
                                                           ff_ac3_window[j]) >> 15;
                input_samples[AC3_WINDOW_SIZE-j-1] = MUL16(input_samples[AC3_WINDOW_SIZE-j-1],
                                                           ff_ac3_window[j]) >> 15;
            }

            /* Normalize the samples to use the maximum available precision */
            v = 14 - log2_tab(input_samples, AC3_WINDOW_SIZE);
            if (v < 0)
                v = 0;
            exp_samples[i][ch] = v - 9;
            lshift_tab(input_samples, AC3_WINDOW_SIZE, v);

            /* do the MDCT */
            mdct512(mdct_coef[i][ch], input_samples);

            /* compute "exponents". We take into account the normalization there */
            for (j = 0; j < AC3_MAX_COEFS; j++) {
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
        while (i < AC3_MAX_BLOCKS) {
            j = i + 1;
            while (j < AC3_MAX_BLOCKS && exp_strategy[j][ch] == EXP_REUSE) {
                exponent_min(exp[i][ch], exp[j][ch], s->nb_coefs[ch]);
                j++;
            }
            frame_bits += encode_exp(encoded_exp[i][ch],
                                     exp[i][ch], s->nb_coefs[ch],
                                     exp_strategy[i][ch]);
            /* copy encoded exponents for reuse case */
            for (k = i+1; k < j; k++) {
                memcpy(encoded_exp[k][ch], encoded_exp[i][ch],
                       s->nb_coefs[ch] * sizeof(uint8_t));
            }
            i = j;
        }
    }

    /* adjust for fractional frame sizes */
    while (s->bits_written >= s->bit_rate && s->samples_written >= s->sample_rate) {
        s->bits_written    -= s->bit_rate;
        s->samples_written -= s->sample_rate;
    }
    s->frame_size = s->frame_size_min + (s->bits_written * s->sample_rate < s->samples_written * s->bit_rate);
    s->bits_written    += s->frame_size * 16;
    s->samples_written += AC3_FRAME_SIZE;

    compute_bit_allocation(s, bap, encoded_exp, exp_strategy, frame_bits);
    /* everything is known... let's output the frame */
    output_frame_header(s, frame);

    for (i = 0; i < AC3_MAX_BLOCKS; i++) {
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

#ifdef TEST
/*************************************************************************/
/* TEST */

#include "libavutil/lfg.h"

#define FN (MDCT_SAMPLES/4)

static void fft_test(AVLFG *lfg)
{
    IComplex in[FN], in1[FN];
    int k, n, i;
    float sum_re, sum_im, a;

    for (i = 0; i < FN; i++) {
        in[i].re = av_lfg_get(lfg) % 65535 - 32767;
        in[i].im = av_lfg_get(lfg) % 65535 - 32767;
        in1[i]   = in[i];
    }
    fft(in, 7);

    /* do it by hand */
    for (k = 0; k < FN; k++) {
        sum_re = 0;
        sum_im = 0;
        for (n = 0; n < FN; n++) {
            a = -2 * M_PI * (n * k) / FN;
            sum_re += in1[n].re * cos(a) - in1[n].im * sin(a);
            sum_im += in1[n].re * sin(a) + in1[n].im * cos(a);
        }
        av_log(NULL, AV_LOG_DEBUG, "%3d: %6d,%6d %6.0f,%6.0f\n",
               k, in[k].re, in[k].im, sum_re / FN, sum_im / FN);
    }
}

static void mdct_test(AVLFG *lfg)
{
    int16_t input[MDCT_SAMPLES];
    int32_t output[AC3_MAX_COEFS];
    float input1[MDCT_SAMPLES];
    float output1[AC3_MAX_COEFS];
    float s, a, err, e, emax;
    int i, k, n;

    for (i = 0; i < MDCT_SAMPLES; i++) {
        input[i]  = (av_lfg_get(lfg) % 65535 - 32767) * 9 / 10;
        input1[i] = input[i];
    }

    mdct512(output, input);

    /* do it by hand */
    for (k = 0; k < AC3_MAX_COEFS; k++) {
        s = 0;
        for (n = 0; n < MDCT_SAMPLES; n++) {
            a = (2*M_PI*(2*n+1+MDCT_SAMPLES/2)*(2*k+1) / (4 * MDCT_SAMPLES));
            s += input1[n] * cos(a);
        }
        output1[k] = -2 * s / MDCT_SAMPLES;
    }

    err  = 0;
    emax = 0;
    for (i = 0; i < AC3_MAX_COEFS; i++) {
        av_log(NULL, AV_LOG_DEBUG, "%3d: %7d %7.0f\n", i, output[i], output1[i]);
        e = output[i] - output1[i];
        if (e > emax)
            emax = e;
        err += e * e;
    }
    av_log(NULL, AV_LOG_DEBUG, "err2=%f emax=%f\n", err / AC3_MAX_COEFS, emax);
}

int main(void)
{
    AVLFG lfg;

    av_log_set_level(AV_LOG_DEBUG);
    mdct_init(9);

    fft_test(&lfg);
    mdct_test(&lfg);

    return 0;
}
#endif /* TEST */

AVCodec ac3_encoder = {
    "ac3",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof(AC3EncodeContext),
    AC3_encode_init,
    AC3_encode_frame,
    AC3_encode_close,
    NULL,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .channel_layouts = (const int64_t[]){
        AV_CH_LAYOUT_MONO,
        AV_CH_LAYOUT_STEREO,
        AV_CH_LAYOUT_2_1,
        AV_CH_LAYOUT_SURROUND,
        AV_CH_LAYOUT_2_2,
        AV_CH_LAYOUT_QUAD,
        AV_CH_LAYOUT_4POINT0,
        AV_CH_LAYOUT_5POINT0,
        AV_CH_LAYOUT_5POINT0_BACK,
       (AV_CH_LAYOUT_MONO     | AV_CH_LOW_FREQUENCY),
       (AV_CH_LAYOUT_STEREO   | AV_CH_LOW_FREQUENCY),
       (AV_CH_LAYOUT_2_1      | AV_CH_LOW_FREQUENCY),
       (AV_CH_LAYOUT_SURROUND | AV_CH_LOW_FREQUENCY),
       (AV_CH_LAYOUT_2_2      | AV_CH_LOW_FREQUENCY),
       (AV_CH_LAYOUT_QUAD     | AV_CH_LOW_FREQUENCY),
       (AV_CH_LAYOUT_4POINT0  | AV_CH_LOW_FREQUENCY),
        AV_CH_LAYOUT_5POINT1,
        AV_CH_LAYOUT_5POINT1_BACK,
        0 },
};
