/*
 * Apple HTTP Live Streaming segmenter
 * Copyright (c) 2012, Luca Barbato
 * Copyright (c) 2017 Akamai Technologies, Inc.
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

#include "config.h"
#include <stdint.h>

#include "libavutil/time_internal.h"

#include "avformat.h"
#include "hlsplaylist.h"

void ff_hls_write_playlist_version(AVIOContext *out, int version) {
    if (!out)
        return;
    avio_printf(out, "#EXTM3U\n");
    avio_printf(out, "#EXT-X-VERSION:%d\n", version);
}

void ff_hls_write_audio_rendition(AVIOContext *out, char *agroup,
                                  char *filename, char *language, int name_id, int is_default) {
    if (!out || !agroup || !filename)
        return;

    avio_printf(out, "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"group_%s\"", agroup);
    avio_printf(out, ",NAME=\"audio_%d\",DEFAULT=%s,", name_id, is_default ? "YES" : "NO");
    if (language) {
        avio_printf(out, "LANGUAGE=\"%s\",", language);
    }
    avio_printf(out, "URI=\"%s\"\n", filename);
}

void ff_hls_write_stream_info(AVStream *st, AVIOContext *out,
                              int bandwidth, char *filename, char *agroup,
                              char *codecs, char *ccgroup) {

    if (!out || !filename)
        return;

    if (!bandwidth) {
        av_log(NULL, AV_LOG_WARNING,
                "Bandwidth info not available, set audio and video bitrates\n");
        return;
    }

    avio_printf(out, "#EXT-X-STREAM-INF:BANDWIDTH=%d", bandwidth);
    if (st && st->codecpar->width > 0 && st->codecpar->height > 0)
        avio_printf(out, ",RESOLUTION=%dx%d", st->codecpar->width,
                st->codecpar->height);
    if (codecs && strlen(codecs) > 0)
        avio_printf(out, ",CODECS=\"%s\"", codecs);
    if (agroup && strlen(agroup) > 0)
        avio_printf(out, ",AUDIO=\"group_%s\"", agroup);
    if (ccgroup && strlen(ccgroup) > 0)
        avio_printf(out, ",CLOSED-CAPTIONS=\"%s\"", ccgroup);
    avio_printf(out, "\n%s\n\n", filename);
}

void ff_hls_write_playlist_header(AVIOContext *out, int version, int allowcache,
                                  int target_duration, int64_t sequence,
                                  uint32_t playlist_type) {
    if (!out)
        return;
    ff_hls_write_playlist_version(out, version);
    if (allowcache == 0 || allowcache == 1) {
        avio_printf(out, "#EXT-X-ALLOW-CACHE:%s\n", allowcache == 0 ? "NO" : "YES");
    }
    avio_printf(out, "#EXT-X-TARGETDURATION:%d\n", target_duration);
    avio_printf(out, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);
    av_log(NULL, AV_LOG_VERBOSE, "EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);

    if (playlist_type == PLAYLIST_TYPE_EVENT) {
        avio_printf(out, "#EXT-X-PLAYLIST-TYPE:EVENT\n");
    } else if (playlist_type == PLAYLIST_TYPE_VOD) {
        avio_printf(out, "#EXT-X-PLAYLIST-TYPE:VOD\n");
    }
}

void ff_hls_write_init_file(AVIOContext *out, char *filename,
                            int byterange_mode, int64_t size, int64_t pos) {
    avio_printf(out, "#EXT-X-MAP:URI=\"%s\"", filename);
    if (byterange_mode) {
        avio_printf(out, ",BYTERANGE=\"%"PRId64"@%"PRId64"\"", size, pos);
    }
    avio_printf(out, "\n");
}

int ff_hls_write_file_entry(AVIOContext *out, int insert_discont,
                             int byterange_mode,
                             double duration, int round_duration,
                             int64_t size, int64_t pos, //Used only if HLS_SINGLE_FILE flag is set
                             char *baseurl, //Ignored if NULL
                             char *filename, double *prog_date_time) {
    if (!out || !filename)
        return AVERROR(EINVAL);

    if (insert_discont) {
        avio_printf(out, "#EXT-X-DISCONTINUITY\n");
    }
    if (round_duration)
        avio_printf(out, "#EXTINF:%ld,\n",  lrint(duration));
    else
        avio_printf(out, "#EXTINF:%f,\n", duration);
    if (byterange_mode)
        avio_printf(out, "#EXT-X-BYTERANGE:%"PRId64"@%"PRId64"\n", size, pos);

    if (prog_date_time) {
        time_t tt, wrongsecs;
        int milli;
        struct tm *tm, tmpbuf;
        char buf0[128], buf1[128];
        tt = (int64_t)*prog_date_time;
        milli = av_clip(lrint(1000*(*prog_date_time - tt)), 0, 999);
        tm = localtime_r(&tt, &tmpbuf);
        if (!strftime(buf0, sizeof(buf0), "%Y-%m-%dT%H:%M:%S", tm)) {
            av_log(NULL, AV_LOG_DEBUG, "strftime error in ff_hls_write_file_entry\n");
            return AVERROR_UNKNOWN;
        }
        if (!strftime(buf1, sizeof(buf1), "%z", tm) || buf1[1]<'0' ||buf1[1]>'2') {
            int tz_min, dst = tm->tm_isdst;
            tm = gmtime_r(&tt, &tmpbuf);
            tm->tm_isdst = dst;
            wrongsecs = mktime(tm);
            tz_min = (FFABS(wrongsecs - tt) + 30) / 60;
            snprintf(buf1, sizeof(buf1),
                     "%c%02d%02d",
                     wrongsecs <= tt ? '+' : '-',
                     tz_min / 60,
                     tz_min % 60);
        }
        avio_printf(out, "#EXT-X-PROGRAM-DATE-TIME:%s.%03d%s\n", buf0, milli, buf1);
        *prog_date_time += duration;
    }
    if (baseurl)
        avio_printf(out, "%s", baseurl);
    avio_printf(out, "%s\n", filename);

    return 0;
}

void ff_hls_write_end_list (AVIOContext *out) {
    if (!out)
        return;
    avio_printf(out, "#EXT-X-ENDLIST\n");
}

