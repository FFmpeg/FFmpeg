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

#include "config.h"

#include <stddef.h>
#include <stdint.h>

#include "adts_header.h"
#include "adts_parser.h"

int av_adts_header_parse(const uint8_t *buf, uint32_t *samples, uint8_t *frames)
{
#if CONFIG_ADTS_HEADER
    GetBitContext gb;
    AACADTSHeaderInfo hdr;
    int err = init_get_bits8(&gb, buf, AV_AAC_ADTS_HEADER_SIZE);
    if (err < 0)
        return err;
    err = ff_adts_header_parse(&gb, &hdr);
    if (err < 0)
        return err;
    *samples = hdr.samples;
    *frames  = hdr.num_aac_frames;
    return 0;
#else
    return AVERROR(ENOSYS);
#endif
}

int avpriv_adts_header_parse(AACADTSHeaderInfo **phdr, const uint8_t *buf, size_t size)
{
#if CONFIG_ADTS_HEADER
    int ret = 0;
    GetBitContext gb;

    if (!phdr || !buf || size < AV_AAC_ADTS_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    *phdr = av_mallocz(sizeof(AACADTSHeaderInfo));
    if (!*phdr)
        return AVERROR(ENOMEM);

    ret = init_get_bits8(&gb, buf, AV_AAC_ADTS_HEADER_SIZE);
    if (ret < 0) {
        av_freep(phdr);
        return ret;
    }

    ret = ff_adts_header_parse(&gb, *phdr);
    if (ret < 0) {
        av_freep(phdr);
        return ret;
    }

    return 0;
#else
    return AVERROR(ENOSYS);
#endif
}
