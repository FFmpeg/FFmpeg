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
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "atsc_a53.h"
#include "avcodec.h"
#include "bytestream.h"
#include "decode.h"
#include "internal.h"

typedef struct Libdav1dContext {
    AVClass *class;
    Dav1dContext *c;
    AVBufferPool *pool;
    int pool_size;

    Dav1dData data;
    int tile_threads;
    int frame_threads;
    int apply_grain;
    int operating_point;
    int all_layers;
} Libdav1dContext;

static const enum AVPixelFormat pix_fmt[][3] = {
    [DAV1D_PIXEL_LAYOUT_I400] = { AV_PIX_FMT_GRAY8,   AV_PIX_FMT_GRAY10,    AV_PIX_FMT_GRAY12 },
    [DAV1D_PIXEL_LAYOUT_I420] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV420P12 },
    [DAV1D_PIXEL_LAYOUT_I422] = { AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV422P12 },
    [DAV1D_PIXEL_LAYOUT_I444] = { AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P12 },
};

static const enum AVPixelFormat pix_fmt_rgb[3] = {
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
};

static void libdav1d_log_callback(void *opaque, const char *fmt, va_list vl)
{
    AVCodecContext *c = opaque;

    av_vlog(c, AV_LOG_ERROR, fmt, vl);
}

static int libdav1d_picture_allocator(Dav1dPicture *p, void *cookie)
{
    Libdav1dContext *dav1d = cookie;
    enum AVPixelFormat format = pix_fmt[p->p.layout][p->seq_hdr->hbd];
    int ret, linesize[4], h = FFALIGN(p->p.h, 128), w = FFALIGN(p->p.w, 128);
    uint8_t *aligned_ptr, *data[4];
    AVBufferRef *buf;

    ret = av_image_get_buffer_size(format, w, h, DAV1D_PICTURE_ALIGNMENT);
    if (ret < 0)
        return ret;

    if (ret != dav1d->pool_size) {
        av_buffer_pool_uninit(&dav1d->pool);
        // Use twice the amount of required padding bytes for aligned_ptr below.
        dav1d->pool = av_buffer_pool_init(ret + DAV1D_PICTURE_ALIGNMENT * 2, NULL);
        if (!dav1d->pool) {
            dav1d->pool_size = 0;
            return AVERROR(ENOMEM);
        }
        dav1d->pool_size = ret;
    }
    buf = av_buffer_pool_get(dav1d->pool);
    if (!buf)
        return AVERROR(ENOMEM);

    // libdav1d requires DAV1D_PICTURE_ALIGNMENT aligned buffers, which av_malloc()
    // doesn't guarantee for example when AVX is disabled at configure time.
    // Use the extra DAV1D_PICTURE_ALIGNMENT padding bytes in the buffer to align it
    // if required.
    aligned_ptr = (uint8_t *)FFALIGN((uintptr_t)buf->data, DAV1D_PICTURE_ALIGNMENT);
    ret = av_image_fill_arrays(data, linesize, aligned_ptr, format, w, h,
                               DAV1D_PICTURE_ALIGNMENT);
    if (ret < 0) {
        av_buffer_unref(&buf);
        return ret;
    }

    p->data[0] = data[0];
    p->data[1] = data[1];
    p->data[2] = data[2];
    p->stride[0] = linesize[0];
    p->stride[1] = linesize[1];
    p->allocator_data = buf;

    return 0;
}

static void libdav1d_picture_release(Dav1dPicture *p, void *cookie)
{
    AVBufferRef *buf = p->allocator_data;

    av_buffer_unref(&buf);
}

static av_cold int libdav1d_init(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;
    Dav1dSettings s;
    int threads = (c->thread_count ? c->thread_count : av_cpu_count()) * 3 / 2;
    int res;

    av_log(c, AV_LOG_INFO, "libdav1d %s\n", dav1d_version());

    dav1d_default_settings(&s);
    s.logger.cookie = c;
    s.logger.callback = libdav1d_log_callback;
    s.allocator.cookie = dav1d;
    s.allocator.alloc_picture_callback = libdav1d_picture_allocator;
    s.allocator.release_picture_callback = libdav1d_picture_release;
    s.frame_size_limit = c->max_pixels;
    if (dav1d->apply_grain >= 0)
        s.apply_grain = dav1d->apply_grain;

    s.all_layers = dav1d->all_layers;
    if (dav1d->operating_point >= 0)
        s.operating_point = dav1d->operating_point;

    s.n_tile_threads = dav1d->tile_threads
                     ? dav1d->tile_threads
                     : FFMIN(floor(sqrt(threads)), DAV1D_MAX_TILE_THREADS);
    s.n_frame_threads = dav1d->frame_threads
                      ? dav1d->frame_threads
                      : FFMIN(ceil(threads / s.n_tile_threads), DAV1D_MAX_FRAME_THREADS);
    av_log(c, AV_LOG_DEBUG, "Using %d frame threads, %d tile threads\n",
           s.n_frame_threads, s.n_tile_threads);

    res = dav1d_open(&dav1d->c, &s);
    if (res < 0)
        return AVERROR(ENOMEM);

    return 0;
}

static void libdav1d_flush(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;

    dav1d_data_unref(&dav1d->data);
    dav1d_flush(dav1d->c);
}

static void libdav1d_data_free(const uint8_t *data, void *opaque) {
    AVBufferRef *buf = opaque;

    av_buffer_unref(&buf);
}

static void libdav1d_user_data_free(const uint8_t *data, void *opaque) {
    av_assert0(data == opaque);
    av_free(opaque);
}

static int libdav1d_receive_frame(AVCodecContext *c, AVFrame *frame)
{
    Libdav1dContext *dav1d = c->priv_data;
    Dav1dData *data = &dav1d->data;
    Dav1dPicture pic = { 0 }, *p = &pic;
    int res;

    if (!data->sz) {
        AVPacket pkt = { 0 };

        res = ff_decode_get_packet(c, &pkt);
        if (res < 0 && res != AVERROR_EOF)
            return res;

        if (pkt.size) {
            res = dav1d_data_wrap(data, pkt.data, pkt.size, libdav1d_data_free, pkt.buf);
            if (res < 0) {
                av_packet_unref(&pkt);
                return res;
            }

            data->m.timestamp = pkt.pts;
            data->m.offset = pkt.pos;
            data->m.duration = pkt.duration;

            pkt.buf = NULL;
            av_packet_unref(&pkt);

            if (c->reordered_opaque != AV_NOPTS_VALUE) {
                uint8_t *reordered_opaque = av_malloc(sizeof(c->reordered_opaque));
                if (!reordered_opaque) {
                    dav1d_data_unref(data);
                    return AVERROR(ENOMEM);
                }

                memcpy(reordered_opaque, &c->reordered_opaque, sizeof(c->reordered_opaque));
                res = dav1d_data_wrap_user_data(data, reordered_opaque,
                                                libdav1d_user_data_free, reordered_opaque);
                if (res < 0) {
                    av_free(reordered_opaque);
                    dav1d_data_unref(data);
                    return res;
                }
            }
        }
    }

    res = dav1d_send_data(dav1d->c, data);
    if (res < 0) {
        if (res == AVERROR(EINVAL))
            res = AVERROR_INVALIDDATA;
        if (res != AVERROR(EAGAIN))
            return res;
    }

    res = dav1d_get_picture(dav1d->c, p);
    if (res < 0) {
        if (res == AVERROR(EINVAL))
            res = AVERROR_INVALIDDATA;
        else if (res == AVERROR(EAGAIN) && c->internal->draining)
            res = AVERROR_EOF;

        return res;
    }

    av_assert0(p->data[0] && p->allocator_data);

    // This requires the custom allocator above
    frame->buf[0] = av_buffer_ref(p->allocator_data);
    if (!frame->buf[0]) {
        dav1d_picture_unref(p);
        return AVERROR(ENOMEM);
    }

    frame->data[0] = p->data[0];
    frame->data[1] = p->data[1];
    frame->data[2] = p->data[2];
    frame->linesize[0] = p->stride[0];
    frame->linesize[1] = p->stride[1];
    frame->linesize[2] = p->stride[1];

    c->profile = p->seq_hdr->profile;
    c->level = ((p->seq_hdr->operating_points[0].major_level - 2) << 2)
               | p->seq_hdr->operating_points[0].minor_level;
    frame->width = p->p.w;
    frame->height = p->p.h;
    if (c->width != p->p.w || c->height != p->p.h) {
        res = ff_set_dimensions(c, p->p.w, p->p.h);
        if (res < 0)
            goto fail;
    }

    av_reduce(&frame->sample_aspect_ratio.num,
              &frame->sample_aspect_ratio.den,
              frame->height * (int64_t)p->frame_hdr->render_width,
              frame->width  * (int64_t)p->frame_hdr->render_height,
              INT_MAX);
    ff_set_sar(c, frame->sample_aspect_ratio);

    switch (p->seq_hdr->chr) {
    case DAV1D_CHR_VERTICAL:
        frame->chroma_location = c->chroma_sample_location = AVCHROMA_LOC_LEFT;
        break;
    case DAV1D_CHR_COLOCATED:
        frame->chroma_location = c->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
        break;
    }
    frame->colorspace = c->colorspace = (enum AVColorSpace) p->seq_hdr->mtrx;
    frame->color_primaries = c->color_primaries = (enum AVColorPrimaries) p->seq_hdr->pri;
    frame->color_trc = c->color_trc = (enum AVColorTransferCharacteristic) p->seq_hdr->trc;
    frame->color_range = c->color_range = p->seq_hdr->color_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    if (p->p.layout == DAV1D_PIXEL_LAYOUT_I444 &&
        p->seq_hdr->mtrx == DAV1D_MC_IDENTITY &&
        p->seq_hdr->pri  == DAV1D_COLOR_PRI_BT709 &&
        p->seq_hdr->trc  == DAV1D_TRC_SRGB)
        frame->format = c->pix_fmt = pix_fmt_rgb[p->seq_hdr->hbd];
    else
        frame->format = c->pix_fmt = pix_fmt[p->p.layout][p->seq_hdr->hbd];

    if (p->m.user_data.data)
        memcpy(&frame->reordered_opaque, p->m.user_data.data, sizeof(frame->reordered_opaque));
    else
        frame->reordered_opaque = AV_NOPTS_VALUE;

    if (p->seq_hdr->num_units_in_tick && p->seq_hdr->time_scale) {
        av_reduce(&c->framerate.den, &c->framerate.num,
                  p->seq_hdr->num_units_in_tick, p->seq_hdr->time_scale, INT_MAX);
        if (p->seq_hdr->equal_picture_interval)
            c->ticks_per_frame = p->seq_hdr->num_ticks_per_picture;
    }

    // match timestamps and packet size
    frame->pts = frame->best_effort_timestamp = p->m.timestamp;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
    frame->pkt_pts = p->m.timestamp;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    frame->pkt_dts = p->m.timestamp;
    frame->pkt_pos = p->m.offset;
    frame->pkt_size = p->m.size;
    frame->pkt_duration = p->m.duration;
    frame->key_frame = p->frame_hdr->frame_type == DAV1D_FRAME_TYPE_KEY;

    switch (p->frame_hdr->frame_type) {
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
        res = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (p->mastering_display) {
        AVMasteringDisplayMetadata *mastering = av_mastering_display_metadata_create_side_data(frame);
        if (!mastering) {
            res = AVERROR(ENOMEM);
            goto fail;
        }

        for (int i = 0; i < 3; i++) {
            mastering->display_primaries[i][0] = av_make_q(p->mastering_display->primaries[i][0], 1 << 16);
            mastering->display_primaries[i][1] = av_make_q(p->mastering_display->primaries[i][1], 1 << 16);
        }
        mastering->white_point[0] = av_make_q(p->mastering_display->white_point[0], 1 << 16);
        mastering->white_point[1] = av_make_q(p->mastering_display->white_point[1], 1 << 16);

        mastering->max_luminance = av_make_q(p->mastering_display->max_luminance, 1 << 8);
        mastering->min_luminance = av_make_q(p->mastering_display->min_luminance, 1 << 14);

        mastering->has_primaries = 1;
        mastering->has_luminance = 1;
    }
    if (p->content_light) {
        AVContentLightMetadata *light = av_content_light_metadata_create_side_data(frame);
        if (!light) {
            res = AVERROR(ENOMEM);
            goto fail;
        }
        light->MaxCLL = p->content_light->max_content_light_level;
        light->MaxFALL = p->content_light->max_frame_average_light_level;
    }
    if (p->itut_t35) {
        GetByteContext gb;
        unsigned int user_identifier;

        bytestream2_init(&gb, p->itut_t35->payload, p->itut_t35->payload_size);
        bytestream2_skip(&gb, 1); // terminal provider code
        bytestream2_skip(&gb, 1); // terminal provider oriented code
        user_identifier = bytestream2_get_be32(&gb);
        switch (user_identifier) {
        case MKBETAG('G', 'A', '9', '4'): { // closed captions
            AVBufferRef *buf = NULL;

            res = ff_parse_a53_cc(&buf, gb.buffer, bytestream2_get_bytes_left(&gb));
            if (res < 0)
                goto fail;
            if (!res)
                break;

            if (!av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_A53_CC, buf))
                av_buffer_unref(&buf);

            c->properties |= FF_CODEC_PROPERTY_CLOSED_CAPTIONS;
            break;
        }
        default: // ignore unsupported identifiers
            break;
        }
    }

    res = 0;
fail:
    dav1d_picture_unref(p);
    if (res < 0)
        av_frame_unref(frame);
    return res;
}

static av_cold int libdav1d_close(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;

    av_buffer_pool_uninit(&dav1d->pool);
    dav1d_data_unref(&dav1d->data);
    dav1d_close(&dav1d->c);

    return 0;
}

#define OFFSET(x) offsetof(Libdav1dContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption libdav1d_options[] = {
    { "tilethreads", "Tile threads", OFFSET(tile_threads), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, DAV1D_MAX_TILE_THREADS, VD },
    { "framethreads", "Frame threads", OFFSET(frame_threads), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, DAV1D_MAX_FRAME_THREADS, VD },
    { "filmgrain", "Apply Film Grain", OFFSET(apply_grain), AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VD },
    { "oppoint",  "Select an operating point of the scalable bitstream", OFFSET(operating_point), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 31, VD },
    { "alllayers", "Output all spatial layers", OFFSET(all_layers), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VD },
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
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class     = &libdav1d_class,
    .wrapper_name   = "libdav1d",
};
