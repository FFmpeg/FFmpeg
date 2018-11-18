/*
 * libkvazaar encoder
 *
 * Copyright (c) 2015 Tampere University of Technology
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

#include <kvazaar.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"

typedef struct LibkvazaarContext {
    const AVClass *class;

    const kvz_api *api;
    kvz_encoder *encoder;
    kvz_config *config;

    char *kvz_params;
} LibkvazaarContext;

static av_cold int libkvazaar_init(AVCodecContext *avctx)
{
    LibkvazaarContext *const ctx = avctx->priv_data;
    const kvz_api *const api = ctx->api = kvz_api_get(8);
    kvz_config *cfg = NULL;
    kvz_encoder *enc = NULL;

    /* Kvazaar requires width and height to be multiples of eight. */
    if (avctx->width % 8 || avctx->height % 8) {
        av_log(avctx, AV_LOG_ERROR,
               "Video dimensions are not a multiple of 8 (%dx%d).\n",
               avctx->width, avctx->height);
        return AVERROR(ENOSYS);
    }

    ctx->config = cfg = api->config_alloc();
    if (!cfg) {
        av_log(avctx, AV_LOG_ERROR,
               "Could not allocate kvazaar config structure.\n");
        return AVERROR(ENOMEM);
    }

    if (!api->config_init(cfg)) {
        av_log(avctx, AV_LOG_ERROR,
               "Could not initialize kvazaar config structure.\n");
        return AVERROR_BUG;
    }

    cfg->width  = avctx->width;
    cfg->height = avctx->height;

    if (avctx->ticks_per_frame > INT_MAX / avctx->time_base.num) {
        av_log(avctx, AV_LOG_ERROR,
               "Could not set framerate for kvazaar: integer overflow\n");
        return AVERROR(EINVAL);
    }
    cfg->framerate_num   = avctx->time_base.den;
    cfg->framerate_denom = avctx->time_base.num * avctx->ticks_per_frame;
    cfg->target_bitrate = avctx->bit_rate;
    cfg->vui.sar_width  = avctx->sample_aspect_ratio.num;
    cfg->vui.sar_height = avctx->sample_aspect_ratio.den;

    if (ctx->kvz_params) {
        AVDictionary *dict = NULL;
        if (!av_dict_parse_string(&dict, ctx->kvz_params, "=", ",", 0)) {
            AVDictionaryEntry *entry = NULL;
            while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
                if (!api->config_parse(cfg, entry->key, entry->value)) {
                    av_log(avctx, AV_LOG_WARNING, "Invalid option: %s=%s.\n",
                           entry->key, entry->value);
                }
            }
            av_dict_free(&dict);
        }
    }

    ctx->encoder = enc = api->encoder_open(cfg);
    if (!enc) {
        av_log(avctx, AV_LOG_ERROR, "Could not open kvazaar encoder.\n");
        return AVERROR_BUG;
    }

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        kvz_data_chunk *data_out = NULL;
        kvz_data_chunk *chunk = NULL;
        uint32_t len_out;
        uint8_t *p;

        if (!api->encoder_headers(enc, &data_out, &len_out))
            return AVERROR(ENOMEM);

        avctx->extradata = p = av_mallocz(len_out + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!p) {
            ctx->api->chunk_free(data_out);
            return AVERROR(ENOMEM);
        }

        avctx->extradata_size = len_out;

        for (chunk = data_out; chunk != NULL; chunk = chunk->next) {
            memcpy(p, chunk->data, chunk->len);
            p += chunk->len;
        }

        ctx->api->chunk_free(data_out);
    }

    return 0;
}

static av_cold int libkvazaar_close(AVCodecContext *avctx)
{
    LibkvazaarContext *ctx = avctx->priv_data;

    if (ctx->api) {
      ctx->api->encoder_close(ctx->encoder);
      ctx->api->config_destroy(ctx->config);
    }

    if (avctx->extradata)
        av_freep(&avctx->extradata);

    return 0;
}

static int libkvazaar_encode(AVCodecContext *avctx,
                             AVPacket *avpkt,
                             const AVFrame *frame,
                             int *got_packet_ptr)
{
    LibkvazaarContext *ctx = avctx->priv_data;
    kvz_picture *input_pic = NULL;
    kvz_picture *recon_pic = NULL;
    kvz_frame_info frame_info;
    kvz_data_chunk *data_out = NULL;
    uint32_t len_out = 0;
    int retval = 0;

    *got_packet_ptr = 0;

    if (frame) {
        if (frame->width != ctx->config->width ||
                frame->height != ctx->config->height) {
            av_log(avctx, AV_LOG_ERROR,
                   "Changing video dimensions during encoding is not supported. "
                   "(changed from %dx%d to %dx%d)\n",
                   ctx->config->width, ctx->config->height,
                   frame->width, frame->height);
            retval = AVERROR_INVALIDDATA;
            goto done;
        }

        if (frame->format != avctx->pix_fmt) {
            av_log(avctx, AV_LOG_ERROR,
                   "Changing pixel format during encoding is not supported. "
                   "(changed from %s to %s)\n",
                   av_get_pix_fmt_name(avctx->pix_fmt),
                   av_get_pix_fmt_name(frame->format));
            retval = AVERROR_INVALIDDATA;
            goto done;
        }

        // Allocate input picture for kvazaar.
        input_pic = ctx->api->picture_alloc(frame->width, frame->height);
        if (!input_pic) {
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate picture.\n");
            retval = AVERROR(ENOMEM);
            goto done;
        }

        // Copy pixels from frame to input_pic.
        {
            int dst_linesizes[4] = {
              frame->width,
              frame->width / 2,
              frame->width / 2,
              0
            };
            av_image_copy(input_pic->data, dst_linesizes,
                          (const uint8_t **)frame->data, frame->linesize,
                          frame->format, frame->width, frame->height);
        }

        input_pic->pts = frame->pts;
    }

    retval = ctx->api->encoder_encode(ctx->encoder,
                                      input_pic,
                                      &data_out, &len_out,
                                      &recon_pic, NULL,
                                      &frame_info);
    if (!retval) {
        av_log(avctx, AV_LOG_ERROR, "Failed to encode frame.\n");
        retval = AVERROR_INVALIDDATA;
        goto done;
    }
    else
        retval = 0; /* kvazaar returns 1 on success */

    if (data_out) {
        kvz_data_chunk *chunk = NULL;
        uint64_t written = 0;

        retval = ff_alloc_packet2(avctx, avpkt, len_out, len_out);
        if (retval < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate output packet.\n");
            goto done;
        }

        for (chunk = data_out; chunk != NULL; chunk = chunk->next) {
            av_assert0(written + chunk->len <= len_out);
            memcpy(avpkt->data + written, chunk->data, chunk->len);
            written += chunk->len;
        }

        avpkt->pts = recon_pic->pts;
        avpkt->dts = recon_pic->dts;
        avpkt->flags = 0;
        // IRAP VCL NAL unit types span the range
        // [BLA_W_LP (16), RSV_IRAP_VCL23 (23)].
        if (frame_info.nal_unit_type >= KVZ_NAL_BLA_W_LP &&
                frame_info.nal_unit_type <= KVZ_NAL_RSV_IRAP_VCL23) {
            avpkt->flags |= AV_PKT_FLAG_KEY;
        }

        *got_packet_ptr = 1;
    }

done:
    ctx->api->picture_free(input_pic);
    ctx->api->picture_free(recon_pic);
    ctx->api->chunk_free(data_out);
    return retval;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

#define OFFSET(x) offsetof(LibkvazaarContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "kvazaar-params", "Set kvazaar parameters as a comma-separated list of key=value pairs.",
        OFFSET(kvz_params), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VE },
    { NULL },
};

static const AVClass class = {
    .class_name = "libkvazaar",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault defaults[] = {
    { "b", "0" },
    { NULL },
};

AVCodec ff_libkvazaar_encoder = {
    .name             = "libkvazaar",
    .long_name        = NULL_IF_CONFIG_SMALL("libkvazaar H.265 / HEVC"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_HEVC,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .pix_fmts         = pix_fmts,

    .priv_class       = &class,
    .priv_data_size   = sizeof(LibkvazaarContext),
    .defaults         = defaults,

    .init             = libkvazaar_init,
    .encode2          = libkvazaar_encode,
    .close            = libkvazaar_close,

    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,

    .wrapper_name     = "libkvazaar",
};
