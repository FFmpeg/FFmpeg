/*
 * cached bitstream reader test
 * copyright (c) 2022 Anton Khirnov <anton@khirnov.net>
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

#define ASSERT_LEVEL 2

#include "libavutil/avassert.h"
#include "libavutil/lfg.h"
#include "libavutil/random_seed.h"

#include "libavcodec/bitstream.h"
#include "libavcodec/defs.h"

#ifdef BITSTREAM_LE
#define BITSTREAM_WRITER_LE
#endif
#include "libavcodec/put_bits.h"

#define SIZE 157

enum Op {
    OP_READ,
    OP_READ_NZ,
    OP_READ_BIT,
    OP_READ_63,
    OP_READ_64,
    OP_READ_SIGNED,
    OP_READ_SIGNED_NZ,
    OP_APPLY_SIGN,
    OP_ALIGN,
    OP_NB,
};

int main(int argc, char **argv)
{
    BitstreamContext bc;
    PutBitContext    pb;
    AVLFG            lfg;

    uint8_t buf[SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t dst[SIZE + AV_INPUT_BUFFER_PADDING_SIZE];

    uint32_t random_seed;
    uint64_t val, val1;
    int32_t  sval, sval1;
    unsigned count;

    /* generate random input, using a given or random seed */
    if (argc > 1)
        random_seed = strtoul(argv[1], NULL, 0);
    else
        random_seed = av_get_random_seed();

    fprintf(stderr, "Testing with LFG seed: %"PRIu32"\n", random_seed);
    av_lfg_init(&lfg, random_seed);

    for (unsigned i = 0; i < SIZE; i++)
        buf[i] = av_lfg_get(&lfg);

    bits_init8   (&bc, buf, SIZE);
    init_put_bits(&pb, dst, SIZE);

    /* use a random sequence of bitreading operations to transfer data
     * from BitstreamContext to PutBitContext */
    while (bits_left(&bc) > 0) {
        enum Op op = av_lfg_get(&lfg) % OP_NB;

        switch (op) {
        case OP_READ:
            count = av_lfg_get(&lfg) % FFMIN(33, bits_left(&bc) + 1);
            val1  = bits_peek(&bc, count);
            val   = bits_read(&bc, count);

            fprintf(stderr, "%d read %u: %"PRIu64"\n", bits_tell(&bc) - count, count, val);

            av_assert0(val == val1);

            put_bits64(&pb, count, val);
            break;
        case OP_READ_NZ:
            count = av_lfg_get(&lfg) % FFMIN(33, bits_left(&bc) + 1);
            count = FFMAX(count, 1);
            val1  = bits_peek_nz(&bc, count);
            val   = bits_read_nz(&bc, count);

            fprintf(stderr, "%d read_nz %u: %"PRIu64"\n", bits_tell(&bc) - count, count, val);

            av_assert0(val == val1);

            put_bits64(&pb, count, val);
            break;
        case OP_READ_BIT:
            val = bits_read_bit(&bc);

            fprintf(stderr, "%d read_bit: %"PRIu64"\n", bits_tell(&bc) - 1, val);

            put_bits(&pb, 1, val);
            break;
        case OP_READ_63:
            count = av_lfg_get(&lfg) % FFMIN(64, bits_left(&bc) + 1);
            val   = bits_read_63(&bc, count);

            fprintf(stderr, "%d read_63 %u: %"PRIu64"\n", bits_tell(&bc) - count, count, val);

            put_bits64(&pb, count, val);
            break;
        case OP_READ_64:
            count = av_lfg_get(&lfg) % FFMIN(65, bits_left(&bc) + 1);
            val   = bits_read_64(&bc, count);

            fprintf(stderr, "%d read_64 %u: %"PRIu64"\n", bits_tell(&bc) - count, count, val);

            put_bits64(&pb, count, val);
            break;
        case OP_READ_SIGNED:
            count = av_lfg_get(&lfg) % FFMIN(33, bits_left(&bc) + 1);
            sval1 = bits_peek_signed(&bc, count);
            sval  = bits_read_signed(&bc, count);

            fprintf(stderr, "%d read_signed %u: %"PRId32"\n", bits_tell(&bc) - count, count, sval);

            av_assert0(sval == sval1);

            if (count == 32) put_bits32(&pb, sval);
            else             put_sbits(&pb, count, sval);
            break;
        case OP_READ_SIGNED_NZ:
            count = av_lfg_get(&lfg) % FFMIN(33, bits_left(&bc) + 1);
            count = FFMAX(count, 1);
            sval1 = bits_peek_signed_nz(&bc, count);
            sval  = bits_read_signed_nz(&bc, count);

            fprintf(stderr, "%d read_signed_nz %u: %"PRId32"\n", bits_tell(&bc) - count, count, sval);

            av_assert0(sval == sval1);

            if (count == 32) put_bits32(&pb, sval);
            else             put_sbits(&pb, count, sval);
            break;
        case OP_ALIGN:
            count = (bits_tell(&bc) + 7) / 8 * 8 - bits_tell(&bc);

            fprintf(stderr, "%d align %u\n", bits_tell(&bc), count);

            put_bits(&pb, count, bits_peek(&bc, count));
            bits_align(&bc);
            break;
        case OP_APPLY_SIGN:
            if (bits_left(&bc) < 2)
                continue;

            count = av_lfg_get(&lfg) % FFMIN(32, bits_left(&bc));
            count = FFMAX(count, 1);

            if (!bits_peek(&bc, count))
                continue;

            val   = bits_read(&bc, count);
            sval  = bits_apply_sign(&bc, val);

            fprintf(stderr, "%d apply_sign %u %"PRId32"\n",
                    bits_tell(&bc) - count - 1, count, sval);

            put_bits64(&pb, count, FFABS(sval));
            put_bits(&pb, 1, sval < 0);

            break;
        default:
            av_assert0(0);
        }
    }

    flush_put_bits(&pb);

    for (unsigned i = 0; i < SIZE; i++)
        if (buf[i] != dst[i]) {
            fprintf(stderr, "Mismatch at byte %u: %hhu %hhu; seed %"PRIu32"\n",
                    i, buf[i], dst[i], random_seed);
            return 1;
        }

    return 0;
}
