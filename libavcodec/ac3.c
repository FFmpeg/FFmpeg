/*
 * Common code between AC3 encoder and decoder
 * Copyright (c) 2000 Fabrice Bellard.
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
 * @file ac3.c
 * Common code between AC3 encoder and decoder.
 */

#include "avcodec.h"
#include "ac3.h"
#include "ac3tab.h"
#include "bitstream.h"

static inline int calc_lowcomp1(int a, int b0, int b1, int c)
{
    if ((b0 + 256) == b1) {
        a = c;
    } else if (b0 > b1) {
        a = FFMAX(a - 64, 0);
    }
    return a;
}

static inline int calc_lowcomp(int a, int b0, int b1, int bin)
{
    if (bin < 7) {
        return calc_lowcomp1(a, b0, b1, 384);
    } else if (bin < 20) {
        return calc_lowcomp1(a, b0, b1, 320);
    } else {
        return FFMAX(a - 128, 0);
    }
}

void ff_ac3_bit_alloc_calc_psd(int8_t *exp, int start, int end, int16_t *psd,
                               int16_t *bndpsd)
{
    int bin, i, j, k, end1, v;

    /* exponent mapping to PSD */
    for(bin=start;bin<end;bin++) {
        psd[bin]=(3072 - (exp[bin] << 7));
    }

    /* PSD integration */
    j=start;
    k=masktab[start];
    do {
        v=psd[j];
        j++;
        end1 = FFMIN(bndtab[k+1], end);
        for(i=j;i<end1;i++) {
            /* logadd */
            int adr = FFMIN(FFABS(v - psd[j]) >> 1, 255);
            v = FFMAX(v, psd[j]) + latab[adr];
            j++;
        }
        bndpsd[k]=v;
        k++;
    } while (end > bndtab[k]);
}

void ff_ac3_bit_alloc_calc_mask(AC3BitAllocParameters *s, int16_t *bndpsd,
                                int start, int end, int fgain, int is_lfe,
                                int deltbae, int deltnseg, uint8_t *deltoffst,
                                uint8_t *deltlen, uint8_t *deltba,
                                int16_t *mask)
{
    int16_t excite[50]; /* excitation */
    int bin, k;
    int bndstrt, bndend, begin, end1, tmp;
    int lowcomp, fastleak, slowleak;

    /* excitation function */
    bndstrt = masktab[start];
    bndend = masktab[end-1] + 1;

    if (bndstrt == 0) {
        lowcomp = 0;
        lowcomp = calc_lowcomp1(lowcomp, bndpsd[0], bndpsd[1], 384);
        excite[0] = bndpsd[0] - fgain - lowcomp;
        lowcomp = calc_lowcomp1(lowcomp, bndpsd[1], bndpsd[2], 384);
        excite[1] = bndpsd[1] - fgain - lowcomp;
        begin = 7;
        for (bin = 2; bin < 7; bin++) {
            if (!(is_lfe && bin == 6))
                lowcomp = calc_lowcomp1(lowcomp, bndpsd[bin], bndpsd[bin+1], 384);
            fastleak = bndpsd[bin] - fgain;
            slowleak = bndpsd[bin] - s->sgain;
            excite[bin] = fastleak - lowcomp;
            if (!(is_lfe && bin == 6)) {
                if (bndpsd[bin] <= bndpsd[bin+1]) {
                    begin = bin + 1;
                    break;
                }
            }
        }

        end1=bndend;
        if (end1 > 22) end1=22;

        for (bin = begin; bin < end1; bin++) {
            if (!(is_lfe && bin == 6))
                lowcomp = calc_lowcomp(lowcomp, bndpsd[bin], bndpsd[bin+1], bin);

            fastleak = FFMAX(fastleak - s->fdecay, bndpsd[bin] - fgain);
            slowleak = FFMAX(slowleak - s->sdecay, bndpsd[bin] - s->sgain);
            excite[bin] = FFMAX(fastleak - lowcomp, slowleak);
        }
        begin = 22;
    } else {
        /* coupling channel */
        begin = bndstrt;

        fastleak = (s->cplfleak << 8) + 768;
        slowleak = (s->cplsleak << 8) + 768;
    }

    for (bin = begin; bin < bndend; bin++) {
        fastleak = FFMAX(fastleak - s->fdecay, bndpsd[bin] - fgain);
        slowleak = FFMAX(slowleak - s->sdecay, bndpsd[bin] - s->sgain);
        excite[bin] = FFMAX(fastleak, slowleak);
    }

    /* compute masking curve */

    for (bin = bndstrt; bin < bndend; bin++) {
        tmp = s->dbknee - bndpsd[bin];
        if (tmp > 0) {
            excite[bin] += tmp >> 2;
        }
        mask[bin] = FFMAX(hth[bin >> s->halfratecod][s->fscod], excite[bin]);
    }

    /* delta bit allocation */

    if (deltbae == 0 || deltbae == 1) {
        int band, seg, delta;
        band = 0;
        for (seg = 0; seg < deltnseg; seg++) {
            band += deltoffst[seg];
            if (deltba[seg] >= 4) {
                delta = (deltba[seg] - 3) << 7;
            } else {
                delta = (deltba[seg] - 4) << 7;
            }
            for (k = 0; k < deltlen[seg]; k++) {
                mask[band] += delta;
                band++;
            }
        }
    }
}

void ff_ac3_bit_alloc_calc_bap(int16_t *mask, int16_t *psd, int start, int end,
                               int snroffset, int floor, uint8_t *bap)
{
    int i, j, k, end1, v, address;

    /* special case, if snroffset is -960, set all bap's to zero */
    if(snroffset == -960) {
        memset(bap, 0, 256);
        return;
    }

    i = start;
    j = masktab[start];
    do {
        v = (FFMAX(mask[j] - snroffset - floor, 0) & 0x1FE0) + floor;
        end1 = FFMIN(bndtab[j] + bndsz[j], end);
        for (k = i; k < end1; k++) {
            address = av_clip((psd[i] - v) >> 5, 0, 63);
            bap[i] = baptab[address];
            i++;
        }
    } while (end > bndtab[j++]);
}

/* AC3 bit allocation. The algorithm is the one described in the AC3
   spec. */
void ac3_parametric_bit_allocation(AC3BitAllocParameters *s, uint8_t *bap,
                                   int8_t *exp, int start, int end,
                                   int snroffset, int fgain, int is_lfe,
                                   int deltbae,int deltnseg,
                                   uint8_t *deltoffst, uint8_t *deltlen,
                                   uint8_t *deltba)
{
    int16_t psd[256];   /* scaled exponents */
    int16_t bndpsd[50]; /* interpolated exponents */
    int16_t mask[50];   /* masking value */

    ff_ac3_bit_alloc_calc_psd(exp, start, end, psd, bndpsd);

    ff_ac3_bit_alloc_calc_mask(s, bndpsd, start, end, fgain, is_lfe,
                               deltbae, deltnseg, deltoffst, deltlen, deltba,
                               mask);

    ff_ac3_bit_alloc_calc_bap(mask, psd, start, end, snroffset, s->floor, bap);
}

/**
 * Initializes some tables.
 * note: This function must remain thread safe because it is called by the
 *       AVParser init code.
 */
void ac3_common_init(void)
{
    int i, j, k, l, v;
    /* compute bndtab and masktab from bandsz */
    k = 0;
    l = 0;
    for(i=0;i<50;i++) {
        bndtab[i] = l;
        v = bndsz[i];
        for(j=0;j<v;j++) masktab[k++]=i;
        l += v;
    }
    bndtab[50] = l;
}

int ff_ac3_parse_header(const uint8_t buf[7], AC3HeaderInfo *hdr)
{
    GetBitContext gbc;

    memset(hdr, 0, sizeof(*hdr));

    init_get_bits(&gbc, buf, 54);

    hdr->sync_word = get_bits(&gbc, 16);
    if(hdr->sync_word != 0x0B77)
        return -1;

    /* read ahead to bsid to make sure this is AC-3, not E-AC-3 */
    hdr->bsid = show_bits_long(&gbc, 29) & 0x1F;
    if(hdr->bsid > 10)
        return -2;

    hdr->crc1 = get_bits(&gbc, 16);
    hdr->fscod = get_bits(&gbc, 2);
    if(hdr->fscod == 3)
        return -3;

    hdr->frmsizecod = get_bits(&gbc, 6);
    if(hdr->frmsizecod > 37)
        return -4;

    skip_bits(&gbc, 5); // skip bsid, already got it

    hdr->bsmod = get_bits(&gbc, 3);
    hdr->acmod = get_bits(&gbc, 3);
    if((hdr->acmod & 1) && hdr->acmod != 1) {
        hdr->cmixlev = get_bits(&gbc, 2);
    }
    if(hdr->acmod & 4) {
        hdr->surmixlev = get_bits(&gbc, 2);
    }
    if(hdr->acmod == 2) {
        hdr->dsurmod = get_bits(&gbc, 2);
    }
    hdr->lfeon = get_bits1(&gbc);

    hdr->halfratecod = FFMAX(hdr->bsid, 8) - 8;
    hdr->sample_rate = ff_ac3_freqs[hdr->fscod] >> hdr->halfratecod;
    hdr->bit_rate = (ff_ac3_bitratetab[hdr->frmsizecod>>1] * 1000) >> hdr->halfratecod;
    hdr->channels = ff_ac3_channels[hdr->acmod] + hdr->lfeon;
    hdr->frame_size = ff_ac3_frame_sizes[hdr->frmsizecod][hdr->fscod] * 2;

    return 0;
}
