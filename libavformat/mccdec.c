/*
 * MCC subtitle demuxer
 * Copyright (c) 2020 Paul B Mahol
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
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct MCCContext {
    FFDemuxSubtitlesQueue q;
} MCCContext;

static int mcc_probe(const AVProbeData *p)
{
    char buf[28];
    FFTextReader tr;

    ff_text_init_buf(&tr, p->buf, p->buf_size);

    while (ff_text_peek_r8(&tr) == '\r' || ff_text_peek_r8(&tr) == '\n')
        ff_text_r8(&tr);

    ff_text_read(&tr, buf, sizeof(buf));

    if (!memcmp(buf, "File Format=MacCaption_MCC V", 28))
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

typedef struct alias {
    uint8_t key;
    int len;
    const char *value;
} alias;

static const alias aliases[20] = {
    { .key = 16, .len =  3, .value = "\xFA\x0\x0", },
    { .key = 17, .len =  6, .value = "\xFA\x0\x0\xFA\x0\x0", },
    { .key = 18, .len =  9, .value = "\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0", },
    { .key = 19, .len = 12, .value = "\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0", },
    { .key = 20, .len = 15, .value = "\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0", },
    { .key = 21, .len = 18, .value = "\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0", },
    { .key = 22, .len = 21, .value = "\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0", },
    { .key = 23, .len = 24, .value = "\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0", },
    { .key = 24, .len = 27, .value = "\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0\xFA\x0\x0", },
    { .key = 25, .len =  3, .value = "\xFB\x80\x80", },
    { .key = 26, .len =  3, .value = "\xFC\x80\x80", },
    { .key = 27, .len =  3, .value = "\xFD\x80\x80", },
    { .key = 28, .len =  2, .value = "\x96\x69", },
    { .key = 29, .len =  2, .value = "\x61\x01", },
    { .key = 30, .len =  3, .value = "\xFC\x80\x80", },
    { .key = 31, .len =  3, .value = "\xFC\x80\x80", },
    { .key = 32, .len =  4, .value = "\xE1\x00\x00\x00", },
    { .key = 33, .len =  0, .value = NULL, },
    { .key = 34, .len =  0, .value = NULL, },
    { .key = 35, .len =  1, .value = "\x0", },
};

static int mcc_read_header(AVFormatContext *s)
{
    MCCContext *mcc = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    AVRational rate;
    int64_t ts, pos;
    uint8_t out[4096];
    char line[4096];
    FFTextReader tr;
    int ret = 0;

    ff_text_init_avio(s, &tr, s->pb);

    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_EIA_608;
    avpriv_set_pts_info(st, 64, 1, 30);

    while (!ff_text_eof(&tr)) {
        int hh, mm, ss, fs, i = 0, j = 0;
        int start = 12, count = 0;
        AVPacket *sub;
        char *lline;

        ff_subtitles_read_line(&tr, line, sizeof(line));
        if (!strncmp(line, "File Format=MacCaption_MCC V", 28))
            continue;
        if (!strncmp(line, "//", 2))
            continue;
        if (!strncmp(line, "Time Code Rate=", 15)) {
            char *rate_str = line + 15;
            char *df = NULL;
            int num = -1, den = -1;

            if (rate_str[0]) {
                num = strtol(rate_str, &df, 10);
                den = 1;
                if (df && !av_strncasecmp(df, "DF", 2)) {
                    av_reduce(&num, &den, num * 1000LL, 1001, INT_MAX);
                }
            }

            if (num > 0 && den > 0) {
                rate = av_make_q(num, den);
                avpriv_set_pts_info(st, 64, rate.den, rate.num);
            }
            continue;
        }

        if (av_sscanf(line, "%d:%d:%d:%d", &hh, &mm, &ss, &fs) != 4)
            continue;

        ts = av_sat_add64(av_rescale(hh * 3600LL + mm * 60LL + ss, rate.num, rate.den), fs);

        lline = (char *)&line;
        lline += 12;
        pos = ff_text_pos(&tr);

        while (lline[i]) {
            uint8_t v = convert(lline[i]);

            if (v >= 16 && v <= 35) {
                int idx = v - 16;
                if (aliases[idx].len) {
                    if (j >= sizeof(out) - 1 - aliases[idx].len) {
                        j = 0;
                        break;
                    }
                    memcpy(out + j, aliases[idx].value, aliases[idx].len);
                    j += aliases[idx].len;
                }
            } else {
                uint8_t vv;

                if (i + 13 >= sizeof(line) - 1)
                    break;
                vv = convert(lline[i + 1]);
                if (j >= sizeof(out) - 1) {
                    j = 0;
                    break;
                }
                out[j++] = vv | (v << 4);
                i++;
            }

            i++;
        }
        out[j] = 0;

        if (out[7] & 0x80)
            start += 4;
        count = (out[11] & 0x1f) * 3;
        if (j < start + count + 1)
            continue;

        if (!count)
            continue;
        sub = ff_subtitles_queue_insert(&mcc->q, out + start, count, 0);
        if (!sub)
            return AVERROR(ENOMEM);

        sub->pos = pos;
        sub->pts = ts;
        sub->duration = 1;
    }

    ff_subtitles_queue_finalize(s, &mcc->q);

    return ret;
}

const AVInputFormat ff_mcc_demuxer = {
    .name           = "mcc",
    .long_name      = NULL_IF_CONFIG_SMALL("MacCaption"),
    .priv_data_size = sizeof(MCCContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = mcc_probe,
    .read_header    = mcc_read_header,
    .extensions     = "mcc",
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close,
};
