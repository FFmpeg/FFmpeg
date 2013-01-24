/*
 * SSA/ASS demuxer
 * Copyright (c) 2008 Michael Niedermayer
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
#include "libavcodec/internal.h"
#include "libavutil/bprint.h"

typedef struct ASSContext{
    FFDemuxSubtitlesQueue q;
}ASSContext;

static int ass_probe(AVProbeData *p)
{
    const char *header= "[Script Info]";

    if(   !memcmp(p->buf  , header, strlen(header))
       || !memcmp(p->buf+3, header, strlen(header)))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int ass_read_close(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    ff_subtitles_queue_clean(&ass->q);
    return 0;
}

static int read_ts(const uint8_t *p, int64_t *start, int *duration)
{
    int64_t end;
    int hh1, mm1, ss1, ms1;
    int hh2, mm2, ss2, ms2;

    if (sscanf(p, "%*[^,],%d:%d:%d%*c%d,%d:%d:%d%*c%d",
               &hh1, &mm1, &ss1, &ms1,
               &hh2, &mm2, &ss2, &ms2) == 8) {
        end    = (hh2*3600LL + mm2*60LL + ss2) * 100LL + ms2;
        *start = (hh1*3600LL + mm1*60LL + ss1) * 100LL + ms1;
        *duration = end - *start;
        return 0;
    }
    return -1;
}

static int64_t get_line(AVBPrint *buf, AVIOContext *pb)
{
    int64_t pos = avio_tell(pb);

    av_bprint_clear(buf);
    for (;;) {
        char c = avio_r8(pb);
        if (!c)
            break;
        av_bprint_chars(buf, c, 1);
        if (c == '\n')
            break;
    }
    return pos;
}

static int ass_read_header(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVBPrint header, line;
    int header_remaining, res = 0;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id= AV_CODEC_ID_SSA;

    header_remaining= INT_MAX;

    av_bprint_init(&header, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&line,   0, AV_BPRINT_SIZE_UNLIMITED);

    for (;;) {
        int64_t pos = get_line(&line, s->pb);

        if (!line.str[0]) // EOF
            break;

        if (!memcmp(line.str, "[Events]", 8))
            header_remaining= 2;
        else if (line.str[0]=='[')
            header_remaining= INT_MAX;

        if (header_remaining) {
            av_bprintf(&header, "%s", line.str);
            header_remaining--;
        } else {
            int64_t ts_start = AV_NOPTS_VALUE;
            int duration = -1;
            AVPacket *sub;

            if (read_ts(line.str, &ts_start, &duration) < 0)
                continue;
            sub = ff_subtitles_queue_insert(&ass->q, line.str, line.len, 0);
            if (!sub) {
                res = AVERROR(ENOMEM);
                goto end;
            }
            sub->pos = pos;
            sub->pts = ts_start;
            sub->duration = duration;
        }
    }

    av_bprint_finalize(&line, NULL);

    res = avpriv_bprint_to_extradata(st->codec, &header);
    if (res < 0)
        goto end;

    ff_subtitles_queue_finalize(&ass->q);

end:
    return res;
}

static int ass_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASSContext *ass = s->priv_data;
    return ff_subtitles_queue_read_packet(&ass->q, pkt);
}

static int ass_read_seek(AVFormatContext *s, int stream_index,
                         int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    ASSContext *ass = s->priv_data;
    return ff_subtitles_queue_seek(&ass->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

AVInputFormat ff_ass_demuxer = {
    .name           = "ass",
    .long_name      = NULL_IF_CONFIG_SMALL("SSA (SubStation Alpha) subtitle"),
    .priv_data_size = sizeof(ASSContext),
    .read_probe     = ass_probe,
    .read_header    = ass_read_header,
    .read_packet    = ass_read_packet,
    .read_close     = ass_read_close,
    .read_seek2     = ass_read_seek,
};
