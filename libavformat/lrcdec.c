/*
 * LRC lyrics file format demuxer
 * Copyright (c) 2014 StarBrilliant <m13253@hotmail.com>
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

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "lrc.h"
#include "metadata.h"
#include "subtitles.h"
#include "libavutil/bprint.h"
#include "libavutil/dict.h"

typedef struct LRCContext {
    FFDemuxSubtitlesQueue q;
    int64_t ts_offset; // offset metadata item
} LRCContext;

static int64_t find_header(const char *p)
{
    int64_t offset = 0;
    while(p[offset] == ' ' || p[offset] == '\t') {
        offset++;
    }
    if(p[offset] == '[' && p[offset + 1] >= 'a' && p[offset + 1] <= 'z') {
        return offset;
    } else {
        return -1;
    }
}

static int64_t count_ts(const char *p)
{
    int64_t offset = 0;
    int in_brackets = 0;

    for(;;) {
        if(p[offset] == ' ' || p[offset] == '\t') {
            offset++;
        } else if(p[offset] == '[') {
            offset++;
            in_brackets++;
        } else if (p[offset] == ']' && in_brackets) {
            offset++;
            in_brackets--;
        } else if(in_brackets &&
                 (p[offset] == ':' || p[offset] == '.' || p[offset] == '-' ||
                 (p[offset] >= '0' && p[offset] <= '9'))) {
            offset++;
        } else {
            break;
        }
    }
    return offset;
}

static int64_t read_ts(const char *p, int64_t *start)
{
    int64_t offset = 0;
    uint64_t mm;
    double ss;
    char prefix[3];

    while(p[offset] == ' ' || p[offset] == '\t') {
        offset++;
    }
    if(p[offset] != '[') {
        return 0;
    }
    int ret = sscanf(p, "%2[[-]%"SCNu64":%lf]", prefix, &mm, &ss);
    if (ret != 3 || prefix[0] != '[') {
        return 0;
    }
    *start = (mm * 60 + ss) * AV_TIME_BASE;
    if (prefix[1] == '-') {
        *start = - *start;
    }
    do {
        offset++;
    } while(p[offset] && p[offset-1] != ']');
    return offset;
}

static int64_t read_line(AVBPrint *buf, AVIOContext *pb)
{
    int64_t pos = avio_tell(pb);

    av_bprint_clear(buf);
    while(!avio_feof(pb)) {
        int c = avio_r8(pb);
        if(c != '\r') {
            av_bprint_chars(buf, c, 1);
        }
        if(c == '\n') {
            break;
        }
    }
    return pos;
}

static int lrc_probe(const AVProbeData *p)
{
    int64_t offset = 0;
    int64_t mm;
    uint64_t ss, cs;
    const AVMetadataConv *metadata_item;

    if(!memcmp(p->buf, "\xef\xbb\xbf", 3)) { // Skip UTF-8 BOM header
        offset += 3;
    }
    while(p->buf[offset] == '\n' || p->buf[offset] == '\r') {
        offset++;
    }
    if(p->buf[offset] != '[') {
        return 0;
    }
    offset++;
    // Common metadata item but not exist in ff_lrc_metadata_conv
    if(!memcmp(p->buf + offset, "offset:", 7)) {
        return 40;
    }
    if(sscanf(p->buf + offset, "%"SCNd64":%"SCNu64".%"SCNu64"]",
              &mm, &ss, &cs) == 3) {
        return 50;
    }
    // Metadata items exist in ff_lrc_metadata_conv
    for(metadata_item = ff_lrc_metadata_conv;
        metadata_item->native; metadata_item++) {
        size_t metadata_item_len = strlen(metadata_item->native);
        if(p->buf[offset + metadata_item_len] == ':' &&
           !memcmp(p->buf + offset, metadata_item->native, metadata_item_len)) {
            return 40;
        }
    }
    return 5; // Give it 5 scores since it starts with a bracket
}

static int lrc_read_header(AVFormatContext *s)
{
    LRCContext *lrc = s->priv_data;
    AVBPrint line;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if(!st) {
        return AVERROR(ENOMEM);
    }
    avpriv_set_pts_info(st, 64, 1, AV_TIME_BASE);
    lrc->ts_offset = 0;
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_TEXT;
    av_bprint_init(&line, 0, AV_BPRINT_SIZE_UNLIMITED);

    while(!avio_feof(s->pb)) {
        int64_t header_offset, pos = read_line(&line, s->pb);

        if (!av_bprint_is_complete(&line))
            goto err_nomem_out;
        header_offset = find_header(line.str);
        if(header_offset >= 0) {
            char *comma_offset = strchr(line.str, ':');
            if(comma_offset) {
                char *right_bracket_offset = strchr(line.str, ']');
                if(!right_bracket_offset) {
                    continue;
                }

                *right_bracket_offset = *comma_offset = '\0';
                if(strcmp(line.str + 1, "offset") ||
                   sscanf(comma_offset + 1, "%"SCNd64, &lrc->ts_offset) != 1) {
                    av_dict_set(&s->metadata, line.str + 1, comma_offset + 1, 0);
                }
                lrc->ts_offset = av_clip64(lrc->ts_offset, INT64_MIN/4, INT64_MAX/4);

                *comma_offset = ':';
                *right_bracket_offset = ']';
            }

        } else {
            AVPacket *sub;
            int64_t ts_start = AV_NOPTS_VALUE;
            int64_t ts_stroffset = 0;
            int64_t ts_stroffset_incr = 0;
            int64_t ts_strlength = count_ts(line.str);

            while((ts_stroffset_incr = read_ts(line.str + ts_stroffset,
                                               &ts_start)) != 0) {
                ts_start = av_clip64(ts_start, INT64_MIN/4, INT64_MAX/4);
                ts_stroffset += ts_stroffset_incr;
                sub = ff_subtitles_queue_insert(&lrc->q, line.str + ts_strlength,
                                                line.len - ts_strlength, 0);
                if (!sub)
                    goto err_nomem_out;
                sub->pos = pos;
                sub->pts = ts_start - lrc->ts_offset;
                sub->duration = -1;
            }
        }
    }
    ff_subtitles_queue_finalize(s, &lrc->q);
    ff_metadata_conv_ctx(s, NULL, ff_lrc_metadata_conv);
    av_bprint_finalize(&line, NULL);
    return 0;
err_nomem_out:
    av_bprint_finalize(&line, NULL);
    return AVERROR(ENOMEM);
}

const FFInputFormat ff_lrc_demuxer = {
    .p.name         = "lrc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("LRC lyrics"),
    .priv_data_size = sizeof (LRCContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = lrc_probe,
    .read_header    = lrc_read_header,
    .read_packet    = ff_subtitles_read_packet,
    .read_close     = ff_subtitles_read_close,
    .read_seek2     = ff_subtitles_read_seek
};
