/*
 * BRender PIX (.pix) image decoder
 * Copyright (c) 2012 Aleksi Nurmi
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

/* Tested against samples from I-War / Independence War and Defiance. */

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

#define HEADER1_CHUNK    0x03
#define HEADER2_CHUNK    0x3D
#define IMAGE_DATA_CHUNK 0x21

/* In 8-bit colour mode, 256 colours are available at any time. Which 256
 * colours are available is determined by the contents of the hardware palette
 * (or CLUT). In this case, the palette supplied with BRender (std.pal) has
 * been loaded into the CLUT.
 *
 * The 256 colours in std.pal are divided into seven ranges, or `colour ramps'.
 * The first 64 colours represent shades of grey ranging from very dark grey
 * (black) to very light grey (white). The following colours are 32-element
 * ramps for six colours as shown below.
 */
static const uint32_t std_pal_table[256] = {
    // gray
    0xFF000000, 0xFF030303, 0xFF060606, 0xFF090909, 0xFF0C0C0C, 0xFF0F0F0F,
    0xFF121212, 0xFF151515, 0xFF181818, 0xFF1B1B1B, 0xFF1E1E1E, 0xFF212121,
    0xFF242424, 0xFF272727, 0xFF2A2A2A, 0xFF2D2D2D, 0xFF313131, 0xFF343434,
    0xFF373737, 0xFF3A3A3A, 0xFF3D3D3D, 0xFF404040, 0xFF434343, 0xFF464646,
    0xFF494949, 0xFF4C4C4C, 0xFF4F4F4F, 0xFF525252, 0xFF555555, 0xFF585858,
    0xFF5B5B5B, 0xFF5E5E5E, 0xFF626262, 0xFF656565, 0xFF686868, 0xFF6B6B6B,
    0xFF6E6E6E, 0xFF717171, 0xFF747474, 0xFF777777, 0xFF7A7A7A, 0xFF7D7D7D,
    0xFF808080, 0xFF838383, 0xFF868686, 0xFF898989, 0xFF8C8C8C, 0xFF8F8F8F,
    0xFF939393, 0xFF999999, 0xFFA0A0A0, 0xFFA7A7A7, 0xFFAEAEAE, 0xFFB4B4B4,
    0xFFBBBBBB, 0xFFC2C2C2, 0xFFC9C9C9, 0xFFCFCFCF, 0xFFD6D6D6, 0xFFDDDDDD,
    0xFFE4E4E4, 0xFFEAEAEA, 0xFFF1F1F1, 0xFFF8F8F8,

    // blue
    0xFF000000, 0xFF020209, 0xFF050513, 0xFF07071D, 0xFF0A0A27, 0xFF0C0C31,
    0xFF0F0F3B, 0xFF111145, 0xFF14144F, 0xFF161659, 0xFF181863, 0xFF1B1B6D,
    0xFF1E1E77, 0xFF202080, 0xFF22228A, 0xFF252594, 0xFF28289E, 0xFF2A2AA8,
    0xFF2D2DB2, 0xFF2F2FBC, 0xFF3131C6, 0xFF3434D0, 0xFF3737DA, 0xFF3939E4,
    0xFF3C3CEE, 0xFF5454F0, 0xFF6C6CF2, 0xFF8585F4, 0xFF9D9DF6, 0xFFB5B5F8,
    0xFFCECEFA, 0xFFE6E6FC,

    // green
    0xFF000000, 0xFF020902, 0xFF051305, 0xFF071D07, 0xFF0A270A, 0xFF0C310C,
    0xFF0F3B0F, 0xFF114511, 0xFF144F14, 0xFF165916, 0xFF186318, 0xFF1B6D1B,
    0xFF1E771E, 0xFF208020, 0xFF228A22, 0xFF259425, 0xFF289E28, 0xFF2AA82A,
    0xFF2DB22D, 0xFF2FBC2F, 0xFF31C631, 0xFF34D034, 0xFF37DA37, 0xFF39E439,
    0xFF3CEE3C, 0xFF54F054, 0xFF6CF26C, 0xFF85F485, 0xFF9DF69D, 0xFFB5F8B5,
    0xFFCEFACE, 0xFFE6FCE6,

    // cyan
    0xFF000000, 0xFF020909, 0xFF051313, 0xFF071D1D, 0xFF0A2727, 0xFF0C3131,
    0xFF0F3B3B, 0xFF114545, 0xFF144F4F, 0xFF165959, 0xFF186363, 0xFF1B6D6D,
    0xFF1E7777, 0xFF208080, 0xFF228A8A, 0xFF259494, 0xFF289E9E, 0xFF2AA8A8,
    0xFF2DB2B2, 0xFF2FBCBC, 0xFF31C6C6, 0xFF34D0D0, 0xFF37DADA, 0xFF39E4E4,
    0xFF3CEEEE, 0xFF54F0F0, 0xFF6CF2F2, 0xFF85F4F4, 0xFF9DF6F6, 0xFFB5F8F8,
    0xFFCEFAFA, 0xFFE6FCFC,

    // red
    0xFF000000, 0xFF090202, 0xFF130505, 0xFF1D0707, 0xFF270A0A, 0xFF310C0C,
    0xFF3B0F0F, 0xFF451111, 0xFF4F1414, 0xFF591616, 0xFF631818, 0xFF6D1B1B,
    0xFF771E1E, 0xFF802020, 0xFF8A2222, 0xFF942525, 0xFF9E2828, 0xFFA82A2A,
    0xFFB22D2D, 0xFFBC2F2F, 0xFFC63131, 0xFFD03434, 0xFFDA3737, 0xFFE43939,
    0xFFEE3C3C, 0xFFF05454, 0xFFF26C6C, 0xFFF48585, 0xFFF69D9D, 0xFFF8B5B5,
    0xFFFACECE, 0xFFFCE6E6,

    // magenta
    0xFF000000, 0xFF090209, 0xFF130513, 0xFF1D071D, 0xFF270A27, 0xFF310C31,
    0xFF3B0F3B, 0xFF451145, 0xFF4F144F, 0xFF591659, 0xFF631863, 0xFF6D1B6D,
    0xFF771E77, 0xFF802080, 0xFF8A228A, 0xFF942594, 0xFF9E289E, 0xFFA82AA8,
    0xFFB22DB2, 0xFFBC2FBC, 0xFFC631C6, 0xFFD034D0, 0xFFDA37DA, 0xFFE439E4,
    0xFFEE3CEE, 0xFFF054F0, 0xFFF26CF2, 0xFFF485F4, 0xFFF69DF6, 0xFFF8B5F8,
    0xFFFACEFA, 0xFFFCE6FC,

    // yellow
    0xFF000000, 0xFF090902, 0xFF131305, 0xFF1D1D07, 0xFF27270A, 0xFF31310C,
    0xFF3B3B0F, 0xFF454511, 0xFF4F4F14, 0xFF595916, 0xFF636318, 0xFF6D6D1B,
    0xFF77771E, 0xFF808020, 0xFF8A8A22, 0xFF949425, 0xFF9E9E28, 0xFFA8A82A,
    0xFFB2B22D, 0xFFBCBC2F, 0xFFC6C631, 0xFFD0D034, 0xFFDADA37, 0xFFE4E439,
    0xFFEEEE3C, 0xFFF0F054, 0xFFF2F26C, 0xFFF4F485, 0xFFF6F69D, 0xFFF8F8B5,
    0xFFFAFACE, 0xFFFCFCE6,
};

typedef struct PixHeader {
    int width;
    int height;
    int format;
} PixHeader;

static int pix_decode_header(PixHeader *out, GetByteContext *pgb)
{
    unsigned int header_len = bytestream2_get_be32(pgb);

    out->format = bytestream2_get_byte(pgb);
    bytestream2_skip(pgb, 2);
    out->width  = bytestream2_get_be16(pgb);
    out->height = bytestream2_get_be16(pgb);

    // the header is at least 11 bytes long; we read the first 7
    if (header_len < 11)
        return AVERROR_INVALIDDATA;

    // skip the rest of the header
    bytestream2_skip(pgb, header_len - 7);

    return 0;
}

static int pix_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    int ret, i;
    GetByteContext gb;

    unsigned int bytes_pp;
    unsigned int magic[4];
    unsigned int chunk_type;
    unsigned int data_len;
    unsigned int bytes_per_scanline;
    unsigned int bytes_left;
    PixHeader hdr;

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    magic[0] = bytestream2_get_be32(&gb);
    magic[1] = bytestream2_get_be32(&gb);
    magic[2] = bytestream2_get_be32(&gb);
    magic[3] = bytestream2_get_be32(&gb);

    if (magic[0] != 0x12 ||
        magic[1] != 0x08 ||
        magic[2] != 0x02 ||
        magic[3] != 0x02) {
        av_log(avctx, AV_LOG_ERROR, "Not a BRender PIX file.\n");
        return AVERROR_INVALIDDATA;
    }

    chunk_type = bytestream2_get_be32(&gb);
    if (chunk_type != HEADER1_CHUNK && chunk_type != HEADER2_CHUNK) {
        av_log(avctx, AV_LOG_ERROR, "Invalid chunk type %d.\n", chunk_type);
        return AVERROR_INVALIDDATA;
    }

    ret = pix_decode_header(&hdr, &gb);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid header length.\n");
        return ret;
    }
    switch (hdr.format) {
    case 3:
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        bytes_pp = 1;
        break;
    case 4:
        avctx->pix_fmt = AV_PIX_FMT_RGB555BE;
        bytes_pp = 2;
        break;
    case 5:
        avctx->pix_fmt = AV_PIX_FMT_RGB565BE;
        bytes_pp = 2;
        break;
    case 6:
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
        bytes_pp = 3;
        break;
    case 7:
        avctx->pix_fmt = AV_PIX_FMT_0RGB;
        bytes_pp = 4;
        break;
    case 8: // ARGB
        avctx->pix_fmt = AV_PIX_FMT_ARGB;
        bytes_pp = 4;
        break;
    case 18:
        avctx->pix_fmt = AV_PIX_FMT_YA8;
        bytes_pp = 2;
        break;
    default:
        avpriv_request_sample(avctx, "Format %d", hdr.format);
        return AVERROR_PATCHWELCOME;
    }
    bytes_per_scanline = bytes_pp * hdr.width;

    if (bytestream2_get_bytes_left(&gb) < hdr.height * bytes_per_scanline)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_set_dimensions(avctx, hdr.width, hdr.height)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    chunk_type = bytestream2_get_be32(&gb);

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8 &&
        (chunk_type == HEADER1_CHUNK ||
         chunk_type == HEADER2_CHUNK)) {
        /* read palette data from data[1] */
        PixHeader palhdr;
        uint32_t *pal_out = (uint32_t *)frame->data[1];

        ret = pix_decode_header(&palhdr, &gb);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid palette header length.\n");
            return ret;
        }
        if (palhdr.format != 7)
            avpriv_request_sample(avctx, "Palette not in RGB format");

        chunk_type = bytestream2_get_be32(&gb);
        data_len = bytestream2_get_be32(&gb);
        bytestream2_skip(&gb, 8);
        if (chunk_type != IMAGE_DATA_CHUNK || data_len != 1032 ||
            bytestream2_get_bytes_left(&gb) < 1032) {
            av_log(avctx, AV_LOG_ERROR, "Invalid palette data.\n");
            return AVERROR_INVALIDDATA;
        }
        // palette data is surrounded by 8 null bytes (both top and bottom)
        // convert 0RGB to machine endian format (ARGB32)
        for (i = 0; i < 256; ++i)
            *pal_out++ = (0xFFU << 24) | bytestream2_get_be32u(&gb);
        bytestream2_skip(&gb, 8);

#if FF_API_PALETTE_HAS_CHANGED
FF_DISABLE_DEPRECATION_WARNINGS
        frame->palette_has_changed = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        chunk_type = bytestream2_get_be32(&gb);
    } else if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        /* no palette supplied, use the default one */
        uint32_t *pal_out = (uint32_t *)frame->data[1];

        // TODO: add an AVOption to load custom palette files
        av_log(avctx, AV_LOG_WARNING,
               "Using default palette, colors might be off.\n");
        memcpy(pal_out, std_pal_table, sizeof(uint32_t) * 256);

#if FF_API_PALETTE_HAS_CHANGED
FF_DISABLE_DEPRECATION_WARNINGS
        frame->palette_has_changed = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    data_len = bytestream2_get_be32(&gb);
    bytestream2_skip(&gb, 8);

    // read the image data to the buffer
    bytes_left = bytestream2_get_bytes_left(&gb);

    if (chunk_type != IMAGE_DATA_CHUNK || data_len != bytes_left ||
        bytes_left / bytes_per_scanline < hdr.height) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image data.\n");
        return AVERROR_INVALIDDATA;
    }

    av_image_copy_plane(frame->data[0], frame->linesize[0],
                        avpkt->data + bytestream2_tell(&gb),
                        bytes_per_scanline,
                        bytes_per_scanline, hdr.height);

    *got_frame = 1;

    return avpkt->size;
}

const FFCodec ff_brender_pix_decoder = {
    .p.name         = "brender_pix",
    CODEC_LONG_NAME("BRender PIX image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_BRENDER_PIX,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_DECODE_CB(pix_decode_frame),
};
