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

#include "avcodec.h"
#include "bytestream.h"
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

    const uint8_t *buf_start;
    const uint8_t *buf;
    const uint8_t *buf_end;
    int bit_index;

    int16_t curtileno;

    J2kTile *tile;
} J2kDecoderContext;

static int get_bits(J2kDecoderContext *s, int n)
{
    int res = 0;
    if (s->buf_end - s->buf < ((n - s->bit_index) >> 8))
        return AVERROR(EINVAL);
    while (--n >= 0){
        res <<= 1;
        if (s->bit_index == 0){
            s->bit_index = 7 + (*s->buf != 0xff);
            s->buf++;
        }
        s->bit_index--;
        res |= (*s->buf >> s->bit_index) & 1;
    }
    return res;
}

static void j2k_flush(J2kDecoderContext *s)
{
    if (*s->buf == 0xff)
        s->buf++;
    s->bit_index = 8;
    s->buf++;
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

    if (s->buf_end - s->buf < 36)
        return AVERROR(EINVAL);

                        bytestream_get_be16(&s->buf); // Rsiz (skipped)
             s->width = bytestream_get_be32(&s->buf); // width
            s->height = bytestream_get_be32(&s->buf); // height
    s->image_offset_x = bytestream_get_be32(&s->buf); // X0Siz
    s->image_offset_y = bytestream_get_be32(&s->buf); // Y0Siz

        s->tile_width = bytestream_get_be32(&s->buf); // XTSiz
       s->tile_height = bytestream_get_be32(&s->buf); // YTSiz
     s->tile_offset_x = bytestream_get_be32(&s->buf); // XT0Siz
     s->tile_offset_y = bytestream_get_be32(&s->buf); // YT0Siz
       s->ncomponents = bytestream_get_be16(&s->buf); // CSiz

    if (s->buf_end - s->buf < 2 * s->ncomponents)
        return AVERROR(EINVAL);

    for (i = 0; i < s->ncomponents; i++){ // Ssiz_i XRsiz_i, YRsiz_i
        uint8_t x = bytestream_get_byte(&s->buf);
        s->cbps[i] = (x & 0x7f) + 1;
        s->precision = FFMAX(s->cbps[i], s->precision);
        s->sgnd[i] = !!(x & 0x80);
        s->cdx[i] = bytestream_get_byte(&s->buf);
        s->cdy[i] = bytestream_get_byte(&s->buf);
    }

    s->numXtiles = ff_j2k_ceildiv(s->width - s->tile_offset_x, s->tile_width);
    s->numYtiles = ff_j2k_ceildiv(s->height - s->tile_offset_y, s->tile_height);

    s->tile = av_mallocz(s->numXtiles * s->numYtiles * sizeof(J2kTile));
    if (!s->tile)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->numXtiles * s->numYtiles; i++){
        J2kTile *tile = s->tile + i;

        tile->comp = av_mallocz(s->ncomponents * sizeof(J2kComponent));
        if (!tile->comp)
            return AVERROR(ENOMEM);
    }

    s->avctx->width = s->width - s->image_offset_x;
    s->avctx->height = s->height - s->image_offset_y;

    switch(s->ncomponents){
        case 1: if (s->precision > 8) {
                    s->avctx->pix_fmt    = PIX_FMT_GRAY16;
                } else s->avctx->pix_fmt = PIX_FMT_GRAY8;
                break;
        case 3: if (s->precision > 8) {
                    s->avctx->pix_fmt    = PIX_FMT_RGB48;
                } else s->avctx->pix_fmt = PIX_FMT_RGB24;
                break;
        case 4: s->avctx->pix_fmt = PIX_FMT_BGRA; break;
    }

    if (s->picture.data[0])
        s->avctx->release_buffer(s->avctx, &s->picture);

    if ((ret = s->avctx->get_buffer(s->avctx, &s->picture)) < 0)
        return ret;

    s->picture.pict_type = FF_I_TYPE;
    s->picture.key_frame = 1;

    return 0;
}

/** get common part for COD and COC segments */
static int get_cox(J2kDecoderContext *s, J2kCodingStyle *c)
{
    if (s->buf_end - s->buf < 5)
        return AVERROR(EINVAL);
          c->nreslevels = bytestream_get_byte(&s->buf) + 1; // num of resolution levels - 1
     c->log2_cblk_width = bytestream_get_byte(&s->buf) + 2; // cblk width
    c->log2_cblk_height = bytestream_get_byte(&s->buf) + 2; // cblk height

    c->cblk_style = bytestream_get_byte(&s->buf);
    if (c->cblk_style != 0){ // cblk style
        av_log(s->avctx, AV_LOG_WARNING, "extra cblk styles %X\n", c->cblk_style);
    }
    c->transform = bytestream_get_byte(&s->buf); // transformation
    if (c->csty & J2K_CSTY_PREC) {
        int i;
        for (i = 0; i < c->nreslevels; i++)
            bytestream_get_byte(&s->buf);
    }
    return 0;
}

/** get coding parameters for a particular tile or whole image*/
static int get_cod(J2kDecoderContext *s, J2kCodingStyle *c, uint8_t *properties)
{
    J2kCodingStyle tmp;
    int compno;

    if (s->buf_end - s->buf < 5)
        return AVERROR(EINVAL);

    tmp.log2_prec_width  =
    tmp.log2_prec_height = 15;

    tmp.csty = bytestream_get_byte(&s->buf);

    if (bytestream_get_byte(&s->buf)){ // progression level
        av_log(s->avctx, AV_LOG_ERROR, "only LRCP progression supported\n");
        return -1;
    }

    tmp.nlayers = bytestream_get_be16(&s->buf);
        tmp.mct = bytestream_get_byte(&s->buf); // multiple component transformation

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

    if (s->buf_end - s->buf < 2)
        return AVERROR(EINVAL);

    compno = bytestream_get_byte(&s->buf);

    c += compno;
    c->csty = bytestream_get_byte(&s->buf);
    get_cox(s, c);

    properties[compno] |= HAD_COC;
    return 0;
}

/** get common part for QCD and QCC segments */
static int get_qcx(J2kDecoderContext *s, int n, J2kQuantStyle *q)
{
    int i, x;

    if (s->buf_end - s->buf < 1)
        return AVERROR(EINVAL);

    x = bytestream_get_byte(&s->buf); // Sqcd

    q->nguardbits = x >> 5;
      q->quantsty = x & 0x1f;

    if (q->quantsty == J2K_QSTY_NONE){
        n -= 3;
        if (s->buf_end - s->buf < n)
            return AVERROR(EINVAL);
        for (i = 0; i < n; i++)
            q->expn[i] = bytestream_get_byte(&s->buf) >> 3;
    } else if (q->quantsty == J2K_QSTY_SI){
        if (s->buf_end - s->buf < 2)
            return AVERROR(EINVAL);
        x = bytestream_get_be16(&s->buf);
        q->expn[0] = x >> 11;
        q->mant[0] = x & 0x7ff;
        for (i = 1; i < 32 * 3; i++){
            int curexpn = FFMAX(0, q->expn[0] - (i-1)/3);
            q->expn[i] = curexpn;
            q->mant[i] = q->mant[0];
        }
    } else{
        n = (n - 3) >> 1;
        if (s->buf_end - s->buf < n)
            return AVERROR(EINVAL);
        for (i = 0; i < n; i++){
            x = bytestream_get_be16(&s->buf);
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

    if (s->buf_end - s->buf < 1)
        return AVERROR(EINVAL);

    compno = bytestream_get_byte(&s->buf);
    properties[compno] |= HAD_QCC;
    return get_qcx(s, n-1, q+compno);
}

/** get start of tile segment */
static uint8_t get_sot(J2kDecoderContext *s)
{
    if (s->buf_end - s->buf < 4)
        return AVERROR(EINVAL);

    s->curtileno = bytestream_get_be16(&s->buf); ///< Isot

    s->buf += 4; ///< Psot (ignored)

    if (!bytestream_get_byte(&s->buf)){ ///< TPsot
        J2kTile *tile = s->tile + s->curtileno;

        /* copy defaults */
        memcpy(tile->codsty, s->codsty, s->ncomponents * sizeof(J2kCodingStyle));
        memcpy(tile->qntsty, s->qntsty, s->ncomponents * sizeof(J2kQuantStyle));
    }
    bytestream_get_byte(&s->buf); ///< TNsot

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
        if (AV_RB16(s->buf) == J2K_EPH) {
            s->buf += 2;
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
                if (s->buf_end - s->buf < cblk->lengthinc)
                    return AVERROR(EINVAL);
                bytestream_get_buffer(&s->buf, cblk->data, cblk->lengthinc);
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
                                    tmp2 = FFABS(tmp>>1) + FFABS(tmp&1);
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

    if (s->avctx->pix_fmt == PIX_FMT_BGRA) // RGBA -> BGRA
        FFSWAP(int *, src[0], src[2]);

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
                dst = line + (x * s->ncomponents + compno) * 2;
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
        int marker, len, ret = 0;
        const uint8_t *oldbuf;
        if (s->buf_end - s->buf < 2){
            av_log(s->avctx, AV_LOG_ERROR, "Missing EOC\n");
            break;
        }

        marker = bytestream_get_be16(&s->buf);
        if(s->avctx->debug & FF_DEBUG_STARTCODE)
            av_log(s->avctx, AV_LOG_DEBUG, "marker 0x%.4X at pos 0x%x\n", marker, s->buf - s->buf_start - 4);
        oldbuf = s->buf;

        if (marker == J2K_SOD){
            J2kTile *tile = s->tile + s->curtileno;
            if (ret = init_tile(s, s->curtileno))
                return ret;
            if (ret = decode_packets(s, tile))
                return ret;
            continue;
        }
        if (marker == J2K_EOC)
            break;

        if (s->buf_end - s->buf < 2)
            return AVERROR(EINVAL);
        len = bytestream_get_be16(&s->buf);
        switch(marker){
            case J2K_SIZ:
                ret = get_siz(s); break;
            case J2K_COC:
                ret = get_coc(s, codsty, properties); break;
            case J2K_COD:
                ret = get_cod(s, codsty, properties); break;
            case J2K_QCC:
                ret = get_qcc(s, len, qntsty, properties); break;
            case J2K_QCD:
                ret = get_qcd(s, len, qntsty, properties); break;
            case J2K_SOT:
                if (!(ret = get_sot(s))){
                    codsty = s->tile[s->curtileno].codsty;
                    qntsty = s->tile[s->curtileno].qntsty;
                    properties = s->tile[s->curtileno].properties;
                }
                break;
            case J2K_COM:
                // the comment is ignored
                s->buf += len - 2; break;
            default:
                av_log(s->avctx, AV_LOG_ERROR, "unsupported marker 0x%.4X at pos 0x%tx\n", marker, s->buf - s->buf_start - 4);
                s->buf += len - 2; break;
        }
        if (s->buf - oldbuf != len || ret){
            av_log(s->avctx, AV_LOG_ERROR, "error during processing marker segment %.4x\n", marker);
            return ret ? ret : -1;
        }
    }
    return 0;
}

static int jp2_find_codestream(J2kDecoderContext *s)
{
    uint32_t atom_size;
    int found_codestream = 0, search_range = 10;

    // skip jpeg2k signature atom
    s->buf += 12;

    while(!found_codestream && search_range && s->buf_end - s->buf >= 8) {
        atom_size = AV_RB32(s->buf);
        if(AV_RB32(s->buf + 4) == JP2_CODESTREAM) {
            found_codestream = 1;
            s->buf += 8;
        } else {
            if (s->buf_end - s->buf < atom_size)
                return 0;
            s->buf += atom_size;
            search_range--;
        }
    }

    if(found_codestream)
        return 1;
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    J2kDecoderContext *s = avctx->priv_data;
    AVFrame *picture = data;
    int tileno, ret;

    s->avctx = avctx;
    av_log(s->avctx, AV_LOG_DEBUG, "start\n");

    // init
    s->buf = s->buf_start = avpkt->data;
    s->buf_end = s->buf_start + avpkt->size;
    s->curtileno = -1;

    ff_j2k_init_tier1_luts();

    if (s->buf_end - s->buf < 2)
        return AVERROR(EINVAL);

    // check if the image is in jp2 format
    if(s->buf_end - s->buf >= 12 &&
       (AV_RB32(s->buf) == 12) && (AV_RB32(s->buf + 4) == JP2_SIG_TYPE) &&
       (AV_RB32(s->buf + 8) == JP2_SIG_VALUE)) {
        if(!jp2_find_codestream(s)) {
            av_log(avctx, AV_LOG_ERROR, "couldn't find jpeg2k codestream atom\n");
            return -1;
        }
    }

    if (bytestream_get_be16(&s->buf) != J2K_SOC){
        av_log(avctx, AV_LOG_ERROR, "SOC marker not present\n");
        return -1;
    }
    if (ret = decode_codestream(s))
        return ret;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++)
        if (ret = decode_tile(s, s->tile + tileno))
            return ret;

    cleanup(s);
    av_log(s->avctx, AV_LOG_DEBUG, "end\n");

    *data_size = sizeof(AVPicture);
    *picture = s->picture;

    return s->buf - s->buf_start;
}

static av_cold int j2kdec_init(AVCodecContext *avctx)
{
    J2kDecoderContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame = (AVFrame*)&s->picture;
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
    "j2k",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_JPEG2000,
    sizeof(J2kDecoderContext),
    j2kdec_init,
    NULL,
    decode_end,
    decode_frame,
    .capabilities = CODEC_CAP_EXPERIMENTAL,
    .long_name = NULL_IF_CONFIG_SMALL("JPEG 2000"),
    .pix_fmts =
        (enum PixelFormat[]) {PIX_FMT_GRAY8, PIX_FMT_RGB24, -1}
};
