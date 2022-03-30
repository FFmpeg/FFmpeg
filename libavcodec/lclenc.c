/*
 * LCL (LossLess Codec Library) Codec
 * Copyright (c) 2002-2004 Roberto Togni
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
 * LCL (LossLess Codec Library) Video Codec
 * Decoder for MSZH and ZLIB codecs
 * Experimental encoder for ZLIB RGB24
 *
 * Fourcc: MSZH, ZLIB
 *
 * Original Win32 dll:
 * Ver2.23 By Kenji Oshima 2000.09.20
 * avimszh.dll, avizlib.dll
 *
 * A description of the decoding algorithm can be found here:
 *   http://www.pcisys.net/~melanson/codecs
 *
 * Supports: BGR24 (RGB 24bpp)
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/avassert.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "lcl.h"
#include "zlib_wrapper.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include <zlib.h>

typedef struct LclEncContext {

    AVCodecContext *avctx;

    // Image type
    int imgtype;
    // Compression type
    int compression;
    // Flags
    int flags;
    FFZStream zstream;
} LclEncContext;

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *p, int *got_packet)
{
    LclEncContext *c = avctx->priv_data;
    z_stream *const zstream = &c->zstream.zstream;
    int i, ret;
    int zret; // Zlib return code
    int max_size = deflateBound(zstream, avctx->width * avctx->height * 3);

    if ((ret = ff_alloc_packet(avctx, pkt, max_size)) < 0)
        return ret;

    if(avctx->pix_fmt != AV_PIX_FMT_BGR24){
        av_log(avctx, AV_LOG_ERROR, "Format not supported!\n");
        return -1;
    }

    zret = deflateReset(zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Deflate reset error: %d\n", zret);
        return -1;
    }
    zstream->next_out  = pkt->data;
    zstream->avail_out = pkt->size;

    for(i = avctx->height - 1; i >= 0; i--) {
        zstream->next_in  = p->data[0] + p->linesize[0] * i;
        zstream->avail_in = avctx->width * 3;
        zret = deflate(zstream, Z_NO_FLUSH);
        if (zret != Z_OK) {
            av_log(avctx, AV_LOG_ERROR, "Deflate error: %d\n", zret);
            return -1;
        }
    }
    zret = deflate(zstream, Z_FINISH);
    if (zret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR, "Deflate error: %d\n", zret);
        return -1;
    }

    pkt->size   = zstream->total_out;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    LclEncContext *c = avctx->priv_data;

    c->avctx= avctx;

    av_assert0(avctx->width && avctx->height);

    avctx->extradata = av_mallocz(8 + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    c->compression = avctx->compression_level == FF_COMPRESSION_DEFAULT ?
                            COMP_ZLIB_NORMAL :
                            av_clip(avctx->compression_level, 0, 9);
    c->flags = 0;
    c->imgtype = IMGTYPE_RGB24;
    avctx->bits_per_coded_sample= 24;

    avctx->extradata[0]= 4;
    avctx->extradata[1]= 0;
    avctx->extradata[2]= 0;
    avctx->extradata[3]= 0;
    avctx->extradata[4]= c->imgtype;
    avctx->extradata[5]= c->compression;
    avctx->extradata[6]= c->flags;
    avctx->extradata[7]= CODEC_ZLIB;
    c->avctx->extradata_size= 8;

    return ff_deflate_init(&c->zstream, c->compression, avctx);
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    LclEncContext *c = avctx->priv_data;

    ff_deflate_end(&c->zstream);

    return 0;
}

const FFCodec ff_zlib_encoder = {
    .p.name         = "zlib",
    .p.long_name    = NULL_IF_CONFIG_SMALL("LCL (LossLess Codec Library) ZLIB"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ZLIB,
    .priv_data_size = sizeof(LclEncContext),
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    .close          = encode_end,
    .p.capabilities = AV_CODEC_CAP_FRAME_THREADS,
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_BGR24, AV_PIX_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
