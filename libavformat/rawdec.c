/*
 * RAW demuxers
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2005 Alex Beregszaszi
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

#include "avformat.h"
#include "avio_internal.h"
#include "rawdec.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"

/* raw input */
int ff_raw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    enum CodecID id;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

        id = s->iformat->value;
        if (id == CODEC_ID_RAWVIDEO) {
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        } else {
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        }
        st->codec->codec_id = id;

        switch(st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO: {
            RawAudioDemuxerContext *s1 = s->priv_data;

#if FF_API_FORMAT_PARAMETERS
            if (ap->sample_rate)
                st->codec->sample_rate = ap->sample_rate;
            if (ap->channels)
                st->codec->channels    = ap->channels;
            else st->codec->channels   = 1;
#endif

            if (s1->sample_rate)
                st->codec->sample_rate = s1->sample_rate;
            if (s1->channels)
                st->codec->channels    = s1->channels;

            st->codec->bits_per_coded_sample = av_get_bits_per_sample(st->codec->codec_id);
            assert(st->codec->bits_per_coded_sample > 0);
            st->codec->block_align = st->codec->bits_per_coded_sample*st->codec->channels/8;
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            break;
            }
        case AVMEDIA_TYPE_VIDEO: {
            FFRawVideoDemuxerContext *s1 = s->priv_data;
            int width = 0, height = 0, ret = 0;
            enum PixelFormat pix_fmt;
            AVRational framerate;

            if (s1->video_size && (ret = av_parse_video_size(&width, &height, s1->video_size)) < 0) {
                av_log(s, AV_LOG_ERROR, "Couldn't parse video size.\n");
                goto fail;
            }
            if ((pix_fmt = av_get_pix_fmt(s1->pixel_format)) == PIX_FMT_NONE) {
                av_log(s, AV_LOG_ERROR, "No such pixel format: %s.\n", s1->pixel_format);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if ((ret = av_parse_video_rate(&framerate, s1->framerate)) < 0) {
                av_log(s, AV_LOG_ERROR, "Could not parse framerate: %s.\n", s1->framerate);
                goto fail;
            }
#if FF_API_FORMAT_PARAMETERS
            if (ap->width > 0)
                width = ap->width;
            if (ap->height > 0)
                height = ap->height;
            if (ap->pix_fmt)
                pix_fmt = ap->pix_fmt;
            if (ap->time_base.num)
                framerate = (AVRational){ap->time_base.den, ap->time_base.num};
#endif
            av_set_pts_info(st, 64, framerate.den, framerate.num);
            st->codec->width  = width;
            st->codec->height = height;
            st->codec->pix_fmt = pix_fmt;
fail:
            return ret;
            }
        default:
            return -1;
        }
    return 0;
}

#define RAW_PACKET_SIZE 1024

int ff_raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR(ENOMEM);

    pkt->pos= avio_tell(s->pb);
    pkt->stream_index = 0;
    ret = ffio_read_partial(s->pb, pkt->data, size);
    if (ret < 0) {
        av_free_packet(pkt);
        return ret;
    }
    pkt->size = ret;
    return ret;
}

int ff_raw_audio_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    st->start_time = 0;
    /* the parameters will be extracted from the compressed bitstream */

    return 0;
}

/* MPEG-1/H.263 input */
int ff_raw_video_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st;
    FFRawVideoDemuxerContext *s1 = s->priv_data;
    AVRational framerate;
    int ret = 0;


    st = av_new_stream(s, 0);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;

    if ((ret = av_parse_video_rate(&framerate, s1->framerate)) < 0) {
        av_log(s, AV_LOG_ERROR, "Could not parse framerate: %s.\n", s1->framerate);
        goto fail;
    }
#if FF_API_FORMAT_PARAMETERS
    if (ap->time_base.num)
        framerate = (AVRational){ap->time_base.den, ap->time_base.num};
#endif

    st->codec->time_base = (AVRational){framerate.den, framerate.num};
    av_set_pts_info(st, 64, 1, 1200000);

fail:
    return ret;
}

/* Note: Do not forget to add new entries to the Makefile as well. */

static const AVOption audio_options[] = {
    { "sample_rate", "", offsetof(RawAudioDemuxerContext, sample_rate), FF_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(RawAudioDemuxerContext, channels),    FF_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

const AVClass ff_rawaudio_demuxer_class = {
    .class_name     = "rawaudio demuxer",
    .item_name      = av_default_item_name,
    .option         = audio_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

#define OFFSET(x) offsetof(FFRawVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption video_options[] = {
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(video_size), FF_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "pixel_format", "", OFFSET(pixel_format), FF_OPT_TYPE_STRING, {.str = "yuv420p"}, 0, 0, DEC },
    { "framerate", "", OFFSET(framerate), FF_OPT_TYPE_STRING, {.str = "25"}, 0, 0, DEC },
    { NULL },
};
#undef OFFSET
#undef DEC

const AVClass ff_rawvideo_demuxer_class = {
    .class_name     = "rawvideo demuxer",
    .item_name      = av_default_item_name,
    .option         = video_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_G722_DEMUXER
AVInputFormat ff_g722_demuxer = {
    "g722",
    NULL_IF_CONFIG_SMALL("raw G.722"),
    sizeof(RawAudioDemuxerContext),
    NULL,
    ff_raw_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "g722,722",
    .value = CODEC_ID_ADPCM_G722,
    .priv_class = &ff_rawaudio_demuxer_class,
};
#endif

#if CONFIG_GSM_DEMUXER
AVInputFormat ff_gsm_demuxer = {
    "gsm",
    NULL_IF_CONFIG_SMALL("raw GSM"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "gsm",
    .value = CODEC_ID_GSM,
};
#endif

#if CONFIG_MJPEG_DEMUXER
FF_DEF_RAWVIDEO_DEMUXER(mjpeg, "raw MJPEG video", NULL, "mjpg,mjpeg", CODEC_ID_MJPEG)
#endif

#if CONFIG_MLP_DEMUXER
AVInputFormat ff_mlp_demuxer = {
    "mlp",
    NULL_IF_CONFIG_SMALL("raw MLP"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mlp",
    .value = CODEC_ID_MLP,
};
#endif

#if CONFIG_TRUEHD_DEMUXER
AVInputFormat ff_truehd_demuxer = {
    "truehd",
    NULL_IF_CONFIG_SMALL("raw TrueHD"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "thd",
    .value = CODEC_ID_TRUEHD,
};
#endif

#if CONFIG_SHORTEN_DEMUXER
AVInputFormat ff_shorten_demuxer = {
    "shn",
    NULL_IF_CONFIG_SMALL("raw Shorten"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "shn",
    .value = CODEC_ID_SHORTEN,
};
#endif

#if CONFIG_VC1_DEMUXER
FF_DEF_RAWVIDEO_DEMUXER(vc1, "raw VC-1", NULL, "vc1", CODEC_ID_VC1)
#endif
