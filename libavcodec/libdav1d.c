/*
 * Copyright (c) 2018 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (c) 2018 James Almer <jamrial gmail com>
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

#include <dav1d/dav1d.h>

#include "libavutil/avassert.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "decode.h"
#include "internal.h"

typedef struct Libdav1dContext {
    AVClass *class;
    Dav1dContext *c;

    AVFifoBuffer *cache;
    Dav1dData data;
    int tile_threads;
} Libdav1dContext;

static av_cold int libdav1d_init(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;
    Dav1dSettings s;
    int res;

    av_log(c, AV_LOG_INFO, "libdav1d %s\n", dav1d_version());

    dav1d_default_settings(&s);
    s.n_tile_threads = dav1d->tile_threads;
    s.n_frame_threads = FFMIN(c->thread_count ? c->thread_count : av_cpu_count(), 256);

    dav1d->cache = av_fifo_alloc(8 * sizeof(AVPacket));
    if (!dav1d->cache)
        return AVERROR(ENOMEM);

    res = dav1d_open(&dav1d->c, &s);
    if (res < 0)
        return AVERROR(ENOMEM);

    return 0;
}

static void libdav1d_flush(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;

    av_fifo_reset(dav1d->cache);
    dav1d_data_unref(&dav1d->data);
    dav1d_flush(dav1d->c);
}

static int libdav1d_fifo_write(void *src, void *dst, int dst_size) {
    AVPacket *pkt_dst = dst, *pkt_src = src;

    av_assert2(dst_size >= sizeof(AVPacket));

    pkt_src->buf = NULL;
    av_packet_free_side_data(pkt_src);
    *pkt_dst = *pkt_src;

    return sizeof(AVPacket);
}

static void libdav1d_data_free(const uint8_t *data, void *opaque) {
    AVBufferRef *buf = opaque;

    av_buffer_unref(&buf);
}

static void libdav1d_frame_free(void *opaque, uint8_t *data) {
    Dav1dPicture p = { 0 };

    p.ref = opaque;
    p.data[0] = (void *) 0x1; // this has to be non-NULL
    dav1d_picture_unref(&p);
}

static const enum AVPixelFormat pix_fmt[][2] = {
    [DAV1D_PIXEL_LAYOUT_I400] = { AV_PIX_FMT_GRAY8,   AV_PIX_FMT_GRAY10 },
    [DAV1D_PIXEL_LAYOUT_I420] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10 },
    [DAV1D_PIXEL_LAYOUT_I422] = { AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV422P10 },
    [DAV1D_PIXEL_LAYOUT_I444] = { AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10 },
};

// TODO: Update once 12bit support is added.
static const int profile[] = {
    [DAV1D_PIXEL_LAYOUT_I400] = FF_PROFILE_AV1_MAIN,
    [DAV1D_PIXEL_LAYOUT_I420] = FF_PROFILE_AV1_MAIN,
    [DAV1D_PIXEL_LAYOUT_I422] = FF_PROFILE_AV1_PROFESSIONAL,
    [DAV1D_PIXEL_LAYOUT_I444] = FF_PROFILE_AV1_HIGH,
};

static int libdav1d_receive_frame(AVCodecContext *c, AVFrame *frame)
{
    Libdav1dContext *dav1d = c->priv_data;
    Dav1dData *data = &dav1d->data;
    AVPacket pkt = { 0 };
    Dav1dPicture p = { 0 };
    int res;

    if (!data->sz) {
        res = ff_decode_get_packet(c, &pkt);
        if (res < 0 && res != AVERROR_EOF)
            return res;

        if (pkt.size) {
            if (!av_fifo_space(dav1d->cache)) {
                res = av_fifo_grow(dav1d->cache, 8 * sizeof(pkt));
                if (res < 0) {
                    av_packet_unref(&pkt);
                    return res;
                }
            }

            res = dav1d_data_wrap(data, pkt.data, pkt.size, libdav1d_data_free, pkt.buf);
            if (res < 0) {
                av_packet_unref(&pkt);
                return res;
            }

            av_fifo_generic_write(dav1d->cache, &pkt, sizeof(pkt), libdav1d_fifo_write);
        } else {
            data = NULL;
        }
    }

    res = dav1d_decode(dav1d->c, data, &p);
    if (res < 0) {
        if (res == -EINVAL)
            res = AVERROR_INVALIDDATA;
        else if (res == -EAGAIN && c->internal->draining)
            res = AVERROR_EOF;

        return res;
    }

    av_assert0(p.data[0] != NULL);

    av_fifo_generic_read(dav1d->cache, &pkt, sizeof(pkt), NULL);

    frame->buf[0] = av_buffer_create(NULL, 0, libdav1d_frame_free,
                                     p.ref, AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        dav1d_picture_unref(&p);
        return AVERROR(ENOMEM);
    }

    frame->data[0] = p.data[0];
    frame->data[1] = p.data[1];
    frame->data[2] = p.data[2];
    frame->linesize[0] = p.stride[0];
    frame->linesize[1] = p.stride[1];
    frame->linesize[2] = p.stride[1];

    c->profile = profile[p.p.layout];
    frame->format = c->pix_fmt = pix_fmt[p.p.layout][p.p.bpc == 10];
    frame->width = p.p.w;
    frame->height = p.p.h;
    if (c->width != p.p.w || c->height != p.p.h) {
        res = ff_set_dimensions(c, p.p.w, p.p.h);
        if (res < 0)
            return res;
    }

    switch (p.p.chr) {
    case DAV1D_CHR_VERTICAL:
        frame->chroma_location = c->chroma_sample_location = AVCHROMA_LOC_LEFT;
        break;
    case DAV1D_CHR_COLOCATED:
        frame->chroma_location = c->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
        break;
    }
    frame->colorspace = c->colorspace = (enum AVColorSpace) p.p.mtrx;
    frame->color_primaries = c->color_primaries = (enum AVColorPrimaries) p.p.pri;
    frame->color_trc = c->color_trc = (enum AVColorTransferCharacteristic) p.p.trc;
    frame->color_range = c->color_range = p.p.fullrange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    // match timestamps and packet size
    frame->pts = frame->best_effort_timestamp = pkt.pts;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
    frame->pkt_pts = pkt.pts;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    frame->pkt_dts = pkt.dts;
    frame->pkt_pos = pkt.pos;
    frame->pkt_size = pkt.size;
    frame->pkt_duration = pkt.duration;
    frame->key_frame = p.p.type == DAV1D_FRAME_TYPE_KEY;

    switch (p.p.type) {
    case DAV1D_FRAME_TYPE_KEY:
    case DAV1D_FRAME_TYPE_INTRA:
        frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case DAV1D_FRAME_TYPE_INTER:
        frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    case DAV1D_FRAME_TYPE_SWITCH:
        frame->pict_type = AV_PICTURE_TYPE_SP;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static av_cold int libdav1d_close(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;

    av_fifo_freep(&dav1d->cache);
    dav1d_data_unref(&dav1d->data);
    dav1d_close(&dav1d->c);

    return 0;
}

#define OFFSET(x) offsetof(Libdav1dContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption libdav1d_options[] = {
    { "tilethreads",  "Tile threads",  OFFSET(tile_threads),  AV_OPT_TYPE_INT, { .i64 = 1 }, 1,  64, VD, NULL },
    { NULL }
};

static const AVClass libdav1d_class = {
    .class_name = "libdav1d decoder",
    .item_name  = av_default_item_name,
    .option     = libdav1d_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libdav1d_decoder = {
    .name           = "libdav1d",
    .long_name      = NULL_IF_CONFIG_SMALL("dav1d AV1 decoder by VideoLAN"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AV1,
    .priv_data_size = sizeof(Libdav1dContext),
    .init           = libdav1d_init,
    .close          = libdav1d_close,
    .flush          = libdav1d_flush,
    .receive_frame  = libdav1d_receive_frame,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class     = &libdav1d_class,
    .wrapper_name   = "libdav1d",
};
