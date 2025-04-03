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

#include "libavutil/random_seed.h"
#include "libavutil/log.h"
#include "libavutil/mem_internal.h"
#include "libavutil/aes_ctr.h"

static const DECLARE_ALIGNED(8, uint8_t, plain)[] = {
    0x6d, 0x6f, 0x73, 0x74, 0x20, 0x72, 0x61, 0x6e, 0x64, 0x6f,
    0x6d, 0x6f, 0x73, 0x74, 0x20, 0x72, 0x61, 0x6e, 0x64, 0x6f
};

static const DECLARE_ALIGNED(8, uint8_t, encrypted)[] = {
    0x95, 0xcd, 0x9a, 0x8a, 0x83, 0xa2, 0x1a, 0x84, 0x92, 0xed,
    0xd6, 0xf2, 0x57, 0x2f, 0x61, 0x98, 0xbc, 0x20, 0x98, 0xee
};

static const DECLARE_ALIGNED(8, uint8_t, fixed_iv)[] = {
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef
};

static const DECLARE_ALIGNED(8, uint8_t, fixed_key)[] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66
};

static DECLARE_ALIGNED(8, uint32_t, key)[4];

static DECLARE_ALIGNED(8, uint8_t, tmp)[20];

int main (void)
{
    int ret = 1;
    struct AVAESCTR *ae, *ad;
    const uint8_t *iv, *k;

    for (int i = 0; i < 2; i++) {
        ae = av_aes_ctr_alloc();
        ad = av_aes_ctr_alloc();

        if (!ae || !ad)
            goto ERROR;

        if (i)
            k = fixed_key;
        else {
            // Note: av_random_bytes() should be used in a real world scenario,
            // but since that function can fail, av_get_random_seed() is used
            // here for the purpose of this test, as its output is sufficient.
            key[0] = av_get_random_seed();
            key[1] = av_get_random_seed();
            key[2] = av_get_random_seed();
            key[3] = av_get_random_seed();
            k = (uint8_t *)key;
        }

        if (av_aes_ctr_init(ae, k) < 0)
            goto ERROR;

        if (av_aes_ctr_init(ad, k) < 0)
            goto ERROR;

        if (i)
            av_aes_ctr_set_iv(ae, fixed_iv);
        else
            av_aes_ctr_set_random_iv(ae);
        iv =   av_aes_ctr_get_iv(ae);
        av_aes_ctr_set_full_iv(ad, iv);

        av_aes_ctr_crypt(ae, tmp, plain, sizeof(tmp));
        if (i && memcmp(tmp, encrypted, sizeof(tmp)) != 0) {
            av_log(NULL, AV_LOG_ERROR, "test failed\n");
            goto ERROR;
        }

        av_aes_ctr_crypt(ad, tmp, tmp,   sizeof(tmp));
        if (memcmp(tmp, plain, sizeof(tmp)) != 0){
            av_log(NULL, AV_LOG_ERROR, "test failed\n");
            goto ERROR;
        }

        av_aes_ctr_free(ae);
        av_aes_ctr_free(ad);
        ae = ad = NULL;
    }

    av_log(NULL, AV_LOG_INFO, "test passed\n");
    ret = 0;

ERROR:
    av_aes_ctr_free(ae);
    av_aes_ctr_free(ad);
    return ret;
}
