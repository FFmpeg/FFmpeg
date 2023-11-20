/*
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

/**
 * @file
 * text expansion utilities
 */

#include <fenv.h>
#include <math.h>
#include <string.h>

#include "textutils.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/file.h"
#include "libavutil/time.h"

static int ff_expand_text_function_internal(FFExpandTextContext *expand_text, AVBPrint *bp,
                                            char *name, unsigned argc, char **argv)
{
    void *log_ctx = expand_text->log_ctx;
    FFExpandTextFunction *functions = expand_text->functions;
    unsigned i;

    for (i = 0; i < expand_text->functions_nb; i++) {
        if (strcmp(name, functions[i].name))
            continue;
        if (argc < functions[i].argc_min) {
            av_log(log_ctx, AV_LOG_ERROR, "%%{%s} requires at least %d arguments\n",
                   name, functions[i].argc_min);
            return AVERROR(EINVAL);
        }
        if (argc > functions[i].argc_max) {
            av_log(log_ctx, AV_LOG_ERROR, "%%{%s} requires at most %d arguments\n",
                   name, functions[i].argc_max);
            return AVERROR(EINVAL);
        }
        break;
    }
    if (i >= expand_text->functions_nb) {
        av_log(log_ctx, AV_LOG_ERROR, "%%{%s} is not known\n", name);
        return AVERROR(EINVAL);
    }

    return functions[i].func(log_ctx, bp, name, argc, argv);
}

/**
 * Expand text template pointed to by *rtext.
 *
 * Expand text template defined in text using the logic defined in a text
 * expander object.
 *
 * This function expects the text to be in the format %{FUNCTION_NAME[:PARAMS]},
 * where PARAMS is a sequence of strings separated by : and represents the function
 * arguments to use for the function evaluation.
 *
 * @param text_expander TextExpander object used to expand the text
 * @param bp   BPrint object where the expanded text is written to
 * @param rtext pointer to pointer to the text to expand, it is updated to point
 * to the next part of the template to process
 * @return negative value corresponding to an AVERROR error code in case of
 * errors, a non-negative value otherwise
 */
static int ff_expand_text_function(FFExpandTextContext *expand_text, AVBPrint *bp, char **rtext)
{
    void *log_ctx = expand_text->log_ctx;
    const char *text = *rtext;
    char *argv[16] = { NULL };
    unsigned argc = 0, i;
    int ret;

    if (*text != '{') {
        av_log(log_ctx, AV_LOG_ERROR, "Stray %% near '%s'\n", text);
        return AVERROR(EINVAL);
    }
    text++;
    while (1) {
        if (!(argv[argc++] = av_get_token(&text, ":}"))) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        if (!*text) {
            av_log(log_ctx, AV_LOG_ERROR, "Unterminated %%{} near '%s'\n", *rtext);
            ret = AVERROR(EINVAL);
            goto end;
        }
        if (argc == FF_ARRAY_ELEMS(argv))
            av_freep(&argv[--argc]); /* error will be caught later */
        if (*text == '}')
            break;
        text++;
    }

    if ((ret = ff_expand_text_function_internal(expand_text, bp, argv[0], argc - 1, argv + 1)) < 0)
        goto end;
    ret = 0;
    *rtext = (char *)text + 1;

end:
    for (i = 0; i < argc; i++)
        av_freep(&argv[i]);
    return ret;
}

int ff_expand_text(FFExpandTextContext *expand_text, char *text, AVBPrint *bp)
{
    int ret;

    av_bprint_clear(bp);
    if (!text)
        return 0;

    while (*text) {
        if (*text == '\\' && text[1]) {
            av_bprint_chars(bp, text[1], 1);
            text += 2;
        } else if (*text == '%') {
            text++;
            if ((ret = ff_expand_text_function(expand_text, bp, &text)) < 0)
                return ret;
        } else {
            av_bprint_chars(bp, *text, 1);
            text++;
        }
    }
    if (!av_bprint_is_complete(bp))
        return AVERROR(ENOMEM);
    return 0;
}

int ff_print_pts(void *log_ctx, AVBPrint *bp, double pts, const char *delta,
                 const char *fmt, const char *strftime_fmt)
{
    int ret;

    if (delta) {
        int64_t delta_i;
        if ((ret = av_parse_time(&delta_i, delta, 1)) < 0) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid delta '%s'\n", delta);
            return ret;
        }
        pts += (double)delta_i / AV_TIME_BASE;
    }

    if (!strcmp(fmt, "flt")) {
        av_bprintf(bp, "%.6f", pts);
    } else if (!strcmp(fmt, "hms") ||
               !strcmp(fmt, "hms24hh")) {
        if (isnan(pts)) {
            av_bprintf(bp, " ??:??:??.???");
        } else {
            int64_t ms = llrint(pts * 1000);
            char sign = ' ';
            if (ms < 0) {
                sign = '-';
                ms = -ms;
            }
            if (!strcmp(fmt, "hms24hh")) {
                /* wrap around 24 hours */
                ms %= 24 * 60 * 60 * 1000;
            }
            av_bprintf(bp, "%c%02d:%02d:%02d.%03d", sign,
                       (int)(ms / (60 * 60 * 1000)),
                       (int)(ms / (60 * 1000)) % 60,
                       (int)(ms / 1000) % 60,
                       (int)(ms % 1000));
        }
    } else if (!strcmp(fmt, "localtime") ||
               !strcmp(fmt, "gmtime")) {
        struct tm tm;
        time_t ms = (time_t)pts;
        if (!strcmp(fmt, "localtime"))
            localtime_r(&ms, &tm);
        else
            gmtime_r(&ms, &tm);
        av_bprint_strftime(bp, av_x_if_null(strftime_fmt, "%Y-%m-%d %H:%M:%S"), &tm);
    } else {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid format '%s'\n", fmt);
        return AVERROR(EINVAL);
    }
    return 0;
}

int ff_print_time(void *log_ctx, AVBPrint *bp,
                  const char *strftime_fmt, char localtime)
{
    const char *fmt = av_x_if_null(strftime_fmt, "%Y-%m-%d %H:%M:%S");
    const char *fmt_begin = fmt;
    int64_t unow;
    time_t now;
    struct tm tm;
    const char *begin;
    const char *tmp;
    int len;
    int div;
    AVBPrint fmt_bp;

    av_bprint_init(&fmt_bp, 0, AV_BPRINT_SIZE_UNLIMITED);

    unow = av_gettime();
    now  = unow / 1000000;
    if (localtime)
        localtime_r(&now, &tm);
    else
        tm = *gmtime_r(&now, &tm);

    // manually parse format for %N (fractional seconds)
    begin = fmt;
    while ((begin = strchr(begin, '%'))) {
        tmp = begin + 1;
        len = 0;

        // skip escaped "%%"
        if (*tmp == '%') {
            begin = tmp + 1;
            continue;
        }

        // count digits between % and possible N
        while (*tmp != '\0' && av_isdigit((int)*tmp)) {
            len++;
            tmp++;
        }

        // N encountered, insert time
        if (*tmp == 'N') {
            int num_digits = 3; // default show millisecond [1,6]

            // if digit given, expect [1,6], warn & clamp otherwise
            if (len == 1) {
                num_digits = av_clip(*(begin + 1) - '0', 1, 6);
            } else if (len > 1) {
                av_log(log_ctx, AV_LOG_WARNING, "Invalid number of decimals for %%N, using default of %i\n", num_digits);
            }

            len += 2; // add % and N to get length of string part

            div = pow(10, 6 - num_digits);

            av_bprintf(&fmt_bp, "%.*s%0*d", (int)(begin - fmt_begin), fmt_begin, num_digits, (int)(unow % 1000000) / div);

            begin += len;
            fmt_begin = begin;

            continue;
        }

        begin = tmp;
    }

    av_bprintf(&fmt_bp, "%s", fmt_begin);
    if (!av_bprint_is_complete(&fmt_bp)) {
        av_log(log_ctx, AV_LOG_WARNING, "Format string truncated at %u/%u.", fmt_bp.size, fmt_bp.len);
    }

    av_bprint_strftime(bp, fmt_bp.str, &tm);

    av_bprint_finalize(&fmt_bp, NULL);

    return 0;
}

int ff_print_eval_expr(void *log_ctx, AVBPrint *bp,
                       const char *expr,
                       const char * const *fun_names, const ff_eval_func2 *fun_values,
                       const char * const *var_names, const double *var_values,
                       void *eval_ctx)
{
    double res;
    int ret;

    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values,
                                 NULL, NULL, fun_names, fun_values,
                                 eval_ctx, 0, log_ctx);
    if (ret < 0)
        av_log(log_ctx, AV_LOG_ERROR,
               "Text expansion expression '%s' is not valid\n",
               expr);
    else
        av_bprintf(bp, "%f", res);

    return ret;
}

int ff_print_formatted_eval_expr(void *log_ctx, AVBPrint *bp,
                                 const char *expr,
                                 const char * const *fun_names, const ff_eval_func2 *fun_values,
                                 const char * const *var_names, const double *var_values,
                                 void *eval_ctx,
                                 const char format, int positions)
{
    double res;
    int intval;
    int ret;
    char fmt_str[30] = "%";

    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values,
                                 NULL, NULL, fun_names, fun_values,
                                 eval_ctx, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Text expansion expression '%s' is not valid\n",
               expr);
        return ret;
    }

    if (!strchr("xXdu", format)) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid format '%c' specified,"
                " allowed values: 'x', 'X', 'd', 'u'\n", format);
        return AVERROR(EINVAL);
    }

    feclearexcept(FE_ALL_EXCEPT);
    intval = res;
#if defined(FE_INVALID) && defined(FE_OVERFLOW) && defined(FE_UNDERFLOW)
    if ((ret = fetestexcept(FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW))) {
        av_log(log_ctx, AV_LOG_ERROR, "Conversion of floating-point result to int failed. Control register: 0x%08x. Conversion result: %d\n", ret, intval);
        return AVERROR(EINVAL);
    }
#endif

    if (positions >= 0)
        av_strlcatf(fmt_str, sizeof(fmt_str), "0%u", positions);
    av_strlcatf(fmt_str, sizeof(fmt_str), "%c", format);

    av_log(log_ctx, AV_LOG_DEBUG, "Formatting value %f (expr '%s') with spec '%s'\n",
           res, expr, fmt_str);

    av_bprintf(bp, fmt_str, intval);

    return 0;
}


int ff_load_textfile(void *log_ctx, const char *textfile,
                     unsigned char **text, size_t *text_size)
{
    int err;
    uint8_t *textbuf;
    uint8_t *tmp;
    size_t textbuf_size;

    if ((err = av_file_map(textfile, &textbuf, &textbuf_size, 0, log_ctx)) < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "The text file '%s' could not be read or is empty\n",
               textfile);
        return err;
    }

    if (textbuf_size > 0 && ff_is_newline(textbuf[textbuf_size - 1]))
        textbuf_size--;
    if (textbuf_size > SIZE_MAX - 1 || !(tmp = av_realloc(*text, textbuf_size + 1))) {
        av_file_unmap(textbuf, textbuf_size);
        return AVERROR(ENOMEM);
    }
    *text = tmp;
    memcpy(*text, textbuf, textbuf_size);
    (*text)[textbuf_size] = 0;
    if (text_size)
        *text_size = textbuf_size;
    av_file_unmap(textbuf, textbuf_size);

    return 0;
}

