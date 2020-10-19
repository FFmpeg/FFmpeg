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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFABS(a) ((a) >= 0 ? (a) : (-(a)))

static int64_t fsize(FILE *f) {
    int64_t end, pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);
    return end;
}

int main(int argc, char **argv) {
    FILE *f[2];
    int i, pos;
    int siglen, datlen;
    int bestpos = 0;
    double bestc = 0;
    double sigamp = 0;
    int16_t *signal, *data;
    int maxshift = 16384;

    if (argc < 3) {
        printf("audiomatch <testfile> <reffile>\n");
        printf("WAV headers are skipped automatically.\n");
        return 1;
    }

    f[0] = fopen(argv[1], "rb");
    f[1] = fopen(argv[2], "rb");
    if (!f[0] || !f[1]) {
        fprintf(stderr, "Could not open input files.\n");
        return 1;
    }

    for (i = 0; i < 2; i++) {
        uint8_t p[100];
        if (fread(p, 1, 12, f[i]) != 12)
            return 1;
        if (!memcmp(p, "RIFF", 4) &&
            !memcmp(p + 8, "WAVE", 4)) {
            if (fread(p, 1, 8, f[i]) != 8)
                return 1;
            while (memcmp(p, "data", 4)) {
                int s = p[4] | p[5] << 8 | p[6] << 16 | p[7] << 24;
                fseek(f[i], s, SEEK_CUR);
                if (fread(p, 1, 8, f[i]) != 8)
                    return 1;
            }
        } else {
            fseek(f[i], -12, SEEK_CUR);
        }
    }

    datlen = fsize(f[0]) - ftell(f[0]);
    siglen = fsize(f[1]) - ftell(f[1]);
    data   = malloc(datlen * sizeof(*data));
    signal = malloc(siglen * sizeof(*signal));

    if (fread(data  , 1, datlen, f[0]) != datlen)
        return 1;
    if (fread(signal, 1, siglen, f[1]) != siglen)
        return 1;
    datlen /= 2;
    siglen /= 2;

    for (i = 0; i < siglen; i++) {
        signal[i] = ((uint8_t*)(signal + i))[0] + 256*((uint8_t*)(signal + i))[1];
        sigamp += signal[i] * signal[i];
    }
    for (i = 0; i < datlen; i++)
        data[i] = ((uint8_t*)(data + i))[0] + 256*((uint8_t*)(data + i))[1];

    for (pos = 0; pos < maxshift; pos = pos < 0 ? -pos: -pos-1) {
        int64_t c = 0;
        int testlen = FFMIN(siglen, datlen-pos);
        for (i = FFMAX(0, -pos); i < testlen; i++) {
            int j = pos + i;
            c += signal[i] * data[j];
        }
        if (FFABS(c) > sigamp * 0.94)
            maxshift = FFMIN(maxshift, FFABS(pos)+32);
        if (FFABS(c) > FFABS(bestc)) {
            bestc = c;
            bestpos = pos;
        }
    }
    printf("presig: %d postsig:%d c:%7.4f lenerr:%d\n", bestpos, datlen - siglen - bestpos, bestc / sigamp, datlen - siglen);

    return 0;
}
