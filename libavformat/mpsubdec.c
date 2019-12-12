/*
 * Copyright (c) 2012 Clément Bœsch
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
 * MPlayer subtitles format demuxer
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"

#define TSBASE 10000000

typedef struct {
    FFDemuxSubtitlesQueue q;
} MPSubContext;

static int mpsub_probe(const AVProbeData *p)
{
    const char *ptr     = p->buf;
    const char *ptr_end = p->buf + p->buf_size;

    while (ptr < ptr_end) {
        int inc;

        if (!memcmp(ptr, "FORMAT=TIME", 11))
            return AVPROBE_SCORE_EXTENSION;
        if (!memcmp(ptr, "FORMAT=", 7))
            return AVPROBE_SCORE_EXTENSION / 3;
        inc = ff_subtitles_next_line(ptr);
        if (!inc)
            break;
        ptr += inc;
    }
    return 0;
}

static int parse_line(const char *line, int64_t *value, int64_t *value2)
{
    int vi, p1, p2;

    for (vi = 0; vi < 2; vi++) {
        long long intval, fracval;
        int n = av_sscanf(line, "%lld%n.%lld%n", &intval, &p1, &fracval, &p2);
        if (n <= 0 || intval < INT64_MIN / TSBASE || intval > INT64_MAX / TSBASE)
            return AVERROR_INVALIDDATA;

        intval *= TSBASE;

        if (n == 2) {
            if (fracval < 0)
                return AVERROR_INVALIDDATA;
            for (;p2 - p1 < 7 + 1; p1--)
                fracval *= 10;
            for (;p2 - p1 > 7 + 1; p1++)
                fracval /= 10;
            if (intval > 0) intval += fracval;
            else            intval -= fracval;
            line += p2;
        } else
            line += p1;

        *value = intval;

        value = value2;
    }

    return 0;
}

static int mpsub_read_header(AVFormatContext *s)
{
    MPSubContext *mpsub = s->priv_data;
    AVStream *st;
    AVBPrint buf;
    AVRational pts_info = (AVRational){ TSBASE, 1 }; // ts based by default
    int res = 0;
    int64_t current_pts = 0;
    int i;
    int common_factor = 0;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!avio_feof(s->pb)) {
        char line[1024];
        int64_t start, duration;
        int fps, len = ff_get_line(s->pb, line, sizeof(line));

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;

        if (sscanf(line, "FORMAT=%d", &fps) == 1 && fps > 3 && fps < 100) {
            /* frame based timing */
            pts_info = (AVRational){ TSBASE * fps, 1 };
        } else if (parse_line(line, &start, &duration) >= 0) {
            AVPacket *sub;
            const int64_t pos = avio_tell(s->pb);

            ff_subtitles_read_chunk(s->pb, &buf);
            if (buf.len) {
                sub = ff_subtitles_queue_insert(&mpsub->q, buf.str, buf.len, 0);
                if (!sub) {
                    res = AVERROR(ENOMEM);
                    goto end;
                }
                if (   current_pts < 0 && start < INT64_MIN - current_pts
                    || current_pts > 0 && start > INT64_MAX - current_pts) {
                    res = AVERROR_INVALIDDATA;
                    goto end;
                }
                sub->pts = current_pts + start;
                if (duration < 0 || sub->pts > INT64_MAX - duration) {
                    res = AVERROR_INVALIDDATA;
                    goto end;
                }
                sub->duration = duration;

                common_factor = av_gcd(duration, common_factor);
                common_factor = av_gcd(sub->pts, common_factor);

                current_pts = sub->pts + duration;
                sub->pos = pos;
            }
        }
    }

    if (common_factor > 1) {
        common_factor = av_gcd(pts_info.num, common_factor);
        for (i = 0; i < mpsub->q.nb_subs; i++) {
            mpsub->q.subs[i].pts      /= common_factor;
            mpsub->q.subs[i].duration /= common_factor;
        }
        pts_info.num /= common_factor;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, pts_info.den, pts_info.num);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_TEXT;

    ff_subtitles_queue_finalize(s, &mpsub->q);

end:
    if (res < 0)
        ff_subtitles_queue_clean(&mpsub->q);

    av_bprint_finalize(&buf, NULL);
    return res;
}

static int mpsub_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MPSubContext *mpsub = s->priv_data;
    return ff_subtitles_queue_read_packet(&mpsub->q, pkt);
}

static int mpsub_read_seek(AVFormatContext *s, int stream_index,
                           int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    MPSubContext *mpsub = s->priv_data;
    return ff_subtitles_queue_seek(&mpsub->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int mpsub_read_close(AVFormatContext *s)
{
    MPSubContext *mpsub = s->priv_data;
    ff_subtitles_queue_clean(&mpsub->q);
    return 0;
}

AVInputFormat ff_mpsub_demuxer = {
    .name           = "mpsub",
    .long_name      = NULL_IF_CONFIG_SMALL("MPlayer subtitles"),
    .priv_data_size = sizeof(MPSubContext),
    .read_probe     = mpsub_probe,
    .read_header    = mpsub_read_header,
    .read_packet    = mpsub_read_packet,
    .read_seek2     = mpsub_read_seek,
    .read_close     = mpsub_read_close,
    .extensions     = "sub",
};
