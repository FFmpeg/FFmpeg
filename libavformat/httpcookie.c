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

#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"

#include "http.h"

#define MAX_DATE_LEN  19
#define WHITESPACES " \n\t\r"

int ff_http_parse_date(const char *date_str, struct tm *buf)
{
    char date_buf[MAX_DATE_LEN];
    int i, j, date_buf_len = MAX_DATE_LEN-1;
    char *date;

    // strip off any punctuation or whitespace
    for (i = 0, j = 0; date_str[i] != '\0' && j < date_buf_len; i++) {
        if ((date_str[i] >= '0' && date_str[i] <= '9') ||
            (date_str[i] >= 'A' && date_str[i] <= 'Z') ||
            (date_str[i] >= 'a' && date_str[i] <= 'z')) {
            date_buf[j] = date_str[i];
            j++;
        }
    }
    date_buf[j] = '\0';
    date = date_buf;

    // move the string beyond the day of week
    while ((*date < '0' || *date > '9') && *date != '\0')
        date++;

    return av_small_strptime(date, "%d%b%Y%H%M%S", buf) ? 0 : AVERROR(EINVAL);
}

int ff_http_parse_set_cookie(const char *set_cookie, AVDictionary **dict)
{
    char *param, *next_param, *cstr, *back;
    char *saveptr = NULL;

    if (!set_cookie[0])
        return 0;

    if (!(cstr = av_strdup(set_cookie)))
        return AVERROR(EINVAL);

    // strip any trailing whitespace
    back = &cstr[strlen(cstr)-1];
    while (strchr(WHITESPACES, *back)) {
        *back='\0';
        if (back == cstr)
            break;
        back--;
    }

    next_param = cstr;
    while ((param = av_strtok(next_param, ";", &saveptr))) {
        char *name, *value;
        next_param = NULL;
        param += strspn(param, WHITESPACES);
        if ((name = av_strtok(param, "=", &value))) {
            char *end = name + strlen(name);
            while (end > name && strchr(WHITESPACES, end[-1]))
                *--end = '\0';
            if (av_dict_set(dict, name, value ? value : "", 0) < 0) {
                av_free(cstr);
                return -1;
            }
        }
    }

    av_free(cstr);
    return 0;
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
        AVDictionary *params = NULL;
        int len = strlen(cookie), host_only;

        next = NULL;
        while (len && strchr(WHITESPACES, cookie[len - 1]))
            len--;
        if ((ret = ff_http_parse_set_cookie(cookie, &params)) < 0) {
            av_dict_free(&params);
            break;
        }
        host_only = !ff_http_cookie_domain(params) && !av_dict_get(params, "@hostonly", NULL, 0);
        av_dict_free(&params);
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
