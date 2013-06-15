/*
 * Copyright (c) 2012 Martin Storsjo
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

#include "url.h"

static void test(const char *base, const char *rel)
{
    char buf[200], buf2[200];
    ff_make_absolute_url(buf, sizeof(buf), base, rel);
    printf("%s\n", buf);
    if (base) {
        /* Test in-buffer replacement */
        snprintf(buf2, sizeof(buf2), "%s", base);
        ff_make_absolute_url(buf2, sizeof(buf2), buf2, rel);
        if (strcmp(buf, buf2)) {
            printf("In-place handling of %s + %s failed\n", base, rel);
            exit(1);
        }
    }
}

int main(void)
{
    test(NULL, "baz");
    test("/foo/bar", "baz");
    test("/foo/bar", "../baz");
    test("/foo/bar", "/baz");
    test("http://server/foo/", "baz");
    test("http://server/foo/bar", "baz");
    test("http://server/foo/", "../baz");
    test("http://server/foo/bar/123", "../../baz");
    test("http://server/foo/bar/123", "/baz");
    test("http://server/foo/bar/123", "https://other/url");
    test("http://server/foo/bar?param=value/with/slashes", "/baz");
    test("http://server/foo/bar?param&otherparam", "?someparam");
    test("http://server/foo/bar", "//other/url");
    return 0;
}
