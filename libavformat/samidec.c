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
 * SAMI subtitle demuxer
 * @see http://msdn.microsoft.com/en-us/library/ms971327.aspx
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} SAMIContext;

static int sami_probe(const AVProbeData *p)
{
    char buf[6];
    FFTextReader tr;
    ff_text_init_buf(&tr, p->buf, p->buf_size);
    ff_text_read(&tr, buf, sizeof(buf));

    return !strncmp(buf, "<SAMI>", 6) ? AVPROBE_SCORE_MAX : 0;
}

static int sami_read_header(AVFormatContext *s)
{
    SAMIContext *sami = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    AVBPrint buf, hdr_buf;
    char c = 0;
    int res = 0, got_first_sync_point = 0;
    FFTextReader tr;
    ff_text_init_avio(s, &tr, s->pb);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_SAMI;

    av_bprint_init(&buf,     0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&hdr_buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!ff_text_eof(&tr)) {
        AVPacket *sub;
        const int64_t pos = ff_text_pos(&tr) - (c != 0);
        int is_sync, is_body, n = ff_smil_extract_next_text_chunk(&tr, &buf, &c);

        if (n == 0)
            break;

        is_body = !av_strncasecmp(buf.str, "</BODY", 6);
        if (is_body) {
             av_bprint_clear(&buf);
             break;
        }

        is_sync = !av_strncasecmp(buf.str, "<SYNC", 5);
        if (is_sync)
            got_first_sync_point = 1;

        if (!got_first_sync_point) {
            av_bprintf(&hdr_buf, "%s", buf.str);
        } else {
            sub = ff_subtitles_queue_insert(&sami->q, buf.str, buf.len, !is_sync);
            if (!sub) {
                res = AVERROR(ENOMEM);
                av_bprint_finalize(&hdr_buf, NULL);
                goto end;
            }
            if (is_sync) {
                const char *p = ff_smil_get_attr_ptr(buf.str, "Start");
                sub->pos      = pos;
                sub->pts      = p ? strtol(p, NULL, 10) : 0;
                if (sub->pts <= INT64_MIN/2 || sub->pts >= INT64_MAX/2) {
                    res = AVERROR_PATCHWELCOME;
                    av_bprint_finalize(&hdr_buf, NULL);
                    goto end;
                }

                sub->duration = -1;
            }
        }
        av_bprint_clear(&buf);
    }

    res = ff_bprint_to_codecpar_extradata(st->codecpar, &hdr_buf);
    if (res < 0)
        goto end;

    ff_subtitles_queue_finalize(s, &sami->q);

end:
    av_bprint_finalize(&buf, NULL);
    return res;
}

const AVInputFormat ff_sami_demuxer = {
    .name           = "sami",
    .long_name      = NULL_IF_CONFIG_SMALL("SAMI subtitle format"),
    .priv_data_size = sizeof(SAMIContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = sami_probe,
    .read_header    = sami_read_header,
    .extensions     = "smi,sami",
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close,
};
