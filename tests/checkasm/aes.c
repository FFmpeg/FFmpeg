/*
 * Copyright (c) 2015 Rodger Combs <rodger.combs@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "checkasm.h"
#include "libavutil/aes.h"
#include "libavutil/aes_internal.h"
#include "libavutil/internal.h"

#define MAX_COUNT 16

void checkasm_check_aes(void)
{
    int i, j, d;
    AVAES b;
    uint8_t pt[MAX_COUNT * 16];
    uint8_t temp[2][MAX_COUNT * 16];
    uint8_t iv[2][16];

    for (d = 0; d <= 1; d++) {
        for (i = 128; i <= 256; i += 64) {
            av_aes_init(&b, (const uint8_t*)"PI=3.1415926535897932384626433..", i, d);
            if (check_func(b.crypt, "aes_%scrypt_%i", d ? "de" : "en", i)) {
                declare_func(void, AVAES *a, uint8_t *dst, const uint8_t *src,
                             int count, uint8_t *iv, int rounds);
                int count = (rnd() & (MAX_COUNT - 1)) + 1;
                for (j = 0; j < 16 * MAX_COUNT; j++)
                    pt[j] = rnd();
                for (j = 0; j < 16; j++)
                    iv[0][j] = iv[1][j] = rnd();
                call_ref(&b, temp[0], pt, count, iv[0], b.rounds);
                call_new(&b, temp[1], pt, count, iv[1], b.rounds);
                if (memcmp(temp[0], temp[1], sizeof(16 * count)))
                    fail();
                if (memcmp(iv[0], iv[1], sizeof(iv[0])))
                    fail();
                call_ref(&b, temp[0], pt, count, NULL, b.rounds);
                call_new(&b, temp[1], pt, count, NULL, b.rounds);
                if (memcmp(temp[0], temp[1], sizeof(16 * count)))
                    fail();
                if (memcmp(iv[0], iv[1], sizeof(iv[0])))
                    fail();
                bench_new(&b, temp[1], pt, MAX_COUNT, NULL, b.rounds);
            }
        }
        report("%scrypt", d ? "de" : "en");
    }
}
