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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "avassert.h"
#include "avstring.h"
#include "dict.h"
#include "error.h"
#include "mem.h"
#include "bprint.h"

struct AVDictionary {
    int count;
    AVDictionaryEntry *elems;
};

int av_dict_count(const AVDictionary *m)
{
    return m ? m->count : 0;
}

const AVDictionaryEntry *av_dict_iterate(const AVDictionary *m,
                                         const AVDictionaryEntry *prev)
{
    int i = 0;

    if (!m)
        return NULL;

    if (prev)
        i = prev - m->elems + 1;

    av_assert2(i >= 0);
    if (i >= m->count)
        return NULL;

    return &m->elems[i];
}

AVDictionaryEntry *av_dict_get(const AVDictionary *m, const char *key,
                               const AVDictionaryEntry *prev, int flags)
{
    const AVDictionaryEntry *entry = prev;
    unsigned int j;

    if (!key)
        return NULL;

    while ((entry = av_dict_iterate(m, entry))) {
        const char *s = entry->key;
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
        return (AVDictionaryEntry *)entry;
    }
    return NULL;
}

int av_dict_set(AVDictionary **pm, const char *key, const char *value,
                int flags)
{
    AVDictionary *m = *pm;
    AVDictionaryEntry *tag = NULL;
    char *copy_key = NULL, *copy_value = NULL;
    int err;

    if (flags & AV_DICT_DONT_STRDUP_VAL)
        copy_value = (void *)value;
    else if (value)
        copy_value = av_strdup(value);
    if (!key) {
        err = AVERROR(EINVAL);
        goto err_out;
    }
    if (flags & AV_DICT_DONT_STRDUP_KEY)
        copy_key = (void *)key;
    else
        copy_key = av_strdup(key);
    if (!copy_key || (value && !copy_value))
        goto enomem;

    if (!(flags & AV_DICT_MULTIKEY)) {
        tag = av_dict_get(m, key, NULL, flags);
    } else if (flags & AV_DICT_DEDUP) {
        while ((tag = av_dict_get(m, key, tag, flags))) {
            if ((!value && !tag->value) ||
                (value && tag->value && !strcmp(value, tag->value))) {
                av_free(copy_key);
                av_free(copy_value);
                return 0;
            }
        }
    }
    if (!m)
        m = *pm = av_mallocz(sizeof(*m));
    if (!m)
        goto enomem;

    if (tag) {
        if (flags & AV_DICT_DONT_OVERWRITE) {
            av_free(copy_key);
            av_free(copy_value);
            return 0;
        }
        if (copy_value && flags & AV_DICT_APPEND) {
            size_t oldlen = strlen(tag->value);
            size_t new_part_len = strlen(copy_value);
            size_t len = oldlen + new_part_len + 1;
            char *newval = av_realloc(tag->value, len);
            if (!newval)
                goto enomem;
            memcpy(newval + oldlen, copy_value, new_part_len + 1);
            av_freep(&copy_value);
            copy_value = newval;
        } else
            av_free(tag->value);
        av_free(tag->key);
        *tag = m->elems[--m->count];
    } else if (copy_value) {
        AVDictionaryEntry *tmp = av_realloc_array(m->elems,
                                                  m->count + 1, sizeof(*m->elems));
        if (!tmp)
            goto enomem;
        m->elems = tmp;
    }
    if (copy_value) {
        m->elems[m->count].key = copy_key;
        m->elems[m->count].value = copy_value;
        m->count++;
    } else {
        err = 0;
        goto end;
    }

    return 0;

enomem:
    err = AVERROR(ENOMEM);
err_out:
    av_free(copy_value);
end:
    if (m && !m->count) {
        av_freep(&m->elems);
        av_freep(pm);
    }
    av_free(copy_key);
    return err;
}

int av_dict_set_int(AVDictionary **pm, const char *key, int64_t value,
                int flags)
{
    char valuestr[22];
    snprintf(valuestr, sizeof(valuestr), "%"PRId64, value);
    flags &= ~AV_DICT_DONT_STRDUP_VAL;
    return av_dict_set(pm, key, valuestr, flags);
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
            av_freep(&m->elems[m->count].key);
            av_freep(&m->elems[m->count].value);
        }
        av_freep(&m->elems);
    }
    av_freep(pm);
}

int av_dict_copy(AVDictionary **dst, const AVDictionary *src, int flags)
{
    const AVDictionaryEntry *t = NULL;

    while ((t = av_dict_iterate(src, t))) {
        int ret = av_dict_set(dst, t->key, t->value, flags);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int av_dict_get_string(const AVDictionary *m, char **buffer,
                       const char key_val_sep, const char pairs_sep)
{
    const AVDictionaryEntry *t = NULL;
    AVBPrint bprint;
    int cnt = 0;
    char special_chars[] = {pairs_sep, key_val_sep, '\0'};

    if (!buffer || pairs_sep == '\0' || key_val_sep == '\0' || pairs_sep == key_val_sep ||
        pairs_sep == '\\' || key_val_sep == '\\')
        return AVERROR(EINVAL);

    if (!av_dict_count(m)) {
        *buffer = av_strdup("");
        return *buffer ? 0 : AVERROR(ENOMEM);
    }

    av_bprint_init(&bprint, 64, AV_BPRINT_SIZE_UNLIMITED);
    while ((t = av_dict_iterate(m, t))) {
        if (cnt++)
            av_bprint_append_data(&bprint, &pairs_sep, 1);
        av_bprint_escape(&bprint, t->key, special_chars, AV_ESCAPE_MODE_BACKSLASH, 0);
        av_bprint_append_data(&bprint, &key_val_sep, 1);
        av_bprint_escape(&bprint, t->value, special_chars, AV_ESCAPE_MODE_BACKSLASH, 0);
    }
    return av_bprint_finalize(&bprint, buffer);
}
