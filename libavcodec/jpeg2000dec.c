/*
 * JPEG 2000 image decoder
 * Copyright (c) 2007 Kamil Nowosad
 * Copyright (c) 2013 Nicolas Bertrand <nicoinattendu@gmail.com>
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
 * JPEG 2000 image decoder
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "thread.h"
#include "jpeg2000.h"

#define JP2_SIG_TYPE    0x6A502020
#define JP2_SIG_VALUE   0x0D0A870A
#define JP2_CODESTREAM  0x6A703263

#define HAD_COC 0x01
#define HAD_QCC 0x02

typedef struct Jpeg2000TilePart {
    uint16_t tp_idx;                    // Tile-part index
    uint8_t tile_index;                 // Tile index who refers the tile-part
    uint32_t tp_len;                    // Length of tile-part
    const uint8_t *tp_start_bstrm;      // Start address bit stream in tile-part
    const uint8_t *tp_end_bstrm;        // End address of the bit stream tile part
} Jpeg2000TilePart;

/* RMK: For JPEG2000 DCINEMA 3 tile-parts in a tile
 * one per component, so tile_part elements have a size of 3 */
typedef struct Jpeg2000Tile {
    Jpeg2000Component   *comp;
    uint8_t             properties[4];
    Jpeg2000CodingStyle codsty[4];
    Jpeg2000QuantStyle  qntsty[4];
    Jpeg2000TilePart    tile_part[3];
} Jpeg2000Tile;

typedef struct Jpeg2000DecoderContext {
    AVClass         *class;
    AVCodecContext  *avctx;

    int             width, height;
    int             image_offset_x, image_offset_y;
    int             tile_offset_x, tile_offset_y;
    uint8_t         cbps[4];    // bits per sample in particular components
    uint8_t         sgnd[4];    // if a component is signed
    uint8_t         properties[4];
    int             cdx[4], cdy[4];
    int             precision;
    int             ncomponents;
    int             tile_width, tile_height;
    int             numXtiles, numYtiles;
    int             maxtilelen;

    Jpeg2000CodingStyle codsty[4];
    Jpeg2000QuantStyle  qntsty[4];

    const uint8_t   *buf_start;
    const uint8_t   *buf;
    const uint8_t   *buf_end;
    int             bit_index;

    int16_t         curtileno;
    Jpeg2000Tile    *tile;

    /*options parameters*/
    int16_t         lowres;
    int16_t         reduction_factor;
} Jpeg2000DecoderContext;

/* get_bits functions for JPEG2000 packet bitstream
 * It is a get_bit function with a bit-stuffing routine. If the value of the
 * byte is 0xFF, the next byte includes an extra zero bit stuffed into the MSB.
 * cf. ISO-15444-1:2002 / B.10.1 Bit-stuffing routine */
static int get_bits(Jpeg2000DecoderContext *s, int n)
{
    int res = 0;
    if (s->buf_end - s->buf < ((n - s->bit_index) >> 8))
        return AVERROR(EINVAL);
    while (--n >= 0) {
        res <<= 1;
        if (s->bit_index == 0) {
            s->bit_index = 7 + (*s->buf != 0xff);
            s->buf++;
        }
        s->bit_index--;
        res |= (*s->buf >> s->bit_index) & 1;
    }
    return res;
}

static void jpeg2000_flush(Jpeg2000DecoderContext *s)
{
    if (*s->buf == 0xff)
        s->buf++;
    s->bit_index = 8;
    s->buf++;
}

/* decode the value stored in node */
static int tag_tree_decode(Jpeg2000DecoderContext *s, Jpeg2000TgtNode *node,
                           int threshold)
{
    Jpeg2000TgtNode *stack[30];
    int sp = -1, curval = 0;

    while (node && !node->vis) {
        stack[++sp] = node;
        node        = node->parent;
    }

    if (node)
        curval = node->val;
    else
        curval = stack[sp]->val;

    while (curval < threshold && sp >= 0) {
        if (curval < stack[sp]->val)
            curval = stack[sp]->val;
        while (curval < threshold) {
            int ret;
            if ((ret = get_bits(s, 1)) > 0) {
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
/* get sizes and offsets of image, tiles; number of components */
static int get_siz(Jpeg2000DecoderContext *s)
{
    int i;

    if (s->buf_end - s->buf < 36)
        return AVERROR(EINVAL);

    s->avctx->profile = bytestream_get_be16(&s->buf); // Rsiz
    s->width          = bytestream_get_be32(&s->buf); // Width
    s->height         = bytestream_get_be32(&s->buf); // Height
    s->image_offset_x = bytestream_get_be32(&s->buf); // X0Siz
    s->image_offset_y = bytestream_get_be32(&s->buf); // Y0Siz
    s->tile_width     = bytestream_get_be32(&s->buf); // XTSiz
    s->tile_height    = bytestream_get_be32(&s->buf); // YTSiz
    s->tile_offset_x  = bytestream_get_be32(&s->buf); // XT0Siz
    s->tile_offset_y  = bytestream_get_be32(&s->buf); // YT0Siz
    s->ncomponents    = bytestream_get_be16(&s->buf); // CSiz

    if (s->buf_end - s->buf < 2 * s->ncomponents)
        return AVERROR(EINVAL);

    for (i = 0; i < s->ncomponents; i++) { // Ssiz_i XRsiz_i, YRsiz_i
        uint8_t x = bytestream_get_byte(&s->buf);
        s->cbps[i]   = (x & 0x7f) + 1;
        s->precision = FFMAX(s->cbps[i], s->precision);
        s->sgnd[i]   = (x & 0x80) == 1;
        s->cdx[i]    = bytestream_get_byte(&s->buf);
        s->cdy[i]    = bytestream_get_byte(&s->buf);
    }

    s->numXtiles = ff_jpeg2000_ceildiv(s->width  - s->tile_offset_x, s->tile_width);
    s->numYtiles = ff_jpeg2000_ceildiv(s->height - s->tile_offset_y, s->tile_height);

    s->tile = av_mallocz(s->numXtiles * s->numYtiles * sizeof(*s->tile));
    if (!s->tile)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->numXtiles * s->numYtiles; i++) {
        Jpeg2000Tile *tile = s->tile + i;

        tile->comp = av_mallocz(s->ncomponents * sizeof(*tile->comp));
        if (!tile->comp)
            return AVERROR(ENOMEM);
    }

    /* compute image size with reduction factor */
    s->avctx->width  = ff_jpeg2000_ceildivpow2(s->width  - s->image_offset_x,
                                               s->reduction_factor);
    s->avctx->height = ff_jpeg2000_ceildivpow2(s->height - s->image_offset_y,
                                               s->reduction_factor);

    switch (s->avctx->profile) {
    case FF_PROFILE_JPEG2000_DCINEMA_2K:
    case FF_PROFILE_JPEG2000_DCINEMA_4K:
        /* XYZ color-space for digital cinema profiles */
        s->avctx->pix_fmt = AV_PIX_FMT_XYZ12;
        break;
    default:
        /* For other profiles selects color-space according number of
         * components and bit depth precision. */
        switch (s->ncomponents) {
        case 1:
            if (s->precision > 8)
                s->avctx->pix_fmt = AV_PIX_FMT_GRAY16;
            else
                s->avctx->pix_fmt = AV_PIX_FMT_GRAY8;
            break;
        case 3:
            if (s->precision > 8)
                s->avctx->pix_fmt = AV_PIX_FMT_RGB48;
            else
                s->avctx->pix_fmt = AV_PIX_FMT_RGB24;
            break;
        case 4:
            s->avctx->pix_fmt = AV_PIX_FMT_BGRA;
            break;
        default:
            /* pixel format can not be identified */
            s->avctx->pix_fmt = AV_PIX_FMT_NONE;
            break;
        }
        break;
    }
    return 0;
}

/* get common part for COD and COC segments */
static int get_cox(Jpeg2000DecoderContext *s, Jpeg2000CodingStyle *c)
{
    uint8_t byte;

    if (s->buf_end - s->buf < 5)
        return AVERROR(EINVAL);
    c->nreslevels = bytestream_get_byte(&s->buf) + 1; // num of resolution levels - 1

    /* compute number of resolution levels to decode */
    if (c->nreslevels < s->reduction_factor)
        c->nreslevels2decode = 1;
    else
        c->nreslevels2decode = c->nreslevels - s->reduction_factor;

    c->log2_cblk_width  = bytestream_get_byte(&s->buf) + 2; // cblk width
    c->log2_cblk_height = bytestream_get_byte(&s->buf) + 2; // cblk height

    c->cblk_style = bytestream_get_byte(&s->buf);
    if (c->cblk_style != 0) { // cblk style
        av_log(s->avctx, AV_LOG_ERROR, "no extra cblk styles supported\n");
        return -1;
    }
    c->transform = bytestream_get_byte(&s->buf); // DWT transformation type
    /* set integer 9/7 DWT in case of BITEXACT flag */
    if ((s->avctx->flags & CODEC_FLAG_BITEXACT) && (c->transform == FF_DWT97))
        c->transform = FF_DWT97_INT;

    if (c->csty & JPEG2000_CSTY_PREC) {
        int i;
        for (i = 0; i < c->nreslevels; i++) {
            byte = bytestream_get_byte(&s->buf);
            c->log2_prec_widths[i]  =  byte       & 0x0F;    // precinct PPx
            c->log2_prec_heights[i] = (byte >> 4) & 0x0F;    // precinct PPy
        }
    }
    return 0;
}

/* get coding parameters for a particular tile or whole image*/
static int get_cod(Jpeg2000DecoderContext *s, Jpeg2000CodingStyle *c,
                   uint8_t *properties)
{
    Jpeg2000CodingStyle tmp;
    int compno;

    if (s->buf_end - s->buf < 5)
        return AVERROR(EINVAL);

    tmp.log2_prec_width  =
    tmp.log2_prec_height = 15;

    tmp.csty = bytestream_get_byte(&s->buf);

    // get progression order
    tmp.prog_order = bytestream_get_byte(&s->buf);

    tmp.nlayers = bytestream_get_be16(&s->buf);
    tmp.mct     = bytestream_get_byte(&s->buf); // multiple component transformation

    get_cox(s, &tmp);
    for (compno = 0; compno < s->ncomponents; compno++)
        if (!(properties[compno] & HAD_COC))
            memcpy(c + compno, &tmp, sizeof(tmp));
    return 0;
}

/* Get coding parameters for a component in the whole image or a
 * particular tile. */
static int get_coc(Jpeg2000DecoderContext *s, Jpeg2000CodingStyle *c,
                   uint8_t *properties)
{
    int compno;

    if (s->buf_end - s->buf < 2)
        return AVERROR(EINVAL);

    compno = bytestream_get_byte(&s->buf);

    c      += compno;
    c->csty = bytestream_get_byte(&s->buf);
    get_cox(s, c);

    properties[compno] |= HAD_COC;
    return 0;
}

/* Get common part for QCD and QCC segments. */
static int get_qcx(Jpeg2000DecoderContext *s, int n, Jpeg2000QuantStyle *q)
{
    int i, x;

    if (s->buf_end - s->buf < 1)
        return AVERROR(EINVAL);

    x = bytestream_get_byte(&s->buf); // Sqcd

    q->nguardbits = x >> 5;
    q->quantsty   = x & 0x1f;

    if (q->quantsty == JPEG2000_QSTY_NONE) {
        n -= 3;
        if (s->buf_end - s->buf < n)
            return AVERROR(EINVAL);
        for (i = 0; i < n; i++)
            q->expn[i] = bytestream_get_byte(&s->buf) >> 3;
    } else if (q->quantsty == JPEG2000_QSTY_SI) {
        if (s->buf_end - s->buf < 2)
            return AVERROR(EINVAL);
        x          = bytestream_get_be16(&s->buf);
        q->expn[0] = x >> 11;
        q->mant[0] = x & 0x7ff;
        for (i = 1; i < 32 * 3; i++) {
            int curexpn = FFMAX(0, q->expn[0] - (i - 1) / 3);
            q->expn[i] = curexpn;
            q->mant[i] = q->mant[0];
        }
    } else {
        n = (n - 3) >> 1;
        if (s->buf_end - s->buf < n)
            return AVERROR(EINVAL);
        for (i = 0; i < n; i++) {
            x          = bytestream_get_be16(&s->buf);
            q->expn[i] = x >> 11;
            q->mant[i] = x & 0x7ff;
        }
    }
    return 0;
}

/* Get quantization parameters for a particular tile or a whole image. */
static int get_qcd(Jpeg2000DecoderContext *s, int n, Jpeg2000QuantStyle *q,
                   uint8_t *properties)
{
    Jpeg2000QuantStyle tmp;
    int compno;

    if (get_qcx(s, n, &tmp))
        return -1;
    for (compno = 0; compno < s->ncomponents; compno++)
        if (!(properties[compno] & HAD_QCC))
            memcpy(q + compno, &tmp, sizeof(tmp));
    return 0;
}

/* Get quantization parameters for a component in the whole image
 * on in a particular tile. */
static int get_qcc(Jpeg2000DecoderContext *s, int n, Jpeg2000QuantStyle *q,
                   uint8_t *properties)
{
    int compno;

    if (s->buf_end - s->buf < 1)
        return AVERROR(EINVAL);

    compno              = bytestream_get_byte(&s->buf);
    properties[compno] |= HAD_QCC;
    return get_qcx(s, n - 1, q + compno);
}

/* Get start of tile segment. */
static uint8_t get_sot(Jpeg2000DecoderContext *s, int n)
{
    Jpeg2000TilePart *tp;
    uint16_t Isot;
    uint32_t Psot;
    uint8_t TPsot;

    if (s->buf_end - s->buf < 4)
        return AVERROR(EINVAL);

    Isot = bytestream_get_be16(&s->buf);        // Isot
    if (Isot) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Not a DCINEMA JP2K file: more than one tile\n");
        return -1;
    }
    Psot  = bytestream_get_be32(&s->buf);       // Psot
    TPsot = bytestream_get_byte(&s->buf);       // TPsot

    /* Read TNSot but not used */
    bytestream_get_byte(&s->buf);               // TNsot

    tp             = s->tile[s->curtileno].tile_part + TPsot;
    tp->tile_index = Isot;
    tp->tp_len     = Psot;
    tp->tp_idx     = TPsot;

    /* Start of bit stream. Pointer to SOD marker
     * Check SOD marker is present. */
    if (JPEG2000_SOD == bytestream_get_be16(&s->buf))
        tp->tp_start_bstrm = s->buf;
    else {
        av_log(s->avctx, AV_LOG_ERROR, "SOD marker not found \n");
        return -1;
    }

    /* End address of bit stream =
     *     start address + (Psot - size of SOT HEADER(n)
     *     - size of SOT MARKER(2)  - size of SOD marker(2) */
    tp->tp_end_bstrm = s->buf + (tp->tp_len - n - 4);

    // set buffer pointer to end of tile part header
    s->buf = tp->tp_end_bstrm;

    return 0;
}

/* Tile-part lengths: see ISO 15444-1:2002, section A.7.1
 * Used to know the number of tile parts and lengths.
 * There may be multiple TLMs in the header.
 * TODO: The function is not used for tile-parts management, nor anywhere else.
 * It can be useful to allocate memory for tile parts, before managing the SOT
 * markers. Parsing the TLM header is needed to increment the input header
 * buffer.
 * This marker is mandatory for DCI. */
static uint8_t get_tlm(Jpeg2000DecoderContext *s, int n)
{
    uint8_t Stlm, ST, SP, tile_tlm, i;
    bytestream_get_byte(&s->buf);               /* Ztlm: skipped */
    Stlm = bytestream_get_byte(&s->buf);

    // too complex ? ST = ((Stlm >> 4) & 0x01) + ((Stlm >> 4) & 0x02);
    ST = (Stlm >> 4) & 0x03;
    // TODO: Manage case of ST = 0b11 --> raise error
    SP       = (Stlm >> 6) & 0x01;
    tile_tlm = (n - 4) / ((SP + 1) * 2 + ST);
    for (i = 0; i < tile_tlm; i++) {
        switch (ST) {
        case 0:
            break;
        case 1:
            bytestream_get_byte(&s->buf);
            break;
        case 2:
            bytestream_get_be16(&s->buf);
            break;
        case 3:
            bytestream_get_be32(&s->buf);
            break;
        }
        if (SP == 0) {
            bytestream_get_be16(&s->buf);
        } else {
            bytestream_get_be32(&s->buf);
        }
    }
    return 0;
}

static int init_tile(Jpeg2000DecoderContext *s, int tileno)
{
    int compno;
    int tilex = tileno % s->numXtiles;
    int tiley = tileno / s->numXtiles;
    Jpeg2000Tile *tile = s->tile + tileno;
    Jpeg2000CodingStyle *codsty;
    Jpeg2000QuantStyle  *qntsty;

    if (!tile->comp)
        return AVERROR(ENOMEM);

    /* copy codsty, qnsty to tile. TODO: Is it the best way?
     * codsty, qnsty is an array of 4 structs Jpeg2000CodingStyle
     * and Jpeg2000QuantStyle */
    memcpy(tile->codsty, s->codsty, s->ncomponents * sizeof(*codsty));
    memcpy(tile->qntsty, s->qntsty, s->ncomponents * sizeof(*qntsty));

    for (compno = 0; compno < s->ncomponents; compno++) {
        Jpeg2000Component *comp = tile->comp + compno;
        int ret; // global bandno
        codsty = tile->codsty + compno;
        qntsty = tile->qntsty + compno;

        comp->coord_o[0][0] = FFMAX(tilex       * s->tile_width  + s->tile_offset_x, s->image_offset_x);
        comp->coord_o[0][1] = FFMIN((tilex + 1) * s->tile_width  + s->tile_offset_x, s->width);
        comp->coord_o[1][0] = FFMAX(tiley       * s->tile_height + s->tile_offset_y, s->image_offset_y);
        comp->coord_o[1][1] = FFMIN((tiley + 1) * s->tile_height + s->tile_offset_y, s->height);

        // FIXME: add a dcinema profile check ?
        // value is guaranteed by profile (orig=0, 1 tile)
        comp->coord[0][0] = 0;
        comp->coord[0][1] = s->avctx->width;
        comp->coord[1][0] = 0;
        comp->coord[1][1] = s->avctx->height;

        if (ret = ff_jpeg2000_init_component(comp, codsty, qntsty,
                                             s->cbps[compno], s->cdx[compno],
                                             s->cdy[compno], s->avctx))
            return ret;
    }
    return 0;
}

/* Read the number of coding passes. */
static int getnpasses(Jpeg2000DecoderContext *s)
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

static int getlblockinc(Jpeg2000DecoderContext *s)
{
    int res = 0, ret;
    while (ret = get_bits(s, 1)) {
        if (ret < 0)
            return ret;
        res++;
    }
    return res;
}

static int jpeg2000_decode_packet(Jpeg2000DecoderContext *s,
                                  Jpeg2000CodingStyle *codsty,
                                  Jpeg2000ResLevel *rlevel, int precno,
                                  int layno, uint8_t *expn, int numgbits)
{
    int bandno, cblkno, ret, nb_code_blocks;

    if (!(ret = get_bits(s, 1))) {
        jpeg2000_flush(s);
        return 0;
    } else if (ret < 0)
        return ret;

    for (bandno = 0; bandno < rlevel->nbands; bandno++) {
        Jpeg2000Band *band = rlevel->band + bandno;
        Jpeg2000Prec *prec = band->prec + precno;

        if (band->coord[0][0] == band->coord[0][1] ||
            band->coord[1][0] == band->coord[1][1])
            continue;
        prec->yi0 = 0;
        prec->xi0 = 0;
        nb_code_blocks =  prec->nb_codeblocks_height *
                          prec->nb_codeblocks_width;
        for (cblkno = 0; cblkno < nb_code_blocks; cblkno++) {
            Jpeg2000Cblk *cblk = prec->cblk + cblkno;
            int incl, newpasses, llen;

            if (cblk->npasses)
                incl = get_bits(s, 1);
            else
                incl = tag_tree_decode(s, prec->cblkincl + cblkno, layno + 1) == layno;
            if (!incl)
                continue;
            else if (incl < 0)
                return incl;

            if (!cblk->npasses)
                cblk->nonzerobits = expn[bandno] + numgbits - 1 -
                                    tag_tree_decode(s, prec->zerobits + cblkno,
                                                    100);
            if ((newpasses = getnpasses(s)) < 0)
                return newpasses;
            if ((llen = getlblockinc(s)) < 0)
                return llen;
            cblk->lblock += llen;
            if ((ret = get_bits(s, av_log2(newpasses) + cblk->lblock)) < 0)
                return ret;
            cblk->lengthinc = ret;
            cblk->npasses  += newpasses;
        }
    }
    jpeg2000_flush(s);

    if (codsty->csty & JPEG2000_CSTY_EPH) {
        if (AV_RB16(s->buf) == JPEG2000_EPH)
            s->buf += 2;
        else
            av_log(s->avctx, AV_LOG_ERROR, "EPH marker not found.\n");
    }

    for (bandno = 0; bandno < rlevel->nbands; bandno++) {
        Jpeg2000Band *band = rlevel->band + bandno;
        Jpeg2000Prec *prec = band->prec + precno;

        nb_code_blocks = prec->nb_codeblocks_height * prec->nb_codeblocks_width;
        for (cblkno = 0; cblkno < nb_code_blocks; cblkno++) {
            Jpeg2000Cblk *cblk = prec->cblk + cblkno;
            if (s->buf_end - s->buf < cblk->lengthinc)
                return AVERROR(EINVAL);
            /* Code-block data can be empty. In that case initialize data
             * with 0xFFFF. */
            if (cblk->lengthinc > 0) {
                bytestream_get_buffer(&s->buf, cblk->data, cblk->lengthinc);
            } else {
                cblk->data[0] = 0xFF;
                cblk->data[1] = 0xFF;
            }
            cblk->length   += cblk->lengthinc;
            cblk->lengthinc = 0;
        }
    }
    return 0;
}

static int jpeg2000_decode_packets(Jpeg2000DecoderContext *s, Jpeg2000Tile *tile)
{
    int layno, reslevelno, compno, precno, ok_reslevel;
    uint8_t prog_order = tile->codsty[0].prog_order;
    uint16_t x;
    uint16_t y;

    s->bit_index = 8;
    switch (prog_order) {
    case JPEG2000_PGOD_LRCP:
        for (layno = 0; layno < tile->codsty[0].nlayers; layno++) {
            ok_reslevel = 1;
            for (reslevelno = 0; ok_reslevel; reslevelno++) {
                ok_reslevel = 0;
                for (compno = 0; compno < s->ncomponents; compno++) {
                    Jpeg2000CodingStyle *codsty = tile->codsty + compno;
                    Jpeg2000QuantStyle *qntsty  = tile->qntsty + compno;
                    if (reslevelno < codsty->nreslevels) {
                        Jpeg2000ResLevel *rlevel = tile->comp[compno].reslevel +
                                                   reslevelno;
                        ok_reslevel = 1;
                        for (precno = 0; precno < rlevel->num_precincts_x * rlevel->num_precincts_y; precno++)
                            if (jpeg2000_decode_packet(s,
                                                       codsty, rlevel,
                                                       precno, layno,
                                                       qntsty->expn + (reslevelno ? 3 * (reslevelno - 1) + 1 : 0),
                                                       qntsty->nguardbits))
                                return -1;
                    }
                }
            }
        }
        break;

    case JPEG2000_PGOD_CPRL:
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000CodingStyle *codsty = tile->codsty + compno;
            Jpeg2000QuantStyle *qntsty  = tile->qntsty + compno;

            /* Set bit stream buffer address according to tile-part.
             * For DCinema one tile-part per component, so can be
             * indexed by component. */
            s->buf = tile->tile_part[compno].tp_start_bstrm;

            /* Position loop (y axis)
             * TODO: Automate computing of step 256.
             * Fixed here, but to be computed before entering here. */
            for (y = 0; y < s->height; y += 256) {
                /* Position loop (y axis)
                 * TODO: automate computing of step 256.
                 * Fixed here, but to be computed before entering here. */
                for (x = 0; x < s->width; x += 256) {
                    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++) {
                        uint16_t prcx, prcy;
                        uint8_t reducedresno = codsty->nreslevels - 1 -reslevelno; //  ==> N_L - r
                        Jpeg2000ResLevel *rlevel = tile->comp[compno].reslevel + reslevelno;

                        if (!((y % (1 << (rlevel->log2_prec_height + reducedresno)) == 0) ||
                              (y == 0))) // TODO: 2nd condition simplified as try0 always =0 for dcinema
                            continue;

                        if (!((x % (1 << (rlevel->log2_prec_width + reducedresno)) == 0) ||
                              (x == 0))) // TODO: 2nd condition simplified as try0 always =0 for dcinema
                            continue;

                        // check if a precinct exists
                        prcx   = ff_jpeg2000_ceildivpow2(x, reducedresno) >> rlevel->log2_prec_width;
                        prcy   = ff_jpeg2000_ceildivpow2(y, reducedresno) >> rlevel->log2_prec_height;
                        precno = prcx + rlevel->num_precincts_x * prcy;
                        for (layno = 0; layno < tile->codsty[0].nlayers; layno++) {
                            if (jpeg2000_decode_packet(s, codsty, rlevel,
                                                       precno, layno,
                                                       qntsty->expn + (reslevelno ? 3 * (reslevelno - 1) + 1 : 0),
                                                       qntsty->nguardbits))
                                return -1;
                        }
                    }
                }
            }
        }
        break;

    default:
        break;
    }

    /* EOC marker reached */
    s->buf += 2;

    return 0;
}

/* TIER-1 routines */
static void decode_sigpass(Jpeg2000T1Context *t1, int width, int height,
                           int bpno, int bandno)
{
    int mask = 3 << (bpno - 1), y0, x, y;

    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0 + 4; y++)
                if ((t1->flags[y + 1][x + 1] & JPEG2000_T1_SIG_NB)
                    && !(t1->flags[y + 1][x + 1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS))) {
                    if (ff_mqc_decode(&t1->mqc,
                                      t1->mqc.cx_states +
                                      ff_jpeg2000_getsigctxno(t1->flags[y + 1][x + 1],
                                                             bandno))) {
                        int xorbit, ctxno = ff_jpeg2000_getsgnctxno(t1->flags[y + 1][x + 1],
                                                                    &xorbit);

                        t1->data[y][x] =
                            (ff_mqc_decode(&t1->mqc,
                                           t1->mqc.cx_states + ctxno) ^ xorbit)
                            ? -mask : mask;

                        ff_jpeg2000_set_significance(t1, x, y,
                                                     t1->data[y][x] < 0);
                    }
                    t1->flags[y + 1][x + 1] |= JPEG2000_T1_VIS;
                }
}

static void decode_refpass(Jpeg2000T1Context *t1, int width, int height,
                           int bpno)
{
    int phalf, nhalf;
    int y0, x, y;

    phalf = 1 << (bpno - 1);
    nhalf = -phalf;

    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0 + 4; y++)
                if ((t1->flags[y + 1][x + 1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS)) == JPEG2000_T1_SIG) {
                    int ctxno = ff_jpeg2000_getrefctxno(t1->flags[y + 1][x + 1]);
                    int r     = ff_mqc_decode(&t1->mqc,
                                              t1->mqc.cx_states + ctxno)
                                ? phalf : nhalf;
                    t1->data[y][x]          += t1->data[y][x] < 0 ? -r : r;
                    t1->flags[y + 1][x + 1] |= JPEG2000_T1_REF;
                }
}

static void decode_clnpass(Jpeg2000DecoderContext *s, Jpeg2000T1Context *t1,
                           int width, int height, int bpno, int bandno,
                           int seg_symbols)
{
    int mask = 3 << (bpno - 1), y0, x, y, runlen, dec;

    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++) {
            if (y0 + 3 < height &&
                !((t1->flags[y0 + 1][x + 1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG)) ||
                  (t1->flags[y0 + 2][x + 1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG)) ||
                  (t1->flags[y0 + 3][x + 1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG)) ||
                  (t1->flags[y0 + 4][x + 1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG)))) {
                if (!ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_RL))
                    continue;
                runlen = ff_mqc_decode(&t1->mqc,
                                       t1->mqc.cx_states + MQC_CX_UNI);
                runlen = (runlen << 1) | ff_mqc_decode(&t1->mqc,
                                                       t1->mqc.cx_states +
                                                       MQC_CX_UNI);
                dec = 1;
            } else {
                runlen = 0;
                dec    = 0;
            }

            for (y = y0 + runlen; y < y0 + 4 && y < height; y++) {
                if (!dec) {
                    if (!(t1->flags[y + 1][x + 1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS)))
                        dec = ff_mqc_decode(&t1->mqc,
                                            t1->mqc.cx_states +
                                            ff_jpeg2000_getsigctxno(t1->flags[y + 1][x + 1],
                                                                   bandno));
                }
                if (dec) {
                    int xorbit;
                    int ctxno = ff_jpeg2000_getsgnctxno(t1->flags[y + 1][x + 1],
                                                        &xorbit);
                    t1->data[y][x] = (ff_mqc_decode(&t1->mqc,
                                                    t1->mqc.cx_states + ctxno) ^
                                      xorbit)
                                     ? -mask : mask;
                    ff_jpeg2000_set_significance(t1, x, y, t1->data[y][x] < 0);
                }
                dec = 0;
                t1->flags[y + 1][x + 1] &= ~JPEG2000_T1_VIS;
            }
        }
    if (seg_symbols) {
        int val;
        val = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        val = (val << 1) + ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        val = (val << 1) + ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        val = (val << 1) + ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI);
        if (val != 0xa)
            av_log(s->avctx, AV_LOG_ERROR,
                   "Segmentation symbol value incorrect\n");
    }
}

static int decode_cblk(Jpeg2000DecoderContext *s, Jpeg2000CodingStyle *codsty,
                       Jpeg2000T1Context *t1, Jpeg2000Cblk *cblk,
                       int width, int height, int bandpos)
{
    int passno = cblk->npasses, pass_t = 2, bpno = cblk->nonzerobits - 1, y;

    for (y = 0; y < height; y++)
        memset(t1->data[y], 0, width * sizeof(width));

    /* If code-block contains no compressed data: nothing to do. */
    if (!cblk->length)
        return 0;
    for (y = 0; y < height + 2; y++)
        memset(t1->flags[y], 0, (width + 2) * sizeof(width));

    ff_mqc_initdec(&t1->mqc, cblk->data);
    cblk->data[cblk->length]     = 0xff;
    cblk->data[cblk->length + 1] = 0xff;

    while (passno--) {
        switch (pass_t) {
        case 0:
            decode_sigpass(t1, width, height, bpno + 1, bandpos);
            break;
        case 1:
            decode_refpass(t1, width, height, bpno + 1);
            break;
        case 2:
            decode_clnpass(s, t1, width, height, bpno + 1, bandpos,
                           codsty->cblk_style & JPEG2000_CBLK_SEGSYM);
            break;
        }

        pass_t++;
        if (pass_t == 3) {
            bpno--;
            pass_t = 0;
        }
    }
    return 0;
}

/* TODO: Verify dequantization for lossless case
 * comp->data can be float or int
 * band->stepsize can be float or int
 * depending on the type of DWT transformation.
 * see ISO/IEC 15444-1:2002 A.6.1 */

/* Float dequantization of a codeblock.*/
static void dequantization_float(int x, int y, Jpeg2000Cblk *cblk,
                                 Jpeg2000Component *comp,
                                 Jpeg2000T1Context *t1, Jpeg2000Band *band)
{
    int i, j, idx;
    float *datap = &comp->data[(comp->coord[0][1] - comp->coord[0][0]) * y + x];
    for (j = 0; j < (cblk->coord[1][1] - cblk->coord[1][0]); ++j)
        for (i = 0; i < (cblk->coord[0][1] - cblk->coord[0][0]); ++i) {
            idx        = (comp->coord[0][1] - comp->coord[0][0]) * j + i;
            datap[idx] = (float)(t1->data[j][i]) * ((float)band->stepsize);
        }
    return;
}

/* Integer dequantization of a codeblock.*/
static void dequantization_int(int x, int y, Jpeg2000Cblk *cblk,
                               Jpeg2000Component *comp,
                               Jpeg2000T1Context *t1, Jpeg2000Band *band)
{
    int i, j, idx;
    int32_t *datap =
        (int32_t *) &comp->data[(comp->coord[0][1] - comp->coord[0][0]) * y + x];
    for (j = 0; j < (cblk->coord[1][1] - cblk->coord[1][0]); ++j)
        for (i = 0; i < (cblk->coord[0][1] - cblk->coord[0][0]); ++i) {
            idx        = (comp->coord[0][1] - comp->coord[0][0]) * j + i;
            datap[idx] =
                ((int32_t)(t1->data[j][i]) * ((int32_t)band->stepsize) + (1 << 15)) >> 16;
        }
    return;
}

/* Inverse ICT parameters in float and integer.
 * int value = (float value) * (1<<16) */
static const float f_ict_params[4] = {
    1.402f,
    0.34413f,
    0.71414f,
    1.772f
};
static const int   i_ict_params[4] = {
     91881,
     22553,
     46802,
    116130
};

static int mct_decode(Jpeg2000DecoderContext *s, Jpeg2000Tile *tile)
{
    int i, csize = 1;
    int ret = 0;
    int32_t *src[3],  i0,  i1,  i2;
    float   *srcf[3], i0f, i1f, i2f;

    for (i = 0; i < 3; i++)
        if (tile->codsty[0].transform == FF_DWT97)
            srcf[i] = tile->comp[i].data;
        else
            src[i] = (int32_t *)tile->comp[i].data;

    for (i = 0; i < 2; i++)
        csize *= tile->comp[0].coord[i][1] - tile->comp[0].coord[i][0];
    switch (tile->codsty[0].transform) {
    case FF_DWT97:
        for (i = 0; i < csize; i++) {
            i0f = *srcf[0] + (f_ict_params[0] * *srcf[2]);
            i1f = *srcf[0] - (f_ict_params[1] * *srcf[1])
                           - (f_ict_params[2] * *srcf[2]);
            i2f = *srcf[0] + (f_ict_params[3] * *srcf[1]);
            *srcf[0]++ = i0f;
            *srcf[1]++ = i1f;
            *srcf[2]++ = i2f;
        }
        break;
    case FF_DWT97_INT:
        for (i = 0; i < csize; i++) {
            i0 = *src[0] + (((i_ict_params[0] * *src[2]) + (1 << 15)) >> 16);
            i1 = *src[0] - (((i_ict_params[1] * *src[1]) + (1 << 15)) >> 16)
                         - (((i_ict_params[2] * *src[2]) + (1 << 15)) >> 16);
            i2 = *src[0] + (((i_ict_params[3] * *src[1]) + (1 << 15)) >> 16);
            *src[0]++ = i0;
            *src[1]++ = i1;
            *src[2]++ = i2;
        }
        break;
    case FF_DWT53:
        for (i = 0; i < csize; i++) {
            i1 = *src[0] - (*src[2] + *src[1] >> 2);
            i0 = i1 + *src[2];
            i2 = i1 + *src[1];
            *src[0]++ = i0;
            *src[1]++ = i1;
            *src[2]++ = i2;
        }
        break;
    }
    return ret;
}

static int jpeg2000_decode_tile(Jpeg2000DecoderContext *s, Jpeg2000Tile *tile,
                                AVFrame *picture)
{
    int compno, reslevelno, bandno;
    int x, y;

    uint8_t *line;
    Jpeg2000T1Context t1;
    /* Loop on tile components */

    for (compno = 0; compno < s->ncomponents; compno++) {
        Jpeg2000Component *comp     = tile->comp + compno;
        Jpeg2000CodingStyle *codsty = tile->codsty + compno;
        /* Loop on resolution levels */
        for (reslevelno = 0; reslevelno < codsty->nreslevels2decode; reslevelno++) {
            Jpeg2000ResLevel *rlevel = comp->reslevel + reslevelno;
            /* Loop on bands */
            for (bandno = 0; bandno < rlevel->nbands; bandno++) {
                uint16_t nb_precincts, precno;
                Jpeg2000Band *band = rlevel->band + bandno;
                int cblkno = 0, bandpos;
                bandpos = bandno + (reslevelno > 0);

                nb_precincts = rlevel->num_precincts_x * rlevel->num_precincts_y;
                /* Loop on precincts */
                for (precno = 0; precno < nb_precincts; precno++) {
                    Jpeg2000Prec *prec = band->prec + precno;

                    /* Loop on codeblocks */
                    for (cblkno = 0; cblkno < prec->nb_codeblocks_width * prec->nb_codeblocks_height; cblkno++) {
                        int x, y;
                        Jpeg2000Cblk *cblk = prec->cblk + cblkno;
                        decode_cblk(s, codsty, &t1, cblk,
                                    cblk->coord[0][1] - cblk->coord[0][0],
                                    cblk->coord[1][1] - cblk->coord[1][0],
                                    bandpos);

                        /* Manage band offsets */
                        x = cblk->coord[0][0];
                        y = cblk->coord[1][0];
                        if ((reslevelno > 0) && ((bandno + 1) & 1)) {
                            Jpeg2000ResLevel *pres = comp->reslevel + (reslevelno - 1);
                            x += pres->coord[0][1] - pres->coord[0][0];
                        }
                        if ((reslevelno > 0) && ((bandno + 1) & 2)) {
                            Jpeg2000ResLevel *pres = comp->reslevel + (reslevelno - 1);
                            y += pres->coord[1][1] - pres->coord[1][0];
                        }

                        if (s->avctx->flags & CODEC_FLAG_BITEXACT)
                            dequantization_int(x, y, cblk, comp, &t1, band);
                        else
                            dequantization_float(x, y, cblk, comp, &t1, band);
                   } /* end cblk */
                } /*end prec */
            } /* end band */
        } /* end reslevel */

        /* inverse DWT */
        ff_dwt_decode(&comp->dwt, comp->data);
    } /*end comp */

    /* inverse MCT transformation */
    if (tile->codsty[0].mct)
        mct_decode(s, tile);

    if (s->avctx->pix_fmt == PIX_FMT_BGRA) // RGBA -> BGRA
        FFSWAP(float *, tile->comp[0].data, tile->comp[2].data);

    if (s->precision <= 8) {
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp = tile->comp + compno;
            int32_t *datap = (int32_t *)comp->data;
            y    = tile->comp[compno].coord[1][0] - s->image_offset_y;
            line = picture->data[0] + y * picture->linesize[0];
            for (; y < tile->comp[compno].coord[1][1] - s->image_offset_y; y += s->cdy[compno]) {
                uint8_t *dst;

                x   = tile->comp[compno].coord[0][0] - s->image_offset_x;
                dst = line + x * s->ncomponents + compno;

                for (; x < tile->comp[compno].coord[0][1] - s->image_offset_x; x += s->cdx[compno]) {
                    *datap += 1 << (s->cbps[compno] - 1);
                    if (*datap < 0)
                        *datap = 0;
                    else if (*datap >= (1 << s->cbps[compno]))
                        *datap = (1 << s->cbps[compno]) - 1;
                    *dst = *datap++;
                    dst += s->ncomponents;
                }
                line += picture->linesize[0];
            }
        }
    } else {
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp = tile->comp + compno;
            float *datap = comp->data;
            int32_t *i_datap = (int32_t *) comp->data;
            uint16_t *linel;

            y     = tile->comp[compno].coord[1][0] - s->image_offset_y;
            linel = (uint16_t *)picture->data[0] + y * (picture->linesize[0] >> 1);
            for (; y < tile->comp[compno].coord[1][1] - s->image_offset_y; y += s->cdy[compno]) {
                uint16_t *dst;
                x   = tile->comp[compno].coord[0][0] - s->image_offset_x;
                dst = linel + (x * s->ncomponents + compno);
                for (; x < s->avctx->width; x += s->cdx[compno]) {
                    int16_t val;
                    /* DC level shift and clip see ISO 15444-1:2002 G.1.2 */
                    if (s->avctx->flags & CODEC_FLAG_BITEXACT)
                        val = *i_datap + (1 << (s->cbps[compno] - 1));
                    else
                        val = lrintf(*datap) + (1 << (s->cbps[compno] - 1));
                    val = av_clip(val, 0, (1 << s->cbps[compno]) - 1);
                    /* align 12 bit values in little-endian mode */
                    *dst = val << 4;
                    datap++;
                    i_datap++;
                    dst += s->ncomponents;
                }
                linel += picture->linesize[0] >> 1;
            }
        }
    }
    return 0;
}

static void jpeg2000_dec_cleanup(Jpeg2000DecoderContext *s)
{
    int tileno, compno;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++) {
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp     = s->tile[tileno].comp   + compno;
            Jpeg2000CodingStyle *codsty = s->tile[tileno].codsty + compno;

            ff_jpeg2000_cleanup(comp, codsty);
        }
        av_freep(&s->tile[tileno].comp);
    }
    av_freep(&s->tile);
}

static int jpeg2000_read_main_headers(Jpeg2000DecoderContext *s)
{
    Jpeg2000CodingStyle *codsty = s->codsty;
    Jpeg2000QuantStyle *qntsty  = s->qntsty;
    uint8_t *properties         = s->properties;

    for (;;) {
        int len, ret = 0;
        uint16_t marker;
        const uint8_t *oldbuf;

        if (s->buf_end - s->buf < 2) {
            av_log(s->avctx, AV_LOG_ERROR, "Missing EOC\n");
            break;
        }

        marker = bytestream_get_be16(&s->buf);
        oldbuf = s->buf;

        if (marker == JPEG2000_EOC)
            break;

        if (s->buf_end - s->buf < 2)
            return AVERROR(EINVAL);
        len = bytestream_get_be16(&s->buf);
        switch (marker) {
        case JPEG2000_SIZ:
            ret = get_siz(s);
            break;
        case JPEG2000_COC:
            ret = get_coc(s, codsty, properties);
            break;
        case JPEG2000_COD:
            ret = get_cod(s, codsty, properties);
            break;
        case JPEG2000_QCC:
            ret = get_qcc(s, len, qntsty, properties);
            break;
        case JPEG2000_QCD:
            ret = get_qcd(s, len, qntsty, properties);
            break;
        case JPEG2000_SOT:
            ret = get_sot(s, len);
            break;
        case JPEG2000_COM:
            // the comment is ignored
            s->buf += len - 2;
            break;
        case JPEG2000_TLM:
            // Tile-part lengths
            ret = get_tlm(s, len);
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR,
                   "unsupported marker 0x%.4X at pos 0x%lX\n",
                   marker, (uint64_t)(s->buf - s->buf_start - 4));
            s->buf += len - 2;
            break;
        }
        if (((s->buf - oldbuf != len) && (marker != JPEG2000_SOT)) || ret) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "error during processing marker segment %.4x\n", marker);
            return ret ? ret : -1;
        }
    }
    return 0;
}

/* Read bit stream packets --> T2 operation. */
static int jpeg2000_read_bitstream_packets(Jpeg2000DecoderContext *s)
{
    int ret = 0;
    Jpeg2000Tile *tile = s->tile + s->curtileno;

    if (ret = init_tile(s, s->curtileno))
        return ret;
    if (ret = jpeg2000_decode_packets(s, tile))
        return ret;

    return 0;
}

static int jp2_find_codestream(Jpeg2000DecoderContext *s)
{
    int32_t atom_size;
    int found_codestream = 0, search_range = 10;

    // Skip JPEG 2000 signature atom.
    s->buf += 12;

    while (!found_codestream && search_range) {
        atom_size = AV_RB32(s->buf);
        if (AV_RB32(s->buf + 4) == JP2_CODESTREAM) {
            found_codestream = 1;
            s->buf += 8;
        } else {
            s->buf += atom_size;
            search_range--;
        }
    }

    if (found_codestream)
        return 1;
    return 0;
}

static int jpeg2000_decode_frame(AVCodecContext *avctx, void *data,
                                 int *got_frame, AVPacket *avpkt)
{
    Jpeg2000DecoderContext *s = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    AVFrame *picture = data;
    int tileno, ret;

    s->avctx     = avctx;
    s->buf       = s->buf_start = avpkt->data;
    s->buf_end   = s->buf_start + avpkt->size;
    s->curtileno = 0; // TODO: only one tile in DCI JP2K. to implement for more tiles

    // reduction factor, i.e number of resolution levels to skip
    s->reduction_factor = s->lowres;

    if (s->buf_end - s->buf < 2)
        return AVERROR(EINVAL);

    // check if the image is in jp2 format
    if ((AV_RB32(s->buf) == 12) &&
        (AV_RB32(s->buf + 4) == JP2_SIG_TYPE) &&
        (AV_RB32(s->buf + 8) == JP2_SIG_VALUE)) {
        if (!jp2_find_codestream(s)) {
            av_log(avctx, AV_LOG_ERROR,
                   "couldn't find jpeg2k codestream atom\n");
            return -1;
        }
    } else if (AV_RB16(s->buf) != JPEG2000_SOC && AV_RB32(s->buf + 4) == JP2_CODESTREAM) {
        s->buf += 8;
    }

    if (bytestream_get_be16(&s->buf) != JPEG2000_SOC) {
        av_log(avctx, AV_LOG_ERROR, "SOC marker not present\n");
        return -1;
    }
    if (ret = jpeg2000_read_main_headers(s))
        goto end;

    /* get picture buffer */
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_thread_get_buffer() failed.\n");
        goto end;
    }
    picture->pict_type = AV_PICTURE_TYPE_I;
    picture->key_frame = 1;

    if (ret = jpeg2000_read_bitstream_packets(s))
        goto end;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++)
        if (ret = jpeg2000_decode_tile(s, s->tile + tileno, picture))
            goto end;
    jpeg2000_dec_cleanup(s);

    *got_frame = 1;

    return s->buf - s->buf_start;
end:
    jpeg2000_dec_cleanup(s);
    return ret;
}

static void jpeg2000_init_static_data(AVCodec *codec)
{
    ff_jpeg2000_init_tier1_luts();
}

#define OFFSET(x) offsetof(Jpeg2000DecoderContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "lowres",  "Lower the decoding resolution by a power of two",
        OFFSET(lowres), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, JPEG2000_MAX_RESLEVELS - 1, VD },
    { NULL },
};

static const AVProfile profiles[] = {
    { FF_PROFILE_JPEG2000_CSTREAM_RESTRICTION_0,  "JPEG 2000 codestream restriction 0"   },
    { FF_PROFILE_JPEG2000_CSTREAM_RESTRICTION_1,  "JPEG 2000 codestream restriction 1"   },
    { FF_PROFILE_JPEG2000_CSTREAM_NO_RESTRICTION, "JPEG 2000 no codestream restrictions" },
    { FF_PROFILE_JPEG2000_DCINEMA_2K,             "JPEG 2000 digital cinema 2K"          },
    { FF_PROFILE_JPEG2000_DCINEMA_4K,             "JPEG 2000 digital cinema 4K"          },
    { FF_PROFILE_UNKNOWN },
};

static const AVClass class = {
    .class_name = "jpeg2000",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_jpeg2000_decoder = {
    .name             = "jpeg2000",
    .long_name        = NULL_IF_CONFIG_SMALL("JPEG 2000"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_JPEG2000,
    .capabilities     = CODEC_CAP_FRAME_THREADS,
    .priv_data_size   = sizeof(Jpeg2000DecoderContext),
    .init_static_data = jpeg2000_init_static_data,
    .decode           = jpeg2000_decode_frame,
    .priv_class       = &class,
    .pix_fmts         = (enum PixelFormat[]) { AV_PIX_FMT_XYZ12,
                                               AV_PIX_FMT_GRAY8,
                                               -1 },
    .profiles         = NULL_IF_CONFIG_SMALL(profiles)
};
