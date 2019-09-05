/*
 * DirectDraw Surface image decoder
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
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
 * DDS decoder
 *
 * https://msdn.microsoft.com/en-us/library/bb943982%28v=vs.85%29.aspx
 */

#include <stdint.h>

#include "libavutil/libm.h"
#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "texturedsp.h"
#include "thread.h"

#define DDPF_FOURCC    (1 <<  2)
#define DDPF_PALETTE   (1 <<  5)
#define DDPF_NORMALMAP (1U << 31)

enum DDSPostProc {
    DDS_NONE = 0,
    DDS_ALPHA_EXP,
    DDS_NORMAL_MAP,
    DDS_RAW_YCOCG,
    DDS_SWAP_ALPHA,
    DDS_SWIZZLE_A2XY,
    DDS_SWIZZLE_RBXG,
    DDS_SWIZZLE_RGXB,
    DDS_SWIZZLE_RXBG,
    DDS_SWIZZLE_RXGB,
    DDS_SWIZZLE_XGBR,
    DDS_SWIZZLE_XRBG,
    DDS_SWIZZLE_XGXR,
};

enum DDSDXGIFormat {
    DXGI_FORMAT_R16G16B16A16_TYPELESS       =  9,
    DXGI_FORMAT_R16G16B16A16_FLOAT          = 10,
    DXGI_FORMAT_R16G16B16A16_UNORM          = 11,
    DXGI_FORMAT_R16G16B16A16_UINT           = 12,
    DXGI_FORMAT_R16G16B16A16_SNORM          = 13,
    DXGI_FORMAT_R16G16B16A16_SINT           = 14,

    DXGI_FORMAT_R8G8B8A8_TYPELESS           = 27,
    DXGI_FORMAT_R8G8B8A8_UNORM              = 28,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB         = 29,
    DXGI_FORMAT_R8G8B8A8_UINT               = 30,
    DXGI_FORMAT_R8G8B8A8_SNORM              = 31,
    DXGI_FORMAT_R8G8B8A8_SINT               = 32,

    DXGI_FORMAT_BC1_TYPELESS                = 70,
    DXGI_FORMAT_BC1_UNORM                   = 71,
    DXGI_FORMAT_BC1_UNORM_SRGB              = 72,
    DXGI_FORMAT_BC2_TYPELESS                = 73,
    DXGI_FORMAT_BC2_UNORM                   = 74,
    DXGI_FORMAT_BC2_UNORM_SRGB              = 75,
    DXGI_FORMAT_BC3_TYPELESS                = 76,
    DXGI_FORMAT_BC3_UNORM                   = 77,
    DXGI_FORMAT_BC3_UNORM_SRGB              = 78,
    DXGI_FORMAT_BC4_TYPELESS                = 79,
    DXGI_FORMAT_BC4_UNORM                   = 80,
    DXGI_FORMAT_BC4_SNORM                   = 81,
    DXGI_FORMAT_BC5_TYPELESS                = 82,
    DXGI_FORMAT_BC5_UNORM                   = 83,
    DXGI_FORMAT_BC5_SNORM                   = 84,
    DXGI_FORMAT_B5G6R5_UNORM                = 85,
    DXGI_FORMAT_B8G8R8A8_UNORM              = 87,
    DXGI_FORMAT_B8G8R8X8_UNORM              = 88,
    DXGI_FORMAT_B8G8R8A8_TYPELESS           = 90,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB         = 91,
    DXGI_FORMAT_B8G8R8X8_TYPELESS           = 92,
    DXGI_FORMAT_B8G8R8X8_UNORM_SRGB         = 93,
};

typedef struct DDSContext {
    TextureDSPContext texdsp;
    GetByteContext gbc;

    int compressed;
    int paletted;
    int bpp;
    enum DDSPostProc postproc;

    const uint8_t *tex_data; // Compressed texture
    int tex_ratio;           // Compression ratio
    int slice_count;         // Number of slices for threaded operations

    /* Pointer to the selected compress or decompress function. */
    int (*tex_funct)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
} DDSContext;

static int parse_pixel_format(AVCodecContext *avctx)
{
    DDSContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    uint32_t flags, fourcc, gimp_tag;
    enum DDSDXGIFormat dxgi;
    int size, bpp, r, g, b, a;
    int alpha_exponent, ycocg_classic, ycocg_scaled, normal_map, array;

    /* Alternative DDS implementations use reserved1 as custom header. */
    bytestream2_skip(gbc, 4 * 3);
    gimp_tag = bytestream2_get_le32(gbc);
    alpha_exponent = gimp_tag == MKTAG('A', 'E', 'X', 'P');
    ycocg_classic  = gimp_tag == MKTAG('Y', 'C', 'G', '1');
    ycocg_scaled   = gimp_tag == MKTAG('Y', 'C', 'G', '2');
    bytestream2_skip(gbc, 4 * 7);

    /* Now the real DDPF starts. */
    size = bytestream2_get_le32(gbc);
    if (size != 32) {
        av_log(avctx, AV_LOG_ERROR, "Invalid pixel format header %d.\n", size);
        return AVERROR_INVALIDDATA;
    }
    flags = bytestream2_get_le32(gbc);
    ctx->compressed = flags & DDPF_FOURCC;
    ctx->paletted   = flags & DDPF_PALETTE;
    normal_map      = flags & DDPF_NORMALMAP;
    fourcc = bytestream2_get_le32(gbc);

    if (ctx->compressed && ctx->paletted) {
        av_log(avctx, AV_LOG_WARNING,
               "Disabling invalid palette flag for compressed dds.\n");
        ctx->paletted = 0;
    }

    bpp = ctx->bpp = bytestream2_get_le32(gbc); // rgbbitcount
    r   = bytestream2_get_le32(gbc); // rbitmask
    g   = bytestream2_get_le32(gbc); // gbitmask
    b   = bytestream2_get_le32(gbc); // bbitmask
    a   = bytestream2_get_le32(gbc); // abitmask

    bytestream2_skip(gbc, 4); // caps
    bytestream2_skip(gbc, 4); // caps2
    bytestream2_skip(gbc, 4); // caps3
    bytestream2_skip(gbc, 4); // caps4
    bytestream2_skip(gbc, 4); // reserved2

    av_log(avctx, AV_LOG_VERBOSE, "fourcc %s bpp %d "
           "r 0x%x g 0x%x b 0x%x a 0x%x\n", av_fourcc2str(fourcc), bpp, r, g, b, a);
    if (gimp_tag)
        av_log(avctx, AV_LOG_VERBOSE, "and GIMP-DDS tag %s\n", av_fourcc2str(gimp_tag));

    if (ctx->compressed)
        avctx->pix_fmt = AV_PIX_FMT_RGBA;

    if (ctx->compressed) {
        switch (fourcc) {
        case MKTAG('D', 'X', 'T', '1'):
            ctx->tex_ratio = 8;
            ctx->tex_funct = ctx->texdsp.dxt1a_block;
            break;
        case MKTAG('D', 'X', 'T', '2'):
            ctx->tex_ratio = 16;
            ctx->tex_funct = ctx->texdsp.dxt2_block;
            break;
        case MKTAG('D', 'X', 'T', '3'):
            ctx->tex_ratio = 16;
            ctx->tex_funct = ctx->texdsp.dxt3_block;
            break;
        case MKTAG('D', 'X', 'T', '4'):
            ctx->tex_ratio = 16;
            ctx->tex_funct = ctx->texdsp.dxt4_block;
            break;
        case MKTAG('D', 'X', 'T', '5'):
            ctx->tex_ratio = 16;
            if (ycocg_scaled)
                ctx->tex_funct = ctx->texdsp.dxt5ys_block;
            else if (ycocg_classic)
                ctx->tex_funct = ctx->texdsp.dxt5y_block;
            else
                ctx->tex_funct = ctx->texdsp.dxt5_block;
            break;
        case MKTAG('R', 'X', 'G', 'B'):
            ctx->tex_ratio = 16;
            ctx->tex_funct = ctx->texdsp.dxt5_block;
            /* This format may be considered as a normal map,
             * but it is handled differently in a separate postproc. */
            ctx->postproc = DDS_SWIZZLE_RXGB;
            normal_map = 0;
            break;
        case MKTAG('A', 'T', 'I', '1'):
        case MKTAG('B', 'C', '4', 'U'):
            ctx->tex_ratio = 8;
            ctx->tex_funct = ctx->texdsp.rgtc1u_block;
            break;
        case MKTAG('B', 'C', '4', 'S'):
            ctx->tex_ratio = 8;
            ctx->tex_funct = ctx->texdsp.rgtc1s_block;
            break;
        case MKTAG('A', 'T', 'I', '2'):
            /* RGT2 variant with swapped R and G (3Dc)*/
            ctx->tex_ratio = 16;
            ctx->tex_funct = ctx->texdsp.dxn3dc_block;
            break;
        case MKTAG('B', 'C', '5', 'U'):
            ctx->tex_ratio = 16;
            ctx->tex_funct = ctx->texdsp.rgtc2u_block;
            break;
        case MKTAG('B', 'C', '5', 'S'):
            ctx->tex_ratio = 16;
            ctx->tex_funct = ctx->texdsp.rgtc2s_block;
            break;
        case MKTAG('U', 'Y', 'V', 'Y'):
            ctx->compressed = 0;
            avctx->pix_fmt = AV_PIX_FMT_UYVY422;
            break;
        case MKTAG('Y', 'U', 'Y', '2'):
            ctx->compressed = 0;
            avctx->pix_fmt = AV_PIX_FMT_YUYV422;
            break;
        case MKTAG('P', '8', ' ', ' '):
            /* ATI Palette8, same as normal palette */
            ctx->compressed = 0;
            ctx->paletted   = 1;
            avctx->pix_fmt  = AV_PIX_FMT_PAL8;
            break;
        case MKTAG('G', '1', ' ', ' '):
            ctx->compressed = 0;
            avctx->pix_fmt  = AV_PIX_FMT_MONOBLACK;
            break;
        case MKTAG('D', 'X', '1', '0'):
            /* DirectX 10 extra header */
            dxgi = bytestream2_get_le32(gbc);
            bytestream2_skip(gbc, 4); // resourceDimension
            bytestream2_skip(gbc, 4); // miscFlag
            array = bytestream2_get_le32(gbc);
            bytestream2_skip(gbc, 4); // miscFlag2

            if (array != 0)
                av_log(avctx, AV_LOG_VERBOSE,
                       "Found array of size %d (ignored).\n", array);

            /* Only BC[1-5] are actually compressed. */
            ctx->compressed = (dxgi >= 70) && (dxgi <= 84);

            av_log(avctx, AV_LOG_VERBOSE, "DXGI format %d.\n", dxgi);
            switch (dxgi) {
            /* RGB types. */
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_UNORM:
            case DXGI_FORMAT_R16G16B16A16_UINT:
            case DXGI_FORMAT_R16G16B16A16_SNORM:
            case DXGI_FORMAT_R16G16B16A16_SINT:
                avctx->pix_fmt = AV_PIX_FMT_BGRA64;
                break;
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                avctx->colorspace = AVCOL_SPC_RGB;
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UINT:
            case DXGI_FORMAT_R8G8B8A8_SNORM:
            case DXGI_FORMAT_R8G8B8A8_SINT:
                avctx->pix_fmt = AV_PIX_FMT_BGRA;
                break;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                avctx->colorspace = AVCOL_SPC_RGB;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                avctx->pix_fmt = AV_PIX_FMT_RGBA;
                break;
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                avctx->colorspace = AVCOL_SPC_RGB;
            case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            case DXGI_FORMAT_B8G8R8X8_UNORM:
                avctx->pix_fmt = AV_PIX_FMT_RGBA; // opaque
                break;
            case DXGI_FORMAT_B5G6R5_UNORM:
                avctx->pix_fmt = AV_PIX_FMT_RGB565LE;
                break;
            /* Texture types. */
            case DXGI_FORMAT_BC1_UNORM_SRGB:
                avctx->colorspace = AVCOL_SPC_RGB;
            case DXGI_FORMAT_BC1_TYPELESS:
            case DXGI_FORMAT_BC1_UNORM:
                ctx->tex_ratio = 8;
                ctx->tex_funct = ctx->texdsp.dxt1a_block;
                break;
            case DXGI_FORMAT_BC2_UNORM_SRGB:
                avctx->colorspace = AVCOL_SPC_RGB;
            case DXGI_FORMAT_BC2_TYPELESS:
            case DXGI_FORMAT_BC2_UNORM:
                ctx->tex_ratio = 16;
                ctx->tex_funct = ctx->texdsp.dxt3_block;
                break;
            case DXGI_FORMAT_BC3_UNORM_SRGB:
                avctx->colorspace = AVCOL_SPC_RGB;
            case DXGI_FORMAT_BC3_TYPELESS:
            case DXGI_FORMAT_BC3_UNORM:
                ctx->tex_ratio = 16;
                ctx->tex_funct = ctx->texdsp.dxt5_block;
                break;
            case DXGI_FORMAT_BC4_TYPELESS:
            case DXGI_FORMAT_BC4_UNORM:
                ctx->tex_ratio = 8;
                ctx->tex_funct = ctx->texdsp.rgtc1u_block;
                break;
            case DXGI_FORMAT_BC4_SNORM:
                ctx->tex_ratio = 8;
                ctx->tex_funct = ctx->texdsp.rgtc1s_block;
                break;
            case DXGI_FORMAT_BC5_TYPELESS:
            case DXGI_FORMAT_BC5_UNORM:
                ctx->tex_ratio = 16;
                ctx->tex_funct = ctx->texdsp.rgtc2u_block;
                break;
            case DXGI_FORMAT_BC5_SNORM:
                ctx->tex_ratio = 16;
                ctx->tex_funct = ctx->texdsp.rgtc2s_block;
                break;
            default:
                av_log(avctx, AV_LOG_ERROR,
                       "Unsupported DXGI format %d.\n", dxgi);
                return AVERROR_INVALIDDATA;
            }
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported %s fourcc.\n", av_fourcc2str(fourcc));
            return AVERROR_INVALIDDATA;
        }
    } else if (ctx->paletted) {
        if (bpp == 8) {
            avctx->pix_fmt = AV_PIX_FMT_PAL8;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported palette bpp %d.\n", bpp);
            return AVERROR_INVALIDDATA;
        }
    } else {
        /*  4 bpp */
        if (bpp == 4 && r == 0 && g == 0 && b == 0 && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_PAL8;
        /*  8 bpp */
        else if (bpp == 8 && r == 0xff && g == 0 && b == 0 && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        else if (bpp == 8 && r == 0 && g == 0 && b == 0 && a == 0xff)
            avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        /* 16 bpp */
        else if (bpp == 16 && r == 0xff && g == 0 && b == 0 && a == 0xff00)
            avctx->pix_fmt = AV_PIX_FMT_YA8;
        else if (bpp == 16 && r == 0xff00 && g == 0 && b == 0 && a == 0xff) {
            avctx->pix_fmt = AV_PIX_FMT_YA8;
            ctx->postproc = DDS_SWAP_ALPHA;
        }
        else if (bpp == 16 && r == 0xffff && g == 0 && b == 0 && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_GRAY16LE;
        else if (bpp == 16 && r == 0x7c00 && g == 0x3e0 && b == 0x1f && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_RGB555LE;
        else if (bpp == 16 && r == 0x7c00 && g == 0x3e0 && b == 0x1f && a == 0x8000)
            avctx->pix_fmt = AV_PIX_FMT_RGB555LE; // alpha ignored
        else if (bpp == 16 && r == 0xf800 && g == 0x7e0 && b == 0x1f && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_RGB565LE;
        /* 24 bpp */
        else if (bpp == 24 && r == 0xff0000 && g == 0xff00 && b == 0xff && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_BGR24;
        /* 32 bpp */
        else if (bpp == 32 && r == 0xff0000 && g == 0xff00 && b == 0xff && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_BGR0; // opaque
        else if (bpp == 32 && r == 0xff && g == 0xff00 && b == 0xff0000 && a == 0)
            avctx->pix_fmt = AV_PIX_FMT_RGB0; // opaque
        else if (bpp == 32 && r == 0xff0000 && g == 0xff00 && b == 0xff && a == 0xff000000)
            avctx->pix_fmt = AV_PIX_FMT_BGRA;
        else if (bpp == 32 && r == 0xff && g == 0xff00 && b == 0xff0000 && a == 0xff000000)
            avctx->pix_fmt = AV_PIX_FMT_RGBA;
        /* give up */
        else {
            av_log(avctx, AV_LOG_ERROR, "Unknown pixel format "
                   "[bpp %d r 0x%x g 0x%x b 0x%x a 0x%x].\n", bpp, r, g, b, a);
            return AVERROR_INVALIDDATA;
        }
    }

    /* Set any remaining post-proc that should happen before frame is ready. */
    if (alpha_exponent)
        ctx->postproc = DDS_ALPHA_EXP;
    else if (normal_map)
        ctx->postproc = DDS_NORMAL_MAP;
    else if (ycocg_classic && !ctx->compressed)
        ctx->postproc = DDS_RAW_YCOCG;

    /* ATI/NVidia variants sometimes add swizzling in bpp. */
    switch (bpp) {
    case MKTAG('A', '2', 'X', 'Y'):
        ctx->postproc = DDS_SWIZZLE_A2XY;
        break;
    case MKTAG('x', 'G', 'B', 'R'):
        ctx->postproc = DDS_SWIZZLE_XGBR;
        break;
    case MKTAG('x', 'R', 'B', 'G'):
        ctx->postproc = DDS_SWIZZLE_XRBG;
        break;
    case MKTAG('R', 'B', 'x', 'G'):
        ctx->postproc = DDS_SWIZZLE_RBXG;
        break;
    case MKTAG('R', 'G', 'x', 'B'):
        ctx->postproc = DDS_SWIZZLE_RGXB;
        break;
    case MKTAG('R', 'x', 'B', 'G'):
        ctx->postproc = DDS_SWIZZLE_RXBG;
        break;
    case MKTAG('x', 'G', 'x', 'R'):
        ctx->postproc = DDS_SWIZZLE_XGXR;
        break;
    case MKTAG('A', '2', 'D', '5'):
        ctx->postproc = DDS_NORMAL_MAP;
        break;
    }

    return 0;
}

static int decompress_texture_thread(AVCodecContext *avctx, void *arg,
                                     int slice, int thread_nb)
{
    DDSContext *ctx = avctx->priv_data;
    AVFrame *frame = arg;
    const uint8_t *d = ctx->tex_data;
    int w_block = avctx->coded_width / TEXTURE_BLOCK_W;
    int h_block = avctx->coded_height / TEXTURE_BLOCK_H;
    int x, y;
    int start_slice, end_slice;
    int base_blocks_per_slice = h_block / ctx->slice_count;
    int remainder_blocks = h_block % ctx->slice_count;

    /* When the frame height (in blocks) doesn't divide evenly between the
     * number of slices, spread the remaining blocks evenly between the first
     * operations */
    start_slice = slice * base_blocks_per_slice;
    /* Add any extra blocks (one per slice) that have been added before this slice */
    start_slice += FFMIN(slice, remainder_blocks);

    end_slice = start_slice + base_blocks_per_slice;
    /* Add an extra block if there are still remainder blocks to be accounted for */
    if (slice < remainder_blocks)
        end_slice++;

    for (y = start_slice; y < end_slice; y++) {
        uint8_t *p = frame->data[0] + y * frame->linesize[0] * TEXTURE_BLOCK_H;
        int off  = y * w_block;
        for (x = 0; x < w_block; x++) {
            ctx->tex_funct(p + x * 16, frame->linesize[0],
                           d + (off + x) * ctx->tex_ratio);
        }
    }

    return 0;
}

static void do_swizzle(AVFrame *frame, int x, int y)
{
    int i;
    for (i = 0; i < frame->linesize[0] * frame->height; i += 4) {
        uint8_t *src = frame->data[0] + i;
        FFSWAP(uint8_t, src[x], src[y]);
    }
}

static void run_postproc(AVCodecContext *avctx, AVFrame *frame)
{
    DDSContext *ctx = avctx->priv_data;
    int i, x_off;

    switch (ctx->postproc) {
    case DDS_ALPHA_EXP:
        /* Alpha-exponential mode divides each channel by the maximum
         * R, G or B value, and stores the multiplying factor in the
         * alpha channel. */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing alpha exponent.\n");

        for (i = 0; i < frame->linesize[0] * frame->height; i += 4) {
            uint8_t *src = frame->data[0] + i;
            int r = src[0];
            int g = src[1];
            int b = src[2];
            int a = src[3];

            src[0] = r * a / 255;
            src[1] = g * a / 255;
            src[2] = b * a / 255;
            src[3] = 255;
        }
        break;
    case DDS_NORMAL_MAP:
        /* Normal maps work in the XYZ color space and they encode
         * X in R or in A, depending on the texture type, Y in G and
         * derive Z with a square root of the distance.
         *
         * http://www.realtimecollisiondetection.net/blog/?p=28 */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing normal map.\n");

        x_off = ctx->tex_ratio == 8 ? 0 : 3;
        for (i = 0; i < frame->linesize[0] * frame->height; i += 4) {
            uint8_t *src = frame->data[0] + i;
            int x = src[x_off];
            int y = src[1];
            int z = 127;

            int d = (255 * 255 - x * x - y * y) / 2;
            if (d > 0)
                z = lrint(sqrtf(d));

            src[0] = x;
            src[1] = y;
            src[2] = z;
            src[3] = 255;
        }
        break;
    case DDS_RAW_YCOCG:
        /* Data is Y-Co-Cg-A and not RGBA, but they are represented
         * with the same masks in the DDPF header. */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing raw YCoCg.\n");

        for (i = 0; i < frame->linesize[0] * frame->height; i += 4) {
            uint8_t *src = frame->data[0] + i;
            int a  = src[0];
            int cg = src[1] - 128;
            int co = src[2] - 128;
            int y  = src[3];

            src[0] = av_clip_uint8(y + co - cg);
            src[1] = av_clip_uint8(y + cg);
            src[2] = av_clip_uint8(y - co - cg);
            src[3] = a;
        }
        break;
    case DDS_SWAP_ALPHA:
        /* Alpha and Luma are stored swapped. */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing swapped Luma/Alpha.\n");

        for (i = 0; i < frame->linesize[0] * frame->height; i += 2) {
            uint8_t *src = frame->data[0] + i;
            FFSWAP(uint8_t, src[0], src[1]);
        }
        break;
    case DDS_SWIZZLE_A2XY:
        /* Swap R and G, often used to restore a standard RGTC2. */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing A2XY swizzle.\n");
        do_swizzle(frame, 0, 1);
        break;
    case DDS_SWIZZLE_RBXG:
        /* Swap G and A, then B and new A (G). */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing RBXG swizzle.\n");
        do_swizzle(frame, 1, 3);
        do_swizzle(frame, 2, 3);
        break;
    case DDS_SWIZZLE_RGXB:
        /* Swap B and A. */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing RGXB swizzle.\n");
        do_swizzle(frame, 2, 3);
        break;
    case DDS_SWIZZLE_RXBG:
        /* Swap G and A. */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing RXBG swizzle.\n");
        do_swizzle(frame, 1, 3);
        break;
    case DDS_SWIZZLE_RXGB:
        /* Swap R and A (misleading name). */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing RXGB swizzle.\n");
        do_swizzle(frame, 0, 3);
        break;
    case DDS_SWIZZLE_XGBR:
        /* Swap B and A, then R and new A (B). */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing XGBR swizzle.\n");
        do_swizzle(frame, 2, 3);
        do_swizzle(frame, 0, 3);
        break;
    case DDS_SWIZZLE_XGXR:
        /* Swap G and A, then R and new A (G), then new R (G) and new G (A).
         * This variant does not store any B component. */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing XGXR swizzle.\n");
        do_swizzle(frame, 1, 3);
        do_swizzle(frame, 0, 3);
        do_swizzle(frame, 0, 1);
        break;
    case DDS_SWIZZLE_XRBG:
        /* Swap G and A, then R and new A (G). */
        av_log(avctx, AV_LOG_DEBUG, "Post-processing XRBG swizzle.\n");
        do_swizzle(frame, 1, 3);
        do_swizzle(frame, 0, 3);
        break;
    }
}

static int dds_decode(AVCodecContext *avctx, void *data,
                      int *got_frame, AVPacket *avpkt)
{
    DDSContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    AVFrame *frame = data;
    int mipmap;
    int ret;
    int width, height;

    ff_texturedsp_init(&ctx->texdsp);
    bytestream2_init(gbc, avpkt->data, avpkt->size);

    if (bytestream2_get_bytes_left(gbc) < 128) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too small (%d).\n",
               bytestream2_get_bytes_left(gbc));
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_le32(gbc) != MKTAG('D', 'D', 'S', ' ') ||
        bytestream2_get_le32(gbc) != 124) { // header size
        av_log(avctx, AV_LOG_ERROR, "Invalid DDS header.\n");
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skip(gbc, 4); // flags

    height = bytestream2_get_le32(gbc);
    width  = bytestream2_get_le32(gbc);
    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    /* Since codec is based on 4x4 blocks, size is aligned to 4. */
    avctx->coded_width  = FFALIGN(avctx->width,  TEXTURE_BLOCK_W);
    avctx->coded_height = FFALIGN(avctx->height, TEXTURE_BLOCK_H);

    bytestream2_skip(gbc, 4); // pitch
    bytestream2_skip(gbc, 4); // depth
    mipmap = bytestream2_get_le32(gbc);
    if (mipmap != 0)
        av_log(avctx, AV_LOG_VERBOSE, "Found %d mipmaps (ignored).\n", mipmap);

    /* Extract pixel format information, considering additional elements
     * in reserved1 and reserved2. */
    ret = parse_pixel_format(avctx);
    if (ret < 0)
        return ret;

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    if (ctx->compressed) {
        int size = (avctx->coded_height / TEXTURE_BLOCK_H) *
                   (avctx->coded_width / TEXTURE_BLOCK_W) * ctx->tex_ratio;
        ctx->slice_count = av_clip(avctx->thread_count, 1,
                                   avctx->coded_height / TEXTURE_BLOCK_H);

        if (bytestream2_get_bytes_left(gbc) < size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Compressed Buffer is too small (%d < %d).\n",
                   bytestream2_get_bytes_left(gbc), size);
            return AVERROR_INVALIDDATA;
        }

        /* Use the decompress function on the texture, one block per thread. */
        ctx->tex_data = gbc->buffer;
        avctx->execute2(avctx, decompress_texture_thread, frame, NULL, ctx->slice_count);
    } else if (!ctx->paletted && ctx->bpp == 4 && avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        uint8_t *dst = frame->data[0];
        int x, y, i;

        /* Use the first 64 bytes as palette, then copy the rest. */
        bytestream2_get_buffer(gbc, frame->data[1], 16 * 4);
        for (i = 0; i < 16; i++) {
            AV_WN32(frame->data[1] + i*4,
                    (frame->data[1][2+i*4]<<0)+
                    (frame->data[1][1+i*4]<<8)+
                    (frame->data[1][0+i*4]<<16)+
                    ((unsigned)frame->data[1][3+i*4]<<24)
            );
        }
        frame->palette_has_changed = 1;

        if (bytestream2_get_bytes_left(gbc) < frame->height * frame->width / 2) {
            av_log(avctx, AV_LOG_ERROR, "Buffer is too small (%d < %d).\n",
                   bytestream2_get_bytes_left(gbc), frame->height * frame->width / 2);
            return AVERROR_INVALIDDATA;
        }

        for (y = 0; y < frame->height; y++) {
            for (x = 0; x < frame->width; x += 2) {
                uint8_t val = bytestream2_get_byte(gbc);
                dst[x    ] = val & 0xF;
                dst[x + 1] = val >> 4;
            }
            dst += frame->linesize[0];
        }
    } else {
        int linesize = av_image_get_linesize(avctx->pix_fmt, frame->width, 0);

        if (ctx->paletted) {
            int i;
            /* Use the first 1024 bytes as palette, then copy the rest. */
            bytestream2_get_buffer(gbc, frame->data[1], 256 * 4);
            for (i = 0; i < 256; i++)
                AV_WN32(frame->data[1] + i*4,
                        (frame->data[1][2+i*4]<<0)+
                        (frame->data[1][1+i*4]<<8)+
                        (frame->data[1][0+i*4]<<16)+
                        ((unsigned)frame->data[1][3+i*4]<<24)
                );

            frame->palette_has_changed = 1;
        }

        if (bytestream2_get_bytes_left(gbc) < frame->height * linesize) {
            av_log(avctx, AV_LOG_ERROR, "Buffer is too small (%d < %d).\n",
                   bytestream2_get_bytes_left(gbc), frame->height * linesize);
            return AVERROR_INVALIDDATA;
        }

        av_image_copy_plane(frame->data[0], frame->linesize[0],
                            gbc->buffer, linesize,
                            linesize, frame->height);
    }

    /* Run any post processing here if needed. */
    if (ctx->postproc != DDS_NONE)
        run_postproc(avctx, frame);

    /* Frame is ready to be output. */
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_dds_decoder = {
    .name           = "dds",
    .long_name      = NULL_IF_CONFIG_SMALL("DirectDraw Surface image decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DDS,
    .decode         = dds_decode,
    .priv_data_size = sizeof(DDSContext),
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE
};
