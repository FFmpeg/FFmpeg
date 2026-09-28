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

#define OGG_PAGE_HEADER_SIZE 27
#define OGG_PAGE_MAX_SIZE    (OGG_PAGE_HEADER_SIZE + 255 + 255 * 255)

static uint8_t page[OGG_PAGE_MAX_SIZE];

int main(int argc, char **argv)
{
    const AVCRC *crc_table = av_crc_get_table(AV_CRC_32_IEEE);
    size_t nb_read;
    long pos = 0;
    int ret = 1;
    FILE *file;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.ogg>\n"
                "Print the header of every page and check its checksum.\n",
                argv[0]);
        return 1;
    }

    file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 1;
    }

    while ((nb_read = fread(page, 1, OGG_PAGE_HEADER_SIZE, file)) == OGG_PAGE_HEADER_SIZE) {
        int nb_segments = page[26], body_size = 0, page_size;
        uint32_t crc;

        if (memcmp(page, "OggS", 4) ||
            fread(page + OGG_PAGE_HEADER_SIZE, 1, nb_segments, file) != nb_segments) {
            fprintf(stderr, "Invalid page at %ld\n", pos);
            goto end;
        }

        for (int i = 0; i < nb_segments; i++)
            body_size += page[OGG_PAGE_HEADER_SIZE + i];

        page_size = OGG_PAGE_HEADER_SIZE + nb_segments + body_size;
        if (fread(page + page_size - body_size, 1, body_size, file) != body_size) {
            fprintf(stderr, "Truncated page at %ld\n", pos);
            goto end;
        }

        crc = AV_RB32(page + 22);
        AV_WB32(page + 22, 0);
        if (av_crc(crc_table, 0, page, page_size) != crc) {
            fprintf(stderr, "Checksum mismatch in page at %ld\n", pos);
            goto end;
        }

        printf("pos=%ld serial=%"PRIu32" sequence=%"PRIu32" granule=%"PRId64
               " flags=%c%c%c segments=%d size=%d\n",
               pos, AV_RL32(page + 14), AV_RL32(page + 18),
               (int64_t)AV_RL64(page + 6),
               page[5] & 1 ? 'c' : '-', page[5] & 2 ? 'b' : '-',
               page[5] & 4 ? 'e' : '-', nb_segments, body_size);

        pos += page_size;
    }

    ret = nb_read || ferror(file);
    if (ret)
        fprintf(stderr, "Truncated page at %ld\n", pos);

end:
    fclose(file);
    return ret;
}
