/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard
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

#include "avcodec.h"
#include "internal.h"
#include "bytestream.h"
#include "huffyuvencdsp.h"
#include "png.h"
#include "apng.h"

#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/libm.h"
#include "libavutil/opt.h"
#include "libavutil/color_utils.h"

#include <zlib.h>

#define IOBUF_SIZE 4096

typedef struct PNGEncContext {
    AVClass *class;
    HuffYUVEncDSPContext hdsp;

    uint8_t *bytestream;
    uint8_t *bytestream_start;
    uint8_t *bytestream_end;

    int filter_type;

    z_stream zstream;
    uint8_t buf[IOBUF_SIZE];
    int dpi;                     ///< Physical pixel density, in dots per inch, if set
    int dpm;                     ///< Physical pixel density, in dots per meter, if set

    int is_progressive;
    int bit_depth;
    int color_type;
    int bits_per_pixel;

    // APNG
    uint32_t palette_checksum;   // Used to ensure a single unique palette
    uint32_t sequence_number;
} PNGEncContext;

static void png_get_interlaced_row(uint8_t *dst, int row_size,
                                   int bits_per_pixel, int pass,
                                   const uint8_t *src, int width)
{
    int x, mask, dst_x, j, b, bpp;
    uint8_t *d;
    const uint8_t *s;
    static const int masks[] = {0x80, 0x08, 0x88, 0x22, 0xaa, 0x55, 0xff};

    mask = masks[pass];
    switch (bits_per_pixel) {
    case 1:
        memset(dst, 0, row_size);
        dst_x = 0;
        for (x = 0; x < width; x++) {
            j = (x & 7);
            if ((mask << j) & 0x80) {
                b = (src[x >> 3] >> (7 - j)) & 1;
                dst[dst_x >> 3] |= b << (7 - (dst_x & 7));
                dst_x++;
            }
        }
        break;
    default:
        bpp = bits_per_pixel >> 3;
        d = dst;
        s = src;
        for (x = 0; x < width; x++) {
            j = x & 7;
            if ((mask << j) & 0x80) {
                memcpy(d, s, bpp);
                d += bpp;
            }
            s += bpp;
        }
        break;
    }
}

static void sub_png_paeth_prediction(uint8_t *dst, uint8_t *src, uint8_t *top,
                                     int w, int bpp)
{
    int i;
    for (i = 0; i < w; i++) {
        int a, b, c, p, pa, pb, pc;

        a = src[i - bpp];
        b = top[i];
        c = top[i - bpp];

        p  = b - c;
        pc = a - c;

        pa = abs(p);
        pb = abs(pc);
        pc = abs(p + pc);

        if (pa <= pb && pa <= pc)
            p = a;
        else if (pb <= pc)
            p = b;
        else
            p = c;
        dst[i] = src[i] - p;
    }
}

static void sub_left_prediction(PNGEncContext *c, uint8_t *dst, const uint8_t *src, int bpp, int size)
{
    const uint8_t *src1 = src + bpp;
    const uint8_t *src2 = src;
    int x, unaligned_w;

    memcpy(dst, src, bpp);
    dst += bpp;
    size -= bpp;
    unaligned_w = FFMIN(32 - bpp, size);
    for (x = 0; x < unaligned_w; x++)
        *dst++ = *src1++ - *src2++;
    size -= unaligned_w;
    c->hdsp.diff_bytes(dst, src1, src2, size);
}

static void png_filter_row(PNGEncContext *c, uint8_t *dst, int filter_type,
                           uint8_t *src, uint8_t *top, int size, int bpp)
{
    int i;

    switch (filter_type) {
    case PNG_FILTER_VALUE_NONE:
        memcpy(dst, src, size);
        break;
    case PNG_FILTER_VALUE_SUB:
        sub_left_prediction(c, dst, src, bpp, size);
        break;
    case PNG_FILTER_VALUE_UP:
        c->hdsp.diff_bytes(dst, src, top, size);
        break;
    case PNG_FILTER_VALUE_AVG:
        for (i = 0; i < bpp; i++)
            dst[i] = src[i] - (top[i] >> 1);
        for (; i < size; i++)
            dst[i] = src[i] - ((src[i - bpp] + top[i]) >> 1);
        break;
    case PNG_FILTER_VALUE_PAETH:
        for (i = 0; i < bpp; i++)
            dst[i] = src[i] - top[i];
        sub_png_paeth_prediction(dst + i, src + i, top + i, size - i, bpp);
        break;
    }
}

static uint8_t *png_choose_filter(PNGEncContext *s, uint8_t *dst,
                                  uint8_t *src, uint8_t *top, int size, int bpp)
{
    int pred = s->filter_type;
    av_assert0(bpp || !pred);
    if (!top && pred)
        pred = PNG_FILTER_VALUE_SUB;
    if (pred == PNG_FILTER_VALUE_MIXED) {
        int i;
        int cost, bcost = INT_MAX;
        uint8_t *buf1 = dst, *buf2 = dst + size + 16;
        for (pred = 0; pred < 5; pred++) {
            png_filter_row(s, buf1 + 1, pred, src, top, size, bpp);
            buf1[0] = pred;
            cost = 0;
            for (i = 0; i <= size; i++)
                cost += abs((int8_t) buf1[i]);
            if (cost < bcost) {
                bcost = cost;
                FFSWAP(uint8_t *, buf1, buf2);
            }
        }
        return buf2;
    } else {
        png_filter_row(s, dst + 1, pred, src, top, size, bpp);
        dst[0] = pred;
        return dst;
    }
}

static void png_write_chunk(uint8_t **f, uint32_t tag,
                            const uint8_t *buf, int length)
{
    const AVCRC *crc_table = av_crc_get_table(AV_CRC_32_IEEE_LE);
    uint32_t crc = ~0U;
    uint8_t tagbuf[4];

    bytestream_put_be32(f, length);
    AV_WL32(tagbuf, tag);
    crc = av_crc(crc_table, crc, tagbuf, 4);
    bytestream_put_be32(f, av_bswap32(tag));
    if (length > 0) {
        crc = av_crc(crc_table, crc, buf, length);
        memcpy(*f, buf, length);
        *f += length;
    }
    bytestream_put_be32(f, ~crc);
}

static void png_write_image_data(AVCodecContext *avctx,
                                 const uint8_t *buf, int length)
{
    PNGEncContext *s = avctx->priv_data;
    const AVCRC *crc_table = av_crc_get_table(AV_CRC_32_IEEE_LE);
    uint32_t crc = ~0U;

    if (avctx->codec_id == AV_CODEC_ID_PNG || avctx->frame_number == 0) {
        png_write_chunk(&s->bytestream, MKTAG('I', 'D', 'A', 'T'), buf, length);
        return;
    }

    bytestream_put_be32(&s->bytestream, length + 4);

    bytestream_put_be32(&s->bytestream, MKBETAG('f', 'd', 'A', 'T'));
    bytestream_put_be32(&s->bytestream, s->sequence_number);
    crc = av_crc(crc_table, crc, s->bytestream - 8, 8);

    crc = av_crc(crc_table, crc, buf, length);
    memcpy(s->bytestream, buf, length);
    s->bytestream += length;

    bytestream_put_be32(&s->bytestream, ~crc);

    ++s->sequence_number;
}

/* XXX: do filtering */
static int png_write_row(AVCodecContext *avctx, const uint8_t *data, int size)
{
    PNGEncContext *s = avctx->priv_data;
    int ret;

    s->zstream.avail_in = size;
    s->zstream.next_in  = data;
    while (s->zstream.avail_in > 0) {
        ret = deflate(&s->zstream, Z_NO_FLUSH);
        if (ret != Z_OK)
            return -1;
        if (s->zstream.avail_out == 0) {
            if (s->bytestream_end - s->bytestream > IOBUF_SIZE + 100)
                png_write_image_data(avctx, s->buf, IOBUF_SIZE);
            s->zstream.avail_out = IOBUF_SIZE;
            s->zstream.next_out  = s->buf;
        }
    }
    return 0;
}

#define AV_WB32_PNG(buf, n) AV_WB32(buf, lrint((n) * 100000))
static int png_get_chrm(enum AVColorPrimaries prim,  uint8_t *buf)
{
    double rx, ry, gx, gy, bx, by, wx = 0.3127, wy = 0.3290;
    switch (prim) {
        case AVCOL_PRI_BT709:
            rx = 0.640; ry = 0.330;
            gx = 0.300; gy = 0.600;
            bx = 0.150; by = 0.060;
            break;
        case AVCOL_PRI_BT470M:
            rx = 0.670; ry = 0.330;
            gx = 0.210; gy = 0.710;
            bx = 0.140; by = 0.080;
            wx = 0.310; wy = 0.316;
            break;
        case AVCOL_PRI_BT470BG:
            rx = 0.640; ry = 0.330;
            gx = 0.290; gy = 0.600;
            bx = 0.150; by = 0.060;
            break;
        case AVCOL_PRI_SMPTE170M:
        case AVCOL_PRI_SMPTE240M:
            rx = 0.630; ry = 0.340;
            gx = 0.310; gy = 0.595;
            bx = 0.155; by = 0.070;
            break;
        case AVCOL_PRI_BT2020:
            rx = 0.708; ry = 0.292;
            gx = 0.170; gy = 0.797;
            bx = 0.131; by = 0.046;
            break;
        default:
            return 0;
    }

    AV_WB32_PNG(buf     , wx); AV_WB32_PNG(buf + 4 , wy);
    AV_WB32_PNG(buf + 8 , rx); AV_WB32_PNG(buf + 12, ry);
    AV_WB32_PNG(buf + 16, gx); AV_WB32_PNG(buf + 20, gy);
    AV_WB32_PNG(buf + 24, bx); AV_WB32_PNG(buf + 28, by);
    return 1;
}

static int png_get_gama(enum AVColorTransferCharacteristic trc, uint8_t *buf)
{
    double gamma = avpriv_get_gamma_from_trc(trc);
    if (gamma <= 1e-6)
        return 0;

    AV_WB32_PNG(buf, 1.0 / gamma);
    return 1;
}

static int encode_headers(AVCodecContext *avctx, const AVFrame *pict)
{
    PNGEncContext *s = avctx->priv_data;

    /* write png header */
    AV_WB32(s->buf, avctx->width);
    AV_WB32(s->buf + 4, avctx->height);
    s->buf[8]  = s->bit_depth;
    s->buf[9]  = s->color_type;
    s->buf[10] = 0; /* compression type */
    s->buf[11] = 0; /* filter type */
    s->buf[12] = s->is_progressive; /* interlace type */
    png_write_chunk(&s->bytestream, MKTAG('I', 'H', 'D', 'R'), s->buf, 13);

    /* write physical information */
    if (s->dpm) {
      AV_WB32(s->buf, s->dpm);
      AV_WB32(s->buf + 4, s->dpm);
      s->buf[8] = 1; /* unit specifier is meter */
    } else {
      AV_WB32(s->buf, avctx->sample_aspect_ratio.num);
      AV_WB32(s->buf + 4, avctx->sample_aspect_ratio.den);
      s->buf[8] = 0; /* unit specifier is unknown */
    }
    png_write_chunk(&s->bytestream, MKTAG('p', 'H', 'Y', 's'), s->buf, 9);

    /* write colorspace information */
    if (pict->color_primaries == AVCOL_PRI_BT709 &&
        pict->color_trc == AVCOL_TRC_IEC61966_2_1) {
        s->buf[0] = 1; /* rendering intent, relative colorimetric by default */
        png_write_chunk(&s->bytestream, MKTAG('s', 'R', 'G', 'B'), s->buf, 1);
    }

    if (png_get_chrm(pict->color_primaries, s->buf))
        png_write_chunk(&s->bytestream, MKTAG('c', 'H', 'R', 'M'), s->buf, 32);
    if (png_get_gama(pict->color_trc, s->buf))
        png_write_chunk(&s->bytestream, MKTAG('g', 'A', 'M', 'A'), s->buf, 4);

    /* put the palette if needed */
    if (s->color_type == PNG_COLOR_TYPE_PALETTE) {
        int has_alpha, alpha, i;
        unsigned int v;
        uint32_t *palette;
        uint8_t *ptr, *alpha_ptr;

        palette   = (uint32_t *)pict->data[1];
        ptr       = s->buf;
        alpha_ptr = s->buf + 256 * 3;
        has_alpha = 0;
        for (i = 0; i < 256; i++) {
            v     = palette[i];
            alpha = v >> 24;
            if (alpha != 0xff)
                has_alpha = 1;
            *alpha_ptr++ = alpha;
            bytestream_put_be24(&ptr, v);
        }
        png_write_chunk(&s->bytestream,
                        MKTAG('P', 'L', 'T', 'E'), s->buf, 256 * 3);
        if (has_alpha) {
            png_write_chunk(&s->bytestream,
                            MKTAG('t', 'R', 'N', 'S'), s->buf + 256 * 3, 256);
        }
    }

    return 0;
}

static int encode_frame(AVCodecContext *avctx, const AVFrame *pict)
{
    PNGEncContext *s       = avctx->priv_data;
    const AVFrame *const p = pict;
    int y, len, ret;
    int row_size, pass_row_size;
    uint8_t *ptr, *top, *crow_buf, *crow;
    uint8_t *crow_base       = NULL;
    uint8_t *progressive_buf = NULL;
    uint8_t *top_buf         = NULL;

    row_size = (avctx->width * s->bits_per_pixel + 7) >> 3;

    crow_base = av_malloc((row_size + 32) << (s->filter_type == PNG_FILTER_VALUE_MIXED));
    if (!crow_base) {
        ret = AVERROR(ENOMEM);
        goto the_end;
    }
    // pixel data should be aligned, but there's a control byte before it
    crow_buf = crow_base + 15;
    if (s->is_progressive) {
        progressive_buf = av_malloc(row_size + 1);
        top_buf = av_malloc(row_size + 1);
        if (!progressive_buf || !top_buf) {
            ret = AVERROR(ENOMEM);
            goto the_end;
        }
    }

    /* put each row */
    s->zstream.avail_out = IOBUF_SIZE;
    s->zstream.next_out  = s->buf;
    if (s->is_progressive) {
        int pass;

        for (pass = 0; pass < NB_PASSES; pass++) {
            /* NOTE: a pass is completely omitted if no pixels would be
             * output */
            pass_row_size = ff_png_pass_row_size(pass, s->bits_per_pixel, avctx->width);
            if (pass_row_size > 0) {
                top = NULL;
                for (y = 0; y < avctx->height; y++)
                    if ((ff_png_pass_ymask[pass] << (y & 7)) & 0x80) {
                        ptr = p->data[0] + y * p->linesize[0];
                        FFSWAP(uint8_t *, progressive_buf, top_buf);
                        png_get_interlaced_row(progressive_buf, pass_row_size,
                                               s->bits_per_pixel, pass,
                                               ptr, avctx->width);
                        crow = png_choose_filter(s, crow_buf, progressive_buf,
                                                 top, pass_row_size, s->bits_per_pixel >> 3);
                        png_write_row(avctx, crow, pass_row_size + 1);
                        top = progressive_buf;
                    }
            }
        }
    } else {
        top = NULL;
        for (y = 0; y < avctx->height; y++) {
            ptr = p->data[0] + y * p->linesize[0];
            crow = png_choose_filter(s, crow_buf, ptr, top,
                                     row_size, s->bits_per_pixel >> 3);
            png_write_row(avctx, crow, row_size + 1);
            top = ptr;
        }
    }
    /* compress last bytes */
    for (;;) {
        ret = deflate(&s->zstream, Z_FINISH);
        if (ret == Z_OK || ret == Z_STREAM_END) {
            len = IOBUF_SIZE - s->zstream.avail_out;
            if (len > 0 && s->bytestream_end - s->bytestream > len + 100) {
                png_write_image_data(avctx, s->buf, len);
            }
            s->zstream.avail_out = IOBUF_SIZE;
            s->zstream.next_out  = s->buf;
            if (ret == Z_STREAM_END)
                break;
        } else {
            ret = -1;
            goto the_end;
        }
    }

    ret = 0;

the_end:
    av_freep(&crow_base);
    av_freep(&progressive_buf);
    av_freep(&top_buf);
    deflateReset(&s->zstream);
    return ret;
}

static int encode_png(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *pict, int *got_packet)
{
    PNGEncContext *s = avctx->priv_data;
    int ret;
    int enc_row_size;
    size_t max_packet_size;

    enc_row_size    = deflateBound(&s->zstream, (avctx->width * s->bits_per_pixel + 7) >> 3);
    max_packet_size =
        FF_MIN_BUFFER_SIZE + // headers
        avctx->height * (
            enc_row_size +
            12 * (((int64_t)enc_row_size + IOBUF_SIZE - 1) / IOBUF_SIZE) // IDAT * ceil(enc_row_size / IOBUF_SIZE)
        );
    if (max_packet_size > INT_MAX)
        return AVERROR(ENOMEM);
    ret = ff_alloc_packet2(avctx, pkt, max_packet_size);
    if (ret < 0)
        return ret;

    s->bytestream_start =
    s->bytestream       = pkt->data;
    s->bytestream_end   = pkt->data + pkt->size;

    AV_WB64(s->bytestream, PNGSIG);
    s->bytestream += 8;

    ret = encode_headers(avctx, pict);
    if (ret < 0)
        return ret;

    ret = encode_frame(avctx, pict);
    if (ret < 0)
        return ret;

    png_write_chunk(&s->bytestream, MKTAG('I', 'E', 'N', 'D'), NULL, 0);

    pkt->size = s->bytestream - s->bytestream_start;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static int encode_apng(AVCodecContext *avctx, AVPacket *pkt,
                       const AVFrame *pict, int *got_packet)
{
    PNGEncContext *s = avctx->priv_data;
    int ret;
    int enc_row_size;
    size_t max_packet_size;
    uint8_t buf[26];

    if (avctx->codec_id == AV_CODEC_ID_APNG && s->color_type == PNG_COLOR_TYPE_PALETTE) {
        uint32_t checksum = ~av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), ~0U, pict->data[1], 256 * sizeof(uint32_t));

        if (avctx->frame_number == 0) {
            s->palette_checksum = checksum;
        } else if (checksum != s->palette_checksum) {
            av_log(avctx, AV_LOG_ERROR,
                   "Input contains more than one unique palette. APNG does not support multiple palettes.\n");
            return -1;
        }
    }

    enc_row_size    = deflateBound(&s->zstream, (avctx->width * s->bits_per_pixel + 7) >> 3);
    max_packet_size =
        FF_MIN_BUFFER_SIZE + // headers
        avctx->height * (
            enc_row_size +
            (4 + 12) * (((int64_t)enc_row_size + IOBUF_SIZE - 1) / IOBUF_SIZE) // fdAT * ceil(enc_row_size / IOBUF_SIZE)
        );
    if (max_packet_size > INT_MAX)
        return AVERROR(ENOMEM);
    ret = ff_alloc_packet2(avctx, pkt, max_packet_size);
    if (ret < 0)
        return ret;

    s->bytestream_start =
    s->bytestream       = pkt->data;
    s->bytestream_end   = pkt->data + pkt->size;

    if (avctx->frame_number == 0) {
        ret = encode_headers(avctx, pict);
        if (ret < 0)
            return ret;

        avctx->extradata = av_malloc(s->bytestream - s->bytestream_start);
        if (!avctx->extradata)
            return AVERROR(ENOMEM);
        avctx->extradata_size = s->bytestream - s->bytestream_start;
        memcpy(avctx->extradata, s->bytestream_start, s->bytestream - s->bytestream_start);

        s->bytestream = s->bytestream_start;
    }

    AV_WB32(buf, s->sequence_number);
    AV_WB32(buf + 4, avctx->width);
    AV_WB32(buf + 8, avctx->height);
    AV_WB32(buf + 12, 0); // x offset
    AV_WB32(buf + 16, 0); // y offset
    AV_WB16(buf + 20, 0); // delay numerator (filled in during muxing)
    AV_WB16(buf + 22, 0); // delay denominator
    buf[24] = APNG_DISPOSE_OP_BACKGROUND;
    buf[25] = APNG_BLEND_OP_SOURCE;
    png_write_chunk(&s->bytestream, MKTAG('f', 'c', 'T', 'L'), buf, 26);
    ++s->sequence_number;

    ret = encode_frame(avctx, pict);
    if (ret < 0)
        return ret;

    pkt->size = s->bytestream - s->bytestream_start;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int png_enc_init(AVCodecContext *avctx)
{
    PNGEncContext *s = avctx->priv_data;
    int compression_level;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_RGBA:
        avctx->bits_per_coded_sample = 32;
        break;
    case AV_PIX_FMT_RGB24:
        avctx->bits_per_coded_sample = 24;
        break;
    case AV_PIX_FMT_GRAY8:
        avctx->bits_per_coded_sample = 0x28;
        break;
    case AV_PIX_FMT_MONOBLACK:
        avctx->bits_per_coded_sample = 1;
        break;
    case AV_PIX_FMT_PAL8:
        avctx->bits_per_coded_sample = 8;
    }

    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;

    ff_huffyuvencdsp_init(&s->hdsp);

    s->filter_type = av_clip(avctx->prediction_method,
                             PNG_FILTER_VALUE_NONE,
                             PNG_FILTER_VALUE_MIXED);
    if (avctx->pix_fmt == AV_PIX_FMT_MONOBLACK)
        s->filter_type = PNG_FILTER_VALUE_NONE;

    if (s->dpi && s->dpm) {
      av_log(avctx, AV_LOG_ERROR, "Only one of 'dpi' or 'dpm' options should be set\n");
      return AVERROR(EINVAL);
    } else if (s->dpi) {
      s->dpm = s->dpi * 10000 / 254;
    }

    s->is_progressive = !!(avctx->flags & CODEC_FLAG_INTERLACED_DCT);
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_RGBA64BE:
        s->bit_depth = 16;
        s->color_type = PNG_COLOR_TYPE_RGB_ALPHA;
        break;
    case AV_PIX_FMT_RGB48BE:
        s->bit_depth = 16;
        s->color_type = PNG_COLOR_TYPE_RGB;
        break;
    case AV_PIX_FMT_RGBA:
        s->bit_depth  = 8;
        s->color_type = PNG_COLOR_TYPE_RGB_ALPHA;
        break;
    case AV_PIX_FMT_RGB24:
        s->bit_depth  = 8;
        s->color_type = PNG_COLOR_TYPE_RGB;
        break;
    case AV_PIX_FMT_GRAY16BE:
        s->bit_depth  = 16;
        s->color_type = PNG_COLOR_TYPE_GRAY;
        break;
    case AV_PIX_FMT_GRAY8:
        s->bit_depth  = 8;
        s->color_type = PNG_COLOR_TYPE_GRAY;
        break;
    case AV_PIX_FMT_GRAY8A:
        s->bit_depth = 8;
        s->color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
        break;
    case AV_PIX_FMT_YA16BE:
        s->bit_depth = 16;
        s->color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
        break;
    case AV_PIX_FMT_MONOBLACK:
        s->bit_depth  = 1;
        s->color_type = PNG_COLOR_TYPE_GRAY;
        break;
    case AV_PIX_FMT_PAL8:
        s->bit_depth  = 8;
        s->color_type = PNG_COLOR_TYPE_PALETTE;
        break;
    default:
        return -1;
    }
    s->bits_per_pixel = ff_png_get_nb_channels(s->color_type) * s->bit_depth;

    s->zstream.zalloc = ff_png_zalloc;
    s->zstream.zfree  = ff_png_zfree;
    s->zstream.opaque = NULL;
    compression_level = avctx->compression_level == FF_COMPRESSION_DEFAULT
                      ? Z_DEFAULT_COMPRESSION
                      : av_clip(avctx->compression_level, 0, 9);
    if (deflateInit2(&s->zstream, compression_level, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return -1;

    return 0;
}

static av_cold int png_enc_close(AVCodecContext *avctx)
{
    PNGEncContext *s = avctx->priv_data;

    deflateEnd(&s->zstream);
    av_frame_free(&avctx->coded_frame);
    return 0;
}

#define OFFSET(x) offsetof(PNGEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"dpi", "Set image resolution (in dots per inch)",  OFFSET(dpi), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 0x10000, VE},
    {"dpm", "Set image resolution (in dots per meter)", OFFSET(dpm), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 0x10000, VE},
    { NULL }
};

static const AVClass pngenc_class = {
    .class_name = "PNG encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass apngenc_class = {
    .class_name = "APNG encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_png_encoder = {
    .name           = "png",
    .long_name      = NULL_IF_CONFIG_SMALL("PNG (Portable Network Graphics) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PNG,
    .priv_data_size = sizeof(PNGEncContext),
    .init           = png_enc_init,
    .close          = png_enc_close,
    .encode2        = encode_png,
    .capabilities   = CODEC_CAP_FRAME_THREADS | CODEC_CAP_INTRA_ONLY,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGB48BE, AV_PIX_FMT_RGBA64BE,
        AV_PIX_FMT_PAL8,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY8A,
        AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_YA16BE,
        AV_PIX_FMT_MONOBLACK, AV_PIX_FMT_NONE
    },
    .priv_class     = &pngenc_class,
};

AVCodec ff_apng_encoder = {
    .name           = "apng",
    .long_name      = NULL_IF_CONFIG_SMALL("APNG (Animated Portable Network Graphics) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_APNG,
    .priv_data_size = sizeof(PNGEncContext),
    .init           = png_enc_init,
    .close          = png_enc_close,
    .encode2        = encode_apng,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGB48BE, AV_PIX_FMT_RGBA64BE,
        AV_PIX_FMT_PAL8,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY8A,
        AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_YA16BE,
        AV_PIX_FMT_MONOBLACK, AV_PIX_FMT_NONE
    },
    .priv_class     = &apngenc_class,
};
