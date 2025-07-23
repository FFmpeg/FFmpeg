/*
 * LRC lyrics file format muxer
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
#include "mux.h"
#include "version.h"
#include "libavutil/dict.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/opt.h"

typedef struct LRCSubtitleContext {
    const AVClass *class;
    int precision; ///< precision of the fractional part of the timestamp, 2 for centiseconds
} LRCSubtitleContext;

static int lrc_write_header(AVFormatContext *s)
{
    const AVDictionaryEntry *metadata_item;

    if(s->streams[0]->codecpar->codec_id != AV_CODEC_ID_SUBRIP &&
       s->streams[0]->codecpar->codec_id != AV_CODEC_ID_TEXT) {
        av_log(s, AV_LOG_ERROR, "Unsupported subtitle codec: %s\n",
               avcodec_get_name(s->streams[0]->codecpar->codec_id));
        return AVERROR(EINVAL);
    }
    avpriv_set_pts_info(s->streams[0], 64, 1, AV_TIME_BASE);

    ff_standardize_creation_time(s);
    ff_metadata_conv_ctx(s, ff_lrc_metadata_conv, NULL);
    if(!(s->flags & AVFMT_FLAG_BITEXACT)) { // avoid breaking regression tests
        /* LRC provides a metadata slot for specifying encoder version
         * in addition to encoder name. We will store LIBAVFORMAT_VERSION
         * to it.
         */
        av_dict_set(&s->metadata, "ve", AV_STRINGIFY(LIBAVFORMAT_VERSION), 0);
    } else {
        av_dict_set(&s->metadata, "ve", NULL, 0);
    }
    for(metadata_item = NULL;
       (metadata_item = av_dict_iterate(s->metadata, metadata_item));) {
        char *delim;
        if(!metadata_item->value[0]) {
            continue;
        }
        while((delim = strchr(metadata_item->value, '\n'))) {
            *delim = ' ';
        }
        while((delim = strchr(metadata_item->value, '\r'))) {
            *delim = ' ';
        }
        avio_printf(s->pb, "[%s:%s]\n",
                    metadata_item->key, metadata_item->value);
    }
    avio_w8(s->pb, '\n');
    return 0;
}

static int lrc_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    const LRCSubtitleContext *p = s->priv_data;

    if(pkt->pts != AV_NOPTS_VALUE) {
        const uint8_t *line = pkt->data;
        const uint8_t *end  = pkt->data + pkt->size;

        while (end > line && (end[-1] == '\n' || end[-1] == '\r'))
            end--;
        if (line != end) {
            while (line[0] == '\n' || line[0] == '\r')
                line++; // Skip first empty lines
        }

        int frac_mult = 1;
        for (int i = 0; i < p->precision; ++i)
            frac_mult *= 10;

        while(line) {
            const uint8_t *next_line = memchr(line, '\n', end - line);
            size_t size = end - line;

            if (next_line) {
                size = next_line - line;
                if (next_line > line && next_line[-1] == '\r')
                    size--;
                next_line++;
            }
            if (size && line[0] == '[') {
                av_log(s, AV_LOG_WARNING,
                       "Subtitle starts with '[', may cause problems with LRC format.\n");
            }

            /* Offset feature of LRC can easily make pts negative,
             * we just output it directly and let the player drop it. */
            uint64_t abs_pts = FFABS64U(pkt->pts);
            uint64_t minutes = abs_pts / (60 * AV_TIME_BASE);
            uint64_t seconds = (abs_pts / AV_TIME_BASE) % 60;
            uint64_t fraction = abs_pts % AV_TIME_BASE;
            uint64_t rescaled = av_rescale_q(fraction, AV_TIME_BASE_Q, (AVRational){1, frac_mult});
            avio_write(s->pb, "[-", 1 + (pkt->pts < 0));
            avio_printf(s->pb, "%02"PRIu64":%02"PRIu64".%0*"PRIu64"]",
                        minutes, seconds, p->precision, rescaled);

            avio_write(s->pb, line, size);
            avio_w8(s->pb, '\n');
            line = next_line;
        }
    }
    return 0;
}

#define OFFSET(x) offsetof(LRCSubtitleContext, x)
#define SE AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"precision", "precision of the fractional part of the timestamp, 2 for centiseconds", OFFSET(precision), AV_OPT_TYPE_INT, {.i64 = 2}, 1, 6, SE},
    { NULL },
};

static const AVClass lrcenc_class = {
    .class_name = "lrc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_lrc_muxer = {
    .p.name           = "lrc",
    .p.long_name      = NULL_IF_CONFIG_SMALL("LRC lyrics"),
    .p.extensions     = "lrc",
    .p.flags          = AVFMT_VARIABLE_FPS | AVFMT_GLOBALHEADER |
                        AVFMT_TS_NEGATIVE | AVFMT_TS_NONSTRICT,
    .p.video_codec    = AV_CODEC_ID_NONE,
    .p.audio_codec    = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_SUBRIP,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH,
    .priv_data_size = sizeof(LRCSubtitleContext),
    .write_header   = lrc_write_header,
    .write_packet   = lrc_write_packet,
    .p.priv_class   = &lrcenc_class,
};
