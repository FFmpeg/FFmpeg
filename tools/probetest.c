/*
 * copyright (c) 2009 Michael Niedermayer <michaelni@gmx.at>
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

#include <stdlib.h>

#include "libavformat/avformat.h"
#include "libavcodec/put_bits.h"
#include "libavutil/lfg.h"

static int score_array[1000]; //this must be larger than the number of formats
static int failures = 0;

static void probe(AVProbeData *pd, int type, int p, int size)
{
    int i = 0;
    AVInputFormat *fmt = NULL;

    while ((fmt = av_iformat_next(fmt))) {
        if (fmt->flags & AVFMT_NOFILE)
            continue;
        if (fmt->read_probe) {
            int score = fmt->read_probe(pd);
            if (score > score_array[i] && score > AVPROBE_SCORE_MAX / 4) {
                score_array[i] = score;
                fprintf(stderr,
                        "Failure of %s probing code with score=%d type=%d p=%X size=%d\n",
                        fmt->name, score, type, p, size);
                failures++;
            }
        }
        i++;
    }
}

int main(int argc, char **argv)
{
    unsigned int p, i, type, size, retry;
    AVProbeData pd;
    AVLFG state;
    PutBitContext pb;
    int retry_count= 4097;
    int max_size = 65537;

    if(argc >= 2)
        retry_count = atoi(argv[1]);
    if(argc >= 3)
        max_size = atoi(argv[2]);

    avcodec_register_all();
    av_register_all();

    av_lfg_init(&state, 0xdeadbeef);

    pd.buf = NULL;
    for (size = 1; size < max_size; size *= 2) {
        pd.buf_size = size;
        pd.buf      = av_realloc(pd.buf, size + AVPROBE_PADDING_SIZE);
        pd.filename = "";

        fprintf(stderr, "testing size=%d\n", size);

        for (retry = 0; retry < retry_count; retry += FFMAX(size, 32)) {
            for (type = 0; type < 4; type++) {
                for (p = 0; p < 4096; p++) {
                    unsigned hist = 0;
                    init_put_bits(&pb, pd.buf, size);
                    switch (type) {
                    case 0:
                        for (i = 0; i < size * 8; i++)
                            put_bits(&pb, 1, (av_lfg_get(&state) & 0xFFFFFFFF) > p << 20);
                        break;
                    case 1:
                        for (i = 0; i < size * 8; i++) {
                            unsigned int p2 = hist ? p & 0x3F : (p >> 6);
                            unsigned int v  = (av_lfg_get(&state) & 0xFFFFFFFF) > p2 << 26;
                            put_bits(&pb, 1, v);
                            hist = v;
                        }
                        break;
                    case 2:
                        for (i = 0; i < size * 8; i++) {
                            unsigned int p2 = (p >> (hist * 3)) & 7;
                            unsigned int v  = (av_lfg_get(&state) & 0xFFFFFFFF) > p2 << 29;
                            put_bits(&pb, 1, v);
                            hist = (2 * hist + v) & 3;
                        }
                        break;
                    case 3:
                        for (i = 0; i < size; i++) {
                            int c = 0;
                            while (p & 63) {
                                c = (av_lfg_get(&state) & 0xFFFFFFFF) >> 24;
                                if (c >= 'a' && c <= 'z' && (p & 1))
                                    break;
                                else if (c >= 'A' && c <= 'Z' && (p & 2))
                                    break;
                                else if (c >= '0' && c <= '9' && (p & 4))
                                    break;
                                else if (c == ' ' && (p & 8))
                                    break;
                                else if (c == 0 && (p & 16))
                                    break;
                                else if (c == 1 && (p & 32))
                                    break;
                            }
                            pd.buf[i] = c;
                        }
                    }
                    flush_put_bits(&pb);
                    probe(&pd, type, p, size);
                }
            }
        }
    }
    return failures;
}
