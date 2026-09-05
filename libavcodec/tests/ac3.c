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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/macros.h"
#include "libavcodec/ac3.h"
#include "libavcodec/ac3dsp.h"
#include "libavcodec/ac3tab.h"

static uint32_t hash_int16(const AVCRC *crc, uint32_t hash,
                          const int16_t *data, int count)
{
    uint8_t bytes[2 * AC3_MAX_COEFS];

    for (int i = 0; i < count; i++)
        AV_WL16(bytes + 2 * i, data[i]);
    return av_crc(crc, hash, bytes, 2 * count);
}

int main(void)
{
    static const int ends[] = { 7, 27, 28, 29, 37, 73, 133, 229, 253 };
    static const int offsets[] = { -960, -959, 0, 1024, 3132 };
    const AVCRC *crc = av_crc_get_table(AV_CRC_32_IEEE);
    int8_t exp[AC3_MAX_COEFS];
    int16_t psd[AC3_MAX_COEFS], band_psd[AC3_CRITICAL_BANDS];
    int16_t mask[AC3_CRITICAL_BANDS];
    uint8_t bap[AC3_MAX_COEFS];
    AC3DSPContext dsp;
    AVLFG lfg;

    ff_ac3dsp_init(&dsp);
    av_lfg_init(&lfg, 1);
    for (int i = 0; i < AC3_CRITICAL_BANDS; i++)
        mask[i] = av_lfg_get(&lfg) % 3073;

    for (int pattern = 0; pattern < 4; pattern++) {
        uint32_t psd_hash = 0, bap_hash = 0;

        for (int i = 0; i < AC3_MAX_COEFS; i++)
            exp[i] = pattern == 0 ? 0 : pattern == 1 ? 24 :
                     pattern == 2 ? (i & 1) * 24 : av_lfg_get(&lfg) % 25;

        for (int start = 0; start < 253; start++) {
            for (int j = 0; j <= FF_ARRAY_ELEMS(ends); j++) {
                int end = j ? ends[j - 1] : start + 1;

                if (start >= end)
                    continue;
                memset(psd,      0x5a, sizeof(psd));
                memset(band_psd, 0x5a, sizeof(band_psd));
                ff_ac3_bit_alloc_calc_psd(exp, start, end, psd, band_psd);
                psd_hash = hash_int16(crc, psd_hash, psd, AC3_MAX_COEFS);
                psd_hash = hash_int16(crc, psd_hash, band_psd, AC3_CRITICAL_BANDS);

                for (int floor = 0; floor < 8; floor++) {
                    for (int k = 0; k < FF_ARRAY_ELEMS(offsets); k++) {
                        memset(bap, 0x5a, sizeof(bap));
                        dsp.bit_alloc_calc_bap(mask, psd, start, end,
                                               offsets[k], ff_ac3_floor_tab[floor],
                                               ff_ac3_bap_tab, bap);
                        bap_hash = av_crc(crc, bap_hash, bap, sizeof(bap));
                    }
                }
            }
        }
        printf("pattern %d: psd %08"PRIx32" bap %08"PRIx32"\n",
               pattern, psd_hash, bap_hash);
    }
    return 0;
}
