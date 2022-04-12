/*
 * Vizrt Binary Image encoder
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
 * Vizrt Binary Image encoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "texturedsp.h"
#include "vbn.h"

#include "libavutil/imgutils.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"

typedef struct VBNContext {
    AVClass *class;
    TextureDSPContext dxtc;
    int format;
    TextureDSPThreadContext enc;
} VBNContext;

static int vbn_encode(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *frame, int *got_packet)
{
    VBNContext *ctx = avctx->priv_data;
    PutByteContext pb0, *const pb = &pb0;
    int ret;
    ptrdiff_t linesize;
    int64_t pkt_size;

    ret = av_image_check_size2(frame->width, frame->height, INT_MAX, frame->format, 0, avctx);
    if (ret < 0)
        return ret;

    if (ctx->format == VBN_FORMAT_DXT1 || ctx->format == VBN_FORMAT_DXT5) {
        if (frame->width % TEXTURE_BLOCK_W || frame->height % TEXTURE_BLOCK_H) {
            av_log(avctx, AV_LOG_ERROR, "Video size %dx%d is not multiple of 4\n", frame->width, frame->height);
            return AVERROR(EINVAL);
        }
        if (frame->format != AV_PIX_FMT_RGBA) {
            av_log(avctx, AV_LOG_ERROR, "DXT formats only support RGBA pixel format\n");
            return AVERROR(EINVAL);
        }
        ctx->enc.raw_ratio = 16;
        ctx->enc.slice_count = av_clip(avctx->thread_count, 1, avctx->height / TEXTURE_BLOCK_H);
    }

    switch (ctx->format) {
    case VBN_FORMAT_DXT1:
        linesize = frame->width / 2;
        ctx->enc.tex_funct = ctx->dxtc.dxt1_block;
        ctx->enc.tex_ratio = 8;
        break;
    case VBN_FORMAT_DXT5:
        linesize = frame->width;
        ctx->enc.tex_funct = ctx->dxtc.dxt5_block;
        ctx->enc.tex_ratio = 16;
        break;
    case VBN_FORMAT_RAW:
        linesize = av_image_get_linesize(frame->format, frame->width, 0);
        if (linesize < 0)
            return linesize;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid format %02X\n", ctx->format);
        return AVERROR(EINVAL);
    }

    pkt_size = VBN_HEADER_SIZE + linesize * frame->height;
    if (pkt_size > INT_MAX)
        return AVERROR(EINVAL);

    if ((ret = ff_get_encode_buffer(avctx, pkt, pkt_size, 0)) < 0)
        return ret;

    memset(pkt->data, 0, VBN_HEADER_SIZE);
    bytestream2_init_writer(pb, pkt->data, pkt_size);
    bytestream2_put_le32u(pb, VBN_MAGIC);
    bytestream2_put_le32u(pb, VBN_MAJOR);
    bytestream2_put_le32u(pb, VBN_MINOR);
    bytestream2_put_le32u(pb, frame->width);
    bytestream2_put_le32u(pb, frame->height);
    bytestream2_put_le32u(pb, frame->format == AV_PIX_FMT_RGBA ? 4 : 3);
    bytestream2_put_le32u(pb, ctx->format);
    bytestream2_put_le32u(pb, frame->format == AV_PIX_FMT_RGBA ? VBN_PIX_RGBA : VBN_PIX_RGB);
    bytestream2_put_le32u(pb, 0); // mipmaps
    bytestream2_put_le32u(pb, pkt_size - VBN_HEADER_SIZE);
    bytestream2_seek_p(pb, 64, SEEK_SET);
    bytestream2_put_le32u(pb, pkt_size - VBN_HEADER_SIZE);

    if (ctx->format == VBN_FORMAT_DXT1 || ctx->format == VBN_FORMAT_DXT5) {
        ctx->enc.frame_data.in = (frame->height - 1) * frame->linesize[0] + frame->data[0];
        ctx->enc.stride = -frame->linesize[0];
        ctx->enc.tex_data.out = pkt->data + VBN_HEADER_SIZE;
        avctx->execute2(avctx, ff_texturedsp_compress_thread, &ctx->enc, NULL, ctx->enc.slice_count);
    } else {
        uint8_t *flipped = frame->data[0] + frame->linesize[0] * (frame->height - 1);
        av_image_copy_plane(pkt->data + VBN_HEADER_SIZE, linesize, flipped, -frame->linesize[0], linesize, frame->height);
    }

    *got_packet = 1;
    return 0;
}

static av_cold int vbn_init(AVCodecContext *avctx)
{
    VBNContext *ctx = avctx->priv_data;
    ff_texturedspenc_init(&ctx->dxtc);
    return 0;
}

#define OFFSET(x) offsetof(VBNContext, x)
#define FLAGS     AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "format",      "Texture format", OFFSET(format), AV_OPT_TYPE_INT,   { .i64 = VBN_FORMAT_DXT5 }, VBN_FORMAT_RAW, VBN_FORMAT_DXT5, FLAGS, "format" },
        { "raw",     "RAW texture",                 0, AV_OPT_TYPE_CONST, { .i64 = VBN_FORMAT_RAW  },              0,               0, FLAGS, "format" },
        { "dxt1",    "DXT1 texture",                0, AV_OPT_TYPE_CONST, { .i64 = VBN_FORMAT_DXT1 },              0,               0, FLAGS, "format" },
        { "dxt5",    "DXT5 texture",                0, AV_OPT_TYPE_CONST, { .i64 = VBN_FORMAT_DXT5 },              0,               0, FLAGS, "format" },
    { NULL },
};

static const AVClass vbnenc_class = {
    .class_name = "VBN encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_vbn_encoder = {
    .p.name         = "vbn",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Vizrt Binary Image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VBN,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SLICE_THREADS,
    .p.priv_class   = &vbnenc_class,
    .init           = vbn_init,
    FF_CODEC_ENCODE_CB(vbn_encode),
    .priv_data_size = sizeof(VBNContext),
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE,
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
