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

#include <string.h>

#include "avstring.h"
#include "dict.h"
#include "internal.h"
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
            if (flags & AV_DICT_DONT_STRDUP_KEY) av_free((void*)key);
            if (flags & AV_DICT_DONT_STRDUP_VAL) av_free((void*)value);
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
        if (!tmp)
            goto err_out;
        m->elems = tmp;
    }
    if (value) {
        if (flags & AV_DICT_DONT_STRDUP_KEY)
            m->elems[m->count].key = (char*)(intptr_t)key;
        else
            m->elems[m->count].key = av_strdup(key);
        if (flags & AV_DICT_DONT_STRDUP_VAL) {
            m->elems[m->count].value = (char*)(intptr_t)value;
        } else if (oldval && flags & AV_DICT_APPEND) {
            int len = strlen(oldval) + strlen(value) + 1;
            char *newval = av_mallocz(len);
            if (!newval)
                goto err_out;
            av_strlcat(newval, oldval, len);
            av_freep(&oldval);
            av_strlcat(newval, value, len);
            m->elems[m->count].value = newval;
        } else
            m->elems[m->count].value = av_strdup(value);
        m->count++;
    }
    if (!m->count) {
        av_free(m->elems);
        av_freep(pm);
    }

    return 0;

err_out:
    if (!m->count) {
        av_free(m->elems);
        av_freep(pm);
    }
    if (flags & AV_DICT_DONT_STRDUP_KEY) av_free((void*)key);
    if (flags & AV_DICT_DONT_STRDUP_VAL) av_free((void*)value);
    return AVERROR(ENOMEM);
}

int av_dict_set_int(AVDictionary **pm, const char *key, int64_t value,
                int flags)
{
    char valuestr[22];
    snprintf(valuestr, sizeof(valuestr), "%"PRId64, value);
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

int av_dict_get_string(const AVDictionary *m, char **buffer,
                       const char key_val_sep, const char pairs_sep)
{
    AVDictionaryEntry *t = NULL;
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
    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX))) {
        if (cnt++)
            av_bprint_append_data(&bprint, &pairs_sep, 1);
        av_bprint_escape(&bprint, t->key, special_chars, AV_ESCAPE_MODE_BACKSLASH, 0);
        av_bprint_append_data(&bprint, &key_val_sep, 1);
        av_bprint_escape(&bprint, t->value, special_chars, AV_ESCAPE_MODE_BACKSLASH, 0);
    }
    return av_bprint_finalize(&bprint, buffer);
}

#ifdef TEST
static void print_dict(const AVDictionary *m)
{
    AVDictionaryEntry *t = NULL;
    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX)))
        printf("%s %s   ", t->key, t->value);
    printf("\n");
}

static void test_separators(const AVDictionary *m, const char pair, const char val)
{
    AVDictionary *dict = NULL;
    char pairs[] = {pair , '\0'};
    char vals[]  = {val, '\0'};

    char *buffer = NULL;
    av_dict_copy(&dict, m, 0);
    print_dict(dict);
    av_dict_get_string(dict, &buffer, val, pair);
    printf("%s\n", buffer);
    av_dict_free(&dict);
    av_dict_parse_string(&dict, buffer, vals, pairs, 0);
    av_freep(&buffer);
    print_dict(dict);
    av_dict_free(&dict);
}

int main(void)
{
    AVDictionary *dict = NULL;
    char *buffer = NULL;

    printf("Testing av_dict_get_string() and av_dict_parse_string()\n");
    av_dict_get_string(dict, &buffer, '=', ',');
    printf("%s\n", buffer);
    av_freep(&buffer);
    av_dict_set(&dict, "aaa", "aaa", 0);
    av_dict_set(&dict, "b,b", "bbb", 0);
    av_dict_set(&dict, "c=c", "ccc", 0);
    av_dict_set(&dict, "ddd", "d,d", 0);
    av_dict_set(&dict, "eee", "e=e", 0);
    av_dict_set(&dict, "f,f", "f=f", 0);
    av_dict_set(&dict, "g=g", "g,g", 0);
    test_separators(dict, ',', '=');
    av_dict_free(&dict);
    av_dict_set(&dict, "aaa", "aaa", 0);
    av_dict_set(&dict, "bbb", "bbb", 0);
    av_dict_set(&dict, "ccc", "ccc", 0);
    av_dict_set(&dict, "\\,=\'\"", "\\,=\'\"", 0);
    test_separators(dict, '"',  '=');
    test_separators(dict, '\'', '=');
    test_separators(dict, ',', '"');
    test_separators(dict, ',', '\'');
    test_separators(dict, '\'', '"');
    test_separators(dict, '"', '\'');
    av_dict_free(&dict);

    return 0;
}
#endif
