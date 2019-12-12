/*
 * Copyright (c) 2009 Baptiste Coudurier <baptiste.coudurier@gmail.com>
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

#include "config.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#if HAVE_BCRYPT
#include <windows.h>
#include <bcrypt.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "avassert.h"
#include "internal.h"
#include "intreadwrite.h"
#include "timer.h"
#include "random_seed.h"
#include "sha.h"

#ifndef TEST
#define TEST 0
#endif

static int read_random(uint32_t *dst, const char *file)
{
#if HAVE_UNISTD_H
    int fd = avpriv_open(file, O_RDONLY);
    int err = -1;

    if (fd == -1)
        return -1;
    err = read(fd, dst, sizeof(*dst));
    close(fd);

    return err;
#else
    return -1;
#endif
}

static uint32_t get_generic_seed(void)
{
    uint64_t tmp[120/8];
    struct AVSHA *sha = (void*)tmp;
    clock_t last_t  = 0;
    clock_t last_td = 0;
    clock_t init_t = 0;
    static uint64_t i = 0;
    static uint32_t buffer[512] = { 0 };
    unsigned char digest[20];
    uint64_t last_i = i;

    av_assert0(sizeof(tmp) >= av_sha_size);

    if(TEST){
        memset(buffer, 0, sizeof(buffer));
        last_i = i = 0;
    }else{
#ifdef AV_READ_TIME
        buffer[13] ^= AV_READ_TIME();
        buffer[41] ^= AV_READ_TIME()>>32;
#endif
    }

    for (;;) {
        clock_t t = clock();
        if (last_t + 2*last_td + (CLOCKS_PER_SEC > 1000) >= t) {
            last_td = t - last_t;
            buffer[i & 511] = 1664525*buffer[i & 511] + 1013904223 + (last_td % 3294638521U);
        } else {
            last_td = t - last_t;
            buffer[++i & 511] += last_td % 3294638521U;
            if ((t - init_t) >= CLOCKS_PER_SEC>>5)
                if (last_i && i - last_i > 4 || i - last_i > 64 || TEST && i - last_i > 8)
                    break;
        }
        last_t = t;
        if (!init_t)
            init_t = t;
    }

    if(TEST) {
        buffer[0] = buffer[1] = 0;
    } else {
#ifdef AV_READ_TIME
        buffer[111] += AV_READ_TIME();
#endif
    }

    av_sha_init(sha, 160);
    av_sha_update(sha, (const uint8_t *)buffer, sizeof(buffer));
    av_sha_final(sha, digest);
    return AV_RB32(digest) + AV_RB32(digest + 16);
}

uint32_t av_get_random_seed(void)
{
    uint32_t seed;

#if HAVE_BCRYPT
    BCRYPT_ALG_HANDLE algo_handle;
    NTSTATUS ret = BCryptOpenAlgorithmProvider(&algo_handle, BCRYPT_RNG_ALGORITHM,
                                               MS_PRIMITIVE_PROVIDER, 0);
    if (BCRYPT_SUCCESS(ret)) {
        NTSTATUS ret = BCryptGenRandom(algo_handle, (UCHAR*)&seed, sizeof(seed), 0);
        BCryptCloseAlgorithmProvider(algo_handle, 0);
        if (BCRYPT_SUCCESS(ret))
            return seed;
    }
#endif

#if HAVE_ARC4RANDOM
    return arc4random();
#endif

    if (read_random(&seed, "/dev/urandom") == sizeof(seed))
        return seed;
    if (read_random(&seed, "/dev/random")  == sizeof(seed))
        return seed;
    return get_generic_seed();
}
