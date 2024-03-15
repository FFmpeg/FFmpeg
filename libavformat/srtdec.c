/*
 * SubRip subtitle demuxer
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (c) 2015  Clément Bœsch <u pkh me>
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
#include "demux.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} SRTContext;

static int srt_probe(const AVProbeData *p)
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
    pbuf = buf;
    if (buf[0] == '-')
        pbuf++;
    if (pbuf[0] >= '0' && pbuf[0] <= '9' && strstr(buf, " --> ")
        && sscanf(buf, "%*d:%*d:%*d%*1[,.]%*d --> %*d:%*d:%*d%*1[,.]%d", &v) == 1)
        return AVPROBE_SCORE_MAX;

    return 0;
}

struct event_info {
    int32_t x1, x2, y1, y2;
    int duration;
    int64_t pts;
    int64_t pos;
};

static int get_event_info(const char *line, struct event_info *ei)
{
    int hh1, mm1, ss1, ms1;
    int hh2, mm2, ss2, ms2;

    ei->x1 = ei->x2 = ei->y1 = ei->y2 = ei->duration = -1;
    ei->pts = AV_NOPTS_VALUE;
    ei->pos = -1;
    if (sscanf(line, "%d:%d:%d%*1[,.]%d --> %d:%d:%d%*1[,.]%d"
               "%*[ ]X1:%"PRId32" X2:%"PRId32" Y1:%"PRId32" Y2:%"PRId32,
               &hh1, &mm1, &ss1, &ms1,
               &hh2, &mm2, &ss2, &ms2,
               &ei->x1, &ei->x2, &ei->y1, &ei->y2) >= 8) {
        const int64_t start = (hh1*3600LL + mm1*60LL + ss1) * 1000LL + ms1;
        const int64_t end   = (hh2*3600LL + mm2*60LL + ss2) * 1000LL + ms2;
        ei->duration = end - start;
        ei->pts = start;
        return 0;
    }
    return -1;
}

static int add_event(FFDemuxSubtitlesQueue *q, AVBPrint *buf, char *line_cache,
                     const struct event_info *ei, int append_cache)
{
    if (append_cache && line_cache[0])
        av_bprintf(buf, "%s\n", line_cache);
    line_cache[0] = 0;
    if (!av_bprint_is_complete(buf))
        return AVERROR(ENOMEM);

    while (buf->len > 0 && buf->str[buf->len - 1] == '\n')
        buf->str[--buf->len] = 0;

    if (buf->len) {
        AVPacket *sub = ff_subtitles_queue_insert_bprint(q, buf, 0);
        if (!sub)
            return AVERROR(ENOMEM);
        av_bprint_clear(buf);
        sub->pos = ei->pos;
        sub->pts = ei->pts;
        sub->duration = ei->duration;
        if (ei->x1 != -1) {
            uint8_t *p = av_packet_new_side_data(sub, AV_PKT_DATA_SUBTITLE_POSITION, 16);
            if (p) {
                AV_WL32(p,      ei->x1);
                AV_WL32(p +  4, ei->y1);
                AV_WL32(p +  8, ei->x2);
                AV_WL32(p + 12, ei->y2);
            }
        }
    }

    return 0;
}

static int srt_read_header(AVFormatContext *s)
{
    SRTContext *srt = s->priv_data;
    AVBPrint buf;
    AVStream *st = avformat_new_stream(s, NULL);
    int res = 0;
    char line[4096], line_cache[4096];
    int has_event_info = 0;
    struct event_info ei;
    FFTextReader tr;
    ff_text_init_avio(s, &tr, s->pb);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_SUBRIP;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    line_cache[0] = 0;

    while (!ff_text_eof(&tr)) {
        struct event_info tmp_ei;
        const int64_t pos = ff_text_pos(&tr);
        ptrdiff_t len = ff_subtitles_read_line(&tr, line, sizeof(line));

        if (len < 0)
            break;

        if (!len || !line[0])
            continue;

        if (get_event_info(line, &tmp_ei) < 0) {
            char *pline;

            if (!has_event_info)
                continue;

            if (line_cache[0]) {
                /* We got some cache and a new line so we assume the cached
                 * line was actually part of the payload */
                av_bprintf(&buf, "%s\n", line_cache);
                line_cache[0] = 0;
            }

            /* If the line doesn't start with a number, we assume it's part of
             * the payload, otherwise is likely an event number preceding the
             * timing information... but we can't be sure of this yet, so we
             * cache it */
            if (strtol(line, &pline, 10) < 0 || line == pline)
                av_bprintf(&buf, "%s\n", line);
            else
                strcpy(line_cache, line);
        } else {
            if (has_event_info) {
                /* We have the information of previous event, append it to the
                 * queue. We insert the cached line if and only if the payload
                 * is empty and the cached line is not a standalone number. */
                char *pline = NULL;
                const int standalone_number = strtol(line_cache, &pline, 10) >= 0 && pline && !*pline;
                res = add_event(&srt->q, &buf, line_cache, &ei, !buf.len && !standalone_number);
                if (res < 0)
                    goto end;
            } else {
                has_event_info = 1;
            }
            tmp_ei.pos = pos;
            ei = tmp_ei;
        }
    }

    /* Append the last event. Here we force the cache to be flushed, because a
     * trailing number is more likely to be geniune (for example a copyright
     * date) and not the event index of an inexistant event */
    if (has_event_info) {
        res = add_event(&srt->q, &buf, line_cache, &ei, 1);
        if (res < 0)
            goto end;
    }

    ff_subtitles_queue_finalize(s, &srt->q);

end:
    av_bprint_finalize(&buf, NULL);
    return res;
}

const FFInputFormat ff_srt_demuxer = {
    .p.name      = "srt",
    .p.long_name = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .priv_data_size = sizeof(SRTContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe  = srt_probe,
    .read_header = srt_read_header,
    .read_packet = ff_subtitles_read_packet,
    .read_seek2  = ff_subtitles_read_seek,
    .read_close  = ff_subtitles_read_close,
};
