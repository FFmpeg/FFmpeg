/*
 * Copyright (c) 2009 Baptiste Coudurier <baptiste.coudurier@gmail.com>
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

#include "config.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_CRYPTGENRANDOM
#include <windows.h>
#include <wincrypt.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include "internal.h"
#include "intreadwrite.h"
#include "mem.h"
#include "timer.h"
#include "random_seed.h"
#include "sha.h"

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
    struct AVSHA *sha = av_sha_alloc();
    clock_t last_t  = 0;
    static uint64_t i = 0;
    static uint32_t buffer[512] = { 0 };
    unsigned char digest[20];
    uint64_t last_i = i;

    for (;;) {
        clock_t t = clock();

        if (last_t == t) {
            buffer[i & 511]++;
        } else {
            buffer[++i & 511] += (t - last_t) % 3294638521U;
            if (last_i && i - last_i > 4 || i - last_i > 64)
                break;
        }
        last_t = t;
    }

    if (!sha) {
        uint32_t seed = 0;
        int j;
        // Unable to allocate an SHA context, just XOR the buffer together
        // to create something hopefully unique.
        for (j = 0; j < 512; j++)
            seed ^= buffer[j];
        return seed;
    }
    av_sha_init(sha, 160);
    av_sha_update(sha, (const uint8_t *) buffer, sizeof(buffer));
    av_sha_final(sha, digest);
    av_free(sha);
    return AV_RB32(digest) + AV_RB32(digest + 16);
}

uint32_t av_get_random_seed(void)
{
    uint32_t seed;

#if HAVE_CRYPTGENRANDOM
    HCRYPTPROV provider;
    if (CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_FULL,
                            CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        BOOL ret = CryptGenRandom(provider, sizeof(seed), (PBYTE) &seed);
        CryptReleaseContext(provider, 0);
        if (ret)
            return seed;
    }
#endif

    if (read_random(&seed, "/dev/urandom") == sizeof(seed))
        return seed;
    if (read_random(&seed, "/dev/random")  == sizeof(seed))
        return seed;
    return get_generic_seed();
}
