/*
 * SubRip subtitle demuxer
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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
#include "internal.h"
#include "subtitles.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} SRTContext;

static int srt_probe(AVProbeData *p)
{
    int v;
    char buf[64], *pbuf;
    FFTextReader tr;

    ff_text_init_buf(&tr, p->buf, p->buf_size);

    while (ff_text_peek_r8(&tr) == '\r' || ff_text_peek_r8(&tr) == '\n')
        ff_text_r8(&tr);

    /* Check if the first non-empty line is a number. We do not check what the
     * number is because in practice it can be anything.
     * Also, that number can be followed by random garbage, so we can not
     * unfortunately check that we only have a number. */
    if (ff_subtitles_read_line(&tr, buf, sizeof(buf)) < 0 ||
        strtol(buf, &pbuf, 10) < 0 || pbuf == buf)
        return 0;

    /* Check if the next line matches a SRT timestamp */
    if (ff_subtitles_read_line(&tr, buf, sizeof(buf)) < 0)
        return 0;
    if (buf[0] >= '0' && buf[0] <= '9' && strstr(buf, " --> ")
        && sscanf(buf, "%*d:%*2d:%*2d%*1[,.]%*3d --> %*d:%*2d:%*2d%*1[,.]%3d", &v) == 1)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int64_t get_pts(const char **buf, int *duration,
                       int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2)
{
    int i;

    for (i=0; i<2; i++) {
        int hh1, mm1, ss1, ms1;
        int hh2, mm2, ss2, ms2;
        if (sscanf(*buf, "%d:%2d:%2d%*1[,.]%3d --> %d:%2d:%2d%*1[,.]%3d"
                   "%*[ ]X1:%u X2:%u Y1:%u Y2:%u",
                   &hh1, &mm1, &ss1, &ms1,
                   &hh2, &mm2, &ss2, &ms2,
                   x1, x2, y1, y2) >= 8) {
            int64_t start = (hh1*3600LL + mm1*60LL + ss1) * 1000LL + ms1;
            int64_t end   = (hh2*3600LL + mm2*60LL + ss2) * 1000LL + ms2;
            *duration = end - start;
            *buf += ff_subtitles_next_line(*buf);
            return start;
        }
        *buf += ff_subtitles_next_line(*buf);
    }
    return AV_NOPTS_VALUE;
}

static int srt_read_header(AVFormatContext *s)
{
    SRTContext *srt = s->priv_data;
    AVBPrint buf;
    AVStream *st = avformat_new_stream(s, NULL);
    int res = 0;
    FFTextReader tr;
    ff_text_init_avio(s, &tr, s->pb);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_SUBRIP;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!ff_text_eof(&tr)) {
        ff_subtitles_read_text_chunk(&tr, &buf);

        if (buf.len) {
            int64_t pos = ff_text_pos(&tr);
            int64_t pts;
            int duration;
            const char *ptr = buf.str;
            int32_t x1 = -1, y1 = -1, x2 = -1, y2 = -1;
            AVPacket *sub;

            pts = get_pts(&ptr, &duration, &x1, &y1, &x2, &y2);
            if (pts != AV_NOPTS_VALUE) {
                int len = buf.len - (ptr - buf.str);
                if (len <= 0)
                    continue;
                sub = ff_subtitles_queue_insert(&srt->q, ptr, len, 0);
                if (!sub) {
                    res = AVERROR(ENOMEM);
                    goto end;
                }
                sub->pos = pos;
                sub->pts = pts;
                sub->duration = duration;
                if (x1 != -1) {
                    uint8_t *p = av_packet_new_side_data(sub, AV_PKT_DATA_SUBTITLE_POSITION, 16);
                    if (p) {
                        AV_WL32(p,      x1);
                        AV_WL32(p +  4, y1);
                        AV_WL32(p +  8, x2);
                        AV_WL32(p + 12, y2);
                    }
                }
            }
        }
    }

    ff_subtitles_queue_finalize(&srt->q);

end:
    av_bprint_finalize(&buf, NULL);
    return res;
}

static int srt_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SRTContext *srt = s->priv_data;
    return ff_subtitles_queue_read_packet(&srt->q, pkt);
}

static int srt_read_seek(AVFormatContext *s, int stream_index,
                         int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    SRTContext *srt = s->priv_data;
    return ff_subtitles_queue_seek(&srt->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int srt_read_close(AVFormatContext *s)
{
    SRTContext *srt = s->priv_data;
    ff_subtitles_queue_clean(&srt->q);
    return 0;
}

AVInputFormat ff_srt_demuxer = {
    .name        = "srt",
    .long_name   = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .priv_data_size = sizeof(SRTContext),
    .read_probe  = srt_probe,
    .read_header = srt_read_header,
    .read_packet = srt_read_packet,
    .read_seek2  = srt_read_seek,
    .read_close  = srt_read_close,
};
