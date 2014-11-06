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

#include <inttypes.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "thread.h"
#include "jpeg2000.h"
#include "jpeg2000dsp.h"

#define JP2_SIG_TYPE    0x6A502020
#define JP2_SIG_VALUE   0x0D0A870A
#define JP2_CODESTREAM  0x6A703263
#define JP2_HEADER      0x6A703268

#define HAD_COC 0x01
#define HAD_QCC 0x02

typedef struct Jpeg2000TilePart {
    uint8_t tile_index;                 // Tile index who refers the tile-part
    const uint8_t *tp_end;
    GetByteContext tpg;                 // bit stream in tile-part
} Jpeg2000TilePart;

/* RMK: For JPEG2000 DCINEMA 3 tile-parts in a tile
 * one per component, so tile_part elements have a size of 3 */
typedef struct Jpeg2000Tile {
    Jpeg2000Component   *comp;
    uint8_t             properties[4];
    Jpeg2000CodingStyle codsty[4];
    Jpeg2000QuantStyle  qntsty[4];
    Jpeg2000TilePart    tile_part[4];
    uint16_t tp_idx;                    // Tile-part index
} Jpeg2000Tile;

typedef struct Jpeg2000DecoderContext {
    AVClass         *class;
    AVCodecContext  *avctx;
    GetByteContext  g;

    int             width, height;
    int             image_offset_x, image_offset_y;
    int             tile_offset_x, tile_offset_y;
    uint8_t         cbps[4];    // bits per sample in particular components
    uint8_t         sgnd[4];    // if a component is signed
    uint8_t         properties[4];
    int             cdx[4], cdy[4];
    int             precision;
    int             ncomponents;
    int             colour_space;
    uint32_t        palette[256];
    int8_t          pal8;
    int             cdef[4];
    int             tile_width, tile_height;
    unsigned        numXtiles, numYtiles;
    int             maxtilelen;

    Jpeg2000CodingStyle codsty[4];
    Jpeg2000QuantStyle  qntsty[4];

    int             bit_index;

    int             curtileno;

    Jpeg2000Tile    *tile;
    Jpeg2000DSPContext dsp;

    /*options parameters*/
    int             reduction_factor;
} Jpeg2000DecoderContext;

/* get_bits functions for JPEG2000 packet bitstream
 * It is a get_bit function with a bit-stuffing routine. If the value of the
 * byte is 0xFF, the next byte includes an extra zero bit stuffed into the MSB.
 * cf. ISO-15444-1:2002 / B.10.1 Bit-stuffing routine */
static int get_bits(Jpeg2000DecoderContext *s, int n)
{
    int res = 0;

    while (--n >= 0) {
        res <<= 1;
        if (s->bit_index == 0) {
            s->bit_index = 7 + (bytestream2_get_byte(&s->g) != 0xFFu);
        }
        s->bit_index--;
        res |= (bytestream2_peek_byte(&s->g) >> s->bit_index) & 1;
    }
    return res;
}

static void jpeg2000_flush(Jpeg2000DecoderContext *s)
{
    if (bytestream2_get_byte(&s->g) == 0xff)
        bytestream2_skip(&s->g, 1);
    s->bit_index = 8;
}

/* decode the value stored in node */
static int tag_tree_decode(Jpeg2000DecoderContext *s, Jpeg2000TgtNode *node,
                           int threshold)
{
    Jpeg2000TgtNode *stack[30];
    int sp = -1, curval = 0;

    if (!node)
        return AVERROR_INVALIDDATA;

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

static int pix_fmt_match(enum AVPixelFormat pix_fmt, int components,
                         int bpc, uint32_t log2_chroma_wh, int pal8)
{
    int match = 1;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    if (desc->nb_components != components) {
        return 0;
    }

    switch (components) {
    case 4:
        match = match && desc->comp[3].depth_minus1 + 1 >= bpc &&
                         (log2_chroma_wh >> 14 & 3) == 0 &&
                         (log2_chroma_wh >> 12 & 3) == 0;
    case 3:
        match = match && desc->comp[2].depth_minus1 + 1 >= bpc &&
                         (log2_chroma_wh >> 10 & 3) == desc->log2_chroma_w &&
                         (log2_chroma_wh >>  8 & 3) == desc->log2_chroma_h;
    case 2:
        match = match && desc->comp[1].depth_minus1 + 1 >= bpc &&
                         (log2_chroma_wh >>  6 & 3) == desc->log2_chroma_w &&
                         (log2_chroma_wh >>  4 & 3) == desc->log2_chroma_h;

    case 1:
        match = match && desc->comp[0].depth_minus1 + 1 >= bpc &&
                         (log2_chroma_wh >>  2 & 3) == 0 &&
                         (log2_chroma_wh       & 3) == 0 &&
                         (desc->flags & AV_PIX_FMT_FLAG_PAL) == pal8 * AV_PIX_FMT_FLAG_PAL;
    }
    return match;
}

// pix_fmts with lower bpp have to be listed before
// similar pix_fmts with higher bpp.
#define RGB_PIXEL_FORMATS   AV_PIX_FMT_PAL8,AV_PIX_FMT_RGB24,AV_PIX_FMT_RGBA,AV_PIX_FMT_RGB48,AV_PIX_FMT_RGBA64
#define GRAY_PIXEL_FORMATS  AV_PIX_FMT_GRAY8,AV_PIX_FMT_GRAY8A,AV_PIX_FMT_GRAY16
#define YUV_PIXEL_FORMATS   AV_PIX_FMT_YUV410P,AV_PIX_FMT_YUV411P,AV_PIX_FMT_YUVA420P, \
                            AV_PIX_FMT_YUV420P,AV_PIX_FMT_YUV422P,AV_PIX_FMT_YUVA422P, \
                            AV_PIX_FMT_YUV440P,AV_PIX_FMT_YUV444P,AV_PIX_FMT_YUVA444P, \
                            AV_PIX_FMT_YUV420P9,AV_PIX_FMT_YUV422P9,AV_PIX_FMT_YUV444P9, \
                            AV_PIX_FMT_YUVA420P9,AV_PIX_FMT_YUVA422P9,AV_PIX_FMT_YUVA444P9, \
                            AV_PIX_FMT_YUV420P10,AV_PIX_FMT_YUV422P10,AV_PIX_FMT_YUV444P10, \
                            AV_PIX_FMT_YUVA420P10,AV_PIX_FMT_YUVA422P10,AV_PIX_FMT_YUVA444P10, \
                            AV_PIX_FMT_YUV420P12,AV_PIX_FMT_YUV422P12,AV_PIX_FMT_YUV444P12, \
                            AV_PIX_FMT_YUV420P14,AV_PIX_FMT_YUV422P14,AV_PIX_FMT_YUV444P14, \
                            AV_PIX_FMT_YUV420P16,AV_PIX_FMT_YUV422P16,AV_PIX_FMT_YUV444P16, \
                            AV_PIX_FMT_YUVA420P16,AV_PIX_FMT_YUVA422P16,AV_PIX_FMT_YUVA444P16
#define XYZ_PIXEL_FORMATS   AV_PIX_FMT_XYZ12

static const enum AVPixelFormat rgb_pix_fmts[]  = {RGB_PIXEL_FORMATS};
static const enum AVPixelFormat gray_pix_fmts[] = {GRAY_PIXEL_FORMATS};
static const enum AVPixelFormat yuv_pix_fmts[]  = {YUV_PIXEL_FORMATS};
static const enum AVPixelFormat xyz_pix_fmts[]  = {XYZ_PIXEL_FORMATS};
static const enum AVPixelFormat all_pix_fmts[]  = {RGB_PIXEL_FORMATS,
                                                   GRAY_PIXEL_FORMATS,
                                                   YUV_PIXEL_FORMATS,
                                                   XYZ_PIXEL_FORMATS};

/* marker segments */
/* get sizes and offsets of image, tiles; number of components */
static int get_siz(Jpeg2000DecoderContext *s)
{
    int i;
    int ncomponents;
    uint32_t log2_chroma_wh = 0;
    const enum AVPixelFormat *possible_fmts = NULL;
    int possible_fmts_nb = 0;

    if (bytestream2_get_bytes_left(&s->g) < 36)
        return AVERROR_INVALIDDATA;

    s->avctx->profile = bytestream2_get_be16u(&s->g); // Rsiz
    s->width          = bytestream2_get_be32u(&s->g); // Width
    s->height         = bytestream2_get_be32u(&s->g); // Height
    s->image_offset_x = bytestream2_get_be32u(&s->g); // X0Siz
    s->image_offset_y = bytestream2_get_be32u(&s->g); // Y0Siz
    s->tile_width     = bytestream2_get_be32u(&s->g); // XTSiz
    s->tile_height    = bytestream2_get_be32u(&s->g); // YTSiz
    s->tile_offset_x  = bytestream2_get_be32u(&s->g); // XT0Siz
    s->tile_offset_y  = bytestream2_get_be32u(&s->g); // YT0Siz
    ncomponents       = bytestream2_get_be16u(&s->g); // CSiz

    if (s->image_offset_x || s->image_offset_y) {
        avpriv_request_sample(s->avctx, "Support for image offsets");
        return AVERROR_PATCHWELCOME;
    }

    if (ncomponents <= 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid number of components: %d\n",
               s->ncomponents);
        return AVERROR_INVALIDDATA;
    }

    if (ncomponents > 4) {
        avpriv_request_sample(s->avctx, "Support for %d components",
                              s->ncomponents);
        return AVERROR_PATCHWELCOME;
    }

    s->ncomponents = ncomponents;

    if (s->tile_width <= 0 || s->tile_height <= 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid tile dimension %dx%d.\n",
               s->tile_width, s->tile_height);
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_bytes_left(&s->g) < 3 * s->ncomponents)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < s->ncomponents; i++) { // Ssiz_i XRsiz_i, YRsiz_i
        uint8_t x    = bytestream2_get_byteu(&s->g);
        s->cbps[i]   = (x & 0x7f) + 1;
        s->precision = FFMAX(s->cbps[i], s->precision);
        s->sgnd[i]   = !!(x & 0x80);
        s->cdx[i]    = bytestream2_get_byteu(&s->g);
        s->cdy[i]    = bytestream2_get_byteu(&s->g);
        if (   !s->cdx[i] || s->cdx[i] == 3 || s->cdx[i] > 4
            || !s->cdy[i] || s->cdy[i] == 3 || s->cdy[i] > 4) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid sample separation %d/%d\n", s->cdx[i], s->cdy[i]);
            return AVERROR_INVALIDDATA;
        }
        log2_chroma_wh |= s->cdy[i] >> 1 << i * 4 | s->cdx[i] >> 1 << i * 4 + 2;
    }

    s->numXtiles = ff_jpeg2000_ceildiv(s->width  - s->tile_offset_x, s->tile_width);
    s->numYtiles = ff_jpeg2000_ceildiv(s->height - s->tile_offset_y, s->tile_height);

    if (s->numXtiles * (uint64_t)s->numYtiles > INT_MAX/sizeof(*s->tile)) {
        s->numXtiles = s->numYtiles = 0;
        return AVERROR(EINVAL);
    }

    s->tile = av_mallocz_array(s->numXtiles * s->numYtiles, sizeof(*s->tile));
    if (!s->tile) {
        s->numXtiles = s->numYtiles = 0;
        return AVERROR(ENOMEM);
    }

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

    if (s->avctx->profile == FF_PROFILE_JPEG2000_DCINEMA_2K ||
        s->avctx->profile == FF_PROFILE_JPEG2000_DCINEMA_4K) {
        possible_fmts = xyz_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(xyz_pix_fmts);
    } else {
        switch (s->colour_space) {
        case 16:
            possible_fmts = rgb_pix_fmts;
            possible_fmts_nb = FF_ARRAY_ELEMS(rgb_pix_fmts);
            break;
        case 17:
            possible_fmts = gray_pix_fmts;
            possible_fmts_nb = FF_ARRAY_ELEMS(gray_pix_fmts);
            break;
        case 18:
            possible_fmts = yuv_pix_fmts;
            possible_fmts_nb = FF_ARRAY_ELEMS(yuv_pix_fmts);
            break;
        default:
            possible_fmts = all_pix_fmts;
            possible_fmts_nb = FF_ARRAY_ELEMS(all_pix_fmts);
            break;
        }
    }
    for (i = 0; i < possible_fmts_nb; ++i) {
        if (pix_fmt_match(possible_fmts[i], ncomponents, s->precision, log2_chroma_wh, s->pal8)) {
            s->avctx->pix_fmt = possible_fmts[i];
            break;
        }
    }
    if (i == possible_fmts_nb) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Unknown pix_fmt, profile: %d, colour_space: %d, "
               "components: %d, precision: %d, "
               "cdx[1]: %d, cdy[1]: %d, cdx[2]: %d, cdy[2]: %d\n",
               s->avctx->profile, s->colour_space, ncomponents, s->precision,
               ncomponents > 2 ? s->cdx[1] : 0,
               ncomponents > 2 ? s->cdy[1] : 0,
               ncomponents > 2 ? s->cdx[2] : 0,
               ncomponents > 2 ? s->cdy[2] : 0);
        return AVERROR_PATCHWELCOME;
    }
    s->avctx->bits_per_raw_sample = s->precision;
    return 0;
}

/* get common part for COD and COC segments */
static int get_cox(Jpeg2000DecoderContext *s, Jpeg2000CodingStyle *c)
{
    uint8_t byte;

    if (bytestream2_get_bytes_left(&s->g) < 5)
        return AVERROR_INVALIDDATA;

    /*  nreslevels = number of resolution levels
                   = number of decomposition level +1 */
    c->nreslevels = bytestream2_get_byteu(&s->g) + 1;
    if (c->nreslevels >= JPEG2000_MAX_RESLEVELS) {
        av_log(s->avctx, AV_LOG_ERROR, "nreslevels %d is invalid\n", c->nreslevels);
        return AVERROR_INVALIDDATA;
    }

    if (c->nreslevels <= s->reduction_factor) {
        /* we are forced to update reduction_factor as its requested value is
           not compatible with this bitstream, and as we might have used it
           already in setup earlier we have to fail this frame until
           reinitialization is implemented */
        av_log(s->avctx, AV_LOG_ERROR, "reduction_factor too large for this bitstream, max is %d\n", c->nreslevels - 1);
        s->reduction_factor = c->nreslevels - 1;
        return AVERROR(EINVAL);
    }

    /* compute number of resolution levels to decode */
    c->nreslevels2decode = c->nreslevels - s->reduction_factor;

    c->log2_cblk_width  = (bytestream2_get_byteu(&s->g) & 15) + 2; // cblk width
    c->log2_cblk_height = (bytestream2_get_byteu(&s->g) & 15) + 2; // cblk height

    if (c->log2_cblk_width > 10 || c->log2_cblk_height > 10 ||
        c->log2_cblk_width + c->log2_cblk_height > 12) {
        av_log(s->avctx, AV_LOG_ERROR, "cblk size invalid\n");
        return AVERROR_INVALIDDATA;
    }

    if (c->log2_cblk_width > 6 || c->log2_cblk_height > 6) {
        avpriv_request_sample(s->avctx, "cblk size > 64");
        return AVERROR_PATCHWELCOME;
    }

    c->cblk_style = bytestream2_get_byteu(&s->g);
    if (c->cblk_style != 0) { // cblk style
        av_log(s->avctx, AV_LOG_WARNING, "extra cblk styles %X\n", c->cblk_style);
    }
    c->transform = bytestream2_get_byteu(&s->g); // DWT transformation type
    /* set integer 9/7 DWT in case of BITEXACT flag */
    if ((s->avctx->flags & CODEC_FLAG_BITEXACT) && (c->transform == FF_DWT97))
        c->transform = FF_DWT97_INT;

    if (c->csty & JPEG2000_CSTY_PREC) {
        int i;
        for (i = 0; i < c->nreslevels; i++) {
            byte = bytestream2_get_byte(&s->g);
            c->log2_prec_widths[i]  =  byte       & 0x0F;    // precinct PPx
            c->log2_prec_heights[i] = (byte >> 4) & 0x0F;    // precinct PPy
        }
    } else {
        memset(c->log2_prec_widths , 15, sizeof(c->log2_prec_widths ));
        memset(c->log2_prec_heights, 15, sizeof(c->log2_prec_heights));
    }
    return 0;
}

/* get coding parameters for a particular tile or whole image*/
static int get_cod(Jpeg2000DecoderContext *s, Jpeg2000CodingStyle *c,
                   uint8_t *properties)
{
    Jpeg2000CodingStyle tmp;
    int compno, ret;

    if (bytestream2_get_bytes_left(&s->g) < 5)
        return AVERROR_INVALIDDATA;

    tmp.csty = bytestream2_get_byteu(&s->g);

    // get progression order
    tmp.prog_order = bytestream2_get_byteu(&s->g);

    tmp.nlayers    = bytestream2_get_be16u(&s->g);
    tmp.mct        = bytestream2_get_byteu(&s->g); // multiple component transformation

    if (tmp.mct && s->ncomponents < 3) {
        av_log(s->avctx, AV_LOG_ERROR,
               "MCT %"PRIu8" with too few components (%d)\n",
               tmp.mct, s->ncomponents);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = get_cox(s, &tmp)) < 0)
        return ret;

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
    int compno, ret;

    if (bytestream2_get_bytes_left(&s->g) < 2)
        return AVERROR_INVALIDDATA;

    compno = bytestream2_get_byteu(&s->g);

    if (compno >= s->ncomponents) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Invalid compno %d. There are %d components in the image.\n",
               compno, s->ncomponents);
        return AVERROR_INVALIDDATA;
    }

    c      += compno;
    c->csty = bytestream2_get_byteu(&s->g);

    if ((ret = get_cox(s, c)) < 0)
        return ret;

    properties[compno] |= HAD_COC;
    return 0;
}

/* Get common part for QCD and QCC segments. */
static int get_qcx(Jpeg2000DecoderContext *s, int n, Jpeg2000QuantStyle *q)
{
    int i, x;

    if (bytestream2_get_bytes_left(&s->g) < 1)
        return AVERROR_INVALIDDATA;

    x = bytestream2_get_byteu(&s->g); // Sqcd

    q->nguardbits = x >> 5;
    q->quantsty   = x & 0x1f;

    if (q->quantsty == JPEG2000_QSTY_NONE) {
        n -= 3;
        if (bytestream2_get_bytes_left(&s->g) < n ||
            n > JPEG2000_MAX_DECLEVELS*3)
            return AVERROR_INVALIDDATA;
        for (i = 0; i < n; i++)
            q->expn[i] = bytestream2_get_byteu(&s->g) >> 3;
    } else if (q->quantsty == JPEG2000_QSTY_SI) {
        if (bytestream2_get_bytes_left(&s->g) < 2)
            return AVERROR_INVALIDDATA;
        x          = bytestream2_get_be16u(&s->g);
        q->expn[0] = x >> 11;
        q->mant[0] = x & 0x7ff;
        for (i = 1; i < JPEG2000_MAX_DECLEVELS * 3; i++) {
            int curexpn = FFMAX(0, q->expn[0] - (i - 1) / 3);
            q->expn[i] = curexpn;
            q->mant[i] = q->mant[0];
        }
    } else {
        n = (n - 3) >> 1;
        if (bytestream2_get_bytes_left(&s->g) < 2 * n ||
            n > JPEG2000_MAX_DECLEVELS*3)
            return AVERROR_INVALIDDATA;
        for (i = 0; i < n; i++) {
            x          = bytestream2_get_be16u(&s->g);
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
    int compno, ret;

    memset(&tmp, 0, sizeof(tmp));

    if ((ret = get_qcx(s, n, &tmp)) < 0)
        return ret;
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

    if (bytestream2_get_bytes_left(&s->g) < 1)
        return AVERROR_INVALIDDATA;

    compno = bytestream2_get_byteu(&s->g);

    if (compno >= s->ncomponents) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Invalid compno %d. There are %d components in the image.\n",
               compno, s->ncomponents);
        return AVERROR_INVALIDDATA;
    }

    properties[compno] |= HAD_QCC;
    return get_qcx(s, n - 1, q + compno);
}

/* Get start of tile segment. */
static int get_sot(Jpeg2000DecoderContext *s, int n)
{
    Jpeg2000TilePart *tp;
    uint16_t Isot;
    uint32_t Psot;
    uint8_t TPsot;

    if (bytestream2_get_bytes_left(&s->g) < 8)
        return AVERROR_INVALIDDATA;

    s->curtileno = 0;
    Isot = bytestream2_get_be16u(&s->g);        // Isot
    if (Isot >= s->numXtiles * s->numYtiles)
        return AVERROR_INVALIDDATA;

    s->curtileno = Isot;
    Psot  = bytestream2_get_be32u(&s->g);       // Psot
    TPsot = bytestream2_get_byteu(&s->g);       // TPsot

    /* Read TNSot but not used */
    bytestream2_get_byteu(&s->g);               // TNsot

    if (Psot > bytestream2_get_bytes_left(&s->g) + n + 2) {
        av_log(s->avctx, AV_LOG_ERROR, "Psot %"PRIu32" too big\n", Psot);
        return AVERROR_INVALIDDATA;
    }

    if (TPsot >= FF_ARRAY_ELEMS(s->tile[Isot].tile_part)) {
        avpriv_request_sample(s->avctx, "Support for %"PRIu8" components", TPsot);
        return AVERROR_PATCHWELCOME;
    }

    s->tile[Isot].tp_idx = TPsot;
    tp             = s->tile[Isot].tile_part + TPsot;
    tp->tile_index = Isot;
    tp->tp_end     = s->g.buffer + Psot - n - 2;

    if (!TPsot) {
        Jpeg2000Tile *tile = s->tile + s->curtileno;

        /* copy defaults */
        memcpy(tile->codsty, s->codsty, s->ncomponents * sizeof(Jpeg2000CodingStyle));
        memcpy(tile->qntsty, s->qntsty, s->ncomponents * sizeof(Jpeg2000QuantStyle));
    }

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
    bytestream2_get_byte(&s->g);               /* Ztlm: skipped */
    Stlm = bytestream2_get_byte(&s->g);

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
            bytestream2_get_byte(&s->g);
            break;
        case 2:
            bytestream2_get_be16(&s->g);
            break;
        case 3:
            bytestream2_get_be32(&s->g);
            break;
        }
        if (SP == 0) {
            bytestream2_get_be16(&s->g);
        } else {
            bytestream2_get_be32(&s->g);
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

    if (!tile->comp)
        return AVERROR(ENOMEM);

    for (compno = 0; compno < s->ncomponents; compno++) {
        Jpeg2000Component *comp = tile->comp + compno;
        Jpeg2000CodingStyle *codsty = tile->codsty + compno;
        Jpeg2000QuantStyle  *qntsty = tile->qntsty + compno;
        int ret; // global bandno

        comp->coord_o[0][0] = FFMAX(tilex       * s->tile_width  + s->tile_offset_x, s->image_offset_x);
        comp->coord_o[0][1] = FFMIN((tilex + 1) * s->tile_width  + s->tile_offset_x, s->width);
        comp->coord_o[1][0] = FFMAX(tiley       * s->tile_height + s->tile_offset_y, s->image_offset_y);
        comp->coord_o[1][1] = FFMIN((tiley + 1) * s->tile_height + s->tile_offset_y, s->height);

        comp->coord[0][0] = ff_jpeg2000_ceildivpow2(comp->coord_o[0][0], s->reduction_factor);
        comp->coord[0][1] = ff_jpeg2000_ceildivpow2(comp->coord_o[0][1], s->reduction_factor);
        comp->coord[1][0] = ff_jpeg2000_ceildivpow2(comp->coord_o[1][0], s->reduction_factor);
        comp->coord[1][1] = ff_jpeg2000_ceildivpow2(comp->coord_o[1][1], s->reduction_factor);

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

            if (!cblk->npasses) {
                int v = expn[bandno] + numgbits - 1 -
                        tag_tree_decode(s, prec->zerobits + cblkno, 100);
                if (v < 0) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "nonzerobits %d invalid\n", v);
                    return AVERROR_INVALIDDATA;
                }
                cblk->nonzerobits = v;
            }
            if ((newpasses = getnpasses(s)) < 0)
                return newpasses;
            if ((llen = getlblockinc(s)) < 0)
                return llen;
            cblk->lblock += llen;
            if ((ret = get_bits(s, av_log2(newpasses) + cblk->lblock)) < 0)
                return ret;
            if (ret > sizeof(cblk->data)) {
                avpriv_request_sample(s->avctx,
                                      "Block with lengthinc greater than %"SIZE_SPECIFIER"",
                                      sizeof(cblk->data));
                return AVERROR_PATCHWELCOME;
            }
            cblk->lengthinc = ret;
            cblk->npasses  += newpasses;
        }
    }
    jpeg2000_flush(s);

    if (codsty->csty & JPEG2000_CSTY_EPH) {
        if (bytestream2_peek_be16(&s->g) == JPEG2000_EPH)
            bytestream2_skip(&s->g, 2);
        else
            av_log(s->avctx, AV_LOG_ERROR, "EPH marker not found.\n");
    }

    for (bandno = 0; bandno < rlevel->nbands; bandno++) {
        Jpeg2000Band *band = rlevel->band + bandno;
        Jpeg2000Prec *prec = band->prec + precno;

        nb_code_blocks = prec->nb_codeblocks_height * prec->nb_codeblocks_width;
        for (cblkno = 0; cblkno < nb_code_blocks; cblkno++) {
            Jpeg2000Cblk *cblk = prec->cblk + cblkno;
            if (   bytestream2_get_bytes_left(&s->g) < cblk->lengthinc
                || sizeof(cblk->data) < cblk->length + cblk->lengthinc + 2
            ) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Block length %"PRIu16" or lengthinc %d is too large\n",
                       cblk->length, cblk->lengthinc);
                return AVERROR_INVALIDDATA;
            }

            bytestream2_get_bufferu(&s->g, cblk->data + cblk->length, cblk->lengthinc);
            cblk->length   += cblk->lengthinc;
            cblk->lengthinc = 0;
        }
    }
    return 0;
}

static int jpeg2000_decode_packets(Jpeg2000DecoderContext *s, Jpeg2000Tile *tile)
{
    int ret = 0;
    int layno, reslevelno, compno, precno, ok_reslevel;
    int x, y;

    s->bit_index = 8;
    switch (tile->codsty[0].prog_order) {
    case JPEG2000_PGOD_RLCP:
        avpriv_request_sample(s->avctx, "Progression order RLCP");

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
                            if ((ret = jpeg2000_decode_packet(s,
                                                              codsty, rlevel,
                                                              precno, layno,
                                                              qntsty->expn + (reslevelno ? 3 * (reslevelno - 1) + 1 : 0),
                                                              qntsty->nguardbits)) < 0)
                                return ret;
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
            s->g = tile->tile_part[compno].tpg;

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

                        if (prcx >= rlevel->num_precincts_x || prcy >= rlevel->num_precincts_y)
                            return AVERROR_PATCHWELCOME;

                        for (layno = 0; layno < tile->codsty[0].nlayers; layno++) {
                            if ((ret = jpeg2000_decode_packet(s, codsty, rlevel,
                                                              precno, layno,
                                                              qntsty->expn + (reslevelno ? 3 * (reslevelno - 1) + 1 : 0),
                                                              qntsty->nguardbits)) < 0)
                                return ret;
                        }
                    }
                }
            }
        }
        break;

    case JPEG2000_PGOD_RPCL:
        avpriv_request_sample(s->avctx, "Progression order RPCL");
        ret = AVERROR_PATCHWELCOME;
        break;

    case JPEG2000_PGOD_PCRL:
        avpriv_request_sample(s->avctx, "Progression order PCRL");
        ret = AVERROR_PATCHWELCOME;
        break;

    default:
        break;
    }

    /* EOC marker reached */
    bytestream2_skip(&s->g, 2);

    return ret;
}

/* TIER-1 routines */
static void decode_sigpass(Jpeg2000T1Context *t1, int width, int height,
                           int bpno, int bandno, int bpass_csty_symbol,
                           int vert_causal_ctx_csty_symbol)
{
    int mask = 3 << (bpno - 1), y0, x, y;

    for (y0 = 0; y0 < height; y0 += 4)
        for (x = 0; x < width; x++)
            for (y = y0; y < height && y < y0 + 4; y++) {
                if ((t1->flags[y+1][x+1] & JPEG2000_T1_SIG_NB)
                && !(t1->flags[y+1][x+1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS))) {
                    int flags_mask = -1;
                    if (vert_causal_ctx_csty_symbol && y == y0 + 3)
                        flags_mask &= ~(JPEG2000_T1_SIG_S | JPEG2000_T1_SIG_SW | JPEG2000_T1_SIG_SE);
                    if (ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ff_jpeg2000_getsigctxno(t1->flags[y+1][x+1] & flags_mask, bandno))) {
                        int xorbit, ctxno = ff_jpeg2000_getsgnctxno(t1->flags[y+1][x+1], &xorbit);
                        if (bpass_csty_symbol)
                             t1->data[y][x] = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ctxno) ? -mask : mask;
                        else
                             t1->data[y][x] = (ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ctxno) ^ xorbit) ?
                                               -mask : mask;

                        ff_jpeg2000_set_significance(t1, x, y,
                                                     t1->data[y][x] < 0);
                    }
                    t1->flags[y + 1][x + 1] |= JPEG2000_T1_VIS;
                }
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
                           int seg_symbols, int vert_causal_ctx_csty_symbol)
{
    int mask = 3 << (bpno - 1), y0, x, y, runlen, dec;

    for (y0 = 0; y0 < height; y0 += 4) {
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
                    if (!(t1->flags[y+1][x+1] & (JPEG2000_T1_SIG | JPEG2000_T1_VIS))) {
                        int flags_mask = -1;
                        if (vert_causal_ctx_csty_symbol && y == y0 + 3)
                            flags_mask &= ~(JPEG2000_T1_SIG_S | JPEG2000_T1_SIG_SW | JPEG2000_T1_SIG_SE);
                        dec = ff_mqc_decode(&t1->mqc, t1->mqc.cx_states + ff_jpeg2000_getsigctxno(t1->flags[y+1][x+1] & flags_mask,
                                                                                             bandno));
                    }
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
    int clnpass_cnt = 0;
    int bpass_csty_symbol           = codsty->cblk_style & JPEG2000_CBLK_BYPASS;
    int vert_causal_ctx_csty_symbol = codsty->cblk_style & JPEG2000_CBLK_VSC;

    av_assert0(width  <= JPEG2000_MAX_CBLKW);
    av_assert0(height <= JPEG2000_MAX_CBLKH);

    for (y = 0; y < height; y++)
        memset(t1->data[y], 0, width * sizeof(**t1->data));

    /* If code-block contains no compressed data: nothing to do. */
    if (!cblk->length)
        return 0;

    for (y = 0; y < height + 2; y++)
        memset(t1->flags[y], 0, (width + 2) * sizeof(**t1->flags));

    cblk->data[cblk->length] = 0xff;
    cblk->data[cblk->length+1] = 0xff;
    ff_mqc_initdec(&t1->mqc, cblk->data);

    while (passno--) {
        switch(pass_t) {
        case 0:
            decode_sigpass(t1, width, height, bpno + 1, bandpos,
                           bpass_csty_symbol && (clnpass_cnt >= 4),
                           vert_causal_ctx_csty_symbol);
            break;
        case 1:
            decode_refpass(t1, width, height, bpno + 1);
            if (bpass_csty_symbol && clnpass_cnt >= 4)
                ff_mqc_initdec(&t1->mqc, cblk->data);
            break;
        case 2:
            decode_clnpass(s, t1, width, height, bpno + 1, bandpos,
                           codsty->cblk_style & JPEG2000_CBLK_SEGSYM,
                           vert_causal_ctx_csty_symbol);
            clnpass_cnt = clnpass_cnt + 1;
            if (bpass_csty_symbol && clnpass_cnt >= 4)
                ff_mqc_initdec(&t1->mqc, cblk->data);
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
    int i, j;
    int w = cblk->coord[0][1] - cblk->coord[0][0];
    for (j = 0; j < (cblk->coord[1][1] - cblk->coord[1][0]); ++j) {
        float *datap = &comp->f_data[(comp->coord[0][1] - comp->coord[0][0]) * (y + j) + x];
        int *src = t1->data[j];
        for (i = 0; i < w; ++i)
            datap[i] = src[i] * band->f_stepsize;
    }
}

/* Integer dequantization of a codeblock.*/
static void dequantization_int(int x, int y, Jpeg2000Cblk *cblk,
                               Jpeg2000Component *comp,
                               Jpeg2000T1Context *t1, Jpeg2000Band *band)
{
    int i, j;
    int w = cblk->coord[0][1] - cblk->coord[0][0];
    for (j = 0; j < (cblk->coord[1][1] - cblk->coord[1][0]); ++j) {
        int32_t *datap = &comp->i_data[(comp->coord[0][1] - comp->coord[0][0]) * (y + j) + x];
        int *src = t1->data[j];
        for (i = 0; i < w; ++i)
            datap[i] = (src[i] * band->i_stepsize + (1 << 14)) >> 15;
    }
}

static inline void mct_decode(Jpeg2000DecoderContext *s, Jpeg2000Tile *tile)
{
    int i, csize = 1;
    void *src[3];

    for (i = 1; i < 3; i++)
        if (tile->codsty[0].transform != tile->codsty[i].transform) {
            av_log(s->avctx, AV_LOG_ERROR, "Transforms mismatch, MCT not supported\n");
            return;
        }

    for (i = 0; i < 3; i++)
        if (tile->codsty[0].transform == FF_DWT97)
            src[i] = tile->comp[i].f_data;
        else
            src[i] = tile->comp[i].i_data;

    for (i = 0; i < 2; i++)
        csize *= tile->comp[0].coord[i][1] - tile->comp[0].coord[i][0];

    s->dsp.mct_decode[tile->codsty[0].transform](src[0], src[1], src[2], csize);
}

static int jpeg2000_decode_tile(Jpeg2000DecoderContext *s, Jpeg2000Tile *tile,
                                AVFrame *picture)
{
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(s->avctx->pix_fmt);
    int compno, reslevelno, bandno;
    int x, y;
    int planar    = !!(pixdesc->flags & AV_PIX_FMT_FLAG_PLANAR);
    int pixelsize = planar ? 1 : pixdesc->nb_components;

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
                int nb_precincts, precno;
                Jpeg2000Band *band = rlevel->band + bandno;
                int cblkno = 0, bandpos;

                bandpos = bandno + (reslevelno > 0);

                if (band->coord[0][0] == band->coord[0][1] ||
                    band->coord[1][0] == band->coord[1][1])
                    continue;

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

                        x = cblk->coord[0][0];
                        y = cblk->coord[1][0];

                        if (codsty->transform == FF_DWT97)
                            dequantization_float(x, y, cblk, comp, &t1, band);
                        else
                            dequantization_int(x, y, cblk, comp, &t1, band);
                   } /* end cblk */
                } /*end prec */
            } /* end band */
        } /* end reslevel */

        /* inverse DWT */
        ff_dwt_decode(&comp->dwt, codsty->transform == FF_DWT97 ? (void*)comp->f_data : (void*)comp->i_data);
    } /*end comp */

    /* inverse MCT transformation */
    if (tile->codsty[0].mct)
        mct_decode(s, tile);

    if (s->cdef[0] < 0) {
        for (x = 0; x < s->ncomponents; x++)
            s->cdef[x] = x + 1;
        if ((s->ncomponents & 1) == 0)
            s->cdef[s->ncomponents-1] = 0;
    }

    if (s->precision <= 8) {
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp = tile->comp + compno;
            Jpeg2000CodingStyle *codsty = tile->codsty + compno;
            float *datap = comp->f_data;
            int32_t *i_datap = comp->i_data;
            int cbps = s->cbps[compno];
            int w = tile->comp[compno].coord[0][1] - s->image_offset_x;
            int plane = 0;

            if (planar)
                plane = s->cdef[compno] ? s->cdef[compno]-1 : (s->ncomponents-1);


            y    = tile->comp[compno].coord[1][0] - s->image_offset_y;
            line = picture->data[plane] + y / s->cdy[compno] * picture->linesize[plane];
            for (; y < tile->comp[compno].coord[1][1] - s->image_offset_y; y += s->cdy[compno]) {
                uint8_t *dst;

                x   = tile->comp[compno].coord[0][0] - s->image_offset_x;
                dst = line + x / s->cdx[compno] * pixelsize + compno*!planar;

                if (codsty->transform == FF_DWT97) {
                    for (; x < w; x += s->cdx[compno]) {
                        int val = lrintf(*datap) + (1 << (cbps - 1));
                        /* DC level shift and clip see ISO 15444-1:2002 G.1.2 */
                        val = av_clip(val, 0, (1 << cbps) - 1);
                        *dst = val << (8 - cbps);
                        datap++;
                        dst += pixelsize;
                    }
                } else {
                    for (; x < w; x += s->cdx[compno]) {
                        int val = *i_datap + (1 << (cbps - 1));
                        /* DC level shift and clip see ISO 15444-1:2002 G.1.2 */
                        val = av_clip(val, 0, (1 << cbps) - 1);
                        *dst = val << (8 - cbps);
                        i_datap++;
                        dst += pixelsize;
                    }
                }
                line += picture->linesize[plane];
            }
        }
    } else {
        for (compno = 0; compno < s->ncomponents; compno++) {
            Jpeg2000Component *comp = tile->comp + compno;
            Jpeg2000CodingStyle *codsty = tile->codsty + compno;
            float *datap = comp->f_data;
            int32_t *i_datap = comp->i_data;
            uint16_t *linel;
            int cbps = s->cbps[compno];
            int w = tile->comp[compno].coord[0][1] - s->image_offset_x;
            int plane = 0;

            if (planar)
                plane = s->cdef[compno] ? s->cdef[compno]-1 : (s->ncomponents-1);

            y     = tile->comp[compno].coord[1][0] - s->image_offset_y;
            linel = (uint16_t *)picture->data[plane] + y / s->cdy[compno] * (picture->linesize[plane] >> 1);
            for (; y < tile->comp[compno].coord[1][1] - s->image_offset_y; y += s->cdy[compno]) {
                uint16_t *dst;

                x   = tile->comp[compno].coord[0][0] - s->image_offset_x;
                dst = linel + (x / s->cdx[compno] * pixelsize + compno*!planar);
                if (codsty->transform == FF_DWT97) {
                    for (; x < w; x += s-> cdx[compno]) {
                        int  val = lrintf(*datap) + (1 << (cbps - 1));
                        /* DC level shift and clip see ISO 15444-1:2002 G.1.2 */
                        val = av_clip(val, 0, (1 << cbps) - 1);
                        /* align 12 bit values in little-endian mode */
                        *dst = val << (16 - cbps);
                        datap++;
                        dst += pixelsize;
                    }
                } else {
                    for (; x < w; x += s-> cdx[compno]) {
                        int val = *i_datap + (1 << (cbps - 1));
                        /* DC level shift and clip see ISO 15444-1:2002 G.1.2 */
                        val = av_clip(val, 0, (1 << cbps) - 1);
                        /* align 12 bit values in little-endian mode */
                        *dst = val << (16 - cbps);
                        i_datap++;
                        dst += pixelsize;
                    }
                }
                linel += picture->linesize[plane] >> 1;
            }
        }
    }

    return 0;
}

static void jpeg2000_dec_cleanup(Jpeg2000DecoderContext *s)
{
    int tileno, compno;
    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++) {
        if (s->tile[tileno].comp) {
            for (compno = 0; compno < s->ncomponents; compno++) {
                Jpeg2000Component *comp     = s->tile[tileno].comp   + compno;
                Jpeg2000CodingStyle *codsty = s->tile[tileno].codsty + compno;

                ff_jpeg2000_cleanup(comp, codsty);
            }
            av_freep(&s->tile[tileno].comp);
        }
    }
    av_freep(&s->tile);
    memset(s->codsty, 0, sizeof(s->codsty));
    memset(s->qntsty, 0, sizeof(s->qntsty));
    s->numXtiles = s->numYtiles = 0;
}

static int jpeg2000_read_main_headers(Jpeg2000DecoderContext *s)
{
    Jpeg2000CodingStyle *codsty = s->codsty;
    Jpeg2000QuantStyle *qntsty  = s->qntsty;
    uint8_t *properties         = s->properties;

    for (;;) {
        int len, ret = 0;
        uint16_t marker;
        int oldpos;

        if (bytestream2_get_bytes_left(&s->g) < 2) {
            av_log(s->avctx, AV_LOG_ERROR, "Missing EOC\n");
            break;
        }

        marker = bytestream2_get_be16u(&s->g);
        oldpos = bytestream2_tell(&s->g);

        if (marker == JPEG2000_SOD) {
            Jpeg2000Tile *tile;
            Jpeg2000TilePart *tp;

            if (!s->tile) {
                av_log(s->avctx, AV_LOG_ERROR, "Missing SIZ\n");
                return AVERROR_INVALIDDATA;
            }
            if (s->curtileno < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Missing SOT\n");
                return AVERROR_INVALIDDATA;
            }

            tile = s->tile + s->curtileno;
            tp = tile->tile_part + tile->tp_idx;
            if (tp->tp_end < s->g.buffer) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid tpend\n");
                return AVERROR_INVALIDDATA;
            }
            bytestream2_init(&tp->tpg, s->g.buffer, tp->tp_end - s->g.buffer);
            bytestream2_skip(&s->g, tp->tp_end - s->g.buffer);

            continue;
        }
        if (marker == JPEG2000_EOC)
            break;

        len = bytestream2_get_be16(&s->g);
        if (len < 2 || bytestream2_get_bytes_left(&s->g) < len - 2)
            return AVERROR_INVALIDDATA;

        switch (marker) {
        case JPEG2000_SIZ:
            ret = get_siz(s);
            if (!s->tile)
                s->numXtiles = s->numYtiles = 0;
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
            if (!(ret = get_sot(s, len))) {
                av_assert1(s->curtileno >= 0);
                codsty = s->tile[s->curtileno].codsty;
                qntsty = s->tile[s->curtileno].qntsty;
                properties = s->tile[s->curtileno].properties;
            }
            break;
        case JPEG2000_COM:
            // the comment is ignored
            bytestream2_skip(&s->g, len - 2);
            break;
        case JPEG2000_TLM:
            // Tile-part lengths
            ret = get_tlm(s, len);
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR,
                   "unsupported marker 0x%.4"PRIX16" at pos 0x%X\n",
                   marker, bytestream2_tell(&s->g) - 4);
            bytestream2_skip(&s->g, len - 2);
            break;
        }
        if (bytestream2_tell(&s->g) - oldpos != len || ret) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "error during processing marker segment %.4"PRIx16"\n",
                   marker);
            return ret ? ret : -1;
        }
    }
    return 0;
}

/* Read bit stream packets --> T2 operation. */
static int jpeg2000_read_bitstream_packets(Jpeg2000DecoderContext *s)
{
    int ret = 0;
    int tileno;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++) {
        Jpeg2000Tile *tile = s->tile + tileno;

        if (ret = init_tile(s, tileno))
            return ret;

        s->g = tile->tile_part[0].tpg;
        if (ret = jpeg2000_decode_packets(s, tile))
            return ret;
    }

    return 0;
}

static int jp2_find_codestream(Jpeg2000DecoderContext *s)
{
    uint32_t atom_size, atom, atom_end;
    int search_range = 10;

    while (search_range
           &&
           bytestream2_get_bytes_left(&s->g) >= 8) {
        atom_size = bytestream2_get_be32u(&s->g);
        atom      = bytestream2_get_be32u(&s->g);
        atom_end  = bytestream2_tell(&s->g) + atom_size - 8;

        if (atom == JP2_CODESTREAM)
            return 1;

        if (bytestream2_get_bytes_left(&s->g) < atom_size || atom_end < atom_size)
            return 0;

        if (atom == JP2_HEADER &&
                   atom_size >= 16) {
            uint32_t atom2_size, atom2, atom2_end;
            do {
                atom2_size = bytestream2_get_be32u(&s->g);
                atom2      = bytestream2_get_be32u(&s->g);
                atom2_end  = bytestream2_tell(&s->g) + atom2_size - 8;
                if (atom2_size < 8 || atom2_end > atom_end || atom2_end < atom2_size)
                    break;
                if (atom2 == JP2_CODESTREAM) {
                    return 1;
                } else if (atom2 == MKBETAG('c','o','l','r') && atom2_size >= 7) {
                    int method = bytestream2_get_byteu(&s->g);
                    bytestream2_skipu(&s->g, 2);
                    if (method == 1) {
                        s->colour_space = bytestream2_get_be32u(&s->g);
                    }
                } else if (atom2 == MKBETAG('p','c','l','r') && atom2_size >= 6) {
                    int i, size, colour_count, colour_channels, colour_depth[3];
                    uint32_t r, g, b;
                    colour_count = bytestream2_get_be16u(&s->g);
                    colour_channels = bytestream2_get_byteu(&s->g);
                    // FIXME: Do not ignore channel_sign
                    colour_depth[0] = (bytestream2_get_byteu(&s->g) & 0x7f) + 1;
                    colour_depth[1] = (bytestream2_get_byteu(&s->g) & 0x7f) + 1;
                    colour_depth[2] = (bytestream2_get_byteu(&s->g) & 0x7f) + 1;
                    size = (colour_depth[0] + 7 >> 3) * colour_count +
                           (colour_depth[1] + 7 >> 3) * colour_count +
                           (colour_depth[2] + 7 >> 3) * colour_count;
                    if (colour_count > 256   ||
                        colour_channels != 3 ||
                        colour_depth[0] > 16 ||
                        colour_depth[1] > 16 ||
                        colour_depth[2] > 16 ||
                        atom2_size < size) {
                        avpriv_request_sample(s->avctx, "Unknown palette");
                        bytestream2_seek(&s->g, atom2_end, SEEK_SET);
                        continue;
                    }
                    s->pal8 = 1;
                    for (i = 0; i < colour_count; i++) {
                        if (colour_depth[0] <= 8) {
                            r = bytestream2_get_byteu(&s->g) << 8 - colour_depth[0];
                            r |= r >> colour_depth[0];
                        } else {
                            r = bytestream2_get_be16u(&s->g) >> colour_depth[0] - 8;
                        }
                        if (colour_depth[1] <= 8) {
                            g = bytestream2_get_byteu(&s->g) << 8 - colour_depth[1];
                            r |= r >> colour_depth[1];
                        } else {
                            g = bytestream2_get_be16u(&s->g) >> colour_depth[1] - 8;
                        }
                        if (colour_depth[2] <= 8) {
                            b = bytestream2_get_byteu(&s->g) << 8 - colour_depth[2];
                            r |= r >> colour_depth[2];
                        } else {
                            b = bytestream2_get_be16u(&s->g) >> colour_depth[2] - 8;
                        }
                        s->palette[i] = 0xffu << 24 | r << 16 | g << 8 | b;
                    }
                } else if (atom2 == MKBETAG('c','d','e','f') && atom2_size >= 2) {
                    int n = bytestream2_get_be16u(&s->g);
                    for (; n>0; n--) {
                        int cn   = bytestream2_get_be16(&s->g);
                        int av_unused typ  = bytestream2_get_be16(&s->g);
                        int asoc = bytestream2_get_be16(&s->g);
                        if (cn < 4 || asoc < 4)
                            s->cdef[cn] = asoc;
                    }
                }
                bytestream2_seek(&s->g, atom2_end, SEEK_SET);
            } while (atom_end - atom2_end >= 8);
        } else {
            search_range--;
        }
        bytestream2_seek(&s->g, atom_end, SEEK_SET);
    }

    return 0;
}

static av_cold int jpeg2000_decode_init(AVCodecContext *avctx)
{
    Jpeg2000DecoderContext *s = avctx->priv_data;

    ff_jpeg2000dsp_init(&s->dsp);

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
    bytestream2_init(&s->g, avpkt->data, avpkt->size);
    s->curtileno = -1;
    memset(s->cdef, -1, sizeof(s->cdef));

    if (bytestream2_get_bytes_left(&s->g) < 2) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    // check if the image is in jp2 format
    if (bytestream2_get_bytes_left(&s->g) >= 12 &&
       (bytestream2_get_be32u(&s->g) == 12) &&
       (bytestream2_get_be32u(&s->g) == JP2_SIG_TYPE) &&
       (bytestream2_get_be32u(&s->g) == JP2_SIG_VALUE)) {
        if (!jp2_find_codestream(s)) {
            av_log(avctx, AV_LOG_ERROR,
                   "Could not find Jpeg2000 codestream atom.\n");
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
    } else {
        bytestream2_seek(&s->g, 0, SEEK_SET);
    }

    while (bytestream2_get_bytes_left(&s->g) >= 3 && bytestream2_peek_be16(&s->g) != JPEG2000_SOC)
        bytestream2_skip(&s->g, 1);

    if (bytestream2_get_be16u(&s->g) != JPEG2000_SOC) {
        av_log(avctx, AV_LOG_ERROR, "SOC marker not present\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    if (ret = jpeg2000_read_main_headers(s))
        goto end;

    /* get picture buffer */
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        goto end;
    picture->pict_type = AV_PICTURE_TYPE_I;
    picture->key_frame = 1;

    if (ret = jpeg2000_read_bitstream_packets(s))
        goto end;

    for (tileno = 0; tileno < s->numXtiles * s->numYtiles; tileno++)
        if (ret = jpeg2000_decode_tile(s, s->tile + tileno, picture))
            goto end;

    jpeg2000_dec_cleanup(s);

    *got_frame = 1;

    if (s->avctx->pix_fmt == AV_PIX_FMT_PAL8)
        memcpy(picture->data[1], s->palette, 256 * sizeof(uint32_t));

    return bytestream2_tell(&s->g);

end:
    jpeg2000_dec_cleanup(s);
    return ret;
}

static av_cold void jpeg2000_init_static_data(AVCodec *codec)
{
    ff_jpeg2000_init_tier1_luts();
    ff_mqc_init_context_tables();
}

#define OFFSET(x) offsetof(Jpeg2000DecoderContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "lowres",  "Lower the decoding resolution by a power of two",
        OFFSET(reduction_factor), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, JPEG2000_MAX_RESLEVELS - 1, VD },
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

static const AVClass jpeg2000_class = {
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
    .init             = jpeg2000_decode_init,
    .decode           = jpeg2000_decode_frame,
    .priv_class       = &jpeg2000_class,
    .max_lowres       = 5,
    .profiles         = NULL_IF_CONFIG_SMALL(profiles)
};
