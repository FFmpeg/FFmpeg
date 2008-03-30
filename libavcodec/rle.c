/*
 * RLE encoder
 * Copyright (c) 2007 Bobby Bingham
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
#include "avcodec.h"
#include "rle.h"

/**
 * Count up to 127 consecutive pixels which are either all the same or
 * all differ from the previous and next pixels.
 * @param start Pointer to the first pixel
 * @param len Maximum number of pixels
 * @param bpp Bytes per pixel
 * @param same 1 if searching for identical pixel values.  0 for differing
 * @return Number of matching consecutive pixels found
 */
static int count_pixels(const uint8_t *start, int len, int bpp, int same)
{
    const uint8_t *pos;
    int count = 1;

    for(pos = start + bpp; count < FFMIN(127, len); pos += bpp, count ++) {
        if(same != !memcmp(pos-bpp, pos, bpp)) {
            if(!same) {
                /* if bpp == 1, then 0 1 1 0 is more efficiently encoded as a single
                 * raw block of pixels.  for larger bpp, RLE is as good or better */
                if(bpp == 1 && count + 1 < FFMIN(127, len) && *pos != *(pos+1))
                    continue;

                /* if RLE can encode the next block better than as a raw block,
                 * back up and leave _all_ the identical pixels for RLE */
                count --;
            }
            break;
        }
    }

    return count;
}

int ff_rle_encode(uint8_t *outbuf, int out_size, const uint8_t *ptr , int bpp, int w,
                  int add_rep, int xor_rep, int add_raw, int xor_raw)
{
    int count, x;
    uint8_t *out = outbuf;

    for(x = 0; x < w; x += count) {
        /* see if we can encode the next set of pixels with RLE */
        if((count = count_pixels(ptr, w-x, bpp, 1)) > 1) {
            if(out + bpp + 1 > outbuf + out_size) return -1;
            *out++ = (count ^ xor_rep) + add_rep;
            memcpy(out, ptr, bpp);
            out += bpp;
        } else {
            /* fall back on uncompressed */
            count = count_pixels(ptr, w-x, bpp, 0);
            *out++ = (count ^ xor_raw) + add_raw;

            if(out + bpp*count > outbuf + out_size) return -1;
            memcpy(out, ptr, bpp * count);
            out += bpp * count;
        }

        ptr += count * bpp;
    }

    return out - outbuf;
}
