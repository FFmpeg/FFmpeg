/*
 * JPEG2000 image encoder
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
 *
 * **********************************************************************************************************************
 *
 *
 *
 * This source code incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2002-2007, Communications and Remote Sensing Laboratory, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2007, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux and Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


/**
 * JPEG2000 image encoder
 * @file
 * @author Kamil Nowosad
 */

#include <float.h>
#include "avcodec.h"
#include "internal.h"
#include "bytestream.h"
#include "jpeg2000.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"

#define NMSEDEC_BITS 7
#define NMSEDEC_FRACBITS (NMSEDEC_BITS-1)
#define WMSEDEC_SHIFT 13 ///< must be >= 13
#define LAMBDA_SCALE (100000000LL << (WMSEDEC_SHIFT - 13))

#define CODEC_JP2 1
#define CODEC_J2K 0

static int lut_nmsedec_ref [1<<NMSEDEC_BITS],
           lut_nmsedec_ref0[1<<NMSEDEC_BITS],
           lut_nmsedec_sig [1<<NMSEDEC_BITS],
           lut_nmsedec_sig0[1<<NMSEDEC_BITS];

static const int dwt_norms[2][4][10] = { // [dwt_type][band][rlevel] (multiplied by 10000)
    {{10000, 19650, 41770,  84030, 169000, 338400,  676900, 1353000, 2706000, 5409000},
     {20220, 39890, 83550, 170400, 342700, 686300, 1373000, 2746000, 5490000},
     {20220, 39890, 83550, 170400, 342700, 686300, 1373000, 2746000, 5490000},
     {20800, 38650, 83070, 171800, 347100, 695900, 1393000, 2786000, 5572000}},

    {{10000, 15000, 27500, 53750, 106800, 213400, 426700, 853300, 1707000, 3413000},
     {10380, 15920, 29190, 57030, 113300, 226400, 452500, 904800, 1809000},
     {10380, 15920, 29190, 57030, 113300, 226400, 452500, 904800, 1809000},
     { 7186,  9218, 15860, 30430,  60190, 120100, 240000, 479700,  959300}}
};

typedef struct {
   Jpeg2000Component *comp;
} Jpeg2000Tile;

typedef struct {
    AVClass *class;
    AVCodecContext *avctx;
    const AVFrame *picture;

    int width, height; ///< image width and height
    uint8_t cbps[4]; ///< bits per sample in particular components
    int chroma_shift[2];
    uint8_t planar;
    int ncomponents;
    int tile_width, tile_height; ///< tile size
    int numXtiles, numYtiles;

    uint8_t *buf_start;
    uint8_t *buf;
    uint8_t *buf_end;
    int bit_index;

    int64_t lambda;

    Jpeg2000CodingStyle codsty;
    Jpeg2000QuantStyle  qntsty;

    Jpeg2000Tile *tile;

    int format;
    int pred;
} Jpeg2000EncoderContext;


/* debug */
#if 0
#undef ifprintf
#undef printf

static void nspaces(FILE *fd, int n)
{
    while(n--) putc(' ', fd);
}

static void printcomp(Jpeg2000Component *comp)
{
    int i;
    for (i = 0; i < comp->y1 - comp->y0; i++)
        ff_jpeg2000_printv(comp->i_data + i * (comp->x1 - comp->x0), comp->x1 - comp->x0);
}

static void dump(Jpeg2000EncoderContext *s, FILE *fd)
{
    int tileno, compno, reslevelno, bandno, precno;
    fprintf(fd, "XSiz = %d, YSiz = %d, tile_width = %d, tile_height = %d\n"
                "numXtiles = %d, numYtiles = %d, ncomponents = %d\n"
                "tiles:\n",
            s->width, s->height, s->tile_width, s->tile_height,
            s->numXtiles, s->numYtiles, s->ncomponents);
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        Jpeg2000Tile *tile = s->tile + tileno;
        nspaces(fd, 2);
        fprintf(fd, "tile %d:\n", tileno);
        for(compno = 0; compno < s->ncomponents; compno++){
            Jpeg2000Component *comp = tile->comp + compno;
            nspaces(fd, 4);
            fprintf(fd, "component %d:\n", compno);
            nspaces(fd, 4);
            fprintf(fd, "x0 = %d, x1 = %d, y0 = %d, y1 = %d\n",
                        comp->x0, comp->x1, comp->y0, comp->y1);
            for(reslevelno = 0; reslevelno < s->nreslevels; reslevelno++){
                Jpeg2000ResLevel *reslevel = comp->reslevel + reslevelno;
                nspaces(fd, 6);
                fprintf(fd, "reslevel %d:\n", reslevelno);
                nspaces(fd, 6);
                fprintf(fd, "x0 = %d, x1 = %d, y0 = %d, y1 = %d, nbands = %d\n",
                        reslevel->x0, reslevel->x1, reslevel->y0,
                        reslevel->y1, reslevel->nbands);
                for(bandno = 0; bandno < reslevel->nbands; bandno++){
                    Jpeg2000Band *band = reslevel->band + bandno;
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
                        Jpeg2000Prec *prec = band->prec + precno;
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

/* bitstream routines */

/** put n times val bit */
static void put_bits(Jpeg2000EncoderContext *s, int val, int n) // TODO: optimize
{
    while (n-- > 0){
        if (s->bit_index == 8)
        {
            s->bit_index = *s->buf == 0xff;
            *(++s->buf) = 0;
        }
        *s->buf |= val << (7 - s->bit_index++);
    }
}

/** put n least significant bits of a number num */
static void put_num(Jpeg2000EncoderContext *s, int num, int n)
{
    while(--n >= 0)
        put_bits(s, (num >> n) & 1, 1);
}

/** flush the bitstream */
static void j2k_flush(Jpeg2000EncoderContext *s)
{
    if (s->bit_index){
        s->bit_index = 0;
        s->buf++;
    }
}

/* tag tree routines */

/** code the value stored in node */
static void tag_tree_code(Jpeg2000EncoderContext *s, Jpeg2000TgtNode *node, int threshold)
{
    Jpeg2000TgtNode *stack[30];
    int sp = 1, curval = 0;
    stack[0] = node;

    node = node->parent;
    while(node){
        if (node->vis){
            curval = node->val;
            break;
        }
        node->vis++;
        stack[sp++] = node;
        node = node->parent;
    }
    while(--sp >= 0){
        if (stack[sp]->val >= threshold){
            put_bits(s, 0, threshold - curval);
            break;
        }
        put_bits(s, 0, stack[sp]->val - curval);
        put_bits(s, 1, 1);
        curval = stack[sp]->val;
    }
}

/** update the value in node */
static void tag_tree_update(Jpeg2000TgtNode *node)
{
    int lev = 0;
    while (node->parent){
        if (node->parent->val <= node->val)
            break;
        node->parent->val = node->val;
        node = node->parent;
        lev++;
    }
}

static int put_siz(Jpeg2000EncoderContext *s)
{
    int i;

    if (s->buf_end - s->buf < 40 + 3 * s->ncomponents)
        return -1;

    bytestream_put_be16(&s->buf, JPEG2000_SIZ);
    bytestream_put_be16(&s->buf, 38 + 3 * s->ncomponents); // Lsiz
    bytestream_put_be16(&s->buf, 0); // Rsiz
    bytestream_put_be32(&s->buf, s->width); // width
    bytestream_put_be32(&s->buf, s->height); // height
    bytestream_put_be32(&s->buf, 0); // X0Siz
    bytestream_put_be32(&s->buf, 0); // Y0Siz

    bytestream_put_be32(&s->buf, s->tile_width); // XTSiz
    bytestream_put_be32(&s->buf, s->tile_height); // YTSiz
    bytestream_put_be32(&s->buf, 0); // XT0Siz
    bytestream_put_be32(&s->buf, 0); // YT0Siz
    bytestream_put_be16(&s->buf, s->ncomponents); // CSiz

    for (i = 0; i < s->ncomponents; i++){ // Ssiz_i XRsiz_i, YRsiz_i
        bytestream_put_byte(&s->buf, 7);
        bytestream_put_byte(&s->buf, i?1<<s->chroma_shift[0]:1);
        bytestream_put_byte(&s->buf, i?1<<s->chroma_shift[1]:1);
    }
    return 0;
}

static int put_cod(Jpeg2000EncoderContext *s)
{
    Jpeg2000CodingStyle *codsty = &s->codsty;

    if (s->buf_end - s->buf < 14)
        return -1;

    bytestream_put_be16(&s->buf, JPEG2000_COD);
    bytestream_put_be16(&s->buf, 12); // Lcod
    bytestream_put_byte(&s->buf, 0);  // Scod
    // SGcod
    bytestream_put_byte(&s->buf, 0); // progression level
    bytestream_put_be16(&s->buf, 1); // num of layers
    if(s->avctx->pix_fmt == AV_PIX_FMT_YUV444P){
        bytestream_put_byte(&s->buf, 0); // unspecified
    }else{
        bytestream_put_byte(&s->buf, 0); // unspecified
    }
    // SPcod
    bytestream_put_byte(&s->buf, codsty->nreslevels - 1); // num of decomp. levels
    bytestream_put_byte(&s->buf, codsty->log2_cblk_width-2); // cblk width
    bytestream_put_byte(&s->buf, codsty->log2_cblk_height-2); // cblk height
    bytestream_put_byte(&s->buf, 0); // cblk style
    bytestream_put_byte(&s->buf, codsty->transform == FF_DWT53); // transformation
    return 0;
}

static int put_qcd(Jpeg2000EncoderContext *s, int compno)
{
    int i, size;
    Jpeg2000CodingStyle *codsty = &s->codsty;
    Jpeg2000QuantStyle  *qntsty = &s->qntsty;

    if (qntsty->quantsty == JPEG2000_QSTY_NONE)
        size = 4 + 3 * (codsty->nreslevels-1);
    else // QSTY_SE
        size = 5 + 6 * (codsty->nreslevels-1);

    if (s->buf_end - s->buf < size + 2)
        return -1;

    bytestream_put_be16(&s->buf, JPEG2000_QCD);
    bytestream_put_be16(&s->buf, size);  // LQcd
    bytestream_put_byte(&s->buf, (qntsty->nguardbits << 5) | qntsty->quantsty);  // Sqcd
    if (qntsty->quantsty == JPEG2000_QSTY_NONE)
        for (i = 0; i < codsty->nreslevels * 3 - 2; i++)
            bytestream_put_byte(&s->buf, qntsty->expn[i] << 3);
    else // QSTY_SE
        for (i = 0; i < codsty->nreslevels * 3 - 2; i++)
            bytestream_put_be16(&s->buf, (qntsty->expn[i] << 11) | qntsty->mant[i]);
    return 0;
}

static int put_com(Jpeg2000EncoderContext *s, int compno)
{
    int size = 4 + strlen(LIBAVCODEC_IDENT);

    if (s->avctx->flags & AV_CODEC_FLAG_BITEXACT)
        return 0;

    if (s->buf_end - s->buf < size + 2)
        return -1;

    bytestream_put_be16(&s->buf, JPEG2000_COM);
    bytestream_put_be16(&s->buf, size);
    bytestream_put_be16(&s->buf, 1); // General use (ISO/IEC 8859-15 (Latin) values)

    bytestream_put_buffer(&s->buf, LIBAVCODEC_IDENT, strlen(LIBAVCODEC_IDENT));

    return 0;
}

static uint8_t *put_sot(Jpeg2000EncoderContext *s, int tileno)
{
    uint8_t *psotptr;

    if (s->buf_end - s->buf < 12)
        return NULL;

    bytestream_put_be16(&s->buf, JPEG2000_SOT);
    bytestream_put_be16(&s->buf, 10); // Lsot
    bytestream_put_be16(&s->buf, tileno); // Isot

    psotptr = s->buf;
    bytestream_put_be32(&s->buf, 0); // Psot (filled in later)

    bytestream_put_byte(&s->buf, 0); // TPsot
    bytestream_put_byte(&s->buf, 1); // TNsot
    return psotptr;
}

/**
 * compute the sizes of tiles, resolution levels, bands, etc.
 * allocate memory for them
 * divide the input image into tile-components
 */
static int init_tiles(Jpeg2000EncoderContext *s)
{
    int tileno, tilex, tiley, compno;
    Jpeg2000CodingStyle *codsty = &s->codsty;
    Jpeg2000QuantStyle  *qntsty = &s->qntsty;

    s->numXtiles = ff_jpeg2000_ceildiv(s->width, s->tile_width);
    s->numYtiles = ff_jpeg2000_ceildiv(s->height, s->tile_height);

    s->tile = av_malloc_array(s->numXtiles, s->numYtiles * sizeof(Jpeg2000Tile));
    if (!s->tile)
        return AVERROR(ENOMEM);
    for (tileno = 0, tiley = 0; tiley < s->numYtiles; tiley++)
        for (tilex = 0; tilex < s->numXtiles; tilex++, tileno++){
            Jpeg2000Tile *tile = s->tile + tileno;

            tile->comp = av_mallocz_array(s->ncomponents, sizeof(Jpeg2000Component));
            if (!tile->comp)
                return AVERROR(ENOMEM);
            for (compno = 0; compno < s->ncomponents; compno++){
                Jpeg2000Component *comp = tile->comp + compno;
                int ret, i, j;

                comp->coord[0][0] = comp->coord_o[0][0] = tilex * s->tile_width;
                comp->coord[0][1] = comp->coord_o[0][1] = FFMIN((tilex+1)*s->tile_width, s->width);
                comp->coord[1][0] = comp->coord_o[1][0] = tiley * s->tile_height;
                comp->coord[1][1] = comp->coord_o[1][1] = FFMIN((tiley+1)*s->tile_height, s->height);
                if (compno > 0)
                    for (i = 0; i < 2; i++)
                        for (j = 0; j < 2; j++)
                            comp->coord[i][j] = comp->coord_o[i][j] = ff_jpeg2000_ceildivpow2(comp->coord[i][j], s->chroma_shift[i]);

                if ((ret = ff_jpeg2000_init_component(comp,
                                                codsty,
                                                qntsty,
                                                s->cbps[compno],
                                                compno?1<<s->chroma_shift[0]:1,
                                                compno?1<<s->chroma_shift[1]:1,
                                                s->avctx
                                               )) < 0)
                    return ret;
            }
        }
    return 0;
}

static void copy_frame(Jpeg2000EncoderContext *s)
{
    int tileno, compno, i, y, x;
    uint8_t *line;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        Jpeg2000Tile *tile = s->tile + tileno;
        if (s->planar){
            for (compno = 0; compno < s->ncomponents; compno++){
                Jpeg2000Component *comp = tile->comp + compno;
                int *dst = comp->i_data;
                line = s->picture->data[compno]
                       + comp->coord[1][0] * s->picture->linesize[compno]
                       + comp->coord[0][0];
                for (y = comp->coord[1][0]; y < comp->coord[1][1]; y++){
                    uint8_t *ptr = line;
                    for (x = comp->coord[0][0]; x < comp->coord[0][1]; x++)
                        *dst++ = *ptr++ - (1 << 7);
                    line += s->picture->linesize[compno];
                }
            }
        } else{
            line = s->picture->data[0] + tile->comp[0].coord[1][0] * s->picture->linesize[0]
                   + tile->comp[0].coord[0][0] * s->ncomponents;

            i = 0;
            for (y = tile->comp[0].coord[1][0]; y < tile->comp[0].coord[1][1]; y++){
                uint8_t *ptr = line;
                for (x = tile->comp[0].coord[0][0]; x < tile->comp[0].coord[0][1]; x++, i++){
                    for (compno = 0; compno < s->ncomponents; compno++){
                        tile->comp[compno].i_data[i] = *ptr++  - (1 << 7);
                    }
                }
                line += s->picture->linesize[0];
            }
        }
    }
}

static void init_quantization(Jpeg2000EncoderContext *s)
{
    int compno, reslevelno, bandno;
    Jpeg2000QuantStyle  *qntsty = &s->qntsty;
    Jpeg2000CodingStyle *codsty = &s->codsty;

    for (compno = 0; compno < s->ncomponents; compno++){
        int gbandno = 0;
        for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
            int nbands, lev = codsty->nreslevels - reslevelno - 1;
            nbands = reslevelno ? 3 : 1;
            for (bandno = 0; bandno < nbands; bandno++, gbandno++){
                int expn, mant = 0;

                if (codsty->transform == FF_DWT97_INT){
                    int bandpos = bandno + (reslevelno>0),
                        ss = 81920000 / dwt_norms[0][bandpos][lev],
                        log = av_log2(ss);
                    mant = (11 - log < 0 ? ss >> log - 11 : ss << 11 - log) & 0x7ff;
                    expn = s->cbps[compno] - log + 13;
                } else
                    expn = ((bandno&2)>>1) + (reslevelno>0) + s->cbps[compno];

                qntsty->expn[gbandno] = expn;
                qntsty->mant[gbandno] = mant;
            }
        }
    }
}

static void init_luts(void)
{
    int i, a,
        mask = ~((1<<NMSEDEC_FRACBITS)-1);

    for (i = 0; i < (1 << NMSEDEC_BITS); i++){
        lut_nmsedec_sig[i]  = FFMAX((3 * i << (13 - NMSEDEC_FRACBITS)) - (9 << 11), 0);
        lut_nmsedec_sig0[i] = FFMAX((i*i + (1<<NMSEDEC_FRACBITS-1) & mask) << 1, 0);

        a = (i >> (NMSEDEC_BITS-2)&2) + 1;
        lut_nmsedec_ref[i]  = FFMAX((a - 2) * (i << (13 - NMSEDEC_FRACBITS)) +
                                    (1 << 13) - (a * a << 11), 0);
        lut_nmsedec_ref0[i] = FFMAX(((i * i - (i << NMSEDEC_BITS) + (1 << 2 * NMSEDEC_FRACBITS) + (1 << (NMSEDEC_FRACBITS - 1))) & mask)
                                    << 1, 0);
    }
}

/* tier-1 routines */
static int getnmsedec_sig(int x, int bpno)
{
    if (bpno > NMSEDEC_FRACBITS)
        return lut_nmsedec_sig[(x >> (bpno - NMSEDEC_FRACBITS)) & ((1 << NMSEDEC_BITS) - 1)];
    return lut_nmsedec_sig0[x & ((1 << NMSEDEC_BITS) - 1)];
}

static int getnmsedec_ref(int x, int bpno)
{
    if (bpno > NMSEDEC_FRACBITS)
        return lut_nmsedec_ref[(x >> (bpno - NMSEDEC_FRACBITS)) & ((1 << NMSEDEC_BITS) - 1)];
    return lut_nmsedec_ref0[x & ((1 << NMSEDEC_BITS) - 1)];
}

static void encode_sigpass(Jpeg2000T1Context *t1, int width, int height, int bandno, int *nmsedec, int bpno)
{
    int y0, x, y, mask = 1 << (bpno + NMSEDEC_FRACBITS);
    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0+4; y++){
                if (!(t1->flags[(y+1) * t1->stride + x+1] & JPEG2000_T1_SIG) && (t1->flags[(y+1) * t1->stride + x+1] & JPEG2000_T1_SIG_NB)){
                    int ctxno = ff_jpeg2000_getsigctxno(t1->flags[(y+1) * t1->stride + x+1], bandno),
                        bit = t1->data[(y) * t1->stride + x] & mask ? 1 : 0;
                    ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, bit);
                    if (bit){
                        int xorbit;
                        int ctxno = ff_jpeg2000_getsgnctxno(t1->flags[(y+1) * t1->stride + x+1], &xorbit);
                        ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, (t1->flags[(y+1) * t1->stride + x+1] >> 15) ^ xorbit);
                        *nmsedec += getnmsedec_sig(t1->data[(y) * t1->stride + x], bpno + NMSEDEC_FRACBITS);
                        ff_jpeg2000_set_significance(t1, x, y, t1->flags[(y+1) * t1->stride + x+1] >> 15);
                    }
                    t1->flags[(y+1) * t1->stride + x+1] |= JPEG2000_T1_VIS;
                }
            }
}

static void encode_refpass(Jpeg2000T1Context *t1, int width, int height, int *nmsedec, int bpno)
{
    int y0, x, y, mask = 1 << (bpno + NMSEDEC_FRACBITS);
    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0+4; y++)
                if ((t1->flags[(y+1) * t1->stride + x+1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS)) == JPEG2000_T1_SIG){
                    int ctxno = ff_jpeg2000_getrefctxno(t1->flags[(y+1) * t1->stride + x+1]);
                    *nmsedec += getnmsedec_ref(t1->data[(y) * t1->stride + x], bpno + NMSEDEC_FRACBITS);
                    ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, t1->data[(y) * t1->stride + x] & mask ? 1:0);
                    t1->flags[(y+1) * t1->stride + x+1] |= JPEG2000_T1_REF;
                }
}

static void encode_clnpass(Jpeg2000T1Context *t1, int width, int height, int bandno, int *nmsedec, int bpno)
{
    int y0, x, y, mask = 1 << (bpno + NMSEDEC_FRACBITS);
    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++){
            if (y0 + 3 < height && !(
            (t1->flags[(y0+1) * t1->stride + x+1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG)) ||
            (t1->flags[(y0+2) * t1->stride + x+1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG)) ||
            (t1->flags[(y0+3) * t1->stride + x+1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG)) ||
            (t1->flags[(y0+4) * t1->stride + x+1] & (JPEG2000_T1_SIG_NB | JPEG2000_T1_VIS | JPEG2000_T1_SIG))))
            {
                // aggregation mode
                int rlen;
                for (rlen = 0; rlen < 4; rlen++)
                    if (t1->data[(y0+rlen) * t1->stride + x] & mask)
                        break;
                ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + MQC_CX_RL, rlen != 4);
                if (rlen == 4)
                    continue;
                ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI, rlen >> 1);
                ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI, rlen & 1);
                for (y = y0 + rlen; y < y0 + 4; y++){
                    if (!(t1->flags[(y+1) * t1->stride + x+1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS))){
                        int ctxno = ff_jpeg2000_getsigctxno(t1->flags[(y+1) * t1->stride + x+1], bandno);
                        if (y > y0 + rlen)
                            ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, t1->data[(y) * t1->stride + x] & mask ? 1:0);
                        if (t1->data[(y) * t1->stride + x] & mask){ // newly significant
                            int xorbit;
                            int ctxno = ff_jpeg2000_getsgnctxno(t1->flags[(y+1) * t1->stride + x+1], &xorbit);
                            *nmsedec += getnmsedec_sig(t1->data[(y) * t1->stride + x], bpno + NMSEDEC_FRACBITS);
                            ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, (t1->flags[(y+1) * t1->stride + x+1] >> 15) ^ xorbit);
                            ff_jpeg2000_set_significance(t1, x, y, t1->flags[(y+1) * t1->stride + x+1] >> 15);
                        }
                    }
                    t1->flags[(y+1) * t1->stride + x+1] &= ~JPEG2000_T1_VIS;
                }
            } else{
                for (y = y0; y < y0 + 4 && y < height; y++){
                    if (!(t1->flags[(y+1) * t1->stride + x+1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS))){
                        int ctxno = ff_jpeg2000_getsigctxno(t1->flags[(y+1) * t1->stride + x+1], bandno);
                        ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, t1->data[(y) * t1->stride + x] & mask ? 1:0);
                        if (t1->data[(y) * t1->stride + x] & mask){ // newly significant
                            int xorbit;
                            int ctxno = ff_jpeg2000_getsgnctxno(t1->flags[(y+1) * t1->stride + x+1], &xorbit);
                            *nmsedec += getnmsedec_sig(t1->data[(y) * t1->stride + x], bpno + NMSEDEC_FRACBITS);
                            ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, (t1->flags[(y+1) * t1->stride + x+1] >> 15) ^ xorbit);
                            ff_jpeg2000_set_significance(t1, x, y, t1->flags[(y+1) * t1->stride + x+1] >> 15);
                        }
                    }
                    t1->flags[(y+1) * t1->stride + x+1] &= ~JPEG2000_T1_VIS;
                }
            }
        }
}

static void encode_cblk(Jpeg2000EncoderContext *s, Jpeg2000T1Context *t1, Jpeg2000Cblk *cblk, Jpeg2000Tile *tile,
                        int width, int height, int bandpos, int lev)
{
    int pass_t = 2, passno, x, y, max=0, nmsedec, bpno;
    int64_t wmsedec = 0;

    memset(t1->flags, 0, t1->stride * (height + 2) * sizeof(*t1->flags));

    for (y = 0; y < height; y++){
        for (x = 0; x < width; x++){
            if (t1->data[(y) * t1->stride + x] < 0){
                t1->flags[(y+1) * t1->stride + x+1] |= JPEG2000_T1_SGN;
                t1->data[(y) * t1->stride + x] = -t1->data[(y) * t1->stride + x];
            }
            max = FFMAX(max, t1->data[(y) * t1->stride + x]);
        }
    }

    if (max == 0){
        cblk->nonzerobits = 0;
        bpno = 0;
    } else{
        cblk->nonzerobits = av_log2(max) + 1 - NMSEDEC_FRACBITS;
        bpno = cblk->nonzerobits - 1;
    }

    cblk->data[0] = 0;
    ff_mqc_initenc(&t1->mqc, cblk->data + 1);

    for (passno = 0; bpno >= 0; passno++){
        nmsedec=0;

        switch(pass_t){
            case 0: encode_sigpass(t1, width, height, bandpos, &nmsedec, bpno);
                    break;
            case 1: encode_refpass(t1, width, height, &nmsedec, bpno);
                    break;
            case 2: encode_clnpass(t1, width, height, bandpos, &nmsedec, bpno);
                    break;
        }

        cblk->passes[passno].rate = ff_mqc_flush_to(&t1->mqc, cblk->passes[passno].flushed, &cblk->passes[passno].flushed_len);
        wmsedec += (int64_t)nmsedec << (2*bpno);
        cblk->passes[passno].disto = wmsedec;

        if (++pass_t == 3){
            pass_t = 0;
            bpno--;
        }
    }
    cblk->npasses = passno;
    cblk->ninclpasses = passno;

    if (passno)
        cblk->passes[passno-1].rate = ff_mqc_flush_to(&t1->mqc, cblk->passes[passno-1].flushed, &cblk->passes[passno-1].flushed_len);
}

/* tier-2 routines: */

static void putnumpasses(Jpeg2000EncoderContext *s, int n)
{
    if (n == 1)
        put_num(s, 0, 1);
    else if (n == 2)
        put_num(s, 2, 2);
    else if (n <= 5)
        put_num(s, 0xc | (n-3), 4);
    else if (n <= 36)
        put_num(s, 0x1e0 | (n-6), 9);
    else
        put_num(s, 0xff80 | (n-37), 16);
}


static int encode_packet(Jpeg2000EncoderContext *s, Jpeg2000ResLevel *rlevel, int precno,
                          uint8_t *expn, int numgbits)
{
    int bandno, empty = 1;

    // init bitstream
    *s->buf = 0;
    s->bit_index = 0;

    // header

    // is the packet empty?
    for (bandno = 0; bandno < rlevel->nbands; bandno++){
        if (rlevel->band[bandno].coord[0][0] < rlevel->band[bandno].coord[0][1]
        &&  rlevel->band[bandno].coord[1][0] < rlevel->band[bandno].coord[1][1]){
            empty = 0;
            break;
        }
    }

    put_bits(s, !empty, 1);
    if (empty){
        j2k_flush(s);
        return 0;
    }

    for (bandno = 0; bandno < rlevel->nbands; bandno++){
        Jpeg2000Band *band = rlevel->band + bandno;
        Jpeg2000Prec *prec = band->prec + precno;
        int yi, xi, pos;
        int cblknw = prec->nb_codeblocks_width;

        if (band->coord[0][0] == band->coord[0][1]
        ||  band->coord[1][0] == band->coord[1][1])
            continue;

        for (pos=0, yi = 0; yi < prec->nb_codeblocks_height; yi++){
            for (xi = 0; xi < cblknw; xi++, pos++){
                prec->cblkincl[pos].val = prec->cblk[yi * cblknw + xi].ninclpasses == 0;
                tag_tree_update(prec->cblkincl + pos);
                prec->zerobits[pos].val = expn[bandno] + numgbits - 1 - prec->cblk[yi * cblknw + xi].nonzerobits;
                tag_tree_update(prec->zerobits + pos);
            }
        }

        for (pos=0, yi = 0; yi < prec->nb_codeblocks_height; yi++){
            for (xi = 0; xi < cblknw; xi++, pos++){
                int pad = 0, llen, length;
                Jpeg2000Cblk *cblk = prec->cblk + yi * cblknw + xi;

                if (s->buf_end - s->buf < 20) // approximately
                    return -1;

                // inclusion information
                tag_tree_code(s, prec->cblkincl + pos, 1);
                if (!cblk->ninclpasses)
                    continue;
                // zerobits information
                tag_tree_code(s, prec->zerobits + pos, 100);
                // number of passes
                putnumpasses(s, cblk->ninclpasses);

                length = cblk->passes[cblk->ninclpasses-1].rate;
                llen = av_log2(length) - av_log2(cblk->ninclpasses) - 2;
                if (llen < 0){
                    pad = -llen;
                    llen = 0;
                }
                // length of code block
                put_bits(s, 1, llen);
                put_bits(s, 0, 1);
                put_num(s, length, av_log2(length)+1+pad);
            }
        }
    }
    j2k_flush(s);
    for (bandno = 0; bandno < rlevel->nbands; bandno++){
        Jpeg2000Band *band = rlevel->band + bandno;
        Jpeg2000Prec *prec = band->prec + precno;
        int yi, cblknw = prec->nb_codeblocks_width;
        for (yi =0; yi < prec->nb_codeblocks_height; yi++){
            int xi;
            for (xi = 0; xi < cblknw; xi++){
                Jpeg2000Cblk *cblk = prec->cblk + yi * cblknw + xi;
                if (cblk->ninclpasses){
                    if (s->buf_end - s->buf < cblk->passes[cblk->ninclpasses-1].rate)
                        return -1;
                    bytestream_put_buffer(&s->buf, cblk->data + 1,   cblk->passes[cblk->ninclpasses-1].rate
                                                               - cblk->passes[cblk->ninclpasses-1].flushed_len);
                    bytestream_put_buffer(&s->buf, cblk->passes[cblk->ninclpasses-1].flushed,
                                                   cblk->passes[cblk->ninclpasses-1].flushed_len);
                }
            }
        }
    }
    return 0;
}

static int encode_packets(Jpeg2000EncoderContext *s, Jpeg2000Tile *tile, int tileno)
{
    int compno, reslevelno, ret;
    Jpeg2000CodingStyle *codsty = &s->codsty;
    Jpeg2000QuantStyle  *qntsty = &s->qntsty;

    av_log(s->avctx, AV_LOG_DEBUG, "tier2\n");
    // lay-rlevel-comp-pos progression
    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
        for (compno = 0; compno < s->ncomponents; compno++){
            int precno;
            Jpeg2000ResLevel *reslevel = s->tile[tileno].comp[compno].reslevel + reslevelno;
            for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                if ((ret = encode_packet(s, reslevel, precno, qntsty->expn + (reslevelno ? 3*reslevelno-2 : 0),
                              qntsty->nguardbits)) < 0)
                    return ret;
            }
        }
    }
    av_log(s->avctx, AV_LOG_DEBUG, "after tier2\n");
    return 0;
}

static int getcut(Jpeg2000Cblk *cblk, int64_t lambda, int dwt_norm)
{
    int passno, res = 0;
    for (passno = 0; passno < cblk->npasses; passno++){
        int dr;
        int64_t dd;

        dr = cblk->passes[passno].rate
           - (res ? cblk->passes[res-1].rate:0);
        dd = cblk->passes[passno].disto
           - (res ? cblk->passes[res-1].disto:0);

        if (((dd * dwt_norm) >> WMSEDEC_SHIFT) * dwt_norm >= dr * lambda)
            res = passno+1;
    }
    return res;
}

static void truncpasses(Jpeg2000EncoderContext *s, Jpeg2000Tile *tile)
{
    int precno, compno, reslevelno, bandno, cblkno, lev;
    Jpeg2000CodingStyle *codsty = &s->codsty;

    for (compno = 0; compno < s->ncomponents; compno++){
        Jpeg2000Component *comp = tile->comp + compno;

        for (reslevelno = 0, lev = codsty->nreslevels-1; reslevelno < codsty->nreslevels; reslevelno++, lev--){
            Jpeg2000ResLevel *reslevel = comp->reslevel + reslevelno;

            for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                for (bandno = 0; bandno < reslevel->nbands ; bandno++){
                    int bandpos = bandno + (reslevelno > 0);
                    Jpeg2000Band *band = reslevel->band + bandno;
                    Jpeg2000Prec *prec = band->prec + precno;

                    for (cblkno = 0; cblkno < prec->nb_codeblocks_height * prec->nb_codeblocks_width; cblkno++){
                        Jpeg2000Cblk *cblk = prec->cblk + cblkno;

                        cblk->ninclpasses = getcut(cblk, s->lambda,
                                (int64_t)dwt_norms[codsty->transform == FF_DWT53][bandpos][lev] * (int64_t)band->i_stepsize >> 15);
                    }
                }
            }
        }
    }
}

static int encode_tile(Jpeg2000EncoderContext *s, Jpeg2000Tile *tile, int tileno)
{
    int compno, reslevelno, bandno, ret;
    Jpeg2000T1Context t1;
    Jpeg2000CodingStyle *codsty = &s->codsty;
    for (compno = 0; compno < s->ncomponents; compno++){
        Jpeg2000Component *comp = s->tile[tileno].comp + compno;

        t1.stride = (1<<codsty->log2_cblk_width) + 2;

        av_log(s->avctx, AV_LOG_DEBUG,"dwt\n");
        if ((ret = ff_dwt_encode(&comp->dwt, comp->i_data)) < 0)
            return ret;
        av_log(s->avctx, AV_LOG_DEBUG,"after dwt -> tier1\n");

        for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
            Jpeg2000ResLevel *reslevel = comp->reslevel + reslevelno;

            for (bandno = 0; bandno < reslevel->nbands ; bandno++){
                Jpeg2000Band *band = reslevel->band + bandno;
                Jpeg2000Prec *prec = band->prec; // we support only 1 precinct per band ATM in the encoder
                int cblkx, cblky, cblkno=0, xx0, x0, xx1, y0, yy0, yy1, bandpos;
                yy0 = bandno == 0 ? 0 : comp->reslevel[reslevelno-1].coord[1][1] - comp->reslevel[reslevelno-1].coord[1][0];
                y0 = yy0;
                yy1 = FFMIN(ff_jpeg2000_ceildivpow2(band->coord[1][0] + 1, band->log2_cblk_height) << band->log2_cblk_height,
                            band->coord[1][1]) - band->coord[1][0] + yy0;

                if (band->coord[0][0] == band->coord[0][1] || band->coord[1][0] == band->coord[1][1])
                    continue;

                bandpos = bandno + (reslevelno > 0);

                for (cblky = 0; cblky < prec->nb_codeblocks_height; cblky++){
                    if (reslevelno == 0 || bandno == 1)
                        xx0 = 0;
                    else
                        xx0 = comp->reslevel[reslevelno-1].coord[0][1] - comp->reslevel[reslevelno-1].coord[0][0];
                    x0 = xx0;
                    xx1 = FFMIN(ff_jpeg2000_ceildivpow2(band->coord[0][0] + 1, band->log2_cblk_width) << band->log2_cblk_width,
                                band->coord[0][1]) - band->coord[0][0] + xx0;

                    for (cblkx = 0; cblkx < prec->nb_codeblocks_width; cblkx++, cblkno++){
                        int y, x;
                        if (codsty->transform == FF_DWT53){
                            for (y = yy0; y < yy1; y++){
                                int *ptr = t1.data + (y-yy0)*t1.stride;
                                for (x = xx0; x < xx1; x++){
                                    *ptr++ = comp->i_data[(comp->coord[0][1] - comp->coord[0][0]) * y + x] * (1 << NMSEDEC_FRACBITS);
                                }
                            }
                        } else{
                            for (y = yy0; y < yy1; y++){
                                int *ptr = t1.data + (y-yy0)*t1.stride;
                                for (x = xx0; x < xx1; x++){
                                    *ptr = (comp->i_data[(comp->coord[0][1] - comp->coord[0][0]) * y + x]);
                                    *ptr = (int64_t)*ptr * (int64_t)(16384 * 65536 / band->i_stepsize) >> 15 - NMSEDEC_FRACBITS;
                                    ptr++;
                                }
                            }
                        }
                        if (!prec->cblk[cblkno].data)
                            prec->cblk[cblkno].data = av_malloc(1 + 8192);
                        if (!prec->cblk[cblkno].passes)
                            prec->cblk[cblkno].passes = av_malloc_array(JPEG2000_MAX_PASSES, sizeof (*prec->cblk[cblkno].passes));
                        if (!prec->cblk[cblkno].data || !prec->cblk[cblkno].passes)
                            return AVERROR(ENOMEM);
                        encode_cblk(s, &t1, prec->cblk + cblkno, tile, xx1 - xx0, yy1 - yy0,
                                    bandpos, codsty->nreslevels - reslevelno - 1);
                        xx0 = xx1;
                        xx1 = FFMIN(xx1 + (1 << band->log2_cblk_width), band->coord[0][1] - band->coord[0][0] + x0);
                    }
                    yy0 = yy1;
                    yy1 = FFMIN(yy1 + (1 << band->log2_cblk_height), band->coord[1][1] - band->coord[1][0] + y0);
                }
            }
        }
        av_log(s->avctx, AV_LOG_DEBUG, "after tier1\n");
    }

    av_log(s->avctx, AV_LOG_DEBUG, "rate control\n");
    truncpasses(s, tile);
    if ((ret = encode_packets(s, tile, tileno)) < 0)
        return ret;
    av_log(s->avctx, AV_LOG_DEBUG, "after rate control\n");
    return 0;
}

static void cleanup(Jpeg2000EncoderContext *s)
{
    int tileno, compno;
    Jpeg2000CodingStyle *codsty = &s->codsty;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        for (compno = 0; compno < s->ncomponents; compno++){
            Jpeg2000Component *comp = s->tile[tileno].comp + compno;
            ff_jpeg2000_cleanup(comp, codsty);
        }
        av_freep(&s->tile[tileno].comp);
    }
    av_freep(&s->tile);
}

static void reinit(Jpeg2000EncoderContext *s)
{
    int tileno, compno;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        Jpeg2000Tile *tile = s->tile + tileno;
        for (compno = 0; compno < s->ncomponents; compno++)
            ff_jpeg2000_reinit(tile->comp + compno, &s->codsty);
    }
}

static void update_size(uint8_t *size, const uint8_t *end)
{
    AV_WB32(size, end-size);
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pict, int *got_packet)
{
    int tileno, ret;
    Jpeg2000EncoderContext *s = avctx->priv_data;
    uint8_t *chunkstart, *jp2cstart, *jp2hstart;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width*avctx->height*9 + AV_INPUT_BUFFER_MIN_SIZE, 0)) < 0)
        return ret;

    // init:
    s->buf = s->buf_start = pkt->data;
    s->buf_end = pkt->data + pkt->size;

    s->picture = pict;

    s->lambda = s->picture->quality * LAMBDA_SCALE;

    copy_frame(s);
    reinit(s);

    if (s->format == CODEC_JP2) {
        av_assert0(s->buf == pkt->data);

        bytestream_put_be32(&s->buf, 0x0000000C);
        bytestream_put_be32(&s->buf, 0x6A502020);
        bytestream_put_be32(&s->buf, 0x0D0A870A);

        chunkstart = s->buf;
        bytestream_put_be32(&s->buf, 0);
        bytestream_put_buffer(&s->buf, "ftyp", 4);
        bytestream_put_buffer(&s->buf, "jp2\040\040", 4);
        bytestream_put_be32(&s->buf, 0);
        bytestream_put_buffer(&s->buf, "jp2\040", 4);
        update_size(chunkstart, s->buf);

        jp2hstart = s->buf;
        bytestream_put_be32(&s->buf, 0);
        bytestream_put_buffer(&s->buf, "jp2h", 4);

        chunkstart = s->buf;
        bytestream_put_be32(&s->buf, 0);
        bytestream_put_buffer(&s->buf, "ihdr", 4);
        bytestream_put_be32(&s->buf, avctx->height);
        bytestream_put_be32(&s->buf, avctx->width);
        bytestream_put_be16(&s->buf, s->ncomponents);
        bytestream_put_byte(&s->buf, s->cbps[0]);
        bytestream_put_byte(&s->buf, 7);
        bytestream_put_byte(&s->buf, 0);
        bytestream_put_byte(&s->buf, 0);
        update_size(chunkstart, s->buf);

        chunkstart = s->buf;
        bytestream_put_be32(&s->buf, 0);
        bytestream_put_buffer(&s->buf, "colr", 4);
        bytestream_put_byte(&s->buf, 1);
        bytestream_put_byte(&s->buf, 0);
        bytestream_put_byte(&s->buf, 0);
        if (avctx->pix_fmt == AV_PIX_FMT_RGB24 || avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            bytestream_put_be32(&s->buf, 16);
        } else if (s->ncomponents == 1) {
            bytestream_put_be32(&s->buf, 17);
        } else {
            bytestream_put_be32(&s->buf, 18);
        }
        update_size(chunkstart, s->buf);
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            int i;
            uint8_t *palette = pict->data[1];
            chunkstart = s->buf;
            bytestream_put_be32(&s->buf, 0);
            bytestream_put_buffer(&s->buf, "pclr", 4);
            bytestream_put_be16(&s->buf, AVPALETTE_COUNT);
            bytestream_put_byte(&s->buf, 3); // colour channels
            bytestream_put_be24(&s->buf, 0x070707); //colour depths
            for (i = 0; i < AVPALETTE_COUNT; i++) {
                bytestream_put_be24(&s->buf, HAVE_BIGENDIAN ? AV_RB24(palette + 1) : AV_RL24(palette));
                palette += 4;
            }
            update_size(chunkstart, s->buf);
            chunkstart = s->buf;
            bytestream_put_be32(&s->buf, 0);
            bytestream_put_buffer(&s->buf, "cmap", 4);
            for (i = 0; i < 3; i++) {
                bytestream_put_be16(&s->buf, 0); // component
                bytestream_put_byte(&s->buf, 1); // palette mapping
                bytestream_put_byte(&s->buf, i); // index
            }
            update_size(chunkstart, s->buf);
        }
        update_size(jp2hstart, s->buf);

        jp2cstart = s->buf;
        bytestream_put_be32(&s->buf, 0);
        bytestream_put_buffer(&s->buf, "jp2c", 4);
    }

    if (s->buf_end - s->buf < 2)
        return -1;
    bytestream_put_be16(&s->buf, JPEG2000_SOC);
    if ((ret = put_siz(s)) < 0)
        return ret;
    if ((ret = put_cod(s)) < 0)
        return ret;
    if ((ret = put_qcd(s, 0)) < 0)
        return ret;
    if ((ret = put_com(s, 0)) < 0)
        return ret;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        uint8_t *psotptr;
        if (!(psotptr = put_sot(s, tileno)))
            return -1;
        if (s->buf_end - s->buf < 2)
            return -1;
        bytestream_put_be16(&s->buf, JPEG2000_SOD);
        if ((ret = encode_tile(s, s->tile + tileno, tileno)) < 0)
            return ret;
        bytestream_put_be32(&psotptr, s->buf - psotptr + 6);
    }
    if (s->buf_end - s->buf < 2)
        return -1;
    bytestream_put_be16(&s->buf, JPEG2000_EOC);

    if (s->format == CODEC_JP2)
        update_size(jp2cstart, s->buf);

    av_log(s->avctx, AV_LOG_DEBUG, "end\n");
    pkt->size = s->buf - s->buf_start;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int j2kenc_init(AVCodecContext *avctx)
{
    int i, ret;
    Jpeg2000EncoderContext *s = avctx->priv_data;
    Jpeg2000CodingStyle *codsty = &s->codsty;
    Jpeg2000QuantStyle  *qntsty = &s->qntsty;

    s->avctx = avctx;
    av_log(s->avctx, AV_LOG_DEBUG, "init\n");

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->prediction_method)
        s->pred = avctx->prediction_method;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8 && (s->pred != FF_DWT97_INT || s->format != CODEC_JP2)) {
        av_log(s->avctx, AV_LOG_WARNING, "Forcing lossless jp2 for pal8\n");
        s->pred = FF_DWT97_INT;
        s->format = CODEC_JP2;
    }

    // defaults:
    // TODO: implement setting non-standard precinct size
    memset(codsty->log2_prec_widths , 15, sizeof(codsty->log2_prec_widths ));
    memset(codsty->log2_prec_heights, 15, sizeof(codsty->log2_prec_heights));
    codsty->nreslevels2decode=
    codsty->nreslevels       = 7;
    codsty->log2_cblk_width  = 4;
    codsty->log2_cblk_height = 4;
    codsty->transform        = s->pred ? FF_DWT53 : FF_DWT97_INT;

    qntsty->nguardbits       = 1;

    if ((s->tile_width  & (s->tile_width -1)) ||
        (s->tile_height & (s->tile_height-1))) {
        av_log(avctx, AV_LOG_WARNING, "Tile dimension not a power of 2\n");
    }

    if (codsty->transform == FF_DWT53)
        qntsty->quantsty = JPEG2000_QSTY_NONE;
    else
        qntsty->quantsty = JPEG2000_QSTY_SE;

    s->width = avctx->width;
    s->height = avctx->height;

    for (i = 0; i < 3; i++)
        s->cbps[i] = 8;

    if (avctx->pix_fmt == AV_PIX_FMT_RGB24){
        s->ncomponents = 3;
    } else if (avctx->pix_fmt == AV_PIX_FMT_GRAY8 || avctx->pix_fmt == AV_PIX_FMT_PAL8){
        s->ncomponents = 1;
    } else{ // planar YUV
        s->planar = 1;
        s->ncomponents = 3;
        ret = av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt,
                                               s->chroma_shift, s->chroma_shift + 1);
        if (ret)
            return ret;
    }

    ff_jpeg2000_init_tier1_luts();
    ff_mqc_init_context_tables();
    init_luts();

    init_quantization(s);
    if ((ret=init_tiles(s)) < 0)
        return ret;

    av_log(s->avctx, AV_LOG_DEBUG, "after init\n");

    return 0;
}

static int j2kenc_destroy(AVCodecContext *avctx)
{
    Jpeg2000EncoderContext *s = avctx->priv_data;

    cleanup(s);
    return 0;
}

// taken from the libopenjpeg wraper so it matches

#define OFFSET(x) offsetof(Jpeg2000EncoderContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "format",        "Codec Format",      OFFSET(format),        AV_OPT_TYPE_INT,   { .i64 = CODEC_JP2   }, CODEC_J2K, CODEC_JP2,   VE, "format"      },
    { "j2k",           NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CODEC_J2K   }, 0,         0,           VE, "format"      },
    { "jp2",           NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CODEC_JP2   }, 0,         0,           VE, "format"      },
    { "tile_width",    "Tile Width",        OFFSET(tile_width),    AV_OPT_TYPE_INT,   { .i64 = 256         }, 1,     1<<30,           VE, },
    { "tile_height",   "Tile Height",       OFFSET(tile_height),   AV_OPT_TYPE_INT,   { .i64 = 256         }, 1,     1<<30,           VE, },
    { "pred",          "DWT Type",          OFFSET(pred),          AV_OPT_TYPE_INT,   { .i64 = 0           }, 0,         1,           VE, "pred"        },
    { "dwt97int",      NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = 0           }, INT_MIN, INT_MAX,       VE, "pred"        },
    { "dwt53",         NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = 0           }, INT_MIN, INT_MAX,       VE, "pred"        },

    { NULL }
};

static const AVClass j2k_class = {
    .class_name = "jpeg 2000 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_jpeg2000_encoder = {
    .name           = "jpeg2000",
    .long_name      = NULL_IF_CONFIG_SMALL("JPEG 2000"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(Jpeg2000EncoderContext),
    .init           = j2kenc_init,
    .encode2        = encode_frame,
    .close          = j2kenc_destroy,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_YUV444P, AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_PAL8,
        AV_PIX_FMT_NONE
    },
    .priv_class     = &j2k_class,
};
