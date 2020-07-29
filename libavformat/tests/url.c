/*
 * Copyright (c) 2012 Martin Storsjo
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

#include "libavformat/url.h"
#include "libavformat/avformat.h"

static void test_decompose(const char *url)
{
    URLComponents uc;
    int len, ret;

    printf("%s =>\n", url);
    ret = ff_url_decompose(&uc, url, NULL);
    if (ret < 0) {
        printf("  error: %s\n", av_err2str(ret));
        return;
    }
#define PRINT_COMPONENT(comp) \
    len = uc.url_component_end_##comp - uc.comp; \
    if (len) printf("  "#comp": %.*s\n", len, uc.comp);
    PRINT_COMPONENT(scheme);
    PRINT_COMPONENT(authority);
    PRINT_COMPONENT(userinfo);
    PRINT_COMPONENT(host);
    PRINT_COMPONENT(port);
    PRINT_COMPONENT(path);
    PRINT_COMPONENT(query);
    PRINT_COMPONENT(fragment);
    printf("\n");
}

static void test(const char *base, const char *rel)
{
    char buf[200], buf2[200];
    int ret;

    ret = ff_make_absolute_url(buf, sizeof(buf), base, rel);
    if (ret < 0) {
        printf("%50s %-20s => error %s\n", base, rel, av_err2str(ret));
        return;
    }
    printf("%50s %-20s => %s\n", base, rel, buf);
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

static void test2(const char *url)
{
    char proto[64];
    char auth[256];
    char host[256];
    char path[256];
    int port=-1;

    av_url_split(proto, sizeof(proto), auth, sizeof(auth), host, sizeof(host), &port, path, sizeof(path), url);
    printf("%-60s => %-15s %-15s %-15s %5d %s\n", url, proto, auth, host, port, path);
}

int main(void)
{
    printf("Testing ff_url_decompose:\n\n");
    test_decompose("http://user:pass@ffmpeg:8080/dir/file?query#fragment");
    test_decompose("http://ffmpeg/dir/file");
    test_decompose("file:///dev/null");
    test_decompose("file:/dev/null");
    test_decompose("http://[::1]/dev/null");
    test_decompose("http://[::1]:8080/dev/null");
    test_decompose("//ffmpeg/dev/null");

    printf("Testing ff_make_absolute_url:\n");
    test(NULL, "baz");
    test("/foo/bar", "baz");
    test("/foo/bar", "../baz");
    test("/foo/bar", "/baz");
    test("/foo/bar", "../../../baz");
    test("http://server/foo/", "baz");
    test("http://server/foo/bar", "baz");
    test("http://server/foo/", "../baz");
    test("http://server/foo/bar/123", "../../baz");
    test("http://server/foo/bar/123", "/baz");
    test("http://server/foo/bar/123", "https://other/url");
    test("http://server/foo/bar?param=value/with/slashes", "/baz");
    test("http://server/foo/bar?param&otherparam", "?someparam");
    test("http://server/foo/bar", "//other/url");
    test("http://server/foo/bar", "../../../../../other/url");
    test("http://server/foo/bar", "/../../../../../other/url");
    test("http://server/foo/bar", "/test/../../../../../other/url");
    test("http://server/foo/bar", "/test/../../test/../../../other/url");
    test("http://server/foo/bar", "file:../baz/qux");
    test("http://server/foo//bar/", "../../");
    test("file:../tmp/foo", "../bar/");
    test("file:../tmp/foo", "file:../bar/");
    test("http://server/foo/bar", "./");
    test("http://server/foo/bar", ".dotfile");
    test("http://server/foo/bar", "..doubledotfile");
    test("http://server/foo/bar", "double..dotfile");
    test("http://server/foo/bar", "doubledotfile..");

    /* From https://tools.ietf.org/html/rfc3986#section-5.4 */
    test("http://a/b/c/d;p?q", "g:h");           // g:h
    test("http://a/b/c/d;p?q", "g");             // http://a/b/c/g
    test("http://a/b/c/d;p?q", "./g");           // http://a/b/c/g
    test("http://a/b/c/d;p?q", "g/");            // http://a/b/c/g/
    test("http://a/b/c/d;p?q", "/g");            // http://a/g
    test("http://a/b/c/d;p?q", "//g");           // http://g
    test("http://a/b/c/d;p?q", "?y");            // http://a/b/c/d;p?y
    test("http://a/b/c/d;p?q", "g?y");           // http://a/b/c/g?y
    test("http://a/b/c/d;p?q", "#s");            // http://a/b/c/d;p?q#s
    test("http://a/b/c/d;p?q", "g#s");           // http://a/b/c/g#s
    test("http://a/b/c/d;p?q", "g?y#s");         // http://a/b/c/g?y#s
    test("http://a/b/c/d;p?q", ";x");            // http://a/b/c/;x
    test("http://a/b/c/d;p?q", "g;x");           // http://a/b/c/g;x
    test("http://a/b/c/d;p?q", "g;x?y#s");       // http://a/b/c/g;x?y#s
    test("http://a/b/c/d;p?q", "");              // http://a/b/c/d;p?q
    test("http://a/b/c/d;p?q", ".");             // http://a/b/c/
    test("http://a/b/c/d;p?q", "./");            // http://a/b/c/
    test("http://a/b/c/d;p?q", "..");            // http://a/b/
    test("http://a/b/c/d;p?q", "../");           // http://a/b/
    test("http://a/b/c/d;p?q", "../g");          // http://a/b/g
    test("http://a/b/c/d;p?q", "../..");         // http://a/
    test("http://a/b/c/d;p?q", "../../");        // http://a/
    test("http://a/b/c/d;p?q", "../../g");       // http://a/g
    test("http://a/b/c/d;p?q", "../../../g");    // http://a/g
    test("http://a/b/c/d;p?q", "../../../../g"); // http://a/g
    test("http://a/b/c/d;p?q", "/./g");          // http://a/g
    test("http://a/b/c/d;p?q", "/../g");         // http://a/g
    test("http://a/b/c/d;p?q", "g.");            // http://a/b/c/g.
    test("http://a/b/c/d;p?q", ".g");            // http://a/b/c/.g
    test("http://a/b/c/d;p?q", "g..");           // http://a/b/c/g..
    test("http://a/b/c/d;p?q", "..g");           // http://a/b/c/..g
    test("http://a/b/c/d;p?q", "./../g");        // http://a/b/g
    test("http://a/b/c/d;p?q", "./g/.");         // http://a/b/c/g/
    test("http://a/b/c/d;p?q", "g/./h");         // http://a/b/c/g/h
    test("http://a/b/c/d;p?q", "g/../h");        // http://a/b/c/h
    test("http://a/b/c/d;p?q", "g;x=1/./y");     // http://a/b/c/g;x=1/y
    test("http://a/b/c/d;p?q", "g;x=1/../y");    // http://a/b/c/y
    test("http://a/b/c/d;p?q", "g?y/./x");       // http://a/b/c/g?y/./x
    test("http://a/b/c/d;p?q", "g?y/../x");      // http://a/b/c/g?y/../x
    test("http://a/b/c/d;p?q", "g#s/./x");       // http://a/b/c/g#s/./x
    test("http://a/b/c/d;p?q", "g#s/../x");      // http://a/b/c/g#s/../x

    printf("\nTesting av_url_split:\n");
    test2("/foo/bar");
    test2("http://server/foo/");
    test2("http://example.com/foo/bar");
    test2("http://user:pass@localhost:8080/foo/bar/123");
    test2("http://server/foo/bar?param=value/with/slashes");
    test2("https://1l-lh.a.net/i/1LIVE_HDS@179577/master.m3u8");
    test2("ftp://u:p%2B%2F2@ftp.pbt.com/ExportHD.mpg");
    test2("https://key.dns.com?key_id=2&model_id=12345&&access_key=");
    test2("http://example.com#tag");

    return 0;
}
