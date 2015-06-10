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
 * JACOsub subtitle demuxer
 * @see http://unicorn.us.com/jacosub/jscripts.html
 * @todo Support P[ALETTE] directive.
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"
#include "libavcodec/internal.h"
#include "libavcodec/jacosub.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    int shift;
    unsigned timeres;
    FFDemuxSubtitlesQueue q;
} JACOsubContext;

static int timed_line(const char *ptr)
{
    char c;
    int fs, fe;
    return (sscanf(ptr, "%*u:%*u:%*u.%*u %*u:%*u:%*u.%*u %c", &c) == 1 ||
            (sscanf(ptr, "@%u @%u %c", &fs, &fe, &c) == 3 && fs < fe));
}

static int jacosub_probe(AVProbeData *p)
{
    const char *ptr     = p->buf;
    const char *ptr_end = p->buf + p->buf_size;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3; /* skip UTF-8 BOM */

    while (ptr < ptr_end) {
        while (jss_whitespace(*ptr))
            ptr++;
        if (*ptr != '#' && *ptr != '\n') {
            if (timed_line(ptr))
                return AVPROBE_SCORE_EXTENSION + 1;
            return 0;
        }
        ptr += ff_subtitles_next_line(ptr);
    }
    return 0;
}

static const char * const cmds[] = {
    "CLOCKPAUSE",
    "DIRECTIVE",
    "FONT",
    "HRES",
    "INCLUDE",
    "PALETTE",
    "QUANTIZE",
    "RAMP",
    "SHIFT",
    "TIMERES",
};

static int get_jss_cmd(char k)
{
    int i;

    k = av_toupper(k);
    for (i = 0; i < FF_ARRAY_ELEMS(cmds); i++)
        if (k == cmds[i][0])
            return i;
    return -1;
}

static int jacosub_read_close(AVFormatContext *s)
{
    JACOsubContext *jacosub = s->priv_data;
    ff_subtitles_queue_clean(&jacosub->q);
    return 0;
}

static const char *read_ts(JACOsubContext *jacosub, const char *buf,
                           int64_t *start, int *duration)
{
    int len;
    unsigned hs, ms, ss, fs; // hours, minutes, seconds, frame start
    unsigned he, me, se, fe; // hours, minutes, seconds, frame end
    int ts_start, ts_end;

    /* timed format */
    if (sscanf(buf, "%u:%u:%u.%u %u:%u:%u.%u %n",
               &hs, &ms, &ss, &fs,
               &he, &me, &se, &fe, &len) == 8) {
        ts_start = (hs*3600 + ms*60 + ss) * jacosub->timeres + fs;
        ts_end   = (he*3600 + me*60 + se) * jacosub->timeres + fe;
        goto shift_and_ret;
    }

    /* timestamps format */
    if (sscanf(buf, "@%u @%u %n", &ts_start, &ts_end, &len) == 2)
        goto shift_and_ret;

    return NULL;

shift_and_ret:
    ts_start  = (ts_start + jacosub->shift) * 100 / jacosub->timeres;
    ts_end    = (ts_end   + jacosub->shift) * 100 / jacosub->timeres;
    *start    = ts_start;
    *duration = ts_start + ts_end;
    return buf + len;
}

static int get_shift(int timeres, const char *buf)
{
    int sign = 1;
    int a = 0, b = 0, c = 0, d = 0;
#define SSEP "%*1[.:]"
    int n = sscanf(buf, "%d"SSEP"%d"SSEP"%d"SSEP"%d", &a, &b, &c, &d);
#undef SSEP

    if (*buf == '-' || a < 0) {
        sign = -1;
        a = FFABS(a);
    }

    switch (n) {
    case 4: return sign * ((a*3600 + b*60 + c) * timeres + d);
    case 3: return sign * ((         a*60 + b) * timeres + c);
    case 2: return sign * ((                a) * timeres + b);
    }

    return 0;
}

static int jacosub_read_header(AVFormatContext *s)
{
    AVBPrint header;
    AVIOContext *pb = s->pb;
    char line[JSS_MAX_LINESIZE];
    JACOsubContext *jacosub = s->priv_data;
    int shift_set = 0; // only the first shift matters
    int merge_line = 0;
    int i, ret;

    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_JACOSUB;

    jacosub->timeres = 30;

    av_bprint_init(&header, 1024+FF_INPUT_BUFFER_PADDING_SIZE, 4096);

    while (!avio_feof(pb)) {
        int cmd_len;
        const char *p = line;
        int64_t pos = avio_tell(pb);
        int len = ff_get_line(pb, line, sizeof(line));

        p = jss_skip_whitespace(p);

        /* queue timed line */
        if (merge_line || timed_line(p)) {
            AVPacket *sub;

            sub = ff_subtitles_queue_insert(&jacosub->q, line, len, merge_line);
            if (!sub)
                return AVERROR(ENOMEM);
            sub->pos = pos;
            merge_line = len > 1 && !strcmp(&line[len - 2], "\\\n");
            continue;
        }

        /* skip all non-compiler commands and focus on the command */
        if (*p != '#')
            continue;
        p++;
        i = get_jss_cmd(p[0]);
        if (i == -1)
            continue;

        /* trim command + spaces */
        cmd_len = strlen(cmds[i]);
        if (av_strncasecmp(p, cmds[i], cmd_len) == 0)
            p += cmd_len;
        else
            p++;
        p = jss_skip_whitespace(p);

        /* handle commands which affect the whole script */
        switch (cmds[i][0]) {
        case 'S': // SHIFT command affect the whole script...
            if (!shift_set) {
                jacosub->shift = get_shift(jacosub->timeres, p);
                shift_set = 1;
            }
            av_bprintf(&header, "#S %s", p);
            break;
        case 'T': // ...but must be placed after TIMERES
            jacosub->timeres = strtol(p, NULL, 10);
            if (!jacosub->timeres)
                jacosub->timeres = 30;
            else
                av_bprintf(&header, "#T %s", p);
            break;
        }
    }

    /* general/essential directives in the extradata */
    ret = avpriv_bprint_to_extradata(st->codec, &header);
    if (ret < 0)
        goto fail;

    /* SHIFT and TIMERES affect the whole script so packet timing can only be
     * done in a second pass */
    for (i = 0; i < jacosub->q.nb_subs; i++) {
        AVPacket *sub = &jacosub->q.subs[i];
        read_ts(jacosub, sub->data, &sub->pts, &sub->duration);
    }
    ff_subtitles_queue_finalize(&jacosub->q);

    return 0;
fail:
    jacosub_read_close(s);
    return ret;
}

static int jacosub_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    JACOsubContext *jacosub = s->priv_data;
    return ff_subtitles_queue_read_packet(&jacosub->q, pkt);
}

static int jacosub_read_seek(AVFormatContext *s, int stream_index,
                             int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    JACOsubContext *jacosub = s->priv_data;
    return ff_subtitles_queue_seek(&jacosub->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

AVInputFormat ff_jacosub_demuxer = {
    .name           = "jacosub",
    .long_name      = NULL_IF_CONFIG_SMALL("JACOsub subtitle format"),
    .priv_data_size = sizeof(JACOsubContext),
    .read_probe     = jacosub_probe,
    .read_header    = jacosub_read_header,
    .read_packet    = jacosub_read_packet,
    .read_seek2     = jacosub_read_seek,
    .read_close     = jacosub_read_close,
};
