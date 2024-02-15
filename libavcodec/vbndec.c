/*
 * Vizrt Binary Image decoder
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
 * Vizrt Binary Image decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "texturedsp.h"
#include "vbn.h"
#include "libavutil/imgutils.h"

typedef struct VBNContext {
    TextureDSPContext texdsp;
    TextureDSPThreadContext dec;
} VBNContext;

static av_cold int vbn_init(AVCodecContext *avctx)
{
    VBNContext *ctx = avctx->priv_data;
    ff_texturedsp_init(&ctx->texdsp);
    return 0;
}

static int decompress(AVCodecContext *avctx, GetByteContext *gb,
                      int compression, uint8_t **outbuf)
{
    if (compression == VBN_COMPRESSION_NONE) // outbuf is left NULL because gb->buf can be used directly
        return bytestream2_get_bytes_left(gb);

    av_log(avctx, AV_LOG_ERROR, "Unsupported VBN compression: 0x%08x\n", compression);
    return AVERROR_PATCHWELCOME;
}

static int vbn_decode_frame(AVCodecContext *avctx,
                            AVFrame *frame, int *got_frame,
                            AVPacket *avpkt)
{
    VBNContext *ctx    = avctx->priv_data;
    GetByteContext gb0, *const gb = &gb0;
    uint8_t *image_buf = NULL;
    int      image_len;
    int width, height, components, format, compression, pix_fmt, linesize, data_size;
    int ret;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    if (bytestream2_get_bytes_left(gb) < VBN_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "VBN header truncated\n");
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_le32u(gb) != VBN_MAGIC ||
        bytestream2_get_le32u(gb) != VBN_MAJOR ||
        bytestream2_get_le32u(gb) != VBN_MINOR) {
        av_log(avctx, AV_LOG_ERROR, "Invalid VBN header\n");
        return AVERROR_INVALIDDATA;
    }

    width       = bytestream2_get_le32u(gb);
    height      = bytestream2_get_le32u(gb);
    components  = bytestream2_get_le32u(gb);
    format      = bytestream2_get_le32u(gb);
    pix_fmt     = bytestream2_get_le32u(gb);
    bytestream2_get_le32u(gb); // mipmaps
    data_size   = bytestream2_get_le32u(gb);
    bytestream2_seek(gb, VBN_HEADER_SIZE, SEEK_SET);

    compression = format & 0xffffff00;
    format      = format & 0xff;

    if (data_size != bytestream2_get_bytes_left(gb)) {
        av_log(avctx, AV_LOG_ERROR, "Truncated packet\n");
        return AVERROR_INVALIDDATA;
    }

    if (pix_fmt != VBN_PIX_RGBA && pix_fmt != VBN_PIX_RGB) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format: 0x%08x\n", pix_fmt);
        return AVERROR_PATCHWELCOME;
    }

    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        return ret;

    if (format == VBN_FORMAT_RAW) {
        if (pix_fmt == VBN_PIX_RGB && components == 3) {
             avctx->pix_fmt = AV_PIX_FMT_RGB24;
             linesize = avctx->width * 3;
        } else if (pix_fmt == VBN_PIX_RGBA && components == 4) {
             avctx->pix_fmt = AV_PIX_FMT_RGBA;
             linesize = avctx->width * 4;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported number of components: %d\n", components);
            return AVERROR_PATCHWELCOME;
        }
    } else if (format == VBN_FORMAT_DXT1 || format == VBN_FORMAT_DXT5)  {
        if (avctx->width % TEXTURE_BLOCK_W || avctx->height % TEXTURE_BLOCK_H) {
            av_log(avctx, AV_LOG_ERROR, "DXTx compression only supports 4 pixel aligned resolutions\n");
            return AVERROR_INVALIDDATA;
        }

        avctx->pix_fmt = AV_PIX_FMT_RGBA;
        if (format == VBN_FORMAT_DXT1) {
            ctx->dec.tex_funct = ctx->texdsp.dxt1_block;
            ctx->dec.tex_ratio = 8;
            linesize = avctx->coded_width / 2;
        } else {
            ctx->dec.tex_funct = ctx->texdsp.dxt5_block;
            ctx->dec.tex_ratio = 16;
            linesize = avctx->coded_width;
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "Unsupported VBN format: 0x%02x\n", format);
        return AVERROR_PATCHWELCOME;
    }

    image_len = decompress(avctx, gb, compression, &image_buf);
    if (image_len < 0)
        return image_len;

    if (image_len < linesize * avctx->coded_height) {
        av_log(avctx, AV_LOG_ERROR, "Insufficent data\n");
        ret = AVERROR_INVALIDDATA;
        goto out;
    }

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        goto out;

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->flags |= AV_FRAME_FLAG_KEY;

    if (format == VBN_FORMAT_RAW) {
        uint8_t *flipped = frame->data[0] + frame->linesize[0] * (frame->height - 1);
        av_image_copy_plane(flipped, -frame->linesize[0], image_buf ? image_buf : gb->buffer, linesize, linesize, frame->height);
    } else {
        ctx->dec.slice_count = av_clip(avctx->thread_count, 1, avctx->coded_height / TEXTURE_BLOCK_H);
        ctx->dec.tex_data.in = image_buf ? image_buf : gb->buffer;
        ctx->dec.raw_ratio = 16;
        ctx->dec.frame_data.out = frame->data[0] + frame->linesize[0] * (frame->height - 1);
        ctx->dec.stride = -frame->linesize[0];
        ctx->dec.width  = avctx->coded_width;
        ctx->dec.height = avctx->coded_height;
        ff_texturedsp_exec_decompress_threads(avctx, &ctx->dec);
    }

    *got_frame = 1;
    ret = avpkt->size;

out:
    av_freep(&image_buf);
    return ret;
}

const FFCodec ff_vbn_decoder = {
    .p.name         = "vbn",
    CODEC_LONG_NAME("Vizrt Binary Image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VBN,
    .init           = vbn_init,
    FF_CODEC_DECODE_CB(vbn_decode_frame),
    .priv_data_size = sizeof(VBNContext),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SLICE_THREADS,
};
