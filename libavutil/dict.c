/*
 * copyright (c) 2009 Michael Niedermayer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>

#include "avstring.h"
#include "dict.h"
#include "internal.h"
#include "mem.h"

struct AVDictionary {
    int count;
    AVDictionaryEntry *elems;
};

int av_dict_count(const AVDictionary *m)
{
    return m ? m->count : 0;
}

AVDictionaryEntry *av_dict_get(const AVDictionary *m, const char *key,
                               const AVDictionaryEntry *prev, int flags)
{
    unsigned int i, j;

    if (!m)
        return NULL;

    if (prev)
        i = prev - m->elems + 1;
    else
        i = 0;

    for (; i < m->count; i++) {
        const char *s = m->elems[i].key;
        if (flags & AV_DICT_MATCH_CASE)
            for (j = 0; s[j] == key[j] && key[j]; j++)
                ;
        else
            for (j = 0; av_toupper(s[j]) == av_toupper(key[j]) && key[j]; j++)
                ;
        if (key[j])
            continue;
        if (s[j] && !(flags & AV_DICT_IGNORE_SUFFIX))
            continue;
        return &m->elems[i];
    }
    return NULL;
}

int av_dict_set(AVDictionary **pm, const char *key, const char *value,
                int flags)
{
    AVDictionary *m = *pm;
    AVDictionaryEntry *tag = av_dict_get(m, key, NULL, flags);
    char *oldval = NULL;

    if (!m)
        m = *pm = av_mallocz(sizeof(*m));

    if (tag) {
        if (flags & AV_DICT_DONT_OVERWRITE) {
            if (flags & AV_DICT_DONT_STRDUP_KEY) av_free(key);
            if (flags & AV_DICT_DONT_STRDUP_VAL) av_free(value);
            return 0;
        }
        if (flags & AV_DICT_APPEND)
            oldval = tag->value;
        else
            av_free(tag->value);
        av_free(tag->key);
        *tag = m->elems[--m->count];
    } else {
        AVDictionaryEntry *tmp = av_realloc(m->elems,
                                            (m->count + 1) * sizeof(*m->elems));
        if (tmp)
            m->elems = tmp;
        else
            return AVERROR(ENOMEM);
    }
    if (value) {
        if (flags & AV_DICT_DONT_STRDUP_KEY)
            m->elems[m->count].key = key;
        else
            m->elems[m->count].key = av_strdup(key);
        if (flags & AV_DICT_DONT_STRDUP_VAL) {
            m->elems[m->count].value = value;
        } else if (oldval && flags & AV_DICT_APPEND) {
            int len = strlen(oldval) + strlen(value) + 1;
            if (!(oldval = av_realloc(oldval, len)))
                return AVERROR(ENOMEM);
            av_strlcat(oldval, value, len);
            m->elems[m->count].value = oldval;
        } else
            m->elems[m->count].value = av_strdup(value);
        m->count++;
    }
    if (!m->count) {
        av_free(m->elems);
        av_freep(pm);
    }

    return 0;
}

static int parse_key_value_pair(AVDictionary **pm, const char **buf,
                                const char *key_val_sep, const char *pairs_sep,
                                int flags)
{
    char *key = av_get_token(buf, key_val_sep);
    char *val = NULL;
    int ret;

    if (key && *key && strspn(*buf, key_val_sep)) {
        (*buf)++;
        val = av_get_token(buf, pairs_sep);
    }

    if (key && *key && val && *val)
        ret = av_dict_set(pm, key, val, flags);
    else
        ret = AVERROR(EINVAL);

    av_freep(&key);
    av_freep(&val);

    return ret;
}

int av_dict_parse_string(AVDictionary **pm, const char *str,
                         const char *key_val_sep, const char *pairs_sep,
                         int flags)
{
    int ret;

    if (!str)
        return 0;

    /* ignore STRDUP flags */
    flags &= ~(AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);

    while (*str) {
        if ((ret = parse_key_value_pair(pm, &str, key_val_sep, pairs_sep, flags)) < 0)
            return ret;

        if (*str)
            str++;
    }

    return 0;
}

void av_dict_free(AVDictionary **pm)
{
    AVDictionary *m = *pm;

    if (m) {
        while (m->count--) {
            av_free(m->elems[m->count].key);
            av_free(m->elems[m->count].value);
        }
        av_free(m->elems);
    }
    av_freep(pm);
}

void av_dict_copy(AVDictionary **dst, const AVDictionary *src, int flags)
{
    AVDictionaryEntry *t = NULL;

    while ((t = av_dict_get(src, "", t, AV_DICT_IGNORE_SUFFIX)))
        av_dict_set(dst, t->key, t->value, flags);
}
