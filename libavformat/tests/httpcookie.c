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

#include <inttypes.h>
#include <stdio.h>

#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"
#include "libavformat/http.h"

static const char *const cookies[] = {
    "sid=secret",
    "sid= secret ; Path= /a ; Domain= .example.com",
    "domain=localhost",
    "path=secret; Domain=example.com; Path=/",
    "sid=secret; =domain=example.com",
    "sid=secret; Path=abc",
    "sid=secret; Path=/a; Path=\"/\"",
    "sid=secret; Secure; HttpOnly",
    "sid=secret; Max-Age=0; Expires=Wed, 09 Jun 2100 10:18:14 GMT",
    "sid=secret; Max-Age=-1",
    "sid=secret; Max-Age=0; Max-Age=bad",
    "sid=secret; Expires=Sunday, 06-Nov-1994 08:49:37 GMT; Expires=bad",
    "sid=secret; Domain=example.com; Domain=",
    "sid=secret; Max-Age=-; Expires=Wed, 09 Jun 2100 10:18:14 GMT",
    "sid=secret; Max-Age=1x; Expires=Sunday, 06-Nov-1994 08:49:37 GMT",
    "sid=sec\tret",
    "sid=sec\rret",
    "si\x7f" "d=secret",
    "sid",
    "=secret",
    "; sid=secret",
    "",
};

static const char *const dates[] = {
    "Wed, 21 Oct 2015 07:28:00 GMT",
    "Wednesday, 21-Oct-15 07:28:00 GMT",
    "Wed Oct 21 07:28:00 2015",
    "Thu, 01 Jan 1970 00:00:00 GMT",
    "Thu, 29 Feb 2024 12:00:00 GMT",
    "Thu, 29 Feb 2023 12:00:00 GMT",
    "Wed, 21 Oct 1600 07:28:00 GMT",
    "Wed, 21 Oct 2015 24:00:00 GMT",
    "120",
};

int main(void)
{
    char *bound = av_strdup("sid=secret\ndomain=localhost\nsid=secret; Domain=example.com\n"
                            "sid=secret; @hostonly=other\r\n");

    for (int i = 0; i < FF_ARRAY_ELEMS(cookies); i++) {
        AVDictionary *pair = NULL, *attributes = NULL;
        const AVDictionaryEntry *e = NULL;
        const char *domain;
        int ret = ff_http_parse_set_cookie(cookies[i], &pair, &attributes);

        printf("%d:", i);
        if (ret < 0) {
            printf(" %s\n", ret == AVERROR_INVALIDDATA ? "no cookie" : "error");
        } else {
            e = av_dict_iterate(pair, NULL);
            printf(" [%s]=[%s]", e->key, e->value);
            for (e = NULL; (e = av_dict_iterate(attributes, e)); )
                printf(" [%s]=[%s]", e->key, e->value);
            domain = ff_http_cookie_domain(attributes);
            printf(" domain=%s expiry=%"PRId64"\n", domain ? domain : "-",
                   FFMIN(ff_http_cookie_expiry(attributes), INT32_MAX));
        }
        av_dict_free(&pair);
        av_dict_free(&attributes);
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(dates); i++) {
        struct tm tm;

        if (ff_http_parse_date(dates[i], &tm) < 0)
            printf("%s: invalid\n", dates[i]);
        else
            printf("%s: %"PRId64"\n", dates[i], (int64_t)av_timegm(&tm));
    }

    if (!bound || ff_http_bind_cookies_to_host(&bound, "example.com") < 0)
        return 1;
    printf("%s", bound);
    av_free(bound);
    return 0;
}
