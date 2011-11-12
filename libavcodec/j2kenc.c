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
 */

/**
 * JPEG2000 image encoder
 * @file
 * @author Kamil Nowosad
 */

#include <float.h>
#include "avcodec.h"
#include "bytestream.h"
#include "j2k.h"
#include "libavutil/common.h"

#define NMSEDEC_BITS 7
#define NMSEDEC_FRACBITS (NMSEDEC_BITS-1)
#define WMSEDEC_SHIFT 13 ///< must be >= 13
#define LAMBDA_SCALE (100000000LL << (WMSEDEC_SHIFT - 13))

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
   J2kComponent *comp;
} J2kTile;

typedef struct {
    AVCodecContext *avctx;
    AVFrame picture;

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

    J2kCodingStyle codsty;
    J2kQuantStyle  qntsty;

    J2kTile *tile;
} J2kEncoderContext;


/* debug */
#if 0
#undef ifprintf
#undef printf

static void nspaces(FILE *fd, int n)
{
    while(n--) putc(' ', fd);
}

static void printv(int *tab, int l)
{
    int i;
    for (i = 0; i < l; i++)
        printf("%.3d ", tab[i]);
    printf("\n");
}

static void printu(uint8_t *tab, int l)
{
    int i;
    for (i = 0; i < l; i++)
        printf("%.3hd ", tab[i]);
    printf("\n");
}

static void printcomp(J2kComponent *comp)
{
    int i;
    for (i = 0; i < comp->y1 - comp->y0; i++)
        printv(comp->data + i * (comp->x1 - comp->x0), comp->x1 - comp->x0);
}

static void dump(J2kEncoderContext *s, FILE *fd)
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
            for(reslevelno = 0; reslevelno < s->nreslevels; reslevelno++){
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

/* bitstream routines */

/** put n times val bit */
static void put_bits(J2kEncoderContext *s, int val, int n) // TODO: optimize
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
static void put_num(J2kEncoderContext *s, int num, int n)
{
    while(--n >= 0)
        put_bits(s, (num >> n) & 1, 1);
}

/** flush the bitstream */
static void j2k_flush(J2kEncoderContext *s)
{
    if (s->bit_index){
        s->bit_index = 0;
        s->buf++;
    }
}

/* tag tree routines */

/** code the value stored in node */
static void tag_tree_code(J2kEncoderContext *s, J2kTgtNode *node, int threshold)
{
    J2kTgtNode *stack[30];
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
static void tag_tree_update(J2kTgtNode *node)
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

static int put_siz(J2kEncoderContext *s)
{
    int i;

    if (s->buf_end - s->buf < 40 + 3 * s->ncomponents)
        return -1;

    bytestream_put_be16(&s->buf, J2K_SIZ);
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

static int put_cod(J2kEncoderContext *s)
{
    J2kCodingStyle *codsty = &s->codsty;

    if (s->buf_end - s->buf < 14)
        return -1;

    bytestream_put_be16(&s->buf, J2K_COD);
    bytestream_put_be16(&s->buf, 12); // Lcod
    bytestream_put_byte(&s->buf, 0);  // Scod
    // SGcod
    bytestream_put_byte(&s->buf, 0); // progression level
    bytestream_put_be16(&s->buf, 1); // num of layers
    if(s->avctx->pix_fmt == PIX_FMT_YUV444P){
        bytestream_put_byte(&s->buf, 2); // ICT
    }else{
        bytestream_put_byte(&s->buf, 0); // unspecified
    }
    // SPcod
    bytestream_put_byte(&s->buf, codsty->nreslevels - 1); // num of decomp. levels
    bytestream_put_byte(&s->buf, codsty->log2_cblk_width-2); // cblk width
    bytestream_put_byte(&s->buf, codsty->log2_cblk_height-2); // cblk height
    bytestream_put_byte(&s->buf, 0); // cblk style
    bytestream_put_byte(&s->buf, codsty->transform); // transformation
    return 0;
}

static int put_qcd(J2kEncoderContext *s, int compno)
{
    int i, size;
    J2kCodingStyle *codsty = &s->codsty;
    J2kQuantStyle  *qntsty = &s->qntsty;

    if (qntsty->quantsty == J2K_QSTY_NONE)
        size = 4 + 3 * (codsty->nreslevels-1);
    else // QSTY_SE
        size = 5 + 6 * (codsty->nreslevels-1);

    if (s->buf_end - s->buf < size + 2)
        return -1;

    bytestream_put_be16(&s->buf, J2K_QCD);
    bytestream_put_be16(&s->buf, size);  // LQcd
    bytestream_put_byte(&s->buf, (qntsty->nguardbits << 5) | qntsty->quantsty);  // Sqcd
    if (qntsty->quantsty == J2K_QSTY_NONE)
        for (i = 0; i < codsty->nreslevels * 3 - 2; i++)
            bytestream_put_byte(&s->buf, qntsty->expn[i] << 3);
    else // QSTY_SE
        for (i = 0; i < codsty->nreslevels * 3 - 2; i++)
            bytestream_put_be16(&s->buf, (qntsty->expn[i] << 11) | qntsty->mant[i]);
    return 0;
}

static uint8_t *put_sot(J2kEncoderContext *s, int tileno)
{
    uint8_t *psotptr;

    if (s->buf_end - s->buf < 12)
        return NULL;

    bytestream_put_be16(&s->buf, J2K_SOT);
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
static int init_tiles(J2kEncoderContext *s)
{
    int tileno, tilex, tiley, compno;
    J2kCodingStyle *codsty = &s->codsty;
    J2kQuantStyle  *qntsty = &s->qntsty;

    s->numXtiles = ff_j2k_ceildiv(s->width, s->tile_width);
    s->numYtiles = ff_j2k_ceildiv(s->height, s->tile_height);

    s->tile = av_malloc(s->numXtiles * s->numYtiles * sizeof(J2kTile));
    if (!s->tile)
        return AVERROR(ENOMEM);
    for (tileno = 0, tiley = 0; tiley < s->numYtiles; tiley++)
        for (tilex = 0; tilex < s->numXtiles; tilex++, tileno++){
            J2kTile *tile = s->tile + tileno;

            tile->comp = av_malloc(s->ncomponents * sizeof(J2kComponent));
            if (!tile->comp)
                return AVERROR(ENOMEM);
            for (compno = 0; compno < s->ncomponents; compno++){
                J2kComponent *comp = tile->comp + compno;
                int ret, i, j;

                comp->coord[0][0] = tilex * s->tile_width;
                comp->coord[0][1] = FFMIN((tilex+1)*s->tile_width, s->width);
                comp->coord[1][0] = tiley * s->tile_height;
                comp->coord[1][1] = FFMIN((tiley+1)*s->tile_height, s->height);
                if (compno > 0)
                    for (i = 0; i < 2; i++)
                        for (j = 0; j < 2; j++)
                            comp->coord[i][j] = ff_j2k_ceildivpow2(comp->coord[i][j], s->chroma_shift[i]);

                if (ret = ff_j2k_init_component(comp, codsty, qntsty, s->cbps[compno], compno?1<<s->chroma_shift[0]:1, compno?1<<s->chroma_shift[1]:1))
                    return ret;
            }
        }
    return 0;
}

static void copy_frame(J2kEncoderContext *s)
{
    int tileno, compno, i, y, x;
    uint8_t *line;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        J2kTile *tile = s->tile + tileno;
        if (s->planar){
            for (compno = 0; compno < s->ncomponents; compno++){
                J2kComponent *comp = tile->comp + compno;
                int *dst = comp->data;
                line = s->picture.data[compno]
                       + comp->coord[1][0] * s->picture.linesize[compno]
                       + comp->coord[0][0];
                for (y = comp->coord[1][0]; y < comp->coord[1][1]; y++){
                    uint8_t *ptr = line;
                    for (x = comp->coord[0][0]; x < comp->coord[0][1]; x++)
                        *dst++ = *ptr++ - (1 << 7);
                    line += s->picture.linesize[compno];
                }
            }
        } else{
            line = s->picture.data[0] + tile->comp[0].coord[1][0] * s->picture.linesize[0]
                   + tile->comp[0].coord[0][0] * s->ncomponents;

            i = 0;
            for (y = tile->comp[0].coord[1][0]; y < tile->comp[0].coord[1][1]; y++){
                uint8_t *ptr = line;
                for (x = tile->comp[0].coord[0][0]; x < tile->comp[0].coord[0][1]; x++, i++){
                    for (compno = 0; compno < s->ncomponents; compno++){
                        tile->comp[compno].data[i] = *ptr++  - (1 << 7);
                    }
                }
                line += s->picture.linesize[0];
            }
        }
    }
}

static void init_quantization(J2kEncoderContext *s)
{
    int compno, reslevelno, bandno;
    J2kQuantStyle  *qntsty = &s->qntsty;
    J2kCodingStyle *codsty = &s->codsty;

    for (compno = 0; compno < s->ncomponents; compno++){
        int gbandno = 0;
        for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
            int nbands, lev = codsty->nreslevels - reslevelno - 1;
            nbands = reslevelno ? 3 : 1;
            for (bandno = 0; bandno < nbands; bandno++, gbandno++){
                int expn, mant;

                if (codsty->transform == FF_DWT97){
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

static void init_luts()
{
    int i, a,
        mask = ~((1<<NMSEDEC_FRACBITS)-1);

    for (i = 0; i < (1 << NMSEDEC_BITS); i++){
        lut_nmsedec_sig[i]  = FFMAX(6*i - (9<<NMSEDEC_FRACBITS-1) << 12-NMSEDEC_FRACBITS, 0);
        lut_nmsedec_sig0[i] = FFMAX((i*i + (1<<NMSEDEC_FRACBITS-1) & mask) << 1, 0);

        a = (i >> (NMSEDEC_BITS-2)&2) + 1;
        lut_nmsedec_ref[i]  = FFMAX((-2*i + (1<<NMSEDEC_FRACBITS) + a*i - (a*a<<NMSEDEC_FRACBITS-2))
                                    << 13-NMSEDEC_FRACBITS, 0);
        lut_nmsedec_ref0[i] = FFMAX(((i*i + (1-4*i << NMSEDEC_FRACBITS-1) + (1<<2*NMSEDEC_FRACBITS)) & mask)
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

static void encode_sigpass(J2kT1Context *t1, int width, int height, int bandno, int *nmsedec, int bpno)
{
    int y0, x, y, mask = 1 << (bpno + NMSEDEC_FRACBITS);
    int vert_causal_ctx_csty_loc_symbol;
    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0+4; y++){
                if (!(t1->flags[y+1][x+1] & J2K_T1_SIG) && (t1->flags[y+1][x+1] & J2K_T1_SIG_NB)){
                    int ctxno = ff_j2k_getnbctxno(t1->flags[y+1][x+1], bandno, vert_causal_ctx_csty_loc_symbol),
                        bit = t1->data[y][x] & mask ? 1 : 0;
                    ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, bit);
                    if (bit){
                        int xorbit;
                        int ctxno = ff_j2k_getsgnctxno(t1->flags[y+1][x+1], &xorbit);
                        ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, (t1->flags[y+1][x+1] >> 15) ^ xorbit);
                        *nmsedec += getnmsedec_sig(t1->data[y][x], bpno + NMSEDEC_FRACBITS);
                        ff_j2k_set_significant(t1, x, y, t1->flags[y+1][x+1] >> 15);
                    }
                    t1->flags[y+1][x+1] |= J2K_T1_VIS;
                }
            }
}

static void encode_refpass(J2kT1Context *t1, int width, int height, int *nmsedec, int bpno)
{
    int y0, x, y, mask = 1 << (bpno + NMSEDEC_FRACBITS);
    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0+4; y++)
                if ((t1->flags[y+1][x+1] & (J2K_T1_SIG | J2K_T1_VIS)) == J2K_T1_SIG){
                    int ctxno = ff_j2k_getrefctxno(t1->flags[y+1][x+1]);
                    *nmsedec += getnmsedec_ref(t1->data[y][x], bpno + NMSEDEC_FRACBITS);
                    ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, t1->data[y][x] & mask ? 1:0);
                    t1->flags[y+1][x+1] |= J2K_T1_REF;
                }
}

static void encode_clnpass(J2kT1Context *t1, int width, int height, int bandno, int *nmsedec, int bpno)
{
    int y0, x, y, mask = 1 << (bpno + NMSEDEC_FRACBITS);
    int vert_causal_ctx_csty_loc_symbol;
    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++){
            if (y0 + 3 < height && !(
            (t1->flags[y0+1][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG)) ||
            (t1->flags[y0+2][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG)) ||
            (t1->flags[y0+3][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG)) ||
            (t1->flags[y0+4][x+1] & (J2K_T1_SIG_NB | J2K_T1_VIS | J2K_T1_SIG))))
            {
                // aggregation mode
                int rlen;
                for (rlen = 0; rlen < 4; rlen++)
                    if (t1->data[y0+rlen][x] & mask)
                        break;
                ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + MQC_CX_RL, rlen != 4);
                if (rlen == 4)
                    continue;
                ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI, rlen >> 1);
                ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + MQC_CX_UNI, rlen & 1);
                for (y = y0 + rlen; y < y0 + 4; y++){
                    if (!(t1->flags[y+1][x+1] & (J2K_T1_SIG | J2K_T1_VIS))){
                        int ctxno = ff_j2k_getnbctxno(t1->flags[y+1][x+1], bandno, vert_causal_ctx_csty_loc_symbol);
                        if (y > y0 + rlen)
                            ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, t1->data[y][x] & mask ? 1:0);
                        if (t1->data[y][x] & mask){ // newly significant
                            int xorbit;
                            int ctxno = ff_j2k_getsgnctxno(t1->flags[y+1][x+1], &xorbit);
                            *nmsedec += getnmsedec_sig(t1->data[y][x], bpno + NMSEDEC_FRACBITS);
                            ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, (t1->flags[y+1][x+1] >> 15) ^ xorbit);
                            ff_j2k_set_significant(t1, x, y, t1->flags[y+1][x+1] >> 15);
                        }
                    }
                    t1->flags[y+1][x+1] &= ~J2K_T1_VIS;
                }
            } else{
                for (y = y0; y < y0 + 4 && y < height; y++){
                    if (!(t1->flags[y+1][x+1] & (J2K_T1_SIG | J2K_T1_VIS))){
                        int ctxno = ff_j2k_getnbctxno(t1->flags[y+1][x+1], bandno, vert_causal_ctx_csty_loc_symbol);
                        ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, t1->data[y][x] & mask ? 1:0);
                        if (t1->data[y][x] & mask){ // newly significant
                            int xorbit;
                            int ctxno = ff_j2k_getsgnctxno(t1->flags[y+1][x+1], &xorbit);
                            *nmsedec += getnmsedec_sig(t1->data[y][x], bpno + NMSEDEC_FRACBITS);
                            ff_mqc_encode(&t1->mqc, t1->mqc.cx_states + ctxno, (t1->flags[y+1][x+1] >> 15) ^ xorbit);
                            ff_j2k_set_significant(t1, x, y, t1->flags[y+1][x+1] >> 15);
                        }
                    }
                    t1->flags[y+1][x+1] &= ~J2K_T1_VIS;
                }
            }
        }
}

static void encode_cblk(J2kEncoderContext *s, J2kT1Context *t1, J2kCblk *cblk, J2kTile *tile,
                        int width, int height, int bandpos, int lev)
{
    int pass_t = 2, passno, x, y, max=0, nmsedec, bpno;
    int64_t wmsedec = 0;

    for (y = 0; y < height+2; y++)
        memset(t1->flags[y], 0, (width+2)*sizeof(int));

    for (y = 0; y < height; y++){
        for (x = 0; x < width; x++){
            if (t1->data[y][x] < 0){
                t1->flags[y+1][x+1] |= J2K_T1_SGN;
                t1->data[y][x] = -t1->data[y][x];
            }
            max = FFMAX(max, t1->data[y][x]);
        }
    }

    if (max == 0){
        cblk->nonzerobits = 0;
        bpno = 0;
    } else{
        cblk->nonzerobits = av_log2(max) + 1 - NMSEDEC_FRACBITS;
        bpno = cblk->nonzerobits - 1;
    }

    ff_mqc_initenc(&t1->mqc, cblk->data);

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

        cblk->passes[passno].rate = 3 + ff_mqc_length(&t1->mqc);
        wmsedec += (int64_t)nmsedec << (2*bpno);
        cblk->passes[passno].disto = wmsedec;

        if (++pass_t == 3){
            pass_t = 0;
            bpno--;
        }
    }
    cblk->npasses = passno;
    cblk->ninclpasses = passno;

    // TODO: optional flush on each pass
    cblk->passes[passno-1].rate = ff_mqc_flush(&t1->mqc);
}

/* tier-2 routines: */

static void putnumpasses(J2kEncoderContext *s, int n)
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


static int encode_packet(J2kEncoderContext *s, J2kResLevel *rlevel, int precno,
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
        J2kBand *band = rlevel->band + bandno;
        J2kPrec *prec = band->prec + precno;
        int yi, xi, pos;
        int cblknw = prec->xi1 - prec->xi0;

        if (band->coord[0][0] == band->coord[0][1]
        ||  band->coord[1][0] == band->coord[1][1])
            continue;

        for (pos=0, yi = prec->yi0; yi < prec->yi1; yi++){
            for (xi = prec->xi0; xi < prec->xi1; xi++, pos++){
                prec->cblkincl[pos].val = band->cblk[yi * cblknw + xi].ninclpasses == 0;
                tag_tree_update(prec->cblkincl + pos);
                prec->zerobits[pos].val = expn[bandno] + numgbits - 1 - band->cblk[yi * cblknw + xi].nonzerobits;
                tag_tree_update(prec->zerobits + pos);
            }
        }

        for (pos=0, yi = prec->yi0; yi < prec->yi1; yi++){
            for (xi = prec->xi0; xi < prec->xi1; xi++, pos++){
                int pad = 0, llen, length;
                J2kCblk *cblk = band->cblk + yi * cblknw + xi;

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
        J2kBand *band = rlevel->band + bandno;
        J2kPrec *prec = band->prec + precno;
        int yi, cblknw = prec->xi1 - prec->xi0;
        for (yi = prec->yi0; yi < prec->yi1; yi++){
            int xi;
            for (xi = prec->xi0; xi < prec->xi1; xi++){
                J2kCblk *cblk = band->cblk + yi * cblknw + xi;
                if (cblk->ninclpasses){
                    if (s->buf_end - s->buf < cblk->passes[cblk->ninclpasses-1].rate)
                        return -1;
                    bytestream_put_buffer(&s->buf, cblk->data, cblk->passes[cblk->ninclpasses-1].rate);
                }
            }
        }
    }
    return 0;
}

static int encode_packets(J2kEncoderContext *s, J2kTile *tile, int tileno)
{
    int compno, reslevelno, ret;
    J2kCodingStyle *codsty = &s->codsty;
    J2kQuantStyle  *qntsty = &s->qntsty;

    av_log(s->avctx, AV_LOG_DEBUG, "tier2\n");
    // lay-rlevel-comp-pos progression
    for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
        for (compno = 0; compno < s->ncomponents; compno++){
            int precno;
            J2kResLevel *reslevel = s->tile[tileno].comp[compno].reslevel + reslevelno;
            for (precno = 0; precno < reslevel->num_precincts_x * reslevel->num_precincts_y; precno++){
                if (ret = encode_packet(s, reslevel, precno, qntsty->expn + (reslevelno ? 3*reslevelno-2 : 0),
                              qntsty->nguardbits))
                    return ret;
            }
        }
    }
    av_log(s->avctx, AV_LOG_DEBUG, "after tier2\n");
    return 0;
}

static int getcut(J2kCblk *cblk, int64_t lambda, int dwt_norm)
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

static void truncpasses(J2kEncoderContext *s, J2kTile *tile)
{
    int compno, reslevelno, bandno, cblkno, lev;
    J2kCodingStyle *codsty = &s->codsty;

    for (compno = 0; compno < s->ncomponents; compno++){
        J2kComponent *comp = tile->comp + compno;

        for (reslevelno = 0, lev = codsty->nreslevels-1; reslevelno < codsty->nreslevels; reslevelno++, lev--){
            J2kResLevel *reslevel = comp->reslevel + reslevelno;

            for (bandno = 0; bandno < reslevel->nbands ; bandno++){
                int bandpos = bandno + (reslevelno > 0);
                J2kBand *band = reslevel->band + bandno;

                for (cblkno = 0; cblkno < band->cblknx * band->cblkny; cblkno++){
                    J2kCblk *cblk = band->cblk + cblkno;

                    cblk->ninclpasses = getcut(cblk, s->lambda,
                            (int64_t)dwt_norms[codsty->transform][bandpos][lev] * (int64_t)band->stepsize >> 13);
                }
            }
        }
    }
}

static int encode_tile(J2kEncoderContext *s, J2kTile *tile, int tileno)
{
    int compno, reslevelno, bandno, ret;
    J2kT1Context t1;
    J2kCodingStyle *codsty = &s->codsty;
    for (compno = 0; compno < s->ncomponents; compno++){
        J2kComponent *comp = s->tile[tileno].comp + compno;

        av_log(s->avctx, AV_LOG_DEBUG,"dwt\n");
        if (ret = ff_j2k_dwt_encode(&comp->dwt, comp->data))
            return ret;
        av_log(s->avctx, AV_LOG_DEBUG,"after dwt -> tier1\n");

        for (reslevelno = 0; reslevelno < codsty->nreslevels; reslevelno++){
            J2kResLevel *reslevel = comp->reslevel + reslevelno;

            for (bandno = 0; bandno < reslevel->nbands ; bandno++){
                J2kBand *band = reslevel->band + bandno;
                int cblkx, cblky, cblkno=0, xx0, x0, xx1, y0, yy0, yy1, bandpos;
                yy0 = bandno == 0 ? 0 : comp->reslevel[reslevelno-1].coord[1][1] - comp->reslevel[reslevelno-1].coord[1][0];
                y0 = yy0;
                yy1 = FFMIN(ff_j2k_ceildiv(band->coord[1][0] + 1, band->codeblock_height) * band->codeblock_height,
                            band->coord[1][1]) - band->coord[1][0] + yy0;

                if (band->coord[0][0] == band->coord[0][1] || band->coord[1][0] == band->coord[1][1])
                    continue;

                bandpos = bandno + (reslevelno > 0);

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
                        if (codsty->transform == FF_DWT53){
                            for (y = yy0; y < yy1; y++){
                                int *ptr = t1.data[y-yy0];
                                for (x = xx0; x < xx1; x++){
                                    *ptr++ = comp->data[(comp->coord[0][1] - comp->coord[0][0]) * y + x] << NMSEDEC_FRACBITS;
                                }
                            }
                        } else{
                            for (y = yy0; y < yy1; y++){
                                int *ptr = t1.data[y-yy0];
                                for (x = xx0; x < xx1; x++){
                                    *ptr = (comp->data[(comp->coord[0][1] - comp->coord[0][0]) * y + x]);
                                    *ptr = (int64_t)*ptr * (int64_t)(8192 * 8192 / band->stepsize) >> 13 - NMSEDEC_FRACBITS;
                                    *ptr++;
                                }
                            }
                        }
                        encode_cblk(s, &t1, band->cblk + cblkno, tile, xx1 - xx0, yy1 - yy0,
                                    bandpos, codsty->nreslevels - reslevelno - 1);
                        xx0 = xx1;
                        xx1 = FFMIN(xx1 + band->codeblock_width, band->coord[0][1] - band->coord[0][0] + x0);
                    }
                    yy0 = yy1;
                    yy1 = FFMIN(yy1 + band->codeblock_height, band->coord[1][1] - band->coord[1][0] + y0);
                }
            }
        }
        av_log(s->avctx, AV_LOG_DEBUG, "after tier1\n");
    }

    av_log(s->avctx, AV_LOG_DEBUG, "rate control\n");
    truncpasses(s, tile);
    if (ret = encode_packets(s, tile, tileno))
        return ret;
    av_log(s->avctx, AV_LOG_DEBUG, "after rate control\n");
    return 0;
}

static void cleanup(J2kEncoderContext *s)
{
    int tileno, compno;
    J2kCodingStyle *codsty = &s->codsty;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        for (compno = 0; compno < s->ncomponents; compno++){
            J2kComponent *comp = s->tile[tileno].comp + compno;
            ff_j2k_cleanup(comp, codsty);
        }
        av_freep(&s->tile[tileno].comp);
    }
    av_freep(&s->tile);
}

static void reinit(J2kEncoderContext *s)
{
    int tileno, compno;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        J2kTile *tile = s->tile + tileno;
        for (compno = 0; compno < s->ncomponents; compno++)
            ff_j2k_reinit(tile->comp + compno, &s->codsty);
    }
}

static int encode_frame(AVCodecContext *avctx,
                        uint8_t *buf, int buf_size,
                        void *data)
{
    int tileno, ret;
    J2kEncoderContext *s = avctx->priv_data;

    // init:
    s->buf = s->buf_start = buf;
    s->buf_end = buf + buf_size;

    s->picture = *(AVFrame*)data;
    avctx->coded_frame= &s->picture;

    s->lambda = s->picture.quality * LAMBDA_SCALE;

    copy_frame(s);
    reinit(s);

    if (s->buf_end - s->buf < 2)
        return -1;
    bytestream_put_be16(&s->buf, J2K_SOC);
    if (ret = put_siz(s))
        return ret;
    if (ret = put_cod(s))
        return ret;
    if (ret = put_qcd(s, 0))
        return ret;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++){
        uint8_t *psotptr;
        if (!(psotptr = put_sot(s, tileno)))
            return -1;
        if (s->buf_end - s->buf < 2)
            return -1;
        bytestream_put_be16(&s->buf, J2K_SOD);
        if (ret = encode_tile(s, s->tile + tileno, tileno))
            return ret;
        bytestream_put_be32(&psotptr, s->buf - psotptr + 6);
    }
    if (s->buf_end - s->buf < 2)
        return -1;
    bytestream_put_be16(&s->buf, J2K_EOC);

    av_log(s->avctx, AV_LOG_DEBUG, "end\n");
    return s->buf - s->buf_start;
}

static av_cold int j2kenc_init(AVCodecContext *avctx)
{
    int i, ret;
    J2kEncoderContext *s = avctx->priv_data;
    J2kCodingStyle *codsty = &s->codsty;
    J2kQuantStyle  *qntsty = &s->qntsty;

    s->avctx = avctx;
    av_log(s->avctx, AV_LOG_DEBUG, "init\n");

    // defaults:
    // TODO: implement setting non-standard precinct size
    codsty->log2_prec_width  = 15;
    codsty->log2_prec_height = 15;
    codsty->nreslevels       = 7;
    codsty->log2_cblk_width  = 4;
    codsty->log2_cblk_height = 4;
    codsty->transform        = 1;

    qntsty->nguardbits       = 1;

    s->tile_width            = 256;
    s->tile_height           = 256;

    if (codsty->transform == FF_DWT53)
        qntsty->quantsty = J2K_QSTY_NONE;
    else
        qntsty->quantsty = J2K_QSTY_SE;

    s->width = avctx->width;
    s->height = avctx->height;

    for (i = 0; i < 3; i++)
        s->cbps[i] = 8;

    if (avctx->pix_fmt == PIX_FMT_RGB24){
        s->ncomponents = 3;
    } else if (avctx->pix_fmt == PIX_FMT_GRAY8){
        s->ncomponents = 1;
    } else{ // planar YUV
        s->planar = 1;
        s->ncomponents = 3;
        avcodec_get_chroma_sub_sample(avctx->pix_fmt,
                s->chroma_shift, s->chroma_shift + 1);
    }

    ff_j2k_init_tier1_luts();

    init_luts();

    init_quantization(s);
    if (ret=init_tiles(s))
        return ret;

    av_log(s->avctx, AV_LOG_DEBUG, "after init\n");

    return 0;
}

static int j2kenc_destroy(AVCodecContext *avctx)
{
    J2kEncoderContext *s = avctx->priv_data;

    cleanup(s);
    return 0;
}

AVCodec ff_jpeg2000_encoder = {
    "j2k",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_JPEG2000,
    sizeof(J2kEncoderContext),
    j2kenc_init,
    encode_frame,
    j2kenc_destroy,
    .capabilities= CODEC_CAP_EXPERIMENTAL,
    .long_name = NULL_IF_CONFIG_SMALL("JPEG 2000"),
    .pix_fmts =
        (enum PixelFormat[]) {PIX_FMT_RGB24, PIX_FMT_YUV444P, PIX_FMT_GRAY8,
/*                              PIX_FMT_YUV420P,
                              PIX_FMT_YUV422P, PIX_FMT_YUV444P,
                              PIX_FMT_YUV410P, PIX_FMT_YUV411P,*/
                              -1}
};
