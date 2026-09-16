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

/**
 * Walks the MPEG audio frames of a file, checks that every frame header
 * encodes back to its original bits, and prints each distinct header.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/file.h"
#include "libavutil/mem.h"
#include "libavutil/intreadwrite.h"

#include "libavcodec/mpegaudiodecheader.h"

#define PADDING_BIT (1U << 9)

static size_t id3v2_size(const uint8_t *buf, size_t size)
{
    if (size < 10 || memcmp(buf, "ID3", 3))
        return 0;

    return 10 + (buf[5] & 0x10 ? 10 : 0) +
           ((buf[6] & 0x7f) << 21 | (buf[7] & 0x7f) << 14 |
            (buf[8] & 0x7f) <<  7 | (buf[9] & 0x7f));
}

static void print_header(size_t offset, unsigned frames, uint32_t bits,
                         const MPADecodeHeader2 *header)
{
    printf("offset=%zu frames=%u header=%08"PRIX32" layer=%d sample_rate=%d "
           "bit_rate=%d crc=%d mode=%d mode_ext=%d copyright=%d original=%d "
           "emphasis=%d private_bit=%d\n",
           offset, frames, bits, header->layer, header->sample_rate,
           header->bit_rate, header->error_protection, header->mode,
           header->mode_ext, header->copyright, header->original,
           header->emphasis, header->private_bit);
}

int main(int argc, char *argv[])
{
    MPADecodeHeader2 run_header, *header = NULL;
    uint8_t *buf;
    size_t size, pos, run_offset = 0;
    uint32_t run_bits = 0;
    unsigned run_frames = 0, total_frames = 0;
    int ret = 1;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    if (av_file_map(argv[1], &buf, &size, 0, NULL) < 0) {
        fprintf(stderr, "Failed to open '%s'\n", argv[1]);
        return 1;
    }

    pos = id3v2_size(buf, size);
    while (pos + 4 <= size) {
        uint32_t bits = AV_RB32(buf + pos);
        uint32_t encoded;
        int decoded;

        if (ff_mpa_check_header(bits) < 0) {
            pos++;
            continue;
        }

        if (header)
            memset(header, 0, sizeof(*header));

        decoded = avpriv_mpegaudio_decode_header2(&header, bits);
        if (decoded < 0)
            goto end;

        encoded = ff_mpa_encode_header(header);
        if (encoded != bits) {
            printf("offset=%zu header=%08"PRIX32" encoded back as %08"PRIX32"\n",
                   pos, bits, encoded);
            goto end;
        }

        if (run_frames && (bits & ~PADDING_BIT) != (run_bits & ~PADDING_BIT)) {
            print_header(run_offset, run_frames, run_bits, &run_header);
            run_frames = 0;
        }
        if (!run_frames) {
            run_offset = pos;
            run_bits   = bits;
            run_header = *header;
        }
        run_frames++;
        total_frames++;

        pos += decoded == 0 ? header->frame_size : 1;
    }
    if (run_frames)
        print_header(run_offset, run_frames, run_bits, &run_header);

    printf("frames=%u\n", total_frames);
    ret = !total_frames;

end:
    av_freep(&header);
    av_file_unmap(buf, size);
    return ret;
}
