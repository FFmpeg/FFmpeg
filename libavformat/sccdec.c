/*
 * SCC subtitle demuxer
 * Copyright (c) 2017 Paul B Mahol
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

typedef struct SCCContext {
    FFDemuxSubtitlesQueue q;
} SCCContext;

static int scc_probe(const AVProbeData *p)
{
    char buf[18];
    FFTextReader tr;

    ff_text_init_buf(&tr, p->buf, p->buf_size);

    while (ff_text_peek_r8(&tr) == '\r' || ff_text_peek_r8(&tr) == '\n')
        ff_text_r8(&tr);

    ff_text_read(&tr, buf, sizeof(buf));

    if (!memcmp(buf, "Scenarist_SCC V1.0", 18))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int convert(uint8_t x)
{
    if (x >= 'a')
        x -= 87;
    else if (x >= 'A')
        x -= 55;
    else
        x -= '0';
    return x;
}

static int scc_read_header(AVFormatContext *s)
{
    SCCContext *scc = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    char line[4096], line2[4096];
    int count = 0, ret = 0;
    ptrdiff_t len2, len;
    uint8_t out[4096];
    FFTextReader tr;

    ff_text_init_avio(s, &tr, s->pb);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_EIA_608;

    while (!ff_text_eof(&tr)) {
        const int64_t pos = ff_text_pos(&tr);
        char *saveptr = NULL, *lline;
        int hh1, mm1, ss1, fs1, i;
        int hh2, mm2, ss2, fs2;
        int64_t ts_start, ts_end;
        AVPacket *sub;

        if (count == 0) {
            while (!ff_text_eof(&tr)) {
                len = ff_subtitles_read_line(&tr, line, sizeof(line));
                if (len > 13)
                    break;
            }
        }

        if (!strncmp(line, "Scenarist_SCC V1.0", 18))
            continue;
        if (sscanf(line, "%d:%d:%d%*[:;]%d", &hh1, &mm1, &ss1, &fs1) != 4)
            continue;

        ts_start = (hh1 * 3600LL + mm1 * 60LL + ss1) * 1000LL + fs1 * 33;

        while (!ff_text_eof(&tr)) {
            len2 = ff_subtitles_read_line(&tr, line2, sizeof(line2));
            if (len2 > 13)
                break;
        }
        if (sscanf(line2, "%d:%d:%d%*[:;]%d", &hh2, &mm2, &ss2, &fs2) != 4)
            continue;

        ts_end = (hh2 * 3600LL + mm2 * 60LL + ss2) * 1000LL + fs2 * 33;
        count++;

try_again:
        lline = (char *)&line;
        lline += 12;

        for (i = 0; i < 4095; i += 3) {
            char *ptr = av_strtok(lline, " ", &saveptr);
            char c1, c2, c3, c4;

            if (!ptr)
                break;

            if (sscanf(ptr, "%c%c%c%c", &c1, &c2, &c3, &c4) != 4)
                break;

            lline = NULL;
            out[i+0] = 0xfc;
            out[i+1] = convert(c2) | (convert(c1) << 4);
            out[i+2] = convert(c4) | (convert(c3) << 4);
        }
        out[i] = 0;

        sub = ff_subtitles_queue_insert(&scc->q, out, i, 0);
        if (!sub)
            goto fail;

        sub->pos = pos;
        sub->pts = ts_start;
        sub->duration = FFMAX(1200, ts_end - ts_start);
        memmove(line, line2, sizeof(line));
        line2[0] = 0;
        FFSWAP(ptrdiff_t, len, len2);
    }

    if (line[0])
        goto try_again;

    ff_subtitles_queue_finalize(s, &scc->q);

    return ret;
fail:
    ff_subtitles_queue_clean(&scc->q);
    return AVERROR(ENOMEM);
}

static int scc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SCCContext *scc = s->priv_data;
    return ff_subtitles_queue_read_packet(&scc->q, pkt);
}

static int scc_read_seek(AVFormatContext *s, int stream_index,
                         int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    SCCContext *scc = s->priv_data;
    return ff_subtitles_queue_seek(&scc->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int scc_read_close(AVFormatContext *s)
{
    SCCContext *scc = s->priv_data;
    ff_subtitles_queue_clean(&scc->q);
    return 0;
}

AVInputFormat ff_scc_demuxer = {
    .name           = "scc",
    .long_name      = NULL_IF_CONFIG_SMALL("Scenarist Closed Captions"),
    .priv_data_size = sizeof(SCCContext),
    .read_probe     = scc_probe,
    .read_header    = scc_read_header,
    .read_packet    = scc_read_packet,
    .read_seek2     = scc_read_seek,
    .read_close     = scc_read_close,
    .extensions     = "scc",
};
