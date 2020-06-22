/*
 * MicroDVD subtitle demuxer
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (c) 2012  Clément Bœsch <u pkh me>
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
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#define MAX_LINESIZE 2048


typedef struct {
    const AVClass *class;
    FFDemuxSubtitlesQueue q;
    AVRational frame_rate;
} MicroDVDContext;


static int microdvd_probe(const AVProbeData *p)
{
    unsigned char c;
    const uint8_t *ptr = p->buf;
    int i;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */

    for (i=0; i<3; i++) {
        if (sscanf(ptr, "{%*d}{}%c",     &c) != 1 &&
            sscanf(ptr, "{%*d}{%*d}%c",  &c) != 1 &&
            sscanf(ptr, "{DEFAULT}{}%c", &c) != 1)
            return 0;
        ptr += ff_subtitles_next_line(ptr);
    }
    return AVPROBE_SCORE_MAX;
}

static int64_t get_pts(const char *buf)
{
    int frame;
    char c;

    if (sscanf(buf, "{%d}{%c", &frame, &c) == 2)
        return frame;
    return AV_NOPTS_VALUE;
}

static int get_duration(const char *buf)
{
    int frame_start, frame_end;

    if (sscanf(buf, "{%d}{%d}", &frame_start, &frame_end) == 2)
        return frame_end - frame_start;
    return -1;
}

static const char *bom = "\xEF\xBB\xBF";

static int microdvd_read_header(AVFormatContext *s)
{
    AVRational pts_info = (AVRational){ 2997, 125 };  /* default: 23.976 fps */
    MicroDVDContext *microdvd = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    int i = 0, ret;
    char line_buf[MAX_LINESIZE];
    int has_real_fps = 0;

    if (!st)
        return AVERROR(ENOMEM);

    while (!avio_feof(s->pb)) {
        char *p;
        AVPacket *sub;
        int64_t pos = avio_tell(s->pb);
        int len = ff_get_line(s->pb, line_buf, sizeof(line_buf));
        char *line = line_buf;
        int64_t pts;

        if (!strncmp(line, bom, 3))
            line += 3;
        p = line;

        if (!len)
            break;
        line[strcspn(line, "\r\n")] = 0;
        if (!*p)
            continue;
        if (i++ < 3) {
            int frame;
            double fps;
            char c;

            if ((sscanf(line, "{%d}{}%6lf",    &frame, &fps) == 2 ||
                 sscanf(line, "{%d}{%*d}%6lf", &frame, &fps) == 2)
                && frame <= 1 && fps > 3 && fps < 100) {
                pts_info = av_d2q(fps, 100000);
                has_real_fps = 1;
                continue;
            }
            if (!st->codecpar->extradata && sscanf(line, "{DEFAULT}{}%c", &c) == 1) {
                int size = strlen(line + 11);
                ret = ff_alloc_extradata(st->codecpar, size);
                if (ret < 0)
                    goto fail;
                memcpy(st->codecpar->extradata, line + 11, size);
                continue;
            }
        }
#define SKIP_FRAME_ID                                       \
    p = strchr(p, '}');                                     \
    if (!p) {                                               \
        av_log(s, AV_LOG_WARNING, "Invalid event \"%s\""    \
               " at line %d\n", line, i);                   \
        continue;                                           \
    }                                                       \
    p++
        SKIP_FRAME_ID;
        SKIP_FRAME_ID;
        if (!*p)
            continue;
        pts = get_pts(line);
        if (pts == AV_NOPTS_VALUE)
            continue;
        sub = ff_subtitles_queue_insert(&microdvd->q, p, strlen(p), 0);
        if (!sub) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        sub->pos = pos;
        sub->pts = pts;
        sub->duration = get_duration(line);
    }
    ff_subtitles_queue_finalize(s, &microdvd->q);
    if (has_real_fps) {
        /* export the FPS info only if set in the file */
        microdvd->frame_rate = pts_info;
    } else if (microdvd->frame_rate.num) {
        /* fallback on user specified frame rate */
        pts_info = microdvd->frame_rate;
    }
    avpriv_set_pts_info(st, 64, pts_info.den, pts_info.num);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_MICRODVD;
    return 0;
fail:
    ff_subtitles_queue_clean(&microdvd->q);
    return ret;
}

static int microdvd_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MicroDVDContext *microdvd = s->priv_data;
    return ff_subtitles_queue_read_packet(&microdvd->q, pkt);
}

static int microdvd_read_seek(AVFormatContext *s, int stream_index,
                             int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    MicroDVDContext *microdvd = s->priv_data;
    return ff_subtitles_queue_seek(&microdvd->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int microdvd_read_close(AVFormatContext *s)
{
    MicroDVDContext *microdvd = s->priv_data;
    ff_subtitles_queue_clean(&microdvd->q);
    return 0;
}


#define OFFSET(x) offsetof(MicroDVDContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM|AV_OPT_FLAG_DECODING_PARAM
static const AVOption microdvd_options[] = {
    { "subfps", "set the movie frame rate fallback", OFFSET(frame_rate), AV_OPT_TYPE_RATIONAL, {.dbl=0}, 0, INT_MAX, SD },
    { NULL }
};

static const AVClass microdvd_class = {
    .class_name = "microdvddec",
    .item_name  = av_default_item_name,
    .option     = microdvd_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_microdvd_demuxer = {
    .name           = "microdvd",
    .long_name      = NULL_IF_CONFIG_SMALL("MicroDVD subtitle format"),
    .priv_data_size = sizeof(MicroDVDContext),
    .read_probe     = microdvd_probe,
    .read_header    = microdvd_read_header,
    .read_packet    = microdvd_read_packet,
    .read_seek2     = microdvd_read_seek,
    .read_close     = microdvd_read_close,
    .priv_class     = &microdvd_class,
};
