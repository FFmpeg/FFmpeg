/*
 * copyright (c) 2009 Michael Niedermayer
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

AVMetadataTag *
av_metadata_get(AVMetadata *m, const char *key, const AVMetadataTag *prev, int flags)
{
    unsigned int i, j;

    if(!m)
        return NULL;

    if(prev) i= prev - m->elems + 1;
    else     i= 0;

    for(; i<m->count; i++){
        const char *s= m->elems[i].key;
        if(flags & AV_METADATA_MATCH_CASE) for(j=0;         s[j]  ==         key[j]  && key[j]; j++);
        else                               for(j=0; toupper(s[j]) == toupper(key[j]) && key[j]; j++);
        if(key[j])
            continue;
        if(s[j] && !(flags & AV_METADATA_IGNORE_SUFFIX))
            continue;
        return &m->elems[i];
    }
    return NULL;
}

int av_metadata_set2(AVMetadata **pm, const char *key, const char *value, int flags)
{
    AVMetadata *m= *pm;
    AVMetadataTag *tag= av_metadata_get(m, key, NULL, flags);

    if(!m)
        m=*pm= av_mallocz(sizeof(*m));

    if(tag){
        if (flags & AV_METADATA_DONT_OVERWRITE)
            return 0;
        av_free(tag->value);
        av_free(tag->key);
        *tag= m->elems[--m->count];
    }else{
        AVMetadataTag *tmp= av_realloc(m->elems, (m->count+1) * sizeof(*m->elems));
        if(tmp){
            m->elems= tmp;
        }else
            return AVERROR(ENOMEM);
    }
    if(value){
        if(flags & AV_METADATA_DONT_STRDUP_KEY){
            m->elems[m->count].key  = key;
        }else
        m->elems[m->count].key  = av_strdup(key  );
        if(flags & AV_METADATA_DONT_STRDUP_VAL){
            m->elems[m->count].value= value;
        }else
        m->elems[m->count].value= av_strdup(value);
        m->count++;
    }
    if(!m->count) {
        av_free(m->elems);
        av_freep(pm);
    }

    return 0;
}

#if FF_API_OLD_METADATA
int av_metadata_set(AVMetadata **pm, const char *key, const char *value)
{
    return av_metadata_set2(pm, key, value, 0);
}

void av_metadata_conv(AVFormatContext *ctx, const AVMetadataConv *d_conv,
                                            const AVMetadataConv *s_conv)
{
    return;
}
#endif

void av_metadata_free(AVMetadata **pm)
{
    AVMetadata *m= *pm;

    if(m){
        while(m->count--){
            av_free(m->elems[m->count].key);
            av_free(m->elems[m->count].value);
        }
        av_free(m->elems);
    }
    av_freep(pm);
}

void ff_metadata_conv(AVMetadata **pm, const AVMetadataConv *d_conv,
                                       const AVMetadataConv *s_conv)
{
    /* TODO: use binary search to look up the two conversion tables
       if the tables are getting big enough that it would matter speed wise */
    const AVMetadataConv *sc, *dc;
    AVMetadataTag *mtag = NULL;
    AVMetadata *dst = NULL;
    const char *key;

    if (d_conv == s_conv)
        return;

    while((mtag=av_metadata_get(*pm, "", mtag, AV_METADATA_IGNORE_SUFFIX))) {
        key = mtag->key;
        if (s_conv)
            for (sc=s_conv; sc->native; sc++)
                if (!strcasecmp(key, sc->native)) {
                    key = sc->generic;
                    break;
                }
        if (d_conv)
            for (dc=d_conv; dc->native; dc++)
                if (!strcasecmp(key, dc->generic)) {
                    key = dc->native;
                    break;
                }
        av_metadata_set2(&dst, key, mtag->value, 0);
    }
    av_metadata_free(pm);
    *pm = dst;
}

void ff_metadata_conv_ctx(AVFormatContext *ctx, const AVMetadataConv *d_conv,
                                                const AVMetadataConv *s_conv)
{
    int i;
    ff_metadata_conv(&ctx->metadata, d_conv, s_conv);
    for (i=0; i<ctx->nb_streams ; i++)
        ff_metadata_conv(&ctx->streams [i]->metadata, d_conv, s_conv);
    for (i=0; i<ctx->nb_chapters; i++)
        ff_metadata_conv(&ctx->chapters[i]->metadata, d_conv, s_conv);
    for (i=0; i<ctx->nb_programs; i++)
        ff_metadata_conv(&ctx->programs[i]->metadata, d_conv, s_conv);
}

void av_metadata_copy(AVMetadata **dst, AVMetadata *src, int flags)
{
    AVMetadataTag *t = NULL;

    while ((t = av_metadata_get(src, "", t, AV_METADATA_IGNORE_SUFFIX)))
        av_metadata_set2(dst, t->key, t->value, flags);
}
