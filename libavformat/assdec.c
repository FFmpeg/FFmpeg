/*
 * SSA/ASS demuxer
 * Copyright (c) 2008 Michael Niedermayer
 * Copyright (c) 2014 Clément Bœsch
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

#include <stdint.h>

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"
#include "libavcodec/internal.h"
#include "libavutil/bprint.h"

typedef struct ASSContext {
    FFDemuxSubtitlesQueue q;
    unsigned readorder;
} ASSContext;

static int ass_probe(const AVProbeData *p)
{
    char buf[13];
    FFTextReader tr;
    ff_text_init_buf(&tr, p->buf, p->buf_size);

    while (ff_text_peek_r8(&tr) == '\r' || ff_text_peek_r8(&tr) == '\n')
        ff_text_r8(&tr);

    ff_text_read(&tr, buf, sizeof(buf));

    if (!memcmp(buf, "[Script Info]", 13))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int read_dialogue(ASSContext *ass, AVBPrint *dst, const uint8_t *p,
                         int64_t *start, int *duration)
{
    int pos = 0;
    int64_t end;
    int hh1, mm1, ss1, ms1;
    int hh2, mm2, ss2, ms2;

    if (sscanf(p, "Dialogue: %*[^,],%d:%d:%d%*c%d,%d:%d:%d%*c%d,%n",
               &hh1, &mm1, &ss1, &ms1,
               &hh2, &mm2, &ss2, &ms2, &pos) >= 8 && pos > 0) {

        /* This is not part of the sscanf itself in order to handle an actual
         * number (which would be the Layer) or the form "Marked=N" (which is
         * the old SSA field, now replaced by Layer, and will lead to Layer
         * being 0 here). */
        const int layer = atoi(p + 10);

        end    = (hh2*3600LL + mm2*60LL + ss2) * 100LL + ms2;
        *start = (hh1*3600LL + mm1*60LL + ss1) * 100LL + ms1;
        *duration = end - *start;

        av_bprint_clear(dst);
        av_bprintf(dst, "%u,%d,%s", ass->readorder++, layer, p + pos);

        /* right strip the buffer */
        while (dst->len > 0 &&
               dst->str[dst->len - 1] == '\r' ||
               dst->str[dst->len - 1] == '\n')
            dst->str[--dst->len] = 0;
        return 0;
    }
    return -1;
}

static int64_t get_line(AVBPrint *buf, FFTextReader *tr)
{
    int64_t pos = ff_text_pos(tr);

    av_bprint_clear(buf);
    for (;;) {
        char c = ff_text_r8(tr);
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
    AVBPrint header, line, rline;
    int res = 0;
    AVStream *st;
    FFTextReader tr;
    ff_text_init_avio(s, &tr, s->pb);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_ASS;

    av_bprint_init(&header, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&line,   0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&rline,  0, AV_BPRINT_SIZE_UNLIMITED);

    ass->q.keep_duplicates = 1;

    for (;;) {
        int64_t pos = get_line(&line, &tr);
        int64_t ts_start = AV_NOPTS_VALUE;
        int duration = -1;
        AVPacket *sub;

        if (!line.str[0]) // EOF
            break;

        if (read_dialogue(ass, &rline, line.str, &ts_start, &duration) < 0) {
            av_bprintf(&header, "%s", line.str);
            continue;
        }
        sub = ff_subtitles_queue_insert(&ass->q, rline.str, rline.len, 0);
        if (!sub) {
            res = AVERROR(ENOMEM);
            goto end;
        }
        sub->pos = pos;
        sub->pts = ts_start;
        sub->duration = duration;
    }

    res = ff_bprint_to_codecpar_extradata(st->codecpar, &header);
    if (res < 0)
        goto end;

    ff_subtitles_queue_finalize(s, &ass->q);

end:
    av_bprint_finalize(&header, NULL);
    av_bprint_finalize(&line,   NULL);
    av_bprint_finalize(&rline,  NULL);
    return res;
}

const AVInputFormat ff_ass_demuxer = {
    .name           = "ass",
    .long_name      = NULL_IF_CONFIG_SMALL("SSA (SubStation Alpha) subtitle"),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .priv_data_size = sizeof(ASSContext),
    .read_probe     = ass_probe,
    .read_header    = ass_read_header,
    .read_packet    = ff_subtitles_read_packet,
    .read_close     = ff_subtitles_read_close,
    .read_seek2     = ff_subtitles_read_seek,
};
