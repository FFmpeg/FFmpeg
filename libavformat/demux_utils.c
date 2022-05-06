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
