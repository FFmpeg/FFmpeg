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
#include "libavutil/intreadwrite.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} SAMIContext;

static int sami_probe(AVProbeData *p)
{
    const unsigned char *ptr = p->buf;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */
    return !strncmp(ptr, "<SAMI>", 6) ? AVPROBE_SCORE_MAX : 0;
}

static int sami_read_header(AVFormatContext *s)
{
    SAMIContext *sami = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    AVBPrint buf, hdr_buf;
    char c = 0;
    int res = 0, got_first_sync_point = 0;

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_SAMI;

    av_bprint_init(&buf,     0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&hdr_buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!url_feof(s->pb)) {
        AVPacket *sub;
        const int64_t pos = avio_tell(s->pb) - (c != 0);
        int is_sync, n = ff_smil_extract_next_chunk(s->pb, &buf, &c);

        if (n == 0)
            break;

        is_sync = !av_strncasecmp(buf.str, "<SYNC", 5);
        if (is_sync)
            got_first_sync_point = 1;

        if (!got_first_sync_point) {
            av_bprintf(&hdr_buf, "%s", buf.str);
        } else {
            sub = ff_subtitles_queue_insert(&sami->q, buf.str, buf.len, !is_sync);
            if (!sub) {
                res = AVERROR(ENOMEM);
                goto end;
            }
            if (is_sync) {
                const char *p = ff_smil_get_attr_ptr(buf.str, "Start");
                sub->pos      = pos;
                sub->pts      = p ? strtol(p, NULL, 10) : 0;
                sub->duration = -1;
            }
        }
        av_bprint_clear(&buf);
    }

    st->codec->extradata_size = hdr_buf.len + 1;
    av_bprint_finalize(&hdr_buf, (char **)&st->codec->extradata);
    if (!st->codec->extradata) {
        st->codec->extradata_size = 0;
        res = AVERROR(ENOMEM);
        goto end;
    }

    ff_subtitles_queue_finalize(&sami->q);

end:
    av_bprint_finalize(&buf, NULL);
    return res;
}

static int sami_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SAMIContext *sami = s->priv_data;
    return ff_subtitles_queue_read_packet(&sami->q, pkt);
}

static int sami_read_close(AVFormatContext *s)
{
    SAMIContext *sami = s->priv_data;
    ff_subtitles_queue_clean(&sami->q);
    return 0;
}

AVInputFormat ff_sami_demuxer = {
    .name           = "sami",
    .long_name      = NULL_IF_CONFIG_SMALL("SAMI subtitle format"),
    .priv_data_size = sizeof(SAMIContext),
    .read_probe     = sami_probe,
    .read_header    = sami_read_header,
    .read_packet    = sami_read_packet,
    .read_close     = sami_read_close,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "smi,sami",
};
