/*
 * Animated WebP demuxer
 * Copyright (c) 2020 Pexeso Inc.
 * Copyright (c) 2026 Ramiro Polla
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

/**
 * @file
 * Animated WebP demuxer.
 */

#include "avio_internal.h"
#include "demux.h"
#include "avformat.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#define VP8X_FLAG_ANIMATION             0x02
#define VP8X_FLAG_XMP_METADATA          0x04
#define VP8X_FLAG_EXIF_METADATA         0x08
#define VP8X_FLAG_ALPHA                 0x10
#define VP8X_FLAG_ICC                   0x20

typedef struct WebPAnimDemuxContext {
    const AVClass *class;

    /**
     * Minimum allowed delay between frames in milliseconds.
     * Values below this threshold are considered to be invalid
     * and set to value of default_delay.
     */
    int min_delay;
    int max_delay;
    int default_delay;

    /*
     * loop options
     */
    int ignore_loop;                ///< ignore loop setting
    int loop_count;                 ///< number of times to loop the animation
    int cur_loop;                   ///< current loop counter

    /*
     * variables for the key frame detection
     */
    int cur_frame;                  ///< number of frames of the current animation file
    int vp8x_flags;

    int has_iccp;
    int has_exif;
    int has_anim;
    int has_xmp;

    int usebgcolor;

    int64_t first_anmf_offset;
} WebPAnimDemuxContext;

/**
 * Major web browsers display WebPs at ~10-15fps when rate is not
 * explicitly set or have too low values. We assume default rate to be 10.
 * Default delay = 1000 microseconds / 10fps = 100 milliseconds per frame.
 */
#define WEBP_DEFAULT_DELAY   100
/**
 * By default delay values less than this threshold considered to be invalid.
 */
#define WEBP_MIN_DELAY       10

static int webp_anim_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RL32(b)      == MKTAG('R', 'I', 'F', 'F') &&
        AV_RL32(b +  8) == MKTAG('W', 'E', 'B', 'P') &&
        AV_RL32(b + 12) == MKTAG('V', 'P', '8', 'X') &&
        AV_RL32(b + 16) == 10 &&
        (b[20] & VP8X_FLAG_ANIMATION))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int webp_anim_read_header(AVFormatContext *s)
{
    WebPAnimDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    /* Check for signature. */
    if (avio_rl32(pb) != MKTAG('R', 'I', 'F', 'F'))
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 4); /* file size */
    if (avio_rl32(pb) != MKTAG('W', 'E', 'B', 'P'))
        return AVERROR_INVALIDDATA;

    /* VP8X must be first chunk */
    if (avio_rl32(pb) != MKTAG('V', 'P', '8', 'X') ||
        avio_rl32(pb) != 10 /* chunk size */)
        return AVERROR_INVALIDDATA;
    ctx->vp8x_flags = avio_r8(pb);
    if (!(ctx->vp8x_flags & VP8X_FLAG_ANIMATION))
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 3);

    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->format     = AV_PIX_FMT_ARGB;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_WEBP_ANIM;
    st->codecpar->width      = avio_rl24(pb) + 1;
    st->codecpar->height     = avio_rl24(pb) + 1;
    st->start_time           = 0;

    int explode = (s->error_recognition & AV_EF_EXPLODE);
    int loglevel = explode ? AV_LOG_ERROR : AV_LOG_WARNING;
    while (1) {
        int64_t offset = avio_tell(pb);
        uint32_t fourcc = avio_rl32(pb);
        uint32_t size = avio_rl32(pb);

        av_log(s, AV_LOG_DEBUG, "Chunk %s of size %u at offset %" PRId64 "\n",
               av_fourcc2str(fourcc), size, offset);

        if (size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        size += size & 1;

        if (avio_feof(pb))
            break;

        switch (fourcc) {
        case MKTAG('I', 'C', 'C', 'P'):
            if (ctx->has_iccp) {
                av_log(s, loglevel, "Extra ICCP chunk found\n");
                if (explode)
                    return AVERROR_INVALIDDATA;
                avio_skip(pb, size);
            } else {
                if (!(ctx->vp8x_flags & VP8X_FLAG_ICC)) {
                    av_log(s, loglevel,
                           "ICCP chunk present, but ICC Profile bit not set in the VP8X header\n");
                    if (explode)
                        return AVERROR_INVALIDDATA;
                }

                AVPacketSideData *sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                                               &st->codecpar->nb_coded_side_data,
                                                               AV_PKT_DATA_ICC_PROFILE, size, 0);
                if (!sd)
                    return AVERROR(ENOMEM);
                ret = avio_read(pb, sd->data, size);
                if (ret < 0)
                    return ret;
                ctx->has_iccp = 1;
            }
            break;
        case MKTAG('A', 'N', 'I', 'M'):
            if (ctx->has_anim) {
                av_log(s, loglevel, "Extra ANIM chunk found\n");
                if (explode)
                    return AVERROR_INVALIDDATA;
                avio_skip(pb, size);
            } else {
                if (size != 6)
                    return AVERROR_INVALIDDATA;
                uint32_t bg_color = avio_rb32(pb);
                ctx->loop_count   = avio_rl16(pb);
                if (ctx->usebgcolor) {
                    st->codecpar->extradata = av_mallocz(4 + AV_INPUT_BUFFER_PADDING_SIZE);
                    if (!st->codecpar->extradata)
                        return AVERROR(ENOMEM);
                    AV_WB32(st->codecpar->extradata, bg_color);
                    st->codecpar->extradata_size = 4;
                }
                av_log(s, AV_LOG_DEBUG,
                       "ANIM: background BGRA 0x%08x loop count %d\n",
                       bg_color, ctx->loop_count);
                ctx->has_anim = 1;
            }
            break;
        case MKTAG('A', 'N', 'M', 'F'):
            if (!ctx->has_anim) {
                av_log(s, loglevel,
                       "ANMF chunk present, but no previous ANIM chunk found\n");
                if (explode)
                    return AVERROR_INVALIDDATA;
                ctx->loop_count = 1;
            } else if (!ctx->ignore_loop && ctx->loop_count != 1) {
                int64_t file_size = avio_size(pb);
                if (file_size < 0 || offset < 0 ||
                    (ret = ffio_ensure_seekback(pb, file_size - offset)) < 0) {
                    av_log(s, AV_LOG_WARNING,
                           "Could not ensure seekback, will not loop\n");
                    ctx->loop_count = 1;
                }
            }
            ctx->first_anmf_offset = offset;
            ret = avio_seek(pb, -8, SEEK_CUR);
            if (ret < 0)
                return ret;
            return 0;
        default:
            av_log(s, AV_LOG_WARNING, "Skipping chunk: %s\n", av_fourcc2str(fourcc));
            avio_skip(pb, size);
            break;
        }
    }

    return AVERROR_INVALIDDATA;
}

static int webp_anim_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebPAnimDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    int explode = (s->error_recognition & AV_EF_EXPLODE);
    int loglevel = explode ? AV_LOG_ERROR : AV_LOG_WARNING;
    while (1) {
        int64_t offset = avio_tell(pb);
        uint32_t fourcc = avio_rl32(pb);
        uint32_t size = avio_rl32(pb);

        if (size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        size += size & 1;

        if (avio_feof(pb)) {
            if (!ctx->ignore_loop &&
                (ctx->loop_count == 0 || ++ctx->cur_loop < ctx->loop_count)) {
                ctx->cur_frame = 0;
                ret = avio_seek(pb, ctx->first_anmf_offset, SEEK_SET);
                if (ret < 0)
                    return ret;
                continue;
            }
            break;
        }

        av_log(s, AV_LOG_DEBUG, "Chunk %s of size %u at offset %" PRId64 "\n",
               av_fourcc2str(fourcc), size, offset);

        switch (fourcc) {
        case MKTAG('A', 'N', 'M', 'F'):
            if (size < 16)
                return AVERROR_INVALIDDATA;
            ret = av_get_packet(pb, pkt, size);
            if (ret < 0)
                return ret;
            if (!ctx->cur_frame++)
                pkt->flags |= AV_PKT_FLAG_KEY;
            pkt->pts = AV_NOPTS_VALUE;
            pkt->dts = AV_NOPTS_VALUE;
            uint32_t duration = AV_RL24(pkt->data + 12);
            if (duration <= ctx->min_delay)
                duration = ctx->default_delay;
            pkt->duration = FFMIN(duration, ctx->max_delay);
            return ret;
        case MKTAG('E', 'X', 'I', 'F'):
            if (ctx->has_exif) {
                av_log(s, loglevel, "Extra EXIF chunk found\n");
                if (explode)
                    return AVERROR_INVALIDDATA;
                avio_skip(pb, size);
            } else {
                if (!(ctx->vp8x_flags & VP8X_FLAG_EXIF_METADATA)) {
                    av_log(s, loglevel,
                           "EXIF chunk present, but EXIF bit not set in the VP8X header\n");
                    if (explode)
                        return AVERROR_INVALIDDATA;
                }

                AVStream *st = s->streams[0];
                AVPacketSideData *sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                                               &st->codecpar->nb_coded_side_data,
                                                               AV_PKT_DATA_EXIF, size, 0);
                if (!sd)
                    return AVERROR(ENOMEM);
                ret = avio_read(pb, sd->data, size);
                if (ret < 0)
                    return ret;
                ctx->has_exif = 1;
            }
            break;
        case MKTAG('X', 'M', 'P', ' '):
            if (ctx->has_xmp) {
                av_log(s, loglevel, "Extra XMP chunk found\n");
                if (explode)
                    return AVERROR_INVALIDDATA;
                avio_skip(pb, size);
            } else {
                if (!(ctx->vp8x_flags & VP8X_FLAG_XMP_METADATA)) {
                    av_log(s, loglevel,
                           "XMP chunk present, but XMP bit not set in the VP8X header\n");
                    if (explode)
                        return AVERROR_INVALIDDATA;
                }

                uint8_t *xmp = av_malloc(size + 1);
                if (!xmp)
                    return AVERROR(ENOMEM);
                ret = ffio_read_size(pb, xmp, size);
                if (ret < 0) {
                    av_free(xmp);
                    return ret;
                }
                xmp[size] = '\0';
                av_dict_set(&s->metadata, "xmp", xmp, AV_DICT_DONT_STRDUP_VAL);
                ctx->has_xmp = 1;
            }
            break;
        default:
            av_log(s, AV_LOG_WARNING, "Skipping chunk: %s\n", av_fourcc2str(fourcc));
            avio_skip(pb, size);
            break;
        }
    }

    return AVERROR_EOF;
}

static const AVOption options[] = {
    { "min_delay",      "minimum valid delay between frames (in milliseconds)", offsetof(WebPAnimDemuxContext, min_delay),     AV_OPT_TYPE_INT,  {.i64 = WEBP_MIN_DELAY},     0, 1000 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "max_webp_delay", "maximum valid delay between frames (in milliseconds)", offsetof(WebPAnimDemuxContext, max_delay),     AV_OPT_TYPE_INT,  {.i64 = 0xffffff},           0, 0xffffff,  AV_OPT_FLAG_DECODING_PARAM },
    { "default_delay",  "default delay between frames (in milliseconds)",       offsetof(WebPAnimDemuxContext, default_delay), AV_OPT_TYPE_INT,  {.i64 = WEBP_DEFAULT_DELAY}, 0, 1000 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "ignore_loop",    "ignore loop setting",                                  offsetof(WebPAnimDemuxContext, ignore_loop),   AV_OPT_TYPE_BOOL, {.i64 = 1},                  0, 1,         AV_OPT_FLAG_DECODING_PARAM },
    { "usebgcolor",     "use background color from ANIM chunk",                 offsetof(WebPAnimDemuxContext, usebgcolor),    AV_OPT_TYPE_BOOL, {.i64 = 0},                  0, 1,         AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "Animated WebP demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

const FFInputFormat ff_webp_anim_demuxer = {
    .p.name         = "webp_anim",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Animated WebP"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.priv_class   = &demuxer_class,
    .priv_data_size = sizeof(WebPAnimDemuxContext),
    .read_probe     = webp_anim_probe,
    .read_header    = webp_anim_read_header,
    .read_packet    = webp_anim_read_packet,
};
