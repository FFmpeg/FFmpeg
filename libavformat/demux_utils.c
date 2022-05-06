/*
 * Various utility demuxing functions
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "libavutil/avassert.h"
#include "libavcodec/packet_internal.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

struct AVCodecParserContext *av_stream_get_parser(const AVStream *st)
{
    return cffstream(st)->parser;
}

void avpriv_stream_set_need_parsing(AVStream *st, enum AVStreamParseType type)
{
    ffstream(st)->need_parsing = type;
}

AVChapter *avpriv_new_chapter(AVFormatContext *s, int64_t id, AVRational time_base,
                              int64_t start, int64_t end, const char *title)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVChapter *chapter = NULL;
    int ret;

    if (end != AV_NOPTS_VALUE && start > end) {
        av_log(s, AV_LOG_ERROR, "Chapter end time %"PRId64" before start %"PRId64"\n", end, start);
        return NULL;
    }

    if (!s->nb_chapters) {
        si->chapter_ids_monotonic = 1;
    } else if (!si->chapter_ids_monotonic || s->chapters[s->nb_chapters-1]->id >= id) {
        for (unsigned i = 0; i < s->nb_chapters; i++)
            if (s->chapters[i]->id == id)
                chapter = s->chapters[i];
        if (!chapter)
            si->chapter_ids_monotonic = 0;
    }

    if (!chapter) {
        chapter = av_mallocz(sizeof(*chapter));
        if (!chapter)
            return NULL;
        ret = av_dynarray_add_nofree(&s->chapters, &s->nb_chapters, chapter);
        if (ret < 0) {
            av_free(chapter);
            return NULL;
        }
    }
    av_dict_set(&chapter->metadata, "title", title, 0);
    chapter->id        = id;
    chapter->time_base = time_base;
    chapter->start     = start;
    chapter->end       = end;

    return chapter;
}

void av_format_inject_global_side_data(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);
    si->inject_global_side_data = 1;
    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        ffstream(st)->inject_global_side_data = 1;
    }
}

int avformat_queue_attached_pictures(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);
    int ret;
    for (unsigned i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC &&
            s->streams[i]->discard < AVDISCARD_ALL) {
            if (s->streams[i]->attached_pic.size <= 0) {
                av_log(s, AV_LOG_WARNING,
                       "Attached picture on stream %d has invalid size, "
                       "ignoring\n", i);
                continue;
            }

            ret = avpriv_packet_list_put(&si->raw_packet_buffer,
                                         &s->streams[i]->attached_pic,
                                         av_packet_ref, 0);
            if (ret < 0)
                return ret;
        }
    return 0;
}

int ff_add_attached_pic(AVFormatContext *s, AVStream *st0, AVIOContext *pb,
                        AVBufferRef **buf, int size)
{
    AVStream *st = st0;
    AVPacket *pkt;
    int ret;

    if (!st && !(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);
    pkt = &st->attached_pic;
    if (buf) {
        av_assert1(*buf);
        av_packet_unref(pkt);
        pkt->buf  = *buf;
        pkt->data = (*buf)->data;
        pkt->size = (*buf)->size - AV_INPUT_BUFFER_PADDING_SIZE;
        *buf = NULL;
    } else {
        ret = av_get_packet(pb, pkt, size);
        if (ret < 0)
            goto fail;
    }
    st->disposition         |= AV_DISPOSITION_ATTACHED_PIC;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    pkt->stream_index = st->index;
    pkt->flags       |= AV_PKT_FLAG_KEY;

    return 0;
fail:
    if (!st0)
        ff_remove_stream(s, st);
    return ret;
}
