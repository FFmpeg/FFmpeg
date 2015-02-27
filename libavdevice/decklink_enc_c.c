/*
 * Blackmagic DeckLink output
 * Copyright (c) 2013-2014 Ramiro Polla
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

#include "libavformat/avformat.h"
#include "libavutil/opt.h"

#include "decklink_common_c.h"
#include "decklink_enc.h"

#define OFFSET(x) offsetof(struct decklink_cctx, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "list_devices", "list available devices"  , OFFSET(list_devices), AV_OPT_TYPE_INT   , { .i64 = 0   }, 0, 1, ENC },
    { "list_formats", "list supported formats"  , OFFSET(list_formats), AV_OPT_TYPE_INT   , { .i64 = 0   }, 0, 1, ENC },
    { "preroll"     , "video preroll in seconds", OFFSET(preroll     ), AV_OPT_TYPE_DOUBLE, { .dbl = 0.5 }, 0, 5, ENC },
    { NULL },
};

static const AVClass decklink_muxer_class = {
    .class_name = "Blackmagic DeckLink muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_decklink_muxer = {
    .name           = "decklink",
    .long_name      = NULL_IF_CONFIG_SMALL("Blackmagic DeckLink output"),
    .audio_codec    = AV_CODEC_ID_PCM_S16LE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .subtitle_codec = AV_CODEC_ID_NONE,
    .flags          = AVFMT_NOFILE | AVFMT_RAWPICTURE,
    .priv_class     = &decklink_muxer_class,
    .priv_data_size = sizeof(struct decklink_cctx),
    .write_header   = ff_decklink_write_header,
    .write_packet   = ff_decklink_write_packet,
    .write_trailer  = ff_decklink_write_trailer,
};
