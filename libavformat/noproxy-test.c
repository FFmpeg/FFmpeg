/*
 * Copyright (c) 2013 Martin Storsjo
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

#include "network.h"

static void test(const char *pattern, const char *host)
{
    int res = ff_http_match_no_proxy(pattern, host);
    printf("The pattern \"%s\" %s the hostname %s\n",
           pattern ? pattern : "(null)", res ? "matches" : "does not match",
           host);
}

int main(void)
{
    test(NULL, "domain.com");
    test("example.com domain.com", "domain.com");
    test("example.com other.com", "domain.com");
    test("example.com,domain.com", "domain.com");
    test("example.com,domain.com", "otherdomain.com");
    test("example.com, *.domain.com", "sub.domain.com");
    test("example.com, *.domain.com", "domain.com");
    test("example.com, .domain.com", "domain.com");
    test("*", "domain.com");
    return 0;
}
