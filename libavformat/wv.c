/*
 * WavPack shared functions
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

#include <stdint.h>
#include <string.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#include "wv.h"

int ff_wv_parse_header(WvHeader *wv, const uint8_t *data)
{
    memset(wv, 0, sizeof(*wv));

    if (AV_RL32(data) != MKTAG('w', 'v', 'p', 'k'))
        return AVERROR_INVALIDDATA;

    wv->blocksize     = AV_RL32(data + 4);
    if (wv->blocksize < 24 || wv->blocksize > WV_BLOCK_LIMIT)
        return AVERROR_INVALIDDATA;
    wv->blocksize -= 24;

    wv->version       = AV_RL16(data + 8);
    wv->total_samples = AV_RL32(data + 12);
    wv->block_idx     = AV_RL32(data + 16);
    wv->samples       = AV_RL32(data + 20);
    wv->flags         = AV_RL32(data + 24);
    wv->crc           = AV_RL32(data + 28);

    wv->initial = !!(wv->flags & WV_FLAG_INITIAL_BLOCK);
    wv->final   = !!(wv->flags & WV_FLAG_FINAL_BLOCK);

    return 0;
}
