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
#include "demux.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/avstring.h"
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
        x -= 'a' - 10;
    else if (x >= 'A')
        x -= 'A' - 10;
    else
        x -= '0';
    return x;
}

static int scc_read_header(AVFormatContext *s)
{
    SCCContext *scc = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    AVPacket *sub = NULL;
    ptrdiff_t len;
    uint8_t out[4096];
    FFTextReader tr;

    ff_text_init_avio(s, &tr, s->pb);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_EIA_608;

    while (1) {
        char *saveptr = NULL, *lline;
        int hh, mm, ss, fs, i;
        char line[4096];
        int64_t pos, ts;

        pos = ff_text_pos(&tr);
        len = ff_subtitles_read_line(&tr, line, sizeof(line));
        if (len <= 13) {
            if (ff_text_eof(&tr))
                break;
            continue;
        }
        if (av_sscanf(line, "%d:%d:%d%*[:;]%d", &hh, &mm, &ss, &fs) != 4)
            continue;

        ts = (hh * 3600LL + mm * 60LL + ss) * 1000LL + fs * 33LL;
        if (sub)
            sub->duration = ts - sub->pts;

        lline  = line;
        lline += 12;

        for (i = 0; i < 4095; i += 3) {
            char *ptr = av_strtok(lline, " ", &saveptr);
            char c1, c2, c3, c4;
            uint8_t o1, o2;

            if (!ptr)
                break;

            if (av_sscanf(ptr, "%c%c%c%c", &c1, &c2, &c3, &c4) != 4)
                break;
            o1 = convert(c2) | (convert(c1) << 4);
            o2 = convert(c4) | (convert(c3) << 4);

            lline = NULL;

            if (i > 12 && o1 == 0x94 && o2 == 0x20 && saveptr &&
                (av_strncasecmp(saveptr, "942f", 4) && !av_strncasecmp(saveptr, "942c", 4))) {

                sub = ff_subtitles_queue_insert(&scc->q, out, i, 0);
                if (!sub)
                    return AVERROR(ENOMEM);

                sub->pos = pos;
                pos += i;
                sub->pts = ts;
                sub->duration = i * 11;
                ts += sub->duration;
                i = 0;
            }

            out[i+0] = 0xfc;
            out[i+1] = o1;
            out[i+2] = o2;
        }

        sub = ff_subtitles_queue_insert(&scc->q, out, i, 0);
        if (!sub)
            return AVERROR(ENOMEM);

        sub->pos = pos;
        sub->pts = ts;
    }

    ff_subtitles_queue_finalize(s, &scc->q);

    return 0;
}

const FFInputFormat ff_scc_demuxer = {
    .p.name         = "scc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Scenarist Closed Captions"),
    .p.extensions   = "scc",
    .priv_data_size = sizeof(SCCContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = scc_probe,
    .read_header    = scc_read_header,
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close,
};
