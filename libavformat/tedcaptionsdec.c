/*
 * TED Talks captions format decoder
 * Copyright (c) 2012 Nicolas George
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

#include "libavutil/bprint.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"
#include "subtitles.h"

typedef struct {
    AVClass *class;
    int64_t start_time;
    FFDemuxSubtitlesQueue subs;
} TEDCaptionsDemuxer;

static const AVOption tedcaptions_options[] = {
    { "start_time", "set the start time (offset) of the subtitles, in ms",
      offsetof(TEDCaptionsDemuxer, start_time), AV_OPT_TYPE_INT64,
      { .i64 = 15000 }, INT64_MIN, INT64_MAX,
      AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass tedcaptions_demuxer_class = {
    .class_name = "tedcaptions_demuxer",
    .item_name  = av_default_item_name,
    .option     = tedcaptions_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#define BETWEEN(a, amin, amax) ((unsigned)((a) - (amin)) <= (amax) - (amin))

#define HEX_DIGIT_TEST(c) (BETWEEN(c, '0', '9') || BETWEEN((c) | 32, 'a', 'z'))
#define HEX_DIGIT_VAL(c) ((c) <= '9' ? (c) - '0' : ((c) | 32) - 'a' + 10)
#define ERR_CODE(c) ((c) < 0 ? (c) : AVERROR_INVALIDDATA)

static void av_bprint_utf8(AVBPrint *bp, unsigned c)
{
    int bytes, i;

    if (c <= 0x7F) {
        av_bprint_chars(bp, c, 1);
        return;
    }
    bytes = (av_log2(c) - 2) / 5;
    av_bprint_chars(bp, (c >> (bytes * 6)) | ((0xFF80 >> bytes) & 0xFF), 1);
    for (i = bytes - 1; i >= 0; i--)
        av_bprint_chars(bp, ((c >> (i * 6)) & 0x3F) | 0x80, 1);
}

static void next_byte(AVIOContext *pb, int *cur_byte)
{
    uint8_t b;
    int ret = avio_read(pb, &b, 1);
    *cur_byte = ret > 0 ? b : ret == 0 ? AVERROR_EOF : ret;
}

static void skip_spaces(AVIOContext *pb, int *cur_byte)
{
    while (*cur_byte == ' '  || *cur_byte == '\t' ||
           *cur_byte == '\n' || *cur_byte == '\r')
        next_byte(pb, cur_byte);
}

static int expect_byte(AVIOContext *pb, int *cur_byte, uint8_t c)
{
    skip_spaces(pb, cur_byte);
    if (*cur_byte != c)
        return ERR_CODE(*cur_byte);
    next_byte(pb, cur_byte);
    return 0;
}

static int parse_string(AVIOContext *pb, int *cur_byte, AVBPrint *bp, int full)
{
    int ret;

    ret = expect_byte(pb, cur_byte, '"');
    if (ret < 0)
        return ret;
    while (*cur_byte > 0 && *cur_byte != '"') {
        if (*cur_byte == '\\') {
            next_byte(pb, cur_byte);
            if (*cur_byte < 0)
                return AVERROR_INVALIDDATA;
            if ((*cur_byte | 32) == 'u') {
                unsigned chr = 0, i;
                for (i = 0; i < 4; i++) {
                    next_byte(pb, cur_byte);
                    if (!HEX_DIGIT_TEST(*cur_byte))
                        return ERR_CODE(*cur_byte);
                    chr = chr * 16 + HEX_DIGIT_VAL(*cur_byte);
                }
                av_bprint_utf8(bp, chr);
            } else {
                av_bprint_chars(bp, *cur_byte, 1);
            }
        } else {
            av_bprint_chars(bp, *cur_byte, 1);
        }
        next_byte(pb, cur_byte);
    }
    ret = expect_byte(pb, cur_byte, '"');
    if (ret < 0)
        return ret;
    if (full && !av_bprint_is_complete(bp))
        return AVERROR(ENOMEM);

    return 0;
}

static int parse_label(AVIOContext *pb, int *cur_byte, AVBPrint *bp)
{
    int ret;

    av_bprint_init(bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
    ret = parse_string(pb, cur_byte, bp, 0);
    if (ret < 0)
        return ret;
    ret = expect_byte(pb, cur_byte, ':');
    if (ret < 0)
        return ret;
    return 0;
}

static int parse_boolean(AVIOContext *pb, int *cur_byte, int *result)
{
    static const char * const text[] = { "false", "true" };
    const char *p;
    int i;

    skip_spaces(pb, cur_byte);
    for (i = 0; i < 2; i++) {
        p = text[i];
        if (*cur_byte != *p)
            continue;
        for (; *p; p++, next_byte(pb, cur_byte))
            if (*cur_byte != *p)
                return AVERROR_INVALIDDATA;
        if (BETWEEN(*cur_byte | 32, 'a', 'z'))
            return AVERROR_INVALIDDATA;
        *result = i;
        return 0;
    }
    return AVERROR_INVALIDDATA;
}

static int parse_int(AVIOContext *pb, int *cur_byte, int64_t *result)
{
    int64_t val = 0;

    skip_spaces(pb, cur_byte);
    if ((unsigned)*cur_byte - '0' > 9)
        return AVERROR_INVALIDDATA;
    while (BETWEEN(*cur_byte, '0', '9')) {
        if (val > INT_MAX/10 - (*cur_byte - '0'))
            return AVERROR_INVALIDDATA;
        val = val * 10 + (*cur_byte - '0');
        next_byte(pb, cur_byte);
    }
    *result = val;
    return 0;
}

static int parse_file(AVIOContext *pb, FFDemuxSubtitlesQueue *subs)
{
    int ret, cur_byte, start_of_par;
    AVBPrint label, content;
    int64_t pos, start, duration;
    AVPacket *pkt;

    av_bprint_init(&content, 0, AV_BPRINT_SIZE_UNLIMITED);

    next_byte(pb, &cur_byte);
    ret = expect_byte(pb, &cur_byte, '{');
    if (ret < 0)
        return AVERROR_INVALIDDATA;
    ret = parse_label(pb, &cur_byte, &label);
    if (ret < 0 || strcmp(label.str, "captions"))
        return AVERROR_INVALIDDATA;
    ret = expect_byte(pb, &cur_byte, '[');
    if (ret < 0)
        return AVERROR_INVALIDDATA;
    while (1) {
        start = duration = AV_NOPTS_VALUE;
        ret = expect_byte(pb, &cur_byte, '{');
        if (ret < 0)
            goto fail;
        pos = avio_tell(pb) - 1;
        while (1) {
            ret = parse_label(pb, &cur_byte, &label);
            if (ret < 0)
                goto fail;
            if (!strcmp(label.str, "startOfParagraph")) {
                ret = parse_boolean(pb, &cur_byte, &start_of_par);
                if (ret < 0)
                    goto fail;
            } else if (!strcmp(label.str, "content")) {
                ret = parse_string(pb, &cur_byte, &content, 1);
                if (ret < 0)
                    goto fail;
            } else if (!strcmp(label.str, "startTime")) {
                ret = parse_int(pb, &cur_byte, &start);
                if (ret < 0)
                    goto fail;
            } else if (!strcmp(label.str, "duration")) {
                ret = parse_int(pb, &cur_byte, &duration);
                if (ret < 0)
                    goto fail;
            } else {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            skip_spaces(pb, &cur_byte);
            if (cur_byte != ',')
                break;
            next_byte(pb, &cur_byte);
        }
        ret = expect_byte(pb, &cur_byte, '}');
        if (ret < 0)
            goto fail;

        if (!content.size || start == AV_NOPTS_VALUE ||
            duration == AV_NOPTS_VALUE) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        pkt = ff_subtitles_queue_insert(subs, content.str, content.len, 0);
        if (!pkt) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        pkt->pos      = pos;
        pkt->pts      = start;
        pkt->duration = duration;
        av_bprint_clear(&content);

        skip_spaces(pb, &cur_byte);
        if (cur_byte != ',')
            break;
        next_byte(pb, &cur_byte);
    }
    ret = expect_byte(pb, &cur_byte, ']');
    if (ret < 0)
        goto fail;
    ret = expect_byte(pb, &cur_byte, '}');
    if (ret < 0)
        goto fail;
    skip_spaces(pb, &cur_byte);
    if (cur_byte != AVERROR_EOF)
        ret = ERR_CODE(cur_byte);
fail:
    av_bprint_finalize(&content, NULL);
    return ret;
}

static av_cold int tedcaptions_read_header(AVFormatContext *avf)
{
    TEDCaptionsDemuxer *tc = avf->priv_data;
    AVStream *st = avformat_new_stream(avf, NULL);
    FFStream *sti;
    int ret, i;
    AVPacket *last;

    if (!st)
        return AVERROR(ENOMEM);

    sti = ffstream(st);
    ret = parse_file(avf->pb, &tc->subs);
    if (ret < 0) {
        if (ret == AVERROR_INVALIDDATA)
            av_log(avf, AV_LOG_ERROR, "Syntax error near offset %"PRId64".\n",
                   avio_tell(avf->pb));
        return ret;
    }
    ff_subtitles_queue_finalize(avf, &tc->subs);
    for (i = 0; i < tc->subs.nb_subs; i++)
        tc->subs.subs[i]->pts += tc->start_time;

    last = tc->subs.subs[tc->subs.nb_subs - 1];
    st->codecpar->codec_type     = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id       = AV_CODEC_ID_TEXT;
    avpriv_set_pts_info(st, 64, 1, 1000);
    sti->probe_packets = 0;
    st->start_time    = 0;
    st->duration      = last->pts + last->duration;
    sti->cur_dts      = 0;

    return 0;
}

static int tedcaptions_read_packet(AVFormatContext *avf, AVPacket *packet)
{
    TEDCaptionsDemuxer *tc = avf->priv_data;

    return ff_subtitles_queue_read_packet(&tc->subs, packet);
}

static int tedcaptions_read_close(AVFormatContext *avf)
{
    TEDCaptionsDemuxer *tc = avf->priv_data;

    ff_subtitles_queue_clean(&tc->subs);
    return 0;
}

static av_cold int tedcaptions_read_probe(const AVProbeData *p)
{
    static const char *const tags[] = {
        "\"captions\"", "\"duration\"", "\"content\"",
        "\"startOfParagraph\"", "\"startTime\"",
    };
    unsigned i, count = 0;
    const char *t;

    if (p->buf[strspn(p->buf, " \t\r\n")] != '{')
        return 0;
    for (i = 0; i < FF_ARRAY_ELEMS(tags); i++) {
        if (!(t = strstr(p->buf, tags[i])))
            continue;
        t += strlen(tags[i]);
        t += strspn(t, " \t\r\n");
        if (*t == ':')
            count++;
    }
    return count == FF_ARRAY_ELEMS(tags) ? AVPROBE_SCORE_MAX :
           count                         ? AVPROBE_SCORE_EXTENSION : 0;
}

static int tedcaptions_read_seek(AVFormatContext *avf, int stream_index,
                                 int64_t min_ts, int64_t ts, int64_t max_ts,
                                 int flags)
{
    TEDCaptionsDemuxer *tc = avf->priv_data;
    return ff_subtitles_queue_seek(&tc->subs, avf, stream_index,
                                   min_ts, ts, max_ts, flags);
}

const AVInputFormat ff_tedcaptions_demuxer = {
    .name           = "tedcaptions",
    .long_name      = NULL_IF_CONFIG_SMALL("TED Talks captions"),
    .priv_data_size = sizeof(TEDCaptionsDemuxer),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .priv_class     = &tedcaptions_demuxer_class,
    .read_header    = tedcaptions_read_header,
    .read_packet    = tedcaptions_read_packet,
    .read_close     = tedcaptions_read_close,
    .read_probe     = tedcaptions_read_probe,
    .read_seek2     = tedcaptions_read_seek,
};
