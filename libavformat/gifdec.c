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
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "libavcodec/gif.h"

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

    int nb_frames;
    int last_duration;
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
    int i;
    for (i = 0; i < 6; i++) {
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
    int64_t nb_frames = 0, duration = 0;

    if ((ret = resync(pb)) < 0)
        return ret;

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
                    if (delay < gdc->min_delay)
                        delay = gdc->default_delay;
                    delay = FFMIN(delay, gdc->max_delay);
                    duration += delay;
                    avio_skip(pb, 1);
                } else {
                    avio_skip(pb, block_size);
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

    /* GIF format operates with time in "hundredths of second",
     * therefore timebase is 1/100 */
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_GIF;
    st->codecpar->width      = width;
    st->codecpar->height     = height;
    st->start_time           = 0;
    st->duration             = duration;
    st->nb_frames            = nb_frames;
    if (n) {
        st->codecpar->sample_aspect_ratio.num = n + 15;
        st->codecpar->sample_aspect_ratio.den = 64;
    }

    /* jump to start because gif decoder needs header data too */
    if (avio_seek(pb, 0, SEEK_SET) != 0)
        return AVERROR(EIO);

    return 0;
}

static int gif_read_ext(AVFormatContext *s)
{
    GIFDemuxContext *gdc = s->priv_data;
    AVIOContext *pb = s->pb;
    int sb_size, ext_label = avio_r8(pb);
    int ret;

    if (ext_label == GIF_GCE_EXT_LABEL) {
        if ((sb_size = avio_r8(pb)) < 4) {
            av_log(s, AV_LOG_FATAL, "Graphic Control Extension block's size less than 4.\n");
            return AVERROR_INVALIDDATA;
        }

        /* skip packed fields */
        if ((ret = avio_skip(pb, 1)) < 0)
            return ret;

        gdc->delay = avio_rl16(pb);

        if (gdc->delay < gdc->min_delay)
            gdc->delay = gdc->default_delay;
        gdc->delay = FFMIN(gdc->delay, gdc->max_delay);

        /* skip the rest of the Graphic Control Extension block */
        if ((ret = avio_skip(pb, sb_size - 3)) < 0 )
            return ret;
    } else if (ext_label == GIF_APP_EXT_LABEL) {
        uint8_t data[256];

        sb_size = avio_r8(pb);
        ret = avio_read(pb, data, sb_size);
        if (ret < 0 || !sb_size)
            return ret;

        if (sb_size == strlen(NETSCAPE_EXT_STR)) {
            sb_size = avio_r8(pb);
            ret = avio_read(pb, data, sb_size);
            if (ret < 0 || !sb_size)
                return ret;

            if (sb_size == 3 && data[0] == 1) {
                gdc->total_iter = AV_RL16(data+1);

                if (gdc->total_iter == 0)
                    gdc->total_iter = -1;
            }
        }
    }

    if ((ret = gif_skip_subblocks(pb)) < 0)
        return ret;

    return 0;
}

static int gif_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    GIFDemuxContext *gdc = s->priv_data;
    AVIOContext *pb = s->pb;
    int packed_fields, block_label, ct_size,
        keyframe, frame_parsed = 0, ret;
    int64_t frame_start = avio_tell(pb), frame_end;
    unsigned char buf[6];

    if ((ret = avio_read(pb, buf, 6)) == 6) {
        keyframe = memcmp(buf, gif87a_sig, 6) == 0 ||
                   memcmp(buf, gif89a_sig, 6) == 0;
    } else if (ret < 0) {
        return ret;
    } else {
        keyframe = 0;
    }

    if (keyframe) {
parse_keyframe:
        /* skip 2 bytes of width and 2 of height */
        if ((ret = avio_skip(pb, 4)) < 0)
            return ret;

        packed_fields = avio_r8(pb);

        /* skip 1 byte of Background Color Index and 1 byte of Pixel Aspect Ratio */
        if ((ret = avio_skip(pb, 2)) < 0)
            return ret;

        /* global color table presence */
        if (packed_fields & 0x80) {
            ct_size = 3 * (1 << ((packed_fields & 0x07) + 1));

            if ((ret = avio_skip(pb, ct_size)) < 0)
                return ret;
        }
    } else {
        avio_seek(pb, -ret, SEEK_CUR);
        ret = AVERROR_EOF;
    }

    while (GIF_TRAILER != (block_label = avio_r8(pb)) && !avio_feof(pb)) {
        if (block_label == GIF_EXTENSION_INTRODUCER) {
            if ((ret = gif_read_ext (s)) < 0 )
                goto resync;
        } else if (block_label == GIF_IMAGE_SEPARATOR) {
            /* skip to last byte of Image Descriptor header */
            if ((ret = avio_skip(pb, 8)) < 0)
                return ret;

            packed_fields = avio_r8(pb);

            /* local color table presence */
            if (packed_fields & 0x80) {
                ct_size = 3 * (1 << ((packed_fields & 0x07) + 1));

                if ((ret = avio_skip(pb, ct_size)) < 0)
                    return ret;
            }

            /* read LZW Minimum Code Size */
            if (avio_r8(pb) < 1) {
                av_log(s, AV_LOG_ERROR, "lzw minimum code size must be >= 1\n");
                goto resync;
            }

            if ((ret = gif_skip_subblocks(pb)) < 0)
                goto resync;

            frame_end = avio_tell(pb);

            if (avio_seek(pb, frame_start, SEEK_SET) != frame_start)
                return AVERROR(EIO);

            ret = av_get_packet(pb, pkt, frame_end - frame_start);
            if (ret < 0)
                return ret;

            if (keyframe)
                pkt->flags |= AV_PKT_FLAG_KEY;

            pkt->stream_index = 0;
            pkt->duration = gdc->delay;

            gdc->nb_frames ++;
            gdc->last_duration = pkt->duration;

            /* Graphic Control Extension's scope is single frame.
             * Remove its influence. */
            gdc->delay = gdc->default_delay;
            frame_parsed = 1;

            break;
        } else {
            av_log(s, AV_LOG_ERROR, "invalid block label\n");
resync:
            if (!keyframe)
                avio_seek(pb, frame_start, SEEK_SET);
            if ((ret = resync(pb)) < 0)
                return ret;
            frame_start = avio_tell(pb) - 6;
            keyframe = 1;
            goto parse_keyframe;
        }
    }

    if ((ret >= 0 && !frame_parsed) || ret == AVERROR_EOF) {
        if (gdc->nb_frames == 1) {
            s->streams[0]->r_frame_rate = (AVRational) {100, gdc->last_duration};
        }
        /* This might happen when there is no image block
         * between extension blocks and GIF_TRAILER or EOF */
        if (!gdc->ignore_loop && (block_label == GIF_TRAILER || avio_feof(pb))
            && (gdc->total_iter < 0 || ++gdc->iter_count < gdc->total_iter))
            return avio_seek(pb, 0, SEEK_SET);
        return AVERROR_EOF;
    } else
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

AVInputFormat ff_gif_demuxer = {
    .name           = "gif",
    .long_name      = NULL_IF_CONFIG_SMALL("CompuServe Graphics Interchange Format (GIF)"),
    .priv_data_size = sizeof(GIFDemuxContext),
    .read_probe     = gif_probe,
    .read_header    = gif_read_header,
    .read_packet    = gif_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .priv_class     = &demuxer_class,
};
