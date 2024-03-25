/*
 * SSA/ASS muxer
 * Copyright (c) 2008 Michael Niedermayer
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

#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "mux.h"

#include "libavutil/opt.h"

typedef struct DialogueLine {
    int readorder;
    char *line;
    struct DialogueLine *prev, *next;
} DialogueLine;

typedef struct ASSContext {
    const AVClass *class;
    int expected_readorder;
    DialogueLine *dialogue_cache;
    DialogueLine *last_added_dialogue;
    int cache_size;
    int ssa_mode;
    int ignore_readorder;
    uint8_t *trailer;
    size_t trailer_size;
} ASSContext;

static int write_header(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;

    avpriv_set_pts_info(s->streams[0], 64, 1, 100);
    if (par->extradata_size > 0) {
        size_t header_size = par->extradata_size;
        uint8_t *trailer = strstr(par->extradata, "\n[Events]");

        if (trailer)
            trailer = strstr(trailer, "Format:");
        if (trailer)
            trailer = strstr(trailer, "\n");

        if (trailer) {
            header_size = (++trailer - par->extradata);
            ass->trailer_size = par->extradata_size - header_size;
            if (ass->trailer_size)
                ass->trailer = trailer;
        }

        ffio_write_lines(s->pb, par->extradata, header_size, NULL);

        ass->ssa_mode = !strstr(par->extradata, "\n[V4+ Styles]");
        if (!strstr(par->extradata, "\n[Events]"))
            avio_printf(s->pb, "[Events]\nFormat: %s, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n",
                        ass->ssa_mode ? "Marked" : "Layer");
    }

    return 0;
}

static void purge_dialogues(AVFormatContext *s, int force)
{
    int n = 0;
    ASSContext *ass = s->priv_data;
    DialogueLine *dialogue = ass->dialogue_cache;

    while (dialogue && (dialogue->readorder == ass->expected_readorder || force)) {
        DialogueLine *next = dialogue->next;
        if (dialogue->readorder != ass->expected_readorder) {
            av_log(s, AV_LOG_WARNING, "ReadOrder gap found between %d and %d\n",
                   ass->expected_readorder, dialogue->readorder);
            ass->expected_readorder = dialogue->readorder;
        }
        avio_print(s->pb, "Dialogue: ", dialogue->line, "\n");
        if (dialogue == ass->last_added_dialogue)
            ass->last_added_dialogue = next;
        av_freep(&dialogue->line);
        av_free(dialogue);
        if (next)
            next->prev = NULL;
        dialogue = ass->dialogue_cache = next;
        ass->expected_readorder++;
        n++;
    }
    ass->cache_size -= n;
    if (n > 1)
        av_log(s, AV_LOG_DEBUG, "wrote %d ASS lines, cached dialogues: %d, waiting for event id %d\n",
               n, ass->cache_size, ass->expected_readorder);
}

static void insert_dialogue(ASSContext *ass, DialogueLine *dialogue)
{
    DialogueLine *cur, *next = NULL, *prev = NULL;

    /* from the last added to the end of the list */
    if (ass->last_added_dialogue) {
        for (cur = ass->last_added_dialogue; cur; cur = cur->next) {
            if (cur->readorder > dialogue->readorder)
                break;
            prev = cur;
            next = cur->next;
        }
    }

    /* from the beginning to the last one added */
    if (!prev) {
        next = ass->dialogue_cache;
        for (cur = next; cur != ass->last_added_dialogue; cur = cur->next) {
            if (cur->readorder > dialogue->readorder)
                break;
            prev = cur;
            next = cur->next;
        }
    }

    if (prev) {
        prev->next = dialogue;
        dialogue->prev = prev;
    } else {
        dialogue->prev = ass->dialogue_cache;
        ass->dialogue_cache = dialogue;
    }
    if (next) {
        next->prev = dialogue;
        dialogue->next = next;
    }
    ass->cache_size++;
    ass->last_added_dialogue = dialogue;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASSContext *ass = s->priv_data;

    long int layer;
    int text_len;
    char *p = pkt->data;
    int64_t start = pkt->pts;
    int64_t end   = start + pkt->duration;
    int hh1, mm1, ss1, ms1;
    int hh2, mm2, ss2, ms2;
    DialogueLine *dialogue = av_mallocz(sizeof(*dialogue));

    if (!dialogue)
        return AVERROR(ENOMEM);

    dialogue->readorder = strtol(p, &p, 10);
    if (dialogue->readorder < ass->expected_readorder)
        av_log(s, AV_LOG_WARNING, "Unexpected ReadOrder %d\n",
               dialogue->readorder);
    if (*p == ',')
        p++;

    if (ass->ssa_mode && !strncmp(p, "Marked=", 7))
        p += 7;

    layer = strtol(p, &p, 10);
    if (*p == ',')
        p++;
    hh1 = (int)(start / 360000);    mm1 = (int)(start / 6000) % 60;
    hh2 = (int)(end   / 360000);    mm2 = (int)(end   / 6000) % 60;
    ss1 = (int)(start / 100) % 60;  ms1 = (int)(start % 100);
    ss2 = (int)(end   / 100) % 60;  ms2 = (int)(end   % 100);
    if (hh1 > 9) hh1 = 9, mm1 = 59, ss1 = 59, ms1 = 99;
    if (hh2 > 9) hh2 = 9, mm2 = 59, ss2 = 59, ms2 = 99;

    text_len = strlen(p);
    while (text_len > 0 && p[text_len - 1] == '\r' || p[text_len - 1] == '\n')
        text_len--;

    dialogue->line = av_asprintf("%s%ld,%d:%02d:%02d.%02d,%d:%02d:%02d.%02d,%.*s",
                                 ass->ssa_mode ? "Marked=" : "",
                                 layer, hh1, mm1, ss1, ms1, hh2, mm2, ss2, ms2, text_len, p);
    if (!dialogue->line) {
        av_free(dialogue);
        return AVERROR(ENOMEM);
    }
    insert_dialogue(ass, dialogue);
    purge_dialogues(s, ass->ignore_readorder);

    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;

    purge_dialogues(s, 1);

    if (ass->trailer) {
        ffio_write_lines(s->pb, ass->trailer, ass->trailer_size, NULL);
    }

    return 0;
}

#define OFFSET(x) offsetof(ASSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "ignore_readorder", "write events immediately, even if they're out-of-order", OFFSET(ignore_readorder), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, E },
    { NULL },
};

static const AVClass ass_class = {
    .class_name = "ass muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_ass_muxer = {
    .p.name           = "ass",
    .p.long_name      = NULL_IF_CONFIG_SMALL("SSA (SubStation Alpha) subtitle"),
    .p.mime_type      = "text/x-ass",
    .p.extensions     = "ass,ssa",
    .p.audio_codec    = AV_CODEC_ID_NONE,
    .p.video_codec    = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_ASS,
    .p.flags          = AVFMT_GLOBALHEADER | AVFMT_NOTIMESTAMPS | AVFMT_TS_NONSTRICT,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .p.priv_class     = &ass_class,
    .priv_data_size = sizeof(ASSContext),
    .write_header   = write_header,
    .write_packet   = write_packet,
    .write_trailer  = write_trailer,
};
