/*
 * JPEG2000 image decoder
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
 * JPEG2000 image decoder
 * @file
 * @author Kamil Nowosad
 */

// #define DEBUG

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "j2k.h"
#include "libavutil/common.h"

#define JP2_SIG_TYPE    0x6A502020
#define JP2_SIG_VALUE   0x0D0A870A
#define JP2_CODESTREAM  0x6A703263

#define HAD_COC 0x01
#define HAD_QCC 0x02

typedef struct {
   J2kComponent *comp;
   uint8_t properties[4];
   J2kCodingStyle codsty[4];
   J2kQuantStyle  qntsty[4];
} J2kTile;

typedef struct {
    AVCodecContext *avctx;
    AVFrame picture;
    GetByteContext g;

    int width, height; ///< image width and height
    int image_offset_x, image_offset_y;
    int tile_offset_x, tile_offset_y;
    uint8_t cbps[4]; ///< bits per sample in particular components
    uint8_t sgnd[4]; ///< if a component is signed
    uint8_t properties[4];
    int cdx[4], cdy[4];
    int precision;
    int ncomponents;
    int tile_width, tile_height; ///< tile size
    int numXtiles, numYtiles;
    int maxtilelen;

    J2kCodingStyle codsty[4];
    J2kQuantStyle  qntsty[4];

    int bit_index;

    int curtileno;

    J2kTile *tile;
} J2kDecoderContext;

static int get_bits(J2kDecoderContext *s, int n)
{
    int res = 0;

    while (--n >= 0){
        res <<= 1;
        if (s->bit_index == 0) {
            s->bit_index = 7 + (bytestream2_get_byte(&s->g) != 0xFFu);
        }
        s->bit_index--;
        res |= (bytestream2_peek_byte(&s->g) >> s->bit_index) & 1;
    }
    return res;
}

static void j2k_flush(J2kDecoderContext *s)
{
    if (bytestream2_get_byte(&s->g) == 0xff)
        bytestream2_skip(&s->g, 1);
    s->bit_index = 8;
}
#if 0
void printcomp(J2kComponent *comp)
{
    int i;
    for (i = 0; i < comp->y1 - comp->y0; i++)
        ff_j2k_printv(comp->data + i * (comp->x1 - comp->x0), comp->x1 - comp->x0);
}

static void nspaces(FILE *fd, int n)
{
    while(n--) putc(' ', fd);
}

static void dump(J2kDecoderContext *s, FILE *fd)
{
    int tileno, compno, reslevelno, bandno, precno;
    fprintf(fd, "XSiz = %d, YSiz = %d, tile_width = %d, tile_height = %d\n"
                "numXtiles = %d, numYtiles = %d, ncomponents = %d\n"
                "tiles:\n",
            s->width, s->height, s->tile_width, s->tile_height,
            s->numXtiles, s->numYtiles, s->ncomponents);
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        J2kTile *tile = s->tile + tileno;
        nspaces(fd, 2);
        fprintf(fd, "tile %d:\n", tileno);
        for(compno = 0; compno < s->ncomponents; compno++){
            J2kComponent *comp = tile->comp + compno;
            nspaces(fd, 4);
            fprintf(fd, "component %d:\n", compno);
            nspaces(fd, 4);
            fprintf(fd, "x0 = %d, x1 = %d, y0 = %d, y1 = %d\n",
                        comp->x0, comp->x1, comp->y0, comp->y1);
            for(reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
                J2kResLevel *reslevel = comp->reslevel + reslevelno;
                nspaces(fd, 6);
                fprintf(fd, "reslevel %d:\n", reslevelno);
                nspaces(fd, 6);
                fprintf(fd, "x0 = %d, x1 = %d, y0 = %d, y1 = %d, nbands = %d\n",
                        reslevel->x0, reslevel->x1, reslevel->y0,
                        reslevel->y1, reslevel->nbands);
                for(bandno = 0; bandno < reslevel->nbands; bandno++){
                    J2kBand *band = reslevel->band + bandno;
                    nspaces(fd, 8);
                    fprintf(fd, "band %d:\n", bandno);
                    nspaces(fd, 8);
                    fprintf(fd, "x0 = %d, x1 = %d, y0 = %d, y1 = %d,"
                                "codeblock_width = %d, codeblock_height = %d cblknx = %d cblkny = %d\n",
                                band->x0, band->x1,
                                band->y0, band->y1,
                                band->codeblock_width, band->codeblock_height,
                                band->cblknx, band->cblkny);
                    for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                        J2kPrec *prec = band->prec + precno;
                        nspaces(fd, 10);
                        fprintf(fd, "prec %d:\n", precno);
                        nspaces(fd, 10);
                        fprintf(fd, "xi0 = %d, xi1 = %d, yi0 = %d, yi1 = %d\n",
                                     prec->xi0, prec->xi1, prec->yi0, prec->yi1);
                    }
                }
            }
        }
    }
}
#endif

/** decode the value stored in node */
static int tag_tree_decode(J2kDecoderContext *s, J2kTgtNode *node, int threshold)
{
    J2kTgtNode *stack[30];
    int sp = -1, curval = 0;

    if(!node)
        return AVERROR(EINVAL);

    while(node && !node->vis){
        stack[++sp] = node;
        node = node->parent;
    }

    if (node)
        curval = node->val;
    else
        curval = stack[sp]->val;

    while(curval < threshold && sp >= 0){
        if (curval < stack[sp]->val)
            curval = stack[sp]->val;
        while (curval < threshold){
            int ret;
            if ((ret = get_bits(s, 1)) > 0){
                stack[sp]->vis++;
                break;
            } else if (!ret)
                curval++;
            else
                return ret;
        }
        stack[sp]->val = curval;
        sp--;
    }
    return curval;
}

/* marker segments */
/** get sizes and offsets of image, tiles; number of components */
static int get_siz(J2kDecoderContext *s)
{
    int i, ret;

    if (bytestream2_get_bytes_left(&s->g) < 36)
        return AVERROR(EINVAL);

                        bytestream2_get_be16u(&s->g); // Rsiz (skipped)
             s->width = bytestream2_get_be32u(&s->g); // width
            s->height = bytestream2_get_be32u(&s->g); // height
    s->image_offset_x = bytestream2_get_be32u(&s->g); // X0Siz
    s->image_offset_y = bytestream2_get_be32u(&s->g); // Y0Siz

        s->tile_width = bytestream2_get_be32u(&s->g); // XTSiz
       s->tile_height = bytestream2_get_be32u(&s->g); // YTSiz
     s->tile_offset_x = bytestream2_get_be32u(&s->g); // XT0Siz
     s->tile_offset_y = bytestream2_get_be32u(&s->g); // YT0Siz
       s->ncomponents = bytestream2_get_be16u(&s->g); // CSiz

    if(s->ncomponents <= 0 || s->ncomponents > 4) {
        av_log(s->avctx, AV_LOG_ERROR, "unsupported/invalid ncomponents: %d\n", s->ncomponents);
        return AVERROR(EINVAL);
    }
    if(s->tile_width<=0 || s->tile_height<=0)
        return AVERROR(EINVAL);

    if (bytestream2_get_bytes_left(&s->g) < 3 * s->ncomponents)
        return AVERROR(EINVAL);

    for (i = 0; i < s->ncomponents; i++){ // Ssiz_i XRsiz_i, YRsiz_i
        uint8_t x = bytestream2_get_byteu(&s->g);
        s->cbps[i] = (x & 0x7f) + 1;
        s->precision = FFMAX(s->cbps[i], s->precision);
        s->sgnd[i] = !!(x & 0x80);
        s->cdx[i] = bytestream2_get_byteu(&s->g);
        s->cdy[i] = bytestream2_get_byteu(&s->g);
    }

    s->numXtiles = ff_j2k_ceildiv(s->width - s->tile_offset_x, s->tile_width);
    s->numYtiles = ff_j2k_ceildiv(s->height - s->tile_offset_y, s->tile_height);

    if(s->numXtiles * (uint64_t)s->numYtiles > INT_MAX/sizeof(J2kTile))
        return AVERROR(EINVAL);

    s->tile = av_mallocz(s->numXtiles * s->numYtiles * sizeof(J2kTile));
    if (!s->tile)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->numXtiles * s->numYtiles; i++){
        J2kTile *tile = s->tile + i;

        tile->comp = av_mallocz(s->ncomponents * sizeof(J2kComponent));
        if (!tile->comp)
            return AVERROR(ENOMEM);
    }

    s->avctx->width  = s->width  - s->image_offset_x;
    s->avctx->height = s->height - s->image_offset_y;

    switch(s->ncomponents){
    case 1:
        if (s->precision > 8) {
            s->avctx->pix_fmt = AV_PIX_FMT_GRAY16;
        } else {
            s->avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        }
        break;
    case 3:
        if (s->precision > 8) {
            s->avctx->pix_fmt = AV_PIX_FMT_RGB48;
        } else {
            s->avctx->pix_fmt = AV_PIX_FMT_RGB24;
        }
        break;
    case 4:
        s->avctx->pix_fmt = AV_PIX_FMT_RGBA;
        break;
    }

    if (s->picture.data[0])
        s->avctx->release_buffer(s->avctx, &s->picture);

    if ((ret = ff_get_buffer(s->avctx, &s->picture)) < 0)
        return ret;

    s->picture.pict_type = AV_PICTURE_TYPE_I;
    s->picture.key_frame = 1;

    return 0;
}

/** get common part for COD and COC segments */
static int get_cox(J2kDecoderContext *s, J2kCodingStyle *c)
{
    if (bytestream2_get_bytes_left(&s->g) < 5)
        return AVERROR(EINVAL);
          c->nreslevels = bytestream2_get_byteu(&s->g) + 1; // num of resolution levels - 1
     c->log2_cblk_width = bytestream2_get_byteu(&s->g) + 2; // cblk width
    c->log2_cblk_height = bytestream2_get_byteu(&s->g) + 2; // cblk height

    c->cblk_style = bytestream2_get_byteu(&s->g);
    if (c->cblk_style != 0){ // cblk style
        av_log(s->avctx, AV_LOG_WARNING, "extra cblk styles %X\n", c->cblk_style);
    }
    c->transform = bytestream2_get_byteu(&s->g); // transformation
    if (c->csty & J2K_CSTY_PREC) {
        int i;

        for (i = 0; i < c->nreslevels; i++)
            bytestream2_get_byte(&s->g);
    }
    return 0;
}

/** get coding parameters for a particular tile or whole image*/
static int get_cod(J2kDecoderContext *s, J2kCodingStyle *c, uint8_t *properties)
{
    J2kCodingStyle tmp;
    int compno;

    if (bytestream2_get_bytes_left(&s->g) < 5)
        return AVERROR(EINVAL);

    tmp.log2_prec_width  =
    tmp.log2_prec_height = 15;

    tmp.csty = bytestream2_get_byteu(&s->g);

    if (bytestream2_get_byteu(&s->g)){ // progression level
        av_log(s->avctx, AV_LOG_ERROR, "only LRCP progression supported\n");
        return -1;
    }

    tmp.nlayers = bytestream2_get_be16u(&s->g);
        tmp.mct = bytestream2_get_byteu(&s->g); // multiple component transformation

    get_cox(s, &tmp);
    for (compno = 0; compno < s->ncomponents; compno++){
        if (!(properties[compno] & HAD_COC))
            memcpy(c + compno, &tmp, sizeof(J2kCodingStyle));
    }
    return 0;
}

/** get coding parameters for a component in the whole image on a particular tile */
static int get_coc(J2kDecoderContext *s, J2kCodingStyle *c, uint8_t *properties)
{
    int compno;

    if (bytestream2_get_bytes_left(&s->g) < 2)
        return AVERROR(EINVAL);

    compno = bytestream2_get_byteu(&s->g);

    c += compno;
    c->csty = bytestream2_get_byte(&s->g);
    get_cox(s, c);

    properties[compno] |= HAD_COC;
    return 0;
}

/** get common part for QCD and QCC segments */
static int get_qcx(J2kDecoderContext *s, int n, J2kQuantStyle *q)
{
    int i, x;

    if (bytestream2_get_bytes_left(&s->g) < 1)
        return AVERROR(EINVAL);

    x = bytestream2_get_byteu(&s->g); // Sqcd

    q->nguardbits = x >> 5;
      q->quantsty = x & 0x1f;

    if (q->quantsty == J2K_QSTY_NONE){
        n -= 3;
        if (bytestream2_get_bytes_left(&s->g) < n || 32*3 < n)
            return AVERROR(EINVAL);
        for (i = 0; i < n; i++)
            q->expn[i] = bytestream2_get_byteu(&s->g) >> 3;
    } else if (q->quantsty == J2K_QSTY_SI){
        if (bytestream2_get_bytes_left(&s->g) < 2)
            return AVERROR(EINVAL);
        x = bytestream2_get_be16u(&s->g);
        q->expn[0] = x >> 11;
        q->mant[0] = x & 0x7ff;
        for (i = 1; i < 32 * 3; i++){
            int curexpn = FFMAX(0, q->expn[0] - (i-1)/3);
            q->expn[i] = curexpn;
            q->mant[i] = q->mant[0];
        }
    } else{
        n = (n - 3) >> 1;
        if (bytestream2_get_bytes_left(&s->g) < 2 * n || 32*3 < n)
            return AVERROR(EINVAL);
        for (i = 0; i < n; i++){
            x = bytestream2_get_be16u(&s->g);
            q->expn[i] = x >> 11;
            q->mant[i] = x & 0x7ff;
        }
    }
    return 0;
}

/** get quantization parameters for a particular tile or a whole image */
static int get_qcd(J2kDecoderContext *s, int n, J2kQuantStyle *q, uint8_t *properties)
{
    J2kQuantStyle tmp;
    int compno;

    if (get_qcx(s, n, &tmp))
        return -1;
    for (compno = 0; compno < s->ncomponents; compno++)
        if (!(properties[compno] & HAD_QCC))
            memcpy(q + compno, &tmp, sizeof(J2kQuantStyle));
    return 0;
}

/** get quantization parameters for a component in the whole image on in a particular tile */
static int get_qcc(J2kDecoderContext *s, int n, J2kQuantStyle *q, uint8_t *properties)
{
    int compno;

    if (bytestream2_get_bytes_left(&s->g) < 1)
        return AVERROR(EINVAL);

    compno = bytestream2_get_byteu(&s->g);
    properties[compno] |= HAD_QCC;
    return get_qcx(s, n-1, q+compno);
}

/** get start of tile segment */
static uint8_t get_sot(J2kDecoderContext *s)
{
    if (bytestream2_get_bytes_left(&s->g) < 8)
        return AVERROR(EINVAL);

    s->curtileno = bytestream2_get_be16u(&s->g); ///< Isot
    if((unsigned)s->curtileno >= s->numXtiles * s->numYtiles){
        s->curtileno=0;
        return AVERROR(EINVAL);
    }

    bytestream2_skipu(&s->g, 4); ///< Psot (ignored)

    if (!bytestream2_get_byteu(&s->g)){ ///< TPsot
        J2kTile *tile = s->tile + s->curtileno;

        /* copy defaults */
        memcpy(tile->codsty, s->codsty, s->ncomponents * sizeof(J2kCodingStyle));
        memcpy(tile->qntsty, s->qntsty, s->ncomponents * sizeof(J2kQuantStyle));
    }
    bytestream2_get_byteu(&s->g); ///< TNsot

    return 0;
}

static int init_tile(J2kDecoderContext *s, int tileno)
{
    int compno,
        tilex = tileno % s->numXtiles,
        tiley = tileno / s->numXtiles;
    J2kTile *tile = s->tile + tileno;

    if (!tile->comp)
        return AVERROR(ENOMEM);
    for (compno = 0; compno < s->ncomponents; compno++){
        J2kComponent *comp = tile->comp + compno;
        J2kCodingStyle *codsty = tile->codsty + compno;
        J2kQuantStyle  *qntsty = tile->qntsty + compno;
        int ret; // global bandno

        comp->coord[0][0] = FFMAX(tilex * s->tile_width + s->tile_offset_x, s->image_offset_x);
        comp->coord[0][1] = FFMIN((tilex+1)*s->tile_width + s->tile_offset_x, s->width);
        comp->coord[1][0] = FFMAX(tiley * s->tile_height + s->tile_offset_y, s->image_offset_y);
        comp->coord[1][1] = FFMIN((tiley+1)*s->tile_height + s->tile_offset_y, s->height);

        if (ret = ff_j2k_init_component(comp, codsty, qntsty, s->cbps[compno], s->cdx[compno], s->cdy[compno]))
            return ret;
    }
    return 0;
}

/** read the number of coding passes */
static int getnpasses(J2kDecoderContext *s)
{
    int num;
    if (!get_bits(s, 1))
        return 1;
    if (!get_bits(s, 1))
        return 2;
    if ((num = get_bits(s, 2)) != 3)
        return num < 0 ? num : 3 + num;
    if ((num = get_bits(s, 5)) != 31)
        return num < 0 ? num : 6 + num;
    num = get_bits(s, 7);
    return num < 0 ? num : 37 + num;
}

static int getlblockinc(J2kDecoderContext *s)
{
    int res = 0, ret;
    while (ret = get_bits(s, 1)){
        if (ret < 0)
            return ret;
        res++;
    }
    return res;
}

static int decode_packet(J2kDecoderContext *s, J2kCodingStyle *codsty, J2kResLevel *rlevel, int precno,
                         int layno, uint8_t *expn, int numgbits)
{
    int bandno, cblkny, cblknx, cblkno, ret;

    if (!(ret = get_bits(s, 1))){
        j2k_flush(s);
        return 0;
    } else if (ret < 0)
        return ret;

    for (bandno = 0; bandno < rlevel->nbands; bandno++){
        J2kBand *band = rlevel->band + bandno;
        J2kPrec *prec = band->prec + precno;
        int pos = 0;

        if (band->coord[0][0] == band->coord[0][1]
        ||  band->coord[1][0] == band->coord[1][1])
            continue;

        for (cblkny = prec->yi0; cblkny < prec->yi1; cblkny++)
            for(cblknx = prec->xi0, cblkno = cblkny * band->cblknx + cblknx; cblknx < prec->xi1; cblknx++, cblkno++, pos++){
                J2kCblk *cblk = band->cblk + cblkno;
                int incl, newpasses, llen;

                if (cblk->npasses)
                    incl = get_bits(s, 1);
                else
                    incl = tag_tree_decode(s, prec->cblkincl + pos, layno+1) == layno;
                if (!incl)
                    continue;
                else if (incl < 0)
                    return incl;

                if (!cblk->npasses)
                    cblk->nonzerobits = expn[bandno] + numgbits - 1 - tag_tree_decode(s, prec->zerobits + pos, 100);
                if ((newpasses = getnpasses(s)) < 0)
                    return newpasses;
                if ((llen = getlblockinc(s)) < 0)
                    return llen;
                cblk->lblock += llen;
                if ((ret = get_bits(s, av_log2(newpasses) + cblk->lblock)) < 0)
                    return ret;
                cblk->lengthinc = ret;
                cblk->npasses += newpasses;
            }
    }
    j2k_flush(s);

    if (codsty->csty & J2K_CSTY_EPH) {
        if (bytestream2_peek_be16(&s->g) == J2K_EPH) {
            bytestream2_skip(&s->g, 2);
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "EPH marker not found.\n");
        }
    }

    for (bandno = 0; bandno < rlevel->nbands; bandno++){
        J2kBand *band = rlevel->band + bandno;
        int yi, cblknw = band->prec[precno].xi1 - band->prec[precno].xi0;
        for (yi = band->prec[precno].yi0; yi < band->prec[precno].yi1; yi++){
            int xi;
            for (xi = band->prec[precno].xi0; xi < band->prec[precno].xi1; xi++){
                J2kCblk *cblk = band->cblk + yi * cblknw + xi;
                if (bytestream2_get_bytes_left(&s->g) < cblk->lengthinc)
                    return AVERROR(EINVAL);
                bytestream2_get_bufferu(&s->g, cblk->data, cblk->lengthinc);
                cblk->length += cblk->lengthinc;
                cblk->lengthinc = 0;
            }
        }
    }
    return 0;
}

static int decode_packets(J2kDecoderContext *s, J2kTile *tile)
{
    int layno, reslevelno, compno, precno, ok_reslevel;
    s->bit_index = 8;
    for (layno = 0; layno < tile->codsty[0].nlayers; layno++){
        ok_reslevel = 1;
        for (reslevelno = 0; ok_reslevel; reslevelno++){
            ok_reslevel = 0;
            for (compno = 0; compno < s->ncomponents; compno++){
                J2kCodingStyle *codsty = tile->codsty + compno;
                J2kQuantStyle  *qntsty = tile->qntsty + compno;
                if (reslevelno < codsty->nreslevels){
                    J2kResLevel *rlevel = tile->comp[compno].reslevel + reslevelno;
                    ok_reslevel = 1;
                    for (precno = 0; precno < rlevel->num_precincts_x * rlevel->num_precincts_y; precno++){
                        if (decode_packet(s, codsty, rlevel, precno, layno, qntsty->expn +
                                          (reslevelno ? 3*(reslevelno-1)+1 : 0), qntsty->nguardbits))
                            return -1;
                    }
                }
            }
        }
    }
    return 0;
}

/* TIER-1 routines */
static void decode_sigpass(J2kT1Context *t1, int width, int height, int bpno, int bandno, int bpass_csty_symbol,
                           int vert_causal_ctx_csty_symbol)
{
    int mask = 3 << (bpno - 1), y0, x, y;

    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0+4; y++){
                if ((t1->flags[y+1][x+1] & J2K_T1_SIG_NB)
                && !(t1->flags[y+1][x+1] & (J2K_T1_SIG | J2K_T1_VIS))){
                    int vert_causal_ctx_csty_loc_symbol = vert_causal_ctx_csty_symbol && (x == 3 && y == 3);
                    if (ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ff_j2k_getnbctxno(t1->flags[y+1][x+1], bandno,
                                      vert_causal_ctx_csty_loc_symbol))){
                        int xorbit, ctxno = ff_j2k_getsgnctxno(t1->flags[y+1][x+1], &xorbit);
                        if (bpass_csty_symbol)
                             t1->data[y][x] = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ctxno) ? -mask : mask;
                        else
                             t1->data[y][x] = (ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ctxno) ^ xorbit) ?
                                               -mask : mask;

                        ff_j2k_set_significant(t1, x, y, t1->data[y][x] < 0);
                    }
                    t1->flags[y+1][x+1] |= J2K_T1_VIS;
                }
            }
}

static void decode_refpass(J2kT1Context *t1, int width, int height, int bpno)
{
    int phalf, nhalf;
    int y0, x, y;

    phalf = 1 << (bpno - 1);
    nhalf = -phalf;

    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0+4; y++){
                if ((t1->flags[y+1][x+1] & (J2K_T1_SIG | J2K_T1_VIS)) == J2K_T1_SIG){
                    int ctxno = ff_j2k_getrefctxno(t1->flags[y+1][x+1]);
                    int r = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ctxno) ? phalf : nhalf;
                    t1->data[y][x] += t1->data[y][x] < 0 ? -r : r;
                    t1->flags[y+1][x+1] |= J2K_T1_REF;
                }
            }
}

static void decode_clnpass(J2kDecoderContext *s, J2kT1Context *t1, int width, int height,
                           int bpno, int bandno, int seg_symbols)
{
    int mask = 3 << (bpno - 1), y0, x, y, runlen, dec;

    for (y0 = 0; y0 < height; y0 += 4) {
        for (x = 0; x < width; x++){
            if (y0 + 3 < height && !(
            (t1->flags[y0+1][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG)) ||
            (t1->flags[y0+2][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG)) ||
            (t1->flags[y0+3][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG)) ||
            (t1->flags[y0+4][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG)))){
                if (!ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_RL))
                    continue;
                runlen = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
                runlen = (runlen << 1) | ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
                dec = 1;
            } else{
                runlen = 0;
                dec = 0;
            }

            for (y = y0 + runlen; y < y0 + 4 && y < height; y++){
                if (!dec){
                    if (!(t1->flags[y+1][x+1] & (J2K_T1_SIG | J2K_T1_VIS)))
                        dec = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ff_j2k_getnbctxno(t1->flags[y+1][x+1],
                                                                                             bandno, 0));
                }
                if (dec){
                    int xorbit, ctxno = ff_j2k_getsgnctxno(t1->flags[y+1][x+1], &xorbit);
                    t1->data[y][x] = (ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ctxno) ^ xorbit) ? -mask : mask;
                    ff_j2k_set_significant(t1, x, y, t1->data[y][x] < 0);
                }
                dec = 0;
                t1->flags[y+1][x+1] &= ~J2K_T1_VIS;
            }
        }
    }
    if (seg_symbols) {
        int val;
        val = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        val = (val << 1) + ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        val = (val << 1) + ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        val = (val << 1) + ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        if (val != 0xa) {
            av_log(s->avctx, AV_LOG_ERROR,"Segmentation symbol value incorrect\n");
        }
    }
}

static int decode_cblk(J2kDecoderContext *s, J2kCodingStyle *codsty, J2kT1Context *t1, J2kCblk *cblk,
                       int width, int height, int bandpos)
{
    int passno = cblk->npasses, pass_t = 2, bpno = cblk->nonzerobits - 1, y, clnpass_cnt = 0;
    int bpass_csty_symbol = J2K_CBLK_BYPASS & codsty->cblk_style;
    int vert_causal_ctx_csty_symbol = J2K_CBLK_VSC & codsty->cblk_style;

    for (y = 0; y < height+2; y++)
        memset(t1->flags[y], 0, (width+2)*sizeof(int));

    for (y = 0; y < height; y++)
        memset(t1->data[y], 0, width*sizeof(int));

    cblk->data[cblk->length] = 0xff;
    cblk->data[cblk->length+1] = 0xff;
    ff_mqc_initdec(&t1->mqc, cblk->data);

    while(passno--){
        switch(pass_t){
            case 0: decode_sigpass(t1, width, height, bpno+1, bandpos,
                                  bpass_csty_symbol && (clnpass_cnt >= 4), vert_causal_ctx_csty_symbol);
                    break;
            case 1: decode_refpass(t1, width, height, bpno+1);
                    if (bpass_csty_symbol && clnpass_cnt >= 4)
                        ff_mqc_initdec(&t1->mqc, cblk->data);
                    break;
            case 2: decode_clnpass(s, t1, width, height, bpno+1, bandpos,
                                   codsty->cblk_style & J2K_CBLK_SEGSYM);
                    clnpass_cnt = clnpass_cnt + 1;
                    if (bpass_csty_symbol && clnpass_cnt >= 4)
                       ff_mqc_initdec(&t1->mqc, cblk->data);
                    break;
        }

        pass_t++;
        if (pass_t == 3){
            bpno--;
            pass_t = 0;
        }
    }
    return 0;
}

static void mct_decode(J2kDecoderContext *s, J2kTile *tile)
{
    int i, *src[3], i0, i1, i2, csize = 1;

    for (i = 0; i < 3; i++)
        src[i] = tile->comp[i].data;

    for (i = 0; i < 2; i++)
        csize *= tile->comp[0].coord[i][1] - tile->comp[0].coord[i][0];

    if (tile->codsty[0].transform == FF_DWT97){
        for (i = 0; i < csize; i++){
            i0 = *src[0] + (*src[2] * 46802 >> 16);
            i1 = *src[0] - (*src[1] * 22553 + *src[2] * 46802 >> 16);
            i2 = *src[0] + (116130 * *src[1] >> 16);
            *src[0]++ = i0;
            *src[1]++ = i1;
            *src[2]++ = i2;
        }
    } else{
        for (i = 0; i < csize; i++){
            i1 = *src[0] - (*src[2] + *src[1] >> 2);
            i0 = i1 + *src[2];
            i2 = i1 + *src[1];
            *src[0]++ = i0;
            *src[1]++ = i1;
            *src[2]++ = i2;
        }
    }
}

static int decode_tile(J2kDecoderContext *s, J2kTile *tile)
{
    int compno, reslevelno, bandno;
    int x, y, *src[4];
    uint8_t *line;
    J2kT1Context t1;

    for (compno = 0; compno < s->ncomponents; compno++){
        J2kComponent *comp = tile->comp + compno;
        J2kCodingStyle *codsty = tile->codsty + compno;

        for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
            J2kResLevel *rlevel = comp->reslevel + reslevelno;
            for (bandno = 0; bandno < rlevel->nbands; bandno++){
                J2kBand *band = rlevel->band + bandno;
                int cblkx, cblky, cblkno=0, xx0, x0, xx1, y0, yy0, yy1, bandpos;

                bandpos = bandno + (reslevelno > 0);

                yy0 = bandno == 0 ? 0 : comp->reslevel[reslevelno-1].coord[1][1] - comp->reslevel[reslevelno-1].coord[1][0];
                y0 = yy0;
                yy1 = FFMIN(ff_j2k_ceildiv(band->coord[1][0] + 1, band->codeblock_height) * band->codeblock_height,
                            band->coord[1][1]) - band->coord[1][0] + yy0;

                if (band->coord[0][0] == band->coord[0][1] || band->coord[1][0] == band->coord[1][1])
                    continue;

                for (cblky = 0; cblky < band->cblkny; cblky++){
                    if (reslevelno == 0 || bandno == 1)
                        xx0 = 0;
                    else
                        xx0 = comp->reslevel[reslevelno-1].coord[0][1] - comp->reslevel[reslevelno-1].coord[0][0];
                    x0 = xx0;
                    xx1 = FFMIN(ff_j2k_ceildiv(band->coord[0][0] + 1, band->codeblock_width) * band->codeblock_width,
                                band->coord[0][1]) - band->coord[0][0] + xx0;

                    for (cblkx = 0; cblkx < band->cblknx; cblkx++, cblkno++){
                        int y, x;
                        decode_cblk(s, codsty, &t1, band->cblk + cblkno, xx1 - xx0, yy1 - yy0, bandpos);
                        if (codsty->transform == FF_DWT53){
                            for (y = yy0; y < yy1; y+=s->cdy[compno]){
                                int *ptr = t1.data[y-yy0];
                                for (x = xx0; x < xx1; x+=s->cdx[compno]){
                                    comp->data[(comp->coord[0][1] - comp->coord[0][0]) * y + x] = *ptr++ >> 1;
                                }
                            }
                        } else{
                            for (y = yy0; y < yy1; y+=s->cdy[compno]){
                                int *ptr = t1.data[y-yy0];
                                for (x = xx0; x < xx1; x+=s->cdx[compno]){
                                    int tmp = ((int64_t)*ptr++) * ((int64_t)band->stepsize) >> 13, tmp2;
                                    tmp2 = FFABS(tmp>>1) + (tmp&1);
                                    comp->data[(comp->coord[0][1] - comp->coord[0][0]) * y + x] = tmp < 0 ? -tmp2 : tmp2;
                                }
                            }
                        }
                        xx0 = xx1;
                        xx1 = FFMIN(xx1 + band->codeblock_width, band->coord[0][1] - band->coord[0][0] + x0);
                    }
                    yy0 = yy1;
                    yy1 = FFMIN(yy1 + band->codeblock_height, band->coord[1][1] - band->coord[1][0] + y0);
                }
            }
        }
        ff_j2k_dwt_decode(&comp->dwt, comp->data);
        src[compno] = comp->data;
    }
    if (tile->codsty[0].mct)
        mct_decode(s, tile);

    if (s->precision <= 8) {
        for (compno = 0; compno < s->ncomponents; compno++){
            y = tile->comp[compno].coord[1][0] - s->image_offset_y;
            line = s->picture.data[0] + y * s->picture.linesize[0];
            for (; y < tile->comp[compno].coord[1][1] - s->image_offset_y; y += s->cdy[compno]){
                uint8_t *dst;

                x = tile->comp[compno].coord[0][0] - s->image_offset_x;
                dst = line + x * s->ncomponents + compno;

                for (; x < tile->comp[compno].coord[0][1] - s->image_offset_x; x += s->cdx[compno]) {
                    *src[compno] += 1 << (s->cbps[compno]-1);
                    if (*src[compno] < 0)
                        *src[compno] = 0;
                    else if (*src[compno] >= (1 << s->cbps[compno]))
                        *src[compno] = (1 << s->cbps[compno]) - 1;
                    *dst = *src[compno]++;
                    dst += s->ncomponents;
                }
                line += s->picture.linesize[0];
            }
        }
    } else {
        for (compno = 0; compno < s->ncomponents; compno++) {
            y = tile->comp[compno].coord[1][0] - s->image_offset_y;
            line = s->picture.data[0] + y * s->picture.linesize[0];
            for (; y < tile->comp[compno].coord[1][1] - s->image_offset_y; y += s->cdy[compno]) {
                uint16_t *dst;

                x = tile->comp[compno].coord[0][0] - s->image_offset_x;
                dst = (uint16_t *)(line + (x * s->ncomponents + compno) * 2);
                for (; x < tile->comp[compno].coord[0][1] - s->image_offset_x; x += s-> cdx[compno]) {
                    int32_t val;

                    val = *src[compno]++ << (16 - s->cbps[compno]);
                    val += 1 << 15;
                    val = av_clip(val, 0, (1 << 16) - 1);
                    *dst = val;
                    dst += s->ncomponents;
                }
                line += s->picture.linesize[0];
            }
        }
    }
    return 0;
}

static void cleanup(J2kDecoderContext *s)
{
    int tileno, compno;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        for (compno = 0; compno < s->ncomponents; compno++){
            J2kComponent *comp = s->tile[tileno].comp + compno;
            J2kCodingStyle *codsty = s->tile[tileno].codsty + compno;

            ff_j2k_cleanup(comp, codsty);
        }
        av_freep(&s->tile[tileno].comp);
    }
    av_freep(&s->tile);
}

static int decode_codestream(J2kDecoderContext *s)
{
    J2kCodingStyle *codsty = s->codsty;
    J2kQuantStyle  *qntsty = s->qntsty;
    uint8_t *properties = s->properties;

    for (;;){
        int oldpos, marker, len, ret = 0;

        if (bytestream2_get_bytes_left(&s->g) < 2){
            av_log(s->avctx, AV_LOG_ERROR, "Missing EOC\n");
            break;
        }

        marker = bytestream2_get_be16u(&s->g);
        av_dlog(s->avctx, "marker 0x%.4X at pos 0x%x\n", marker, bytestream2_tell(&s->g) - 4);
        oldpos = bytestream2_tell(&s->g);

        if (marker == J2K_SOD){
            J2kTile *tile = s->tile + s->curtileno;
            if (ret = init_tile(s, s->curtileno)) {
                av_log(s->avctx, AV_LOG_ERROR, "tile initialization failed\n");
                return ret;
            }
            if (ret = decode_packets(s, tile)) {
                av_log(s->avctx, AV_LOG_ERROR, "packets decoding failed\n");
                return ret;
            }
            continue;
        }
        if (marker == J2K_EOC)
            break;

        if (bytestream2_get_bytes_left(&s->g) < 2)
            return AVERROR(EINVAL);
        len = bytestream2_get_be16u(&s->g);
        switch (marker){
        case J2K_SIZ:
            ret = get_siz(s);
            break;
        case J2K_COC:
            ret = get_coc(s, codsty, properties);
            break;
        case J2K_COD:
            ret = get_cod(s, codsty, properties);
            break;
        case J2K_QCC:
            ret = get_qcc(s, len, qntsty, properties);
            break;
        case J2K_QCD:
            ret = get_qcd(s, len, qntsty, properties);
            break;
        case J2K_SOT:
            if (!(ret = get_sot(s))){
                codsty = s->tile[s->curtileno].codsty;
                qntsty = s->tile[s->curtileno].qntsty;
                properties = s->tile[s->curtileno].properties;
            }
            break;
        case J2K_COM:
            // the comment is ignored
            bytestream2_skip(&s->g, len - 2);
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "unsupported marker 0x%.4X at pos 0x%x\n", marker, bytestream2_tell(&s->g) - 4);
            bytestream2_skip(&s->g, len - 2);
            break;
        }
        if (bytestream2_tell(&s->g) - oldpos != len || ret){
            av_log(s->avctx, AV_LOG_ERROR, "error during processing marker segment %.4x\n", marker);
            return ret ? ret : -1;
        }
    }
    return 0;
}

static int jp2_find_codestream(J2kDecoderContext *s)
{
    uint32_t atom_size, atom;
    int found_codestream = 0, search_range = 10;

    while(!found_codestream && search_range && bytestream2_get_bytes_left(&s->g) >= 8) {
        atom_size = bytestream2_get_be32u(&s->g);
        atom      = bytestream2_get_be32u(&s->g);
        if (atom == JP2_CODESTREAM) {
            found_codestream = 1;
        } else {
            if (bytestream2_get_bytes_left(&s->g) < atom_size - 8)
                return 0;
            bytestream2_skipu(&s->g, atom_size - 8);
            search_range--;
        }
    }

    if (found_codestream)
        return 1;
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    J2kDecoderContext *s = avctx->priv_data;
    AVFrame *picture = data;
    int tileno, ret;

    bytestream2_init(&s->g, avpkt->data, avpkt->size);
    s->curtileno = -1;

    if (bytestream2_get_bytes_left(&s->g) < 2) {
        ret = AVERROR(EINVAL);
        goto err_out;
    }

    // check if the image is in jp2 format
    if (bytestream2_get_bytes_left(&s->g) >= 12 &&
       (bytestream2_get_be32u(&s->g) == 12) &&
       (bytestream2_get_be32u(&s->g) == JP2_SIG_TYPE) &&
       (bytestream2_get_be32u(&s->g) == JP2_SIG_VALUE)) {
        if(!jp2_find_codestream(s)) {
            av_log(avctx, AV_LOG_ERROR, "couldn't find jpeg2k codestream atom\n");
            ret = -1;
            goto err_out;
        }
    } else {
        bytestream2_seek(&s->g, 0, SEEK_SET);
    }

    if (bytestream2_get_be16u(&s->g) != J2K_SOC){
        av_log(avctx, AV_LOG_ERROR, "SOC marker not present\n");
        ret = -1;
        goto err_out;
    }
    if (ret = decode_codestream(s))
        goto err_out;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++)
        if (ret = decode_tile(s, s->tile + tileno))
            goto err_out;

    cleanup(s);

    *got_frame = 1;
    *picture = s->picture;

    return bytestream2_tell(&s->g);

err_out:
    cleanup(s);
    return ret;
}

static av_cold int j2kdec_init(AVCodecContext *avctx)
{
    J2kDecoderContext *s = avctx->priv_data;

    s->avctx = avctx;
    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame = (AVFrame*)&s->picture;

    ff_j2k_init_tier1_luts();

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    J2kDecoderContext *s = avctx->priv_data;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_jpeg2000_decoder = {
    .name           = "j2k",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(J2kDecoderContext),
    .init           = j2kdec_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_EXPERIMENTAL,
    .long_name      = NULL_IF_CONFIG_SMALL("JPEG 2000"),
};
