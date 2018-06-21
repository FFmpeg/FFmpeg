/*
 * AES-CTR cipher
 * Copyright (c) 2015 Eran Kornblau <erankor at gmail dot com>
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

#include "common.h"
#include "aes_ctr.h"
#include "aes.h"
#include "random_seed.h"

#define AES_BLOCK_SIZE (16)

typedef struct AVAESCTR {
    struct AVAES* aes;
    uint8_t counter[AES_BLOCK_SIZE];
    uint8_t encrypted_counter[AES_BLOCK_SIZE];
    int block_offset;
} AVAESCTR;

struct AVAESCTR *av_aes_ctr_alloc(void)
{
    return av_mallocz(sizeof(struct AVAESCTR));
}

void av_aes_ctr_set_iv(struct AVAESCTR *a, const uint8_t* iv)
{
    memcpy(a->counter, iv, AES_CTR_IV_SIZE);
    memset(a->counter + AES_CTR_IV_SIZE, 0, sizeof(a->counter) - AES_CTR_IV_SIZE);
    a->block_offset = 0;
}

void av_aes_ctr_set_full_iv(struct AVAESCTR *a, const uint8_t* iv)
{
    memcpy(a->counter, iv, sizeof(a->counter));
    a->block_offset = 0;
}

const uint8_t* av_aes_ctr_get_iv(struct AVAESCTR *a)
{
    return a->counter;
}

void av_aes_ctr_set_random_iv(struct AVAESCTR *a)
{
    uint32_t iv[2];

    iv[0] = av_get_random_seed();
    iv[1] = av_get_random_seed();

    av_aes_ctr_set_iv(a, (uint8_t*)iv);
}

int av_aes_ctr_init(struct AVAESCTR *a, const uint8_t *key)
{
    a->aes = av_aes_alloc();
    if (!a->aes) {
        return AVERROR(ENOMEM);
    }

    av_aes_init(a->aes, key, 128, 0);

    memset(a->counter, 0, sizeof(a->counter));
    a->block_offset = 0;

    return 0;
}

void av_aes_ctr_free(struct AVAESCTR *a)
{
    if (a) {
        av_freep(&a->aes);
        av_free(a);
    }
}

static void av_aes_ctr_increment_be64(uint8_t* counter)
{
    uint8_t* cur_pos;

    for (cur_pos = counter + 7; cur_pos >= counter; cur_pos--) {
        (*cur_pos)++;
        if (*cur_pos != 0) {
            break;
        }
    }
}

void av_aes_ctr_increment_iv(struct AVAESCTR *a)
{
    av_aes_ctr_increment_be64(a->counter);
    memset(a->counter + AES_CTR_IV_SIZE, 0, sizeof(a->counter) - AES_CTR_IV_SIZE);
    a->block_offset = 0;
}

void av_aes_ctr_crypt(struct AVAESCTR *a, uint8_t *dst, const uint8_t *src, int count)
{
    const uint8_t* src_end = src + count;
    const uint8_t* cur_end_pos;
    uint8_t* encrypted_counter_pos;

    while (src < src_end) {
        if (a->block_offset == 0) {
            av_aes_crypt(a->aes, a->encrypted_counter, a->counter, 1, NULL, 0);

            av_aes_ctr_increment_be64(a->counter + 8);
        }

        encrypted_counter_pos = a->encrypted_counter + a->block_offset;
        cur_end_pos = src + AES_BLOCK_SIZE - a->block_offset;
        cur_end_pos = FFMIN(cur_end_pos, src_end);

        a->block_offset += cur_end_pos - src;
        a->block_offset &= (AES_BLOCK_SIZE - 1);

        while (src < cur_end_pos) {
            *dst++ = *src++ ^ *encrypted_counter_pos++;
        }
    }
}
