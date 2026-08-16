/*
 * astc-encoder wrapper for FFmpeg (ASTC image encoder)
 *
 * Copyright (C) 2026 Jun Zhao
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
 * ASTC encoder using ARM's astc-encoder library (astcenc C API).
 *
 * Output is the raw ASTC bitstream (16 bytes per block). The container
 * (.astc / .ktx) is added by the corresponding FFmpeg muxer.
 */

#include <astcenc/astcenc.h>

#include <limits.h>

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "profiles.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

/* .astc container magic (little-endian 0x5CA1AB13). */
#define ASTC_MAGIC        0x5CA1AB13
#define ASTC_HEADER_SIZE  16
#define ASTC_BLOCK_BYTES  16

typedef struct LibAstcEncContext {
    AVClass *class;

    /* astc-encoder state */
    struct astcenc_config cfg;
    struct astcenc_context *ctx;

    /* options */
    char *block_size_str;
    int block_x;
    int block_y;
    int block_z;
    int profile;        /* astcenc_profile */
    float quality;      /* 0..100 */
    int perceptual;
    int alpha_weight;
} LibAstcEncContext;

static av_cold int libastcenc_init(AVCodecContext *avctx)
{
    LibAstcEncContext *s = avctx->priv_data;
    enum astcenc_profile prf;
    unsigned int flags = 0;
    enum astcenc_error err;

    /* An explicitly set private profile option overrides the public
     * AVCodecContext.profile field. AV_PROFILE_ASTC_LINEAR_ANY describes how a
     * container should interpret a stream and never selects an encoding
     * profile, so it is rejected here. */
    if (s->profile >= 0) {
        prf = (enum astcenc_profile)s->profile;
    } else {
        switch (avctx->profile) {
        case AV_PROFILE_UNKNOWN:
        case AV_PROFILE_ASTC_LDR_SRGB:
            prf = ASTCENC_PRF_LDR_SRGB;
            break;
        case AV_PROFILE_ASTC_LDR:
            prf = ASTCENC_PRF_LDR;
            break;
        case AV_PROFILE_ASTC_HDR_RGB_LDR_A:
            prf = ASTCENC_PRF_HDR_RGB_LDR_A;
            break;
        case AV_PROFILE_ASTC_HDR:
            prf = ASTCENC_PRF_HDR;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                   "Unsupported ASTC encoding profile %d.\n", avctx->profile);
            return AVERROR(EINVAL);
        }
    }

    /* Resolve profile vs input pixel format. Float/half input must be encoded
     * with an HDR profile (auto-promoted here); 8-bit input cannot drive an HDR
     * profile (it would be degenerate -> near-black). This runs at init (not in
     * encode) because pix_fmt is known at open time; the resolved profile is
     * propagated via avctx->profile (-> codecpar->profile) so muxers such as
     * the KTX writer can reject HDR without needing to inspect extradata. */
    int is_float = (avctx->pix_fmt == AV_PIX_FMT_RGBAF16 ||
                    avctx->pix_fmt == AV_PIX_FMT_RGBAF32 ||
                    avctx->pix_fmt == AV_PIX_FMT_GBRAPF32);
    if (is_float) {
        if (prf == ASTCENC_PRF_LDR || prf == ASTCENC_PRF_LDR_SRGB) {
            prf = ASTCENC_PRF_HDR_RGB_LDR_A;
            av_log(avctx, AV_LOG_INFO,
                   "Float/half input: auto-promoted profile to HDR_RGB_LDR_A.\n");
        }
    } else if (prf == ASTCENC_PRF_HDR || prf == ASTCENC_PRF_HDR_RGB_LDR_A) {
        av_log(avctx, AV_LOG_ERROR,
               "HDR profile selected but input is 8-bit; use float/half "
               "input (e.g. -pix_fmt rgbaf16) for HDR.\n");
        return AVERROR(EINVAL);
    }
    s->profile = (int)prf;
    /* Publish the profile the container records, as one of the public ASTC
     * profile values rather than as the astcenc enum, whose ordering is not
     * part of the FFmpeg API. */
    switch (prf) {
    case ASTCENC_PRF_LDR_SRGB:
        avctx->profile = AV_PROFILE_ASTC_LDR_SRGB;
        break;
    case ASTCENC_PRF_LDR:
        avctx->profile = AV_PROFILE_ASTC_LDR;
        break;
    case ASTCENC_PRF_HDR_RGB_LDR_A:
        avctx->profile = AV_PROFILE_ASTC_HDR_RGB_LDR_A;
        break;
    case ASTCENC_PRF_HDR:
        avctx->profile = AV_PROFILE_ASTC_HDR;
        break;
    default:
        avctx->profile = AV_PROFILE_UNKNOWN;
        break;
    }

    if (avctx->width <= 0 || avctx->height <= 0 ||
        avctx->width > 0xFFFFFF || avctx->height > 0xFFFFFF) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image dimensions %dx%d.\n",
               avctx->width, avctx->height);
        return AVERROR(EINVAL);
    }

    /* Accept exactly "WxH" or "WxHxD". Scanning stops at the third literal 'x'
     * when it is absent, so trailing garbage such as "8x8junk" has to be
     * caught by checking that the whole string was consumed. */
    int consumed = 0;

    s->block_z = 1;
    if (sscanf(s->block_size_str, "%dx%dx%d%n", &s->block_x, &s->block_y,
               &s->block_z, &consumed) != 3) {
        consumed = 0;
        s->block_z = 1;
        sscanf(s->block_size_str, "%dx%d%n", &s->block_x, &s->block_y,
               &consumed);
    }
    if (!consumed || s->block_size_str[consumed]) {
        av_log(avctx, AV_LOG_ERROR, "Invalid block_size '%s' "
               "(expected e.g. 8x8 or 6x6x6).\n", s->block_size_str);
        return AVERROR(EINVAL);
    }

    if (s->perceptual)
        flags |= ASTCENC_FLG_USE_PERCEPTUAL;
    if (s->alpha_weight)
        flags |= ASTCENC_FLG_USE_ALPHA_WEIGHT;

    err = astcenc_config_init(prf, s->block_x, s->block_y, s->block_z,
                              s->quality, flags, &s->cfg);
    if (err != ASTCENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "astcenc_config_init failed: %s\n",
               astcenc_get_error_string(err));
        return AVERROR_UNKNOWN;
    }

    /* astcenc normalizes the requested footprint, e.g. a zero block depth is
     * clamped to one, so publish the validated footprint instead of the parsed
     * one. Both the .astc header below and the block accounting in encode()
     * must agree with what the library actually compresses with. */
    s->block_x = s->cfg.block_x;
    s->block_y = s->cfg.block_y;
    s->block_z = s->cfg.block_z;

    err = astcenc_context_alloc(&s->cfg, 1, &s->ctx, NULL);
    if (err != ASTCENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "astcenc_context_alloc failed: %s\n",
               astcenc_get_error_string(err));
        return AVERROR(ENOMEM);
    }

    /* Publish a 16-byte .astc-style header as extradata so the .astc/.ktx
     * muxers can write the container header and the decoder can recover the
     * block size and dimensions. The buffer is padded so consumers that read
     * it as a bitstream do not over-read. */
    avctx->extradata = av_mallocz(ASTC_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    avctx->extradata_size = ASTC_HEADER_SIZE;
    {
        uint8_t *h = avctx->extradata;
        /* .astc header layout (all little-endian):
         *   [0-3]   magic
         *   [4-6]   block dimensions block_x, block_y, block_z (block_z > 1
         *           selects a 3D block footprint, e.g. 4x4x4)
         *   [7-9]   image width  (dim_x, texels)
         *   [10-12] image height (dim_y, texels)
         *   [13-15] image depth  (dim_z, texel slices; 1 for a 2D image)
         * block_z (the block footprint) is independent of dim_z (the image
         * slice count), so a 3D block can still describe a 2D image. */
        AV_WL32(h, ASTC_MAGIC);
        h[4] = (uint8_t)s->block_x;
        h[5] = (uint8_t)s->block_y;
        h[6] = (uint8_t)s->block_z;
        AV_WL24(h + 7,  avctx->width);
        AV_WL24(h + 10, avctx->height);
        AV_WL24(h + 13, 1); /* image depth is 1: 2D image (3D blocks stay 2D) */
    }

    return 0;
}

static int libastcenc_encode(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    LibAstcEncContext *s = avctx->priv_data;
    const int w = frame->width;
    const int h = frame->height;
    const int bx = s->block_x;
    const int by = s->block_y;
    size_t blocks_x, blocks_y, block_count, out_size;

    if (w != avctx->width || h != avctx->height) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame size %dx%d does not match configured %dx%d.\n",
               w, h, avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    blocks_x = (w - 1) / bx + 1;
    blocks_y = (h - 1) / by + 1;
    if (av_size_mult(blocks_x, blocks_y, &block_count) < 0 ||
        av_size_mult(block_count, ASTC_BLOCK_BYTES, &out_size) < 0 ||
        out_size > INT_MAX)
        return AVERROR(EINVAL);

    struct astcenc_swizzle swz = {
        ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A
    };

    uint8_t *buf = NULL;
    uint8_t *packed;   /* buffer handed to astcenc, buf or the frame itself */
    void *data_ptrs[1];
    struct astcenc_image img;
    enum astcenc_error err;
    int ret;

    /* Map input pixel format -> astcenc data type and component layout.
     * astcenc always consumes a tightly packed, RGBA-ordered buffer. */
    int elem;        /* bytes per component */
    int planar;      /* GBRAPF32 stores G,B,R,A on separate planes */
    int const_alpha; /* RGB24: synthesize opaque alpha */
    enum astcenc_type type;

    switch (frame->format) {
    case AV_PIX_FMT_RGBA:       type = ASTCENC_TYPE_U8;  elem = 1; planar = 0; const_alpha = 0; break;
    case AV_PIX_FMT_RGB24:      type = ASTCENC_TYPE_U8;  elem = 1; planar = 0; const_alpha = 1; break;
    case AV_PIX_FMT_RGBAF16:  type = ASTCENC_TYPE_F16; elem = 2; planar = 0; const_alpha = 0; break;
    case AV_PIX_FMT_RGBAF32:  type = ASTCENC_TYPE_F32; elem = 4; planar = 0; const_alpha = 0; break;
    case AV_PIX_FMT_GBRAPF32: type = ASTCENC_TYPE_F32; elem = 4; planar = 1; const_alpha = 0; break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported input pixel format %s.\n",
               av_get_pix_fmt_name(frame->format));
        return AVERROR(EINVAL);
    }

    size_t row_bytes, buf_size;
    if (av_size_mult(w, 4 * elem, &row_bytes) < 0 ||
        av_size_mult(h, row_bytes, &buf_size) < 0)
        return AVERROR(EINVAL);

    /* Packed input whose lines already have the size astcenc expects needs no
     * repacking, so it can be consumed in place. */
    if (!planar && !const_alpha && frame->linesize[0] == (int)row_bytes) {
        packed = frame->data[0];
    } else {
        buf = av_malloc(buf_size);
        if (!buf)
            return AVERROR(ENOMEM);
        packed = buf;

        if (planar) {
            /* GBRAPF32 -> packed RGBA float. */
            for (int y = 0; y < h; y++) {
                const float *g = (const float *)(frame->data[0] + (size_t)y * frame->linesize[0]);
                const float *b = (const float *)(frame->data[1] + (size_t)y * frame->linesize[1]);
                const float *r = (const float *)(frame->data[2] + (size_t)y * frame->linesize[2]);
                const float *a = (const float *)(frame->data[3] + (size_t)y * frame->linesize[3]);
                float *dst = (float *)(buf + (size_t)y * row_bytes);
                for (int x = 0; x < w; x++) {
                    dst[4*x+0] = r[x]; dst[4*x+1] = g[x];
                    dst[4*x+2] = b[x]; dst[4*x+3] = a[x];
                }
            }
        } else if (const_alpha) {
            for (int y = 0; y < h; y++) {
                const uint8_t *src = frame->data[0] + (size_t)y * frame->linesize[0];
                uint8_t *dst = buf + (size_t)y * row_bytes;
                for (int x = 0; x < w; x++) {
                    dst[4*x+0] = src[3*x+0]; dst[4*x+1] = src[3*x+1];
                    dst[4*x+2] = src[3*x+2]; dst[4*x+3] = 255;
                }
            }
        } else {
            /* packed RGBA / RGBA16 / RGBA32: copy row-by-row to drop line padding. */
            for (int y = 0; y < h; y++)
                memcpy(buf + (size_t)y * row_bytes,
                       frame->data[0] + (size_t)y * frame->linesize[0], row_bytes);
        }
    }

    img.dim_x = w;
    img.dim_y = h;
    img.dim_z = 1;
    img.data_type = type;
    data_ptrs[0] = packed;
    img.data = data_ptrs;

    ret = ff_get_encode_buffer(avctx, pkt, out_size, 0);
    if (ret < 0)
        goto end;

    err = astcenc_compress_image(s->ctx, &img, &swz, pkt->data, pkt->size, 0);
    if (err != ASTCENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "astcenc_compress_image failed: %s\n",
               astcenc_get_error_string(err));
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    *got_packet = 1;
    ret = 0;

end:
    av_freep(&buf);
    return ret;
}

static av_cold int libastcenc_close(AVCodecContext *avctx)
{
    LibAstcEncContext *s = avctx->priv_data;
    if (s->ctx) {
        astcenc_context_free(s->ctx);
        s->ctx = NULL;
    }
    return 0;
}

#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
#define OFFSET(x) offsetof(LibAstcEncContext, x)

static const AVOption options[] = {
    { "block_size", "ASTC block size WxH[xD] (e.g. 4x4, 8x8, 6x6x6)", OFFSET(block_size_str),
      AV_OPT_TYPE_STRING, { .str = "8x8" }, 0, 0, VE },
    { "quality", "Compression quality 0..100 (0=fastest, 100=exhaustive)",
      OFFSET(quality), AV_OPT_TYPE_FLOAT, { .dbl = 60.0 }, 0.0, 100.0, VE },
    { "profile", "Color profile (overrides AVCodecContext.profile)",
      OFFSET(profile), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 3, VE, .unit = "profile" },
    { "ldr",       "Linear LDR",        0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_LDR },          0, 0, VE, .unit = "profile" },
    { "ldr-srgb",  "sRGB LDR",          0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_LDR_SRGB },     0, 0, VE, .unit = "profile" },
    { "hdr-ldr-a", "HDR RGB, LDR alpha", 0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_HDR_RGB_LDR_A }, 0, 0, VE, .unit = "profile" },
    { "hdr",       "HDR",               0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_HDR },          0, 0, VE, .unit = "profile" },
    { "perceptual", "Use perceptual (PSNR-weighted) error metric", OFFSET(perceptual),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "alpha_weight", "Enable alpha weighting", OFFSET(alpha_weight),
      AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE },
    { NULL },
};

static const AVClass libastcenc_class = {
    .class_name = "libastcenc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libastcenc_encoder = {
    .p.name         = "libastcenc",
    CODEC_LONG_NAME("ASTC (Adaptive Scalable Texture Compression) image using astc-encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ASTC,
    .p.capabilities = AV_CODEC_CAP_DR1,
    CODEC_PIXFMTS(AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB24,
                  AV_PIX_FMT_RGBAF16, AV_PIX_FMT_RGBAF32, AV_PIX_FMT_GBRAPF32),
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_astc_profiles),
    .p.priv_class   = &libastcenc_class,
    .p.wrapper_name = "libastcenc",
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(LibAstcEncContext),
    .init           = libastcenc_init,
    FF_CODEC_ENCODE_CB(libastcenc_encode),
    .close          = libastcenc_close,
};
