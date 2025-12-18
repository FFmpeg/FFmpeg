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

#include <stdbool.h>

#include "libavutil/imgutils.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "demux.h"
#include "internal.h"
#include "avformat.h"

typedef struct RawVideoDemuxerContext {
    const AVClass *class;     /**< Class for private options. */
    int width, height;        /**< Integers describing video size, set by a private option. */
    enum AVPixelFormat pix_fmt;
    AVRational framerate;     /**< AVRational describing framerate, set by a private option. */

    bool has_padding;
    /* We could derive linesize[1 to N] from linesize[0] for multiplane formats,
     * but having users explicitly specify linesize for each plane can reduce
     * unexpected results and support more use cases.
     */
    int *linesize;
    unsigned nb_linesize;
    // with padding
    size_t frame_size;
    // linesize without padding
    int raw_bytes[4];
} RawVideoDemuxerContext;

// v210 frame width is padded to multiples of 48
#define GET_PACKET_SIZE(w, h) (((w + 47) / 48) * 48 * h * 8 / 3)

static int rawvideo_read_header(AVFormatContext *ctx)
{
    RawVideoDemuxerContext *s = ctx->priv_data;
    enum AVPixelFormat pix_fmt = s->pix_fmt;
    AVStream *st;
    int packet_size;
    int ret;

    st = avformat_new_stream(ctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    st->codecpar->codec_id = ffifmt(ctx->iformat)->raw_codec_id;
    avpriv_set_pts_info(st, 64, s->framerate.den, s->framerate.num);

    ret = av_image_check_size(s->width, s->height, 0, ctx);
    if (ret < 0)
        return ret;

    st->codecpar->width  = s->width;
    st->codecpar->height = s->height;

    if (s->nb_linesize) {
        int n = av_pix_fmt_count_planes(pix_fmt);
        if (s->nb_linesize != n) {
            av_log(ctx, AV_LOG_ERROR, "Invalid number of stride %u, "
                   "pixel format has %d plane\n",
                   s->nb_linesize, n);
            return AVERROR(EINVAL);
        }

        ret = av_image_fill_linesizes(s->raw_bytes, pix_fmt, s->width);
        if (ret < 0)
            return ret;

        size_t linesize[4] = {0};
        for (int i = 0; i < n; i++) {
            if (s->linesize[i] < s->raw_bytes[i]) {
                av_log(ctx, AV_LOG_ERROR, "Invalid stride %u of plane %d, "
                       "minimum required size is %d for width %d\n",
                       s->linesize[i], i, s->raw_bytes[i], s->width);
                return AVERROR(EINVAL);
            }
            if (s->linesize[i] > s->raw_bytes[i])
                s->has_padding = true;
            linesize[i] = s->linesize[i];
        }

        size_t plane_size[4] = {0};
        av_image_fill_plane_sizes(plane_size, pix_fmt, s->height, linesize);
        s->frame_size = plane_size[0] + plane_size[1] + plane_size[2] + plane_size[3];
    }

    if (ffifmt(ctx->iformat)->raw_codec_id == AV_CODEC_ID_BITPACKED) {
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
                    desc->name);
            return AVERROR(EINVAL);
        }
        st->codecpar->codec_tag = tag;
        packet_size  = s->width * s->height * pgroup / xinc;
    } else if ((ffifmt(ctx->iformat)->raw_codec_id == AV_CODEC_ID_V210) ||
               (ffifmt(ctx->iformat)->raw_codec_id == AV_CODEC_ID_V210X)) {
        pix_fmt = ffifmt(ctx->iformat)->raw_codec_id == AV_CODEC_ID_V210 ?
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


static int rawvideo_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    int ret;
    RawVideoDemuxerContext *s = ctx->priv_data;

    if (!s->has_padding) {
        ret = av_get_packet(ctx->pb, pkt, ctx->packet_size);
        if (ret < 0)
            return ret;
        pkt->pts = pkt->dts = pkt->pos / ctx->packet_size;

        return 0;
    }

    ret = av_new_packet(pkt, ctx->packet_size);
    if (ret < 0)
        return ret;

    pkt->pos = avio_tell(ctx->pb);
    pkt->pts = pkt->dts = pkt->pos / s->frame_size;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->pix_fmt);
    uint8_t *p = pkt->data;
    for (int i = 0; i < s->nb_linesize; i++) {
        int shift = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        int h = AV_CEIL_RSHIFT(s->height, shift);
        int skip_bytes = s->linesize[i] - s->raw_bytes[i];

        for (int j = 0; j < h; j++) {
            ret = avio_read(ctx->pb, p, s->raw_bytes[i]);
            if (ret != s->raw_bytes[i]) {
                if (ret < 0 && ret != AVERROR_EOF)
                    return ret;

                if (ret == AVERROR_EOF && p == pkt->data)
                    return AVERROR_EOF;

                memset(p, 0, pkt->size - (p - pkt->data));
                pkt->flags |= AV_PKT_FLAG_CORRUPT;

                return 0;
            }

            p += s->raw_bytes[i];
            avio_skip(ctx->pb, skip_bytes);
        }
    }

    return 0;
}

#define OFFSET(x) offsetof(RawVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption rawvideo_options[] = {
    // Only supported by rawvideo demuxer
    {"stride", "frame line size in bytes", OFFSET(linesize), AV_OPT_TYPE_INT | AV_OPT_TYPE_FLAG_ARRAY, {.arr = NULL}, 0, INT_MAX, DEC },
#define BITPACKED_OPTION_OFFSET 1   // skip stride option
    /* pixel_format is not used by the v210 demuxers. */
    { "pixel_format", "set pixel format", OFFSET(pix_fmt), AV_OPT_TYPE_PIXEL_FMT, {.i64 = AV_PIX_FMT_YUV420P}, AV_PIX_FMT_YUV420P, INT_MAX, DEC },
#define V210_OPTION_OFFSET 2       // skip stride and pixel_format option
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

const FFInputFormat ff_rawvideo_demuxer = {
    .p.name         = "rawvideo",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw video"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "yuv,cif,qcif,rgb",
    .p.priv_class   = &rawvideo_demuxer_class,
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .raw_codec_id   = AV_CODEC_ID_RAWVIDEO,
};

static const AVClass bitpacked_demuxer_class = {
    .class_name = "bitpacked demuxer",
    .item_name  = av_default_item_name,
    .option     = &rawvideo_options[BITPACKED_OPTION_OFFSET],
    .version    = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_BITPACKED_DEMUXER
const FFInputFormat ff_bitpacked_demuxer = {
    .p.name         = "bitpacked",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Bitpacked"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "bitpacked",
    .p.priv_class   = &bitpacked_demuxer_class,
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .raw_codec_id   = AV_CODEC_ID_BITPACKED,
};
#endif // CONFIG_BITPACKED_DEMUXER

static const AVClass v210_demuxer_class = {
    .class_name = "v210(x) demuxer",
    .item_name  = av_default_item_name,
    .option     = &rawvideo_options[V210_OPTION_OFFSET],
    .version    = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_V210_DEMUXER
const FFInputFormat ff_v210_demuxer = {
    .p.name         = "v210",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "v210",
    .p.priv_class   = &v210_demuxer_class,
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .raw_codec_id   = AV_CODEC_ID_V210,
};
#endif // CONFIG_V210_DEMUXER

#if CONFIG_V210X_DEMUXER
const FFInputFormat ff_v210x_demuxer = {
    .p.name         = "v210x",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "yuv10",
    .p.priv_class   = &v210_demuxer_class,
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .raw_codec_id   = AV_CODEC_ID_V210X,
};
#endif // CONFIG_V210X_DEMUXER
