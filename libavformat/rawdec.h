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

typedef struct RawAudioDemuxerContext {
    AVClass *class;
    int sample_rate;
    int channels;
} RawAudioDemuxerContext;

typedef struct FFRawVideoDemuxerContext {
    const AVClass *class;     /**< Class for private options. */
    char *video_size;         /**< String describing video size, set by a private option. */
    char *pixel_format;       /**< Set by a private option. */
    char *framerate;          /**< String describing framerate, set by a private option. */
} FFRawVideoDemuxerContext;

extern const AVClass ff_rawaudio_demuxer_class;
extern const AVClass ff_rawvideo_demuxer_class;

int ff_raw_read_header(AVFormatContext *s, AVFormatParameters *ap);

int ff_raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt);

int ff_raw_audio_read_header(AVFormatContext *s, AVFormatParameters *ap);

int ff_raw_video_read_header(AVFormatContext *s, AVFormatParameters *ap);

#define FF_DEF_RAWVIDEO_DEMUXER(shortname, longname, probe, ext, id)\
AVInputFormat ff_ ## shortname ## _demuxer = {\
    .name           = #shortname,\
    .long_name      = NULL_IF_CONFIG_SMALL(longname),\
    .read_probe     = probe,\
    .read_header    = ff_raw_video_read_header,\
    .read_packet    = ff_raw_read_partial_packet,\
    .extensions     = ext,\
    .flags          = AVFMT_GENERIC_INDEX,\
    .value          = id,\
    .priv_data_size = sizeof(FFRawVideoDemuxerContext),\
    .priv_class     = &ff_rawvideo_demuxer_class,\
};

#endif /* AVFORMAT_RAWDEC_H */
