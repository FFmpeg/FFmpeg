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

#include <stdio.h>

#include "libavutil/mem.h"
#include "libavformat/avio.h"
#include "libavformat/id3v2.h"

int main(void)
{
    for (int version = 3; version <= 4; version++) {
        ID3v2EncContext id3 = { 0 };

        for (int tag = 0; tag < 3; tag++) {
            AVIOContext *pb;
            uint8_t *buf;
            int initial_len, size;

            /* Test zeroed, reused, and explicitly nonzero contexts. */
            if (tag == 2)
                id3.len = 123;

            if (avio_open_dyn_buf(&pb) < 0)
                return 1;

            ff_id3v2_start(&id3, pb, version, ID3v2_DEFAULT_MAGIC);
            initial_len = id3.len;
            ff_id3v2_finish(&id3, pb, 10);
            size = avio_close_dyn_buf(pb, &buf);
            if (size < ID3v2_HEADER_SIZE) {
                av_free(buf);
                return 1;
            }
            printf("version=%d tag=%d initial_len=%d declared=%d actual=%d\n",
                   version, tag, initial_len, ff_id3v2_tag_len(buf), size);
            av_free(buf);
        }
    }
    return 0;
}
