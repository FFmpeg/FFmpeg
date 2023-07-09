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
#include "libavutil/cpu.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "atsc_a53.h"
#include "av1_parse.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"

#define FF_DAV1D_VERSION_AT_LEAST(x,y) \
    (DAV1D_API_VERSION_MAJOR > (x) || DAV1D_API_VERSION_MAJOR == (x) && DAV1D_API_VERSION_MINOR >= (y))

typedef struct Libdav1dContext {
    AVClass *class;
    Dav1dContext *c;
    AVBufferPool *pool;
    int pool_size;

    Dav1dData data;
    int tile_threads;
    int frame_threads;
    int max_frame_delay;
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

static void libdav1d_init_params(AVCodecContext *c, const Dav1dSequenceHeader *seq)
{
    c->profile = seq->profile;
    c->level = ((seq->operating_points[0].major_level - 2) << 2)
               | seq->operating_points[0].minor_level;

    switch (seq->chr) {
    case DAV1D_CHR_VERTICAL:
        c->chroma_sample_location = AVCHROMA_LOC_LEFT;
        break;
    case DAV1D_CHR_COLOCATED:
        c->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
        break;
    }
    c->colorspace = (enum AVColorSpace) seq->mtrx;
    c->color_primaries = (enum AVColorPrimaries) seq->pri;
    c->color_trc = (enum AVColorTransferCharacteristic) seq->trc;
    c->color_range = seq->color_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    if (seq->layout == DAV1D_PIXEL_LAYOUT_I444 &&
        seq->mtrx == DAV1D_MC_IDENTITY &&
        seq->pri  == DAV1D_COLOR_PRI_BT709 &&
        seq->trc  == DAV1D_TRC_SRGB)
        c->pix_fmt = pix_fmt_rgb[seq->hbd];
    else
        c->pix_fmt = pix_fmt[seq->layout][seq->hbd];

    c->framerate = ff_av1_framerate(seq->num_ticks_per_picture,
                                    (unsigned)seq->num_units_in_tick,
                                    (unsigned)seq->time_scale);

   if (seq->film_grain_present)
       c->properties |= FF_CODEC_PROPERTY_FILM_GRAIN;
   else
       c->properties &= ~FF_CODEC_PROPERTY_FILM_GRAIN;
}

static av_cold int libdav1d_parse_extradata(AVCodecContext *c)
{
    Dav1dSequenceHeader seq;
    size_t offset = 0;
    int res;

    if (!c->extradata || c->extradata_size <= 0)
        return 0;

    if (c->extradata[0] & 0x80) {
        int version = c->extradata[0] & 0x7F;

        if (version != 1 || c->extradata_size < 4) {
            int explode = !!(c->err_recognition & AV_EF_EXPLODE);
            av_log(c, explode ? AV_LOG_ERROR : AV_LOG_WARNING,
                   "Error decoding extradata\n");
            return explode ? AVERROR_INVALIDDATA : 0;
        }

        // Do nothing if there are no configOBUs to parse
        if (c->extradata_size == 4)
            return 0;

        offset = 4;
    }

    res = dav1d_parse_sequence_header(&seq, c->extradata + offset,
                                      c->extradata_size  - offset);
    if (res < 0)
        return 0; // Assume no seqhdr OBUs are present

    libdav1d_init_params(c, &seq);
    res = ff_set_dimensions(c, seq.max_width, seq.max_height);
    if (res < 0)
        return res;

    return 0;
}

static av_cold int libdav1d_init(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;
    Dav1dSettings s;
#if FF_DAV1D_VERSION_AT_LEAST(6,0)
    int threads = c->thread_count;
#else
    int threads = (c->thread_count ? c->thread_count : av_cpu_count()) * 3 / 2;
#endif
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
    else
        s.apply_grain = !(c->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN);

    s.all_layers = dav1d->all_layers;
    if (dav1d->operating_point >= 0)
        s.operating_point = dav1d->operating_point;
#if FF_DAV1D_VERSION_AT_LEAST(6,2)
    s.strict_std_compliance = c->strict_std_compliance > 0;
#endif

#if FF_DAV1D_VERSION_AT_LEAST(6,0)
    if (dav1d->frame_threads || dav1d->tile_threads)
        s.n_threads = FFMAX(dav1d->frame_threads, dav1d->tile_threads);
    else
        s.n_threads = FFMIN(threads, DAV1D_MAX_THREADS);
    if (dav1d->max_frame_delay > 0 && (c->flags & AV_CODEC_FLAG_LOW_DELAY))
        av_log(c, AV_LOG_WARNING, "Low delay mode requested, forcing max_frame_delay 1\n");
    s.max_frame_delay = (c->flags & AV_CODEC_FLAG_LOW_DELAY) ? 1 : dav1d->max_frame_delay;
    av_log(c, AV_LOG_DEBUG, "Using %d threads, %d max_frame_delay\n",
           s.n_threads, s.max_frame_delay);
#else
    s.n_tile_threads = dav1d->tile_threads
                     ? dav1d->tile_threads
                     : FFMIN(floor(sqrt(threads)), DAV1D_MAX_TILE_THREADS);
    s.n_frame_threads = dav1d->frame_threads
                      ? dav1d->frame_threads
                      : FFMIN(ceil(threads / s.n_tile_threads), DAV1D_MAX_FRAME_THREADS);
    if (dav1d->max_frame_delay > 0)
        s.n_frame_threads = FFMIN(s.n_frame_threads, dav1d->max_frame_delay);
    av_log(c, AV_LOG_DEBUG, "Using %d frame threads, %d tile threads\n",
           s.n_frame_threads, s.n_tile_threads);
#endif

#if FF_DAV1D_VERSION_AT_LEAST(6,8)
    if (c->skip_frame >= AVDISCARD_NONKEY)
        s.decode_frame_type = DAV1D_DECODEFRAMETYPE_KEY;
    else if (c->skip_frame >= AVDISCARD_NONINTRA)
        s.decode_frame_type = DAV1D_DECODEFRAMETYPE_INTRA;
    else if (c->skip_frame >= AVDISCARD_NONREF)
        s.decode_frame_type = DAV1D_DECODEFRAMETYPE_REFERENCE;
#endif

    res = libdav1d_parse_extradata(c);
    if (res < 0)
        return res;

    res = dav1d_open(&dav1d->c, &s);
    if (res < 0)
        return AVERROR(ENOMEM);

#if FF_DAV1D_VERSION_AT_LEAST(6,7)
    res = dav1d_get_frame_delay(&s);
    if (res < 0) // Should not happen
        return AVERROR_EXTERNAL;

    // When dav1d_get_frame_delay() returns 1, there's no delay whatsoever
    c->delay = res > 1 ? res : 0;
#endif

    return 0;
}

static void libdav1d_flush(AVCodecContext *c)
{
    Libdav1dContext *dav1d = c->priv_data;

    dav1d_data_unref(&dav1d->data);
    dav1d_flush(dav1d->c);
}

typedef struct OpaqueData {
    void    *pkt_orig_opaque;
#if FF_API_REORDERED_OPAQUE
    int64_t  reordered_opaque;
#endif
} OpaqueData;

static void libdav1d_data_free(const uint8_t *data, void *opaque) {
    AVBufferRef *buf = opaque;

    av_buffer_unref(&buf);
}

static void libdav1d_user_data_free(const uint8_t *data, void *opaque) {
    AVPacket *pkt = opaque;
    av_assert0(data == opaque);
    av_free(pkt->opaque);
    av_packet_free(&pkt);
}

static int libdav1d_receive_frame_internal(AVCodecContext *c, Dav1dPicture *p)
{
    Libdav1dContext *dav1d = c->priv_data;
    Dav1dData *data = &dav1d->data;
    int res;

    if (!data->sz) {
        AVPacket *pkt = av_packet_alloc();

        if (!pkt)
            return AVERROR(ENOMEM);

        res = ff_decode_get_packet(c, pkt);
        if (res < 0 && res != AVERROR_EOF) {
            av_packet_free(&pkt);
            return res;
        }

        if (pkt->size) {
            OpaqueData *od = NULL;

            res = dav1d_data_wrap(data, pkt->data, pkt->size,
                                  libdav1d_data_free, pkt->buf);
            if (res < 0) {
                av_packet_free(&pkt);
                return res;
            }

            pkt->buf = NULL;

FF_DISABLE_DEPRECATION_WARNINGS
            if (
#if FF_API_REORDERED_OPAQUE
                c->reordered_opaque != AV_NOPTS_VALUE ||
#endif
                (pkt->opaque && (c->flags & AV_CODEC_FLAG_COPY_OPAQUE))) {
                od = av_mallocz(sizeof(*od));
                if (!od) {
                    av_packet_free(&pkt);
                    dav1d_data_unref(data);
                    return AVERROR(ENOMEM);
                }
                od->pkt_orig_opaque  = pkt->opaque;
#if FF_API_REORDERED_OPAQUE
                od->reordered_opaque = c->reordered_opaque;
#endif
FF_ENABLE_DEPRECATION_WARNINGS
            }
            pkt->opaque = od;

            res = dav1d_data_wrap_user_data(data, (const uint8_t *)pkt,
                                            libdav1d_user_data_free, pkt);
            if (res < 0) {
                av_free(pkt->opaque);
                av_packet_free(&pkt);
                dav1d_data_unref(data);
                return res;
            }
            pkt = NULL;
        } else {
            av_packet_free(&pkt);
            if (res >= 0)
                return AVERROR(EAGAIN);
        }
    }

    res = dav1d_send_data(dav1d->c, data);
    if (res < 0) {
        if (res == AVERROR(EINVAL))
            res = AVERROR_INVALIDDATA;
        if (res != AVERROR(EAGAIN)) {
            dav1d_data_unref(data);
            return res;
        }
    }

    res = dav1d_get_picture(dav1d->c, p);
    if (res < 0) {
        if (res == AVERROR(EINVAL))
            res = AVERROR_INVALIDDATA;
        else if (res == AVERROR(EAGAIN))
            res = c->internal->draining ? AVERROR_EOF : 1;
    }

    return res;
}

static int libdav1d_receive_frame(AVCodecContext *c, AVFrame *frame)
{
    Libdav1dContext *dav1d = c->priv_data;
    Dav1dPicture pic = { 0 }, *p = &pic;
    AVPacket *pkt;
    OpaqueData *od = NULL;
#if FF_DAV1D_VERSION_AT_LEAST(5,1)
    enum Dav1dEventFlags event_flags = 0;
#endif
    int res;

    do {
        res = libdav1d_receive_frame_internal(c, p);
    } while (res > 0);

    if (res < 0)
        return res;

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

#if FF_DAV1D_VERSION_AT_LEAST(5,1)
    dav1d_get_event_flags(dav1d->c, &event_flags);
    if (c->pix_fmt == AV_PIX_FMT_NONE ||
        event_flags & DAV1D_EVENT_FLAG_NEW_SEQUENCE)
#endif
    libdav1d_init_params(c, p->seq_hdr);
    res = ff_decode_frame_props(c, frame);
    if (res < 0)
        goto fail;

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

    pkt = (AVPacket *)p->m.user_data.data;
    od  = pkt->opaque;
#if FF_API_REORDERED_OPAQUE
FF_DISABLE_DEPRECATION_WARNINGS
    if (od && od->reordered_opaque != AV_NOPTS_VALUE)
        frame->reordered_opaque = od->reordered_opaque;
    else
        frame->reordered_opaque = AV_NOPTS_VALUE;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    // restore the original user opaque value for
    // ff_decode_frame_props_from_pkt()
    pkt->opaque = od ? od->pkt_orig_opaque : NULL;
    av_freep(&od);

    // match timestamps and packet size
    res = ff_decode_frame_props_from_pkt(c, frame, pkt);
    pkt->opaque = NULL;
    if (res < 0)
        goto fail;

    frame->pkt_dts = pkt->pts;
    if (p->frame_hdr->frame_type == DAV1D_FRAME_TYPE_KEY)
        frame->flags |= AV_FRAME_FLAG_KEY;
    else
        frame->flags &= ~AV_FRAME_FLAG_KEY;

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
#if FF_DAV1D_VERSION_AT_LEAST(6,9)
        for (size_t i = 0; i < p->n_itut_t35; i++) {
            const Dav1dITUTT35 *itut_t35 = &p->itut_t35[i];
#else
        const Dav1dITUTT35 *itut_t35 = p->itut_t35;
#endif
        GetByteContext gb;
        int provider_code;

        bytestream2_init(&gb, itut_t35->payload, itut_t35->payload_size);

        provider_code = bytestream2_get_be16(&gb);
        switch (provider_code) {
        case 0x31: { // atsc_provider_code
            uint32_t user_identifier = bytestream2_get_be32(&gb);
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
            break;
        }
        case 0x3C: { // smpte_provider_code
            AVDynamicHDRPlus *hdrplus;
            int provider_oriented_code = bytestream2_get_be16(&gb);
            int application_identifier = bytestream2_get_byte(&gb);

            if (itut_t35->country_code != 0xB5 ||
                provider_oriented_code != 1 || application_identifier != 4)
                break;

            hdrplus = av_dynamic_hdr_plus_create_side_data(frame);
            if (!hdrplus) {
                res = AVERROR(ENOMEM);
                goto fail;
            }

            res = av_dynamic_hdr_plus_from_t35(hdrplus, gb.buffer,
                                               bytestream2_get_bytes_left(&gb));
            if (res < 0)
                goto fail;
            break;
        }
        default: // ignore unsupported provider codes
            break;
        }
#if FF_DAV1D_VERSION_AT_LEAST(6,9)
        }
#endif
    }
    if (p->frame_hdr->film_grain.present && (!dav1d->apply_grain ||
        (c->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN))) {
        AVFilmGrainParams *fgp = av_film_grain_params_create_side_data(frame);
        if (!fgp) {
            res = AVERROR(ENOMEM);
            goto fail;
        }

        fgp->type = AV_FILM_GRAIN_PARAMS_AV1;
        fgp->seed = p->frame_hdr->film_grain.data.seed;
        fgp->codec.aom.num_y_points = p->frame_hdr->film_grain.data.num_y_points;
        fgp->codec.aom.chroma_scaling_from_luma = p->frame_hdr->film_grain.data.chroma_scaling_from_luma;
        fgp->codec.aom.scaling_shift = p->frame_hdr->film_grain.data.scaling_shift;
        fgp->codec.aom.ar_coeff_lag = p->frame_hdr->film_grain.data.ar_coeff_lag;
        fgp->codec.aom.ar_coeff_shift = p->frame_hdr->film_grain.data.ar_coeff_shift;
        fgp->codec.aom.grain_scale_shift = p->frame_hdr->film_grain.data.grain_scale_shift;
        fgp->codec.aom.overlap_flag = p->frame_hdr->film_grain.data.overlap_flag;
        fgp->codec.aom.limit_output_range = p->frame_hdr->film_grain.data.clip_to_restricted_range;

        memcpy(&fgp->codec.aom.y_points, &p->frame_hdr->film_grain.data.y_points,
               sizeof(fgp->codec.aom.y_points));
        memcpy(&fgp->codec.aom.num_uv_points, &p->frame_hdr->film_grain.data.num_uv_points,
               sizeof(fgp->codec.aom.num_uv_points));
        memcpy(&fgp->codec.aom.uv_points, &p->frame_hdr->film_grain.data.uv_points,
               sizeof(fgp->codec.aom.uv_points));
        memcpy(&fgp->codec.aom.ar_coeffs_y, &p->frame_hdr->film_grain.data.ar_coeffs_y,
               sizeof(fgp->codec.aom.ar_coeffs_y));
        memcpy(&fgp->codec.aom.ar_coeffs_uv[0], &p->frame_hdr->film_grain.data.ar_coeffs_uv[0],
               sizeof(fgp->codec.aom.ar_coeffs_uv[0]));
        memcpy(&fgp->codec.aom.ar_coeffs_uv[1], &p->frame_hdr->film_grain.data.ar_coeffs_uv[1],
               sizeof(fgp->codec.aom.ar_coeffs_uv[1]));
        memcpy(&fgp->codec.aom.uv_mult, &p->frame_hdr->film_grain.data.uv_mult,
               sizeof(fgp->codec.aom.uv_mult));
        memcpy(&fgp->codec.aom.uv_mult_luma, &p->frame_hdr->film_grain.data.uv_luma_mult,
               sizeof(fgp->codec.aom.uv_mult_luma));
        memcpy(&fgp->codec.aom.uv_offset, &p->frame_hdr->film_grain.data.uv_offset,
               sizeof(fgp->codec.aom.uv_offset));
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

#ifndef DAV1D_MAX_FRAME_THREADS
#define DAV1D_MAX_FRAME_THREADS DAV1D_MAX_THREADS
#endif
#ifndef DAV1D_MAX_TILE_THREADS
#define DAV1D_MAX_TILE_THREADS DAV1D_MAX_THREADS
#endif
#ifndef DAV1D_MAX_FRAME_DELAY
#define DAV1D_MAX_FRAME_DELAY DAV1D_MAX_FRAME_THREADS
#endif

#define OFFSET(x) offsetof(Libdav1dContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption libdav1d_options[] = {
    { "tilethreads", "Tile threads", OFFSET(tile_threads), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, DAV1D_MAX_TILE_THREADS, VD | AV_OPT_FLAG_DEPRECATED },
    { "framethreads", "Frame threads", OFFSET(frame_threads), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, DAV1D_MAX_FRAME_THREADS, VD | AV_OPT_FLAG_DEPRECATED },
    { "max_frame_delay", "Max frame delay", OFFSET(max_frame_delay), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, DAV1D_MAX_FRAME_DELAY, VD },
    { "filmgrain", "Apply Film Grain", OFFSET(apply_grain), AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VD | AV_OPT_FLAG_DEPRECATED },
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

const FFCodec ff_libdav1d_decoder = {
    .p.name         = "libdav1d",
    CODEC_LONG_NAME("dav1d AV1 decoder by VideoLAN"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .priv_data_size = sizeof(Libdav1dContext),
    .init           = libdav1d_init,
    .close          = libdav1d_close,
    .flush          = libdav1d_flush,
    FF_CODEC_RECEIVE_FRAME_CB(libdav1d_receive_frame),
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_SETS_FRAME_PROPS |
                      FF_CODEC_CAP_AUTO_THREADS,
    .p.priv_class   = &libdav1d_class,
    .p.wrapper_name = "libdav1d",
};
