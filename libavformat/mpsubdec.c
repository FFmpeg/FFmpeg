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

typedef struct {
    FFDemuxSubtitlesQueue q;
} MPSubContext;

static int mpsub_probe(AVProbeData *p)
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

static int mpsub_read_header(AVFormatContext *s)
{
    MPSubContext *mpsub = s->priv_data;
    AVStream *st;
    AVBPrint buf;
    AVRational pts_info = (AVRational){ 100, 1 }; // ts based by default
    int res = 0;
    float multiplier = 100.0;
    float current_pts = 0;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!avio_feof(s->pb)) {
        char line[1024];
        float start, duration;
        int fps, len = ff_get_line(s->pb, line, sizeof(line));

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;

        if (sscanf(line, "FORMAT=%d", &fps) == 1 && fps > 3 && fps < 100) {
            /* frame based timing */
            pts_info = (AVRational){ fps, 1 };
            multiplier = 1.0;
        } else if (sscanf(line, "%f %f", &start, &duration) == 2) {
            AVPacket *sub;
            const int64_t pos = avio_tell(s->pb);

            ff_subtitles_read_chunk(s->pb, &buf);
            if (buf.len) {
                sub = ff_subtitles_queue_insert(&mpsub->q, buf.str, buf.len, 0);
                if (!sub) {
                    res = AVERROR(ENOMEM);
                    goto end;
                }
                sub->pts = (int64_t)(current_pts + start*multiplier);
                sub->duration = (int)(duration * multiplier);
                current_pts += (start + duration) * multiplier;
                sub->pos = pos;
            }
        }
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, pts_info.den, pts_info.num);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_TEXT;

    ff_subtitles_queue_finalize(&mpsub->q);

end:
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
