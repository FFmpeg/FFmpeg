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
 * RealText subtitle demuxer
 * @see http://service.real.com/help/library/guides/ProductionGuide/prodguide/htmfiles/realtext.htm
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} RealTextContext;

static int realtext_probe(AVProbeData *p)
{
    const unsigned char *ptr = p->buf;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */
    return !av_strncasecmp(ptr, "<window", 7) ? AVPROBE_SCORE_MAX/2 : 0;
}

static int read_ts(const char *s)
{
    int hh, mm, ss, ms;

    if (sscanf(s, "%u:%u:%u.%u", &hh, &mm, &ss, &ms) == 4) return (hh*3600 + mm*60 + ss) * 100 + ms;
    if (sscanf(s, "%u:%u:%u"   , &hh, &mm, &ss     ) == 3) return (hh*3600 + mm*60 + ss) * 100;
    if (sscanf(s,    "%u:%u.%u",      &mm, &ss, &ms) == 3) return (          mm*60 + ss) * 100 + ms;
    if (sscanf(s,    "%u:%u"   ,      &mm, &ss     ) == 2) return (          mm*60 + ss) * 100;
    if (sscanf(s,       "%u.%u",           &ss, &ms) == 2) return (                  ss) * 100 + ms;
    return strtol(s, NULL, 10) * 100;
}

static int realtext_read_header(AVFormatContext *s)
{
    RealTextContext *rt = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    AVBPrint buf;
    char c = 0;
    int res = 0, duration = read_ts("60"); // default duration is 60 seconds

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_REALTEXT;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!url_feof(s->pb)) {
        AVPacket *sub;
        const int64_t pos = avio_tell(s->pb) - (c != 0);
        int n = ff_smil_extract_next_chunk(s->pb, &buf, &c);

        if (n == 0)
            break;

        if (!av_strncasecmp(buf.str, "<window", 7)) {
            /* save header to extradata */
            const char *p = ff_smil_get_attr_ptr(buf.str, "duration");

            if (p)
                duration = read_ts(p);
            st->codec->extradata = av_strdup(buf.str);
            if (!st->codec->extradata) {
                res = AVERROR(ENOMEM);
                goto end;
            }
            st->codec->extradata_size = buf.len + 1;
        } else {
            /* if we just read a <time> tag, introduce a new event, otherwise merge
             * with the previous one */
            int merge = !av_strncasecmp(buf.str, "<time", 5) ? 0 : 1;
            sub = ff_subtitles_queue_insert(&rt->q, buf.str, buf.len, merge);
            if (!sub) {
                res = AVERROR(ENOMEM);
                goto end;
            }
            if (!merge) {
                const char *begin = ff_smil_get_attr_ptr(buf.str, "begin");
                const char *end   = ff_smil_get_attr_ptr(buf.str, "end");

                sub->pos      = pos;
                sub->pts      = begin ? read_ts(begin) : 0;
                sub->duration = end ? (read_ts(end) - sub->pts) : duration;
            }
        }
        av_bprint_clear(&buf);
    }
    ff_subtitles_queue_finalize(&rt->q);

end:
    av_bprint_finalize(&buf, NULL);
    return res;
}

static int realtext_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    RealTextContext *rt = s->priv_data;
    return ff_subtitles_queue_read_packet(&rt->q, pkt);
}

static int realtext_read_close(AVFormatContext *s)
{
    RealTextContext *rt = s->priv_data;
    ff_subtitles_queue_clean(&rt->q);
    return 0;
}

AVInputFormat ff_realtext_demuxer = {
    .name           = "realtext",
    .long_name      = NULL_IF_CONFIG_SMALL("RealText subtitle format"),
    .priv_data_size = sizeof(RealTextContext),
    .read_probe     = realtext_probe,
    .read_header    = realtext_read_header,
    .read_packet    = realtext_read_packet,
    .read_close     = realtext_read_close,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "rt",
};
