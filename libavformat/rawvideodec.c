/*
 * RAW video demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include "config_components.h"

#include "libavutil/imgutils.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "avformat.h"

typedef struct RawVideoDemuxerContext {
    const AVClass *class;     /**< Class for private options. */
    int width, height;        /**< Integers describing video size, set by a private option. */
    char *pixel_format;       /**< Set by a private option. */
    AVRational framerate;     /**< AVRational describing framerate, set by a private option. */
} RawVideoDemuxerContext;

// v210 frame width is padded to multiples of 48
#define GET_PACKET_SIZE(w, h) (((w + 47) / 48) * 48 * h * 8 / 3)

static int rawvideo_read_header(AVFormatContext *ctx)
{
    RawVideoDemuxerContext *s = ctx->priv_data;
    enum AVPixelFormat pix_fmt;
    AVStream *st;
    int packet_size;
    int ret;

    st = avformat_new_stream(ctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    st->codecpar->codec_id = ctx->iformat->raw_codec_id;

    if ((ctx->iformat->raw_codec_id != AV_CODEC_ID_V210) &&
        (ctx->iformat->raw_codec_id != AV_CODEC_ID_V210X)) {
        if ((pix_fmt = av_get_pix_fmt(s->pixel_format)) == AV_PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "No such pixel format: %s.\n",
                    s->pixel_format);
            return AVERROR(EINVAL);
        }
    }

    avpriv_set_pts_info(st, 64, s->framerate.den, s->framerate.num);

    ret = av_image_check_size(s->width, s->height, 0, ctx);
    if (ret < 0)
        return ret;

    st->codecpar->width  = s->width;
    st->codecpar->height = s->height;

    if (ctx->iformat->raw_codec_id == AV_CODEC_ID_BITPACKED) {
        unsigned int pgroup; /* size of the pixel group in bytes */
        unsigned int xinc;
        const AVPixFmtDescriptor *desc;
        int tag;

        desc = av_pix_fmt_desc_get(pix_fmt);
        st->codecpar->bits_per_coded_sample = av_get_bits_per_pixel(desc);
        if (pix_fmt == AV_PIX_FMT_YUV422P10) {
            tag = MKTAG('U', 'Y', 'V', 'Y');
            pgroup = 5;
            xinc   = 2;
        } else if (pix_fmt == AV_PIX_FMT_UYVY422) {
            tag = MKTAG('U', 'Y', 'V', 'Y');
            pgroup = 4;
            xinc   = 2;
            st->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
        } else {
            av_log(ctx, AV_LOG_ERROR, "unsupported format: %s for bitpacked.\n",
                    s->pixel_format);
            return AVERROR(EINVAL);
        }
        st->codecpar->codec_tag = tag;
        packet_size  = s->width * s->height * pgroup / xinc;
    } else if ((ctx->iformat->raw_codec_id == AV_CODEC_ID_V210) ||
               (ctx->iformat->raw_codec_id == AV_CODEC_ID_V210X)) {
        pix_fmt = ctx->iformat->raw_codec_id == AV_CODEC_ID_V210 ?
                  AV_PIX_FMT_YUV422P10 : AV_PIX_FMT_YUV422P16;

        packet_size = GET_PACKET_SIZE(s->width, s->height);
    } else {
        packet_size = av_image_get_buffer_size(pix_fmt, s->width, s->height, 1);
        if (packet_size < 0)
            return packet_size;
    }
    if (packet_size == 0)
        return AVERROR(EINVAL);

    st->codecpar->format = pix_fmt;
    ctx->packet_size = packet_size;
    st->codecpar->bit_rate = av_rescale_q(ctx->packet_size,
                                       (AVRational){8,1}, st->time_base);

    return 0;
}


static int rawvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = av_get_packet(s->pb, pkt, s->packet_size);
    pkt->pts = pkt->dts = pkt->pos / s->packet_size;

    pkt->stream_index = 0;
    if (ret < 0)
        return ret;
    return 0;
}

#define OFFSET(x) offsetof(RawVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption rawvideo_options[] = {
    /* pixel_format is not used by the v210 demuxers. */
    { "pixel_format", "set pixel format", OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = "yuv420p"}, 0, 0, DEC },
    { "video_size", "set frame size", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, DEC },
    { "framerate", "set frame rate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC },
    { NULL },
};

static const AVClass rawvideo_demuxer_class = {
    .class_name = "rawvideo demuxer",
    .item_name  = av_default_item_name,
    .option     = rawvideo_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVInputFormat ff_rawvideo_demuxer = {
    .name           = "rawvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("raw video"),
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "yuv,cif,qcif,rgb",
    .raw_codec_id   = AV_CODEC_ID_RAWVIDEO,
    .priv_class     = &rawvideo_demuxer_class,
};

static const AVClass bitpacked_demuxer_class = {
    .class_name = "bitpacked demuxer",
    .item_name  = av_default_item_name,
    .option     = rawvideo_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_BITPACKED_DEMUXER
const AVInputFormat ff_bitpacked_demuxer = {
    .name           = "bitpacked",
    .long_name      = NULL_IF_CONFIG_SMALL("Bitpacked"),
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "bitpacked",
    .raw_codec_id   = AV_CODEC_ID_BITPACKED,
    .priv_class     = &bitpacked_demuxer_class,
};
#endif // CONFIG_BITPACKED_DEMUXER

static const AVClass v210_demuxer_class = {
    .class_name = "v210(x) demuxer",
    .item_name  = av_default_item_name,
    .option     = rawvideo_options + 1,
    .version    = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_V210_DEMUXER
const AVInputFormat ff_v210_demuxer = {
    .name           = "v210",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "v210",
    .raw_codec_id   = AV_CODEC_ID_V210,
    .priv_class     = &v210_demuxer_class,
};
#endif // CONFIG_V210_DEMUXER

#if CONFIG_V210X_DEMUXER
const AVInputFormat ff_v210x_demuxer = {
    .name           = "v210x",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "yuv10",
    .raw_codec_id   = AV_CODEC_ID_V210X,
    .priv_class     = &v210_demuxer_class,
};
#endif // CONFIG_V210X_DEMUXER
