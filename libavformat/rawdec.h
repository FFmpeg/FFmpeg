/*
 * RAW demuxers
 * Copyright (C) 2007  Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef AVFORMAT_RAWDEC_H
#define AVFORMAT_RAWDEC_H

#include "avformat.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

typedef struct FFRawVideoDemuxerContext {
    const AVClass *class;     /**< Class for private options. */
    int raw_packet_size;
    char *video_size;         /**< String describing video size, set by a private option. */
    char *pixel_format;       /**< Set by a private option. */
    AVRational framerate;     /**< AVRational describing framerate, set by a private option. */
} FFRawVideoDemuxerContext;

typedef struct FFRawDemuxerContext {
    const AVClass *class;     /**< Class for private options. */
    int raw_packet_size;
} FFRawDemuxerContext;

extern const AVOption ff_rawvideo_options[];
extern const AVOption ff_raw_options[];

int ff_raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt);

int ff_raw_audio_read_header(AVFormatContext *s);

int ff_raw_video_read_header(AVFormatContext *s);

int ff_raw_subtitle_read_header(AVFormatContext *s);

int ff_raw_data_read_header(AVFormatContext *s);

#define FF_RAW_DEMUXER_CLASS(name)\
static const AVClass name ## _demuxer_class = {\
    .class_name = #name " demuxer",\
    .item_name  = av_default_item_name,\
    .option     = ff_raw_options,\
    .version    = LIBAVUTIL_VERSION_INT,\
};

#define FF_RAWVIDEO_DEMUXER_CLASS(name)\
static const AVClass name ## _demuxer_class = {\
    .class_name = #name " demuxer",\
    .item_name  = av_default_item_name,\
    .option     = ff_rawvideo_options,\
    .version    = LIBAVUTIL_VERSION_INT,\
};

#define FF_DEF_RAWVIDEO_DEMUXER2(shortname, longname, probe, ext, id, flag)\
FF_RAWVIDEO_DEMUXER_CLASS(shortname)\
AVInputFormat ff_ ## shortname ## _demuxer = {\
    .name           = #shortname,\
    .long_name      = NULL_IF_CONFIG_SMALL(longname),\
    .read_probe     = probe,\
    .read_header    = ff_raw_video_read_header,\
    .read_packet    = ff_raw_read_partial_packet,\
    .extensions     = ext,\
    .flags          = flag,\
    .raw_codec_id   = id,\
    .priv_data_size = sizeof(FFRawVideoDemuxerContext),\
    .priv_class     = &shortname ## _demuxer_class,\
};

#define FF_DEF_RAWVIDEO_DEMUXER(shortname, longname, probe, ext, id)\
FF_DEF_RAWVIDEO_DEMUXER2(shortname, longname, probe, ext, id, AVFMT_GENERIC_INDEX)

#define FF_RAWSUB_DEMUXER_CLASS(name)\
static const AVClass name ## _demuxer_class = {\
    .class_name = #name " demuxer",\
    .item_name  = av_default_item_name,\
    .option     = ff_raw_options,\
    .version    = LIBAVUTIL_VERSION_INT,\
};

#define FF_DEF_RAWSUB_DEMUXER(shortname, longname, probe, ext, id, flag)\
FF_RAWSUB_DEMUXER_CLASS(shortname)\
AVInputFormat ff_ ## shortname ## _demuxer = {\
    .name           = #shortname,\
    .long_name      = NULL_IF_CONFIG_SMALL(longname),\
    .read_probe     = probe,\
    .read_header    = ff_raw_subtitle_read_header,\
    .read_packet    = ff_raw_read_partial_packet,\
    .extensions     = ext,\
    .flags          = flag,\
    .raw_codec_id   = id,\
    .priv_data_size = sizeof(FFRawDemuxerContext),\
    .priv_class     = &shortname ## _demuxer_class,\
};

#endif /* AVFORMAT_RAWDEC_H */
