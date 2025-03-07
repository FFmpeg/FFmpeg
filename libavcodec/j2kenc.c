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
 * Copyright (c) 2020, Gautam Ramakrishnan <gautamramk@gmail.com>
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
#include "codec_internal.h"
#include "encode.h"
#include "bytestream.h"
#include "jpeg2000.h"
#include "version.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"
#include "libavutil/thread.h"

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
   double *layer_rates;
} Jpeg2000Tile;

typedef struct {
    AVClass *class;
    AVCodecContext *avctx;
    const AVFrame *picture;

    int width, height; ///< image width and height
    uint8_t cbps[4]; ///< bits per sample in particular components
    uint8_t comp_remap[4];
    int chroma_shift[2];
    uint8_t planar;
    int ncomponents;
    int tile_width, tile_height; ///< tile size
    int numXtiles, numYtiles;

    uint8_t *buf_start;
    uint8_t *buf;
    uint8_t *buf_end;
    int bit_index;

    uint64_t lambda;

    Jpeg2000CodingStyle codsty;
    Jpeg2000QuantStyle  qntsty;

    Jpeg2000Tile *tile;
    int layer_rates[100];
    uint8_t compression_rate_enc; ///< Is compression done using compression ratio?

    int format;
    int pred;
    int sop;
    int eph;
    int prog;
    int nlayers;
    char *lr_str;
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
    int sp = -1, curval = 0;

    while(node->parent){
        stack[++sp] = node;
        node = node->parent;
    }

    while (1) {
        if (curval > node->temp_val)
            node->temp_val = curval;
        else {
            curval = node->temp_val;
        }

        if (node->val >= threshold) {
            put_bits(s, 0, threshold - curval);
            curval = threshold;
        } else {
            put_bits(s, 0, node->val - curval);
            curval = node->val;
            if (!node->vis) {
                put_bits(s, 1, 1);
                node->vis = 1;
            }
        }

        node->temp_val = curval;
        if (sp < 0)
            break;
        node = stack[sp--];
    }
}

/** update the value in node */
static void tag_tree_update(Jpeg2000TgtNode *node)
{
    while (node->parent){
        if (node->parent->val <= node->val)
            break;
        node->parent->val = node->val;
        node = node->parent;
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
        bytestream_put_byte(&s->buf, s->cbps[i] - 1);
        bytestream_put_byte(&s->buf, (i+1&2)?1<<s->chroma_shift[0]:1);
        bytestream_put_byte(&s->buf, (i+1&2)?1<<s->chroma_shift[1]:1);
    }
    return 0;
}

static int put_cod(Jpeg2000EncoderContext *s)
{
    Jpeg2000CodingStyle *codsty = &s->codsty;
    uint8_t scod = 0;

    if (s->buf_end - s->buf < 14)
        return -1;

    bytestream_put_be16(&s->buf, JPEG2000_COD);
    bytestream_put_be16(&s->buf, 12); // Lcod
    if (s->sop)
        scod |= JPEG2000_CSTY_SOP;
    if (s->eph)
        scod |= JPEG2000_CSTY_EPH;
    bytestream_put_byte(&s->buf, scod);  // Scod
    // SGcod
    bytestream_put_byte(&s->buf, s->prog); // progression level
    bytestream_put_be16(&s->buf, s->nlayers); // num of layers
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

static void compute_rates(Jpeg2000EncoderContext* s)
{
    int i, j;
    int layno, compno;
    for (i = 0; i < s->numYtiles; i++) {
        for (j = 0; j < s->numXtiles; j++) {
            Jpeg2000Tile *tile = &s->tile[s->numXtiles * i + j];
            for (compno = 0; compno < s->ncomponents; compno++) {
                int tilew = tile->comp[compno].coord[0][1] - tile->comp[compno].coord[0][0];
                int tileh = tile->comp[compno].coord[1][1] - tile->comp[compno].coord[1][0];
                int scale = ((compno+1&2)?1 << s->chroma_shift[0]:1) * ((compno+1&2)?1 << s->chroma_shift[1]:1);
                for (layno = 0; layno < s->nlayers; layno++) {
                    if (s->layer_rates[layno] > 0) {
                        tile->layer_rates[layno] += (double)(tilew * tileh) * s->ncomponents * s->cbps[compno] /
                                                    (double)(s->layer_rates[layno] * 8 * scale);
                    } else {
                        tile->layer_rates[layno] = 0.0;
                    }
                }
            }
        }
    }

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

    s->tile = av_calloc(s->numXtiles, s->numYtiles * sizeof(Jpeg2000Tile));
    if (!s->tile)
        return AVERROR(ENOMEM);
    for (tileno = 0, tiley = 0; tiley < s->numYtiles; tiley++)
        for (tilex = 0; tilex < s->numXtiles; tilex++, tileno++){
            Jpeg2000Tile *tile = s->tile + tileno;

            tile->comp = av_calloc(s->ncomponents, sizeof(*tile->comp));
            if (!tile->comp)
                return AVERROR(ENOMEM);

            tile->layer_rates = av_calloc(s->nlayers, sizeof(*tile->layer_rates));
            if (!tile->layer_rates)
                return AVERROR(ENOMEM);

            for (compno = 0; compno < s->ncomponents; compno++){
                Jpeg2000Component *comp = tile->comp + compno;
                int ret, i, j;

                comp->coord[0][0] = comp->coord_o[0][0] = tilex * s->tile_width;
                comp->coord[0][1] = comp->coord_o[0][1] = FFMIN((tilex+1)*s->tile_width, s->width);
                comp->coord[1][0] = comp->coord_o[1][0] = tiley * s->tile_height;
                comp->coord[1][1] = comp->coord_o[1][1] = FFMIN((tiley+1)*s->tile_height, s->height);
                if (compno + 1 & 2)
                    for (i = 0; i < 2; i++)
                        for (j = 0; j < 2; j++)
                            comp->coord[i][j] = comp->coord_o[i][j] = ff_jpeg2000_ceildivpow2(comp->coord[i][j], s->chroma_shift[i]);

                if ((ret = ff_jpeg2000_init_component(comp,
                                                codsty,
                                                qntsty,
                                                s->cbps[compno],
                                                (compno+1&2)?1<<s->chroma_shift[0]:1,
                                                (compno+1&2)?1<<s->chroma_shift[1]:1,
                                                s->avctx
                                               )) < 0)
                    return ret;
            }
        }
    compute_rates(s);
    return 0;
}

#define COPY_FRAME(D, PIXEL)                                                                                                \
    static void copy_frame_ ##D(Jpeg2000EncoderContext *s)                                                                  \
    {                                                                                                                       \
        int tileno, compno, i, y, x;                                                                                        \
        const PIXEL *line;                                                                                                  \
        for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){                                                   \
            Jpeg2000Tile *tile = s->tile + tileno;                                                                          \
            if (s->planar){                                                                                                 \
                for (compno = 0; compno < s->ncomponents; compno++){                                                        \
                    int icompno = s->comp_remap[compno];                                                                    \
                    Jpeg2000Component *comp = tile->comp + compno;                                                          \
                    int *dst = comp->i_data;                                                                                \
                    int cbps = s->cbps[compno];                                                                             \
                    line = (const PIXEL*)s->picture->data[icompno]                                                           \
                           + comp->coord[1][0] * (s->picture->linesize[icompno] / sizeof(PIXEL))                             \
                           + comp->coord[0][0];                                                                             \
                    for (y = comp->coord[1][0]; y < comp->coord[1][1]; y++){                                                \
                        const PIXEL *ptr = line;                                                                            \
                        for (x = comp->coord[0][0]; x < comp->coord[0][1]; x++)                                             \
                            *dst++ = *ptr++ - (1 << (cbps - 1));                                                            \
                        line += s->picture->linesize[icompno] / sizeof(PIXEL);                                               \
                    }                                                                                                       \
                }                                                                                                           \
            } else{                                                                                                         \
                line = (const PIXEL*)(s->picture->data[0] + tile->comp[0].coord[1][0] * s->picture->linesize[0])            \
                       + tile->comp[0].coord[0][0] * s->ncomponents;                                                        \
                                                                                                                            \
                i = 0;                                                                                                      \
                for (y = tile->comp[0].coord[1][0]; y < tile->comp[0].coord[1][1]; y++){                                    \
                    const PIXEL *ptr = line;                                                                                \
                    for (x = tile->comp[0].coord[0][0]; x < tile->comp[0].coord[0][1]; x++, i++){                           \
                        for (compno = 0; compno < s->ncomponents; compno++){                                                \
                            int cbps = s->cbps[compno];                                                                     \
                            tile->comp[compno].i_data[i] = *ptr++  - (1 << (cbps - 1));                                     \
                        }                                                                                                   \
                    }                                                                                                       \
                    line += s->picture->linesize[0] / sizeof(PIXEL);                                                        \
                }                                                                                                           \
            }                                                                                                               \
        }                                                                                                                   \
    }

COPY_FRAME(8, uint8_t)
COPY_FRAME(16, uint16_t)

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
    ff_jpeg2000_init_tier1_luts();
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
    } else{
        cblk->nonzerobits = av_log2(max) + 1 - NMSEDEC_FRACBITS;
    }
    bpno = cblk->nonzerobits - 1;

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
        cblk->passes[passno].rate -= cblk->passes[passno].flushed_len;

        wmsedec += (int64_t)nmsedec << (2*bpno);
        cblk->passes[passno].disto = wmsedec;

        if (++pass_t == 3){
            pass_t = 0;
            bpno--;
        }
    }
    cblk->npasses = passno;
    cblk->ninclpasses = passno;

    if (passno) {
        cblk->passes[passno-1].rate = ff_mqc_flush_to(&t1->mqc, cblk->passes[passno-1].flushed, &cblk->passes[passno-1].flushed_len);
        cblk->passes[passno-1].rate -= cblk->passes[passno-1].flushed_len;
    }
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


static int encode_packet(Jpeg2000EncoderContext *s, Jpeg2000ResLevel *rlevel, int layno,
                         int precno, const uint8_t *expn, int numgbits, int packetno,
                         int nlayers)
{
    int bandno, empty = 1;
    int i;
    // init bitstream
    *s->buf = 0;
    s->bit_index = 0;

    if (s->sop) {
        bytestream_put_be16(&s->buf, JPEG2000_SOP);
        bytestream_put_be16(&s->buf, 4);
        bytestream_put_be16(&s->buf, packetno);
    }
    // header

    if (!layno) {
        for (bandno = 0; bandno < rlevel->nbands; bandno++) {
            Jpeg2000Band *band = rlevel->band + bandno;
            if (band->coord[0][0] < band->coord[0][1]
            &&  band->coord[1][0] < band->coord[1][1]) {
                Jpeg2000Prec *prec = band->prec + precno;
                int nb_cblks = prec->nb_codeblocks_height * prec->nb_codeblocks_width;
                int pos;
                ff_tag_tree_zero(prec->zerobits, prec->nb_codeblocks_width, prec->nb_codeblocks_height, 99);
                ff_tag_tree_zero(prec->cblkincl, prec->nb_codeblocks_width, prec->nb_codeblocks_height, 99);
                for (pos = 0; pos < nb_cblks; pos++) {
                    Jpeg2000Cblk *cblk = &prec->cblk[pos];
                    prec->zerobits[pos].val = expn[bandno] + numgbits - 1 - cblk->nonzerobits;
                    cblk->incl = 0;
                    cblk->lblock = 3;
                    tag_tree_update(prec->zerobits + pos);
                    for (i = 0; i < nlayers; i++) {
                        if (cblk->layers[i].npasses > 0) {
                            prec->cblkincl[pos].val = i;
                            break;
                        }
                    }
                    if (i == nlayers)
                        prec->cblkincl[pos].val = i;
                    tag_tree_update(prec->cblkincl + pos);
                }
            }
        }
    }

    // is the packet empty?
    for (bandno = 0; bandno < rlevel->nbands; bandno++){
        Jpeg2000Band *band = rlevel->band + bandno;
        if (band->coord[0][0] < band->coord[0][1]
        &&  band->coord[1][0] < band->coord[1][1]) {
            Jpeg2000Prec *prec = band->prec + precno;
            int nb_cblks = prec->nb_codeblocks_height * prec->nb_codeblocks_width;
            int pos;
            for (pos = 0; pos < nb_cblks; pos++) {
                Jpeg2000Cblk *cblk = &prec->cblk[pos];
                if (cblk->layers[layno].npasses) {
                    empty = 0;
                    break;
                }
            }
            if (!empty)
                break;
        }
    }

    put_bits(s, !empty, 1);
    if (empty){
        j2k_flush(s);
        if (s->eph)
            bytestream_put_be16(&s->buf, JPEG2000_EPH);
        return 0;
    }

    for (bandno = 0; bandno < rlevel->nbands; bandno++) {
        Jpeg2000Band *band = rlevel->band + bandno;
        Jpeg2000Prec *prec = band->prec + precno;
        int yi, xi, pos;
        int cblknw = prec->nb_codeblocks_width;

        if (band->coord[0][0] == band->coord[0][1]
        ||  band->coord[1][0] == band->coord[1][1])
            continue;

        for (pos=0, yi = 0; yi < prec->nb_codeblocks_height; yi++) {
            for (xi = 0; xi < cblknw; xi++, pos++){
                int llen = 0, length;
                Jpeg2000Cblk *cblk = prec->cblk + yi * cblknw + xi;

                if (s->buf_end - s->buf < 20) // approximately
                    return -1;

                // inclusion information
                if (!cblk->incl)
                    tag_tree_code(s, prec->cblkincl + pos, layno + 1);
                else {
                    put_bits(s, cblk->layers[layno].npasses > 0, 1);
                }

                if (!cblk->layers[layno].npasses)
                    continue;

                // zerobits information
                if (!cblk->incl) {
                    tag_tree_code(s, prec->zerobits + pos, 100);
                    cblk->incl = 1;
                }

                // number of passes
                putnumpasses(s, cblk->layers[layno].npasses);

                length = cblk->layers[layno].data_len;
                if (layno == nlayers - 1 && cblk->layers[layno].cum_passes){
                    length += cblk->passes[cblk->layers[layno].cum_passes-1].flushed_len;
                }
                if (cblk->lblock + av_log2(cblk->layers[layno].npasses) < av_log2(length) + 1) {
                    llen = av_log2(length) + 1 - cblk->lblock - av_log2(cblk->layers[layno].npasses);
                }

                // length of code block
                cblk->lblock += llen;
                put_bits(s, 1, llen);
                put_bits(s, 0, 1);
                put_num(s, length, cblk->lblock + av_log2(cblk->layers[layno].npasses));
            }
        }
    }
    j2k_flush(s);
    if (s->eph) {
        bytestream_put_be16(&s->buf, JPEG2000_EPH);
    }

    for (bandno = 0; bandno < rlevel->nbands; bandno++) {
        Jpeg2000Band *band = rlevel->band + bandno;
        Jpeg2000Prec *prec = band->prec + precno;
        int yi, cblknw = prec->nb_codeblocks_width;
        for (yi =0; yi < prec->nb_codeblocks_height; yi++) {
            int xi;
            for (xi = 0; xi < cblknw; xi++){
                Jpeg2000Cblk *cblk = prec->cblk + yi * cblknw + xi;
                if (cblk->layers[layno].npasses) {
                    if (s->buf_end - s->buf < cblk->layers[layno].data_len + 2)
                        return -1;
                    bytestream_put_buffer(&s->buf, cblk->layers[layno].data_start + 1, cblk->layers[layno].data_len);
                    if (layno == nlayers - 1 && cblk->layers[layno].cum_passes) {
                        bytestream_put_buffer(&s->buf, cblk->passes[cblk->layers[layno].cum_passes-1].flushed,
                                                       cblk->passes[cblk->layers[layno].cum_passes-1].flushed_len);
                    }
                }
            }
        }
    }
    return 0;
}

static int encode_packets(Jpeg2000EncoderContext *s, Jpeg2000Tile *tile, int tileno, int nlayers)
{
    int compno, reslevelno, layno, ret;
    Jpeg2000CodingStyle *codsty = &s->codsty;
    Jpeg2000QuantStyle  *qntsty = &s->qntsty;
    int packetno = 0;
    int step_x, step_y;
    int x, y;
    int tile_coord[2][2];
    int col = tileno % s->numXtiles;
    int row = tileno / s->numXtiles;

    tile_coord[0][0] = col * s->tile_width;
    tile_coord[0][1] = FFMIN(tile_coord[0][0] + s->tile_width, s->width);
    tile_coord[1][0] = row * s->tile_height;
    tile_coord[1][1] = FFMIN(tile_coord[1][0] + s->tile_height, s->height);

    av_log(s->avctx, AV_LOG_DEBUG, "tier2\n");
    // lay-rlevel-comp-pos progression
    switch (s->prog) {
    case JPEG2000_PGOD_LRCP:
    for (layno = 0; layno < nlayers; layno++) {
        for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
            for (compno = 0; compno < s->ncomponents; compno++){
                int precno;
                Jpeg2000ResLevel *reslevel = s->tile[tileno].comp[compno].reslevel + reslevelno;
                for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                    if ((ret = encode_packet(s, reslevel, layno, precno, qntsty->expn + (reslevelno ? 3*reslevelno-2 : 0),
                                qntsty->nguardbits, packetno++, nlayers)) < 0)
                        return ret;
                }
            }
        }
    }
    break;
    case JPEG2000_PGOD_RLCP:
    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
        for (layno = 0; layno < nlayers; layno++) {
            for (compno = 0; compno < s->ncomponents; compno++){
                int precno;
                Jpeg2000ResLevel *reslevel = s->tile[tileno].comp[compno].reslevel + reslevelno;
                for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                    if ((ret = encode_packet(s, reslevel, layno, precno, qntsty->expn + (reslevelno ? 3*reslevelno-2 : 0),
                                qntsty->nguardbits, packetno++, nlayers)) < 0)
                        return ret;
                }
            }
        }
    }
    break;
    case JPEG2000_PGOD_RPCL:
    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++) {
        int precno;
        step_x = 30;
        step_y = 30;
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp     = tile->comp + compno;
            if (reslevelno < codsty->nreslevels) {
                uint8_t reducedresno = codsty->nreslevels - 1 -reslevelno; //  ==> N_L - r
                Jpeg2000ResLevel *rlevel = comp->reslevel + reslevelno;
                step_x = FFMIN(step_x, rlevel->log2_prec_width  + reducedresno);
                step_y = FFMIN(step_y, rlevel->log2_prec_height + reducedresno);
            }
        }

        step_x = 1<<step_x;
        step_y = 1<<step_y;
        for (y = tile_coord[1][0]; y < tile_coord[1][1]; y = (y/step_y + 1)*step_y) {
            for (x = tile_coord[0][0]; x < tile_coord[0][1]; x = (x/step_x + 1)*step_x) {
                for (compno = 0; compno < s->ncomponents; compno++) {
                    Jpeg2000Component *comp     = tile->comp + compno;
                    uint8_t reducedresno = codsty->nreslevels - 1 -reslevelno; //  ==> N_L - r
                    Jpeg2000ResLevel *reslevel = comp->reslevel + reslevelno;
                    int log_subsampling[2] = { (compno+1&2)?s->chroma_shift[0]:0, (compno+1&2)?s->chroma_shift[1]:0};
                    unsigned prcx, prcy;
                    int trx0, try0;

                    trx0 = ff_jpeg2000_ceildivpow2(tile_coord[0][0], log_subsampling[0] + reducedresno);
                    try0 = ff_jpeg2000_ceildivpow2(tile_coord[1][0], log_subsampling[1] + reducedresno);

                    if (!(y % ((uint64_t)1 << (reslevel->log2_prec_height + reducedresno + log_subsampling[1])) == 0 ||
                        (y == tile_coord[1][0] && (try0 << reducedresno) % (1U << (reducedresno + reslevel->log2_prec_height)))))
                        continue;

                    if (!(x % ((uint64_t)1 << (reslevel->log2_prec_width + reducedresno + log_subsampling[0])) == 0 ||
                        (x == tile_coord[0][0] && (trx0 << reducedresno) % (1U << (reducedresno + reslevel->log2_prec_width)))))
                        continue;

                    // check if a precinct exists
                    prcx   = ff_jpeg2000_ceildivpow2(x, log_subsampling[0] + reducedresno) >> reslevel->log2_prec_width;
                    prcy   = ff_jpeg2000_ceildivpow2(y, log_subsampling[1] + reducedresno) >> reslevel->log2_prec_height;
                    prcx  -= ff_jpeg2000_ceildivpow2(comp->coord_o[0][0], reducedresno) >> reslevel->log2_prec_width;
                    prcy  -= ff_jpeg2000_ceildivpow2(comp->coord_o[1][0], reducedresno) >> reslevel->log2_prec_height;
                    precno = prcx + reslevel->num_precincts_x * prcy;

                    if (prcx >= reslevel->num_precincts_x || prcy >= reslevel->num_precincts_y) {
                        av_log(s->avctx, AV_LOG_WARNING, "prc %d %d outside limits %d %d\n",
                               prcx, prcy, reslevel->num_precincts_x, reslevel->num_precincts_y);
                        continue;
                    }
                    for (layno = 0; layno < nlayers; layno++) {
                        if ((ret = encode_packet(s, reslevel, layno, precno, qntsty->expn + (reslevelno ? 3*reslevelno-2 : 0),
                                qntsty->nguardbits, packetno++, nlayers)) < 0)
                            return ret;
                        }
                    }
                }
            }
    }
    break;
    case JPEG2000_PGOD_PCRL:
        step_x = 32;
        step_y = 32;
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp     = tile->comp + compno;

            for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++) {
                uint8_t reducedresno = codsty->nreslevels - 1 -reslevelno; //  ==> N_L - r
                Jpeg2000ResLevel *rlevel = comp->reslevel + reslevelno;
                step_x = FFMIN(step_x, rlevel->log2_prec_width  + reducedresno);
                step_y = FFMIN(step_y, rlevel->log2_prec_height + reducedresno);
            }
        }
        if (step_x >= 31 || step_y >= 31){
            avpriv_request_sample(s->avctx, "PCRL with large step");
            return AVERROR_PATCHWELCOME;
        }
        step_x = 1<<step_x;
        step_y = 1<<step_y;

        for (y = tile_coord[1][0]; y < tile_coord[1][1]; y = (y/step_y + 1)*step_y) {
            for (x = tile_coord[0][0]; x < tile_coord[0][1]; x = (x/step_x + 1)*step_x) {
                for (compno = 0; compno < s->ncomponents; compno++) {
                    Jpeg2000Component *comp     = tile->comp + compno;
                    int log_subsampling[2] = { (compno+1&2)?s->chroma_shift[0]:0, (compno+1&2)?s->chroma_shift[1]:0};

                    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++) {
                        unsigned prcx, prcy;
                        int precno;
                        uint8_t reducedresno = codsty->nreslevels - 1 -reslevelno; //  ==> N_L - r
                        Jpeg2000ResLevel *reslevel = comp->reslevel + reslevelno;
                        int trx0, try0;

                        trx0 = ff_jpeg2000_ceildivpow2(tile_coord[0][0], log_subsampling[0] + reducedresno);
                        try0 = ff_jpeg2000_ceildivpow2(tile_coord[1][0], log_subsampling[1] + reducedresno);

                        if (!(y % ((uint64_t)1 << (reslevel->log2_prec_height + reducedresno + log_subsampling[1])) == 0 ||
                            (y == tile_coord[1][0] && (try0 << reducedresno) % (1U << (reducedresno + reslevel->log2_prec_height)))))
                            continue;

                        if (!(x % ((uint64_t)1 << (reslevel->log2_prec_width + reducedresno + log_subsampling[0])) == 0 ||
                            (x == tile_coord[0][0] && (trx0 << reducedresno) % (1U << (reducedresno + reslevel->log2_prec_width)))))
                            continue;

                        // check if a precinct exists
                        prcx   = ff_jpeg2000_ceildivpow2(x, log_subsampling[0] + reducedresno) >> reslevel->log2_prec_width;
                        prcy   = ff_jpeg2000_ceildivpow2(y, log_subsampling[1] + reducedresno) >> reslevel->log2_prec_height;
                        prcx  -= ff_jpeg2000_ceildivpow2(comp->coord_o[0][0], reducedresno) >> reslevel->log2_prec_width;
                        prcy  -= ff_jpeg2000_ceildivpow2(comp->coord_o[1][0], reducedresno) >> reslevel->log2_prec_height;

                        precno = prcx + reslevel->num_precincts_x * prcy;

                        if (prcx >= reslevel->num_precincts_x || prcy >= reslevel->num_precincts_y) {
                            av_log(s->avctx, AV_LOG_WARNING, "prc %d %d outside limits %d %d\n",
                                   prcx, prcy, reslevel->num_precincts_x, reslevel->num_precincts_y);
                            continue;
                        }
                        for (layno = 0; layno < nlayers; layno++) {
                            if ((ret = encode_packet(s, reslevel, layno, precno, qntsty->expn + (reslevelno ? 3*reslevelno-2 : 0),
                                    qntsty->nguardbits, packetno++, nlayers)) < 0)
                                return ret;
                        }
                    }
                }
            }
        }
    break;
    case JPEG2000_PGOD_CPRL:
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp     = tile->comp + compno;
            int log_subsampling[2] = { (compno+1&2)?s->chroma_shift[0]:0, (compno+1&2)?s->chroma_shift[1]:0};
            step_x = 32;
            step_y = 32;

            for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++) {
                uint8_t reducedresno = codsty->nreslevels - 1 -reslevelno; //  ==> N_L - r
                Jpeg2000ResLevel *rlevel = comp->reslevel + reslevelno;
                step_x = FFMIN(step_x, rlevel->log2_prec_width  + reducedresno);
                step_y = FFMIN(step_y, rlevel->log2_prec_height + reducedresno);
            }
            if (step_x >= 31 || step_y >= 31){
                avpriv_request_sample(s->avctx, "CPRL with large step");
                return AVERROR_PATCHWELCOME;
            }
            step_x = 1<<step_x;
            step_y = 1<<step_y;

            for (y = tile_coord[1][0]; y < tile_coord[1][1]; y = (y/step_y + 1)*step_y) {
                for (x = tile_coord[0][0]; x < tile_coord[0][1]; x = (x/step_x + 1)*step_x) {
                    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++) {
                        unsigned prcx, prcy;
                        int precno;
                        int trx0, try0;
                        uint8_t reducedresno = codsty->nreslevels - 1 -reslevelno; //  ==> N_L - r
                        Jpeg2000ResLevel *reslevel = comp->reslevel + reslevelno;

                        trx0 = ff_jpeg2000_ceildivpow2(tile_coord[0][0], log_subsampling[0] + reducedresno);
                        try0 = ff_jpeg2000_ceildivpow2(tile_coord[1][0], log_subsampling[1] + reducedresno);

                        if (!(y % ((uint64_t)1 << (reslevel->log2_prec_height + reducedresno + log_subsampling[1])) == 0 ||
                            (y == tile_coord[1][0] && (try0 << reducedresno) % (1U << (reducedresno + reslevel->log2_prec_height)))))
                            continue;

                        if (!(x % ((uint64_t)1 << (reslevel->log2_prec_width + reducedresno + log_subsampling[0])) == 0 ||
                            (x == tile_coord[0][0] && (trx0 << reducedresno) % (1U << (reducedresno + reslevel->log2_prec_width)))))
                            continue;

                        // check if a precinct exists
                        prcx   = ff_jpeg2000_ceildivpow2(x, log_subsampling[0] + reducedresno) >> reslevel->log2_prec_width;
                        prcy   = ff_jpeg2000_ceildivpow2(y, log_subsampling[1] + reducedresno) >> reslevel->log2_prec_height;
                        prcx  -= ff_jpeg2000_ceildivpow2(comp->coord_o[0][0], reducedresno) >> reslevel->log2_prec_width;
                        prcy  -= ff_jpeg2000_ceildivpow2(comp->coord_o[1][0], reducedresno) >> reslevel->log2_prec_height;

                        precno = prcx + reslevel->num_precincts_x * prcy;

                        if (prcx >= reslevel->num_precincts_x || prcy >= reslevel->num_precincts_y) {
                            av_log(s->avctx, AV_LOG_WARNING, "prc %d %d outside limits %d %d\n",
                                   prcx, prcy, reslevel->num_precincts_x, reslevel->num_precincts_y);
                            continue;
                        }
                        for (layno = 0; layno < nlayers; layno++) {
                            if ((ret = encode_packet(s, reslevel, layno, precno, qntsty->expn + (reslevelno ? 3*reslevelno-2 : 0),
                                    qntsty->nguardbits, packetno++, nlayers)) < 0)
                                return ret;
                        }
                    }
                }
            }
        }

    }

    av_log(s->avctx, AV_LOG_DEBUG, "after tier2\n");
    return 0;
}

static void makelayer(Jpeg2000EncoderContext *s, int layno, double thresh, Jpeg2000Tile* tile, int final)
{
    int compno, resno, bandno, precno, cblkno;
    int passno;

    for (compno = 0; compno < s->ncomponents; compno++) {
        Jpeg2000Component *comp = &tile->comp[compno];

        for (resno = 0; resno < s->codsty.nreslevels; resno++) {
            Jpeg2000ResLevel *reslevel = comp->reslevel + resno;

            for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                for (bandno = 0; bandno < reslevel->nbands ; bandno++){
                    Jpeg2000Band *band = reslevel->band + bandno;
                    Jpeg2000Prec *prec = band->prec + precno;

                    for (cblkno = 0; cblkno < prec->nb_codeblocks_height * prec->nb_codeblocks_width; cblkno++){
                        Jpeg2000Cblk *cblk = prec->cblk + cblkno;
                        Jpeg2000Layer *layer = &cblk->layers[layno];
                        int n;

                        if (layno == 0) {
                            cblk->ninclpasses = 0;
                        }

                        n = cblk->ninclpasses;

                        if (thresh < 0) {
                            n = cblk->npasses;
                        } else {
                            for (passno = cblk->ninclpasses; passno < cblk->npasses; passno++) {
                                int32_t dr;
                                double dd;
                                Jpeg2000Pass *pass = &cblk->passes[passno];

                                if (n == 0) {
                                    dr = pass->rate;
                                    dd = pass->disto;
                                } else {
                                    dr = pass->rate - cblk->passes[n - 1].rate;
                                    dd = pass->disto - cblk->passes[n-1].disto;
                                }

                                if (!dr) {
                                    if (dd != 0.0) {
                                        n = passno + 1;
                                    }
                                    continue;
                                }

                                if (thresh - (dd / dr) < DBL_EPSILON)
                                    n = passno + 1;
                            }
                        }
                        layer->npasses = n - cblk->ninclpasses;
                        layer->cum_passes = n;

                        if (layer->npasses == 0) {
                            layer->disto = 0;
                            layer->data_len = 0;
                            continue;
                        }

                        if (cblk->ninclpasses == 0) {
                            layer->data_len = cblk->passes[n - 1].rate;
                            layer->data_start = cblk->data;
                            layer->disto = cblk->passes[n - 1].disto;
                        } else {
                            layer->data_len = cblk->passes[n - 1].rate - cblk->passes[cblk->ninclpasses - 1].rate;
                            layer->data_start = cblk->data + cblk->passes[cblk->ninclpasses - 1].rate;
                            layer->disto = cblk->passes[n - 1].disto -
                                           cblk->passes[cblk->ninclpasses - 1].disto;
                        }
                        if (final) {
                            cblk->ninclpasses = n;
                        }
                    }
                }
            }
        }
    }
}

static void makelayers(Jpeg2000EncoderContext *s, Jpeg2000Tile *tile)
{
    int precno, compno, reslevelno, bandno, cblkno, lev, passno, layno;
    int i;
    double min = DBL_MAX;
    double max = 0;
    double thresh;

    Jpeg2000CodingStyle *codsty = &s->codsty;

    for (compno = 0; compno < s->ncomponents; compno++){
        Jpeg2000Component *comp = tile->comp + compno;

        for (reslevelno = 0, lev = codsty->nreslevels-1; reslevelno < codsty->nreslevels; reslevelno++, lev--){
            Jpeg2000ResLevel *reslevel = comp->reslevel + reslevelno;

            for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                for (bandno = 0; bandno < reslevel->nbands ; bandno++){
                    Jpeg2000Band *band = reslevel->band + bandno;
                    Jpeg2000Prec *prec = band->prec + precno;

                    for (cblkno = 0; cblkno < prec->nb_codeblocks_height * prec->nb_codeblocks_width; cblkno++){
                        Jpeg2000Cblk *cblk = prec->cblk + cblkno;
                        for (passno = 0; passno < cblk->npasses; passno++) {
                            Jpeg2000Pass *pass = &cblk->passes[passno];
                            int dr;
                            double dd, drslope;

                            if (passno == 0) {
                                dr = (int32_t)pass->rate;
                                dd = pass->disto;
                            } else {
                                dr = (int32_t)(pass->rate - cblk->passes[passno - 1].rate);
                                dd = pass->disto - cblk->passes[passno - 1].disto;
                            }

                            if (dr <= 0)
                                continue;

                            drslope = dd / dr;
                            if (drslope < min)
                                min = drslope;

                            if (drslope > max)
                                max = drslope;
                        }
                    }
                }
            }
        }
    }

    for (layno = 0; layno < s->nlayers; layno++) {
        double lo = min;
        double hi = max;
        double stable_thresh = 0.0;
        double good_thresh = 0.0;
        if (!s->layer_rates[layno]) {
            good_thresh = -1.0;
        } else {
            for (i = 0; i < 128; i++) {
                uint8_t *stream_pos = s->buf;
                int ret;
                thresh = (lo + hi) / 2;
                makelayer(s, layno, thresh, tile, 0);
                ret = encode_packets(s, tile, (int)(tile - s->tile), layno + 1);
                memset(stream_pos, 0, s->buf - stream_pos);
                if ((s->buf - stream_pos > ceil(tile->layer_rates[layno])) || ret < 0) {
                    lo = thresh;
                    s->buf = stream_pos;
                    continue;
                }
                hi = thresh;
                stable_thresh = thresh;
                s->buf = stream_pos;
            }
        }
        if (good_thresh >= 0.0)
            good_thresh = stable_thresh == 0.0 ? thresh : stable_thresh;
        makelayer(s, layno, good_thresh, tile, 1);
    }
}

static int getcut(Jpeg2000Cblk *cblk, uint64_t lambda)
{
    int passno, res = 0;
    for (passno = 0; passno < cblk->npasses; passno++){
        int dr;
        int64_t dd;

        dr = cblk->passes[passno].rate
           - (res ? cblk->passes[res-1].rate : 0);
        dd = cblk->passes[passno].disto
           - (res ? cblk->passes[res-1].disto : 0);

        if (dd  >= dr * lambda)
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

                    int64_t dwt_norm = dwt_norms[codsty->transform == FF_DWT53][bandpos][lev] * (int64_t)band->i_stepsize >> 15;
                    int64_t lambda_prime = av_rescale(s->lambda, 1 << WMSEDEC_SHIFT, dwt_norm * dwt_norm);
                    for (cblkno = 0; cblkno < prec->nb_codeblocks_height * prec->nb_codeblocks_width; cblkno++){
                        Jpeg2000Cblk *cblk = prec->cblk + cblkno;

                        cblk->ninclpasses = getcut(cblk, lambda_prime);
                        cblk->layers[0].data_start = cblk->data;
                        cblk->layers[0].cum_passes = cblk->ninclpasses;
                        cblk->layers[0].npasses = cblk->ninclpasses;
                        if (cblk->ninclpasses)
                            cblk->layers[0].data_len = cblk->passes[cblk->ninclpasses - 1].rate;
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
    if (s->compression_rate_enc)
        makelayers(s, tile);
    else
        truncpasses(s, tile);

    if ((ret = encode_packets(s, tile, tileno, s->nlayers)) < 0)
        return ret;
    av_log(s->avctx, AV_LOG_DEBUG, "after rate control\n");
    return 0;
}

static void cleanup(Jpeg2000EncoderContext *s)
{
    int tileno, compno;
    Jpeg2000CodingStyle *codsty = &s->codsty;

    if (!s->tile)
        return;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        if (s->tile[tileno].comp) {
            for (compno = 0; compno < s->ncomponents; compno++){
                Jpeg2000Component *comp = s->tile[tileno].comp + compno;
                ff_jpeg2000_cleanup(comp, codsty);
            }
            av_freep(&s->tile[tileno].comp);
        }
        av_freep(&s->tile[tileno].layer_rates);
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
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    if ((ret = ff_alloc_packet(avctx, pkt, avctx->width*avctx->height*9 + FF_INPUT_BUFFER_MIN_SIZE)) < 0)
        return ret;

    // init:
    s->buf = s->buf_start = pkt->data;
    s->buf_end = pkt->data + pkt->size;

    s->picture = pict;

    s->lambda = s->picture->quality * LAMBDA_SCALE;

    if (s->cbps[0] > 8)
        copy_frame_16(s);
    else
        copy_frame_8(s);

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
        if ((desc->flags & AV_PIX_FMT_FLAG_RGB) || avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            bytestream_put_be32(&s->buf, 16);
        } else if (s->ncomponents == 1) {
            bytestream_put_be32(&s->buf, 17);
        } else {
            bytestream_put_be32(&s->buf, 18);
        }
        update_size(chunkstart, s->buf);
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            int i;
            const uint8_t *palette = pict->data[1];
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
    *got_packet = 1;

    return 0;
}

static int parse_layer_rates(Jpeg2000EncoderContext *s)
{
    int i;
    char *token;
    char *saveptr = NULL;
    int rate;
    int nlayers = 0;
    if (!s->lr_str) {
        s->nlayers = 1;
        s->layer_rates[0] = 0;
        s->compression_rate_enc = 0;
        return 0;
    }

    token = av_strtok(s->lr_str, ",", &saveptr);
    if (token && (rate = strtol(token, NULL, 10))) {
            s->layer_rates[0] = rate <= 1 ? 0:rate;
            nlayers++;
    } else {
            return AVERROR_INVALIDDATA;
    }

    while (1) {
        token = av_strtok(NULL, ",", &saveptr);
        if (!token)
            break;
        if (rate = strtol(token, NULL, 10)) {
            if (nlayers >= 100) {
                return AVERROR_INVALIDDATA;
            }
            s->layer_rates[nlayers] = rate <= 1 ? 0:rate;
            nlayers++;
        } else {
            return AVERROR_INVALIDDATA;
        }
    }

    for (i = 1; i < nlayers; i++) {
        if (s->layer_rates[i] >= s->layer_rates[i-1]) {
            return AVERROR_INVALIDDATA;
        }
    }
    s->nlayers = nlayers;
    s->compression_rate_enc = 1;
    return 0;
}

static av_cold int j2kenc_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    int i, ret;
    Jpeg2000EncoderContext *s = avctx->priv_data;
    Jpeg2000CodingStyle *codsty = &s->codsty;
    Jpeg2000QuantStyle  *qntsty = &s->qntsty;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    s->avctx = avctx;
    av_log(s->avctx, AV_LOG_DEBUG, "init\n");
    if (parse_layer_rates(s)) {
        av_log(avctx, AV_LOG_WARNING, "Layer rates invalid. Encoding with 1 layer based on quality metric.\n");
        s->nlayers = 1;
        s->layer_rates[0] = 0;
        s->compression_rate_enc = 0;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8 && (s->pred != FF_DWT97_INT || s->format != CODEC_JP2)) {
        av_log(s->avctx, AV_LOG_WARNING, "Forcing lossless jp2 for pal8\n");
        s->pred = 1;
        s->format = CODEC_JP2;
    }

    // defaults:
    // TODO: implement setting non-standard precinct size
    memset(codsty->log2_prec_widths , 15, sizeof(codsty->log2_prec_widths ));
    memset(codsty->log2_prec_heights, 15, sizeof(codsty->log2_prec_heights));
    codsty->nreslevels2decode=
    codsty->nreslevels       = 7;
    codsty->nlayers          = s->nlayers;
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

    s->ncomponents = desc->nb_components;
    for (i = 0; i < 4; i++) {
        s->cbps[i] = desc->comp[i].depth;
        s->comp_remap[i] = i; //default
    }

    if ((desc->flags & AV_PIX_FMT_FLAG_PLANAR) && s->ncomponents > 1) {
        s->planar = 1;
        ret = av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt,
                                               s->chroma_shift, s->chroma_shift + 1);
        if (ret)
            return ret;
        if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
            s->comp_remap[0] = 2;
            s->comp_remap[1] = 0;
            s->comp_remap[2] = 1;
        }
    }

    ff_thread_once(&init_static_once, init_luts);

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
    { "format",        "Codec Format",      OFFSET(format),        AV_OPT_TYPE_INT,   { .i64 = CODEC_JP2   }, CODEC_J2K, CODEC_JP2,   VE, .unit = "format"      },
    { "j2k",           NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CODEC_J2K   }, 0,         0,           VE, .unit = "format"      },
    { "jp2",           NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CODEC_JP2   }, 0,         0,           VE, .unit = "format"      },
    { "tile_width",    "Tile Width",        OFFSET(tile_width),    AV_OPT_TYPE_INT,   { .i64 = 256         }, 1,     1<<30,           VE, },
    { "tile_height",   "Tile Height",       OFFSET(tile_height),   AV_OPT_TYPE_INT,   { .i64 = 256         }, 1,     1<<30,           VE, },
    { "pred",          "DWT Type",          OFFSET(pred),          AV_OPT_TYPE_INT,   { .i64 = 0           }, 0,         1,           VE, .unit = "pred"        },
    { "dwt97int",      NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = 0           }, INT_MIN, INT_MAX,       VE, .unit = "pred"        },
    { "dwt53",         NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = 1           }, INT_MIN, INT_MAX,       VE, .unit = "pred"        },
    { "sop",           "SOP marker",        OFFSET(sop),           AV_OPT_TYPE_INT,   { .i64 = 0           }, 0,         1,           VE, },
    { "eph",           "EPH marker",        OFFSET(eph),           AV_OPT_TYPE_INT,   { .i64 = 0           }, 0,         1,           VE, },
    { "prog",          "Progression Order", OFFSET(prog),          AV_OPT_TYPE_INT,   { .i64 = 0           }, JPEG2000_PGOD_LRCP,         JPEG2000_PGOD_CPRL,           VE, .unit = "prog" },
    { "lrcp",          NULL,                0,                     AV_OPT_TYPE_CONST,  { .i64 = JPEG2000_PGOD_LRCP }, 0,         0,           VE, .unit = "prog" },
    { "rlcp",          NULL,                0,                     AV_OPT_TYPE_CONST,  { .i64 = JPEG2000_PGOD_RLCP }, 0,         0,           VE, .unit = "prog" },
    { "rpcl",          NULL,                0,                     AV_OPT_TYPE_CONST,  { .i64 = JPEG2000_PGOD_RPCL }, 0,         0,           VE, .unit = "prog" },
    { "pcrl",          NULL,                0,                     AV_OPT_TYPE_CONST,  { .i64 = JPEG2000_PGOD_PCRL }, 0,         0,           VE, .unit = "prog" },
    { "cprl",          NULL,                0,                     AV_OPT_TYPE_CONST,  { .i64 = JPEG2000_PGOD_CPRL }, 0,         0,           VE, .unit = "prog" },
    { "layer_rates",   "Layer Rates",       OFFSET(lr_str),        AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VE },
    { NULL }
};

static const AVClass j2k_class = {
    .class_name = "jpeg 2000 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_jpeg2000_encoder = {
    .p.name         = "jpeg2000",
    CODEC_LONG_NAME("JPEG 2000"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEG2000,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE |
                      AV_CODEC_CAP_FRAME_THREADS,
    .priv_data_size = sizeof(Jpeg2000EncoderContext),
    .init           = j2kenc_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    .close          = j2kenc_destroy,
    CODEC_PIXFMTS(
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB48,
        AV_PIX_FMT_GBR24P,AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV420P16,
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV422P16,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUV440P,                      AV_PIX_FMT_YUV440P10, AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YA8,                                           AV_PIX_FMT_YA16,
        AV_PIX_FMT_RGBA,                                          AV_PIX_FMT_RGBA64,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P16,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P16,

        AV_PIX_FMT_PAL8),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.priv_class   = &j2k_class,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
