/*
 * Copyright (c) 2009  Aurelien Jacobs <aurel@gnuage.org>
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

#include <strings.h>
#include "avformat.h"
#include "metadata.h"
#include "libavutil/avstring.h"

#if LIBAVFORMAT_VERSION_MAJOR < 53

#define SIZE_OFFSET(x) sizeof(((AVFormatContext*)0)->x),offsetof(AVFormatContext,x)

static const struct {
    const char name[16];
    int   size;
    int   offset;
} compat_tab[] = {
    { "title",           SIZE_OFFSET(title)     },
    { "author",          SIZE_OFFSET(author)    },
    { "copyright",       SIZE_OFFSET(copyright) },
    { "comment",         SIZE_OFFSET(comment)   },
    { "album",           SIZE_OFFSET(album)     },
    { "year",            SIZE_OFFSET(year)      },
    { "track",           SIZE_OFFSET(track)     },
    { "genre",           SIZE_OFFSET(genre)     },

    { "artist",          SIZE_OFFSET(author)    },
    { "creator",         SIZE_OFFSET(author)    },
    { "written_by",      SIZE_OFFSET(author)    },
    { "lead_performer",  SIZE_OFFSET(author)    },
    { "composer",        SIZE_OFFSET(author)    },
    { "performer",       SIZE_OFFSET(author)    },
    { "description",     SIZE_OFFSET(comment)   },
    { "albumtitle",      SIZE_OFFSET(album)     },
    { "date",            SIZE_OFFSET(year)      },
    { "date_written",    SIZE_OFFSET(year)      },
    { "date_released",   SIZE_OFFSET(year)      },
    { "tracknumber",     SIZE_OFFSET(track)     },
    { "part_number",     SIZE_OFFSET(track)     },
};

void ff_metadata_demux_compat(AVFormatContext *ctx)
{
    AVMetadata *m;
    int i, j;

    if ((m = ctx->metadata))
        for (j=0; j<m->count; j++)
            for (i=0; i<FF_ARRAY_ELEMS(compat_tab); i++)
                if (!strcasecmp(m->elems[j].key, compat_tab[i].name)) {
                    int *ptr = (int *)((char *)ctx+compat_tab[i].offset);
                    if (*ptr)  continue;
                    if (compat_tab[i].size > sizeof(int))
                        av_strlcpy((char *)ptr, m->elems[j].value, compat_tab[i].size);
                    else
                        *ptr = atoi(m->elems[j].value);
                }

    for (i=0; i<ctx->nb_chapters; i++)
        if ((m = ctx->chapters[i]->metadata))
            for (j=0; j<m->count; j++)
                if (!strcasecmp(m->elems[j].key, "title")) {
                    av_free(ctx->chapters[i]->title);
                    ctx->chapters[i]->title = av_strdup(m->elems[j].value);
                }

    for (i=0; i<ctx->nb_programs; i++)
        if ((m = ctx->programs[i]->metadata))
            for (j=0; j<m->count; j++) {
                if (!strcasecmp(m->elems[j].key, "name")) {
                    av_free(ctx->programs[i]->name);
                    ctx->programs[i]->name = av_strdup(m->elems[j].value);
                }
                if (!strcasecmp(m->elems[j].key, "provider_name")) {
                    av_free(ctx->programs[i]->provider_name);
                    ctx->programs[i]->provider_name = av_strdup(m->elems[j].value);
                }
            }

    for (i=0; i<ctx->nb_streams; i++)
        if ((m = ctx->streams[i]->metadata))
            for (j=0; j<m->count; j++) {
                if (!strcasecmp(m->elems[j].key, "language"))
                    av_strlcpy(ctx->streams[i]->language, m->elems[j].value, 4);
                if (!strcasecmp(m->elems[j].key, "filename")) {
                    av_free(ctx->streams[i]->filename);
                    ctx->streams[i]->filename= av_strdup(m->elems[j].value);
                }
            }
}


#define FILL_METADATA(s, key, value) {                                        \
    if (value && *value && !av_metadata_get(s->metadata, #key, NULL, 0))      \
        av_metadata_set(&s->metadata, #key, value);                           \
    }
#define FILL_METADATA_STR(s, key)  FILL_METADATA(s, key, s->key)
#define FILL_METADATA_INT(s, key) {                                           \
    char number[10];                                                          \
    snprintf(number, sizeof(number), "%d", s->key);                           \
    if(s->key)  FILL_METADATA(s, key, number) }

void ff_metadata_mux_compat(AVFormatContext *ctx)
{
    int i;

    if (ctx->metadata && ctx->metadata->count > 0)
        return;

    FILL_METADATA_STR(ctx, title);
    FILL_METADATA_STR(ctx, author);
    FILL_METADATA_STR(ctx, copyright);
    FILL_METADATA_STR(ctx, comment);
    FILL_METADATA_STR(ctx, album);
    FILL_METADATA_INT(ctx, year);
    FILL_METADATA_INT(ctx, track);
    FILL_METADATA_STR(ctx, genre);
    for (i=0; i<ctx->nb_chapters; i++)
        FILL_METADATA_STR(ctx->chapters[i], title);
    for (i=0; i<ctx->nb_programs; i++) {
        FILL_METADATA_STR(ctx->programs[i], name);
        FILL_METADATA_STR(ctx->programs[i], provider_name);
    }
    for (i=0; i<ctx->nb_streams; i++) {
        FILL_METADATA_STR(ctx->streams[i], language);
        FILL_METADATA_STR(ctx->streams[i], filename);
    }
}

#endif /* LIBAVFORMAT_VERSION_MAJOR < 53 */
