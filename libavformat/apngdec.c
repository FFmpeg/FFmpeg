/*
 * APNG demuxer
 * Copyright (c) 2014 Benoit Fouet
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
 * APNG demuxer.
 * @see https://wiki.mozilla.org/APNG_Specification
 * @see http://www.w3.org/TR/PNG
 */

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavcodec/apng.h"
#include "libavcodec/png.h"
#include "libavcodec/bytestream.h"

#define DEFAULT_APNG_FPS 15

typedef struct APNGDemuxContext {
    const AVClass *class;

    int max_fps;
    int default_fps;

    int pkt_duration;

    int is_key_frame;

    /*
     * loop options
     */
    int ignore_loop;
    uint32_t num_frames;
    uint32_t num_play;
    uint32_t cur_loop;
} APNGDemuxContext;

/*
 * To be a valid APNG file, we mandate, in this order:
 *     PNGSIG
 *     IHDR
 *     ...
 *     acTL
 *     ...
 *     IDAT
 */
static int apng_probe(const AVProbeData *p)
{
    GetByteContext gb;
    int state = 0;
    uint32_t len, tag;

    bytestream2_init(&gb, p->buf, p->buf_size);

    if (bytestream2_get_be64(&gb) != PNGSIG)
        return 0;

    for (;;) {
        len = bytestream2_get_be32(&gb);
        if (len > 0x7fffffff)
            return 0;

        tag = bytestream2_get_le32(&gb);
        /* we don't check IDAT size, as this is the last tag
         * we check, and it may be larger than the probe buffer */
        if (tag != MKTAG('I', 'D', 'A', 'T') &&
            len + 4 > bytestream2_get_bytes_left(&gb))
            return 0;

        switch (tag) {
        case MKTAG('I', 'H', 'D', 'R'):
            if (len != 13)
                return 0;
            if (av_image_check_size(bytestream2_get_be32(&gb), bytestream2_get_be32(&gb), 0, NULL))
                return 0;
            bytestream2_skip(&gb, 9);
            state++;
            break;
        case MKTAG('a', 'c', 'T', 'L'):
            if (state != 1 ||
                len != 8 ||
                bytestream2_get_be32(&gb) == 0) /* 0 is not a valid value for number of frames */
                return 0;
            bytestream2_skip(&gb, 8);
            state++;
            break;
        case MKTAG('I', 'D', 'A', 'T'):
            if (state != 2)
                return 0;
            goto end;
        default:
            /* skip other tags */
            bytestream2_skip(&gb, len + 4);
            break;
        }
    }

end:
    return AVPROBE_SCORE_MAX;
}

static int append_extradata(AVCodecParameters *par, AVIOContext *pb, int len)
{
    int previous_size = par->extradata_size;
    int new_size, ret;
    uint8_t *new_extradata;

    if (previous_size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE - len)
        return AVERROR_INVALIDDATA;

    new_size = previous_size + len;
    new_extradata = av_realloc(par->extradata, new_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!new_extradata)
        return AVERROR(ENOMEM);
    memset(new_extradata + new_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    par->extradata = new_extradata;
    par->extradata_size = new_size;

    if ((ret = avio_read(pb, par->extradata + previous_size, len)) < 0)
        return ret;

    return previous_size;
}

static int apng_read_header(AVFormatContext *s)
{
    APNGDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    uint32_t len, tag;
    AVStream *st;
    int acTL_found = 0;
    int64_t ret = AVERROR_INVALIDDATA;

    /* verify PNGSIG */
    if (avio_rb64(pb) != PNGSIG)
        return ret;

    /* parse IHDR (must be first chunk) */
    len = avio_rb32(pb);
    tag = avio_rl32(pb);
    if (len != 13 || tag != MKTAG('I', 'H', 'D', 'R'))
        return ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    /* set the timebase to something large enough (1/100,000 of second)
     * to hopefully cope with all sane frame durations */
    avpriv_set_pts_info(st, 64, 1, 100000);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_APNG;
    st->codecpar->width      = avio_rb32(pb);
    st->codecpar->height     = avio_rb32(pb);
    if ((ret = av_image_check_size(st->codecpar->width, st->codecpar->height, 0, s)) < 0)
        return ret;

    /* extradata will contain every chunk up to the first fcTL (excluded) */
    ret = ff_alloc_extradata(st->codecpar, len + 12);
    if (ret < 0)
        return ret;
    AV_WB32(st->codecpar->extradata,    len);
    AV_WL32(st->codecpar->extradata+4,  tag);
    AV_WB32(st->codecpar->extradata+8,  st->codecpar->width);
    AV_WB32(st->codecpar->extradata+12, st->codecpar->height);
    if ((ret = avio_read(pb, st->codecpar->extradata+16, 9)) < 0)
        goto fail;

    while (!avio_feof(pb)) {
        if (acTL_found && ctx->num_play != 1) {
            int64_t size   = avio_size(pb);
            int64_t offset = avio_tell(pb);
            if (size < 0) {
                ret = size;
                goto fail;
            } else if (offset < 0) {
                ret = offset;
                goto fail;
            } else if ((ret = ffio_ensure_seekback(pb, size - offset)) < 0) {
                av_log(s, AV_LOG_WARNING, "Could not ensure seekback, will not loop\n");
                ctx->num_play = 1;
            }
        }
        if ((ctx->num_play == 1 || !acTL_found) &&
            ((ret = ffio_ensure_seekback(pb, 4 /* len */ + 4 /* tag */)) < 0))
            goto fail;

        len = avio_rb32(pb);
        if (len > 0x7fffffff) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        tag = avio_rl32(pb);
        switch (tag) {
        case MKTAG('a', 'c', 'T', 'L'):
            if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0 ||
                (ret = append_extradata(st->codecpar, pb, len + 12)) < 0)
                goto fail;
            acTL_found = 1;
            ctx->num_frames = AV_RB32(st->codecpar->extradata + ret + 8);
            ctx->num_play   = AV_RB32(st->codecpar->extradata + ret + 12);
            av_log(s, AV_LOG_DEBUG, "num_frames: %"PRIu32", num_play: %"PRIu32"\n",
                                    ctx->num_frames, ctx->num_play);
            break;
        case MKTAG('f', 'c', 'T', 'L'):
            if (!acTL_found) {
               ret = AVERROR_INVALIDDATA;
               goto fail;
            }
            if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0)
                goto fail;
            return 0;
        default:
            if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0 ||
                (ret = append_extradata(st->codecpar, pb, len + 12)) < 0)
                goto fail;
        }
    }

fail:
    return ret;
}

static int decode_fctl_chunk(AVFormatContext *s, APNGDemuxContext *ctx, AVPacket *pkt)
{
    uint32_t sequence_number, width, height, x_offset, y_offset;
    uint16_t delay_num, delay_den;
    uint8_t dispose_op, blend_op;

    sequence_number = avio_rb32(s->pb);
    width           = avio_rb32(s->pb);
    height          = avio_rb32(s->pb);
    x_offset        = avio_rb32(s->pb);
    y_offset        = avio_rb32(s->pb);
    delay_num       = avio_rb16(s->pb);
    delay_den       = avio_rb16(s->pb);
    dispose_op      = avio_r8(s->pb);
    blend_op        = avio_r8(s->pb);
    avio_skip(s->pb, 4); /* crc */

    /* default is hundredths of seconds */
    if (!delay_den)
        delay_den = 100;
    if (!delay_num || (ctx->max_fps && delay_den / delay_num > ctx->max_fps)) {
        delay_num = 1;
        delay_den = ctx->default_fps;
    }
    ctx->pkt_duration = av_rescale_q(delay_num,
                                     (AVRational){ 1, delay_den },
                                     s->streams[0]->time_base);

    av_log(s, AV_LOG_DEBUG, "%s: "
            "sequence_number: %"PRId32", "
            "width: %"PRIu32", "
            "height: %"PRIu32", "
            "x_offset: %"PRIu32", "
            "y_offset: %"PRIu32", "
            "delay_num: %"PRIu16", "
            "delay_den: %"PRIu16", "
            "dispose_op: %d, "
            "blend_op: %d\n",
            __FUNCTION__,
            sequence_number,
            width,
            height,
            x_offset,
            y_offset,
            delay_num,
            delay_den,
            dispose_op,
            blend_op);

    if (width != s->streams[0]->codecpar->width ||
        height != s->streams[0]->codecpar->height ||
        x_offset != 0 ||
        y_offset != 0) {
        if (sequence_number == 0 ||
            x_offset >= s->streams[0]->codecpar->width ||
            width > s->streams[0]->codecpar->width - x_offset ||
            y_offset >= s->streams[0]->codecpar->height ||
            height > s->streams[0]->codecpar->height - y_offset)
            return AVERROR_INVALIDDATA;
        ctx->is_key_frame = 0;
    } else {
        if (sequence_number == 0 && dispose_op == APNG_DISPOSE_OP_PREVIOUS)
            dispose_op = APNG_DISPOSE_OP_BACKGROUND;
        ctx->is_key_frame = dispose_op == APNG_DISPOSE_OP_BACKGROUND ||
                            blend_op   == APNG_BLEND_OP_SOURCE;
    }

    return 0;
}

static int apng_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    APNGDemuxContext *ctx = s->priv_data;
    int64_t ret;
    int64_t size;
    AVIOContext *pb = s->pb;
    uint32_t len, tag;

    /*
     * fcTL chunk length, in bytes:
     *  4 (length)
     *  4 (tag)
     * 26 (actual chunk)
     *  4 (crc) bytes
     * and needed next:
     *  4 (length)
     *  4 (tag (must be fdAT or IDAT))
     */
    /* if num_play is not 1, then the seekback is already guaranteed */
    if (ctx->num_play == 1 && (ret = ffio_ensure_seekback(pb, 46)) < 0)
        return ret;

    len = avio_rb32(pb);
    tag = avio_rl32(pb);

    if (avio_feof(pb))
        return AVERROR_EOF;

    switch (tag) {
    case MKTAG('f', 'c', 'T', 'L'):
        if (len != 26)
            return AVERROR_INVALIDDATA;

        if ((ret = decode_fctl_chunk(s, ctx, pkt)) < 0)
            return ret;

        /* fcTL must precede fdAT or IDAT */
        len = avio_rb32(pb);
        tag = avio_rl32(pb);
        if (len > 0x7fffffff ||
            tag != MKTAG('f', 'd', 'A', 'T') &&
            tag != MKTAG('I', 'D', 'A', 'T'))
            return AVERROR_INVALIDDATA;

        size = 38 /* fcTL */ + 8 /* len, tag */ + len + 4 /* crc */;
        if (size > INT_MAX)
            return AVERROR(EINVAL);

        if ((ret = avio_seek(pb, -46, SEEK_CUR)) < 0 ||
            (ret = av_append_packet(pb, pkt, size)) < 0)
            return ret;

        if (ctx->num_play == 1 && (ret = ffio_ensure_seekback(pb, 8)) < 0)
            return ret;

        len = avio_rb32(pb);
        tag = avio_rl32(pb);
        while (tag &&
               tag != MKTAG('f', 'c', 'T', 'L') &&
               tag != MKTAG('I', 'E', 'N', 'D')) {
            if (len > 0x7fffffff)
                return AVERROR_INVALIDDATA;
            if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0 ||
                (ret = av_append_packet(pb, pkt, len + 12)) < 0)
                return ret;
            if (ctx->num_play == 1 && (ret = ffio_ensure_seekback(pb, 8)) < 0)
                return ret;
            len = avio_rb32(pb);
            tag = avio_rl32(pb);
        }
        if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0)
            return ret;

        if (ctx->is_key_frame)
            pkt->flags |= AV_PKT_FLAG_KEY;
        pkt->pts = pkt->dts = AV_NOPTS_VALUE;
        pkt->duration = ctx->pkt_duration;
        return ret;
    case MKTAG('I', 'E', 'N', 'D'):
        ctx->cur_loop++;
        if (ctx->ignore_loop || ctx->num_play >= 1 && ctx->cur_loop == ctx->num_play) {
            avio_seek(pb, -8, SEEK_CUR);
            return AVERROR_EOF;
        }
        if ((ret = avio_seek(pb, s->streams[0]->codecpar->extradata_size + 8, SEEK_SET)) < 0)
            return ret;
        return 0;
    default:
        avpriv_request_sample(s, "In-stream tag=%s (0x%08"PRIX32") len=%"PRIu32,
                              av_fourcc2str(tag), tag, len);
        avio_skip(pb, len + 4);
    }

    /* Handle the unsupported yet cases */
    return AVERROR_PATCHWELCOME;
}

static const AVOption options[] = {
    { "ignore_loop", "ignore loop setting"                         , offsetof(APNGDemuxContext, ignore_loop),
      AV_OPT_TYPE_BOOL, { .i64 = 1 }              , 0, 1      , AV_OPT_FLAG_DECODING_PARAM },
    { "max_fps"    , "maximum framerate (0 is no limit)"           , offsetof(APNGDemuxContext, max_fps),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "default_fps", "default framerate (0 is as fast as possible)", offsetof(APNGDemuxContext, default_fps),
      AV_OPT_TYPE_INT, { .i64 = DEFAULT_APNG_FPS }, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "APNG demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

AVInputFormat ff_apng_demuxer = {
    .name           = "apng",
    .long_name      = NULL_IF_CONFIG_SMALL("Animated Portable Network Graphics"),
    .priv_data_size = sizeof(APNGDemuxContext),
    .read_probe     = apng_probe,
    .read_header    = apng_read_header,
    .read_packet    = apng_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .priv_class     = &demuxer_class,
};
