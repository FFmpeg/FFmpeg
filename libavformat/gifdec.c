/*
 * GIF demuxer
 * Copyright (c) 2012 Vitaliy E Sugrobov
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
 * GIF demuxer.
 */

#include "avformat.h"
#include "demux.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "avio_internal.h"
#include "internal.h"
#include "libavcodec/gif.h"

#define GIF_PACKET_SIZE 1024

typedef struct GIFDemuxContext {
    const AVClass *class;
    /**
     * Time span in hundredths of second before
     * the next frame should be drawn on screen.
     */
    int delay;
    /**
     * Minimum allowed delay between frames in hundredths of
     * second. Values below this threshold considered to be
     * invalid and set to value of default_delay.
     */
    int min_delay;
    int max_delay;
    int default_delay;

    /**
     * loop options
     */
    int total_iter;
    int iter_count;
    int ignore_loop;
} GIFDemuxContext;

/**
 * Major web browsers display gifs at ~10-15fps when rate
 * is not explicitly set or have too low values. We assume default rate to be 10.
 * Default delay = 100hundredths of second / 10fps = 10hos per frame.
 */
#define GIF_DEFAULT_DELAY   10
/**
 * By default delay values less than this threshold considered to be invalid.
 */
#define GIF_MIN_DELAY       2

static int gif_probe(const AVProbeData *p)
{
    /* check magick */
    if (memcmp(p->buf, gif87a_sig, 6) && memcmp(p->buf, gif89a_sig, 6))
        return 0;

    /* width or height contains zero? */
    if (!AV_RL16(&p->buf[6]) || !AV_RL16(&p->buf[8]))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int resync(AVIOContext *pb)
{
    int ret = ffio_ensure_seekback(pb, 13);
    if (ret < 0)
        return ret;

    for (int i = 0; i < 6; i++) {
        int b = avio_r8(pb);
        if (b != gif87a_sig[i] && b != gif89a_sig[i])
            i = -(b != 'G');
        if (avio_feof(pb))
            return AVERROR_EOF;
    }
    return 0;
}

static int gif_skip_subblocks(AVIOContext *pb)
{
    int sb_size, ret = 0;

    while (0x00 != (sb_size = avio_r8(pb))) {
        if ((ret = avio_skip(pb, sb_size)) < 0)
            return ret;
    }

    return ret;
}

static int gif_read_header(AVFormatContext *s)
{
    GIFDemuxContext *gdc = s->priv_data;
    AVIOContext     *pb  = s->pb;
    AVStream        *st;
    int type, width, height, ret, n, flags;
    int64_t nb_frames = 0, duration = 0, pos;

    if ((ret = resync(pb)) < 0)
        return ret;

    pos = avio_tell(pb);
    gdc->delay  = gdc->default_delay;
    width  = avio_rl16(pb);
    height = avio_rl16(pb);
    flags = avio_r8(pb);
    avio_skip(pb, 1);
    n      = avio_r8(pb);

    if (width == 0 || height == 0)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL))
        goto skip;

    if (flags & 0x80)
        avio_skip(pb, 3 * (1 << ((flags & 0x07) + 1)));

    while ((type = avio_r8(pb)) != GIF_TRAILER) {
        if (avio_feof(pb))
            break;
        if (type == GIF_EXTENSION_INTRODUCER) {
            int subtype = avio_r8(pb);
            if (subtype == GIF_COM_EXT_LABEL) {
                AVBPrint bp;
                int block_size;

                av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
                while ((block_size = avio_r8(pb)) != 0) {
                    avio_read_to_bprint(pb, &bp, block_size);
                }
                av_dict_set(&s->metadata, "comment", bp.str, 0);
                av_bprint_finalize(&bp, NULL);
            } else if (subtype == GIF_GCE_EXT_LABEL) {
                int block_size = avio_r8(pb);

                if (block_size == 4) {
                    int delay;

                    avio_skip(pb, 1);
                    delay = avio_rl16(pb);
                    delay = delay ? delay : gdc->default_delay;
                    duration += delay;
                    avio_skip(pb, 1);
                } else {
                    avio_skip(pb, block_size);
                }
                gif_skip_subblocks(pb);
            } else if (subtype == GIF_APP_EXT_LABEL) {
                uint8_t data[256];
                int sb_size;

                sb_size = avio_r8(pb);
                ret = avio_read(pb, data, sb_size);
                if (ret < 0 || !sb_size)
                    break;

                if (sb_size == strlen(NETSCAPE_EXT_STR)) {
                    sb_size = avio_r8(pb);
                    ret = avio_read(pb, data, sb_size);
                    if (ret < 0 || !sb_size)
                        break;

                    if (sb_size == 3 && data[0] == 1) {
                        gdc->total_iter = AV_RL16(data+1);
                        av_log(s, AV_LOG_DEBUG, "Loop count is %d\n", gdc->total_iter);

                        if (gdc->total_iter == 0)
                            gdc->total_iter = -1;
                    }
                }
                gif_skip_subblocks(pb);
            } else {
                gif_skip_subblocks(pb);
            }
        } else if (type == GIF_IMAGE_SEPARATOR) {
            avio_skip(pb, 8);
            flags = avio_r8(pb);
            if (flags & 0x80)
                avio_skip(pb, 3 * (1 << ((flags & 0x07) + 1)));
            avio_skip(pb, 1);
            gif_skip_subblocks(pb);
            nb_frames++;
        } else {
            break;
        }
    }

skip:
    /* jump to start because gif decoder needs header data too */
    if (avio_seek(pb, pos - 6, SEEK_SET) != pos - 6)
        return AVERROR(EIO);

    /* GIF format operates with time in "hundredths of second",
     * therefore timebase is 1/100 */
    avpriv_set_pts_info(st, 64, 1, 100);
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_GIF;
    st->codecpar->width      = width;
    st->codecpar->height     = height;
    if (nb_frames > 1) {
        av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                  100, duration / nb_frames, INT_MAX);
    } else if (duration) {
        st->avg_frame_rate   = (AVRational) { 100, duration };
    }
    st->start_time           = 0;
    st->duration             = duration;
    st->nb_frames            = nb_frames;
    if (n)
        st->codecpar->sample_aspect_ratio = av_make_q(n + 15, 64);

    return 0;
}

static int gif_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    GIFDemuxContext *gdc = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    if ((pb->seekable & AVIO_SEEKABLE_NORMAL) &&
        !gdc->ignore_loop && avio_feof(pb) &&
        (gdc->total_iter < 0 || (++gdc->iter_count < gdc->total_iter))) {
        avio_seek(pb, 0, SEEK_SET);
    }
    if ((ret = av_new_packet(pkt, GIF_PACKET_SIZE)) < 0)
        return ret;

    pkt->pos = avio_tell(pb);
    pkt->stream_index = 0;
    ret = avio_read_partial(pb, pkt->data, GIF_PACKET_SIZE);
    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }
    av_shrink_packet(pkt, ret);
    return ret;
}

static const AVOption options[] = {
    { "min_delay"    , "minimum valid delay between frames (in hundredths of second)", offsetof(GIFDemuxContext, min_delay)    , AV_OPT_TYPE_INT, {.i64 = GIF_MIN_DELAY}    , 0, 100 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "max_gif_delay", "maximum valid delay between frames (in hundredths of seconds)", offsetof(GIFDemuxContext, max_delay)   , AV_OPT_TYPE_INT, {.i64 = 65535}            , 0, 65535   , AV_OPT_FLAG_DECODING_PARAM },
    { "default_delay", "default delay between frames (in hundredths of second)"      , offsetof(GIFDemuxContext, default_delay), AV_OPT_TYPE_INT, {.i64 = GIF_DEFAULT_DELAY}, 0, 100 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "ignore_loop"  , "ignore loop setting (netscape extension)"                    , offsetof(GIFDemuxContext, ignore_loop)  , AV_OPT_TYPE_BOOL,{.i64 = 1}                , 0,        1, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "GIF demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

const FFInputFormat ff_gif_demuxer = {
    .p.name         = "gif",
    .p.long_name    = NULL_IF_CONFIG_SMALL("CompuServe Graphics Interchange Format (GIF)"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "gif",
    .p.priv_class   = &demuxer_class,
    .priv_data_size = sizeof(GIFDemuxContext),
    .read_probe     = gif_probe,
    .read_header    = gif_read_header,
    .read_packet    = gif_read_packet,
};
