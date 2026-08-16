/*
 * astc-encoder wrapper decoder for FFmpeg (ASTC image decoder)
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
 * ASTC decoder using ARM's astc-encoder library (astcenc C API).
 *
 * Consumes the raw ASTC bitstream; block size and dimensions are recovered
 * from the 16-byte .astc-style extradata published by the demuxer.
 */

#include <astcenc/astcenc.h>

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "profiles.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

/* .astc container magic (little-endian 0x5CA1AB13). */
#define ASTC_MAGIC        0x5CA1AB13
#define ASTC_HEADER_SIZE  16
#define ASTC_BLOCK_BYTES  16

typedef struct LibAstcDecContext {
    AVClass *class;
    struct astcenc_config cfg;
    struct astcenc_context *ctx;
    int block_x;
    int block_y;
    int block_z;
    int is_hdr;
    int dec_profile;  /* astcenc_profile, set via private option, -1 = auto */
} LibAstcDecContext;

static av_cold int astcdec_configure(AVCodecContext *avctx,
                                     enum astcenc_profile prf)
{
    LibAstcDecContext *s = avctx->priv_data;
    enum astcenc_error err;

    if (s->ctx) {
        astcenc_context_free(s->ctx);
        s->ctx = NULL;
    }

    err = astcenc_config_init(prf, s->block_x, s->block_y, s->block_z,
                              0, ASTCENC_FLG_DECOMPRESS_ONLY, &s->cfg);
    if (err != ASTCENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "astcenc_config_init failed: %s\n",
               astcenc_get_error_string(err));
        return AVERROR_UNKNOWN;
    }

    err = astcenc_context_alloc(&s->cfg, 1, &s->ctx, NULL);
    if (err != ASTCENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "astcenc_context_alloc failed: %s\n",
               astcenc_get_error_string(err));
        return AVERROR(ENOMEM);
    }

    s->is_hdr = (prf == ASTCENC_PRF_HDR || prf == ASTCENC_PRF_HDR_RGB_LDR_A);
    /* Leave avctx->profile untouched: it is a property of the stream, set by
     * the container, while the profile resolved here is an internal decision.
     * avformat_find_stream_info() copies this context back into the stream
     * parameters, so publishing the resolved profile would, for example, turn
     * a linear KTX into a stream the KTX muxer refuses to copy. */
    avctx->pix_fmt = s->is_hdr ? AV_PIX_FMT_RGBAF16 : AV_PIX_FMT_RGBA;
    return 0;
}

static av_cold int libastcdec_init(AVCodecContext *avctx)
{
    LibAstcDecContext *s = avctx->priv_data;
    const uint8_t *ed = avctx->extradata;

    if (!ed || avctx->extradata_size < ASTC_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR,
               "astc decoder requires %d-byte .astc-style extradata.\n",
               ASTC_HEADER_SIZE);
        return AVERROR_INVALIDDATA;
    }

    /* .astc header layout (all little-endian):
     *   [0-3]   magic
     *   [4-6]   block dimensions block_x, block_y, block_z (block_z > 1
     *           selects a 3D block footprint, e.g. 4x4x4)
     *   [7-9]   image width  (dim_x, texels)
     *   [10-12] image height (dim_y, texels)
     *   [13-15] image depth  (dim_z, texel slices; 1 for a 2D image)
     * block_z (the block footprint) is independent of dim_z (the image
     * slice count), so a 3D block can still describe a 2D image. */
    if (AV_RL32(ed) != ASTC_MAGIC || AV_RL24(ed + 13) != 1) {
        av_log(avctx, AV_LOG_ERROR, "Invalid ASTC header.\n");
        return AVERROR_INVALIDDATA;
    }

    s->block_x = ed[4];
    s->block_y = ed[5];
    s->block_z = ed[6];
    avctx->width  = AV_RL24(ed + 7);
    avctx->height = AV_RL24(ed + 10);
    /* Zero footprints would divide by zero in the block accounting below; the
     * .astc demuxer rejects them, but the decoder must not trust its input. */
    if (!s->block_x || !s->block_y || !s->block_z ||
        avctx->width <= 0 || avctx->height <= 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid ASTC header: block %dx%dx%d, image %dx%d.\n",
               s->block_x, s->block_y, s->block_z,
               avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    /* The .astc header does not record the intended decoding profile.
     * Individual blocks identify their endpoint encodings, but that does not
     * establish the sRGB versus linear interpretation. Use the container
     * metadata or -dec_profile; without either, a raw .astc input defaults to
     * ldr-srgb. A stream that records no profile at all keeps the
     * AV_PROFILE_UNKNOWN default and lands in the fallback branch below. */
    enum astcenc_profile prf = (enum astcenc_profile)s->dec_profile;
    if (s->dec_profile < 0) {
        switch (avctx->profile) {
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
        case AV_PROFILE_ASTC_LINEAR_ANY:
            /* A linear texture may hold HDR endpoint encodings, so sample it
             * with HDR precision; LDR endpoint blocks decode correctly through
             * the same profile instead of coming back as the magenta error
             * colour. */
            prf = ASTCENC_PRF_HDR_RGB_LDR_A;
            break;
        default:
            /* No container information (a raw .astc stream): the documented
             * LDR sRGB fallback. HDR needs an explicit -dec_profile. */
            prf = ASTCENC_PRF_LDR_SRGB;
            break;
        }
    }

    return astcdec_configure(avctx, prf);
}

static int libastcdec_decode(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame, AVPacket *avpkt)
{
    LibAstcDecContext *s = avctx->priv_data;
    const int w = avctx->width;
    const int h = avctx->height;
    const int elem = s->is_hdr ? 2 : 1;
    size_t row_bytes, buf_size, block_count, expected;

    if (av_size_mult(w, 4 * elem, &row_bytes) < 0 ||
        av_size_mult(h, row_bytes, &buf_size) < 0 ||
        av_size_mult((w - 1) / s->block_x + 1,
                     (h - 1) / s->block_y + 1, &block_count) < 0 ||
        av_size_mult(block_count, ASTC_BLOCK_BYTES, &expected) < 0)
        return AVERROR_INVALIDDATA;
    /* dim_z is always 1 (2D input), so blocks_z is always 1. */

    struct astcenc_swizzle swz = {
        ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A
    };

    uint8_t *out_buf = NULL;
    void *data_ptrs[1];
    struct astcenc_image img;
    enum astcenc_error err;
    int ret;

    /* A single ASTC image is exactly the blocks its geometry needs; accepting
     * a larger packet would silently ignore the trailing data below. */
    if ((size_t)avpkt->size != expected) {
        av_log(avctx, AV_LOG_ERROR,
               "ASTC packet size %d does not match the %zu bytes required for "
               "a %dx%d image with %dx%d blocks.\n",
               avpkt->size, expected, w, h, s->block_x, s->block_y);
        return AVERROR_INVALIDDATA;
    }

    frame->width  = w;
    frame->height = h;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    /* astcenc writes a tightly packed image. If the frame has no line padding
     * we can decode straight into it; otherwise use a temporary buffer and
     * copy row by row. */
    if (frame->linesize[0] == row_bytes) {
        data_ptrs[0] = frame->data[0];
    } else {
        out_buf = av_malloc(buf_size);
        if (!out_buf)
            return AVERROR(ENOMEM);
        data_ptrs[0] = out_buf;
    }

    img.dim_x = w;
    img.dim_y = h;
    img.dim_z = 1;
    img.data_type = s->is_hdr ? ASTCENC_TYPE_F16 : ASTCENC_TYPE_U8;
    img.data = data_ptrs;

    err = astcenc_decompress_image(s->ctx, avpkt->data, avpkt->size,
                                   &img, &swz, 0);
    if (err != ASTCENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "astcenc_decompress_image failed: %s\n",
               astcenc_get_error_string(err));
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    if (out_buf) {
        for (int y = 0; y < h; y++)
            memcpy(frame->data[0] + (size_t)y * frame->linesize[0],
                   out_buf + (size_t)y * row_bytes, row_bytes);
    }

    *got_frame = 1;
    ret = 0;

end:
    av_freep(&out_buf);
    return ret;
}

static av_cold int libastcdec_close(AVCodecContext *avctx)
{
    LibAstcDecContext *s = avctx->priv_data;
    if (s->ctx) {
        astcenc_context_free(s->ctx);
        s->ctx = NULL;
    }
    return 0;
}

#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
#define OFFSET(x) offsetof(LibAstcDecContext, x)

static const AVOption dec_options[] = {
    { "dec_profile", "Decoder color profile (must match encoder)", OFFSET(dec_profile),
      AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 3, VD, .unit = "dec_profile" },
    { "ldr",       "Linear LDR",        0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_LDR },          0, 0, VD, .unit = "dec_profile" },
    { "ldr-srgb",  "sRGB LDR",          0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_LDR_SRGB },     0, 0, VD, .unit = "dec_profile" },
    { "hdr-ldr-a", "HDR RGB, LDR alpha", 0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_HDR_RGB_LDR_A }, 0, 0, VD, .unit = "dec_profile" },
    { "hdr",       "HDR",               0, AV_OPT_TYPE_CONST, { .i64 = ASTCENC_PRF_HDR },          0, 0, VD, .unit = "dec_profile" },
    { NULL },
};

static const AVClass libastcenc_dec_class = {
    .class_name = "libastcenc decoder",
    .item_name  = av_default_item_name,
    .option     = dec_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libastcenc_decoder = {
    .p.name         = "libastcenc",
    CODEC_LONG_NAME("ASTC (Adaptive Scalable Texture Compression) image using astc-encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ASTC,
    .p.capabilities = AV_CODEC_CAP_DR1,
    CODEC_PIXFMTS(AV_PIX_FMT_RGBA, AV_PIX_FMT_RGBAF16),
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_astc_profiles),
    .p.priv_class   = &libastcenc_dec_class,
    .p.wrapper_name = "libastcenc",
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(LibAstcDecContext),
    .init           = libastcdec_init,
    FF_CODEC_DECODE_CB(libastcdec_decode),
    .close          = libastcdec_close,
};
