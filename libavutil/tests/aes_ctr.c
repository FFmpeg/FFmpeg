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

#include "libavutil/log.h"
#include "libavutil/mem_internal.h"
#include "libavutil/aes_ctr.h"

static const DECLARE_ALIGNED(8, uint8_t, plain)[] = {
    0x6d, 0x6f, 0x73, 0x74, 0x20, 0x72, 0x61, 0x6e, 0x64, 0x6f, 0x6d
};
static DECLARE_ALIGNED(8, uint8_t, tmp)[11];

int main (void)
{
    int ret = 1;
    struct AVAESCTR *ae, *ad;
    const uint8_t *iv;

    ae = av_aes_ctr_alloc();
    ad = av_aes_ctr_alloc();

    if (!ae || !ad)
        goto ERROR;

    if (av_aes_ctr_init(ae, (const uint8_t*)"0123456789abcdef") < 0)
        goto ERROR;

    if (av_aes_ctr_init(ad, (const uint8_t*)"0123456789abcdef") < 0)
        goto ERROR;

    av_aes_ctr_set_random_iv(ae);
    iv =   av_aes_ctr_get_iv(ae);
    av_aes_ctr_set_full_iv(ad, iv);

    av_aes_ctr_crypt(ae, tmp, plain, sizeof(tmp));
    av_aes_ctr_crypt(ad, tmp, tmp,   sizeof(tmp));

    if (memcmp(tmp, plain, sizeof(tmp)) != 0){
        av_log(NULL, AV_LOG_ERROR, "test failed\n");
        goto ERROR;
    }

    av_log(NULL, AV_LOG_INFO, "test passed\n");
    ret = 0;

ERROR:
    av_aes_ctr_free(ae);
    av_aes_ctr_free(ad);
    return ret;
}
