/*
 * copyright (c) 2017 Raymond Zheng
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

#ifndef AVUTIL_DNS_CACHE_H
#define AVUTIL_DNS_CACHE_H

#include "libavutil/log.h"

typedef struct DnsCacheEntry {
    volatile int ref_count;
    volatile int delete_flag;
    int64_t expired_time;
    struct addrinfo *res;  // construct by private function, not support ai_next and ai_canonname, can only be released using free_private_addrinfo
} DnsCacheEntry;

DnsCacheEntry *get_dns_cache_reference(char *hostname);
int release_dns_cache_reference(char *hostname, DnsCacheEntry **p_entry);
int remove_dns_cache_entry(char *hostname);
int add_dns_cache_entry(char *hostname, struct addrinfo *cur_ai, int64_t timeout);

#endif /* AVUTIL_DNS_CACHE_H */
