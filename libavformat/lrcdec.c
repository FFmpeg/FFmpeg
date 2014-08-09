/*
 * LRC lyrics file format decoder
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
    uint64_t mm, ss, cs;

    while(p[offset] == ' ' || p[offset] == '\t') {
        offset++;
    }
    if(p[offset] != '[') {
        return 0;
    }
    if(sscanf(p, "[-%"SCNu64":%"SCNu64".%"SCNu64"]", &mm, &ss, &cs) == 3) {
        /* Just in case negative pts, players may drop it but we won't. */
        *start = -(int64_t) (mm*60000 + ss*1000 + cs*10);
    } else if(sscanf(p, "[%"SCNu64":%"SCNu64".%"SCNu64"]", &mm, &ss, &cs) == 3) {
        *start = mm*60000 + ss*1000 + cs*10;
    } else {
        return 0;
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

static int lrc_probe(AVProbeData *p)
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
    avpriv_set_pts_info(st, 64, 1, 1000);
    lrc->ts_offset = 0;
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_TEXT;
    av_bprint_init(&line, 0, AV_BPRINT_SIZE_UNLIMITED);

    while(!avio_feof(s->pb)) {
        int64_t pos = read_line(&line, s->pb);
        int64_t header_offset = find_header(line.str);
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
                ts_stroffset += ts_stroffset_incr;
                sub = ff_subtitles_queue_insert(&lrc->q, line.str + ts_strlength,
                                                line.len - ts_strlength, 0);
                if(!sub) {
                    return AVERROR(ENOMEM);
                }
                sub->pos = pos;
                sub->pts = ts_start - lrc->ts_offset;
                sub->duration = -1;
            }
        }
    }
    ff_subtitles_queue_finalize(&lrc->q);
    ff_metadata_conv_ctx(s, NULL, ff_lrc_metadata_conv);
    return 0;
}

static int lrc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    LRCContext *lrc = s->priv_data;
    return ff_subtitles_queue_read_packet(&lrc->q, pkt);
}

static int lrc_read_seek(AVFormatContext *s, int stream_index,
                         int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    LRCContext *lrc = s->priv_data;
    return ff_subtitles_queue_seek(&lrc->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int lrc_read_close(AVFormatContext *s)
{
    LRCContext *lrc = s->priv_data;
    ff_subtitles_queue_clean(&lrc->q);
    return 0;
}

AVInputFormat ff_lrc_demuxer = {
    .name           = "lrc",
    .long_name      = NULL_IF_CONFIG_SMALL("LRC lyrics"),
    .priv_data_size = sizeof (LRCContext),
    .read_probe     = lrc_probe,
    .read_header    = lrc_read_header,
    .read_packet    = lrc_read_packet,
    .read_close     = lrc_read_close,
    .read_seek2     = lrc_read_seek
};
