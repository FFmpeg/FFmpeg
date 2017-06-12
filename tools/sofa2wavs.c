/*
 * Copyright (c) 2017 Paul B Mahol
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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <mysofa.h>

int main(int argc, char **argv)
{
    struct MYSOFA_HRTF *hrtf;
    int sample_rate;
    int err, i, j;

    if (argc < 3) {
        printf("usage: %s input_SOFA_file output_directory\n", argv[0]);
        return 1;
    }

    hrtf = mysofa_load(argv[1], &err);
    if (!hrtf || err) {
        printf("invalid input SOFA file: %s\n", argv[1]);
        return 1;
    }

    if (hrtf->DataSamplingRate.elements != 1)
        goto fail;
    sample_rate = hrtf->DataSamplingRate.values[0];

    err = mkdir(argv[2], 0744);
    if (err)
        goto fail;

    err = chdir(argv[2]);
    if (err)
        goto fail;

    for (i = 0; i < hrtf->M; i++) {
        FILE *file;
        int bps = 32;
        int blkalign = 8;
        int bytespersec = blkalign * sample_rate;
        char filename[1024];
        int azi = hrtf->SourcePosition.values[i * 3];
        int ele = hrtf->SourcePosition.values[i * 3 + 1];
        int dis = hrtf->SourcePosition.values[i * 3 + 2];
        int size = 8 * hrtf->N;
        int offset = i * 2 * hrtf->N;

        snprintf(filename, sizeof(filename), "azi_%d_ele_%d_dis_%d.wav", azi, ele, dis);
        file = fopen(filename, "w+");
        fwrite("RIFF", 4, 1, file);
        fwrite("\xFF\xFF\xFF\xFF", 4, 1, file);
        fwrite("WAVE", 4, 1, file);
        fwrite("fmt ", 4, 1, file);
        fwrite("\x10\x00\00\00", 4, 1, file);
        fwrite("\x03\x00", 2, 1, file);
        fwrite("\x02\x00", 2, 1, file);
        fwrite(&sample_rate, 4, 1, file);
        fwrite(&bytespersec, 4, 1, file);
        fwrite(&blkalign, 2, 1, file);
        fwrite(&bps, 2, 1, file);
        fwrite("data", 4, 1, file);
        fwrite(&size, 4, 1, file);

        for (j = 0; j < hrtf->N; j++) {
            float l, r;

            l = hrtf->DataIR.values[offset + j];
            r = hrtf->DataIR.values[offset + j + hrtf->N];
            fwrite(&l, 4, 1, file);
            fwrite(&r, 4, 1, file);
        }
        fclose(file);
    }

fail:
    mysofa_free(hrtf);

    return 0;
}
