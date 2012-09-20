/*
 * JPEG2000 encoder and decoder common functions
 * Copyright (c) 2007 Kamil Nowosad
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
 * JPEG2000 image encoder and decoder common functions
 * @file
 * @author Kamil Nowosad
 */


#include "avcodec.h"
#include "j2k.h"

#define SHL(a, n) ((n)>=0 ? (a) << (n) : (a) >> -(n))

#if 0
void ff_j2k_printv(int *tab, int l)
{
    int i;
    for (i = 0; i < l; i++)
        printf("%.3d ", tab[i]);
    printf("\n");
}

void ff_j2k_printu(uint8_t *tab, int l)
{
    int i;
    for (i = 0; i < l; i++)
        printf("%.3hd ", tab[i]);
    printf("\n");
}
#endif

/* tag tree routines */

/** allocate the memory for tag tree */

static int tag_tree_size(int w, int h)
{
    int res = 0;
    while (w > 1 || h > 1){
        res += w * h;
        w = (w+1) >> 1;
        h = (h+1) >> 1;
    }
    return res + 1;
}

J2kTgtNode *ff_j2k_tag_tree_init(int w, int h)
{
    int pw = w, ph = h;
    J2kTgtNode *res, *t, *t2;

    t = res = av_mallocz(tag_tree_size(w, h)*sizeof(J2kTgtNode));

    if (res == NULL)
        return NULL;

    while (w > 1 || h > 1){
        int i, j;
        pw = w;
        ph = h;

        w = (w+1) >> 1;
        h = (h+1) >> 1;
        t2 = t + pw*ph;

        for (i = 0; i < ph; i++)
            for (j = 0; j < pw; j++){
                t[i*pw + j].parent = &t2[(i>>1)*w + (j>>1)];
            }
        t = t2;
    }
    t[0].parent = NULL;
    return res;
}

static void tag_tree_zero(J2kTgtNode *t, int w, int h)
{
    int i, siz = tag_tree_size(w, h);

    for (i = 0; i < siz; i++){
        t[i].val = 0;
        t[i].vis = 0;
    }
}

uint8_t ff_j2k_nbctxno_lut[256][4];

static int getnbctxno(int flag, int bandno, int vert_causal_ctx_csty_symbol)
{
    int h, v, d;

    h = ((flag & J2K_T1_SIG_E) ? 1:0)+
        ((flag & J2K_T1_SIG_W) ? 1:0);
    v = ((flag & J2K_T1_SIG_N) ? 1:0);
    if (!vert_causal_ctx_csty_symbol)
         v = v + ((flag & J2K_T1_SIG_S) ? 1:0);
    d = ((flag & J2K_T1_SIG_NE) ? 1:0)+
        ((flag & J2K_T1_SIG_NW) ? 1:0);
    if (!vert_causal_ctx_csty_symbol)
        d = d + ((flag & J2K_T1_SIG_SE) ? 1:0)+
                ((flag & J2K_T1_SIG_SW) ? 1:0);
    if (bandno < 3){
            if (bandno == 1)
                FFSWAP(int, h, v);
            if (h == 2) return 8;
            if (h == 1){
                if (v >= 1) return 7;
                if (d >= 1) return 6;
                return 5;
            }
            if (v == 2) return 4;
            if (v == 1) return 3;
            if (d >= 2) return 2;
            if (d == 1) return 1;
            return 0;
    } else{
            if (d >= 3) return 8;
            if (d == 2){
                if (h+v >= 1) return 7;
                return 6;
            }
            if (d == 1){
                if (h+v >= 2) return 5;
                if (h+v == 1) return 4;
                return 3;
            }
            if (h+v >= 2) return 2;
            if (h+v == 1) return 1;
            return 0;
    }
}

uint8_t ff_j2k_sgnctxno_lut[16][16], ff_j2k_xorbit_lut[16][16];

static int getsgnctxno(int flag, uint8_t *xorbit)
{
    int vcontrib, hcontrib;
    static const int contribtab[3][3] = {{0, -1, 1}, {-1, -1, 0}, {1, 0, 1}};
    static const int ctxlbltab[3][3] = {{13, 12, 11}, {10, 9, 10}, {11, 12, 13}};
    static const int xorbittab[3][3] = {{1, 1, 1,}, {1, 0, 0}, {0, 0, 0}};

    hcontrib = contribtab[flag & J2K_T1_SIG_E ? flag & J2K_T1_SGN_E ? 1:2:0]
                         [flag & J2K_T1_SIG_W ? flag & J2K_T1_SGN_W ? 1:2:0]+1;
    vcontrib = contribtab[flag & J2K_T1_SIG_S ? flag & J2K_T1_SGN_S ? 1:2:0]
                         [flag & J2K_T1_SIG_N ? flag & J2K_T1_SGN_N ? 1:2:0]+1;
    *xorbit = xorbittab[hcontrib][vcontrib];
    return ctxlbltab[hcontrib][vcontrib];
}

void ff_j2k_init_tier1_luts(void)
{
    int i, j;
    for (i = 0; i < 256; i++)
        for (j = 0; j < 4; j++)
            ff_j2k_nbctxno_lut[i][j] = getnbctxno(i, j, 0);
    for (i = 0; i < 16; i++)
        for (j = 0; j < 16; j++)
            ff_j2k_sgnctxno_lut[i][j] = getsgnctxno(i + (j << 8), &ff_j2k_xorbit_lut[i][j]);
}

void ff_j2k_set_significant(J2kT1Context *t1, int x, int y, int negative)
{
    x++; y++;
    t1->flags[y][x] |= J2K_T1_SIG;
    if (negative){
        t1->flags[y][x+1] |= J2K_T1_SIG_W | J2K_T1_SGN_W;
        t1->flags[y][x-1] |= J2K_T1_SIG_E | J2K_T1_SGN_E;
        t1->flags[y+1][x] |= J2K_T1_SIG_N | J2K_T1_SGN_N;
        t1->flags[y-1][x] |= J2K_T1_SIG_S | J2K_T1_SGN_S;
    } else{
        t1->flags[y][x+1] |= J2K_T1_SIG_W;
        t1->flags[y][x-1] |= J2K_T1_SIG_E;
        t1->flags[y+1][x] |= J2K_T1_SIG_N;
        t1->flags[y-1][x] |= J2K_T1_SIG_S;
    }
    t1->flags[y+1][x+1] |= J2K_T1_SIG_NW;
    t1->flags[y+1][x-1] |= J2K_T1_SIG_NE;
    t1->flags[y-1][x+1] |= J2K_T1_SIG_SW;
    t1->flags[y-1][x-1] |= J2K_T1_SIG_SE;
}

int ff_j2k_init_component(J2kComponent *comp, J2kCodingStyle *codsty, J2kQuantStyle *qntsty, int cbps, int dx, int dy)
{
    int reslevelno, bandno, gbandno = 0, ret, i, j, csize = 1;

    if (ret=ff_j2k_dwt_init(&comp->dwt, comp->coord, codsty->nreslevels-1, codsty->transform))
        return ret;
    for (i = 0; i < 2; i++)
        csize *= comp->coord[i][1] - comp->coord[i][0];

    comp->data = av_malloc(csize * sizeof(int));
    if (!comp->data)
        return AVERROR(ENOMEM);
    comp->reslevel = av_malloc(codsty->nreslevels * sizeof(J2kResLevel));

    if (!comp->reslevel)
        return AVERROR(ENOMEM);
    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
        int declvl = codsty->nreslevels - reslevelno;
        J2kResLevel *reslevel = comp->reslevel + reslevelno;

        for (i = 0; i < 2; i++)
            for (j = 0; j < 2; j++)
                reslevel->coord[i][j] =
                    ff_j2k_ceildivpow2(comp->coord[i][j], declvl - 1);

        if (reslevelno == 0)
            reslevel->nbands = 1;
        else
            reslevel->nbands = 3;

        if (reslevel->coord[0][1] == reslevel->coord[0][0])
            reslevel->num_precincts_x = 0;
        else
            reslevel->num_precincts_x = ff_j2k_ceildivpow2(reslevel->coord[0][1], codsty->log2_prec_width)
                                        - (reslevel->coord[0][0] >> codsty->log2_prec_width);

        if (reslevel->coord[1][1] == reslevel->coord[1][0])
            reslevel->num_precincts_y = 0;
        else
            reslevel->num_precincts_y = ff_j2k_ceildivpow2(reslevel->coord[1][1], codsty->log2_prec_height)
                                        - (reslevel->coord[1][0] >> codsty->log2_prec_height);

        reslevel->band = av_malloc(reslevel->nbands * sizeof(J2kBand));
        if (!reslevel->band)
            return AVERROR(ENOMEM);
        for (bandno = 0; bandno < reslevel->nbands; bandno++, gbandno++){
            J2kBand *band = reslevel->band + bandno;
            int cblkno, precx, precy, precno;
            int x0, y0, x1, y1;
            int xi0, yi0, xi1, yi1;
            int cblkperprecw, cblkperprech;

            if (qntsty->quantsty != J2K_QSTY_NONE){
                static const uint8_t lut_gain[2][4] = {{0, 0, 0, 0}, {0, 1, 1, 2}};
                int numbps;

                numbps = cbps + lut_gain[codsty->transform][bandno + reslevelno>0];
                band->stepsize = SHL(2048 + qntsty->mant[gbandno], 2 + numbps - qntsty->expn[gbandno]);
            } else
                band->stepsize = 1 << 13;

            if (reslevelno == 0){  // the same everywhere
                band->codeblock_width = 1 << FFMIN(codsty->log2_cblk_width, codsty->log2_prec_width-1);
                band->codeblock_height = 1 << FFMIN(codsty->log2_cblk_height, codsty->log2_prec_height-1);
                for (i = 0; i < 2; i++)
                    for (j = 0; j < 2; j++)
                        band->coord[i][j] = ff_j2k_ceildivpow2(comp->coord[i][j], declvl-1);
            } else{
                band->codeblock_width = 1 << FFMIN(codsty->log2_cblk_width, codsty->log2_prec_width);
                band->codeblock_height = 1 << FFMIN(codsty->log2_cblk_height, codsty->log2_prec_height);

                for (i = 0; i < 2; i++)
                    for (j = 0; j < 2; j++)
                        band->coord[i][j] = ff_j2k_ceildivpow2(comp->coord[i][j] - (((bandno+1>>i)&1) << declvl-1), declvl);
            }
            band->cblknx = ff_j2k_ceildiv(band->coord[0][1], band->codeblock_width)  - band->coord[0][0] / band->codeblock_width;
            band->cblkny = ff_j2k_ceildiv(band->coord[1][1], band->codeblock_height) - band->coord[1][0] / band->codeblock_height;

            for (j = 0; j < 2; j++)
                band->coord[0][j] = ff_j2k_ceildiv(band->coord[0][j], dx);
            for (j = 0; j < 2; j++)
                band->coord[1][j] = ff_j2k_ceildiv(band->coord[1][j], dy);

            band->cblknx = ff_j2k_ceildiv(band->cblknx, dx);
            band->cblkny = ff_j2k_ceildiv(band->cblkny, dy);

            band->cblk = av_malloc(band->cblknx * band->cblkny * sizeof(J2kCblk));
            if (!band->cblk)
                return AVERROR(ENOMEM);
            band->prec = av_malloc(reslevel->num_precincts_x * reslevel->num_precincts_y * sizeof(J2kPrec));
            if (!band->prec)
                return AVERROR(ENOMEM);

            for (cblkno = 0; cblkno < band->cblknx * band->cblkny; cblkno++){
                J2kCblk *cblk = band->cblk + cblkno;
                cblk->zero = 0;
                cblk->lblock = 3;
                cblk->length = 0;
                cblk->lengthinc = 0;
                cblk->npasses = 0;
            }

            y0 = band->coord[1][0];
            y1 = ((band->coord[1][0] + (1<<codsty->log2_prec_height)) & ~((1<<codsty->log2_prec_height)-1)) - y0;
            yi0 = 0;
            yi1 = ff_j2k_ceildivpow2(y1 - y0, codsty->log2_cblk_height) << codsty->log2_cblk_height;
            yi1 = FFMIN(yi1, band->cblkny);
            cblkperprech = 1<<(codsty->log2_prec_height - codsty->log2_cblk_height);
            for (precy = 0, precno = 0; precy < reslevel->num_precincts_y; precy++){
                for (precx = 0; precx < reslevel->num_precincts_x; precx++, precno++){
                    band->prec[precno].yi0 = yi0;
                    band->prec[precno].yi1 = yi1;
                }
                yi1 += cblkperprech;
                yi0 = yi1 - cblkperprech;
                yi1 = FFMIN(yi1, band->cblkny);
            }
            x0 = band->coord[0][0];
            x1 = ((band->coord[0][0] + (1<<codsty->log2_prec_width)) & ~((1<<codsty->log2_prec_width)-1)) - x0;
            xi0 = 0;
            xi1 = ff_j2k_ceildivpow2(x1 - x0, codsty->log2_cblk_width) << codsty->log2_cblk_width;
            xi1 = FFMIN(xi1, band->cblknx);

            cblkperprecw = 1<<(codsty->log2_prec_width - codsty->log2_cblk_width);
            for (precx = 0, precno = 0; precx < reslevel->num_precincts_x; precx++){
                for (precy = 0; precy < reslevel->num_precincts_y; precy++, precno = 0){
                    J2kPrec *prec = band->prec + precno;
                    prec->xi0 = xi0;
                    prec->xi1 = xi1;
                    prec->cblkincl = ff_j2k_tag_tree_init(prec->xi1 - prec->xi0,
                                                          prec->yi1 - prec->yi0);
                    prec->zerobits = ff_j2k_tag_tree_init(prec->xi1 - prec->xi0,
                                                          prec->yi1 - prec->yi0);
                    if (!prec->cblkincl || !prec->zerobits)
                        return AVERROR(ENOMEM);

                }
                xi1 += cblkperprecw;
                xi0 = xi1 - cblkperprecw;
                xi1 = FFMIN(xi1, band->cblknx);
            }
        }
    }
    return 0;
}

void ff_j2k_reinit(J2kComponent *comp, J2kCodingStyle *codsty)
{
    int reslevelno, bandno, cblkno, precno;
    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
        J2kResLevel *rlevel = comp->reslevel + reslevelno;
        for (bandno = 0; bandno < rlevel->nbands; bandno++){
            J2kBand *band = rlevel->band + bandno;
            for(precno = 0; precno < rlevel->num_precincts_x * rlevel->num_precincts_y; precno++){
                J2kPrec *prec = band->prec + precno;
                tag_tree_zero(prec->zerobits, prec->xi1 - prec->xi0, prec->yi1 - prec->yi0);
                tag_tree_zero(prec->cblkincl, prec->xi1 - prec->xi0, prec->yi1 - prec->yi0);
            }
            for (cblkno = 0; cblkno < band->cblknx * band->cblkny; cblkno++){
                J2kCblk *cblk = band->cblk + cblkno;
                cblk->length = 0;
                cblk->lblock = 3;
            }
        }
    }
}

void ff_j2k_cleanup(J2kComponent *comp, J2kCodingStyle *codsty)
{
    int reslevelno, bandno, precno;
    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
        J2kResLevel *reslevel = comp->reslevel + reslevelno;

        for (bandno = 0; bandno < reslevel->nbands ; bandno++){
            J2kBand *band = reslevel->band + bandno;
                for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                    J2kPrec *prec = band->prec + precno;
                    av_freep(&prec->zerobits);
                    av_freep(&prec->cblkincl);
                }
                av_freep(&band->cblk);
                av_freep(&band->prec);
            }
        av_freep(&reslevel->band);
    }

    ff_j2k_dwt_destroy(&comp->dwt);
    av_freep(&comp->reslevel);
    av_freep(&comp->data);
}
