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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"

#include "http.h"

#define WHITESPACES " \n\t\r"

static int is_date_delimiter(uint8_t c)
{
    return c == 0x09 || (c >= 0x20 && c <= 0x2F) || (c >= 0x3B && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

/* Parse min to max digits not followed by a digit, return their number or 0. */
static int parse_digits(const char *str, int min, int max, int *val)
{
    int n;

    for (n = *val = 0; n < max && av_isdigit(str[n]); n++)
        *val = *val * 10 + str[n] - '0';
    return n >= min && !av_isdigit(str[n]) ? n : 0;
}

static int parse_time(const char *str, struct tm *tm)
{
    int n, m;

    return (n = parse_digits(str, 1, 2, &tm->tm_hour)) && str[n++] == ':' &&
           (m = parse_digits(str + n, 1, 2, &tm->tm_min)) && str[n + m++] == ':' &&
           parse_digits(str + n + m, 1, 2, &tm->tm_sec);
}

static int parse_month(const char *str)
{
    static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";

    for (int i = 0; i < 12; i++)
        if (!av_strncasecmp(str, months + 3 * i, 3))
            return i;
    return -1;
}

int ff_http_parse_date(const char *date_str, struct tm *buf)
{
    static const uint8_t month_days[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int found_time = 0, found_day = 0, found_month = 0, found_year = 0, month, year;
    struct tm tm = { 0 };

    // cookie-date algorithm of RFC 6265 5.1.1, it accepts all HTTP-date formats too
    while (*date_str) {
        while (*date_str && is_date_delimiter(*date_str))
            date_str++;
        if (!*date_str)
            break;
        if (!found_time && parse_time(date_str, &tm)) {
            found_time = 1;
        } else if (!found_day && parse_digits(date_str, 1, 2, &tm.tm_mday)) {
            found_day = 1;
        } else if (!found_month && (month = parse_month(date_str)) >= 0) {
            tm.tm_mon   = month;
            found_month = 1;
        } else if (!found_year && parse_digits(date_str, 2, 4, &year)) {
            found_year = 1;
        }
        while (*date_str && !is_date_delimiter(*date_str))
            date_str++;
    }

    if (!found_time || !found_day || !found_month || !found_year)
        return AVERROR(EINVAL);
    year += year < 70 ? 2000 : year < 100 ? 1900 : 0;
    if (year < 1601 || tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 59 ||
        tm.tm_mday < 1 || tm.tm_mday > month_days[tm.tm_mon] ||
        (tm.tm_mon == 1 && tm.tm_mday == 29 && (year % 4 || (year % 100 == 0 && year % 400))))
        return AVERROR(EINVAL);
    tm.tm_year = year - 1900;
    *buf = tm;
    return 0;
}

static char *trim(char *str)
{
    char *end;

    str += strspn(str, WHITESPACES);
    end  = str + strlen(str);
    while (end > str && strchr(WHITESPACES, end[-1]))
        *--end = '\0';
    return str;
}

static int has_control_char(const char *str)
{
    for (; *str; str++)
        if (((uint8_t)*str < 0x20 && *str != '\t') || *str == 0x7f)
            return 1;
    return 0;
}

static int is_delta_seconds(const char *str)
{
    str += *str == '-';
    return *str && !str[strspn(str, "0123456789")];
}

int ff_http_parse_set_cookie(const char *set_cookie, AVDictionary **cookie, AVDictionary **attributes)
{
    char *param, *next_param, *cstr, *eql;
    char *saveptr = NULL;
    int ret;

    if (!(cstr = av_strdup(set_cookie)))
        return AVERROR(ENOMEM);

    // a line with control characters other than HTAB holds no cookie (RFC 6265bis 5.6)
    param = trim(cstr);
    if (has_control_char(param)) {
        av_free(cstr);
        return AVERROR_INVALIDDATA;
    }

    // the name and value of the cookie come before the first ';'
    next_param = strchr(param, ';');
    if (next_param)
        *next_param++ = '\0';
    if (!(eql = strchr(param, '=')) || eql == param) {
        av_free(cstr);
        return AVERROR_INVALIDDATA;
    }
    *eql = '\0';
    ret = av_dict_set(cookie, trim(param), trim(eql + 1), 0);

    while (ret >= 0 && (param = av_strtok(next_param, ";", &saveptr))) {
        char *value = strchr(param, '=');
        next_param = NULL;
        if (value)
            *value++ = '\0';
        param = trim(param);
        value = value ? trim(value) : NULL;
        // an invalid Max-Age is ignored (RFC 6265 5.2.2)
        if (!av_strcasecmp(param, "max-age") && (!value || !is_delta_seconds(value)))
            continue;
        // an Expires without a valid date and an empty Domain are ignored (RFC 6265 5.2.1, 5.2.3)
        if (!av_strcasecmp(param, "expires") && (!value || ff_http_parse_date(value, &(struct tm){ 0 }) < 0))
            continue;
        if (!av_strcasecmp(param, "domain") && (!value || !*value))
            continue;
        if (*param)
            ret = av_dict_set(attributes, param, value ? value : "", 0);
    }

    av_free(cstr);
    return ret < 0 ? ret : 0;
}

int ff_http_host_is_ip_literal(const char *host)
{
    return !host[strspn(host, "0123456789.")] || strchr(host, ':');
}

const char *ff_http_cookie_domain(const AVDictionary *cookie_params)
{
    const AVDictionaryEntry *e = av_dict_get(cookie_params, "domain", NULL, 0);
    const char *domain = e ? e->value + (e->value[0] == '.') : "";

    return *domain ? domain : NULL;
}

int64_t ff_http_cookie_expiry(const AVDictionary *attributes)
{
    const AVDictionaryEntry *max_age = av_dict_get(attributes, "max-age", NULL, 0);
    const AVDictionaryEntry *expires = av_dict_get(attributes, "expires", NULL, 0);
    struct tm tm = { 0 };

    // Max-Age takes precedence over Expires (RFC 6265 5.3)
    if (max_age) {
        int64_t delta = strtoll(max_age->value, NULL, 10);
        return delta > 0 ? av_sat_add64(time(NULL), delta) : 1;
    }
    if (expires && !ff_http_parse_date(expires->value, &tm))
        return FFMAX(av_timegm(&tm), 1);
    return 0;
}

int ff_http_bind_cookies_to_host(char **cookies, const char *host)
{
    char *copy, *cookie, *next, *saveptr = NULL, *bound;
    AVBPrint bp;
    int ret = 0;

    next = copy = av_strdup(*cookies);
    if (!copy)
        return AVERROR(ENOMEM);

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    while ((cookie = av_strtok(next, "\n", &saveptr))) {
        AVDictionary *pair = NULL, *params = NULL;
        int len = strlen(cookie), host_only;

        next = NULL;
        while (len && strchr(WHITESPACES, cookie[len - 1]))
            len--;
        ret = ff_http_parse_set_cookie(cookie, &pair, &params);
        host_only = !ff_http_cookie_domain(params) && !av_dict_get(params, "@hostonly", NULL, 0);
        av_dict_free(&pair);
        av_dict_free(&params);
        if (ret == AVERROR(ENOMEM))
            break;
        ret = 0;
        av_bprintf(&bp, "%.*s%s%s\n", len, cookie, host_only ? "; @hostonly=" : "", host_only ? host : "");
    }
    av_free(copy);
    if (ret >= 0 && !av_bprint_is_complete(&bp))
        ret = AVERROR(ENOMEM);
    if (ret < 0) {
        av_bprint_finalize(&bp, NULL);
        return ret;
    }
    if ((ret = av_bprint_finalize(&bp, &bound)) < 0)
        return ret;
    av_free(*cookies);
    *cookies = bound;
    return 0;
}
