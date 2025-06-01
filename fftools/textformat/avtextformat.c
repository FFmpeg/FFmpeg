/*
 * Copyright (c) The FFmpeg developers
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

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/avassert.h"
#include "libavutil/bprint.h"
#include "libavutil/error.h"
#include "libavutil/hash.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/opt.h"
#include "avtextformat.h"

#define SECTION_ID_NONE (-1)

#define SHOW_OPTIONAL_FIELDS_AUTO      (-1)
#define SHOW_OPTIONAL_FIELDS_NEVER       0
#define SHOW_OPTIONAL_FIELDS_ALWAYS      1

static const struct {
    double bin_val;
    double dec_val;
    char bin_str[4];
    char dec_str[4];
} si_prefixes[] = {
    { 1.0, 1.0, "", "" },
    { 1.024e3, 1e3, "Ki", "K" },
    { 1.048576e6, 1e6, "Mi", "M" },
    { 1.073741824e9, 1e9, "Gi", "G" },
    { 1.099511627776e12, 1e12, "Ti", "T" },
    { 1.125899906842624e15, 1e15, "Pi", "P" },
};

static const char *textcontext_get_formatter_name(void *p)
{
    AVTextFormatContext *tctx = p;
    return tctx->formatter->name;
}

#define OFFSET(x) offsetof(AVTextFormatContext, x)

static const AVOption textcontext_options[] = {
    { "string_validation", "set string validation mode",
      OFFSET(string_validation), AV_OPT_TYPE_INT, { .i64 = AV_TEXTFORMAT_STRING_VALIDATION_REPLACE }, 0, AV_TEXTFORMAT_STRING_VALIDATION_NB - 1, .unit = "sv" },
    { "sv", "set string validation mode",
      OFFSET(string_validation), AV_OPT_TYPE_INT, { .i64 = AV_TEXTFORMAT_STRING_VALIDATION_REPLACE }, 0, AV_TEXTFORMAT_STRING_VALIDATION_NB - 1, .unit = "sv" },
        { "ignore",  NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = AV_TEXTFORMAT_STRING_VALIDATION_IGNORE },  .unit = "sv" },
        { "replace", NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = AV_TEXTFORMAT_STRING_VALIDATION_REPLACE }, .unit = "sv" },
        { "fail",    NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = AV_TEXTFORMAT_STRING_VALIDATION_FAIL },    .unit = "sv" },
    { "string_validation_replacement", "set string validation replacement string", OFFSET(string_validation_replacement), AV_OPT_TYPE_STRING, { .str = "" } },
    { "svr", "set string validation replacement string", OFFSET(string_validation_replacement), AV_OPT_TYPE_STRING, { .str = "\xEF\xBF\xBD" } },
    { NULL }
};

static void *textcontext_child_next(void *obj, void *prev)
{
    AVTextFormatContext *ctx = obj;
    if (!prev && ctx->formatter && ctx->formatter->priv_class && ctx->priv)
        return ctx->priv;
    return NULL;
}

static const AVClass textcontext_class = {
    .class_name = "AVTextContext",
    .item_name  = textcontext_get_formatter_name,
    .option     = textcontext_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .child_next = textcontext_child_next,
};

static void bprint_bytes(AVBPrint *bp, const uint8_t *ubuf, size_t ubuf_size)
{
    av_bprintf(bp, "0X");
    for (unsigned i = 0; i < ubuf_size; i++)
        av_bprintf(bp, "%02X", ubuf[i]);
}

int avtext_context_close(AVTextFormatContext **ptctx)
{
    AVTextFormatContext *tctx = *ptctx;
    int i;
    int ret = 0;

    if (!tctx)
        return AVERROR(EINVAL);

    av_hash_freep(&tctx->hash);

    av_hash_freep(&tctx->hash);

    if (tctx->formatter) {
        if (tctx->formatter->uninit)
            ret = tctx->formatter->uninit(tctx);
        if (tctx->formatter->priv_class)
            av_opt_free(tctx->priv);
    }
    for (i = 0; i < SECTION_MAX_NB_LEVELS; i++)
        av_bprint_finalize(&tctx->section_pbuf[i], NULL);
    av_freep(&tctx->priv);
    av_opt_free(tctx);
    av_freep(ptctx);
    return ret;
}


int avtext_context_open(AVTextFormatContext **ptctx, const AVTextFormatter *formatter, AVTextWriterContext *writer_context, const char *args,
                        const AVTextFormatSection *sections, int nb_sections, AVTextFormatOptions options, char *show_data_hash)
{
    AVTextFormatContext *tctx;
    int i, ret = 0;

    av_assert0(ptctx && formatter);

    if (!(tctx = av_mallocz(sizeof(AVTextFormatContext)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (int i = 0; i < SECTION_MAX_NB_LEVELS; i++)
        av_bprint_init(&tctx->section_pbuf[i], 1, AV_BPRINT_SIZE_UNLIMITED);

    tctx->class = &textcontext_class;
    av_opt_set_defaults(tctx);

    if (!(tctx->priv = av_mallocz(formatter->priv_size))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    tctx->show_value_unit = options.show_value_unit;
    tctx->use_value_prefix = options.use_value_prefix;
    tctx->use_byte_value_binary_prefix = options.use_byte_value_binary_prefix;
    tctx->use_value_sexagesimal_format = options.use_value_sexagesimal_format;
    tctx->show_optional_fields = options.show_optional_fields;

    if (nb_sections > SECTION_MAX_NB_SECTIONS) {
        av_log(tctx, AV_LOG_ERROR, "The number of section definitions (%d) is larger than the maximum allowed (%d)\n", nb_sections, SECTION_MAX_NB_SECTIONS);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    tctx->formatter = formatter;
    tctx->level = -1;
    tctx->sections = sections;
    tctx->nb_sections = nb_sections;
    tctx->writer = writer_context;

    if (formatter->priv_class) {
        void *priv_ctx = tctx->priv;
        *(const AVClass **)priv_ctx = formatter->priv_class;
        av_opt_set_defaults(priv_ctx);
    }

    /* convert options to dictionary */
    if (args) {
        AVDictionary *opts = NULL;
        const AVDictionaryEntry *opt = NULL;

        if ((ret = av_dict_parse_string(&opts, args, "=", ":", 0)) < 0) {
            av_log(tctx, AV_LOG_ERROR, "Failed to parse option string '%s' provided to textformat context\n", args);
            av_dict_free(&opts);
            goto fail;
        }

        while ((opt = av_dict_iterate(opts, opt))) {
            if ((ret = av_opt_set(tctx, opt->key, opt->value, AV_OPT_SEARCH_CHILDREN)) < 0) {
                av_log(tctx, AV_LOG_ERROR, "Failed to set option '%s' with value '%s' provided to textformat context\n",
                       opt->key, opt->value);
                av_dict_free(&opts);
                goto fail;
            }
        }

        av_dict_free(&opts);
    }

    if (show_data_hash) {
        if ((ret = av_hash_alloc(&tctx->hash, show_data_hash)) < 0) {
            if (ret == AVERROR(EINVAL)) {
                const char *n;
                av_log(NULL, AV_LOG_ERROR, "Unknown hash algorithm '%s'\nKnown algorithms:", show_data_hash);
                for (i = 0; (n = av_hash_names(i)); i++)
                    av_log(NULL, AV_LOG_ERROR, " %s", n);
                av_log(NULL, AV_LOG_ERROR, "\n");
            }
            goto fail;
        }
    }

    /* validate replace string */
    {
        const uint8_t *p = (uint8_t *)tctx->string_validation_replacement;
        const uint8_t *endp = p + strlen((const char *)p);
        while (*p) {
            const uint8_t *p0 = p;
            int32_t code;
            ret = av_utf8_decode(&code, &p, endp, tctx->string_validation_utf8_flags);
            if (ret < 0) {
                AVBPrint bp;
                av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
                bprint_bytes(&bp, p0, p - p0);
                av_log(tctx, AV_LOG_ERROR,
                       "Invalid UTF8 sequence %s found in string validation replace '%s'\n",
                       bp.str, tctx->string_validation_replacement);
                return ret;
                goto fail;
            }
        }
    }

    if (tctx->formatter->init)
        ret = tctx->formatter->init(tctx);
    if (ret < 0)
        goto fail;

    *ptctx = tctx;

    return 0;

fail:
    avtext_context_close(&tctx);
    return ret;
}

/* Temporary definitions during refactoring */
static const char unit_second_str[]         = "s";
static const char unit_hertz_str[]          = "Hz";
static const char unit_byte_str[]           = "byte";
static const char unit_bit_per_second_str[] = "bit/s";


void avtext_print_section_header(AVTextFormatContext *tctx, const void *data, int section_id)
{
    if (section_id < 0 || section_id >= tctx->nb_sections) {
        av_log(tctx, AV_LOG_ERROR, "Invalid section_id for section_header: %d\n", section_id);
        return;
    }

    tctx->level++;
    av_assert0(tctx->level < SECTION_MAX_NB_LEVELS);

    tctx->nb_item[tctx->level] = 0;
    memset(tctx->nb_item_type[tctx->level], 0, sizeof(tctx->nb_item_type[tctx->level]));
    tctx->section[tctx->level] = &tctx->sections[section_id];

    if (tctx->formatter->print_section_header)
        tctx->formatter->print_section_header(tctx, data);
}

void avtext_print_section_footer(AVTextFormatContext *tctx)
{
    if (tctx->level < 0 || tctx->level >= SECTION_MAX_NB_LEVELS) {
        av_log(tctx, AV_LOG_ERROR, "Invalid level for section_footer: %d\n", tctx->level);
        return;
    }

    int section_id = tctx->section[tctx->level]->id;
    int parent_section_id = tctx->level ?
        tctx->section[tctx->level - 1]->id : SECTION_ID_NONE;

    if (parent_section_id != SECTION_ID_NONE) {
        tctx->nb_item[tctx->level - 1]++;
        tctx->nb_item_type[tctx->level - 1][section_id]++;
    }

    if (tctx->formatter->print_section_footer)
        tctx->formatter->print_section_footer(tctx);
    tctx->level--;
}

void avtext_print_integer(AVTextFormatContext *tctx, const char *key, int64_t val, int flags)
{
    const AVTextFormatSection *section;

    av_assert0(tctx);

    if (tctx->show_optional_fields == SHOW_OPTIONAL_FIELDS_NEVER)
        return;

    if (tctx->show_optional_fields == SHOW_OPTIONAL_FIELDS_AUTO
        && (flags & AV_TEXTFORMAT_PRINT_STRING_OPTIONAL)
        && !(tctx->formatter->flags & AV_TEXTFORMAT_FLAG_SUPPORTS_OPTIONAL_FIELDS))
        return;

    av_assert0(key && tctx->level >= 0 && tctx->level < SECTION_MAX_NB_LEVELS);

    section = tctx->section[tctx->level];

    if (section->show_all_entries || av_dict_get(section->entries_to_show, key, NULL, 0)) {
        tctx->formatter->print_integer(tctx, key, val);
        tctx->nb_item[tctx->level]++;
    }
}

static inline int validate_string(AVTextFormatContext *tctx, char **dstp, const char *src)
{
    const uint8_t *p, *endp, *srcp = (const uint8_t *)src;
    AVBPrint dstbuf;
    AVBPrint invalid_seq;
    int invalid_chars_nb = 0, ret = 0;

    *dstp = NULL;
    av_bprint_init(&dstbuf, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&invalid_seq, 0, AV_BPRINT_SIZE_UNLIMITED);

    endp = srcp + strlen(src);
    for (p = srcp; *p;) {
        int32_t code;
        int invalid = 0;
        const uint8_t *p0 = p;

        if (av_utf8_decode(&code, &p, endp, tctx->string_validation_utf8_flags) < 0) {

            av_bprint_clear(&invalid_seq);

            bprint_bytes(&invalid_seq, p0, p - p0);

            av_log(tctx, AV_LOG_DEBUG, "Invalid UTF-8 sequence '%s' found in string '%s'\n", invalid_seq.str, src);
            invalid = 1;
        }

        if (invalid) {
            invalid_chars_nb++;

            switch (tctx->string_validation) {
            case AV_TEXTFORMAT_STRING_VALIDATION_FAIL:
                av_log(tctx, AV_LOG_ERROR, "Invalid UTF-8 sequence found in string '%s'\n", src);
                ret = AVERROR_INVALIDDATA;
                goto end;

            case AV_TEXTFORMAT_STRING_VALIDATION_REPLACE:
                av_bprintf(&dstbuf, "%s", tctx->string_validation_replacement);
                break;
            }
        }

        if (!invalid || tctx->string_validation == AV_TEXTFORMAT_STRING_VALIDATION_IGNORE)
            av_bprint_append_data(&dstbuf, p0, p-p0);
    }

    if (invalid_chars_nb && tctx->string_validation == AV_TEXTFORMAT_STRING_VALIDATION_REPLACE)
        av_log(tctx, AV_LOG_WARNING,
               "%d invalid UTF-8 sequence(s) found in string '%s', replaced with '%s'\n",
               invalid_chars_nb, src, tctx->string_validation_replacement);

end:
    av_bprint_finalize(&dstbuf, dstp);
    av_bprint_finalize(&invalid_seq, NULL);
    return ret;
}

struct unit_value {
    union {
        double  d;
        int64_t i;
    } val;

    const char *unit;
};

static char *value_string(const AVTextFormatContext *tctx, char *buf, int buf_size, struct unit_value uv)
{
    double vald;
    int64_t vali = 0;
    int show_float = 0;

    if (uv.unit == unit_second_str) {
        vald = uv.val.d;
        show_float = 1;
    } else {
        vald = (double)uv.val.i;
        vali = uv.val.i;
    }

    if (uv.unit == unit_second_str && tctx->use_value_sexagesimal_format) {
        double secs;
        int hours, mins;
        secs  = vald;
        mins  = (int)secs / 60;
        secs  = secs - mins * 60;
        hours = mins / 60;
        mins %= 60;
        snprintf(buf, buf_size, "%d:%02d:%09.6f", hours, mins, secs);
    } else {
        const char *prefix_string = "";

        if (tctx->use_value_prefix && vald > 1) {
            int64_t index;

            if (uv.unit == unit_byte_str && tctx->use_byte_value_binary_prefix) {
                index = (int64_t)(log2(vald) / 10);
                index = av_clip64(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
                vald /= si_prefixes[index].bin_val;
                prefix_string = si_prefixes[index].bin_str;
            } else {
                index = (int64_t)(log10(vald) / 3);
                index = av_clip64(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
                vald /= si_prefixes[index].dec_val;
                prefix_string = si_prefixes[index].dec_str;
            }
            vali = (int64_t)vald;
        }

        if (show_float || (tctx->use_value_prefix && vald != (int64_t)vald))
            snprintf(buf, buf_size, "%f", vald);
        else
            snprintf(buf, buf_size, "%"PRId64, vali);

        av_strlcatf(buf, buf_size, "%s%s%s", *prefix_string || tctx->show_value_unit ? " " : "",
                    prefix_string, tctx->show_value_unit ? uv.unit : "");
    }

    return buf;
}


void avtext_print_unit_int(AVTextFormatContext *tctx, const char *key, int value, const char *unit)
{
    char val_str[128];
    struct unit_value uv;
    uv.val.i = value;
    uv.unit = unit;
    avtext_print_string(tctx, key, value_string(tctx, val_str, sizeof(val_str), uv), 0);
}


int avtext_print_string(AVTextFormatContext *tctx, const char *key, const char *val, int flags)
{
    const AVTextFormatSection *section;
    int ret = 0;

    av_assert0(key && val && tctx->level >= 0 && tctx->level < SECTION_MAX_NB_LEVELS);

    section = tctx->section[tctx->level];

    if (tctx->show_optional_fields == SHOW_OPTIONAL_FIELDS_NEVER)
        return 0;

    if (tctx->show_optional_fields == SHOW_OPTIONAL_FIELDS_AUTO
        && (flags & AV_TEXTFORMAT_PRINT_STRING_OPTIONAL)
        && !(tctx->formatter->flags & AV_TEXTFORMAT_FLAG_SUPPORTS_OPTIONAL_FIELDS))
        return 0;

    if (section->show_all_entries || av_dict_get(section->entries_to_show, key, NULL, 0)) {
        if (flags & AV_TEXTFORMAT_PRINT_STRING_VALIDATE) {
            char *key1 = NULL, *val1 = NULL;
            ret = validate_string(tctx, &key1, key);
            if (ret < 0) goto end;
            ret = validate_string(tctx, &val1, val);
            if (ret < 0) goto end;
            tctx->formatter->print_string(tctx, key1, val1);
        end:
            if (ret < 0)
                av_log(tctx, AV_LOG_ERROR,
                       "Invalid key=value string combination %s=%s in section %s\n",
                       key, val, section->unique_name);
            av_free(key1);
            av_free(val1);
        } else {
            tctx->formatter->print_string(tctx, key, val);
        }

        tctx->nb_item[tctx->level]++;
    }

    return ret;
}

void avtext_print_rational(AVTextFormatContext *tctx, const char *key, AVRational q, char sep)
{
    char buf[44];
    snprintf(buf, sizeof(buf), "%d%c%d", q.num, sep, q.den);
    avtext_print_string(tctx, key, buf, 0);
}

void avtext_print_time(AVTextFormatContext *tctx, const char *key,
                       int64_t ts, const AVRational *time_base, int is_duration)
{
    if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0)) {
        avtext_print_string(tctx, key, "N/A", AV_TEXTFORMAT_PRINT_STRING_OPTIONAL);
    } else {
        char buf[128];
        double d = av_q2d(*time_base) * ts;
        struct unit_value uv;
        uv.val.d = d;
        uv.unit = unit_second_str;
        value_string(tctx, buf, sizeof(buf), uv);
        avtext_print_string(tctx, key, buf, 0);
    }
}

void avtext_print_ts(AVTextFormatContext *tctx, const char *key, int64_t ts, int is_duration)
{
    if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0))
        avtext_print_string(tctx, key, "N/A", AV_TEXTFORMAT_PRINT_STRING_OPTIONAL);
    else
        avtext_print_integer(tctx, key, ts, 0);
}

void avtext_print_data(AVTextFormatContext *tctx, const char *key,
                       const uint8_t *data, int size)
{
    AVBPrint bp;
    unsigned offset = 0;
    int l, i;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&bp, "\n");
    while (size) {
        av_bprintf(&bp, "%08x: ", offset);
        l = FFMIN(size, 16);
        for (i = 0; i < l; i++) {
            av_bprintf(&bp, "%02x", data[i]);
            if (i & 1)
                av_bprintf(&bp, " ");
        }
        av_bprint_chars(&bp, ' ', 41 - 2 * i - i / 2);
        for (i = 0; i < l; i++)
            av_bprint_chars(&bp, data[i] - 32U < 95 ? data[i] : '.', 1);
        av_bprintf(&bp, "\n");
        offset += l;
        data   += l;
        size   -= l;
    }
    avtext_print_string(tctx, key, bp.str, 0);
    av_bprint_finalize(&bp, NULL);
}

void avtext_print_data_hash(AVTextFormatContext *tctx, const char *key,
                            const uint8_t *data, int size)
{
    char buf[AV_HASH_MAX_SIZE * 2 + 64] = { 0 };
    int len;

    if (!tctx->hash)
        return;

    av_hash_init(tctx->hash);
    av_hash_update(tctx->hash, data, size);
    len = snprintf(buf, sizeof(buf), "%s:", av_hash_get_name(tctx->hash));
    av_hash_final_hex(tctx->hash, (uint8_t *)&buf[len], (int)sizeof(buf) - len);
    avtext_print_string(tctx, key, buf, 0);
}

void avtext_print_integers(AVTextFormatContext *tctx, const char *key,
                           uint8_t *data, int size, const char *format,
                           int columns, int bytes, int offset_add)
{
    AVBPrint bp;
    unsigned offset = 0;
    int l, i;

    if (!key || !data || !format || columns <= 0 || bytes <= 0)
        return;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&bp, "\n");
    while (size) {
        av_bprintf(&bp, "%08x: ", offset);
        l = FFMIN(size, columns);
        for (i = 0; i < l; i++) {
            if      (bytes == 1) av_bprintf(&bp, format, *data);
            else if (bytes == 2) av_bprintf(&bp, format, AV_RN16(data));
            else if (bytes == 4) av_bprintf(&bp, format, AV_RN32(data));
            data += bytes;
            size--;
        }
        av_bprintf(&bp, "\n");
        offset += offset_add;
    }
    avtext_print_string(tctx, key, bp.str, 0);
    av_bprint_finalize(&bp, NULL);
}

static const char *writercontext_get_writer_name(void *p)
{
    AVTextWriterContext *wctx = p;
    return wctx->writer->name;
}

static void *writercontext_child_next(void *obj, void *prev)
{
    AVTextFormatContext *ctx = obj;
    if (!prev && ctx->formatter && ctx->formatter->priv_class && ctx->priv)
        return ctx->priv;
    return NULL;
}

static const AVClass textwriter_class = {
    .class_name = "AVTextWriterContext",
    .item_name  = writercontext_get_writer_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .child_next = writercontext_child_next,
};


int avtextwriter_context_close(AVTextWriterContext **pwctx)
{
    AVTextWriterContext *wctx = *pwctx;
    int ret = 0;

    if (!wctx)
        return AVERROR(EINVAL);

    if (wctx->writer) {
        if (wctx->writer->uninit)
            ret = wctx->writer->uninit(wctx);
        if (wctx->writer->priv_class)
            av_opt_free(wctx->priv);
    }
    av_freep(&wctx->priv);
    av_freep(pwctx);
    return ret;
}


int avtextwriter_context_open(AVTextWriterContext **pwctx, const AVTextWriter *writer)
{
    AVTextWriterContext *wctx;
    int ret = 0;

    if (!pwctx || !writer)
        return AVERROR(EINVAL);

    if (!((wctx = av_mallocz(sizeof(AVTextWriterContext))))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (writer->priv_size && !((wctx->priv = av_mallocz(writer->priv_size)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (writer->priv_class) {
        void *priv_ctx = wctx->priv;
        *(const AVClass **)priv_ctx = writer->priv_class;
        av_opt_set_defaults(priv_ctx);
    }

    wctx->class = &textwriter_class;
    wctx->writer = writer;

    av_opt_set_defaults(wctx);


    if (wctx->writer->init)
        ret = wctx->writer->init(wctx);
    if (ret < 0)
        goto fail;

    *pwctx = wctx;

    return 0;

fail:
    avtextwriter_context_close(&wctx);
    return ret;
}

static const AVTextFormatter *const registered_formatters[] =
{
    &avtextformatter_default,
    &avtextformatter_compact,
    &avtextformatter_csv,
    &avtextformatter_flat,
    &avtextformatter_ini,
    &avtextformatter_json,
    &avtextformatter_xml,
    &avtextformatter_mermaid,
    &avtextformatter_mermaidhtml,
    NULL
};

const AVTextFormatter *avtext_get_formatter_by_name(const char *name)
{
    for (int i = 0; registered_formatters[i]; i++) {
        const char *end;
        if (av_strstart(name, registered_formatters[i]->name, &end) &&
            (*end == '\0' || *end == '='))
            return registered_formatters[i];
    }

    return NULL;
}
