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
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/dict.h"
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
    int retval = 0;
    kvz_config *cfg = NULL;
    kvz_encoder *enc = NULL;
    const kvz_api *const api = kvz_api_get(8);

    LibkvazaarContext *const ctx = avctx->priv_data;

    // Kvazaar requires width and height to be multiples of eight.
    if (avctx->width % 8 || avctx->height % 8) {
        av_log(avctx, AV_LOG_ERROR, "Video dimensions are not a multiple of 8.\n");
        retval = AVERROR_INVALIDDATA;
        goto done;
    }

    cfg = api->config_alloc();
    if (!cfg) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate kvazaar config structure.\n");
        retval = AVERROR(ENOMEM);
        goto done;
    }

    if (!api->config_init(cfg)) {
        av_log(avctx, AV_LOG_ERROR, "Could not initialize kvazaar config structure.\n");
        retval = AVERROR_EXTERNAL;
        goto done;
    }

    cfg->width = avctx->width;
    cfg->height = avctx->height;
    cfg->framerate =
      (double)(avctx->time_base.num * avctx->ticks_per_frame) / avctx->time_base.den;
    cfg->threads = avctx->thread_count;
    cfg->target_bitrate = avctx->bit_rate;
    cfg->vui.sar_width = avctx->sample_aspect_ratio.num;
    cfg->vui.sar_height = avctx->sample_aspect_ratio.den;

    if (ctx->kvz_params) {
        AVDictionary *dict = NULL;
        if (!av_dict_parse_string(&dict, ctx->kvz_params, "=", ",", 0)) {
            AVDictionaryEntry *entry = NULL;
            while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
                if (!api->config_parse(cfg, entry->key, entry->value)) {
                    av_log(avctx, AV_LOG_WARNING,
                           "Invalid option: %s=%s.\n",
                           entry->key, entry->value);
                }
            }
            av_dict_free(&dict);
        }
    }

    enc = api->encoder_open(cfg);
    if (!enc) {
        av_log(avctx, AV_LOG_ERROR, "Could not open kvazaar encoder.\n");
        retval = AVERROR_EXTERNAL;
        goto done;
    }

    ctx->api = api;
    ctx->encoder = enc;
    ctx->config = cfg;
    enc = NULL;
    cfg = NULL;

done:
    if (cfg) api->config_destroy(cfg);
    if (enc) api->encoder_close(enc);

    return retval;
}

static av_cold int libkvazaar_close(AVCodecContext *avctx)
{
    LibkvazaarContext *ctx = avctx->priv_data;
    if (!ctx->api) return 0;

    if (ctx->encoder) {
        ctx->api->encoder_close(ctx->encoder);
        ctx->encoder = NULL;
    }

    if (ctx->config) {
        ctx->api->config_destroy(ctx->config);
        ctx->config = NULL;
    }

    return 0;
}

static int libkvazaar_encode(AVCodecContext *avctx,
                             AVPacket *avpkt,
                             const AVFrame *frame,
                             int *got_packet_ptr)
{
    int retval = 0;
    kvz_picture *img_in = NULL;
    kvz_data_chunk *data_out = NULL;
    uint32_t len_out = 0;
    LibkvazaarContext *ctx = avctx->priv_data;

    *got_packet_ptr = 0;

    if (frame) {
        int i = 0;

        av_assert0(frame->width == ctx->config->width);
        av_assert0(frame->height == ctx->config->height);
        av_assert0(frame->format == avctx->pix_fmt);

        // Allocate input picture for kvazaar.
        img_in = ctx->api->picture_alloc(frame->width, frame->height);
        if (!img_in) {
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate picture.\n");
            retval = AVERROR(ENOMEM);
            goto done;
        }

        // Copy pixels from frame to img_in.
        for (i = 0; i < 3; ++i) {
            uint8_t *dst = img_in->data[i];
            uint8_t *src = frame->data[i];
            int width = (i == 0) ? frame->width : (frame->width / 2);
            int height = (i == 0) ? frame->height : (frame->height / 2);
            int y = 0;
            for (y = 0; y < height; ++y) {
                memcpy(dst, src, width);
                src += frame->linesize[i];
                dst += width;
            }
        }
    }

    if (!ctx->api->encoder_encode(ctx->encoder, img_in, &data_out, &len_out, NULL)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to encode frame.\n");
        retval = AVERROR_EXTERNAL;
        goto done;
    }

    if (data_out) {
        kvz_data_chunk *chunk = NULL;
        uint64_t written = 0;

        retval = ff_alloc_packet(avpkt, len_out);
        if (retval < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate output packet.\n");
            goto done;
        }

        for (chunk = data_out; chunk != NULL; chunk = chunk->next) {
            av_assert0(written + chunk->len <= len_out);
            memcpy(avpkt->data + written, chunk->data, chunk->len);
            written += chunk->len;
        }
        *got_packet_ptr = 1;

        ctx->api->chunk_free(data_out);
        data_out = NULL;
    }

done:
    if (img_in) ctx->api->picture_free(img_in);
    if (data_out) ctx->api->chunk_free(data_out);
    return retval;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static const AVOption options[] = {
    { "kvazaar-params", "Set kvazaar parameters as a comma-separated list of name=value pairs.",
      offsetof(LibkvazaarContext, kvz_params), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0,
      AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
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
    .capabilities     = AV_CODEC_CAP_DELAY,
    .pix_fmts         = pix_fmts,

    .priv_class       = &class,
    .priv_data_size   = sizeof(LibkvazaarContext),
    .defaults         = defaults,

    .init             = libkvazaar_init,
    .encode2          = libkvazaar_encode,
    .close            = libkvazaar_close,
};
